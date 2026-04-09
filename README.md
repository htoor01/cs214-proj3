# cs214-proj3
Systems Programming Project 3

## Partners
- `hbt20` — Haaris Toor
- `hi125` — Hassan Ibrahim

## Brief Test Plan
We tested `mysh` in both **batch** and **interactive** modes to verify parsing, built-ins, redirection, pipes, and wildcard expansion.

### Cases checked
- **Basic execution:** `echo hello`, `pwd`, `which ls`
- **Built-ins:** `cd` with no argument, valid path, invalid path, and too many arguments; `exit`
- **Batch vs. interactive behavior:** prompt/welcome/goodbye messages only in interactive mode, and status reporting after failed commands
- **Redirection:** input/output redirection with `<` and `>` including file creation and truncation
- **Pipelines:** commands such as `echo hello | tr a-z A-Z`
- **Wildcards:** patterns like `foo*bar` and `*.txt`, including the rule that hidden files are not matched by leading `*`
- **Error handling:** syntax errors like trailing `|`, missing redirection targets, command not found, and nonexistent directories for `cd`

### How we tested
We used `make clean && make` to confirm the project builds cleanly, then ran shell commands manually and through piped batch input to confirm expected output and behavior.
