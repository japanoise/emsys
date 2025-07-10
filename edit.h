#ifndef EMSYS_EDIT_H
#define EMSYS_EDIT_H

#include "emsys.h"

/* Character insertion */
void editorInsertChar(struct editorBuffer *bufr, int c, int count);
void editorInsertUnicode(struct editorBuffer *bufr, int count);

/* Line operations */
void editorInsertNewline(struct editorBuffer *bufr, int count);
void editorOpenLine(struct editorBuffer *bufr, int count);
void editorInsertNewlineAndIndent(struct editorBuffer *bufr, int count);

/* Indentation */
void editorIndent(struct editorBuffer *bufr, int rept);
void editorUnindent(struct editorBuffer *bufr, int rept);
void editorIndentTabs(struct editorConfig *ed, struct editorBuffer *buf);
void editorIndentSpaces(struct editorConfig *ed, struct editorBuffer *buf);

/* Character deletion */
void editorDelChar(struct editorBuffer *bufr, int count);
void editorBackSpace(struct editorBuffer *bufr, int count);

/* Boundary detection */
int isWordBoundary(uint8_t c);
int isParaBoundary(erow *row);

/* Cursor movement */
void editorMoveCursor(int key, int count);

/* Word movement */
void bufferEndOfForwardWord(struct editorBuffer *buf, int *dx, int *dy);
void bufferEndOfBackwardWord(struct editorBuffer *buf, int *dx, int *dy);
void editorForwardWord(int count);
void editorBackWord(int count);

/* Paragraph movement */
void editorBackPara(int count);
void editorForwardPara(int count);

/* Word transformations */
void wordTransform(struct editorBuffer *bufr, int times,
		   uint8_t *(*transformer)(uint8_t *));
void editorUpcaseWord(struct editorBuffer *bufr, int times);
void editorDowncaseWord(struct editorBuffer *bufr, int times);
void editorCapitalCaseWord(struct editorBuffer *bufr, int times);

/* Word deletion */
void editorDeleteWord(struct editorBuffer *bufr, int count);
void editorBackspaceWord(struct editorBuffer *bufr, int count);

/* Character/word transposition */
void editorTransposeWords(struct editorBuffer *bufr);
void editorTransposeChars(struct editorBuffer *bufr);

/* Line operations */
void editorKillLine(int count);
void editorKillLineBackwards(void);

/* Navigation */
void editorGotoLine(void);
void editorPageUp(int count);
void editorPageDown(int count);
void editorBeginningOfLine(int count);
void editorEndOfLine(int count);
void editorQuit(void);

/* External constants */
extern const int page_overlap;

#endif
