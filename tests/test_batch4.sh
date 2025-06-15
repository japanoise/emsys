#!/bin/bash
# Test script for batch 4 - register functions parameter removal

cd /home/ubuntu/emx/emsys

echo "Building emsys..."
make clean && make || exit 1

echo -e "\n=== Running register function tests ==="

# Test region to register
echo -e "\nTest 1: Region to register (C-x r s)"
echo -e "some text to copy\nmore text\n" | ./tests/test_headless -c "
# Set mark and move to create region
C-space
M->
# Copy region to register 'a'
C-x r s a
# Clear and insert from register
C-a C-k C-k
C-x r i a
# Should see the text again
"

# Test rectangle to register  
echo -e "\nTest 2: Rectangle to register (C-x r r)"
echo -e "line1 text\nline2 text\nline3 text\n" | ./tests/test_headless -c "
# Set rectangle mark
C-space
C-n C-n C-f C-f C-f C-f
# Copy rectangle to register 'b'
C-x r r b
# Move and insert rectangle
C-a C-n C-n
C-x r i b
"

# Test point to register
echo -e "\nTest 3: Point to register (C-x r space)"
echo -e "test file content\n" | ./tests/test_headless -c "
# Move to middle of line
C-f C-f C-f C-f
# Save point to register 'p'
C-x r space p
# Move away and jump back
C-a
C-x r j p
"

# Test view register
echo -e "\nTest 4: View register (M-x view-register)"
echo -e "test content\n" | ./tests/test_headless -c "
# Save some text to register
C-space C-e
C-x r s t
# View the register
M-x view-register RET t
"

# Test increment register (with region)
echo -e "\nTest 5: Increment register"
echo -e "first part\nsecond part\n" | ./tests/test_headless -c "
# Save first region
C-space C-e
C-x r s i
# Select second region
C-a C-n C-space C-e
# Increment register with new region
C-x r + i
# Insert combined register
C-a C-k
C-x r i i
"

echo -e "\n=== All register tests completed ==="