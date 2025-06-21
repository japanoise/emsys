#!/bin/sh
# Test suite for emsys
set -e

echo "Building emsys..."
make clean > /dev/null 2>&1
make > /dev/null 2>&1

echo "Running tests..."
# Test 1: Version flag works
./emsys --version > /dev/null || exit 1
echo "✓ Version check"

# Test 2: Binary exists and is executable  
test -x ./emsys || exit 1
echo "✓ Binary executable"

# Test 3: Compile and run core tests
cc -std=c99 -o test_core tests/test_core.c unicode.o wcwidth.o || exit 1
if ./test_core | grep -q "FAIL"; then
    echo "✗ Core tests failed"
    ./test_core
    exit 1
else
    echo "✓ Core functionality"
fi
rm -f test_core

echo ""
echo "All tests passed"