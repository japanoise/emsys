#ifndef EMSYS_UNDO_H
#define EMSYS_UNDO_H 1

#include"emsys.h"

void editorDoUndo(struct editorConfig *ed, struct editorBuffer *buf);
void editorDoRedo(struct editorConfig *ed, struct editorBuffer *buf);
void editorUndoAppendChar(struct editorBuffer *buf, uint8_t c);
void editorUndoAppendUnicode(struct editorConfig *ed, struct editorBuffer *buf);
void editorUndoDelChar(struct editorBuffer *buf, uint8_t c);
struct editorUndo *newUndo();
void clearRedos(struct editorBuffer *buf);
#endif
