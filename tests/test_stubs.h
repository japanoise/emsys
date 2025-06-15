/* Test stubs for isolating I/O during unit tests */
#ifndef TEST_STUBS_H
#define TEST_STUBS_H

#include <stddef.h>

/* Stub types */
typedef struct {
    const char *keys;      /* String of keys to inject */
    size_t index;         /* Current position in keys */
    int enabled;          /* Whether stub is active */
} InputStub;

typedef struct {
    char *buffer;         /* Captured output */
    size_t size;          /* Buffer size */
    size_t pos;           /* Current position */
    int enabled;          /* Whether stub is active */
} OutputStub;

/* Global stubs */
extern InputStub g_input_stub;
extern OutputStub g_output_stub;

/* Stub control functions */
void stub_input_init(const char *keys);
void stub_input_cleanup(void);
int stub_input_enabled(void);

void stub_output_init(size_t buffer_size);
void stub_output_cleanup(void);
int stub_output_enabled(void);
const char *stub_output_get(void);

/* Stubbed versions of terminal.c functions */
int stub_readKey(void);

/* Stubbed versions of display.c functions */
void stub_refreshScreen(void);

#endif /* TEST_STUBS_H */