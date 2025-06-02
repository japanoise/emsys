#ifndef EMSYS_FIND_H
#define EMSYS_FIND_H
#include <stdint.h>
#include "emsys.h"
void findCallback(struct editorBuffer *bufr, uint8_t *query, int key);
void find(struct editorBuffer *bufr);
void regexFind(struct editorBuffer *bufr);
#endif
