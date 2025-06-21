#ifndef TERMINAL_H
#define TERMINAL_H

void die(const char *s);
void disableRawMode(void);
void enableRawMode(void);
int getCursorPosition(int *rows, int *cols);
int getWindowSize(int *rows, int *cols);
int editorReadKey(void);
void editorDeserializeUnicode(void);

#endif /* TERMINAL_H */
