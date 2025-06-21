#ifndef EMSYS_BUFFER_H
#define EMSYS_BUFFER_H
#include "emsys.h"
void updateRow(erow *row);
void editorInsertRow(struct editorBuffer *bufr, int at, char *s, size_t len);
void freeRow(erow *row);
void editorDelRow(struct editorBuffer *bufr, int at);
void rowInsertChar(struct editorBuffer *bufr, erow *row, int at, int c);
void editorRowInsertUnicode(struct editorConfig *ed, struct editorBuffer *bufr,
			    erow *row, int at);
void rowAppendString(struct editorBuffer *bufr, erow *row, char *s, size_t len);
void rowDelChar(struct editorBuffer *bufr, erow *row, int at);
struct editorBuffer *newBuffer(void);
void destroyBuffer(struct editorBuffer *buf);
void editorUpdateBuffer(struct editorBuffer *buf);
void editorSwitchToNamedBuffer(struct editorConfig *ed,
			       struct editorBuffer *current);
void editorNextBuffer(void);
void editorPreviousBuffer(void);
void editorKillBuffer(void);
void invalidateScreenCache(struct editorBuffer *buf);
void buildScreenCache(struct editorBuffer *buf);
int getScreenLineForRow(struct editorBuffer *buf, int row);
int calculateLineWidth(erow *row);
int charsToDisplayColumn(erow *row, int char_pos);
#endif
