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

#include "mysh.h"

/* ──────────────────────────────────────────────
 * resolve_path
 *
 * Search /usr/local/bin, /usr/bin, /bin for `name`.
 * On success, write the full path into resolved_path and return 1.
 * Return 0 if not found.
 * ────────────────────────────────────────────── */
int resolve_path(const char *name, char *resolved_path) {
    (void)name;          /* TODO: remove once implemented */
    (void)resolved_path; /* TODO: remove once implemented */
    for (int i = 0; SEARCH_DIRS[i] != NULL; i++) {
        /* TODO: snprintf(resolved_path, PATH_MAX, "%s/%s", SEARCH_DIRS[i], name) */
        /* TODO: use access(resolved_path, X_OK) to test executability           */
        /* TODO: return 1 if executable found                                     */
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
 *   - /dev/null as default stdin in batch mode
 *
 * read_fd  : read end of the upstream pipe (-1 if none)
 * write_fd : write end of the downstream pipe (-1 if none)
 * interactive: 0 → use /dev/null for stdin when no explicit < redirection
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
        if (fd < 0) {
            perror(cmd->input_file);
            return -1;
        }
        /* TODO: dup2(fd, STDIN_FILENO); close(fd); */
    } else if (read_fd >= 0) {
        /* Read end of an upstream pipe */
        /* TODO: dup2(read_fd, STDIN_FILENO); */
    } else if (!interactive) {
        /* Batch mode default: /dev/null */
        int fd = open("/dev/null", O_RDONLY);
        if (fd < 0) {
            perror("/dev/null");
            return -1;
        }
        /* TODO: dup2(fd, STDIN_FILENO); close(fd); */
    }
    /* else: interactive with no redirection — inherit parent's stdin */

    /* ── Standard output ─────────────────────────────────────────────── */
    if (cmd->output_file != NULL) {
        /* Explicit > redirection: create or truncate */
        int fd = open(cmd->output_file,
                      O_WRONLY | O_CREAT | O_TRUNC,
                      S_IRUSR | S_IWUSR | S_IRGRP);  /* mode 0640 */
        if (fd < 0) {
            perror(cmd->output_file);
            return -1;
        }
        /* TODO: dup2(fd, STDOUT_FILENO); close(fd); */
    } else if (write_fd >= 0) {
        /* Write end of a downstream pipe */
        /* TODO: dup2(write_fd, STDOUT_FILENO); */
    }

    /* Close pipe ends that were passed in (both are duplicated now) */
    /* TODO: if (read_fd  >= 0) close(read_fd);  */
    /* TODO: if (write_fd >= 0) close(write_fd); */

    return 0;
}

/* ──────────────────────────────────────────────
 * execute_single
 *
 * Fork and exec one sub-command.  Pipe file descriptors are provided
 * by the caller; pass -1 when a side is not connected to a pipe.
 *
 * Returns the child's pid on success, -1 on fork/exec failure.
 * ────────────────────────────────────────────── */
static pid_t execute_single(const Command *cmd,
                             int read_fd, int write_fd,
                             int interactive) {
    /* Handle built-ins inline (no fork needed for non-piped built-ins;
     * for piped built-ins the caller decides — for now we always fork). */
    if (is_builtin(cmd->argv[0]) && write_fd < 0 && read_fd < 0) {
        /* TODO: run_builtin() directly (with possible output redirection) */
        /* TODO: return a sentinel value indicating "no child to wait for" */
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        /* ── Child process ─────────────────────────────────────── */
        if (setup_child_io(cmd, read_fd, write_fd, interactive) < 0) {
            exit(EXIT_FAILURE);
        }

        /* Close all inherited pipe ends in child */
        /* (Parent is responsible for closing its copies too.) */

        if (is_builtin(cmd->argv[0])) {
            /* Built-in inside a pipe: run and exit */
            int dummy;
            int ret = run_builtin(cmd, STDOUT_FILENO, &dummy);
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
                fprintf(stderr, "%s: command not found\n", cmd->argv[0]);
                exit(EXIT_FAILURE);
            }
        }

        execv(exec_path, cmd->argv);
        /* execv only returns on error */
        perror(exec_path);
        exit(EXIT_FAILURE);
    }

    /* ── Parent: close pipe ends that belong to this child ──────── */
    /* TODO: if (read_fd  >= 0) close(read_fd);  */
    /* TODO: if (write_fd >= 0) close(write_fd); */

    return pid;
}

/* ──────────────────────────────────────────────
 * execute_pipeline
 *
 * Orchestrate the execution of all sub-commands in a pipeline,
 * creating OS pipes between adjacent sub-commands.
 *
 * Returns the raw status from waitpid() for the last sub-command,
 * or a synthetic failure value on setup error.
 * ────────────────────────────────────────────── */
int execute_pipeline(const Pipeline *pipeline, int interactive) {
    int   n    = pipeline->num_commands;
    pid_t pids[MAX_COMMANDS];
    memset(pids, -1, sizeof(pids));

    int prev_read = -1;  /* read end of the pipe from the previous stage */

    for (int i = 0; i < n; i++) {
        int pipe_fds[2] = {-1, -1};

        if (i < n - 1) {
            /* There is a next stage: create a pipe */
            if (pipe(pipe_fds) < 0) {
                perror("pipe");
                /* TODO: clean up already-started children */
                return -1;
            }
        }

        /* read_fd  = read end of the previous pipe (or -1 for first stage)
         * write_fd = write end of the new pipe     (or -1 for last stage)  */
        pids[i] = execute_single(&pipeline->commands[i],
                                 prev_read,
                                 (i < n - 1) ? pipe_fds[1] : -1,
                                 interactive);

        /* Parent closes the write end it handed to the child */
        if (pipe_fds[1] >= 0) close(pipe_fds[1]);
        /* Parent closes the read end of the *previous* pipe (child has it now) */
        if (prev_read   >= 0) close(prev_read);

        prev_read = pipe_fds[0]; /* read end becomes input for next stage */

        if (pids[i] < 0) {
            /* Fork failed — TODO: decide how to handle partial pipeline */
            if (prev_read >= 0) close(prev_read);
            break;
        }
    }

    /* Wait for all children; capture last child's status */
    int last_status = 0;
    for (int i = 0; i < n; i++) {
        if (pids[i] <= 0) continue;
        int status;
        if (waitpid(pids[i], &status, 0) < 0) {
            perror("waitpid");
            continue;
        }
        if (i == n - 1) {
            last_status = status;
        }
    }

    return last_status;
}
