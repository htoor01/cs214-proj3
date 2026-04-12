/*
 * wildcard.c — glob / wildcard expansion
 *
 * CS 214 Spring 2026 — Project III
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>

#include "mysh.h"

/*
 * name_matches
 *
 * Returns 1 if filename matches the glob pattern, which must contain
 * exactly one '*'. We split the pattern at the star and check that the
 * filename starts with everything before it and ends with everything after.
 *
 * The hidden-file rule: if the pattern starts with '*' (no prefix), we
 * don't match filenames that start with '.', so *.c won't pick up .hidden.c
*/
static int name_matches(const char *pattern, const char *filename) {
    const char *star = strchr(pattern, '*');

    // no wildcard at all — just do an exact match
    if (star == NULL) return strcmp(pattern, filename) == 0;

    size_t prefix_len = (size_t)(star - pattern);
    size_t suffix_len = strlen(star + 1);
    size_t name_len   = strlen(filename);

    // hidden file rule — *.c should not match .hidden.c
    if (prefix_len == 0 && filename[0] == '.') return 0;

    if (name_len < prefix_len + suffix_len) return 0;

    if (strncmp(pattern, filename, prefix_len) != 0) return 0;

    // check the suffix by anchoring from the end of the filename
    if (strcmp(filename + name_len - suffix_len, star + 1) != 0) return 0;

    return 1;
}

/* used by qsort to sort the matched filenames lexicographically */
static int compare_strings(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/*
 * expand_wildcard
 *
 * Takes one token and expands it into zero or more filenames that match
 * the pattern. If the token has no '*', we just strdup it straight through.
 * If it has a '/', we split into a directory prefix and a filename pattern
 * so that baz/foo*bar looks in baz/ instead of the current directory.
 *
 * Matches are sorted and appended to out_argv starting at out_argc.
 * If nothing matches, the original token is kept unchanged (per spec).
 *
 * Returns the new out_argc, or -1 on error.
*/
int expand_wildcard(const char *token, char **out_argv, int out_argc) {
    // no wildcard — just copy it through unchanged
    if (strchr(token, '*') == NULL) {
        out_argv[out_argc] = strdup(token);
        if (out_argv[out_argc] == NULL) { perror("strdup"); return -1; }
        return out_argc + 1;
    }

    // split "baz/foo*bar" into dir="baz" and pattern="foo*bar"
    // if there's no slash the dir defaults to "." (current directory)
    char dir_part[PATH_MAX] = ".";
    char name_pat[PATH_MAX];

    const char *last_slash = strrchr(token, '/');
    if (last_slash != NULL) {
        size_t dir_len = (size_t)(last_slash - token);
        strncpy(dir_part, token, dir_len);
        dir_part[dir_len] = '\0';
        strncpy(name_pat, last_slash + 1, PATH_MAX - 1);
    } else {
        strncpy(name_pat, token, PATH_MAX - 1);
    }
    name_pat[PATH_MAX - 1] = '\0';

    DIR *dir = opendir(dir_part);
    if (dir == NULL) {
        // can't open the directory — pass the token through unchanged
        out_argv[out_argc] = strdup(token);
        if (out_argv[out_argc] == NULL) { perror("strdup"); return -1; }
        return out_argc + 1;
    }

    // collect all matching entries into a temporary array so we can sort them
    char *matches[MAX_ARGS];
    int   match_count = 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!name_matches(name_pat, entry->d_name)) continue;

        // build the full path — if dir_part is "." we skip the prefix
        // so we get "foo.c" not "./foo.c"
        char full[PATH_MAX];
        if (strcmp(dir_part, ".") == 0) {
            snprintf(full, sizeof(full), "%s", entry->d_name);
        } else {
            snprintf(full, sizeof(full), "%s/%s", dir_part, entry->d_name);
        }

        matches[match_count++] = strdup(full);
        if (match_count >= MAX_ARGS) break;
    }
    closedir(dir);

    if (match_count == 0) {
        // no matches — pass the token through unchanged per spec
        out_argv[out_argc] = strdup(token);
        if (out_argv[out_argc] == NULL) { perror("strdup"); return -1; }
        return out_argc + 1;
    }

    qsort(matches, (size_t)match_count, sizeof(char *), compare_strings);

    for (int i = 0; i < match_count; i++) {
        if (out_argc >= MAX_ARGS - 1) {
            // hit the argument limit — drop the rest
            for (int j = i; j < match_count; j++) free(matches[j]);
            break;
        }
        out_argv[out_argc++] = matches[i];
    }

    return out_argc;
}

/*
 * expand_pipeline_wildcards
 *
 * Walks every command in the pipeline and expands any wildcard tokens.
 * We build a fresh argv by running expand_wildcard on each argument,
 * then free the old strdup'd strings from parse_line and swap in the new ones.
*/
int expand_pipeline_wildcards(Pipeline *pipeline) {
    for (int c = 0; c < pipeline->num_commands; c++) {
        Command *cmd = &pipeline->commands[c];

        char *new_argv[MAX_ARGS];
        int   new_argc = 0;

        for (int a = 0; a < cmd->argc; a++) {
            int result = expand_wildcard(cmd->argv[a], new_argv, new_argc);
            if (result < 0) return -1;
            new_argc = result;
        }

        // free the old argv entries before we overwrite them
        for (int a = 0; a < cmd->argc; a++) {
            free(cmd->argv[a]);
            cmd->argv[a] = NULL;
        }

        memcpy(cmd->argv, new_argv, (size_t)new_argc * sizeof(char *));
        cmd->argv[new_argc] = NULL;
        cmd->argc           = new_argc;
    }
    return 0;
}
