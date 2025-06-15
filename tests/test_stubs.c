/* Test stub implementations */
#include "test_stubs.h"
#include "../emsys.h"
#include <stdlib.h>
#include <string.h>

/* Access global editor state */
extern struct editorConfig E;

/* Global stub instances */
InputStub g_input_stub = {0};
OutputStub g_output_stub = {0};

/* Input stub functions */
void stub_input_init(const char *keys) {
    g_input_stub.keys = keys;
    g_input_stub.index = 0;
    g_input_stub.enabled = 1;
}

void stub_input_cleanup(void) {
    g_input_stub.keys = NULL;
    g_input_stub.index = 0;
    g_input_stub.enabled = 0;
}

int stub_input_enabled(void) {
    return g_input_stub.enabled;
}

/* Stubbed readKey - returns next character from input string */
int stub_readKey(void) {
    if (!g_input_stub.enabled || !g_input_stub.keys) {
        return '\x1b';  /* ESC to exit */
    }
    
    if (g_input_stub.keys[g_input_stub.index] == '\0') {
        return '\x1b';  /* ESC when done */
    }
    
    return g_input_stub.keys[g_input_stub.index++];
}

/* Output stub functions */
void stub_output_init(size_t buffer_size) {
    g_output_stub.buffer = malloc(buffer_size);
    g_output_stub.size = buffer_size;
    g_output_stub.pos = 0;
    g_output_stub.enabled = 1;
    if (g_output_stub.buffer) {
        g_output_stub.buffer[0] = '\0';
    }
}

void stub_output_cleanup(void) {
    free(g_output_stub.buffer);
    g_output_stub.buffer = NULL;
    g_output_stub.size = 0;
    g_output_stub.pos = 0;
    g_output_stub.enabled = 0;
}

int stub_output_enabled(void) {
    return g_output_stub.enabled;
}

const char *stub_output_get(void) {
    return g_output_stub.buffer ? g_output_stub.buffer : "";
}

/* Stubbed refreshScreen - captures status messages */
void stub_refreshScreen(void) {
    if (!g_output_stub.enabled || !g_output_stub.buffer) {
        return;
    }
    
    /* Capture status message if present */
    if (E.minibuffer[0] != '\0') {
        size_t len = strlen(E.minibuffer);
        if (g_output_stub.pos + len + 2 < g_output_stub.size) {
            if (g_output_stub.pos > 0) {
                g_output_stub.buffer[g_output_stub.pos++] = '\n';
            }
            strcpy(g_output_stub.buffer + g_output_stub.pos, E.minibuffer);
            g_output_stub.pos += len;
            g_output_stub.buffer[g_output_stub.pos] = '\0';
        }
    }
}