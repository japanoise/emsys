#ifndef EMSYS_FIND_H
#define EMSYS_FIND_H
#include <stdint.h>
#include "emsys.h"
void editorFindCallback(struct editorBuffer *bufr, uint8_t *query, int key);
void editorFind(struct editorBuffer *bufr);
#endif
