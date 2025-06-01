#ifndef EMSYS_ROW_H
#define EMSYS_ROW_H
#include "emsys.h"
void editorInsertRow(struct editorBuffer *bufr, int at, char *s, size_t len);
void editorFreeRow(erow *row);
void editorDelRow(struct editorBuffer *bufr, int at);
void editorRowInsertChar(struct editorBuffer *bufr, erow *row, int at, int c);
void editorRowInsertUnicode(struct editorConfig *ed, struct editorBuffer *bufr,
			    erow *row, int at);
void editorRowDelChar(struct editorBuffer *bufr, erow *row, int at);
void editorRowInsertString(struct editorBuffer *bufr, erow *row, int at,
			   char *s, size_t len);
void editorRowDeleteRange(struct editorBuffer *bufr, erow *row, int start,
			  int end);
void editorRowReplaceRange(struct editorBuffer *bufr, erow *row, int start,
			   int end, char *s, size_t len);
int calculateLineWidth(erow *row);
int charsToDisplayColumn(erow *row, int chars_idx);
int displayColumnToChars(erow *row, int display_col);
#endif
