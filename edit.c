#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "emsys.h"
#include "edit.h"
#include "buffer.h"
#include "undo.h"
#include "unicode.h"
#include "display.h"
#include "keymap.h"
#include "unused.h"

extern struct editorConfig E;

/* Character insertion */

void editorInsertChar(struct editorBuffer *bufr, int c) {
	if (bufr->cy == bufr->numrows) {
		editorInsertRow(bufr, bufr->numrows, "", 0);
	}
	editorRowInsertChar(bufr, &bufr->row[bufr->cy], bufr->cx, c);
	bufr->cx++;
}

void editorInsertUnicode(struct editorBuffer *bufr) {
	if (bufr->cy == bufr->numrows) {
		editorInsertRow(bufr, bufr->numrows, "", 0);
	}
	editorRowInsertUnicode(&E, bufr, &bufr->row[bufr->cy], bufr->cx);
	bufr->cx += E.nunicode;
}

/* Line operations */

void editorIndentTabs(struct editorConfig *UNUSED(ed),
		      struct editorBuffer *buf) {
	buf->indent = 0;
	editorSetStatusMessage("Indentation set to tabs");
}

void editorIndentSpaces(struct editorConfig *UNUSED(ed),
			struct editorBuffer *buf) {
	uint8_t *indentS =
		editorPrompt(buf, "Set indentation to: %s", PROMPT_BASIC, NULL);
	if (indentS == NULL) {
		goto cancel;
	}
	int indent = atoi((char *)indentS);
	free(indentS);
	if (indent <= 0) {
cancel:
		editorSetStatusMessage("Canceled.");
		return;
	}
	buf->indent = indent;
	editorSetStatusMessage("Indentation set to %i spaces", indent);
}

void editorInsertNewline(struct editorBuffer *bufr) {
	if (bufr->cx == 0) {
		editorInsertRow(bufr, bufr->cy, "", 0);
	} else {
		erow *row = &bufr->row[bufr->cy];
		editorInsertRow(bufr, bufr->cy + 1, &row->chars[bufr->cx],
				row->size - bufr->cx);
		row = &bufr->row[bufr->cy];
		row->size = bufr->cx;
		row->chars[row->size] = '\0';
		editorUpdateRow(row);
	}
	bufr->cy++;
	bufr->cx = 0;
}

void editorOpenLine(struct editorBuffer *bufr) {
	int ccx = bufr->cx;
	int ccy = bufr->cy;
	editorInsertNewline(bufr);
	bufr->cx = ccx;
	bufr->cy = ccy;
}

void editorInsertNewlineAndIndent(struct editorBuffer *bufr) {
	editorUndoAppendChar(bufr, '\n');
	editorInsertNewline(bufr);
	int i = 0;
	uint8_t c = bufr->row[bufr->cy - 1].chars[i];
	while (c == ' ' || c == CTRL('i')) {
		editorUndoAppendChar(bufr, c);
		editorInsertChar(bufr, c);
		c = bufr->row[bufr->cy - 1].chars[++i];
	}
}

/* Indentation */

void editorIndent(struct editorBuffer *bufr, int rept) {
	int ocx = bufr->cx;
	int indWidth = 1;
	if (bufr->indent) {
		indWidth = bufr->indent;
	}
	bufr->cx = 0;
	for (int i = 0; i < rept; i++) {
		if (bufr->indent) {
			for (int i = 0; i < bufr->indent; i++) {
				editorUndoAppendChar(bufr, ' ');
				editorInsertChar(bufr, ' ');
			}
		} else {
			editorUndoAppendChar(bufr, '\t');
			editorInsertChar(bufr, '\t');
		}
	}
	bufr->cx = ocx + indWidth * rept;
}

void editorUnindent(struct editorBuffer *bufr, int rept) {
	if (bufr->cy >= bufr->numrows) {
		editorSetStatusMessage("End of buffer.");
		return;
	}

	/* Setup for indent mode */
	int indWidth = 1;
	char indCh = '\t';
	struct erow *row = &bufr->row[bufr->cy];
	if (bufr->indent) {
		indWidth = bufr->indent;
		indCh = ' ';
	}

	/* Calculate size of unindent */
	int trunc = 0;
	for (int i = 0; i < rept; i++) {
		for (int j = 0; j < indWidth; j++) {
			if (row->chars[trunc] != indCh)
				goto UNINDENT_PERFORM;
			trunc++;
		}
	}

UNINDENT_PERFORM:
	if (trunc == 0)
		return;

	/* Create undo */
	struct editorUndo *new = newUndo();
	new->prev = bufr->undo;
	new->startx = 0;
	new->starty = bufr->cy;
	new->endx = trunc;
	new->endy = bufr->cy;
	new->delete = 1;
	new->append = 0;
	bufr->undo = new;
	if (new->datasize < trunc - 1) {
		new->datasize = trunc + 1;
		new->data = realloc(new->data, new->datasize);
	}
	memset(new->data, indCh, trunc);
	new->data[trunc] = 0;
	new->datalen = trunc;

	/* Perform row operation & dirty buffer */
	memmove(&row->chars[0], &row->chars[trunc], row->size - trunc);
	row->size -= trunc;
	bufr->cx -= trunc;
	editorUpdateRow(row);
	bufr->dirty = 1;
}

/* Character deletion */

void editorDelChar(struct editorBuffer *bufr) {
	if (bufr->cy == bufr->numrows)
		return;
	if (bufr->cy == bufr->numrows - 1 &&
	    bufr->cx == bufr->row[bufr->cy].size)
		return;

	erow *row = &bufr->row[bufr->cy];
	editorUndoDelChar(bufr, row);
	if (bufr->cx == row->size) {
		row = &bufr->row[bufr->cy + 1];
		editorRowAppendString(bufr, &bufr->row[bufr->cy], row->chars,
				      row->size);
		editorDelRow(bufr, bufr->cy + 1);
	} else {
		editorRowDelChar(bufr, row, bufr->cx);
	}
}

/* Boundary detection */

int isWordBoundary(uint8_t c) {
	return !(c > '~') && /* Anything outside ASCII is not a boundary */
	       !('a' <= c && c <= 'z') && /* Lower case ASCII not boundaries */
	       !('A' <= c && c <= 'Z') && /* Same with caps */
	       !('0' <= c && c <= '9') && /* And numbers */
	       ((c < '$') ||		  /* ctrl chars & some punctuation */
		(c > '%')); /* Rest of ascii outside $% & other ranges */
}

int isParaBoundary(erow *row) {
	return (row->size == 0);
}

void editorBackSpace(struct editorBuffer *bufr) {
	if (!bufr->numrows)
		return;
	if (bufr->cy == bufr->numrows) {
		bufr->cx = bufr->row[--bufr->cy].size;
		return;
	}
	if (bufr->cy == 0 && bufr->cx == 0)
		return;

	erow *row = &bufr->row[bufr->cy];
	if (bufr->cx > 0) {
		do {
			bufr->cx--;
			editorUndoBackSpace(bufr, row->chars[bufr->cx]);
		} while (utf8_isCont(row->chars[bufr->cx]));
		editorRowDelChar(bufr, row, bufr->cx);
	} else {
		editorUndoBackSpace(bufr, '\n');
		bufr->cx = bufr->row[bufr->cy - 1].size;
		editorRowAppendString(bufr, &bufr->row[bufr->cy - 1],
				      row->chars, row->size);
		editorDelRow(bufr, bufr->cy);
		bufr->cy--;
	}
}