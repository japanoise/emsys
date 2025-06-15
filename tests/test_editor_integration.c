/* Integration test - full editor in headless mode with I/O stubs */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

/* Define TEST_STUBS before including headers to override functions */
#define TEST_STUBS
#include "../emsys.h"
#include "../buffer.h"
#include "../keymap.h"
#include "../edit.h"
#include "../display.h"
#include "../terminal.h"
#include "../util.h"

/* Global editor state */
struct editorConfig E;

/* Override I/O functions with stubs */
static char *input_buffer = NULL;
static int input_pos = 0;
static char output_buffer[4096] = {0};
static int output_pos = 0;

/* Stub implementations - these override the real ones */
#ifdef TEST_STUBS
int editorReadKey(void) {
    if (!input_buffer || !input_buffer[input_pos]) {
        return '\x1b'; /* ESC to exit */
    }
    return input_buffer[input_pos++];
}

void editorSetStatusMessage(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    output_pos += vsnprintf(output_buffer + output_pos, 
                           sizeof(output_buffer) - output_pos, fmt, args);
    output_pos += snprintf(output_buffer + output_pos, 
                          sizeof(output_buffer) - output_pos, "\n");
    va_end(args);
}

void editorRefreshScreen(void) {
    /* Just record that refresh was called */
    output_pos += snprintf(output_buffer + output_pos, 
                          sizeof(output_buffer) - output_pos, 
                          "[Screen Refreshed]\n");
}

/* Stub other functions that would normally require terminal/display */
void editorCursorBottomLineLong(long curs) {
    output_pos += snprintf(output_buffer + output_pos,
                          sizeof(output_buffer) - output_pos,
                          "[Bottom line cursor: %ld]\n", curs);
}

void die(const char *s) {
    printf("DIE: %s\n", s);
    exit(1);
}

int getCursorPosition(int *rows, int *cols) {
    *rows = 24;
    *cols = 80;
    return 0;
}

void setupTerminal(void) {
    /* No-op in test mode */
}

void restoreTerminal(void) {
    /* No-op in test mode */
}

void editorUpdateBuffer(struct editorBuffer *buf) {
    /* Simple stub */
    (void)buf; /* Suppress unused parameter warning */
}

void editorDrawStatus(void) {
    /* No-op */
}

void editorScroll(void) {
    /* No-op */
}
#endif

/* Test scenarios */
void test_type_hello_world(void) {
    printf("\n=== Test: Type 'Hello World' ===\n");
    
    /* Setup input */
    input_buffer = "Hello World\x1b";
    input_pos = 0;
    output_pos = 0;
    
    /* Initialize editor */
    memset(&E, 0, sizeof(E));
    E.firstBuf = newBuffer();
    E.focusBuf = E.firstBuf;
    
    /* Process each character */
    int key;
    while ((key = editorReadKey()) != '\x1b') {
        editorProcessKeypress(key);
    }
    
    /* Verify buffer contains our text */
    if (E.focusBuf->numrows > 0 && E.focusBuf->row[0].chars) {
        printf("Buffer contains: '%s'\n", E.focusBuf->row[0].chars);
        printf("Status messages:\n%s", output_buffer);
    }
    
    /* Cleanup */
    destroyBuffer(E.firstBuf);
}

void test_save_sequence(void) {
    printf("\n=== Test: C-x C-s Save Sequence ===\n");
    
    /* Setup input: Type something then save */
    input_buffer = "Test file\x18\x13\x1b"; /* "Test file" + C-x + C-s + ESC */
    input_pos = 0;
    output_pos = 0;
    
    /* Initialize editor */
    memset(&E, 0, sizeof(E));
    E.firstBuf = newBuffer();
    E.focusBuf = E.firstBuf;
    
    /* Process keys */
    int key;
    while ((key = editorReadKey()) != '\x1b') {
        editorProcessKeypress(key);
    }
    
    printf("Status messages:\n%s", output_buffer);
    
    /* Cleanup */
    destroyBuffer(E.firstBuf);
}

/* Experimental testing function */
void run_interactive_experiment(const char *description, const char *keys) {
    printf("\n=== Experiment: %s ===\n", description);
    
    input_buffer = (char *)keys;
    input_pos = 0;
    output_pos = 0;
    
    memset(&E, 0, sizeof(E));
    E.firstBuf = newBuffer();
    E.focusBuf = E.firstBuf;
    
    int key;
    while ((key = editorReadKey()) != '\x1b') {
        editorProcessKeypress(key);
    }
    
    printf("Final buffer state:\n");
    for (int i = 0; i < E.focusBuf->numrows && i < 10; i++) {
        printf("  Line %d: '%s'\n", i, E.focusBuf->row[i].chars);
    }
    printf("Status messages:\n%s", output_buffer);
    
    destroyBuffer(E.firstBuf);
}

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "--experiment") == 0) {
        /* Experimental mode - pass key sequence as argument */
        if (argc > 2) {
            run_interactive_experiment("Custom experiment", argv[2]);
        } else {
            printf("Usage: %s --experiment \"key_sequence\"\n", argv[0]);
            printf("Example: %s --experiment \"Hello\\x0d\\x1b\"\n", argv[0]);
        }
        return 0;
    }
    
    /* Run standard tests */
    test_type_hello_world();
    test_save_sequence();
    
    /* Example experiments */
    run_interactive_experiment("Multiple lines", "Line 1\x0dLine 2\x0dLine 3\x1b");
    run_interactive_experiment("Word movement", "Hello world\x1b\x62\x1b\x66\x1b"); /* M-b, M-f */
    
    return 0;
}