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

/*
 * err_write
 *
 * Same idea as perror() but uses write() instead of stdio.
 * Prints "label: strerror\n" to stderr.
*/
static void err_write(const char *label) {
    const char *msg = strerror(errno);
    write(STDERR_FILENO, label, strlen(label));
    write(STDERR_FILENO, ": ", 2);
    write(STDERR_FILENO, msg, strlen(msg));
    write(STDERR_FILENO, "\n", 1);
}

/*
 * resolve_path
 *
 * Searches /usr/local/bin, /usr/bin, /bin for an executable named `name`.
 * We use access(X_OK) to check if the file exists and is executable without
 * having to open it or walk the directory ourselves.
 *
 * Returns 1 and fills resolved_path if found, 0 otherwise.
*/
int resolve_path(const char *name, char *resolved_path) {
    for (int i = 0; SEARCH_DIRS[i] != NULL; i++) {
        snprintf(resolved_path, PATH_MAX, "%s/%s", SEARCH_DIRS[i], name);
        if (access(resolved_path, X_OK) == 0) {
            return 1;
        }
    }
    return 0;
}

/*
 * setup_child_io
 *
 * Called inside the child after fork(), before execv(). Sets up stdin and
 * stdout by dup2-ing the right file descriptors into place.
 *
 * The logic for stdin goes in priority order:
 *   1. explicit < redirect — open the file and dup2 it onto stdin
 *   2. upstream pipe (read_fd >= 0) — dup2 the pipe read end onto stdin
 *   3. batch mode, or interactive with output going to a file/pipe — use /dev/null
 *      (spec says children only inherit the terminal when there's no output
 *       redirect and no pipe)
 *   4. plain interactive command — just inherit the terminal, do nothing
 *
 * Same idea for stdout: explicit > beats a downstream pipe, otherwise leave it.
 *
 * Returns 0 on success, -1 if any open/dup2 fails (child should exit).
*/
static int setup_child_io(const Command *cmd,
                          int read_fd, int write_fd,
                          int interactive) {
    // stdin setup
    if (cmd->input_file != NULL) {
        int fd = open(cmd->input_file, O_RDONLY);
        if (fd < 0) { err_write(cmd->input_file); return -1; }
        if (dup2(fd, STDIN_FILENO) < 0) { err_write("dup2"); close(fd); return -1; }
        close(fd);
    } else if (read_fd >= 0) {
        // stdin comes from the upstream pipe
        if (dup2(read_fd, STDIN_FILENO) < 0) { err_write("dup2"); return -1; }
    } else if (!interactive || cmd->output_file != NULL || write_fd >= 0) {
        // batch mode always gets /dev/null; in interactive mode we also use
        // /dev/null if output is being redirected or piped somewhere
        int fd = open("/dev/null", O_RDONLY);
        if (fd < 0) { err_write("/dev/null"); return -1; }
        if (dup2(fd, STDIN_FILENO) < 0) { err_write("dup2"); close(fd); return -1; }
        close(fd);
    }

    // stdout setup
    if (cmd->output_file != NULL) {
        // create or truncate the file, mode 0640 per spec
        int fd = open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC,
                      S_IRUSR | S_IWUSR | S_IRGRP);
        if (fd < 0) { err_write(cmd->output_file); return -1; }
        if (dup2(fd, STDOUT_FILENO) < 0) { err_write("dup2"); close(fd); return -1; }
        close(fd);
    } else if (write_fd >= 0) {
        // stdout goes into the downstream pipe
        if (dup2(write_fd, STDOUT_FILENO) < 0) { err_write("dup2"); return -1; }
    }

    // close the raw pipe ends now that they've been dup2'd into place
    // if we leave them open the pipe won't get EOF when the writer exits
    if (read_fd  >= 0) close(read_fd);
    if (write_fd >= 0) close(write_fd);

    return 0;
}

/*
 * execute_single
 *
 * Runs one sub-command. Either forks a child and execs, or for non-piped
 * builtins, runs directly in the parent (cd has to run in the parent or
 * the chdir won't stick).
 *
 * should_exit   — set to 1 if a parent-executed exit builtin ran
 * builtin_result — set to the builtin's return value so the caller knows
 *                  whether it succeeded, since there's no child to waitpid on
 *
 * Returns:
 *   pid > 0 — child was forked, call waitpid on it
 *      -1   — fork failed (fds already closed)
 *      -2   — builtin ran in parent, no child to wait for
*/
static pid_t execute_single(const Command *cmd,
                             int read_fd, int write_fd,
                             int interactive,
                             int *should_exit,
                             int *builtin_result) {
    *builtin_result = 0;

    // builtins only run in the parent when they're not part of a pipe
    // if they're piped (pwd | cat) we fork them like anything else
    if (is_builtin(cmd->argv[0]) && read_fd < 0 && write_fd < 0) {
        int out_fd    = STDOUT_FILENO;
        int opened_fd = -1;

        // handle > redirect for the builtin (e.g. pwd > file)
        if (cmd->output_file != NULL) {
            opened_fd = open(cmd->output_file, O_WRONLY | O_CREAT | O_TRUNC,
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

        if (se) *should_exit = 1; /* exit builtin ran */
        *builtin_result = (ret != 0) ? 1 : 0;
        return -2;
    }

    pid_t pid = fork();
    if (pid < 0) {
        err_write("fork");
        // close the fds we were handed so they don't leak
        if (read_fd  >= 0) close(read_fd);
        if (write_fd >= 0) close(write_fd);
        return -1;
    }

    if (pid == 0) {
        // we're in the child now — wire up stdin/stdout then exec
        if (setup_child_io(cmd, read_fd, write_fd, interactive) < 0) {
            exit(EXIT_FAILURE);
        }

        if (is_builtin(cmd->argv[0])) {
            // builtin inside a pipe — run it and exit, the parent already
            // detected "exit" via the pre-scan so we don't need to propagate
            int dummy = 0;
            int ret = run_builtin(cmd, STDOUT_FILENO, &dummy);
            exit(ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
        }

        // figure out the full path to the executable
        char exec_path[PATH_MAX];
        if (strchr(cmd->argv[0], '/') != NULL) {
            // already a path like ./foo or /bin/ls — use it directly
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
        // execv only returns if something went wrong
        err_write(exec_path);
        exit(EXIT_FAILURE);
    }

    // parent closes its copies of the pipe ends — the child has them now
    // if we don't close them, the pipe never gets EOF
    if (read_fd  >= 0) close(read_fd);
    if (write_fd >= 0) close(write_fd);

    return pid;
}

/*
 * execute_pipeline
 *
 * Runs all sub-commands in the pipeline, wiring pipes between adjacent ones.
 * For each stage we create a pipe, hand the write end to the current child
 * and the read end to the next child, then close our copies in the parent.
 *
 * should_exit — set to 1 if a parent-executed exit builtin ran
 *
 * Returns the raw waitpid status of the last sub-command.
*/
int execute_pipeline(const Pipeline *pipeline, int interactive, int *should_exit) {
    int   n = pipeline->num_commands;
    pid_t pids[MAX_COMMANDS];
    int   builtin_results[MAX_COMMANDS];

    memset(pids,           -1, sizeof(pids));
    memset(builtin_results, 0, sizeof(builtin_results));

    int prev_read = -1; /* read end of the pipe from the previous stage */

    for (int i = 0; i < n; i++) {
        int pipe_fds[2] = {-1, -1};

        // only create a pipe if there's a next stage to connect to
        if (i < n - 1) {
            if (pipe(pipe_fds) < 0) {
                err_write("pipe");
                // clean up any children we already started before bailing
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

        // execute_single closes read_fd and write_fd for us in all cases
        // (inside the child via setup_child_io, in the parent after fork,
        // or before returning -1 on fork failure)

        prev_read = pipe_fds[0]; /* save read end for the next stage */

        if (pids[i] == -1) {
            // fork failed — clean up the pipe read end we just saved
            if (prev_read >= 0) { close(prev_read); prev_read = -1; }
            break;
        }
    }

    if (prev_read >= 0) close(prev_read);

    // wait for everyone; we only care about the last child's exit status
    int last_status = 0;
    for (int i = 0; i < n; i++) {
        if (pids[i] == -2) {
            // builtin ran in parent — synthesise a waitpid-style status value
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
