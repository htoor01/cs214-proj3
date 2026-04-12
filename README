CS 214 Spring 2026 — Project III: mysh
=======================================

Authors
-------
Hassan [LastName] — NetID: hXXXXXX
[Partner FirstName LastName] — NetID: [partner NetID]


Overview
--------
mysh is a simple UNIX command-line shell implemented in C. It supports
interactive and batch modes, pipelines, I/O redirection, wildcard expansion,
and a small set of built-in commands. All I/O uses POSIX unbuffered system
calls (read, write).


Building
--------
Run:

    make

This produces the executable ./mysh. To clean build artifacts:

    make clean


Usage
-----
Interactive mode (reads from terminal):

    ./mysh

Batch mode from a file:

    ./mysh script.sh

Batch mode from stdin (piped):

    cat script.sh | ./mysh


Modes of Operation
------------------
Interactive mode
  Activated when ./mysh is run with no arguments and stdin is a terminal
  (detected via isatty()). Prints a welcome message on startup, a prompt
  before each command, the exit status of failed commands, and a goodbye
  message on exit.

  Prompt format: the current working directory followed by "$ ". If the
  working directory is inside the user's home directory, the home prefix
  is replaced with "~". Examples:
      ~$
      ~/subdir$
      /etc$

  After each command completes, the exit status is printed immediately
  before the next prompt (not deferred):
  - If it exited with a non-zero code: "Exited with status N"
  - If it was killed by a signal:      "Terminated by signal N: <description>"
  - If it succeeded (exit code 0):     nothing printed

  Child processes inherit the terminal's stdin, EXCEPT when the command
  specifies output redirection (>) or is part of a pipeline — in those
  cases, stdin is redirected from /dev/null per the spec.

Batch mode
  Activated when a script file is given as an argument, or when stdin is
  not a terminal. No prompts or status messages are printed. Child processes
  always receive /dev/null as their standard input unless explicitly
  redirected with <.


Command Format
--------------
One command per line. A command is made of tokens separated by whitespace.
The special characters <, >, and | are always single-character tokens.

Comments
  The # character begins a comment; everything from # to end of line is
  ignored. A line containing only a comment is treated as empty.

Empty lines
  Ignored — the shell re-prompts without updating exit status.


Built-in Commands
-----------------
cd [dir]
  Change the working directory. With no argument, changes to $HOME.
  With one argument, changes to that directory (relative or absolute).
  Prints an error and fails if given too many arguments or if chdir() fails.

pwd
  Print the current working directory to standard output.

which <name>
  Search for <name> in /usr/local/bin, /usr/bin, and /bin (in that order)
  and print the full path if found. Prints nothing and fails if:
    - wrong number of arguments
    - <name> is a built-in command name
    - <name> is not found in the search directories

exit
  Stop reading commands and terminate the shell. mysh always exits with
  EXIT_SUCCESS (unless it failed to open its script argument).

Built-in commands participate in I/O redirection and pipelines just like
external programs. (cd and exit ignore stdin; pwd and which produce output
that can be redirected or piped.)


Program Execution
-----------------
If argv[0] contains a '/', it is treated as a direct path to an executable.

Otherwise, mysh searches /usr/local/bin, /usr/bin, and /bin (in that order)
using access(path, X_OK). The first match is executed via execv().

If the program is not found, mysh prints "<name>: command not found" and
the command fails.

Do NOT use execvp() or execlp() — they use a different search path.


I/O Redirection
---------------
< file
  Open file for reading and use it as the child's standard input.

> file
  Open file for writing (create or truncate, mode 0640) and use it as
  the child's standard output.

Both redirections may appear in the same command in any order:
    foo bar < input.txt > output.txt
    foo bar > output.txt < input.txt   (equivalent)

Redirection is implemented with dup2() inside the child process before execv().
If the file cannot be opened, the command fails with an error message.


Pipelines
---------
The | token connects two or more sub-commands so that the standard output
of each feeds into the standard input of the next:

    cmd1 | cmd2 | cmd3

Pipes are created with pipe() and connected with dup2() before execv().
All sub-commands are started concurrently; mysh waits for all of them.
The pipeline succeeds if and only if the last sub-command succeeds.

For simplicity, sub-commands within a pipeline are not expected to use
file redirection.


Wildcard Expansion
------------------
A token containing * is expanded to the sorted list of matching file names
in the relevant directory.

Pattern rules:
  - foo*bar   — matches names in cwd beginning with "foo" and ending with "bar"
  - baz/foo*bar — same but in subdirectory "baz"
  - *.txt     — matches names ending ".txt" (hidden files excluded)

Hidden files (names beginning with '.') are NOT matched by patterns that
begin with '*'.

If no files match the pattern, the token is passed through to the command
unchanged.

Multiple wildcard tokens in one command are each expanded independently and
the results are concatenated in order.


Source File Structure
---------------------
mysh.c      Entry point, main read-eval loop, prompt and status helpers.
            Status is printed immediately after each command, not deferred.
parse.c     read_line() and parse_line() — tokeniser that builds a Pipeline.
execute.c   resolve_path(), setup_child_io(), execute_single(),
            execute_pipeline() — fork/exec and pipe orchestration.
            err_write() replaces perror/fprintf throughout (POSIX unbuffered IO).
            setup_child_io() redirects stdin from /dev/null in interactive mode
            when the command has output redirection or is part of a pipeline.
builtins.c  is_builtin(), run_builtin(), and the four built-in handlers.
            All output uses write(); all errors use write() + strerror().
wildcard.c  expand_wildcard() and expand_pipeline_wildcards().
mysh.h      Shared constants, data structures, and function declarations.
Makefile    Builds mysh with -Wall -Wextra -Werror -g -std=c11.


Data Structures
---------------
Command
  Holds one sub-command: a NULL-terminated argv[], argc count, and
  optional input_file / output_file strings (heap-allocated).

Pipeline
  An array of up to MAX_COMMANDS (64) Commands connected by pipes.
  A plain command is a Pipeline of length 1.

Memory ownership:
  parse_line() heap-allocates all argv strings and redirection filenames.
  expand_pipeline_wildcards() frees the parse_line allocations and replaces
  them with fresh strdup copies (or glob matches). free_pipeline() frees
  everything.


Test Plan
---------
Testing strategy: all tests run in batch mode by passing script files to
./mysh.  This exercises the full read → parse → expand → execute path for
each scenario.  Each test file is self-contained and produces deterministic
output that can be compared against expected results.

Test scripts (included in submission):

test_basic.sh
  Scenario: bare-name execution and explicit path execution.
  Checks: echo via search path; /bin/echo via absolute path.
  How: ./mysh test_basic.sh, verify "hello", "world", "explicit path works".

test_builtins.sh
  Scenario: cd, pwd, which — correct operation and error handling.
  Checks:
    - pwd prints working directory
    - cd to valid absolute paths, followed by pwd to confirm
    - cd to nonexistent path prints error and leaves cwd unchanged
    - which finds ls, grep, cat in /bin or /usr/bin
    - which on cd, pwd, exit prints nothing (they are built-ins)
    - which on a non-existent name prints nothing
  How: ./mysh test_builtins.sh, inspect output.

test_redirection.sh
  Scenario: > and < file redirection.
  Checks:
    - echo > file creates the file with correct content
    - cat < file reads from file
    - second echo > same file truncates (overwrites)
    - pwd > file redirects built-in output to a file
  How: ./mysh test_redirection.sh, inspect output.

test_pipeline.sh
  Scenario: single and multi-stage pipelines.
  Checks:
    - echo | cat passes data through
    - echo | cat | cat passes through two stages
    - ls | sort | head -3 produces first 3 sorted filenames
    - ls | grep -c counts matching lines
    - ls *.c | sort -r | head -2 combines wildcard expansion with pipeline
  How: ./mysh test_pipeline.sh, inspect output.

test_wildcards.sh
  Scenario: wildcard (glob) expansion.
  Checks:
    - *.c expands to all .c files, sorted
    - *.h, *.o expand similarly
    - m*.c matches only mysh.c
    - *.sh expands to all test scripts
    - test_* matches all test_ prefixed files
    - nomatch_xyz_abc* passes through unchanged (no match)
    - *.txt passes through unchanged (no .txt files present)
  How: ./mysh test_wildcards.sh, inspect output.

test_errors.sh
  Scenario: syntax errors and runtime errors; shell must continue after each.
  Checks:
    - "< <"           → syntax error near '<'
    - "> >"           → syntax error near '>'
    - "| echo bad"    → syntax error near '|'
    - "echo trailing |" → syntax error: trailing '|'
    - nonexistent command → "<name>: command not found"
    - redirect to unwritable path → error message, shell continues
    - cd /nonexistent → cd: No such file or directory
    - cd with too many args → cd: too many arguments
    - final "echo done" confirms shell is still alive after all errors
  How: ./mysh test_errors.sh, verify each error message and "done" at end.

test_comments.sh
  Scenario: comment stripping and exit terminating the script.
  Checks:
    - Full-line comment (#...) is silently ignored
    - Inline comment (echo before # ...) strips from # onward
    - exit stops execution; "echo this should never print" is not run
  How: ./mysh test_comments.sh, verify only "before" and "after comments"
       appear, and the post-exit echo does not.

Additional manual tests performed:
  - Interactive mode: ran ./mysh from a terminal, verified welcome/goodbye
    messages, tilde-substituted prompt, and immediate exit-status display
    (status printed right after the command, before the next prompt).
  - "foo | exit" pipeline: verified mysh terminates after the pipeline
    completes (pre-scan detects "exit" before execution; the child running
    exit exits cleanly; should_exit is also wired through execute_pipeline
    for the non-piped single-exit case).
  - Shell exit code: confirmed ./mysh always exits 0 (EXIT_SUCCESS) even
    after failed commands inside it.
  - Long pipeline: echo hello | cat | cat | cat | cat produces "hello".
  - Interactive stdin with output redirect: in interactive mode, a command
    with > redirect receives /dev/null on stdin per the spec.


Known Limitations
-----------------
- Only a single '*' per token is supported (no ** or multiple wildcards
  within one token), per spec.
- Redirection tokens (<, >) are assumed to be whitespace-separated from
  other tokens (per the spec).
- Sub-commands in a pipeline are not expected to use file redirection
  (per the spec).
- The shell does not support quoted strings or escape characters.
- The '#' character always begins a comment regardless of its position
  within a token, per the spec's literal wording ("The character # begins
  a comment").
- Maximum pipeline depth: 64 sub-commands (MAX_COMMANDS).
- Maximum arguments per command: 256 (MAX_ARGS).
