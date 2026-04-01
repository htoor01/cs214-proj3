/*
 * builtins.c — cd, pwd, which, exit
 *
 * CS 214 Spring 2026 — Project III
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mysh.h"

/* Names of every built-in command */
static const char *const BUILTIN_NAMES[] = {
    "cd", "pwd", "which", "exit", NULL
};

/* ──────────────────────────────────────────────
 * is_builtin
 * ────────────────────────────────────────────── */
int is_builtin(const char *name) {
    for (int i = 0; BUILTIN_NAMES[i] != NULL; i++) {
        if (strcmp(name, BUILTIN_NAMES[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/* ──────────────────────────────────────────────
 * builtin_cd
 *
 * cd [dir]
 *   No argument  → change to HOME
 *   One argument → change to that directory
 *   Otherwise    → error
 * Returns 0 on success, -1 on failure.
 * ────────────────────────────────────────────── */
static int builtin_cd(const Command *cmd) {
    const char *target;

    if (cmd->argc == 1) {
        /* No argument: go home */
        target = getenv("HOME");
        if (target == NULL) {
            fprintf(stderr, "cd: HOME not set\n");
            return -1;
        }
    } else if (cmd->argc == 2) {
        target = cmd->argv[1];
    } else {
        fprintf(stderr, "cd: too many arguments\n");
        return -1;
    }

    if (chdir(target) < 0) {
        /* TODO: use write() with a formatted message rather than perror/fprintf
         *       to be consistent with the POSIX-IO requirement.               */
        perror("cd");
        return -1;
    }
    return 0;
}

/* ──────────────────────────────────────────────
 * builtin_pwd
 *
 * Print the current working directory to out_fd.
 * Returns 0 on success, -1 on failure.
 * ────────────────────────────────────────────── */
static int builtin_pwd(int out_fd) {
    char cwd[4096];

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("pwd");
        return -1;
    }

    /* TODO: use write(out_fd, …) instead of dprintf for pure POSIX IO */
    dprintf(out_fd, "%s\n", cwd);
    return 0;
}

/* ──────────────────────────────────────────────
 * builtin_which
 *
 * which <name>
 *   Prints the resolved path for <name>, or nothing on failure.
 * Returns 0 if found, -1 otherwise.
 * ────────────────────────────────────────────── */
static int builtin_which(const Command *cmd, int out_fd) {
    if (cmd->argc != 2) {
        /* wrong number of arguments */
        return -1;
    }

    const char *name = cmd->argv[1];

    /* built-in names are not reportable */
    if (is_builtin(name)) {
        return -1;
    }

    char resolved[4096];
    if (!resolve_path(name, resolved)) {
        return -1;   /* not found — print nothing */
    }

    /* TODO: use write(out_fd, …) instead of dprintf for pure POSIX IO */
    dprintf(out_fd, "%s\n", resolved);
    return 0;
}

/* ──────────────────────────────────────────────
 * run_builtin
 *
 * Dispatch to the appropriate built-in handler.
 *
 * out_fd      : file descriptor to use for standard output
 * should_exit : set to 1 when the "exit" command is executed
 *
 * Returns 0 on success, -1 on failure.
 * ────────────────────────────────────────────── */
int run_builtin(const Command *cmd, int out_fd, int *should_exit) {
    *should_exit = 0;

    const char *name = cmd->argv[0];

    if (strcmp(name, "cd") == 0) {
        return builtin_cd(cmd);
    }

    if (strcmp(name, "pwd") == 0) {
        return builtin_pwd(out_fd);
    }

    if (strcmp(name, "which") == 0) {
        return builtin_which(cmd, out_fd);
    }

    if (strcmp(name, "exit") == 0) {
        *should_exit = 1;
        return 0;
    }

    /* Should never reach here if is_builtin() was checked first */
    fprintf(stderr, "mysh: unknown built-in: %s\n", name);
    return -1;
}
