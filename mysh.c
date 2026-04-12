/*
 * mysh.c — entry point and main read-eval loop
 *
 * CS 214 Spring 2026 — Project III
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

#include "mysh.h"

/* ──────────────────────────────────────────────
 * Prompt helpers
 * ────────────────────────────────────────────── */

void print_prompt(void) {
    char cwd[4096];
    char prompt[4096 + 4]; /* room for "~", path, "$ ", NUL */

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        write(STDOUT_FILENO, "$ ", 2);
        return;
    }

    const char *home     = getenv("HOME");
    size_t      home_len = (home != NULL) ? strlen(home) : 0;

    if (home != NULL && home_len > 0 &&
        strncmp(cwd, home, home_len) == 0 &&
        (cwd[home_len] == '/' || cwd[home_len] == '\0')) {
        snprintf(prompt, sizeof(prompt), "~%s$ ", cwd + home_len);
    } else {
        snprintf(prompt, sizeof(prompt), "%s$ ", cwd);
    }

    write(STDOUT_FILENO, prompt, strlen(prompt));
}

void print_status(int status) {
    char msg[256];
    int  len;

    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        len = snprintf(msg, sizeof(msg),
                       "Exited with status %d\n", WEXITSTATUS(status));
        write(STDOUT_FILENO, msg, (size_t)len);
    } else if (WIFSIGNALED(status)) {
        len = snprintf(msg, sizeof(msg),
                       "Terminated by signal %d: %s\n",
                       WTERMSIG(status), strsignal(WTERMSIG(status)));
        write(STDOUT_FILENO, msg, (size_t)len);
    }
    /* exit code 0: print nothing */
}

/* ──────────────────────────────────────────────
 * Main shell loop
 * ────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    int input_fd;
    int interactive = 0;

    /* ── Determine input source ───────────────────────────────────────── */
    if (argc == 1) {
        input_fd    = STDIN_FILENO;
        interactive = isatty(STDIN_FILENO);
    } else if (argc == 2) {
        input_fd = open(argv[1], O_RDONLY);
        if (input_fd < 0) {
            perror(argv[1]);
            return EXIT_FAILURE;
        }
        interactive = 0;
    } else {
        write(STDERR_FILENO, "Usage: ", 7);
        write(STDERR_FILENO, argv[0], strlen(argv[0]));
        write(STDERR_FILENO, " [script]\n", 10);
        return EXIT_FAILURE;
    }

    /* ── Welcome message (interactive only) ──────────────────────────── */
    if (interactive) {
        const char *welcome = "Welcome to my shell!\n";
        write(STDOUT_FILENO, welcome, strlen(welcome));
    }

    /* ── Main read-eval loop ─────────────────────────────────────────── */
    char    line_buf[BUF_SIZE];
    ssize_t n;
    int     should_exit = 0;

    while (!should_exit) {
        if (interactive) {
            print_prompt();
        }

        /* Read one complete line */
        n = read_line(input_fd, line_buf, sizeof(line_buf));
        if (n < 0) {
            break; /* EOF or unrecoverable error */
        }
        if (n == 0) {
            continue; /* empty line — re-prompt without printing status */
        }

        /* Parse the line into a Pipeline */
        Pipeline pipeline;
        memset(&pipeline, 0, sizeof(pipeline));

        int parse_result = parse_line(line_buf, &pipeline);
        if (parse_result <= 0) {
            /* 0 = empty/comment, -1 = syntax error (already reported) */
            free_pipeline(&pipeline);
            continue;
        }

        /* Expand wildcards in every sub-command */
        if (expand_pipeline_wildcards(&pipeline) < 0) {
            free_pipeline(&pipeline);
            continue;
        }

        /*
         * Pre-scan for "exit" to handle the piped case ("foo | exit"):
         * the child running exit cannot propagate should_exit to the
         * parent, so we detect it here before executing the pipeline.
         * For non-piped "exit", execute_pipeline also sets should_exit
         * via the wired output parameter.
         */
        for (int i = 0; i < pipeline.num_commands; i++) {
            if (strcmp(pipeline.commands[i].argv[0], "exit") == 0) {
                should_exit = 1;
                break;
            }
        }

        /* Execute and capture status */
        int last_status = execute_pipeline(&pipeline, interactive, &should_exit);

        /*
         * Print exit status immediately after the command completes
         * (interactive only).  Doing it here — not deferred to the next
         * iteration — ensures the last command's status is always shown,
         * even when the next iteration exits the loop.
         */
        if (interactive) {
            print_status(last_status);
        }

        free_pipeline(&pipeline);
    }

    /* ── Goodbye message (interactive only) ─────────────────────────── */
    if (interactive) {
        const char *bye = "Exiting my shell.\n";
        write(STDOUT_FILENO, bye, strlen(bye));
    }

    if (input_fd != STDIN_FILENO) {
        close(input_fd);
    }

    return EXIT_SUCCESS;
}
