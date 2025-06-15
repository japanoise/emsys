/* Test universal argument functionality */
#include "test.h"
#include "test_stubs.h"
#include "../emsys.h"
#include "../buffer.h"
#include "../edit.h"
#include "../keymap.h"
#include "../display.h"
#include "../terminal.h"
#include "../fileio.h"
#include "../undo.h"
#include "../region.h"
#include "../transform.h"
#include "../util.h"
#include <string.h>
#include <time.h>

/* Access global editor state */
struct editorConfig E;

/* We'll use preprocessor to override the real functions */
#define editorReadKey test_editorReadKey
#define editorRefreshScreen test_editorRefreshScreen

/* Test versions that use stubs */
int test_editorReadKey(void) {
    return stub_readKey();
}

void test_editorRefreshScreen(void) {
    stub_refreshScreen();
}

/* Initialize editor for testing */
void initEditor(void) {
    E.buf = newBuffer();
    E.buf->filename = NULL;
    E.buf->special_buffer = 0;
    E.statusmsg_time = 0;
    E.statusmsg[0] = '\0';
    E.kill = NULL;
    E.rectKill = NULL;
    E.recording = 0;
    E.macro.keys = NULL;
    E.macro.nkeys = 0;
    E.macro.skeys = 0;
    E.micro = 0;
    E.playback = 0;
    E.uarg = 0;  /* Initialize universal argument */
    E.uarg_active = 0;
    memset(E.registers, 0, sizeof(E.registers));
    setupCommands(&E);
    E.screenrows = 24;
    E.screencols = 80;
    
    /* Initialize minibuffer */
    E.minibuf = newBuffer();
    E.minibuf->special_buffer = 1;
    E.minibuf->single_line = 1;
    E.minibuf->truncate_lines = 1;
    E.minibuf->filename = stringdup("*minibuffer*");
    E.edbuf = NULL;
    
    /* Setup minimal window configuration */
    E.nwindows = 1;
    E.windows = xmalloc(sizeof(struct editorWindow *));
    E.windows[0] = xmalloc(sizeof(struct editorWindow));
    E.windows[0]->buf = E.buf;
    E.windows[0]->focused = 1;
    E.windows[0]->cx = 0;
    E.windows[0]->cy = 0;
    E.windows[0]->rowoff = 0;
    E.windows[0]->coloff = 0;
    E.windows[0]->height = 24;
}

/* Test fixtures */
void setUp(void) {
    initEditor();
}

void tearDown(void) {
    stub_input_cleanup();
    stub_output_cleanup();
    
    /* Clean up editor state */
    if (E.kill) {
        free(E.kill);
        E.kill = NULL;
    }
    if (E.macro.keys) {
        free(E.macro.keys);
        E.macro.keys = NULL;
    }
    if (E.windows) {
        free(E.windows[0]);
        free(E.windows);
        E.windows = NULL;
    }
    if (E.buf) {
        destroyBuffer(E.buf);
        E.buf = NULL;
    }
    if (E.minibuf) {
        destroyBuffer(E.minibuf);
        E.minibuf = NULL;
    }
}

/* Helper function to insert text into buffer */
void insertText(const char *text) {
    for (const char *p = text; *p; p++) {
        editorInsertChar(E.buf, *p);
    }
}

/* Helper to get buffer text content */
char *getBufferText(void) {
    static char text[1024];
    int pos = 0;
    
    for (int i = 0; i < E.buf->numrows && pos < 1023; i++) {
        struct erow *row = &E.buf->row[i];
        int len = row->size;
        if (pos + len >= 1023) len = 1023 - pos;
        memcpy(text + pos, row->data, len);
        pos += len;
        if (i < E.buf->numrows - 1 && pos < 1023) {
            text[pos++] = '\n';
        }
    }
    text[pos] = '\0';
    return text;
}

/* Test C-u prefix with forward-char */
TEST(test_universal_arg_forward_char) {
    insertText("Hello World!");
    E.buf->cx = 0;  /* Start at beginning */
    
    /* Test C-u C-f (should move 4 chars forward) */
    stub_input_add("\x15");  /* C-u */
    stub_input_add("\x06");  /* C-f */
    
    editorProcessKeypress();  /* Process C-u */
    editorProcessKeypress();  /* Process C-f */
    
    ASSERT(E.buf->cx == 4, "C-u C-f should move 4 characters forward");
}

/* Test C-u with numeric argument */
TEST(test_universal_arg_numeric) {
    insertText("Hello World!");
    E.buf->cx = 0;
    
    /* Test C-u 8 C-f (should move 8 chars forward) */
    stub_input_add("\x15");  /* C-u */
    stub_input_add("8");     /* numeric argument */
    stub_input_add("\x06");  /* C-f */
    
    editorProcessKeypress();  /* Process C-u */
    editorProcessKeypress();  /* Process 8 */
    editorProcessKeypress();  /* Process C-f */
    
    ASSERT(E.buf->cx == 8, "C-u 8 C-f should move 8 characters forward");
}

/* Test C-u C-u (multiply by 4) */
TEST(test_universal_arg_multiply) {
    insertText("0123456789ABCDEF");
    E.buf->cx = 0;
    
    /* Test C-u C-u C-f (should move 16 chars forward) */
    stub_input_add("\x15");  /* C-u */
    stub_input_add("\x15");  /* C-u again */
    stub_input_add("\x06");  /* C-f */
    
    editorProcessKeypress();  /* Process first C-u */
    editorProcessKeypress();  /* Process second C-u */
    editorProcessKeypress();  /* Process C-f */
    
    ASSERT(E.buf->cx == 16, "C-u C-u C-f should move 16 characters forward");
}

/* Test universal argument with delete-char */
TEST(test_universal_arg_delete) {
    insertText("XXXXXXXX");
    E.buf->cx = 0;
    
    /* Test C-u C-d (should delete 4 chars) */
    stub_input_add("\x15");  /* C-u */
    stub_input_add("\x04");  /* C-d */
    
    editorProcessKeypress();  /* Process C-u */
    editorProcessKeypress();  /* Process C-d */
    
    char *text = getBufferText();
    ASSERT(strcmp(text, "XXXX") == 0, "C-u C-d should delete 4 characters");
}

/* Test universal argument with backward-char */
TEST(test_universal_arg_backward) {
    insertText("Hello World!");
    E.buf->cx = 12;  /* Start at end */
    
    /* Test C-u 5 C-b (should move 5 chars backward) */
    stub_input_add("\x15");  /* C-u */
    stub_input_add("5");     /* numeric argument */
    stub_input_add("\x02");  /* C-b */
    
    editorProcessKeypress();  /* Process C-u */
    editorProcessKeypress();  /* Process 5 */
    editorProcessKeypress();  /* Process C-b */
    
    ASSERT(E.buf->cx == 7, "C-u 5 C-b should move 5 characters backward");
}

/* Test universal argument with newline insertion */
TEST(test_universal_arg_newline) {
    insertText("Line");
    E.buf->cx = 4;  /* End of line */
    
    /* Test C-u 3 RET (should insert 3 newlines) */
    stub_input_add("\x15");  /* C-u */
    stub_input_add("3");     /* numeric argument */
    stub_input_add("\r");    /* Return key */
    
    editorProcessKeypress();  /* Process C-u */
    editorProcessKeypress();  /* Process 3 */
    editorProcessKeypress();  /* Process Return */
    
    ASSERT(E.buf->numrows == 4, "C-u 3 RET should create 4 lines total");
}

/* Test universal argument cancellation */
TEST(test_universal_arg_cancel) {
    insertText("Hello");
    E.buf->cx = 0;
    
    /* Test C-u C-g (cancel universal arg) then C-f */
    stub_input_add("\x15");  /* C-u */
    stub_input_add("\x07");  /* C-g (cancel) */
    stub_input_add("\x06");  /* C-f */
    
    editorProcessKeypress();  /* Process C-u */
    editorProcessKeypress();  /* Process C-g */
    editorProcessKeypress();  /* Process C-f */
    
    ASSERT(E.buf->cx == 1, "After C-g, C-f should move only 1 character");
}

/* Test universal argument with kill-line */
TEST(test_universal_arg_kill_line) {
    insertText("Line 1\nLine 2\nLine 3\nLine 4\nLine 5");
    E.buf->cx = 0;
    E.buf->cy = 0;
    
    /* Test C-u 3 C-k (should kill 3 lines) */
    stub_input_add("\x15");  /* C-u */
    stub_input_add("3");     /* numeric argument */
    stub_input_add("\x0b");  /* C-k */
    
    editorProcessKeypress();  /* Process C-u */
    editorProcessKeypress();  /* Process 3 */
    editorProcessKeypress();  /* Process C-k */
    
    ASSERT(E.buf->numrows == 2, "C-u 3 C-k should leave only 2 lines");
    char *text = getBufferText();
    ASSERT(strstr(text, "Line 4") != NULL, "Line 4 should remain");
}

/* Test universal argument with transpose */
TEST(test_universal_arg_transpose) {
    insertText("abcdef");
    E.buf->cx = 2;  /* Position at 'c' */
    
    /* Test C-u 3 C-t (transpose 3 times) */
    stub_input_add("\x15");  /* C-u */
    stub_input_add("3");     /* numeric argument */
    stub_input_add("\x14");  /* C-t */
    
    editorProcessKeypress();  /* Process C-u */
    editorProcessKeypress();  /* Process 3 */
    editorProcessKeypress();  /* Process C-t */
    
    char *text = getBufferText();
    /* After 3 transposes: abc -> acb -> cab -> cba */
    ASSERT(text[0] == 'c', "First char should be 'c' after 3 transposes");
    ASSERT(text[1] == 'b', "Second char should be 'b' after 3 transposes");
}

/* Test universal argument persistence check */
TEST(test_universal_arg_no_persist) {
    insertText("Test");
    E.buf->cx = 0;
    
    /* Test that universal arg doesn't persist across commands */
    stub_input_add("\x15");  /* C-u */
    stub_input_add("2");     /* numeric argument */
    stub_input_add("\x06");  /* C-f */
    stub_input_add("\x06");  /* C-f again */
    
    editorProcessKeypress();  /* Process C-u */
    editorProcessKeypress();  /* Process 2 */
    editorProcessKeypress();  /* Process first C-f */
    
    ASSERT(E.buf->cx == 2, "First C-f should move 2 chars");
    
    editorProcessKeypress();  /* Process second C-f */
    
    ASSERT(E.buf->cx == 3, "Second C-f should move only 1 char (no uarg)");
}

int main(void) {
    RUN_TEST(test_universal_arg_forward_char);
    RUN_TEST(test_universal_arg_numeric);
    RUN_TEST(test_universal_arg_multiply);
    RUN_TEST(test_universal_arg_delete);
    RUN_TEST(test_universal_arg_backward);
    RUN_TEST(test_universal_arg_newline);
    RUN_TEST(test_universal_arg_cancel);
    RUN_TEST(test_universal_arg_kill_line);
    RUN_TEST(test_universal_arg_transpose);
    RUN_TEST(test_universal_arg_no_persist);
    
    printf("\nUniversal Argument Tests: %d passed, %d failed\n", 
           tests_passed, tests_failed);
    
    return tests_failed > 0 ? 1 : 0;
}