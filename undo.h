#ifndef EMSYS_UNDO_H
#define EMSYS_UNDO_H 1

#include "emsys.h"

void doUndo(struct editorBuffer *buf);
void doRedo(struct editorBuffer *buf);
void undoAppendChar(struct editorBuffer *buf, uint8_t c);
void undoAppendUnicode(void);
void undoBackSpace(struct editorBuffer *buf, uint8_t c);
void undoDelChar(struct editorBuffer *buf, erow *row);
struct editorUndo *newUndo();
void clearRedos(struct editorBuffer *buf);
void clearUndosAndRedos(struct editorBuffer *buf);
#endif
