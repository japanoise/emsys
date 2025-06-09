#include "unity.h"
#include "../unicode.h"

void setUp(void) {}
void tearDown(void) {}

void test_utf8_nBytes_ascii(void) {
    TEST_ASSERT_EQUAL(1, utf8_nBytes('A'));
    TEST_ASSERT_EQUAL(1, utf8_nBytes('z'));
    TEST_ASSERT_EQUAL(1, utf8_nBytes('0'));
}

void test_utf8_nBytes_multibyte(void) {
    TEST_ASSERT_EQUAL(2, utf8_nBytes(0xC2));
    TEST_ASSERT_EQUAL(3, utf8_nBytes(0xE0));
    TEST_ASSERT_EQUAL(4, utf8_nBytes(0xF0));
}

void test_utf8_nBytes_invalid(void) {
    TEST_ASSERT_EQUAL(1, utf8_nBytes(0xFF));
    TEST_ASSERT_EQUAL(1, utf8_nBytes(0xC0));
    TEST_ASSERT_EQUAL(1, utf8_nBytes(0xC1));
    TEST_ASSERT_EQUAL(1, utf8_nBytes(0xF5));
}

void test_utf8_isCont_basic(void) {
    TEST_ASSERT_TRUE(utf8_isCont(0x80));
    TEST_ASSERT_TRUE(utf8_isCont(0x8F));
    TEST_ASSERT_TRUE(utf8_isCont(0xBF));
    
    TEST_ASSERT_FALSE(utf8_isCont(0x7F));
    TEST_ASSERT_FALSE(utf8_isCont(0xC0));
    TEST_ASSERT_FALSE(utf8_isCont('A'));
}

void test_stringWidth_basic(void) {
    TEST_ASSERT_EQUAL(5, stringWidth((uint8_t*)"hello"));
    TEST_ASSERT_EQUAL(0, stringWidth((uint8_t*)""));
    TEST_ASSERT_EQUAL(1, stringWidth((uint8_t*)"a"));
}

void test_stringWidth_with_tabs(void) {
    uint8_t *tab_string = (uint8_t*)"a\tb";
    int width = stringWidth(tab_string);
    TEST_ASSERT_TRUE(width > 3);
}

void test_charInStringWidth_control_chars(void) {
    uint8_t ctrl_a[] = {0x01, 0};
    TEST_ASSERT_EQUAL(2, charInStringWidth(ctrl_a, 0));
    
    uint8_t del[] = {0x7F, 0};
    TEST_ASSERT_EQUAL(2, charInStringWidth(del, 0));
}

void test_utf8_malformed_sequences(void) {
    TEST_ASSERT_TRUE(utf8_isCont(0x80));
    TEST_ASSERT_EQUAL(1, utf8_nBytes(0x80));
    
    uint8_t incomplete[] = {0xE0, 0x80, 0};
    TEST_ASSERT_EQUAL(3, utf8_nBytes(incomplete[0]));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_utf8_nBytes_ascii);
    RUN_TEST(test_utf8_nBytes_multibyte);
    RUN_TEST(test_utf8_nBytes_invalid);
    RUN_TEST(test_utf8_isCont_basic);
    RUN_TEST(test_stringWidth_basic);
    RUN_TEST(test_stringWidth_with_tabs);
    RUN_TEST(test_charInStringWidth_control_chars);
    RUN_TEST(test_utf8_malformed_sequences);
    return UNITY_END();
}