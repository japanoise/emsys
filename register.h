#ifndef REGISTER_H
#define REGISTER_H
#include "emsys.h"

void editorJumpToRegister(struct editorConfig *ed);
void editorMacroToRegister(struct editorConfig *ed);
void editorPointToRegister(struct editorConfig *ed);
void editorNumberToRegister(struct editorConfig *ed, int rept);
void editorRegionToRegister(void);
void editorIncrementRegister(void);
void editorInsertRegister(void);
void viewRegister(void);
void editorRectRegister(void);
#endif
