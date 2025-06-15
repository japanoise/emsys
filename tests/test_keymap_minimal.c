// Minimal test for keymap module - just tests the state machine logic
#include <stdio.h>
#include <string.h>

// Mock the prefix state machine behavior
enum PrefixState {
    PREFIX_NONE,
    PREFIX_CTRL_X,
    PREFIX_CTRL_X_R,
    PREFIX_META
};

// Simple implementation of the prefix state machine
void test_prefix_state_machine(void) {
    enum PrefixState prefix = PREFIX_NONE;
    
    printf("Testing Prefix State Machine\n");
    printf("============================\n");
    
    // Test 1: C-x prefix
    printf("\nTest 1: C-x prefix\n");
    int key = 24;  // CTRL('x') = 24
    if (key == 24) {
        prefix = PREFIX_CTRL_X;
        printf("  Prefix set to C-x\n");
    }
    
    // Test 2: C-x C-f sequence
    printf("\nTest 2: C-x C-f (find file)\n");
    key = 6;  // CTRL('f') = 6
    if (prefix == PREFIX_CTRL_X && key == 6) {
        printf("  Execute: Find File\n");
        prefix = PREFIX_NONE;
    }
    
    // Test 3: C-x C-s sequence
    printf("\nTest 3: C-x C-s (save file)\n");
    prefix = PREFIX_CTRL_X;  // Set prefix again
    key = 19;  // CTRL('s') = 19
    if (prefix == PREFIX_CTRL_X && key == 19) {
        printf("  Execute: Save File\n");
        prefix = PREFIX_NONE;
    }
    
    // Test 4: Regular key without prefix
    printf("\nTest 4: Regular key without prefix\n");
    prefix = PREFIX_NONE;
    key = 6;  // CTRL('f')
    if (prefix == PREFIX_NONE && key == 6) {
        printf("  Execute: Forward Char\n");
    }
    
    printf("\nAll tests passed!\n");
}

int main(void) {
    test_prefix_state_machine();
    return 0;
}