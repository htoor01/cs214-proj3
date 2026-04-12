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

/*
 * print_prompt
 *
 * Prints the prompt before reading a command. Format is "cwd$ " but if
 * the cwd starts with the home directory we replace that prefix with ~
 * so you get ~/subdir$ instead of /home/user/subdir$
*/
void print_prompt(void) {
    char cwd[4096];
    char prompt[4096 + 4];

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        write(STDOUT_FILENO, "$ ", 2);
        return;
    }

    const char *home     = getenv("HOME");
    size_t      home_len = (home != NULL) ? strlen(home) : 0;

    // check if cwd starts with home, and that it's a proper prefix
    // (not just a directory that happens to start with the same characters)
    if (home != NULL && home_len > 0 &&
        strncmp(cwd, home, home_len) == 0 &&
        (cwd[home_len] == '/' || cwd[home_len] == '\0')) {
        snprintf(prompt, sizeof(prompt), "~%s$ ", cwd + home_len);
    } else {
        snprintf(prompt, sizeof(prompt), "%s$ ", cwd);
    }

    write(STDOUT_FILENO, prompt, strlen(prompt));
}

/*
 * print_status
 *
 * Called right after a command finishes (interactive mode only).
 * Prints nothing for success — only says something if the command
 * failed or was killed by a signal.
*/
void print_status(int status) {
    char msg[256];
    int  len;

    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        len = snprintf(msg, sizeof(msg), "Exited with status %d\n", WEXITSTATUS(status));
        write(STDOUT_FILENO, msg, (size_t)len);
    } else if (WIFSIGNALED(status)) {
        len = snprintf(msg, sizeof(msg), "Terminated by signal %d: %s\n",
                       WTERMSIG(status), strsignal(WTERMSIG(status)));
        write(STDOUT_FILENO, msg, (size_t)len);
    }
}

int main(int argc, char *argv[]) {
    int input_fd;
    int interactive = 0;

    if (argc == 1) {
        // no argument — read from stdin, check if it's a terminal
        input_fd    = STDIN_FILENO;
        interactive = isatty(STDIN_FILENO);
    } else if (argc == 2) {
        // script file — always batch mode
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

    if (interactive) {
        const char *welcome = "Welcome to my shell!\n";
        write(STDOUT_FILENO, welcome, strlen(welcome));
    }

    char    line_buf[BUF_SIZE];
    ssize_t n;
    int     should_exit = 0;

    while (!should_exit) {
        if (interactive) print_prompt();

        n = read_line(input_fd, line_buf, sizeof(line_buf));
        if (n < 0) break; /* EOF or error */
        if (n == 0) continue; /* empty line — re-prompt without touching status */

        Pipeline pipeline;
        memset(&pipeline, 0, sizeof(pipeline));

        int parse_result = parse_line(line_buf, &pipeline);
        if (parse_result <= 0) {
            // 0 = blank/comment, -1 = syntax error (already printed)
            free_pipeline(&pipeline);
            continue;
        }

        if (expand_pipeline_wildcards(&pipeline) < 0) {
            free_pipeline(&pipeline);
            continue;
        }

        /* Pre-scan for "exit" anywhere in the pipeline.
           For a plain "exit" the execute path will also set should_exit via
           run_builtin, but for "foo | exit" the exit runs in a forked child
           and can't signal us, so we have to detect it here before we run. */
        for (int i = 0; i < pipeline.num_commands; i++) {
            if (strcmp(pipeline.commands[i].argv[0], "exit") == 0) {
                should_exit = 1;
                break;
            }
        }

        int last_status = execute_pipeline(&pipeline, interactive, &should_exit);

        // print status immediately — not deferred to the next iteration
        // so that the last command before exit or EOF still shows its status
        if (interactive) print_status(last_status);

        free_pipeline(&pipeline);
    }

    if (interactive) {
        const char *bye = "Exiting my shell.\n";
        write(STDOUT_FILENO, bye, strlen(bye));
    }

    if (input_fd != STDIN_FILENO) close(input_fd);

    return EXIT_SUCCESS;
}
