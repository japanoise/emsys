/* Interactive editor tests using stub infrastructure */
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
    E.firstBuf = newBuffer();
    E.firstBuf->filename = NULL;
    E.firstBuf->special_buffer = 0;
    E.buf = E.firstBuf;
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
    memset(E.registers, 0, sizeof(E.registers));
    setupCommands(&E);
    E.lastVisitedBuffer = NULL;
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
    if (E.firstBuf) {
        destroyBuffer(E.firstBuf);
        E.firstBuf = NULL;
    }
}

/* Test basic text insertion */
void test_insert_text(void) {
    /* Setup: inject keystrokes to insert "hello" */
    stub_input_init("hello\x1b");
    stub_output_init(1024);
    
    /* Exercise: process each key */
    for (int i = 0; i < 5; i++) {
        int key = editorReadKey();
        editorProcessKeypress(key);
    }
    
    /* Verify: check buffer content */
    TEST_ASSERT_EQUAL(1, E.buf->numrows);
    TEST_ASSERT_EQUAL(5, E.buf->row[0].size);
    TEST_ASSERT_EQUAL_STRING("hello", E.buf->row[0].chars);
    TEST_ASSERT_EQUAL(5, E.buf->cx);
}

/* Test cursor movement */
void test_cursor_movement(void) {
    /* Setup: insert text then move cursor */
    stub_input_init("hello\x1b[D\x1b[D\x1b");  /* hello, left, left */
    stub_output_init(1024);
    
    /* Insert "hello" */
    for (int i = 0; i < 5; i++) {
        editorProcessKeypress(editorReadKey());
    }
    
    /* Move cursor left twice */
    editorProcessKeypress(editorReadKey()); /* ARROW_LEFT */
    TEST_ASSERT_EQUAL(4, E.buf->cx);
    
    editorProcessKeypress(editorReadKey()); /* ARROW_LEFT */
    TEST_ASSERT_EQUAL(3, E.buf->cx);
}

/* Test delete operations */
void test_delete_char(void) {
    /* Setup: insert text then delete */
    stub_input_init("hello\x7f\x7f\x1b");  /* hello, backspace, backspace */
    stub_output_init(1024);
    
    /* Insert "hello" */
    for (int i = 0; i < 5; i++) {
        editorProcessKeypress(editorReadKey());
    }
    
    /* Delete two characters */
    editorProcessKeypress(editorReadKey()); /* BACKSPACE */
    editorProcessKeypress(editorReadKey()); /* BACKSPACE */
    
    /* Verify */
    TEST_ASSERT_EQUAL(3, E.buf->row[0].size);
    TEST_ASSERT_EQUAL_STRING("hel", E.buf->row[0].chars);
    TEST_ASSERT_EQUAL(3, E.buf->cx);
}

/* Test undo functionality */
void test_undo(void) {
    /* Setup: insert text then undo */
    stub_input_init("hello\x1f\x1b");  /* hello, Ctrl-_ (undo) */
    stub_output_init(1024);
    
    /* Insert "hello" */
    for (int i = 0; i < 5; i++) {
        editorProcessKeypress(editorReadKey());
    }
    
    /* Undo the insertion */
    editorProcessKeypress(editorReadKey()); /* CTRL('_') */
    
    /* Verify buffer is empty again */
    TEST_ASSERT_EQUAL(0, E.buf->numrows);
    TEST_ASSERT_EQUAL(0, E.buf->cx);
    TEST_ASSERT_EQUAL(0, E.buf->cy);
}

/* Test line operations */
void test_newline(void) {
    /* Setup: insert text with newline */
    stub_input_init("hello\rworld\x1b");
    stub_output_init(1024);
    
    /* Process keys */
    for (int i = 0; i < 11; i++) {
        editorProcessKeypress(editorReadKey());
    }
    
    /* Verify two lines */
    TEST_ASSERT_EQUAL(2, E.buf->numrows);
    TEST_ASSERT_EQUAL_STRING("hello", E.buf->row[0].chars);
    TEST_ASSERT_EQUAL_STRING("world", E.buf->row[1].chars);
    TEST_ASSERT_EQUAL(1, E.buf->cy);
    TEST_ASSERT_EQUAL(5, E.buf->cx);
}

/* Test kill line (C-k) */
void test_kill_line(void) {
    /* Setup: insert text then kill to end of line */
    stub_input_init("hello world\x01\x06\x06\x06\x06\x06\x06\x0b\x1b");  /* text, C-a, forward 6, C-k */
    stub_output_init(1024);
    
    /* Insert text */
    for (int i = 0; i < 11; i++) {
        editorProcessKeypress(editorReadKey());
    }
    
    /* Go to beginning and forward to space */
    editorProcessKeypress(editorReadKey()); /* C-a */
    for (int i = 0; i < 6; i++) {
        editorProcessKeypress(editorReadKey()); /* C-f */
    }
    
    /* Kill to end of line */
    editorProcessKeypress(editorReadKey()); /* C-k */
    
    /* Verify */
    TEST_ASSERT_EQUAL_STRING("hello ", E.buf->row[0].chars);
    TEST_ASSERT_EQUAL(6, E.buf->row[0].size);
    TEST_ASSERT_EQUAL_STRING("world", E.kill); /* Check kill ring */
}

/* Test universal argument functionality */
void test_universal_arg(void) {
    /* Setup */
    initEditor();
    editorOpen(E.buf, NULL);
    editorInsertRow(E.buf, 0, "0123456789abcdefghijklmnopqrstuvwxyz", 36);
    editorInsertRow(E.buf, 1, "The quick brown fox jumps over the lazy dog", 43);
    editorInsertRow(E.buf, 2, "Line 3", 6);
    editorInsertRow(E.buf, 3, "Line 4", 6);
    E.buf->cx = 0;
    E.buf->cy = 0;
    
    /* Test C-u C-f (forward 4 chars) */
    char input1[] = { CTRL('u'), CTRL('f'), 0 };
    stub_input_init(input1);
    
    editorProcessKeypress(editorReadKey()); /* C-u */
    TEST_ASSERT_EQUAL(4, E.uarg);
    
    editorProcessKeypress(editorReadKey()); /* C-f */
    TEST_ASSERT_EQUAL(4, E.buf->cx);
    TEST_ASSERT_EQUAL(0, E.uarg); /* Should be reset */
    
    /* Test C-u C-u C-f (forward 16 chars) */
    char input2[] = { CTRL('u'), CTRL('u'), CTRL('f'), 0 };
    stub_input_init(input2);
    
    editorProcessKeypress(editorReadKey()); /* First C-u */
    TEST_ASSERT_EQUAL(4, E.uarg);
    
    editorProcessKeypress(editorReadKey()); /* Second C-u */
    TEST_ASSERT_EQUAL(16, E.uarg);
    
    editorProcessKeypress(editorReadKey()); /* C-f */
    TEST_ASSERT_EQUAL(20, E.buf->cx); /* 4 + 16 = 20 */
    TEST_ASSERT_EQUAL(0, E.uarg);
    
    /* Test C-u C-b (backward 4 chars) */
    char input3[] = { CTRL('u'), CTRL('b'), 0 };
    stub_input_init(input3);
    
    editorProcessKeypress(editorReadKey()); /* C-u */
    editorProcessKeypress(editorReadKey()); /* C-b */
    TEST_ASSERT_EQUAL(16, E.buf->cx); /* 20 - 4 = 16 */
    
    /* Test C-u C-n (down 4 lines, but limited by buffer) */
    E.buf->cx = 0;
    E.buf->cy = 0;
    char input4[] = { CTRL('u'), CTRL('n'), 0 };
    stub_input_init(input4);
    
    editorProcessKeypress(editorReadKey()); /* C-u */
    editorProcessKeypress(editorReadKey()); /* C-n */
    TEST_ASSERT_EQUAL(3, E.buf->cy); /* Can only go to line 3 (0-indexed) */
    
    /* Test C-u C-p (up 4 lines, but limited by start) */
    char input5[] = { CTRL('u'), CTRL('p'), 0 };
    stub_input_init(input5);
    
    editorProcessKeypress(editorReadKey()); /* C-u */
    editorProcessKeypress(editorReadKey()); /* C-p */
    TEST_ASSERT_EQUAL(0, E.buf->cy); /* Can't go above line 0 */
    
    /* Test C-u C-d (delete 4 chars) */
    E.buf->cx = 0;
    E.buf->cy = 0;
    char input6[] = { CTRL('u'), CTRL('d'), 0 };
    stub_input_init(input6);
    
    editorProcessKeypress(editorReadKey()); /* C-u */
    editorProcessKeypress(editorReadKey()); /* C-d */
    TEST_ASSERT_EQUAL_STRING("456789abcdefghijklmnopqrstuvwxyz", E.buf->row[0].chars);
    TEST_ASSERT_EQUAL(32, E.buf->row[0].size); /* 36 - 4 = 32 */
    
    /* Test C-u <newline> (insert 4 newlines) */
    E.buf->cx = 5;
    E.buf->cy = 0;
    char input7[] = { CTRL('u'), '\r', 0 };
    stub_input_init(input7);
    
    editorProcessKeypress(editorReadKey()); /* C-u */
    editorProcessKeypress(editorReadKey()); /* Enter */
    
    /* Should have inserted 4 newlines, splitting the line */
    TEST_ASSERT_EQUAL(5, E.buf->numrows); /* Was 4, now 5 (original line split + 3 new) */
    TEST_ASSERT_EQUAL(4, E.buf->cy); /* Cursor at line 4 */
    TEST_ASSERT_EQUAL(0, E.buf->cx); /* At start of line */
    
    /* Test C-u a (insert 'a' 4 times) */
    char input8[] = { CTRL('u'), 'a', 0 };
    stub_input_init(input8);
    
    editorProcessKeypress(editorReadKey()); /* C-u */
    editorProcessKeypress(editorReadKey()); /* 'a' */
    
    TEST_ASSERT_EQUAL_STRING("aaaa89abcdefghijklmnopqrstuvwxyz", E.buf->row[4].chars);
    TEST_ASSERT_EQUAL(4, E.buf->cx); /* Cursor after the 4 'a's */
}

/* Main test runner */
int main(void) {
    TEST_BEGIN();
    
    RUN_TEST(test_insert_text);
    RUN_TEST(test_cursor_movement);
    RUN_TEST(test_delete_char);
    RUN_TEST(test_undo);
    RUN_TEST(test_newline);
    RUN_TEST(test_kill_line);
    RUN_TEST(test_universal_arg);
    
    return TEST_END();
}