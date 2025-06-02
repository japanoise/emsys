#ifndef EMSYS_REGION_H
#define EMSYS_REGION_H 1

#include <stdint.h>
#include "emsys.h"

int markInvalid(struct editorBuffer *buf);

int markInvalidSilent(struct editorBuffer *buf);

void setMark(struct editorBuffer *buf);

void clearMark(struct editorBuffer *buf);

void markRectangle(struct editorBuffer *buf);

void killRegion(void);

void copyRegion(void);

void yank(void);

void transformRegion(uint8_t *(*transformer)(uint8_t *));

void replaceRegex(void);

void stringRectangle(void);

void copyRectangle(void);

void killRectangle(void);

void yankRectangle(void);

void markWholeBuffer(struct editorBuffer *buf);

#endif
