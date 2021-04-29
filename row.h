#ifndef EMSYS_ROW_H
#define EMSYS_ROW_H
void editorUpdateRow(erow *row);
void editorInsertRow(struct editorConfig *ed, int at, char *s, size_t len);
void editorFreeRow(erow *row);
void editorDelRow(struct editorConfig *ed, int at);
void editorRowInsertChar(struct editorConfig *ed, erow *row, int at, int c);
void editorRowInsertUnicode(struct editorConfig *ed, erow *row, int at);
void editorRowAppendString(struct editorConfig *ed, erow *row, char *s, size_t len);
void editorRowDelChar(struct editorConfig *ed, erow *row, int at);
#endif
