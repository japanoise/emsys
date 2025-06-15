/* Simple tests that don't require full editor infrastructure */
#include "test.h"
#include "test_stubs.h"
#include <string.h>
#include <stdlib.h>

/* Test the stub infrastructure works */
void test_stub_input(void) {
    stub_input_init("hello");
    
    TEST_ASSERT_EQUAL('h', stub_readKey());
    TEST_ASSERT_EQUAL('e', stub_readKey());
    TEST_ASSERT_EQUAL('l', stub_readKey());
    TEST_ASSERT_EQUAL('l', stub_readKey());
    TEST_ASSERT_EQUAL('o', stub_readKey());
    TEST_ASSERT_EQUAL(EOF, stub_readKey()); /* End of input */
}

/* Test output capture */
void test_stub_output(void) {
    stub_output_init(256);
    
    stub_output_append("Test message");
    
    TEST_ASSERT_EQUAL_STRING("Test message", stub_output_get());
}

/* Test key sequence parsing */
void test_key_sequences(void) {
    /* Test escape sequences */
    stub_input_init("\x1b[A\x1b[B");  /* Up arrow, Down arrow */
    
    TEST_ASSERT_EQUAL('\x1b', stub_readKey());
    TEST_ASSERT_EQUAL('[', stub_readKey());
    TEST_ASSERT_EQUAL('A', stub_readKey());
    
    TEST_ASSERT_EQUAL('\x1b', stub_readKey());
    TEST_ASSERT_EQUAL('[', stub_readKey());
    TEST_ASSERT_EQUAL('B', stub_readKey());
}

/* Test control characters */
void test_control_chars(void) {
    stub_input_init("\x01\x05\x18\x13");  /* C-a, C-e, C-x, C-s */
    
    TEST_ASSERT_EQUAL(1, stub_readKey());   /* C-a */
    TEST_ASSERT_EQUAL(5, stub_readKey());   /* C-e */
    TEST_ASSERT_EQUAL(24, stub_readKey());  /* C-x */
    TEST_ASSERT_EQUAL(19, stub_readKey());  /* C-s */
}

void setUp(void) {
    stub_input_cleanup();
    stub_output_cleanup();
}

void tearDown(void) {
    stub_input_cleanup();
    stub_output_cleanup();
}

int main(void) {
    TEST_BEGIN();
    
    RUN_TEST(test_stub_input);
    RUN_TEST(test_stub_output);
    RUN_TEST(test_key_sequences);
    RUN_TEST(test_control_chars);
    
    return TEST_END();
}