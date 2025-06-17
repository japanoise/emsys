/* Headless editor for testing - full application with stubbed I/O */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include "../emsys.h"
#include "../terminal.h"
#include "../display.h"
#include "../keymap.h"
#include "../buffer.h"
#include "../register.h"

/* Define CTRL macro for test use */
#ifndef CTRL
#define CTRL(x) ((x) & 0x1f)
#endif

/* Global editor config - this is the real one used by all modules */
struct editorConfig E;

/* Test input buffer */
static const char *test_input = NULL;
static int test_input_pos = 0;

/* Test output buffer */
static char output_buffer[8192];
static int output_pos = 0;

/* Stub display constants */
const int minibuffer_height = 1;
const int statusbar_height = 1;

/*** Terminal stubs - only stub what we need ***/

/* Stub: read from test input instead of real terminal */
int editorReadKey(void) {
    if (!test_input || test_input[test_input_pos] == '\0') {
        return '\x11';  /* Ctrl-Q - Quit when no more input */
    }
    
    char c = test_input[test_input_pos++];
    
    /* Handle escape sequences */
    if (c == '\x1b' && test_input[test_input_pos] != '\0') {
        char seq = test_input[test_input_pos];
        
        /* Handle arrow keys and special sequences */
        if (seq == '[') {
            test_input_pos++; /* skip [ */
            c = test_input[test_input_pos++];
            switch(c) {
                case 'A': return ARROW_UP;
                case 'B': return ARROW_DOWN;
                case 'C': return ARROW_RIGHT;
                case 'D': return ARROW_LEFT;
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
                case '3':
                    if (test_input[test_input_pos] == '~') {
                        test_input_pos++;
                        return DEL_KEY;
                    }
                    break;
                case '5':
                    if (test_input[test_input_pos] == '~') {
                        test_input_pos++;
                        return PAGE_UP;
                    }
                    break;
                case '6':
                    if (test_input[test_input_pos] == '~') {
                        test_input_pos++;
                        return PAGE_DOWN;
                    }
                    break;
            }
        } else {
            /* Handle Meta key sequences (ESC followed by character) */
            test_input_pos++; /* Consume the character after ESC */
            
            /* Handle special Meta sequences */
            if (seq == '<') return BEG_OF_FILE;
            else if (seq == '>') return END_OF_FILE;
            else if (seq == '|') return PIPE_CMD;
            else if (seq == '%') return QUERY_REPLACE;
            else if (seq == '?') return CUSTOM_INFO_MESSAGE;
            else if (seq == '/') return EXPAND;
            else if (seq == 127) return BACKSPACE_WORD;
            else if (seq >= '0' && seq <= '9') return ALT_0 + (seq - '0');
            else {
                /* Convert character to uppercase equivalent for Meta commands */
                switch ((seq & 0x1f) | 0x40) {
                    case 'B': return BACKWARD_WORD;
                    case 'C': return CAPCASE_WORD;
                    case 'D': return DELETE_WORD;
                    case 'F': return FORWARD_WORD;
                    case 'G': return GOTO_LINE;
                    case 'H': return BACKSPACE_WORD;
                    case 'L': return DOWNCASE_WORD;
                    case 'N': return FORWARD_PARA;
                    case 'P': return BACKWARD_PARA;
                    case 'T': return TRANSPOSE_WORDS;
                    case 'U': return UPCASE_WORD;
                    case 'V': return PAGE_UP;
                    case 'W': return COPY;
                    case 'X': return EXEC_CMD;
                }
            }
            
            /* Unknown Meta sequence - return raw ESC */
            test_input_pos--; /* Back up to re-read the character */
            return '\x1b';
        }
    }
    
    return (unsigned char)c;
}

void editorDeserializeUnicode(void) {
    /* Stub - not needed for basic tests */
}

void enableRawMode(void) {
    /* Stub - no terminal setup needed */
}

void disableRawMode(void) {
    /* Stub - no terminal cleanup needed */
}

int getCursorPosition(int *rows, int *cols) {
    *rows = 24;
    *cols = 80;
    return 0;
}

int getWindowSize(int *rows, int *cols) {
    *rows = 24;
    *cols = 80;
    return 0;
}

void die(const char *s) {
    printf("FATAL: %s\n", s);
    exit(1);
}

/*** Display stubs - capture output instead of rendering ***/

/* Append buffer implementation for display */
void abAppend(struct abuf *ab, const char *s, int len) {
    char *new = realloc(ab->b, ab->len + len);
    if (new == NULL) return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab) {
    free(ab->b);
}

/* Stub: capture screen refresh for testing */
void refreshScreen(void) {
    /* Capture status message */
    if (E.statusmsg[0] != '\0') {
        int len = snprintf(output_buffer + output_pos, 
                          sizeof(output_buffer) - output_pos,
                          "[STATUS] %s\n", E.statusmsg);
        if (len > 0) output_pos += len;
    }
    
    /* Capture current buffer state */
    if (E.buf) {
        int len = snprintf(output_buffer + output_pos,
                          sizeof(output_buffer) - output_pos,
                          "[BUFFER] %d lines, dirty=%d, cursor at (%d,%d)",
                          E.buf->numrows, E.buf->dirty,
                          E.buf->cx, E.buf->cy);
        if (len > 0) output_pos += len;
        
        /* Show filename if present */
        if (E.buf->filename && E.buf->filename[0]) {
            len = snprintf(output_buffer + output_pos,
                          sizeof(output_buffer) - output_pos,
                          ", file=%s", E.buf->filename);
            if (len > 0) output_pos += len;
        }
        
        /* Show mark if set */
        if (E.buf->markx != -1 && E.buf->marky != -1) {
            len = snprintf(output_buffer + output_pos,
                          sizeof(output_buffer) - output_pos,
                          ", mark at (%d,%d)", E.buf->markx, E.buf->marky);
            if (len > 0) output_pos += len;
        }
        
        output_buffer[output_pos++] = '\n';
        
        /* Show buffer content */
        if (E.buf->numrows > 0) {
            output_buffer[output_pos++] = '\n';
            len = snprintf(output_buffer + output_pos,
                          sizeof(output_buffer) - output_pos,
                          "--- Buffer Content ---\n");
            if (len > 0) output_pos += len;
            
            for (int i = 0; i < E.buf->numrows && i < 20; i++) {
                len = snprintf(output_buffer + output_pos,
                              sizeof(output_buffer) - output_pos,
                              "%3d| %.*s\n", 
                              i + 1,
                              E.buf->row[i].size,
                              E.buf->row[i].chars);
                if (len > 0) output_pos += len;
            }
            
            if (E.buf->numrows > 20) {
                len = snprintf(output_buffer + output_pos,
                              sizeof(output_buffer) - output_pos,
                              "... (%d more lines)\n", E.buf->numrows - 20);
                if (len > 0) output_pos += len;
            }
            
            /* Show cursor position indicator */
            len = snprintf(output_buffer + output_pos,
                          sizeof(output_buffer) - output_pos,
                          "    %*s^ (cursor at %d,%d)\n",
                          E.buf->cx + 4, "",
                          E.buf->cx, E.buf->cy + 1);
            if (len > 0) output_pos += len;
            
            len = snprintf(output_buffer + output_pos,
                          sizeof(output_buffer) - output_pos,
                          "----------------------\n");
            if (len > 0) output_pos += len;
        }
    }
}

void editorSetStatusMessage(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    va_end(ap);
    E.statusmsg_time = time(NULL);
}

/* Other display stubs that don't need to do anything */
void editorDrawRows(struct editorWindow *win, struct abuf *ab, int screenrows, int screencols) {
    (void)win; (void)ab; (void)screenrows; (void)screencols;
}

void editorDrawStatusBar(struct abuf *ab, struct editorWindow *win) {
    (void)ab; (void)win;
}

void editorDrawMinibuffer(struct abuf *ab) {
    (void)ab;
}

void scroll(void) { }

void editorSetScxScy(struct editorWindow *win) {
    (void)win;
}

void cursorBottomLine(int curs) {
    (void)curs;
}

void editorCursorBottomLineLong(long curs) {
    (void)curs;
}

void editorResizeScreen(int sig) {
    (void)sig;
    E.screenrows = 24;
    E.screencols = 80;
}

void recenter(struct editorWindow *win) {
    (void)win;
}

void editorToggleTruncateLines(void) {
    E.buf->truncate_lines = !E.buf->truncate_lines;
    editorSetStatusMessage("Truncate lines %s", 
                          E.buf->truncate_lines ? "enabled" : "disabled");
}

void editorVersion(void) {
    editorSetStatusMessage("emsys version %s %s", EMSYS_VERSION, EMSYS_BUILD_DATE);
}

int windowFocusedIdx(void) {
    for (int i = 0; i < E.nwindows; i++) {
        if (E.windows[i]->focused) return i;
    }
    return 0;
}

void editorSwitchWindow(void) {
    if (E.nwindows <= 1) return;
    
    int idx = windowFocusedIdx();
    E.windows[idx]->focused = 0;
    idx = (idx + 1) % E.nwindows;
    E.windows[idx]->focused = 1;
}

/* Wrapper functions for command table compatibility */
void editorToggleTruncateLinesWrapper(struct editorConfig *ed, struct editorBuffer *buf) {
    (void)ed; (void)buf;
    editorToggleTruncateLines();
}

void editorVersionWrapper(struct editorConfig *ed, struct editorBuffer *buf) {
    (void)ed; (void)buf;
    editorVersion();
}

/* Missing window management functions */
void editorCreateWindow(void) {
    editorSetStatusMessage("Window creation stubbed");
}

void editorDestroyWindow(void) {
    editorSetStatusMessage("Window destruction stubbed");
}

void editorDestroyOtherWindows(void) {
    editorSetStatusMessage("Destroy other windows stubbed");
}

void editorWhatCursor(void) {
    editorSetStatusMessage("Cursor info stubbed");
}

void synchronizeBufferCursor(struct editorBuffer *buf, struct editorWindow *win) {
    win->cx = buf->cx;
    win->cy = buf->cy;
}

/* Stub for editorPrompt - simulate user input for prompts */
uint8_t *editorPrompt(struct editorBuffer *bufr, uint8_t *prompt,
                      enum promptType t,
                      void (*callback)(struct editorBuffer *, uint8_t *, int)) {
    (void)t;
    
    /* Log the prompt */
    int len = snprintf(output_buffer + output_pos,
                      sizeof(output_buffer) - output_pos,
                      "[PROMPT] %s\n", prompt);
    if (len > 0) output_pos += len;
    
    /* Only handle search prompts with callbacks */
    if (callback != NULL) {
        /* Collect search query until '\r' */
        static uint8_t search_query[256];
        int query_pos = 0;
        
        while (test_input && test_input[test_input_pos] != '\0') {
            char c = test_input[test_input_pos++];
            
            if (c == '\r') {
                search_query[query_pos] = '\0';
                /* Log the search query */
                len = snprintf(output_buffer + output_pos,
                              sizeof(output_buffer) - output_pos,
                              "[SEARCH] Query: %s\n", search_query);
                if (len > 0) output_pos += len;
                
                /* Return a copy of the query */
                uint8_t *result = malloc(query_pos + 1);
                memcpy(result, search_query, query_pos + 1);
                return result;
            } else if (c == '\x07' || c == '\x03') { /* Ctrl-G or Ctrl-C */
                /* Cancel search */
                return NULL;
            } else if (c == '\x13' || c == '\x12') { /* Ctrl-S or Ctrl-R */
                /* Search navigation - call callback */
                if (query_pos > 0) {
                    search_query[query_pos] = '\0';
                    callback(bufr, search_query, c);
                }
            } else {
                /* Add character to query */
                if (query_pos < 255) {
                    search_query[query_pos++] = c;
                    search_query[query_pos] = '\0';
                    /* Call callback to update search */
                    callback(bufr, search_query, c);
                }
            }
        }
    }
    
    /* Return NULL to cancel other prompts */
    return NULL;
}

/*** Test framework ***/

/* Initialize editor - from main.c */
void initEditor(void) {
    E.statusmsg[0] = 0;
    E.kill = NULL;
    E.rectKill = NULL;
    E.windows = malloc(sizeof(struct editorWindow *) * 1);
    E.windows[0] = malloc(sizeof(struct editorWindow));
    E.windows[0]->focused = 1;
    E.nwindows = 1;
    E.recording = 0;
    E.macro.nkeys = 0;
    E.macro.keys = NULL;
    E.micro = 0;
    E.playback = 0;
    E.firstBuf = NULL;
    memset(E.registers, 0, sizeof(E.registers));
    extern void setupCommands(struct editorConfig *ed);
    setupCommands(&E);
    E.lastVisitedBuffer = NULL;
    
    /* Use our stubbed window size */
    E.screenrows = 24;
    E.screencols = 80;
}

/* Run test with given input */
void runTest(const char *name, const char *input, const char *filename) {
    printf("\n=== Test: %s ===\n", name);
    
    /* Reset state */
    test_input = input;
    test_input_pos = 0;
    output_pos = 0;
    output_buffer[0] = '\0';
    
    /* Initialize windows and screen size */
    E.screenrows = 24;
    E.screencols = 80;
    
    /* Initialize editor */
    initEditor();
    
    /* Create initial buffer */
    extern struct editorBuffer *newBuffer(void);
    E.buf = newBuffer();
    E.firstBuf = E.buf;
    E.windows[0]->buf = E.buf;
    
    /* Load file if specified */
    if (filename) {
        extern void editorOpen(struct editorBuffer *bufr, char *filename);
        editorOpen(E.buf, (char *)filename);
    }
    
    /* Run editor until quit */
    int c;
    while ((c = editorReadKey()) != '\x11') { /* Ctrl-Q */
        extern void editorProcessKeypress(int c);
        editorProcessKeypress(c);
        refreshScreen();
    }
    
    /* Print captured output */
    printf("%s", output_buffer);
}

/* Test basic editing */
void test_basic_editing(void) {
    runTest("Basic editing", 
            "Hello world!\n"
            "This is a test."
            "\x18\x13"             /* C-x C-s to save */
            "\x11",                /* C-q to quit */
            NULL);
}

/* Test navigation */
void test_navigation(void) {
    const char *input = 
        "Line 1\n"
        "Line 2\n"
        "Line 3\n"
        "\x10\x10"      /* C-p C-p (up twice) */
        "\x01"          /* C-a (beginning of line) */
        "\x05"          /* C-e (end of line) */
        "\x0e"          /* C-n (down) */
        "\x06"          /* C-f (forward) */
        "\x02"          /* C-b (back) */
        "\x11";         /* C-q to quit */
    
    runTest("Navigation", input, NULL);
}

/* Test file operations */
void test_file_ops(void) {
    /* Create a test file first */
    FILE *fp = fopen("/tmp/test_headless.txt", "w");
    if (fp) {
        fprintf(fp, "Test file content\nLine 2\n");
        fclose(fp);
    }
    
    runTest("File operations",
            "\x05Modified!\x18\x13"   /* C-e, add text, C-x C-s (save) */
            "\x11",                   /* C-q */
            "/tmp/test_headless.txt");
}

/* Test prefix commands */
void test_prefix_commands(void) {
    const char *input = 
        "Test\n"
        "\x18\x13"      /* C-x C-s (save) */
        "\x11";         /* C-q */
    
    runTest("Prefix commands", input, NULL);
}

/* Test kill and yank */
void test_kill_yank(void) {
    runTest("Kill and yank",
            "First line\n"
            "Second line to kill\n"
            "Third line\n"
            "\x10\x10"      /* up twice to line 2 */
            "\x01"          /* beginning of line */
            "\x0b"          /* C-k kill line */
            "\x0e\x0e"      /* down twice */
            "\x19"          /* C-y yank */
            "\x11",         /* C-q */
            NULL);
}

/* Test undo */
void test_undo(void) {
    runTest("Undo operations",
            "Original text\n"
            "\x01"          /* beginning */
            "\x0b"          /* kill line */
            "New text"      /* type new text */
            "\x1f"          /* C-_ undo */
            "\x1f"          /* undo again */
            "\x11",         /* C-q */
            NULL);
}

/* Test search */  
void test_search(void) {
    runTest("Search functionality",
            "Find this word\n"
            "Another line\n"  
            "The word is here\n"
            "\x01"          /* beginning */
            "\x13word\r"    /* C-s search for "word" */
            "\x07"          /* C-g to exit search */
            "\x11",         /* C-q */
            NULL);
}

/* Test region operations */
void test_regions(void) {
    /* Skip - C-@ (null) can't be used in string literal */
    printf("\n=== Test: Region operations ===\n");
    printf("[SKIPPED] Region test requires C-@ which is null character\n");
}

/* Test text transformations */
void test_transforms(void) {
    runTest("Text transformations",
            "hello world test\n"
            "\x01"          /* beginning */
            "\x1bu"         /* M-u uppercase word */
            "\x06\x06\x06\x06\x06\x06"  /* forward to next word */
            "\x1bl"         /* M-l lowercase word */
            "\x06"          /* forward */
            "\x1bc"         /* M-c capitalize word */
            "\x11",         /* C-q */
            NULL);
}

int main(int argc, char *argv[]) {
    /* Check for experimental mode */
    if (argc > 1 && strcmp(argv[1], "--experiment") == 0) {
        if (argc > 2) {
            printf("=== Experimental Mode ===\n");
            printf("Input: %s\n", argv[2]);
            runTest("Experiment", argv[2], argc > 3 ? argv[3] : NULL);
        } else {
            printf("Usage: %s --experiment \"key_sequence\" [filename]\n", argv[0]);
        }
        return 0;
    }
    
    /* Run all tests */
    test_basic_editing();
    test_navigation();
    test_file_ops();
    test_prefix_commands();
    test_kill_yank();
    test_undo();
    /* test_search(); -- skip for now */
    test_regions();
    test_transforms();
    
    printf("\n=== All tests completed ===\n");
    return 0;
}