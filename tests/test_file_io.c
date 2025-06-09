#include "unity.h"
#include "test_helpers.h"
#include "../emsys.h"  // This defines struct editorConfig
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

// Don't declare E here - it's already declared in emsys.h as extern

void setUp(void) {
    create_test_dir();
}

void tearDown(void) {
    cleanup_test_files();
}

void test_file_operations_basic(void) {
    create_test_file("test_files/basic.txt", "Hello World\nLine 2");
    
    struct stat st;
    TEST_ASSERT_EQUAL(0, stat("test_files/basic.txt", &st));
    TEST_ASSERT_TRUE(st.st_size > 0);
    
    FILE *fp = fopen("test_files/basic.txt", "r");
    TEST_ASSERT_NOT_NULL(fp);
    
    char buffer[100];
    fgets(buffer, sizeof(buffer), fp);
    TEST_ASSERT_EQUAL_STRING("Hello World\n", buffer);
    
    fclose(fp);
}

void test_utf8_file_operations(void) {
    create_test_file("test_files/utf8.txt", "Hello 世界\nCafé €");
    
    FILE *fp = fopen("test_files/utf8.txt", "r");
    TEST_ASSERT_NOT_NULL(fp);
    
    char buffer[100];
    fgets(buffer, sizeof(buffer), fp);
    TEST_ASSERT_TRUE(strstr(buffer, "世界") != NULL);
    
    fclose(fp);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_file_operations_basic);
    RUN_TEST(test_utf8_file_operations);
    return UNITY_END();
}
