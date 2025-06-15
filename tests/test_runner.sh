#!/bin/bash
# Enhanced test runner for emsys

# Build the test if needed
if [ ! -f tests/test_headless ] || [ tests/test_headless.c -nt tests/test_headless ]; then
    echo "Building test_headless..."
    make tests/test_headless || exit 1
fi

# Function to show help
show_help() {
    cat << EOF
emsys test runner

Usage: $0 [OPTION] [key_sequence] [filename]

Options:
  -h, --help     Show this help message
  -a, --all      Run all tests (default)
  -i, --input    Test with custom input sequence
  -k, --keys     Show key notation reference
  
Examples:
  $0                           # Run all tests
  $0 -i "hello\\nworld\\x11"    # Test custom input
  $0 -i "\\x18\\x13\\x11" test.txt  # Test with file

EOF
}

# Function to show key reference
show_keys() {
    cat << EOF
Key Notation Reference:

Control keys:
  \\x01 = C-a (beginning of line)    \\x02 = C-b (backward)
  \\x05 = C-e (end of line)          \\x06 = C-f (forward)
  \\x0b = C-k (kill line)            \\x0e = C-n (next line)
  \\x10 = C-p (previous line)        \\x11 = C-q (quit)
  \\x13 = C-s (search/save)          \\x18 = C-x (prefix)
  \\x19 = C-y (yank)                 \\x1f = C-_ (undo)
  \\x20 = C-SPC (set mark)

Meta keys (Alt):
  \\x1b followed by key, e.g.:
  \\x1bu = M-u (uppercase word)
  \\x1bl = M-l (lowercase word)
  \\x1bc = M-c (capitalize word)
  \\x1bw = M-w (copy region)

Special keys:
  \\n = newline
  \\r = return/enter
  \\x1b[A = arrow up
  \\x1b[B = arrow down
  \\x1b[C = arrow right
  \\x1b[D = arrow left
  \\x7f = backspace
  \\x1b[3~ = delete

EOF
}

# Default action
action="all"

# Parse options
while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            show_help
            exit 0
            ;;
        -k|--keys)
            show_keys
            exit 0
            ;;
        -a|--all)
            action="all"
            shift
            ;;
        -i|--input)
            action="input"
            shift
            break
            ;;
        *)
            # Assume it's input
            action="input"
            break
            ;;
    esac
done

# Execute based on action
case $action in
    all)
        echo "Running all tests..."
        ./tests/test_headless
        ;;
    input)
        if [ $# -eq 0 ]; then
            echo "Error: No input sequence provided"
            show_help
            exit 1
        fi
        
        echo "Running test with custom input..."
        if [ $# -eq 1 ]; then
            ./tests/test_headless --experiment "$1"
        else
            ./tests/test_headless --experiment "$1" "$2"
        fi
        ;;
esac