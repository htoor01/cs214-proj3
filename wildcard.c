/*
 * wildcard.c — glob / wildcard expansion
 *
 * CS 214 Spring 2026 — Project III
 *
 * Supported pattern: a single '*' anywhere in the file-name portion
 * of a path (or in a bare file name).
 *
 * Examples:
 *   foo*bar        → matches names in cwd beginning with "foo", ending "bar"
 *   baz/foo*bar    → matches names in "baz/" beginning with "foo", ending "bar"
 *   *.txt          → matches names ending ".txt" (but NOT hidden files, i.e.
 *                    names beginning with '.')
 *
 * If no files match, the original token is kept unchanged.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>

#include "mysh.h"

/* ──────────────────────────────────────────────
 * name_matches
 *
 * Return 1 if `filename` matches the glob pattern `pattern`.
 * pattern must contain exactly one '*'.
 *
 * Hidden files (names starting with '.') are not matched by
 * patterns that start with '*'.
 * ────────────────────────────────────────────── */
static int name_matches(const char *pattern, const char *filename) {
    const char *star = strchr(pattern, '*');
    if (star == NULL) {
        return strcmp(pattern, filename) == 0;
    }

    size_t prefix_len = (size_t)(star - pattern);
    size_t suffix_len = strlen(star + 1);
    size_t name_len   = strlen(filename);

    if (prefix_len == 0 && filename[0] == '.') {
        return 0;
    }

    if (name_len < prefix_len + suffix_len) {
        return 0;
    }

    if (strncmp(pattern, filename, prefix_len) != 0) {
        return 0;
    }

    if (strcmp(filename + name_len - suffix_len, star + 1) != 0) {
        return 0;
    }

    return 1;
}

static int compare_strings(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static void free_arg_list(char **argv, int argc) {
    for (int i = 0; i < argc; i++) {
        free(argv[i]);
    }
}

int expand_wildcard(const char *token, char **out_argv, int out_argc) {
    if (out_argc >= MAX_ARGS - 1) {
        write(STDERR_FILENO, "mysh: too many arguments\n", 25);
        return -1;
    }

    if (strchr(token, '*') == NULL) {
        char *copy = strdup(token);
        if (copy == NULL) {
            perror("strdup");
            return -1;
        }
        out_argv[out_argc++] = copy;
        return out_argc;
    }

    char dir_part[PATH_MAX] = ".";
    char name_pat[PATH_MAX];

    const char *last_slash = strrchr(token, '/');
    if (last_slash != NULL) {
        size_t dir_len = (size_t)(last_slash - token);
        if (dir_len == 0) {
            strncpy(dir_part, "/", sizeof(dir_part) - 1);
            dir_part[sizeof(dir_part) - 1] = '\0';
        } else {
            if (dir_len >= sizeof(dir_part)) {
                write(STDERR_FILENO, "mysh: path too long\n", 20);
                return -1;
            }
            strncpy(dir_part, token, dir_len);
            dir_part[dir_len] = '\0';
        }
        strncpy(name_pat, last_slash + 1, PATH_MAX - 1);
    } else {
        strncpy(name_pat, token, PATH_MAX - 1);
    }
    name_pat[PATH_MAX - 1] = '\0';

    DIR *dir = opendir(dir_part);
    if (dir == NULL) {
        char *copy = strdup(token);
        if (copy == NULL) {
            perror("strdup");
            return -1;
        }
        out_argv[out_argc++] = copy;
        return out_argc;
    }

    char *matches[MAX_ARGS];
    int   match_count = 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!name_matches(name_pat, entry->d_name)) {
            continue;
        }

        char full[PATH_MAX];
        if (strcmp(dir_part, ".") == 0) {
            snprintf(full, sizeof(full), "%s", entry->d_name);
        } else {
            snprintf(full, sizeof(full), "%s/%s", dir_part, entry->d_name);
        }

        matches[match_count] = strdup(full);
        if (matches[match_count] == NULL) {
            perror("strdup");
            closedir(dir);
            free_arg_list(matches, match_count);
            return -1;
        }

        match_count++;
        if (match_count >= MAX_ARGS) {
            break;
        }
    }
    closedir(dir);

    if (match_count == 0) {
        char *copy = strdup(token);
        if (copy == NULL) {
            perror("strdup");
            return -1;
        }
        out_argv[out_argc++] = copy;
        return out_argc;
    }

    qsort(matches, (size_t)match_count, sizeof(char *), compare_strings);

    if (out_argc + match_count > MAX_ARGS - 1) {
        write(STDERR_FILENO, "mysh: too many arguments\n", 25);
        free_arg_list(matches, match_count);
        return -1;
    }

    for (int i = 0; i < match_count; i++) {
        out_argv[out_argc++] = matches[i];
    }

    return out_argc;
}

int expand_pipeline_wildcards(Pipeline *pipeline) {
    for (int c = 0; c < pipeline->num_commands; c++) {
        Command *cmd = &pipeline->commands[c];
        char *new_argv[MAX_ARGS];
        int   new_argc = 0;

        memset(new_argv, 0, sizeof(new_argv));

        for (int a = 0; a < cmd->argc; a++) {
            int result = expand_wildcard(cmd->argv[a], new_argv, new_argc);
            if (result < 0) {
                free_arg_list(new_argv, new_argc);
                return -1;
            }
            new_argc = result;
        }

        for (int a = 0; a < cmd->argc; a++) {
            free(cmd->argv[a]);
            cmd->argv[a] = NULL;
        }

        for (int a = 0; a < new_argc; a++) {
            cmd->argv[a] = new_argv[a];
        }
        cmd->argv[new_argc] = NULL;
        cmd->argc = new_argc;
    }

    return 0;
}
