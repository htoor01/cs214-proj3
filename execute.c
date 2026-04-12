/*
 * execute.c — pipeline execution, path resolution, child management
 *
 * CS 214 Spring 2026 — Project III
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <limits.h>
#include <errno.h>

#include "mysh.h"

/* ──────────────────────────────────────────────
 * err_write
 *
 * Replacement for perror(): writes "label: strerror(errno)\n" to
 * stderr using only write() — no buffered stdio.
 * ────────────────────────────────────────────── */
static void err_write(const char *label) {
    const char *msg = strerror(errno);
    write(STDERR_FILENO, label, strlen(label));
    write(STDERR_FILENO, ": ", 2);
    write(STDERR_FILENO, msg, strlen(msg));
    write(STDERR_FILENO, "\n", 1);
}

/* ──────────────────────────────────────────────
 * resolve_path
 *
 * Search /usr/local/bin, /usr/bin, /bin for `name`.
 * On success, write the full path into resolved_path and return 1.
 * Return 0 if not found.
 * ────────────────────────────────────────────── */
int resolve_path(const char *name, char *resolved_path) {
    for (int i = 0; SEARCH_DIRS[i] != NULL; i++) {
        snprintf(resolved_path, PATH_MAX, "%s/%s", SEARCH_DIRS[i], name);
        if (access(resolved_path, X_OK) == 0) {
            return 1;
        }
    }
    return 0; /* not found */
}

/* ──────────────────────────────────────────────
 * setup_child_io
 *
 * Called inside the child process (after fork) to configure stdin/stdout
 * before exec.  Handles:
 *   - file redirection (< and >)
 *   - pipe ends passed from the parent
 *   - /dev/null as stdin in batch mode, or in interactive mode when the
 *     command's output goes to a file or pipe (spec §1: children inherit
 *     the terminal "except when the command specifies output redirection
 *     or a pipe")
 *
 * read_fd  : read end of the upstream pipe (-1 if none)
 * write_fd : write end of the downstream pipe (-1 if none)
 * interactive: 0 → batch mode
 *
 * Returns 0 on success, -1 on error (child should exit).
 * ────────────────────────────────────────────── */
static int setup_child_io(const Command *cmd,
                          int read_fd, int write_fd,
                          int interactive) {
    /* ── Standard input ──────────────────────────────────────────────── */
    if (cmd->input_file != NULL) {
        /* Explicit < redirection */
        int fd = open(cmd->input_file, O_RDONLY);
        if (fd < 0) { err_write(cmd->input_file); return -1; }
        if (dup2(fd, STDIN_FILENO) < 0) { err_write("dup2"); close(fd); return -1; }
        close(fd);
    } else if (read_fd >= 0) {
        /* Read end of an upstream pipe */
        if (dup2(read_fd, STDIN_FILENO) < 0) { err_write("dup2"); return -1; }
    } else if (!interactive || cmd->output_file != NULL || write_fd >= 0) {
        /*
         * Use /dev/null for stdin when:
         *   - batch mode (always), OR
         *   - interactive mode but output goes to a file (> redirect) or
         *     pipe, per spec: children inherit terminal stdin "except when
         *     the command specifies output redirection or a pipe."
         */
        int fd = open("/dev/null", O_RDONLY);
        if (fd < 0) { err_write("/dev/null"); return -1; }
        if (dup2(fd, STDIN_FILENO) < 0) { err_write("dup2"); close(fd); return -1; }
        close(fd);
    }
    /* else: interactive, no output redirect, no pipe → inherit terminal stdin */

    /* ── Standard output ─────────────────────────────────────────────── */
    if (cmd->output_file != NULL) {
        /* Explicit > redirection: create or truncate, mode 0640 */
        int fd = open(cmd->output_file,
                      O_WRONLY | O_CREAT | O_TRUNC,
                      S_IRUSR | S_IWUSR | S_IRGRP);
        if (fd < 0) { err_write(cmd->output_file); return -1; }
        if (dup2(fd, STDOUT_FILENO) < 0) { err_write("dup2"); close(fd); return -1; }
        close(fd);
    } else if (write_fd >= 0) {
        /* Write end of a downstream pipe */
        if (dup2(write_fd, STDOUT_FILENO) < 0) { err_write("dup2"); return -1; }
    }

    /* Close pipe ends that were passed in (both are duplicated above) */
    if (read_fd  >= 0) close(read_fd);
    if (write_fd >= 0) close(write_fd);

    return 0;
}

/* ──────────────────────────────────────────────
 * execute_single
 *
 * Fork and exec one sub-command.  Pipe file descriptors are provided
 * by the caller; pass -1 when a side is not connected to a pipe.
 *
 * should_exit   : set to 1 if a parent-executed "exit" built-in ran.
 * builtin_result: set to the built-in's return value (0=ok, 1=fail)
 *                 when the built-in runs in the parent (return == -2).
 *
 * Returns:
 *   pid > 0  child was forked successfully
 *   -1       fork failed (read_fd/write_fd are already closed)
 *   -2       built-in ran in parent (no child to wait for)
 * ────────────────────────────────────────────── */
static pid_t execute_single(const Command *cmd,
                             int read_fd, int write_fd,
                             int interactive,
                             int *should_exit,
                             int *builtin_result) {
    *builtin_result = 0;

    /* ── Non-piped built-in: run directly in parent ───────────────────── */
    if (is_builtin(cmd->argv[0]) && read_fd < 0 && write_fd < 0) {
        int out_fd    = STDOUT_FILENO;
        int opened_fd = -1;

        /* Handle > output redirection for the built-in */
        if (cmd->output_file != NULL) {
            opened_fd = open(cmd->output_file,
                             O_WRONLY | O_CREAT | O_TRUNC,
                             S_IRUSR | S_IWUSR | S_IRGRP);
            if (opened_fd < 0) {
                err_write(cmd->output_file);
                *builtin_result = 1;
                return -2;
            }
            out_fd = opened_fd;
        }

        int se  = 0;
        int ret = run_builtin(cmd, out_fd, &se);
        if (opened_fd >= 0) close(opened_fd);

        if (se) *should_exit = 1;
        *builtin_result = (ret != 0) ? 1 : 0;
        return -2; /* sentinel: no child to wait for */
    }

    /* ── Forked execution (external program or piped built-in) ─────────── */
    pid_t pid = fork();
    if (pid < 0) {
        err_write("fork");
        /* Close any pipe fds we were handed so they don't leak */
        if (read_fd  >= 0) close(read_fd);
        if (write_fd >= 0) close(write_fd);
        return -1;
    }

    if (pid == 0) {
        /* ── Child process ─────────────────────────────────────────────── */
        if (setup_child_io(cmd, read_fd, write_fd, interactive) < 0) {
            exit(EXIT_FAILURE);
        }

        if (is_builtin(cmd->argv[0])) {
            /* Built-in inside a pipe: run and exit.
             * should_exit from run_builtin is ignored — the child exits
             * regardless; the parent detects "exit" via the pre-scan. */
            int dummy_se = 0;
            int ret = run_builtin(cmd, STDOUT_FILENO, &dummy_se);
            exit(ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
        }

        /* Resolve program path */
        char exec_path[PATH_MAX];
        if (strchr(cmd->argv[0], '/') != NULL) {
            /* Explicit path */
            strncpy(exec_path, cmd->argv[0], PATH_MAX - 1);
            exec_path[PATH_MAX - 1] = '\0';
        } else {
            if (!resolve_path(cmd->argv[0], exec_path)) {
                write(STDERR_FILENO, cmd->argv[0], strlen(cmd->argv[0]));
                write(STDERR_FILENO, ": command not found\n", 20);
                exit(EXIT_FAILURE);
            }
        }

        execv(exec_path, cmd->argv);
        /* execv only returns on error */
        err_write(exec_path);
        exit(EXIT_FAILURE);
    }

    /* ── Parent: close pipe ends that belong to this child ──────────── */
    if (read_fd  >= 0) close(read_fd);
    if (write_fd >= 0) close(write_fd);

    return pid;
}

/* ──────────────────────────────────────────────
 * execute_pipeline
 *
 * Orchestrate the execution of all sub-commands in a pipeline,
 * creating OS pipes between adjacent sub-commands.
 *
 * should_exit: set to 1 if a parent-executed "exit" built-in ran.
 *
 * Returns the raw waitpid() status for the last sub-command, or a
 * synthetic failure value on setup error.
 * ────────────────────────────────────────────── */
int execute_pipeline(const Pipeline *pipeline, int interactive, int *should_exit) {
    int   n    = pipeline->num_commands;
    pid_t pids[MAX_COMMANDS];
    int   builtin_results[MAX_COMMANDS];

    memset(pids,           -1, sizeof(pids));
    memset(builtin_results, 0, sizeof(builtin_results));

    int prev_read = -1;  /* read end of the pipe from the previous stage */

    for (int i = 0; i < n; i++) {
        int pipe_fds[2] = {-1, -1};

        if (i < n - 1) {
            /* There is a next stage: create a pipe */
            if (pipe(pipe_fds) < 0) {
                err_write("pipe");
                for (int j = 0; j < i; j++) {
                    if (pids[j] > 0) waitpid(pids[j], NULL, 0);
                }
                if (prev_read >= 0) close(prev_read);
                return -1;
            }
        }

        pids[i] = execute_single(&pipeline->commands[i],
                                 prev_read,
                                 (i < n - 1) ? pipe_fds[1] : -1,
                                 interactive,
                                 should_exit,
                                 &builtin_results[i]);

        /*
         * For forked children (pids[i] > 0), execute_single already
         * closed read_fd and write_fd in the parent.
         * For the builtin-in-parent case (-2), read_fd and write_fd were
         * both -1 (the in-parent path requires no pipe), so nothing to close.
         * For fork failure (-1), execute_single closes them before returning.
         */

        prev_read = pipe_fds[0]; /* read end becomes input for next stage */

        if (pids[i] == -1) {
            /* Fork failed — clean up the remaining read end and bail */
            if (prev_read >= 0) { close(prev_read); prev_read = -1; }
            break;
        }
    }

    /* Close the last read end if it was never consumed */
    if (prev_read >= 0) close(prev_read);

    /* Wait for all children; capture the last child's status */
    int last_status = 0;
    for (int i = 0; i < n; i++) {
        if (pids[i] == -2) {
            /* Built-in ran in parent — synthesise a waitpid-style status */
            if (i == n - 1) {
                last_status = builtin_results[i] ? (1 << 8) : 0;
            }
            continue;
        }
        if (pids[i] <= 0) continue;

        int status;
        if (waitpid(pids[i], &status, 0) < 0) {
            err_write("waitpid");
            continue;
        }
        if (i == n - 1) {
            last_status = status;
        }
    }

    return last_status;
}
