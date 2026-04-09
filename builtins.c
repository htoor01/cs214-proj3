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

static const char *const BUILTIN_NAMES[] = {
    "cd", "pwd", "which", "exit", NULL
};

static int write_output(int fd, const char *text) {
    size_t len = strlen(text);
    ssize_t written = write(fd, text, len);
    if (written < 0 || (size_t)written != len) {
        perror("write");
        return -1;
    }
    return 0;
}

int is_builtin(const char *name) {
    for (int i = 0; BUILTIN_NAMES[i] != NULL; i++) {
        if (strcmp(name, BUILTIN_NAMES[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int builtin_cd(const Command *cmd) {
    const char *target;

    if (cmd->argc == 1) {
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
        perror("cd");
        return -1;
    }
    return 0;
}

static int builtin_pwd(const Command *cmd, int out_fd) {
    if (cmd->argc != 1) {
        return -1;
    }

    char cwd[4096];
    char line[4098];

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("pwd");
        return -1;
    }

    int len = snprintf(line, sizeof(line), "%s\n", cwd);
    if (len < 0 || (size_t)len >= sizeof(line)) {
        return -1;
    }

    return write_output(out_fd, line);
}

static int builtin_which(const Command *cmd, int out_fd) {
    if (cmd->argc != 2) {
        return -1;
    }

    const char *name = cmd->argv[1];
    if (is_builtin(name)) {
        return -1;
    }

    char resolved[4096];
    if (!resolve_path(name, resolved)) {
        return -1;
    }

    char line[4098];
    int len = snprintf(line, sizeof(line), "%s\n", resolved);
    if (len < 0 || (size_t)len >= sizeof(line)) {
        return -1;
    }

    return write_output(out_fd, line);
}

int run_builtin(const Command *cmd, int out_fd, int *should_exit) {
    *should_exit = 0;

    const char *name = cmd->argv[0];

    if (strcmp(name, "cd") == 0) {
        return builtin_cd(cmd);
    }

    if (strcmp(name, "pwd") == 0) {
        return builtin_pwd(cmd, out_fd);
    }

    if (strcmp(name, "which") == 0) {
        return builtin_which(cmd, out_fd);
    }

    if (strcmp(name, "exit") == 0) {
        if (cmd->argc != 1) {
            return -1;
        }
        *should_exit = 1;
        return 0;
    }

    fprintf(stderr, "mysh: unknown built-in: %s\n", name);
    return -1;
}
