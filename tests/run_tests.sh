#!/bin/sh
# Test suite for emsys
set -e

echo "Running tests..."

# Test 0: Check for missing newlines (must be first, before any builds)
if ./tests/check-newlines.sh > /dev/null 2>&1; then
    echo "✓ Source file newlines"
else
    echo "✗ Missing newlines in source files"
    ./tests/check-newlines.sh
    exit 1
fi

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
# Check if object files were built with sanitizers by looking for ASAN symbols
if nm unicode.o 2>/dev/null | grep -q "__asan_"; then
    echo "✓ Detected sanitizer build, using sanitizer flags for test"
    cc -std=c99 -fsanitize=address,undefined -o test_core tests/test_core.c unicode.o wcwidth.o util.o || exit 1
else
    cc -std=c99 -o test_core tests/test_core.c unicode.o wcwidth.o util.o || exit 1
fi
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