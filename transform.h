#ifndef EMSYS_TRANSFORM_H
#define EMSYS_TRANSFORM_H
#include <stdint.h>

struct editorConfig;
struct editorBuffer;

uint8_t *transformerUpcase(uint8_t *);
uint8_t *transformerDowncase(uint8_t *);
uint8_t *transformerCapitalCase(uint8_t *);
uint8_t *transformerTransposeWords(uint8_t *);
uint8_t *transformerTransposeChars(uint8_t *);
void editorCapitalizeRegion(struct editorConfig *ed, struct editorBuffer *buf);
void editorWhitespaceCleanup(struct editorConfig *ed, struct editorBuffer *buf);
#endif
