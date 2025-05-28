#ifndef TERMINAL_H
#define TERMINAL_H

#include "emsys.h"

void disableRawMode(void);
void enableRawMode(void);
int editorReadKey(void);
int getCursorPosition(int *rows, int *cols);
int getWindowSize(int *rows, int *cols);
void editorDeserializeUnicode(void);

#endif /* TERMINAL_H */