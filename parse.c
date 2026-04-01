/*
 * parse.c — line reader and tokeniser
 *
 * CS 214 Spring 2026 — Project III
 */

#include <string.h>
#include <unistd.h>
#include <stdio.h>

#include "mysh.h"

#include <errno.h>
#include <stdlib.h>

/* ──────────────────────────────────────────────
 * read_line
 *
 * Read bytes from fd one at a time (read()) until a newline or EOF.
 * Store up to buf_size-1 bytes, NUL-terminated (newline not stored).
 *
 * Returns:
 *   > 0  — bytes stored (non-empty line)
 *   = 0  — empty line (only a newline was read)
 *   = -1 — EOF with no bytes, or unrecoverable read() error
 *
 * EINTR is retried transparently.
 * ────────────────────────────────────────────── */
ssize_t read_line(int fd, char *buf, size_t buf_size) {
    ssize_t total   = 0;
    int     got_eof = 0;
    char    ch;
    ssize_t n;

    while (total < (ssize_t)(buf_size - 1)) {
        do {
            n = read(fd, &ch, 1);
        } while (n < 0 && errno == EINTR);

        if (n < 0)  return -1;              /* real read error          */
        if (n == 0) { got_eof = 1; break; } /* EOF                      */
        if (ch == '\n') break;              /* end of line (not stored) */

        buf[total++] = ch;
    }

    buf[total] = '\0';

    /* True EOF with no content → signal the caller to stop */
    if (got_eof && total == 0) return -1;

    return total; /* 0 = empty line, > 0 = line with content */
}

/* ──────────────────────────────────────────────
 * strip_comment
 *
 * Truncate line at the first '#' character (in-place).
 * ────────────────────────────────────────────── */
static void strip_comment(char *line) {
    char *comment = strchr(line, '#');
    if (comment) *comment = '\0';
}

/* ──────────────────────────────────────────────
 * next_token
 *
 * Advances *pos past whitespace, then extracts the next token.
 * '<', '>', and '|' are single-character tokens on their own.
 * A regular token is a maximal run of non-whitespace, non-meta chars.
 *
 * Returns a pointer into a static buffer, or NULL at end-of-line.
 * ────────────────────────────────────────────── */
static const char *next_token(const char *line, int *pos) {
    static char token_buf[BUF_SIZE];

    /* Skip leading whitespace */
    while (line[*pos] == ' ' || line[*pos] == '\t') (*pos)++;

    if (line[*pos] == '\0') return NULL;

    /* Single-character meta-tokens */
    if (line[*pos] == '<' || line[*pos] == '>' || line[*pos] == '|') {
        token_buf[0] = line[*pos];
        token_buf[1] = '\0';
        (*pos)++;
        return token_buf;
    }

    /* Regular token: maximal non-whitespace, non-meta run */
    int i = 0;
    while (line[*pos] != '\0' &&
           line[*pos] != ' '  && line[*pos] != '\t' &&
           line[*pos] != '<'  && line[*pos] != '>'  && line[*pos] != '|') {
        token_buf[i++] = line[(*pos)++];
    }
    token_buf[i] = '\0';
    return token_buf;
}

/* ──────────────────────────────────────────────
 * free_pipeline
 *
 * Free all heap strings allocated by parse_line inside a Pipeline.
 * Safe to call on a zero-initialised or already-freed Pipeline.
 * ────────────────────────────────────────────── */
void free_pipeline(Pipeline *pipeline) {
    for (int c = 0; c < pipeline->num_commands; c++) {
        Command *cmd = &pipeline->commands[c];
        for (int a = 0; a < cmd->argc; a++) {
            free(cmd->argv[a]);
            cmd->argv[a] = NULL;
        }
        free(cmd->input_file);
        free(cmd->output_file);
        cmd->input_file  = NULL;
        cmd->output_file = NULL;
        cmd->argc        = 0;
    }
}

/* ──────────────────────────────────────────────
 * parse_line
 *
 * Build a Pipeline from one raw input line.
 * All stored strings are heap-allocated; caller must call free_pipeline().
 *
 * Returns:
 *   1  — pipeline populated (>= 1 sub-command with >= 1 argument each)
 *   0  — empty line (blank or comment only)
 *  -1  — syntax error (pipeline cleaned up before returning)
 * ────────────────────────────────────────────── */
int parse_line(const char *raw_line, Pipeline *pipeline) {
    /* Work on a mutable copy so we can strip comments in-place */
    static char work[BUF_SIZE];
    strncpy(work, raw_line, sizeof(work) - 1);
    work[sizeof(work) - 1] = '\0';

    strip_comment(work);

    memset(pipeline, 0, sizeof(*pipeline));
    pipeline->num_commands = 1;
    int cmd_idx = 0;

    int         pos = 0;
    const char *tok;

    while ((tok = next_token(work, &pos)) != NULL) {
        Command *cur = &pipeline->commands[cmd_idx];

        /* ── Pipe operator ────────────────────────────────────────── */
        if (strcmp(tok, "|") == 0) {
            if (cur->argc == 0) {
                write(STDERR_FILENO, "mysh: syntax error near '|'\n", 28);
                free_pipeline(pipeline);
                return -1;
            }
            if (cmd_idx + 1 >= MAX_COMMANDS) {
                write(STDERR_FILENO, "mysh: too many pipe stages\n", 27);
                free_pipeline(pipeline);
                return -1;
            }
            cmd_idx++;
            pipeline->num_commands++;

        /* ── Input redirection ────────────────────────────────────── */
        } else if (strcmp(tok, "<") == 0) {
            tok = next_token(work, &pos);
            if (tok == NULL ||
                strcmp(tok, "<") == 0 ||
                strcmp(tok, ">") == 0 ||
                strcmp(tok, "|") == 0) {
                write(STDERR_FILENO, "mysh: syntax error near '<'\n", 28);
                free_pipeline(pipeline);
                return -1;
            }
            if (cur->input_file != NULL) {
                write(STDERR_FILENO, "mysh: duplicate input redirection\n", 34);
                free_pipeline(pipeline);
                return -1;
            }
            cur->input_file = strdup(tok);
            if (cur->input_file == NULL) {
                perror("strdup");
                free_pipeline(pipeline);
                return -1;
            }

        /* ── Output redirection ───────────────────────────────────── */
        } else if (strcmp(tok, ">") == 0) {
            tok = next_token(work, &pos);
            if (tok == NULL ||
                strcmp(tok, "<") == 0 ||
                strcmp(tok, ">") == 0 ||
                strcmp(tok, "|") == 0) {
                write(STDERR_FILENO, "mysh: syntax error near '>'\n", 28);
                free_pipeline(pipeline);
                return -1;
            }
            if (cur->output_file != NULL) {
                write(STDERR_FILENO, "mysh: duplicate output redirection\n", 35);
                free_pipeline(pipeline);
                return -1;
            }
            cur->output_file = strdup(tok);
            if (cur->output_file == NULL) {
                perror("strdup");
                free_pipeline(pipeline);
                return -1;
            }

        /* ── Normal argument ──────────────────────────────────────── */
        } else {
            if (cur->argc >= MAX_ARGS - 1) {
                write(STDERR_FILENO, "mysh: too many arguments\n", 25);
                free_pipeline(pipeline);
                return -1;
            }
            cur->argv[cur->argc] = strdup(tok);
            if (cur->argv[cur->argc] == NULL) {
                perror("strdup");
                free_pipeline(pipeline);
                return -1;
            }
            cur->argc++;
            cur->argv[cur->argc] = NULL; /* keep argv NULL-terminated */
        }
    }

    /* Empty / comment-only line */
    if (pipeline->num_commands == 1 && pipeline->commands[0].argc == 0) {
        return 0;
    }

    /* Trailing '|' — last sub-command has no arguments */
    if (pipeline->commands[pipeline->num_commands - 1].argc == 0) {
        write(STDERR_FILENO, "mysh: syntax error: trailing '|'\n", 33);
        free_pipeline(pipeline);
        return -1;
    }

    return 1;
}
