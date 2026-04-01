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
    const char *home;

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        /* TODO: handle getcwd error */
        write(STDOUT_FILENO, "$ ", 2);
        return;
    }

    home = getenv("HOME"); /* used below once TODO is implemented */
    (void)home;           /* suppress unused-variable warning */

    /* TODO: if cwd starts with home, replace that prefix with '~' */
    /* TODO: write "<cwd>$ " to STDOUT_FILENO using write() (not printf) */
}

void print_status(int status) {
    (void)status; /* TODO: remove once implemented */
    /* TODO: if WIFEXITED and exit code != 0 → print "Exited with status N"  */
    /* TODO: if WIFSIGNALED             → print "Terminated by signal X"
     *       using strsignal() or psignal()                                  */
}

/* ──────────────────────────────────────────────
 * Main shell loop
 * ────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    int   input_fd;
    int   interactive = 0;
    int   last_status = 0;  /* exit status of most recent command */

    /* ── Determine input source ───────────────────────────────────────── */
    if (argc == 1) {
        input_fd    = STDIN_FILENO;
        interactive = isatty(STDIN_FILENO);
    } else if (argc == 2) {
        /* Batch mode: open the script file */
        input_fd = open(argv[1], O_RDONLY);
        if (input_fd < 0) {
            /* TODO: print error and exit with EXIT_FAILURE */
            perror(argv[1]);
            return EXIT_FAILURE;
        }
        interactive = 0;
    } else {
        /* TODO: print usage message */
        fprintf(stderr, "Usage: %s [script]\n", argv[0]);
        return EXIT_FAILURE;
    }

    /* ── Welcome message (interactive only) ──────────────────────────── */
    if (interactive) {
        /* TODO: write "Welcome to my shell!\n" with write() */
    }

    /* ── Main read-eval loop ─────────────────────────────────────────── */
    char    line_buf[BUF_SIZE];
    ssize_t n;
    int     should_exit = 0;

    while (!should_exit) {
        /* Print prompt in interactive mode */
        if (interactive) {
            /* TODO: optionally print last_status info here before prompt */
            print_prompt();
        }

        /* Read one complete line */
        n = read_line(input_fd, line_buf, sizeof(line_buf));
        if (n < 0) {
            /* TODO: handle read error */
            break;
        }
        if (n == 0) {
            /* EOF */
            break;
        }

        /* Parse the line into a Pipeline */
        Pipeline pipeline;
        memset(&pipeline, 0, sizeof(pipeline));

        int parse_result = parse_line(line_buf, &pipeline);
        if (parse_result == 0) {
            /* Empty line or comment — do nothing */
            continue;
        }
        if (parse_result < 0) {
            /* Syntax error — skip and continue */
            /* TODO: print a syntax-error message */
            last_status = 1;
            continue;
        }

        /* Expand wildcards in every sub-command */
        if (expand_pipeline_wildcards(&pipeline) < 0) {
            /* TODO: handle expansion error */
            last_status = 1;
            continue;
        }

        /* Check for the "exit" built-in at the pipeline level */
        /* (run_builtin / execute_pipeline will set should_exit) */

        /* Execute */
        last_status = execute_pipeline(&pipeline, interactive);

        /* TODO: in interactive mode, call print_status(last_status) if needed */
        (void)last_status; /* suppress unused-variable warning until implemented */
    }

    /* ── Goodbye message (interactive only) ─────────────────────────── */
    if (interactive) {
        /* TODO: write "Exiting my shell.\n" with write() */
    }

    if (input_fd != STDIN_FILENO) {
        close(input_fd);
    }

    return EXIT_SUCCESS;
}
