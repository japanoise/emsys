#ifndef EMSYS_UNDO_H
#define EMSYS_UNDO_H 1

#include "emsys.h"

void doUndo(int times);
void doRedo(int times);
void undoAppendChar(uint8_t c);
void undoAppendUnicode(void);
void undoBackSpace(uint8_t c);
void undoDelChar(erow *row);
struct editorUndo *newUndo();
void clearRedos(void);
void clearUndosAndRedos(void);
#endif
