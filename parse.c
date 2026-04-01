/*
 * parse.c — line reader and tokeniser
 *
 * CS 214 Spring 2026 — Project III
 */

#include <string.h>
#include <unistd.h>
#include <stdio.h>

#include "mysh.h"

/* ──────────────────────────────────────────────
 * read_line
 *
 * Read bytes from fd one at a time (using read()) until a newline or EOF.
 * Store the bytes in buf (NUL-terminated, newline excluded).
 * Returns:
 *   > 0  — number of bytes stored (not counting NUL)
 *   = 0  — EOF with no bytes read
 *   < 0  — read() error
 *
 * IMPORTANT: we read one byte at a time so that we never consume bytes
 * beyond the newline — the spec requires we do not call read() again
 * after receiving a newline until the current command is executed.
 * ────────────────────────────────────────────── */
ssize_t read_line(int fd, char *buf, size_t buf_size) {
    ssize_t total = 0;
    char    ch;
    ssize_t n;

    while (total < (ssize_t)(buf_size - 1)) {
        n = read(fd, &ch, 1);
        if (n < 0) {
            /* TODO: distinguish EINTR (retry) from real errors */
            return -1;
        }
        if (n == 0) {
            /* EOF */
            break;
        }
        if (ch == '\n') {
            break;
        }
        buf[total++] = ch;
    }

    buf[total] = '\0';
    return total;  /* 0 means EOF with nothing read */
}

/* ──────────────────────────────────────────────
 * strip_comment
 *
 * Truncate raw_line at the first '#' character (in-place).
 * ────────────────────────────────────────────── */
static void strip_comment(char *line) {
    char *comment = strchr(line, '#');
    if (comment) {
        *comment = '\0';
    }
}

/* ──────────────────────────────────────────────
 * next_token
 *
 * A minimal tokeniser for the shell command language.
 *
 * Advances *pos past whitespace, then extracts the next token from line.
 * Special single-character tokens '<', '>', and '|' are returned as-is.
 * Otherwise a token is a maximal run of non-whitespace characters.
 *
 * Returns a pointer to a NUL-terminated token (inside a static buffer),
 * or NULL when the end of the line is reached.
 *
 * NOTE: This is a stub.  You need to make the buffer handling robust.
 * ────────────────────────────────────────────── */
static const char *next_token(const char *line, int *pos) {
    static char token_buf[BUF_SIZE];

    /* Skip leading whitespace */
    while (line[*pos] == ' ' || line[*pos] == '\t') {
        (*pos)++;
    }

    if (line[*pos] == '\0') {
        return NULL;    /* end of line */
    }

    /* Single-character meta-tokens */
    if (line[*pos] == '<' || line[*pos] == '>' || line[*pos] == '|') {
        token_buf[0] = line[*pos];
        token_buf[1] = '\0';
        (*pos)++;
        return token_buf;
    }

    /* Regular token: non-whitespace run */
    int i = 0;
    while (line[*pos] != '\0' &&
           line[*pos] != ' '  &&
           line[*pos] != '\t' &&
           line[*pos] != '<'  &&
           line[*pos] != '>'  &&
           line[*pos] != '|') {
        token_buf[i++] = line[(*pos)++];
    }
    token_buf[i] = '\0';
    return token_buf;
}

/* ──────────────────────────────────────────────
 * parse_line
 *
 * Build a Pipeline from one raw input line.
 *
 * Returns:
 *   1  — pipeline populated successfully
 *   0  — empty command (blank line or comment only)
 *  -1  — syntax error
 * ────────────────────────────────────────────── */
int parse_line(const char *raw_line, Pipeline *pipeline) {
    /* Work on a mutable copy so we can strip comments */
    static char work[BUF_SIZE];
    strncpy(work, raw_line, sizeof(work) - 1);
    work[sizeof(work) - 1] = '\0';

    strip_comment(work);

    /* Initialise the pipeline with one empty command */
    memset(pipeline, 0, sizeof(*pipeline));
    pipeline->num_commands = 1;
    int cmd_idx = 0;

    int         pos = 0;
    const char *tok;

    while ((tok = next_token(work, &pos)) != NULL) {
        Command *cur = &pipeline->commands[cmd_idx];

        if (strcmp(tok, "|") == 0) {
            /* ── Pipe: advance to next sub-command ────────────────── */
            if (cmd_idx + 1 >= MAX_COMMANDS) {
                /* TODO: print "too many pipe stages" error */
                return -1;
            }
            cmd_idx++;
            pipeline->num_commands++;
            /* TODO: validate that the current sub-command is non-empty */

        } else if (strcmp(tok, "<") == 0) {
            /* ── Input redirection ────────────────────────────────── */
            tok = next_token(work, &pos);
            if (tok == NULL || strcmp(tok, "<") == 0 ||
                               strcmp(tok, ">") == 0 ||
                               strcmp(tok, "|") == 0) {
                /* TODO: print syntax-error message */
                return -1;
            }
            if (cur->input_file != NULL) {
                /* duplicate redirection — error */
                return -1;
            }
            /* TODO: strdup(tok) and assign to cur->input_file */
            cur->input_file = (char *)tok; /* placeholder — must strdup */

        } else if (strcmp(tok, ">") == 0) {
            /* ── Output redirection ───────────────────────────────── */
            tok = next_token(work, &pos);
            if (tok == NULL || strcmp(tok, "<") == 0 ||
                               strcmp(tok, ">") == 0 ||
                               strcmp(tok, "|") == 0) {
                /* TODO: print syntax-error message */
                return -1;
            }
            if (cur->output_file != NULL) {
                /* duplicate redirection — error */
                return -1;
            }
            /* TODO: strdup(tok) and assign to cur->output_file */
            cur->output_file = (char *)tok; /* placeholder — must strdup */

        } else {
            /* ── Normal argument ──────────────────────────────────── */
            if (cur->argc >= MAX_ARGS - 1) {
                /* TODO: print "too many arguments" error */
                return -1;
            }
            /* TODO: strdup(tok) before storing */
            cur->argv[cur->argc++] = (char *)tok; /* placeholder — must strdup */
            cur->argv[cur->argc]   = NULL;
        }
    }

    /* An entirely empty (or comment-only) line */
    if (pipeline->num_commands == 1 && pipeline->commands[0].argc == 0) {
        return 0;
    }

    /* TODO: validate that no sub-command has an empty argv */

    return 1;
}
