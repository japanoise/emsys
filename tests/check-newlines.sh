#!/bin/sh
# Test that all source files end with a newline
# This catches the Solaris compiler warning: "newline not last character in file"

echo "Checking source files for missing final newlines..."

failed=0
files_checked=0

# Check all .c and .h files in the parent directory
for file in ../*.c ../*.h; do
    if [ -f "$file" ]; then
        files_checked=$((files_checked + 1))
        
        # Check if last character is not a newline
        if [ -n "$(tail -c 1 "$file")" ]; then
            echo "ERROR: $file missing final newline"
            failed=1
        fi
    fi
done

echo "Checked $files_checked files"

if [ $failed -eq 0 ]; then
    echo "PASS: All source files end with newline"
else
    echo "FAIL: Some files missing final newline"
    echo "Fix with: for f in *.c *.h; do [ -n \"\$(tail -c 1 \"\$f\")\" ] && echo >> \"\$f\"; done"
fi

exit $failed