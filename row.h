#ifndef EMSYS_ROW_H
#define EMSYS_ROW_H
void editorUpdateRow(erow *row);
void editorInsertRow(struct editorBuffer *bufr, int at, char *s, size_t len);
void editorFreeRow(erow *row);
void editorDelRow(struct editorBuffer *bufr, int at);
void editorRowInsertChar(struct editorBuffer *bufr, erow *row, int at, int c);
void editorRowInsertUnicode(struct editorConfig *ed, struct editorBuffer *bufr, erow *row, int at);
void editorRowAppendString(struct editorBuffer *bufr, erow *row, char *s, size_t len);
void editorRowDelChar(struct editorBuffer *bufr, erow *row, int at);
#endif
