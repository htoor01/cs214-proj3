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

static int make_exit_status(int exit_code) {
    return (exit_code & 0xff) << 8;
}

static void wait_for_started_children(const pid_t *pids, int count) {
    for (int i = 0; i < count; i++) {
        if (pids[i] <= 0) {
            continue;
        }

        int status;
        while (waitpid(pids[i], &status, 0) < 0) {
            if (errno != EINTR) {
                perror("waitpid");
                break;
            }
        }
    }
}

static int execute_builtin_parent(const Command *cmd) {
    int saved_stdin  = -1;
    int saved_stdout = -1;
    int input_fd     = -1;
    int output_fd    = -1;
    int should_exit  = 0;
    int ret          = -1;

    if (cmd->input_file != NULL) {
        input_fd = open(cmd->input_file, O_RDONLY);
        if (input_fd < 0) {
            perror(cmd->input_file);
            goto cleanup;
        }

        saved_stdin = dup(STDIN_FILENO);
        if (saved_stdin < 0) {
            perror("dup");
            goto cleanup;
        }
        if (dup2(input_fd, STDIN_FILENO) < 0) {
            perror("dup2");
            goto cleanup;
        }
        close(input_fd);
        input_fd = -1;
    }

    if (cmd->output_file != NULL) {
        output_fd = open(cmd->output_file,
                         O_WRONLY | O_CREAT | O_TRUNC,
                         S_IRUSR | S_IWUSR | S_IRGRP);
        if (output_fd < 0) {
            perror(cmd->output_file);
            goto cleanup;
        }

        saved_stdout = dup(STDOUT_FILENO);
        if (saved_stdout < 0) {
            perror("dup");
            goto cleanup;
        }
        if (dup2(output_fd, STDOUT_FILENO) < 0) {
            perror("dup2");
            goto cleanup;
        }
        close(output_fd);
        output_fd = -1;
    }

    ret = run_builtin(cmd, STDOUT_FILENO, &should_exit);

cleanup:
    if (output_fd >= 0) {
        close(output_fd);
    }
    if (input_fd >= 0) {
        close(input_fd);
    }

    if (saved_stdout >= 0) {
        if (dup2(saved_stdout, STDOUT_FILENO) < 0) {
            perror("dup2");
        }
        close(saved_stdout);
    }
    if (saved_stdin >= 0) {
        if (dup2(saved_stdin, STDIN_FILENO) < 0) {
            perror("dup2");
        }
        close(saved_stdin);
    }

    return (ret == 0) ? make_exit_status(EXIT_SUCCESS)
                      : make_exit_status(EXIT_FAILURE);
}

/* ──────────────────────────────────────────────
 * resolve_path
 *
 * Search /usr/local/bin, /usr/bin, /bin for `name`.
 * On success, write the full path into resolved_path and return 1.
 * Return 0 if not found.
 * ────────────────────────────────────────────── */
int resolve_path(const char *name, char *resolved_path) {
    if (name == NULL || *name == '\0' || resolved_path == NULL) {
        return 0;
    }

    for (int i = 0; SEARCH_DIRS[i] != NULL; i++) {
        int len = snprintf(resolved_path, PATH_MAX, "%s/%s", SEARCH_DIRS[i], name);
        if (len < 0 || len >= PATH_MAX) {
            continue;
        }
        if (access(resolved_path, X_OK) == 0) {
            return 1;
        }
    }

    return 0;
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
    if (cmd->input_file != NULL) {
        int fd = open(cmd->input_file, O_RDONLY);
        if (fd < 0) {
            perror(cmd->input_file);
            return -1;
        }
        if (dup2(fd, STDIN_FILENO) < 0) {
            perror("dup2");
            close(fd);
            return -1;
        }
        close(fd);
    } else if (read_fd >= 0) {
        if (dup2(read_fd, STDIN_FILENO) < 0) {
            perror("dup2");
            return -1;
        }
    } else if (!interactive) {
        int fd = open("/dev/null", O_RDONLY);
        if (fd < 0) {
            perror("/dev/null");
            return -1;
        }
        if (dup2(fd, STDIN_FILENO) < 0) {
            perror("dup2");
            close(fd);
            return -1;
        }
        close(fd);
    }

    if (cmd->output_file != NULL) {
        int fd = open(cmd->output_file,
                      O_WRONLY | O_CREAT | O_TRUNC,
                      S_IRUSR | S_IWUSR | S_IRGRP);
        if (fd < 0) {
            perror(cmd->output_file);
            return -1;
        }
        if (dup2(fd, STDOUT_FILENO) < 0) {
            perror("dup2");
            close(fd);
            return -1;
        }
        close(fd);
    } else if (write_fd >= 0) {
        if (dup2(write_fd, STDOUT_FILENO) < 0) {
            perror("dup2");
            return -1;
        }
    }

    if (read_fd >= 0) {
        close(read_fd);
    }
    if (write_fd >= 0) {
        close(write_fd);
    }

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
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        if (setup_child_io(cmd, read_fd, write_fd, interactive) < 0) {
            _exit(EXIT_FAILURE);
        }

        if (is_builtin(cmd->argv[0])) {
            int should_exit = 0;
            int ret = run_builtin(cmd, STDOUT_FILENO, &should_exit);
            _exit(ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
        }

        char exec_path[PATH_MAX];
        if (strchr(cmd->argv[0], '/') != NULL) {
            strncpy(exec_path, cmd->argv[0], PATH_MAX - 1);
            exec_path[PATH_MAX - 1] = '\0';
        } else {
            if (!resolve_path(cmd->argv[0], exec_path)) {
                fprintf(stderr, "%s: command not found\n", cmd->argv[0]);
                _exit(EXIT_FAILURE);
            }
        }

        execv(exec_path, cmd->argv);
        perror(exec_path);
        _exit(EXIT_FAILURE);
    }

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
    int n = pipeline->num_commands;
    if (n <= 0) {
        return make_exit_status(EXIT_FAILURE);
    }

    if (n == 1 && pipeline->commands[0].argc > 0 &&
        is_builtin(pipeline->commands[0].argv[0])) {
        return execute_builtin_parent(&pipeline->commands[0]);
    }

    pid_t pids[MAX_COMMANDS];
    for (int i = 0; i < MAX_COMMANDS; i++) {
        pids[i] = -1;
    }

    int prev_read = -1;

    for (int i = 0; i < n; i++) {
        int pipe_fds[2] = {-1, -1};

        if (i < n - 1) {
            if (pipe(pipe_fds) < 0) {
                perror("pipe");
                if (prev_read >= 0) {
                    close(prev_read);
                }
                wait_for_started_children(pids, i);
                return make_exit_status(EXIT_FAILURE);
            }
        }

        pids[i] = execute_single(&pipeline->commands[i],
                                 prev_read,
                                 (i < n - 1) ? pipe_fds[1] : -1,
                                 interactive);

        if (pipe_fds[1] >= 0) {
            close(pipe_fds[1]);
        }
        if (prev_read >= 0) {
            close(prev_read);
        }

        prev_read = pipe_fds[0];

        if (pids[i] < 0) {
            if (prev_read >= 0) {
                close(prev_read);
            }
            wait_for_started_children(pids, i);
            return make_exit_status(EXIT_FAILURE);
        }
    }

    if (prev_read >= 0) {
        close(prev_read);
    }

    int last_status = make_exit_status(EXIT_SUCCESS);
    for (int i = 0; i < n; i++) {
        if (pids[i] <= 0) {
            continue;
        }

        int status = make_exit_status(EXIT_FAILURE);
        while (waitpid(pids[i], &status, 0) < 0) {
            if (errno != EINTR) {
                perror("waitpid");
                status = make_exit_status(EXIT_FAILURE);
                break;
            }
        }

        if (i == n - 1) {
            last_status = status;
        }
    }

    return last_status;
}
