/* Core functionality tests for emsys - no stubs needed */
#include "test.h"
#include "../unicode.h"
#include "../wcwidth.h"
#include "../emsys.h"
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* Test UTF-8 functionality */
void test_utf8_bytes() {
    TEST_ASSERT_EQUAL_INT(1, utf8_nBytes('A'));
    TEST_ASSERT_EQUAL_INT(1, utf8_nBytes('0'));
    TEST_ASSERT_EQUAL_INT(2, utf8_nBytes(0xC2));
    TEST_ASSERT_EQUAL_INT(3, utf8_nBytes(0xE0));
    TEST_ASSERT_EQUAL_INT(4, utf8_nBytes(0xF0));
}

void test_utf8_continuation() {
    TEST_ASSERT_TRUE(utf8_isCont(0x80));
    TEST_ASSERT_TRUE(utf8_isCont(0xBF));
    TEST_ASSERT_FALSE(utf8_isCont('A'));
    TEST_ASSERT_FALSE(utf8_isCont(0xC0));
}

/* Test character width */
void test_char_width() {
    TEST_ASSERT_EQUAL_INT(1, mk_wcwidth('A'));
    TEST_ASSERT_EQUAL_INT(1, mk_wcwidth(' '));
    TEST_ASSERT_EQUAL_INT(-1, mk_wcwidth('\t'));
    TEST_ASSERT_EQUAL_INT(-1, mk_wcwidth('\n'));
    /* Control chars return 0 or -1 depending on implementation */
    TEST_ASSERT(mk_wcwidth('\0') <= 0);
}

/* Test string width calculation */
void test_string_width() {
    TEST_ASSERT_EQUAL_INT(5, stringWidth((uint8_t*)"Hello"));
    TEST_ASSERT_EQUAL_INT(0, stringWidth((uint8_t*)""));
    TEST_ASSERT_EQUAL_INT(11, stringWidth((uint8_t*)"Hello World"));
}

/* Test control character classification */
void test_control_chars() {
    /* Use the ISCTRL macro from emsys.h */
    /* Note: Current ISCTRL excludes '\0' due to (0 < c) condition */
    TEST_ASSERT_FALSE(ISCTRL('\0')); /* Current behavior excludes null */
    TEST_ASSERT_TRUE(ISCTRL('\n'));
    TEST_ASSERT_TRUE(ISCTRL('\r'));
    TEST_ASSERT_TRUE(ISCTRL('\t'));
    TEST_ASSERT_TRUE(ISCTRL(0x7f));
    
    TEST_ASSERT_FALSE(ISCTRL(' '));
    TEST_ASSERT_FALSE(ISCTRL('A'));
    TEST_ASSERT_FALSE(ISCTRL('z'));
    TEST_ASSERT_FALSE(ISCTRL('0'));
}

/* Test tab stop calculations */
void test_tab_stops() {
    #define TAB_STOP 8
    
    /* Test tab alignment calculations */
    TEST_ASSERT_EQUAL_INT(8, (0 + TAB_STOP) / TAB_STOP * TAB_STOP);
    TEST_ASSERT_EQUAL_INT(8, (7 + TAB_STOP) / TAB_STOP * TAB_STOP);
    TEST_ASSERT_EQUAL_INT(16, (8 + TAB_STOP) / TAB_STOP * TAB_STOP);
    TEST_ASSERT_EQUAL_INT(16, (9 + TAB_STOP) / TAB_STOP * TAB_STOP);
}

/* Test string operations */
void test_string_ops() {
    /* Test basic string length */
    TEST_ASSERT_EQUAL_INT(5, strlen("Hello"));
    TEST_ASSERT_EQUAL_INT(0, strlen(""));
    
    /* Test string comparison */
    TEST_ASSERT_EQUAL_INT(0, strcmp("test", "test"));
    TEST_ASSERT(strcmp("abc", "def") < 0);
    TEST_ASSERT(strcmp("xyz", "abc") > 0);
}

/* Test UTF-8 character type detection */
void test_utf8_char_types() {
    /* Single byte chars */
    TEST_ASSERT_FALSE(utf8_is2Char('A'));
    TEST_ASSERT_FALSE(utf8_is3Char('0'));
    TEST_ASSERT_FALSE(utf8_is4Char(' '));
    
    /* 2-byte UTF-8 start bytes */
    TEST_ASSERT_TRUE(utf8_is2Char(0xC2));
    TEST_ASSERT_TRUE(utf8_is2Char(0xDF));
    TEST_ASSERT_FALSE(utf8_is2Char(0xC1)); /* Invalid UTF-8 */
    TEST_ASSERT_FALSE(utf8_is2Char(0xE0));
    
    /* 3-byte UTF-8 start bytes */
    TEST_ASSERT_TRUE(utf8_is3Char(0xE0));
    TEST_ASSERT_TRUE(utf8_is3Char(0xEF));
    TEST_ASSERT_FALSE(utf8_is3Char(0xDF));
    TEST_ASSERT_FALSE(utf8_is3Char(0xF0));
    
    /* 4-byte UTF-8 start bytes */
    TEST_ASSERT_TRUE(utf8_is4Char(0xF0));
    TEST_ASSERT_TRUE(utf8_is4Char(0xF4));
    TEST_ASSERT_FALSE(utf8_is4Char(0xF5)); /* Invalid UTF-8 */
    TEST_ASSERT_FALSE(utf8_is4Char(0xEF));
}

/* Test charInStringWidth for various character types */
void test_char_in_string_width() {
    /* Control characters - should be 2 wide */
    uint8_t ctrl_str[] = "\x01\x02\x0F";
    TEST_ASSERT_EQUAL_INT(2, charInStringWidth(ctrl_str, 0));
    TEST_ASSERT_EQUAL_INT(2, charInStringWidth(ctrl_str, 1));
    TEST_ASSERT_EQUAL_INT(2, charInStringWidth(ctrl_str, 2));
    
    /* Regular ASCII - should be 1 wide */
    uint8_t ascii_str[] = "ABC";
    TEST_ASSERT_EQUAL_INT(1, charInStringWidth(ascii_str, 0));
    TEST_ASSERT_EQUAL_INT(1, charInStringWidth(ascii_str, 1));
    TEST_ASSERT_EQUAL_INT(1, charInStringWidth(ascii_str, 2));
    
    /* DEL character (0x7F) - should be 2 wide */
    uint8_t del_str[] = "\x7F";
    TEST_ASSERT_EQUAL_INT(2, charInStringWidth(del_str, 0));
}

/* Test nextScreenX function */
void test_next_screen_x() {
    int idx;
    
    /* Test tabs */
    uint8_t tab_str[] = "\t";
    idx = 0;
    TEST_ASSERT_EQUAL_INT(8, nextScreenX(tab_str, &idx, 0));
    idx = 0;
    TEST_ASSERT_EQUAL_INT(8, nextScreenX(tab_str, &idx, 5));
    idx = 0;
    TEST_ASSERT_EQUAL_INT(16, nextScreenX(tab_str, &idx, 8));
    
    /* Test regular ASCII */
    uint8_t ascii[] = "A";
    idx = 0;
    TEST_ASSERT_EQUAL_INT(1, nextScreenX(ascii, &idx, 0));
    idx = 0;
    TEST_ASSERT_EQUAL_INT(6, nextScreenX(ascii, &idx, 5));
    
    /* Test control chars */
    uint8_t ctrl[] = "\x01";
    idx = 0;
    TEST_ASSERT_EQUAL_INT(2, nextScreenX(ctrl, &idx, 0));
    
    /* Test DEL */
    uint8_t del[] = "\x7F";
    idx = 0;
    TEST_ASSERT_EQUAL_INT(2, nextScreenX(del, &idx, 0));
}

/* Test multi-byte UTF-8 sequence validation */
void test_utf8_validation() {
    /* Valid 2-byte sequence */
    uint8_t valid2[] = "\xC2\xA2"; /* ¢ cent sign */
    TEST_ASSERT_TRUE(utf8_is2Char(valid2[0]));
    TEST_ASSERT_TRUE(utf8_isCont(valid2[1]));
    
    /* Valid 3-byte sequence */
    uint8_t valid3[] = "\xE2\x82\xAC"; /* € euro sign */
    TEST_ASSERT_TRUE(utf8_is3Char(valid3[0]));
    TEST_ASSERT_TRUE(utf8_isCont(valid3[1]));
    TEST_ASSERT_TRUE(utf8_isCont(valid3[2]));
    
    /* Valid 4-byte sequence */
    uint8_t valid4[] = "\xF0\x9F\x98\x80"; /* 😀 emoji */
    TEST_ASSERT_TRUE(utf8_is4Char(valid4[0]));
    TEST_ASSERT_TRUE(utf8_isCont(valid4[1]));
    TEST_ASSERT_TRUE(utf8_isCont(valid4[2]));
    TEST_ASSERT_TRUE(utf8_isCont(valid4[3]));
}

/* Dummy functions for Unity compatibility */
void setUp(void) {}
void tearDown(void) {}

int main() {
    TEST_BEGIN();
    
    /* UTF-8 tests */
    RUN_TEST(test_utf8_bytes);
    RUN_TEST(test_utf8_continuation);
    RUN_TEST(test_utf8_char_types);
    
    /* Width tests */
    RUN_TEST(test_char_width);
    RUN_TEST(test_string_width);
    RUN_TEST(test_char_in_string_width);
    
    /* Screen position tests */
    RUN_TEST(test_next_screen_x);
    RUN_TEST(test_utf8_validation);
    
    /* Other tests */
    RUN_TEST(test_control_chars);
    RUN_TEST(test_tab_stops);
    RUN_TEST(test_string_ops);
    
    return TEST_END();
}