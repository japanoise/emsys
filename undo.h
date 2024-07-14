#ifndef EMSYS_UNDO_H
#define EMSYS_UNDO_H 1

#include "emsys.h"

void editorDoUndo(struct editorBuffer *buf);
void editorDoRedo(struct editorBuffer *buf);
void editorUndoAppendChar(struct editorBuffer *buf, uint8_t c);
void editorUndoAppendUnicode(struct editorConfig *ed, struct editorBuffer *buf);
void editorUndoBackSpace(struct editorBuffer *buf, uint8_t c);
void editorUndoDelChar(struct editorBuffer *buf, erow *row);
struct editorUndo *newUndo();
void clearRedos(struct editorBuffer *buf);
void clearUndosAndRedos(struct editorBuffer *buf);
#endif
