#ifndef MYSH_H
#define MYSH_H

#include <sys/types.h>

/* ──────────────────────────────────────────────
 * Constants
 * ────────────────────────────────────────────── */

#define MAX_ARGS      256
#define MAX_COMMANDS  64    /* max sub-commands in one pipeline */
#define BUF_SIZE      4096

/* Directories searched for bare-name programs, in order */
static const char *const SEARCH_DIRS[] = {
    "/usr/local/bin",
    "/usr/bin",
    "/bin",
    NULL
};

/* ──────────────────────────────────────────────
 * Data structures
 * ────────────────────────────────────────────── */

/*
 * Represents a single sub-command:
 *   e.g. "grep foo" in  "cat file | grep foo > out.txt"
 */
typedef struct {
    char *argv[MAX_ARGS]; /* NULL-terminated argument vector          */
    int   argc;           /* number of arguments (excluding NULL)     */
    char *input_file;     /* path for < redirection, or NULL          */
    char *output_file;    /* path for > redirection, or NULL          */
} Command;

/*
 * A pipeline is a sequence of one or more Commands connected by pipes.
 * A plain (non-piped) command is a pipeline of length 1.
 */
typedef struct {
    Command commands[MAX_COMMANDS];
    int     num_commands;
} Pipeline;

/* ──────────────────────────────────────────────
 * parse.c
 * ────────────────────────────────────────────── */

/*
 * Read one full line from fd into buf (up to buf_size-1 bytes).
 * Stops at newline or EOF.  Returns the number of bytes stored
 * (not counting the terminating NUL), or -1 on error, or 0 on EOF.
 */
ssize_t read_line(int fd, char *buf, size_t buf_size);

/*
 * Tokenise raw_line into a Pipeline.
 * Handles #-comments, <, >, and | meta-tokens.
 * Returns 1 on success, 0 if the line is empty/comment, -1 on syntax error.
 */
int parse_line(const char *raw_line, Pipeline *pipeline);

/* ──────────────────────────────────────────────
 * wildcard.c
 * ────────────────────────────────────────────── */

/*
 * Expand a single token that may contain '*'.
 * Appends matching file names to out_argv starting at *out_argc.
 * If nothing matches, appends the original token unchanged.
 * Returns the new value of *out_argc, or -1 on error.
 */
int expand_wildcard(const char *token, char **out_argv, int out_argc);

/*
 * Walk through every Command in pipeline and expand wildcard tokens in-place.
 * Returns 0 on success, -1 on error.
 */
int expand_pipeline_wildcards(Pipeline *pipeline);

/* ──────────────────────────────────────────────
 * execute.c
 * ────────────────────────────────────────────── */

/*
 * Execute a fully parsed (and wildcard-expanded) Pipeline.
 * Returns the exit status of the last sub-command (0 = success).
 */
int execute_pipeline(const Pipeline *pipeline, int interactive);

/*
 * Resolve the path for bare-name argv[0].
 * Fills resolved_path (must be at least PATH_MAX bytes).
 * Returns 1 if found, 0 if not found.
 */
int resolve_path(const char *name, char *resolved_path);

/* ──────────────────────────────────────────────
 * builtins.c
 * ────────────────────────────────────────────── */

/*
 * Returns 1 if name is the name of a built-in command, 0 otherwise.
 */
int is_builtin(const char *name);

/*
 * Execute a built-in command.
 * out_fd: file descriptor for standard output (may be redirected).
 * Returns 0 on success, -1 on failure.
 * Sets *should_exit to 1 when the "exit" built-in is executed.
 */
int run_builtin(const Command *cmd, int out_fd, int *should_exit);

/* ──────────────────────────────────────────────
 * mysh.c (shell state helpers)
 * ────────────────────────────────────────────── */

/*
 * Print the interactive prompt:  "~/subdir$ " or "/abs/path$ "
 * Replaces the home-directory prefix with '~'.
 */
void print_prompt(void);

/*
 * Print exit-status information after a completed command (interactive only).
 * status: the raw status value from waitpid().
 */
void print_status(int status);

#endif /* MYSH_H */
