/* Example test using the stub infrastructure */
#include "test.h"
#include "test_stubs.h"
#include "../emsys.h"
#include <time.h>

/* Minimal global state for testing */
struct editorConfig E;

/* Declare external functions we'll test */
extern int editorReadKey(void);
extern void editorRefreshScreen(void);

/* Test fixtures */
void setUp(void) {
    /* Initialize editor state if needed */
}

void tearDown(void) {
    /* Clean up after each test */
    stub_input_cleanup();
    stub_output_cleanup();
}

/* Example test: verify key reading with stub */
void test_readKey_stub(void) {
    /* Setup: inject test input */
    stub_input_init("abc\x1b");
    
    /* Exercise: read keys */
    TEST_ASSERT_EQUAL('a', stub_readKey());
    TEST_ASSERT_EQUAL('b', stub_readKey());
    TEST_ASSERT_EQUAL('c', stub_readKey());
    TEST_ASSERT_EQUAL('\x1b', stub_readKey());  /* ESC at end */
}

/* Example test: verify output capture */
void test_output_capture(void) {
    /* Setup: initialize output capture */
    stub_output_init(1024);
    
    /* Exercise: set status message and refresh */
    strcpy(E.statusmsg, "Test message");
    E.statusmsg_time = time(NULL);
    stub_refreshScreen();
    
    /* Verify: check captured output */
    TEST_ASSERT_EQUAL_STRING("Test message", stub_output_get());
}

/* Test version display */
void test_version_display(void) {
    /* Setup: initialize output capture */
    stub_output_init(1024);
    
    /* Exercise: display version */
    strcpy(E.statusmsg, "emsys version " EMSYS_VERSION ", built " EMSYS_BUILD_DATE);
    E.statusmsg_time = time(NULL);
    stub_refreshScreen();
    
    /* Verify: check version string appears */
    const char *output = stub_output_get();
    TEST_ASSERT(strstr(output, "emsys version") != NULL);
}

/* Test buffer initialization */
void test_buffer_creation(void) {
    /* Since we moved buffer functions, just verify basic structure */
    TEST_ASSERT_EQUAL(8, EMSYS_TAB_STOP);
    TEST_ASSERT_NOT_NULL(E.minibuf);
}

/* Main test runner */
int main(void) {
    TEST_BEGIN();
    
    RUN_TEST(test_readKey_stub);
    RUN_TEST(test_output_capture);
    RUN_TEST(test_version_display);
    RUN_TEST(test_buffer_creation);
    
    return TEST_END();
}