#ifndef EMSYS_EDIT_H
#define EMSYS_EDIT_H

#include "emsys.h"

/* Character insertion */
void editorInsertChar(struct editorBuffer *bufr, int c);
void editorInsertUnicode(struct editorBuffer *bufr);

/* Line operations */
void editorInsertNewline(struct editorBuffer *bufr);
void editorOpenLine(struct editorBuffer *bufr);
void editorInsertNewlineAndIndent(struct editorBuffer *bufr);

/* Indentation */
void editorIndent(struct editorBuffer *bufr, int rept);
void editorUnindent(struct editorBuffer *bufr, int rept);
void editorIndentTabs(struct editorConfig *ed, struct editorBuffer *buf);
void editorIndentSpaces(struct editorConfig *ed, struct editorBuffer *buf);

/* Character deletion */
void editorDelChar(struct editorBuffer *bufr);
void editorBackSpace(struct editorBuffer *bufr);

/* Boundary detection */
int isWordBoundary(uint8_t c);
int isParaBoundary(erow *row);

#endif