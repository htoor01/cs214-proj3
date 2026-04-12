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
        /* No wildcard: exact match */
        return strcmp(pattern, filename) == 0;
    }

    size_t prefix_len = (size_t)(star - pattern);
    size_t suffix_len = strlen(star + 1);
    size_t name_len   = strlen(filename);

    /* Hidden-file rule: '*' at start of pattern must not match leading '.' */
    if (prefix_len == 0 && filename[0] == '.') {
        return 0;
    }

    if (name_len < prefix_len + suffix_len) {
        return 0;   /* name too short to contain both prefix and suffix */
    }

    /* Check prefix */
    if (strncmp(pattern, filename, prefix_len) != 0) {
        return 0;
    }

    /* Check suffix */
    if (strcmp(filename + name_len - suffix_len, star + 1) != 0) {
        return 0;
    }

    return 1;
}

/* ──────────────────────────────────────────────
 * compare_strings  (for qsort)
 * ────────────────────────────────────────────── */
static int compare_strings(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* ──────────────────────────────────────────────
 * expand_wildcard
 *
 * Expand one potentially-wildcarded token into out_argv[out_argc..].
 * Matches are sorted lexicographically and prepended with the directory
 * prefix if the token contained one.
 *
 * Returns the new out_argc (>= original), or -1 on error.
 * ────────────────────────────────────────────── */
int expand_wildcard(const char *token, char **out_argv, int out_argc) {
    /* If no '*' in the token, nothing to expand */
    if (strchr(token, '*') == NULL) {
        out_argv[out_argc] = strdup(token);
        if (out_argv[out_argc] == NULL) { perror("strdup"); return -1; }
        out_argc++;
        return out_argc;
    }

    /* ── Split token into directory prefix and file-name pattern ─────── */
    char dir_part[PATH_MAX]  = ".";   /* default: current directory */
    char name_pat[PATH_MAX];

    const char *last_slash = strrchr(token, '/');
    if (last_slash != NULL) {
        /* e.g. "baz/foo*bar" → dir="baz", pat="foo*bar" */
        size_t dir_len = (size_t)(last_slash - token);
        strncpy(dir_part, token, dir_len);
        dir_part[dir_len] = '\0';
        strncpy(name_pat, last_slash + 1, PATH_MAX - 1);
    } else {
        strncpy(name_pat, token, PATH_MAX - 1);
    }
    name_pat[PATH_MAX - 1] = '\0';

    /* ── Scan directory for matches ──────────────────────────────────── */
    DIR *dir = opendir(dir_part);
    if (dir == NULL) {
        /* Directory unreadable — pass token through unchanged */
        out_argv[out_argc] = strdup(token);
        if (out_argv[out_argc] == NULL) { perror("strdup"); return -1; }
        out_argc++;
        return out_argc;
    }

    /* Collect matches into a temporary array for sorting */
    char  *matches[MAX_ARGS];
    int    match_count = 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!name_matches(name_pat, entry->d_name)) {
            continue;
        }

        /* Build full path: "dir_part/d_name" */
        char full[PATH_MAX];
        if (strcmp(dir_part, ".") == 0) {
            snprintf(full, sizeof(full), "%s", entry->d_name);
        } else {
            snprintf(full, sizeof(full), "%s/%s", dir_part, entry->d_name);
        }

        /* TODO: strdup(full) for the match entry */
        matches[match_count++] = strdup(full); /* strdup used here — keep */
        if (match_count >= MAX_ARGS) break;
    }
    closedir(dir);

    if (match_count == 0) {
        /* No matches: keep original token unchanged */
        out_argv[out_argc] = strdup(token);
        if (out_argv[out_argc] == NULL) { perror("strdup"); return -1; }
        out_argc++;
        return out_argc;
    }

    /* Sort matches lexicographically */
    qsort(matches, (size_t)match_count, sizeof(char *), compare_strings);

    /* Append matches to out_argv */
    for (int i = 0; i < match_count; i++) {
        if (out_argc >= MAX_ARGS - 1) {
            /* Too many arguments — free remaining and stop */
            for (int j = i; j < match_count; j++) free(matches[j]);
            break;
        }
        out_argv[out_argc++] = matches[i];
    }

    return out_argc;
}

/* ──────────────────────────────────────────────
 * expand_pipeline_wildcards
 *
 * Walk every Command in the Pipeline and expand wildcard tokens in-place.
 * Returns 0 on success, -1 on allocation/other error.
 * ────────────────────────────────────────────── */
int expand_pipeline_wildcards(Pipeline *pipeline) {
    for (int c = 0; c < pipeline->num_commands; c++) {
        Command *cmd = &pipeline->commands[c];

        char *new_argv[MAX_ARGS];
        int   new_argc = 0;

        for (int a = 0; a < cmd->argc; a++) {
            int result = expand_wildcard(cmd->argv[a], new_argv, new_argc);
            if (result < 0) {
                return -1;
            }
            new_argc = result;
        }

        /* Free the old strdup'd argv entries before overwriting */
        for (int a = 0; a < cmd->argc; a++) {
            free(cmd->argv[a]);
            cmd->argv[a] = NULL;
        }

        /* Replace argv with expanded version */
        memcpy(cmd->argv, new_argv, (size_t)new_argc * sizeof(char *));
        cmd->argv[new_argc] = NULL;
        cmd->argc           = new_argc;
    }

    return 0;
}
