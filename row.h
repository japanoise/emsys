#ifndef EMSYS_ROW_H
#define EMSYS_ROW_H
#include "emsys.h"
void insertRow(struct editorBuffer *bufr, int at, char *s, size_t len);
void freeRow(erow *row);
void delRow(struct editorBuffer *bufr, int at);
void rowInsertChar(struct editorBuffer *bufr, erow *row, int at, int c);
void rowInsertUnicode(erow *row, int at);
void rowDelChar(struct editorBuffer *bufr, erow *row, int at);
void rowInsertString(struct editorBuffer *bufr, erow *row, int at, char *s,
		     size_t len);
void rowDeleteRange(struct editorBuffer *bufr, erow *row, int start, int end);
void rowReplaceRange(struct editorBuffer *bufr, erow *row, int start, int end,
		     char *s, size_t len);
int calculateLineWidth(erow *row);
int charsToDisplayColumn(erow *row, int chars_idx);
int displayColumnToChars(erow *row, int display_col);
#endif
