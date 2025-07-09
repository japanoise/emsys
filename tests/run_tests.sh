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

# Test 2: Version consistency (if git is available)
if command -v git >/dev/null 2>&1 && git describe --tags >/dev/null 2>&1; then
    GIT_VERSION=$(git describe --tags --always --dirty | sed 's/^v//')
    # Extract VERSION from Makefile
    MAKEFILE_VERSION=$(grep "^VERSION = " Makefile | cut -d' ' -f3)
    if [ "$GIT_VERSION" != "$MAKEFILE_VERSION" ]; then
        echo "✗ Version mismatch: Git has '$GIT_VERSION', Makefile has '$MAKEFILE_VERSION'"
        echo "  Please update VERSION in Makefile to match your git tag"
        exit 1
    fi
    echo "✓ Version consistency"
fi

# Test 3: Binary exists and is executable  
test -x ./emsys || exit 1
echo "✓ Binary executable"

# Test 4: Compile and run core tests
cc -std=c99 -o test_core tests/test_core.c unicode.o wcwidth.o || exit 1
if ./test_core | grep -q "FAIL"; then
    echo "✗ Core tests failed"
    ./test_core
    exit 1
else
    echo "✓ Core functionality"
fi
rm -f test_core

# Test 5: Check for missing newlines
if ./tests/check-newlines.sh > /dev/null 2>&1; then
    echo "✓ Source file newlines"
else
    echo "✗ Missing newlines in source files"
    ./tests/check-newlines.sh
    exit 1
fi

echo ""
echo "All tests passed"