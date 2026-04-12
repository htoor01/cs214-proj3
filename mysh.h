#ifndef MYSH_H
#define MYSH_H

#include <sys/types.h>
#include <stddef.h>

#define MAX_ARGS      256
#define MAX_COMMANDS  64
#define BUF_SIZE      4096

/* directories searched for bare-name programs, in that order */
static const char *const SEARCH_DIRS[] = {
    "/usr/local/bin",
    "/usr/bin",
    "/bin",
    NULL
};

/*
 * Command — one sub-command in a pipeline, e.g. "grep foo" in
 * "cat file | grep foo > out.txt"
 *
 * argv is NULL-terminated so it can be passed directly to execv.
 * input_file and output_file are NULL when there's no redirection.
*/
typedef struct {
    char *argv[MAX_ARGS];
    int   argc;
    char *input_file;
    char *output_file;
} Command;

/*
 * Pipeline — the full parsed command line.
 * A plain command with no pipes is just a Pipeline with num_commands = 1.
*/
typedef struct {
    Command commands[MAX_COMMANDS];
    int     num_commands;
} Pipeline;

/* parse.c */
ssize_t read_line(int fd, char *buf, size_t buf_size);
int     parse_line(const char *raw_line, Pipeline *pipeline);
void    free_pipeline(Pipeline *pipeline);

/* wildcard.c */
int expand_wildcard(const char *token, char **out_argv, int out_argc);
int expand_pipeline_wildcards(Pipeline *pipeline);

/* execute.c */
int execute_pipeline(const Pipeline *pipeline, int interactive, int *should_exit);
int resolve_path(const char *name, char *resolved_path);

/* builtins.c */
int is_builtin(const char *name);
int run_builtin(const Command *cmd, int out_fd, int *should_exit);

/* mysh.c */
void print_prompt(void);
void print_status(int status);

#endif /* MYSH_H */
