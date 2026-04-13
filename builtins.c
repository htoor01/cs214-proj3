/*
 * builtins.c — cd, pwd, which, exit
 *
 * CS 214 Spring 2026 — Project III
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "mysh.h"

static const char *const BUILTIN_NAMES[] = {
    "cd", "pwd", "which", "exit", NULL
};

/*
 * is_builtin
 *
 * Returns 1 if name is one of our four builtins, 0 otherwise.
*/
int is_builtin(const char *name) {
    for (int i = 0; BUILTIN_NAMES[i] != NULL; i++) {
        if (strcmp(name, BUILTIN_NAMES[i]) == 0) return 1;
    }
    return 0;
}

/*
 * builtin_cd
 *
 * Changes the shell's working directory. Has to run in the parent process —
 * if we forked first, the chdir would only affect the child and get thrown
 * away the moment it exits.
 *
 * No argument means go home, one argument means go there, anything else is an error.
*/
static int builtin_cd(const Command *cmd) {
    const char *target;

    if (cmd->argc == 1) {
        target = getenv("HOME");
        if (target == NULL) {
            write(STDERR_FILENO, "cd: HOME not set\n", 17);
            return -1;
        }
    } else if (cmd->argc == 2) {
        target = cmd->argv[1];
    } else {
        write(STDERR_FILENO, "cd: too many arguments\n", 23);
        return -1;
    }

    /*
    * chdir actually changes the directory but if it fails then we 
    * write to stdir manually with 3 seperate write commands
    */
    if (chdir(target) < 0) {
        const char *err = strerror(errno);
        write(STDERR_FILENO, "cd: ", 4);
        write(STDERR_FILENO, err, strlen(err));
        write(STDERR_FILENO, "\n", 1);
        return -1;
    }
    return 0;
}

/*
 * builtin_pwd
 *
 * Prints the current working directory to out_fd.
 * out_fd might not be stdout if the user did pwd > file.
*/
static int builtin_pwd(int out_fd) {
    char cwd[4096];

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        const char *err = strerror(errno);
        write(STDERR_FILENO, "pwd: ", 5);
        write(STDERR_FILENO, err, strlen(err));
        write(STDERR_FILENO, "\n", 1);
        return -1;
    }

    write(out_fd, cwd, strlen(cwd));
    write(out_fd, "\n", 1);
    return 0;
}

/*
 * builtin_which
 *
 * Looks up where a program lives and prints the path.
 * Prints nothing and returns -1 for builtin names and programs we can't find —
 * that's what the spec told us to do.
*/
static int builtin_which(const Command *cmd, int out_fd) {
    if (cmd->argc != 2) return -1; /* which cd returns nothing */

    const char *name = cmd->argv[1];

    // builtins don't have a path on disk — which shouldn't report them
    if (is_builtin(name)) return -1;

    char resolved[4096];
    if (!resolve_path(name, resolved)) return -1;

    write(out_fd, resolved, strlen(resolved));
    write(out_fd, "\n", 1);
    return 0;
}

/*
 * run_builtin
 *
 * Dispatches to the right builtin handler.
 * out_fd is where stdout should go — may differ from STDOUT_FILENO if
 * the command has a > redirect and we're running in the parent.
 * should_exit is set to 1 when exit runs so the main loop knows to stop.
*/
int run_builtin(const Command *cmd, int out_fd, int *should_exit) {
    *should_exit = 0;

    const char *name = cmd->argv[0];

    if (strcmp(name, "cd") == 0)    return builtin_cd(cmd);
    if (strcmp(name, "pwd") == 0)   return builtin_pwd(out_fd);
    if (strcmp(name, "which") == 0) return builtin_which(cmd, out_fd);

    if (strcmp(name, "exit") == 0) {
        *should_exit = 1;
        return 0;
    }

    // shouldn't get here since is_builtin() gates all callers
    write(STDERR_FILENO, "mysh: unknown built-in: ", 24);
    write(STDERR_FILENO, name, strlen(name));
    write(STDERR_FILENO, "\n", 1);
    return -1;
}
