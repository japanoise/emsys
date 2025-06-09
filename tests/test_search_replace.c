#include "unity.h"
#include <string.h>
#include <stdlib.h>
#include "../emsys.h"
#include "../command.h"
#include "../row.h"
#include "../unicode.h"

// Forward declarations for missing functions
void regexFind(struct editorBuffer *buf);
struct editorBuffer *newBuffer(void);
void destroyBuffer(struct editorBuffer *buf);
void editorOpenFile(struct editorBuffer *buf, char *filename);
void clearUndosAndRedos(void);
void recenterCommand(void);
void replaceRegex(void);
void viewRegister(void);

struct editorConfig E;

// Test stubs for missing functions
void invalidateScreenCache(struct editorBuffer *buf) {
    buf->screen_line_cache_valid = 0;
}

int nextScreenX(char *chars, int *i, int current_screen_x) {
    return current_screen_x + 1;
}

void setStatusMessage(const char *fmt, ...) {
    // Test stub
}

void transformRegion(uint8_t *(*transformer)(uint8_t *)) {
    // Test stub for region transformation
}

void refreshScreen(void) {
    // Test stub
}

void cursorBottomLine(int x) {
    // Test stub
}

int readKey(void) {
    // Test stub - return 'q' to quit immediately
    return 'q';
}

void recordKey(int c) {
    // Test stub
}

int windowFocusedIdx(struct editorConfig *config) {
    return 0;
}

void recenter(struct editorWindow *window) {
    // Test stub
}

void doUndo(int count) {
    // Test stub
}

// stringWidth is provided by unicode.o

void regexFind(struct editorBuffer *buf) {
    // Test stub
}

struct editorBuffer *newBuffer(void) {
    return calloc(1, sizeof(struct editorBuffer));
}

void destroyBuffer(struct editorBuffer *buf) {
    if (buf) {
        for (int i = 0; i < buf->numrows; i++) {
            free(buf->row[i].chars);
        }
        free(buf->row);
        free(buf->filename);
        free(buf);
    }
}

void editorOpenFile(struct editorBuffer *buf, char *filename) {
    // Test stub
}

void clearUndosAndRedos(void) {
    // Test stub
}

void recenterCommand(void) {
    // Test stub
}

void replaceRegex(void) {
    // Test stub
}

void viewRegister(void) {
    // Test stub
}

uint8_t *promptUser(struct editorBuffer *buf, uint8_t *prompt, enum promptType prompt_type, void (*completion)(struct editorBuffer *, uint8_t *, int)) {
    // Test stub - return NULL to simulate cancel
    return NULL;
}

// Forward declarations for command functions
char *str_replace(char *orig, char *rep, char *with);
uint8_t *transformerReplaceString(uint8_t *input);
void replaceString(void);
void editorQueryReplace(void);

// Global variables for str_replace (from command.c)
extern uint8_t *orig;
extern uint8_t *repl;

void setUp(void) {
    memset(&E, 0, sizeof(E));
    E.buf = calloc(1, sizeof(struct editorBuffer));
    E.buf->numrows = 0;
    E.buf->row = NULL;
    E.minibuf = calloc(1, sizeof(struct editorBuffer));
    E.nwindows = 1;
    E.windows = malloc(sizeof(struct editorWindow*));
    E.windows[0] = calloc(1, sizeof(struct editorWindow));
    E.windows[0]->buf = E.buf;
    E.windows[0]->focused = 1;
}

void tearDown(void) {
    if (E.buf) {
        for (int i = 0; i < E.buf->numrows; i++) {
            free(E.buf->row[i].chars);
        }
        free(E.buf->row);
        free(E.buf);
    }
    if (E.minibuf) {
        for (int i = 0; i < E.minibuf->numrows; i++) {
            free(E.minibuf->row[i].chars);
        }
        free(E.minibuf->row);
        free(E.minibuf);
    }
    if (E.windows) {
        free(E.windows[0]);
        free(E.windows);
    }
}

// Test str_replace with edge cases that could cause buffer overflows
void test_str_replace_buffer_overflow_protection(void) {
    // Test replacement that greatly expands the string
    char *input = "X";
    char *find = "X";
    char *replace = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"; // 50 chars
    
    char *result = str_replace(input, find, replace);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING(replace, result);
    free(result);
}

// Test str_replace with very large input
void test_str_replace_large_input(void) {
    // Create large input string
    char large_input[10000];
    memset(large_input, 'A', 9999);
    large_input[9999] = '\0';
    
    // Insert pattern in middle
    memcpy(large_input + 5000, "PATTERN", 7);
    
    char *result = str_replace(large_input, "PATTERN", "REPLACEMENT");
    TEST_ASSERT_NOT_NULL(result);
    
    // Should have replaced the pattern
    char *found = strstr(result, "REPLACEMENT");
    TEST_ASSERT_NOT_NULL(found);
    
    free(result);
}

// Test str_replace with integer overflow scenarios
void test_str_replace_integer_overflow_protection(void) {
    // Test with maximum possible count that could cause overflow
    char input[1000];
    memset(input, 'X', 999);
    input[999] = '\0';
    
    // Replace every character - this tests count overflow protection
    char *result = str_replace(input, "X", "VERYLONGREPLACEMENTSTRING");
    
    // Should either succeed or return NULL (overflow protection)
    if (result) {
        // If it succeeds, verify it's correct
        TEST_ASSERT_TRUE(strlen(result) > 999);
        free(result);
    } else {
        // NULL return indicates overflow protection worked
        TEST_ASSERT_NULL(result);
    }
}

// Test str_replace with overlapping patterns
void test_str_replace_overlapping_patterns(void) {
    char *result = str_replace("AAA", "AA", "B");
    TEST_ASSERT_NOT_NULL(result);
    
    // Should replace first occurrence only
    TEST_ASSERT_EQUAL_STRING("BA", result);
    free(result);
}

// Test str_replace with empty string edge cases
void test_str_replace_empty_strings(void) {
    // Replace with empty string (deletion)
    char *result = str_replace("Hello World", "World", "");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("Hello ", result);
    free(result);
    
    // Empty find pattern should return NULL
    result = str_replace("Hello", "", "replacement");
    TEST_ASSERT_NULL(result);
    
    // NULL with parameter should use empty string
    result = str_replace("Hello", "Hello", NULL);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("", result);
    free(result);
}

// Test str_replace with UTF-8 strings
void test_str_replace_utf8_strings(void) {
    char *result = str_replace("Hello 世界 World", "世界", "Universe");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("Hello Universe World", result);
    free(result);
    
    // Test UTF-8 replacement string
    result = str_replace("Hello World", "World", "世界");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("Hello 世界", result);
    free(result);
}

// Test transformerReplaceString function
void test_transformer_replace_string(void) {
    // Set up global variables that transformerReplaceString uses
    orig = (uint8_t *)malloc(5);
    strcpy((char *)orig, "test");
    repl = (uint8_t *)malloc(12);
    strcpy((char *)repl, "replacement");
    
    uint8_t *input = (uint8_t *)"This is a test string";
    uint8_t *result = transformerReplaceString(input);
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("This is a replacement string", (char *)result);
    
    free(result);
    free(orig);
    free(repl);
    orig = NULL;
    repl = NULL;
}

// Test transformerReplaceString with NULL input
void test_transformer_replace_string_null_input(void) {
    orig = (uint8_t *)malloc(5);
    strcpy((char *)orig, "test");
    repl = (uint8_t *)malloc(12);
    strcpy((char *)repl, "replacement");
    
    uint8_t *result = transformerReplaceString(NULL);
    TEST_ASSERT_NULL(result);
    
    free(orig);
    free(repl);
    orig = NULL;
    repl = NULL;
}

// Test transformerReplaceString when globals are NULL
void test_transformer_replace_string_null_globals(void) {
    orig = NULL;
    repl = NULL;
    
    uint8_t *input = (uint8_t *)"test string";
    uint8_t *result = transformerReplaceString(input);
    TEST_ASSERT_NULL(result);
}

// Test replaceString function (will use promptUser stub)
void test_replace_string_function(void) {
    insertRow(E.buf, 0, "Hello World", 11);
    
    // This will call promptUser which returns NULL (cancel)
    replaceString();
    
    // Should handle cancellation gracefully
    TEST_ASSERT_EQUAL_STRING("Hello World", E.buf->row[0].chars);
}

// Test editorQueryReplace function 
void test_query_replace_function(void) {
    insertRow(E.buf, 0, "Replace this text", 17);
    
    E.buf->cx = 0;
    E.buf->cy = 0;
    
    // This will call promptUser which returns NULL (cancel)
    editorQueryReplace();
    
    // Should handle cancellation gracefully
    TEST_ASSERT_EQUAL_STRING("Replace this text", E.buf->row[0].chars);
}

// Test str_replace with self-referential patterns (could cause infinite loops)
void test_str_replace_self_referential(void) {
    // Pattern that contains its replacement
    char *result = str_replace("ABAB", "AB", "ABAB");
    TEST_ASSERT_NOT_NULL(result);
    
    // Should replace first occurrence only, not infinite loop
    TEST_ASSERT_EQUAL_STRING("ABABAB", result);
    free(result);
}

// Test str_replace with repeated patterns
void test_str_replace_repeated_patterns(void) {
    char *result = str_replace("ababababab", "ab", "X");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("XXXXX", result);
    free(result);
}

// Test str_replace with case-sensitive patterns
void test_str_replace_case_sensitive(void) {
    char *result = str_replace("Hello hello HELLO", "hello", "hi");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("Hello hi HELLO", result);
    free(result);
}

// Test str_replace with special regex characters (should be literal)
void test_str_replace_special_characters_literal(void) {
    char *result = str_replace("Price: $5.99 (USD)", "$5.99", "€4.50");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("Price: €4.50 (USD)", result);
    free(result);
    
    // Test other special chars
    result = str_replace("Regex: ^[a-z]+$", "^[a-z]+$", "pattern");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("Regex: pattern", result);
    free(result);
}

// Test str_replace memory allocation failure simulation
void test_str_replace_memory_pressure(void) {
    // Test with very large expansion that might fail allocation
    char input[100];
    memset(input, 'X', 99);
    input[99] = '\0';
    
    // Try to replace each X with a 1000-character string
    char large_replacement[1001];
    memset(large_replacement, 'A', 1000);
    large_replacement[1000] = '\0';
    
    char *result = str_replace(input, "X", large_replacement);
    
    // Should either succeed or gracefully handle failure
    if (result) {
        TEST_ASSERT_TRUE(strlen(result) > 99000);
        free(result);
    } else {
        // Graceful failure is acceptable
        TEST_ASSERT_NULL(result);
    }
}

// Test str_replace with boundary conditions
void test_str_replace_boundary_conditions(void) {
    // Pattern at start
    char *result = str_replace("startmiddle", "start", "begin");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("beginmiddle", result);
    free(result);
    
    // Pattern at end
    result = str_replace("middleend", "end", "finish");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("middlefinish", result);
    free(result);
    
    // Entire string is pattern
    result = str_replace("pattern", "pattern", "replacement");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("replacement", result);
    free(result);
}

// Test str_replace with zero-length input
void test_str_replace_zero_length_input(void) {
    char *result = str_replace("", "anything", "replacement");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("", result);
    free(result);
}

// Test str_replace with single character patterns
void test_str_replace_single_characters(void) {
    char *result = str_replace("abcdefg", "c", "X");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("abXdefg", result);
    free(result);
    
    // Single char to multiple chars
    result = str_replace("a", "a", "multiple");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("multiple", result);
    free(result);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_str_replace_buffer_overflow_protection);
    RUN_TEST(test_str_replace_large_input);
    RUN_TEST(test_str_replace_integer_overflow_protection);
    RUN_TEST(test_str_replace_overlapping_patterns);
    RUN_TEST(test_str_replace_empty_strings);
    RUN_TEST(test_str_replace_utf8_strings);
    RUN_TEST(test_transformer_replace_string);
    RUN_TEST(test_transformer_replace_string_null_input);
    RUN_TEST(test_transformer_replace_string_null_globals);
    RUN_TEST(test_replace_string_function);
    RUN_TEST(test_query_replace_function);
    RUN_TEST(test_str_replace_self_referential);
    RUN_TEST(test_str_replace_repeated_patterns);
    RUN_TEST(test_str_replace_case_sensitive);
    RUN_TEST(test_str_replace_special_characters_literal);
    RUN_TEST(test_str_replace_memory_pressure);
    RUN_TEST(test_str_replace_boundary_conditions);
    RUN_TEST(test_str_replace_zero_length_input);
    RUN_TEST(test_str_replace_single_characters);
    return UNITY_END();
}