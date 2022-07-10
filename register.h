#ifndef REGISTER_H
#define REGISTER_H
#include "emsys.h"

void editorJumpToRegister(struct editorConfig *ed);
void editorMacroToRegister(struct editorConfig *ed);
void editorPointToRegister(struct editorConfig *ed);
void editorNumberToRegister(struct editorConfig *ed, int rept);
void editorRegionToRegister(struct editorConfig *ed, struct editorBuffer *bufr);
void editorIncrementRegister(struct editorConfig *ed, struct editorBuffer *bufr);
void editorInsertRegister(struct editorConfig *ed, struct editorBuffer *bufr);
void editorViewRegister(struct editorConfig *ed, struct editorBuffer *bufr);
void editorRectRegister(struct editorConfig *ed, struct editorBuffer *bufr);
#endif
