#ifndef TAB_H
#define TAB_H 1
#include <stdint.h>

uint8_t *tabCompleteBufferNames(struct editorConfig *ed, uint8_t *input, struct editorBuffer *currentBuffer);
uint8_t *tabCompleteFiles(uint8_t *);
void editorCompleteWord(struct editorConfig *, struct editorBuffer *);
#endif
