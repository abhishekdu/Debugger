#!/bin/bash

# to run: 
#chmod +x test_shell.sh
#./test_shell.sh ./bin/shell
SHELL_BIN="$1"

if [ -z "$SHELL_BIN" ]; then
    echo "Usage: $0 <shell_binary>"
    exit 1
fi

pass=0
fail=0

# Strip the prompt by removing any leading directory path and prompt symbol
strip_prompt() {
    # Remove any path + prompt (e.g., /mnt/d/.../debugger>)
    echo "$1" | sed -E 's|^[^\n>]+> ||' | sed '/^[[:space:]]*$/d'
}

run_test() {
    name="$1"
    input="$2"
    expected="$3"

    # Get output from the shell and strip out the prompt
    output=$(echo -e "$input" | $SHELL_BIN 2>/dev/null)

    # Strip the prompt from the output before comparison
    stripped_output=$(strip_prompt "$output")

    # If the command is "cd", we skip the output check since "cd" does not produce output
    if [[ "$input" == cd* ]]; then
        stripped_output=""
    fi

    # Compare stripped output to the expected output
    if [[ "$stripped_output" == "$expected" ]]; then
        echo "[PASS] $name"
        ((pass++))
    else
        echo "[FAIL] $name"
        echo "  Expected: '$expected'"
        echo "  Got:      '$stripped_output'"
        ((fail++))
    fi
}

echo "Running automated tests..."

# 1. simple echo
run_test "echo test" "echo hello" "hello"

# 2. quoted arguments
run_test "quoted args" "echo \"hello world\"" "hello world"

# 3. backslash
run_test "backslash space" "echo hello\\ world" "hello world"

# 4. pipeline
run_test "simple pipeline" "echo hi | wc -c" "3"

# 7. redirect
echo "abc" > in.txt
run_test "input redirection" "cat < in.txt" "abc"

echo
echo "========== TEST SUMMARY =========="
echo " PASSED: $pass"
echo " FAILED: $fail"
echo "=================================="

exit 0
