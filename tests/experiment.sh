#!/bin/bash
# Experimental testing script for emsys
# Usage: ./experiment.sh "key_sequence" [filename]

if [ $# -eq 0 ]; then
    echo "Usage: $0 \"key_sequence\" [filename]"
    echo ""
    echo "Examples:"
    echo "  $0 \"Hello world\\\\n\""
    echo "  $0 \"\\\\x18\\\\x13\" test.txt  # C-x C-s on test.txt"
    echo ""
    echo "Key notation:"
    echo "  \\\\x01 = C-a (beginning of line)"
    echo "  \\\\x05 = C-e (end of line)"
    echo "  \\\\x06 = C-f (forward)"
    echo "  \\\\x02 = C-b (backward)"
    echo "  \\\\x10 = C-p (up)"
    echo "  \\\\x0e = C-n (down)"
    echo "  \\\\x18 = C-x (prefix)"
    echo "  \\\\x13 = C-s (save)"
    echo "  \\\\x11 = C-q (quit)"
    exit 1
fi

# Build the test if needed
if [ ! -f tests/test_headless ]; then
    echo "Building test_headless..."
    make tests/test_headless || exit 1
fi

# Run the experiment
if [ $# -eq 1 ]; then
    ./tests/test_headless --experiment "$1"
else
    ./tests/test_headless --experiment "$1" "$2"
fi