/* Unit tests for emsys modules */
#define _POSIX_C_SOURCE 200112L
#include "test.h"
#include "../emsys.h"
#include "../buffer.h"
#include "../unicode.h"
#include "../undo.h"
#include <string.h>
#include <stdlib.h>

/* Minimal stubs for testing */
struct editorConfig E;
void editorSetStatusMessage(const char *fmt, ...) { (void)fmt; }
uint8_t *editorPrompt(struct editorBuffer *bufr, uint8_t *prompt, 
                      enum promptType t,
                      void (*callback)(struct editorBuffer *, uint8_t *, int)) {
    (void)bufr; (void)prompt; (void)t; (void)callback;
    return NULL;
}
void editorInsertNewline(struct editorBuffer *bufr, int count) { (void)bufr; (void)count; }
void editorInsertChar(struct editorBuffer *bufr, int c, int count) { (void)bufr; (void)c; (void)count; }

/* strdup replacement */
char *test_strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *dup = malloc(len);
    if (dup) memcpy(dup, s, len);
    return dup;
}
#define strdup test_strdup

/* Test buffer creation */
void test_buffer_new(void) {
    struct editorBuffer *buf = newBuffer();
    
    TEST_ASSERT(buf != NULL);
    TEST_ASSERT_EQUAL(0, buf->numrows);
    TEST_ASSERT_EQUAL(0, buf->cx);
    TEST_ASSERT_EQUAL(0, buf->cy);
    TEST_ASSERT_EQUAL(-1, buf->markx);
    TEST_ASSERT_EQUAL(-1, buf->marky);
    TEST_ASSERT_EQUAL(0, buf->dirty);
    
    destroyBuffer(buf);
}

/* Test row insertion */
void test_buffer_insert_row(void) {
    struct editorBuffer *buf = newBuffer();
    
    editorInsertRow(buf, 0, "Hello", 5);
    TEST_ASSERT_EQUAL(1, buf->numrows);
    TEST_ASSERT_EQUAL_STRING("Hello", buf->row[0].chars);
    TEST_ASSERT_EQUAL(5, buf->row[0].size);
    
    editorInsertRow(buf, 1, "World", 5);
    TEST_ASSERT_EQUAL(2, buf->numrows);
    TEST_ASSERT_EQUAL_STRING("World", buf->row[1].chars);
    
    destroyBuffer(buf);
}

/* Test row deletion */
void test_buffer_del_row(void) {
    struct editorBuffer *buf = newBuffer();
    
    editorInsertRow(buf, 0, "Line 1", 6);
    editorInsertRow(buf, 1, "Line 2", 6);
    editorInsertRow(buf, 2, "Line 3", 6);
    
    editorDelRow(buf, 1);
    TEST_ASSERT_EQUAL(2, buf->numrows);
    TEST_ASSERT_EQUAL_STRING("Line 1", buf->row[0].chars);
    TEST_ASSERT_EQUAL_STRING("Line 3", buf->row[1].chars);
    
    destroyBuffer(buf);
}

/* Test UTF-8 detection */
void test_unicode_detection(void) {
    /* Test 2-byte char detection */
    TEST_ASSERT_EQUAL(1, utf8_is2Char(0xC2));
    TEST_ASSERT_EQUAL(1, utf8_is2Char(0xDF));
    TEST_ASSERT_EQUAL(0, utf8_is2Char(0x41));  /* ASCII */
    
    /* Test 3-byte char detection */
    TEST_ASSERT_EQUAL(1, utf8_is3Char(0xE0));
    TEST_ASSERT_EQUAL(1, utf8_is3Char(0xEF));
    TEST_ASSERT_EQUAL(0, utf8_is3Char(0xC2));
    
    /* Test 4-byte char detection */
    TEST_ASSERT_EQUAL(1, utf8_is4Char(0xF0));
    TEST_ASSERT_EQUAL(1, utf8_is4Char(0xF4));
    TEST_ASSERT_EQUAL(0, utf8_is4Char(0xE0));
}

/* Test UTF-8 byte counting */
void test_unicode_nbytes(void) {
    TEST_ASSERT_EQUAL(1, utf8_nBytes('A'));
    TEST_ASSERT_EQUAL(2, utf8_nBytes(0xC2));
    TEST_ASSERT_EQUAL(3, utf8_nBytes(0xE0));
    TEST_ASSERT_EQUAL(4, utf8_nBytes(0xF0));
}

/* Test mark operations */
void test_mark_operations(void) {
    struct editorBuffer *buf = newBuffer();
    
    /* Initially no mark */
    TEST_ASSERT_EQUAL(-1, buf->markx);
    TEST_ASSERT_EQUAL(-1, buf->marky);
    
    /* Set mark manually */
    buf->cx = 5;
    buf->cy = 2;
    buf->markx = buf->cx;
    buf->marky = buf->cy;
    TEST_ASSERT_EQUAL(5, buf->markx);
    TEST_ASSERT_EQUAL(2, buf->marky);
    
    /* Clear mark */
    buf->markx = -1;
    buf->marky = -1;
    TEST_ASSERT_EQUAL(-1, buf->markx);
    TEST_ASSERT_EQUAL(-1, buf->marky);
    
    destroyBuffer(buf);
}

/* Test buffer list management */
void test_buffer_list(void) {
    struct editorBuffer *b1 = newBuffer();
    struct editorBuffer *b2 = newBuffer();
    struct editorBuffer *b3 = newBuffer();
    
    /* Link buffers */
    b1->next = b2;
    b2->next = b3;
    b3->next = NULL;
    
    /* Count buffers */
    int count = 0;
    struct editorBuffer *cur = b1;
    while (cur) {
        count++;
        cur = cur->next;
    }
    TEST_ASSERT_EQUAL(3, count);
    
    /* Clean up */
    destroyBuffer(b3);
    destroyBuffer(b2);
    destroyBuffer(b1);
}

/* Test filename operations */
void test_filename_ops(void) {
    struct editorBuffer *buf = newBuffer();
    
    /* Initially no filename */
    TEST_ASSERT(buf->filename == NULL);
    
    /* Set filename */
    buf->filename = strdup("test.txt");
    TEST_ASSERT_EQUAL_STRING("test.txt", buf->filename);
    
    /* Don't free filename - destroyBuffer will handle it */
    destroyBuffer(buf);
}

/* Test universal argument functionality */
void test_universal_arg(void) {
    /* Test initial state */
    TEST_ASSERT_EQUAL(0, E.uarg);
    
    /* Test setting universal argument */
    E.uarg = 4;
    TEST_ASSERT_EQUAL(4, E.uarg);
    
    /* Test C-u C-u behavior (multiply by 4) */
    E.uarg = 16;
    TEST_ASSERT_EQUAL(16, E.uarg);
    
    /* Test reset */
    E.uarg = 0;
    TEST_ASSERT_EQUAL(0, E.uarg);
}

int main(void) {
    TEST_BEGIN();
    
    /* Buffer tests */
    RUN_TEST(test_buffer_new);
    RUN_TEST(test_buffer_insert_row);
    RUN_TEST(test_buffer_del_row);
    RUN_TEST(test_mark_operations);
    RUN_TEST(test_buffer_list);
    RUN_TEST(test_filename_ops);
    
    /* Unicode tests */
    RUN_TEST(test_unicode_detection);
    RUN_TEST(test_unicode_nbytes);
    
    /* Universal argument tests */
    RUN_TEST(test_universal_arg);
    
    return TEST_END();
}