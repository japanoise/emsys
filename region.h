#ifndef EMSYS_REGION_H
#define EMSYS_REGION_H 1

#include <stdint.h>
#include "emsys.h"

int markInvalid(void);

int markInvalidSilent(void);

void setMark(void);

void clearMark(void);

void markRectangle(void);

void killRegion(void);

void copyRegion(void);

void yank(int times);

void transformRegion(uint8_t *(*transformer)(uint8_t *));

void replaceRegex(void);

void stringRectangle(void);

void copyRectangle(void);

void killRectangle(void);

void yankRectangle(void);

void markWholeBuffer(void);

#endif
