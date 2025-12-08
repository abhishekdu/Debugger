# Mini UNIX Shell

## Features
- External command execution with PATH lookup
- Single pipelines: `ls | grep cpp`
- Background jobs: `sleep 10 &`
- I/O Redirection: `<`, `>`, `>>`
- Built-ins: `cd`, `exit`
- Quoted strings and backslash escaping
- Command substitution: `echo $(pwd)`
- Robust signal handling (Ctrl+C safe, no zombies)

## Usage
```bash
make
./bin/shell
