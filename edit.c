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
#include "transform.h"
#include "region.h"
#include "prompt.h"
#include "terminal.h"
#include "util.h"

extern struct editorConfig E;

/* Character insertion */

void editorInsertChar(struct editorBuffer *bufr, int c, int count) {
	
	if (count <= 0)
		count = 1;

	for (int i = 0; i < count; i++) {
		if (bufr->cy == bufr->numrows) {
			editorInsertRow(bufr, bufr->numrows, "", 0);
		}
		rowInsertChar(bufr, &bufr->row[bufr->cy], bufr->cx, c);
		bufr->cx++;
	}
}

void editorInsertUnicode(struct editorBuffer *bufr, int count) {
	int times = count ? count : 1;
	for (int i = 0; i < times; i++) {
		editorUndoAppendUnicode(&E, bufr);
		if (bufr->cy == bufr->numrows) {
			editorInsertRow(bufr, bufr->numrows, "", 0);
		}
		editorRowInsertUnicode(&E, bufr, &bufr->row[bufr->cy],
				       bufr->cx);
		bufr->cx += E.nunicode;
	}
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

void editorInsertNewline(struct editorBuffer *bufr, int count) {
	int times = count ? count : 1;
	for (int i = 0; i < times; i++) {
		editorUndoAppendChar(bufr, '\n');
		if (bufr->cx == 0) {
			editorInsertRow(bufr, bufr->cy, "", 0);
		} else {
			erow *row = &bufr->row[bufr->cy];
			editorInsertRow(bufr, bufr->cy + 1,
					&row->chars[bufr->cx],
					row->size - bufr->cx);
			row = &bufr->row[bufr->cy];
			row->size = bufr->cx;
			row->chars[row->size] = '\0';
			row->render_valid = 0;
		}
		bufr->cy++;
		bufr->cx = 0;
	}
}

void editorOpenLine(struct editorBuffer *bufr, int count) {
	if (count <= 0)
		count = 1;

	for (int i = 0; i < count; i++) {
		int ccx = bufr->cx;
		int ccy = bufr->cy;
		editorInsertNewline(bufr, 1);
		bufr->cx = ccx;
		bufr->cy = ccy;
	}
}

void editorInsertNewlineAndIndent(struct editorBuffer *bufr, int count) {
	if (count <= 0)
		count = 1;

	for (int j = 0; j < count; j++) {
		editorUndoAppendChar(bufr, '\n');
		editorInsertNewline(bufr, 1);
		int i = 0;
		uint8_t c = bufr->row[bufr->cy - 1].chars[i];
		while (c == ' ' || c == CTRL('i')) {
			editorUndoAppendChar(bufr, c);
			editorInsertChar(bufr, c, 1);
			c = bufr->row[bufr->cy - 1].chars[++i];
		}
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
				editorInsertChar(bufr, ' ', 1);
			}
		} else {
			editorUndoAppendChar(bufr, '\t');
			editorInsertChar(bufr, '\t', 1);
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
		new->data = xrealloc(new->data, new->datasize);
	}
	memset(new->data, indCh, trunc);
	new->data[trunc] = 0;
	new->datalen = trunc;

	/* Perform row operation & dirty buffer */
	memmove(&row->chars[0], &row->chars[trunc], row->size - trunc);
	row->size -= trunc;
	bufr->cx -= trunc;
	updateRow(row);
	bufr->dirty = 1;
}

/* Character deletion */

void editorDelChar(struct editorBuffer *bufr, int count) {
	int times = count ? count : 1;
	for (int i = 0; i < times; i++) {
		if (bufr->cy == bufr->numrows)
			return;
		if (bufr->cy == bufr->numrows - 1 &&
		    bufr->cx == bufr->row[bufr->cy].size)
			return;

		erow *row = &bufr->row[bufr->cy];
		editorUndoDelChar(bufr, row);
		if (bufr->cx == row->size) {
			row = &bufr->row[bufr->cy + 1];
			rowAppendString(bufr, &bufr->row[bufr->cy], row->chars,
					row->size);
			editorDelRow(bufr, bufr->cy + 1);
		} else {
			rowDelChar(bufr, row, bufr->cx);
		}
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

void editorBackSpace(struct editorBuffer *bufr, int count) {
	int times = count ? count : 1;
	for (int i = 0; i < times; i++) {
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
			rowDelChar(bufr, row, bufr->cx);
		} else {
			editorUndoBackSpace(bufr, '\n');
			bufr->cx = bufr->row[bufr->cy - 1].size;
			rowAppendString(bufr, &bufr->row[bufr->cy - 1],
					row->chars, row->size);
			editorDelRow(bufr, bufr->cy);
			bufr->cy--;
		}
	}
}

/* Cursor movement */

void editorMoveCursor(int key, int count) {
	int times = count ? count : 1;
	for (int i = 0; i < times; i++) {
		erow *row = (E.buf->cy >= E.buf->numrows) ?
				    NULL :
				    &E.buf->row[E.buf->cy];

		switch (key) {
		case ARROW_LEFT:
			if (E.buf->cx != 0) {
				do
					E.buf->cx--;
				while (E.buf->cx != 0 &&
				       utf8_isCont(row->chars[E.buf->cx]));
			} else if (E.buf->cy > 0) {
				E.buf->cy--;
				E.buf->cx = E.buf->row[E.buf->cy].size;
			}
			break;

		case ARROW_RIGHT:
			if (row && E.buf->cx < row->size) {
				E.buf->cx += utf8_nBytes(row->chars[E.buf->cx]);
			} else if (row && E.buf->cx == row->size) {
				E.buf->cy++;
				E.buf->cx = 0;
			}
			break;
		case ARROW_UP:
			if (E.buf->cy > 0) {
				E.buf->cy--;
				if (E.buf->row[E.buf->cy].chars == NULL)
					break;
				while (utf8_isCont(
					E.buf->row[E.buf->cy].chars[E.buf->cx]))
					E.buf->cx++;
			}
			break;
		case ARROW_DOWN:
			if (E.buf->cy < E.buf->numrows) {
				E.buf->cy++;
				if (E.buf->cy < E.buf->numrows) {
					if (E.buf->row[E.buf->cy].chars == NULL)
						break;
					while (E.buf->cx < E.buf->row[E.buf->cy]
								   .size &&
					       utf8_isCont(
						       E.buf->row[E.buf->cy]
							       .chars[E.buf->cx]))
						E.buf->cx++;
				} else {
					E.buf->cx = 0;
				}
			}
			break;
		}
		row = (E.buf->cy >= E.buf->numrows) ? NULL :
						      &E.buf->row[E.buf->cy];
		int rowlen = row ? row->size : 0;
		if (E.buf->cx > rowlen) {
			E.buf->cx = rowlen;
		}
	}
}

/* Word movement */

void bufferEndOfForwardWord(struct editorBuffer *buf, int *dx, int *dy) {
	int cx = buf->cx;
	int icy = buf->cy;
	if (icy >= buf->numrows) {
		*dx = cx;
		*dy = icy;
		return;
	}
	int pre = 1;
	for (int cy = icy; cy < buf->numrows; cy++) {
		int l = buf->row[cy].size;
		while (cx < l) {
			uint8_t c = buf->row[cy].chars[cx];
			if (isWordBoundary(c) && !pre) {
				*dx = cx;
				*dy = cy;
				return;
			} else if (!isWordBoundary(c)) {
				pre = 0;
			}
			cx++;
		}
		if (!pre) {
			*dx = cx;
			*dy = cy;
			return;
		}
		cx = 0;
	}
	*dx = cx;
	*dy = icy;
}

void bufferEndOfBackwardWord(struct editorBuffer *buf, int *dx, int *dy) {
	int cx = buf->cx;
	int icy = buf->cy;

	if (icy >= buf->numrows) {
		return;
	}

	int pre = 1;

	for (int cy = icy; cy >= 0; cy--) {
		if (cy != icy) {
			cx = buf->row[cy].size;
		}
		while (cx > 0) {
			uint8_t c = buf->row[cy].chars[cx - 1];
			if (isWordBoundary(c) && !pre) {
				*dx = cx;
				*dy = cy;
				return;
			} else if (!isWordBoundary(c)) {
				pre = 0;
			}
			cx--;
		}
		if (!pre) {
			*dx = cx;
			*dy = cy;
			return;
		}
	}

	*dx = cx;
	*dy = 0;
}

void editorForwardWord(int count) {
	int times = count ? count : 1;
	for (int i = 0; i < times; i++) {
		bufferEndOfForwardWord(E.buf, &E.buf->cx, &E.buf->cy);
	}
}

void editorBackWord(int count) {
	int times = count ? count : 1;
	for (int i = 0; i < times; i++) {
		bufferEndOfBackwardWord(E.buf, &E.buf->cx, &E.buf->cy);
	}
}

/* Paragraph movement */

void editorBackPara(int count) {
	int times = count ? count : 1;
	for (int i = 0; i < times; i++) {
		E.buf->cx = 0;
		int icy = E.buf->cy;

		if (icy >= E.buf->numrows) {
			icy--;
		}

		if (E.buf->numrows == 0) {
			return;
		}

		int pre = 1;

		for (int cy = icy; cy >= 0; cy--) {
			erow *row = &E.buf->row[cy];
			if (isParaBoundary(row) && !pre) {
				E.buf->cy = cy;
				return;
			} else if (!isParaBoundary(row)) {
				pre = 0;
			}
		}

		E.buf->cy = 0;
	}
}

void editorForwardPara(int count) {
	int times = count ? count : 1;
	for (int i = 0; i < times; i++) {
		E.buf->cx = 0;
		int icy = E.buf->cy;

		if (icy >= E.buf->numrows) {
			return;
		}

		if (E.buf->numrows == 0) {
			return;
		}

		int pre = 1;

		for (int cy = icy; cy < E.buf->numrows; cy++) {
			erow *row = &E.buf->row[cy];
			if (isParaBoundary(row) && !pre) {
				E.buf->cy = cy;
				return;
			} else if (!isParaBoundary(row)) {
				pre = 0;
			}
		}

		E.buf->cy = E.buf->numrows;
	}
}

/* Word transformations */

void wordTransform(struct editorBuffer *bufr, int times,
		   uint8_t *(*transformer)(uint8_t *)) {
	int icx = bufr->cx;
	int icy = bufr->cy;
	for (int i = 0; i < times; i++) {
		bufferEndOfForwardWord(bufr, &bufr->cx, &bufr->cy);
	}
	bufr->markx = icx;
	bufr->marky = icy;
	editorTransformRegion(&E, bufr, transformer);
}

void editorUpcaseWord(struct editorBuffer *bufr, int times) {
	wordTransform(bufr, times, transformerUpcase);
}

void editorDowncaseWord(struct editorBuffer *bufr, int times) {
	wordTransform(bufr, times, transformerDowncase);
}

void editorCapitalCaseWord(struct editorBuffer *bufr, int times) {
	wordTransform(bufr, times, transformerCapitalCase);
}

/* Word deletion */

void editorDeleteWord(struct editorBuffer *bufr, int count) {
	int times = count ? count : 1;
	for (int i = 0; i < times; i++) {
		int origMarkx = bufr->markx;
		int origMarky = bufr->marky;
		bufferEndOfForwardWord(bufr, &bufr->markx, &bufr->marky);
		editorKillRegion(&E, bufr);
		bufr->markx = origMarkx;
		bufr->marky = origMarky;
	}
}

void editorBackspaceWord(struct editorBuffer *bufr, int count) {
	int times = count ? count : 1;
	for (int i = 0; i < times; i++) {
		int origMarkx = bufr->markx;
		int origMarky = bufr->marky;
		bufferEndOfBackwardWord(bufr, &bufr->markx, &bufr->marky);
		editorKillRegion(&E, bufr);
		bufr->markx = origMarkx;
		bufr->marky = origMarky;
	}
}

/* Character/word transposition */

void editorTransposeWords(struct editorBuffer *bufr) {
	if (bufr->numrows == 0) {
		editorSetStatusMessage("Buffer is empty");
		return;
	}

	if (bufr->cx == 0 && bufr->cy == 0) {
		editorSetStatusMessage("Beginning of buffer");
		return;
	} else if (bufr->cy >= bufr->numrows ||
		   (bufr->cy == bufr->numrows - 1 &&
		    bufr->cx == bufr->row[bufr->cy].size)) {
		editorSetStatusMessage("End of buffer");
		return;
	}

	int startcx, startcy, endcx, endcy;
	bufferEndOfBackwardWord(bufr, &startcx, &startcy);
	bufferEndOfForwardWord(bufr, &endcx, &endcy);
	if ((startcx == bufr->cx && bufr->cy == startcy) ||
	    (endcx == bufr->cx && bufr->cy == endcy)) {
		editorSetStatusMessage("Cannot transpose here");
		return;
	}
	bufr->cx = startcx;
	bufr->cy = startcy;
	bufr->markx = endcx;
	bufr->marky = endcy;

	editorTransformRegion(&E, bufr, transformerTransposeWords);
}

void editorTransposeChars(struct editorBuffer *bufr) {
	if (bufr->numrows == 0) {
		editorSetStatusMessage("Buffer is empty");
		return;
	}

	if (bufr->cx == 0 && bufr->cy == 0) {
		editorSetStatusMessage("Beginning of buffer");
		return;
	} else if (bufr->cy >= bufr->numrows ||
		   (bufr->cy == bufr->numrows - 1 &&
		    bufr->cx == bufr->row[bufr->cy].size)) {
		editorSetStatusMessage("End of buffer");
		return;
	}

	int startcx, startcy;
	editorMoveCursor(ARROW_LEFT, 1);
	startcx = bufr->cx;
	startcy = bufr->cy;
	editorMoveCursor(ARROW_RIGHT, 1);
	editorMoveCursor(ARROW_RIGHT, 1);
	bufr->markx = startcx;
	bufr->marky = startcy;
	editorTransformRegion(&E, bufr, transformerTransposeChars);
}

/* Line operations */

void editorKillLine(int count) {
	int times = count ? count : 1;
	for (int i = 0; i < times; i++) {
		if (E.buf->numrows <= 0) {
			return;
		}

		erow *row = &E.buf->row[E.buf->cy];

		if (E.buf->cx == row->size) {
			editorDelChar(E.buf, 1);
		} else {
			// Copy to kill ring
			int kill_len = row->size - E.buf->cx;
			free(E.kill);
			E.kill = xmalloc(kill_len + 1);
			memcpy(E.kill, &row->chars[E.buf->cx], kill_len);
			E.kill[kill_len] = '\0';

			clearRedos(E.buf);
			struct editorUndo *new = newUndo();
			new->starty = E.buf->cy;
			new->endy = E.buf->cy;
			new->startx = E.buf->cx;
			new->endx = row->size;
			new->delete = 1;
			new->prev = E.buf->undo;
			E.buf->undo = new;

			new->datalen = kill_len;
			if (new->datasize < new->datalen + 1) {
				new->datasize = new->datalen + 1;
				new->data = xrealloc(new->data, new->datasize);
			}
			for (int i = 0; i < kill_len; i++) {
				new->data[i] = E.kill[kill_len - i - 1];
			}
			new->data[kill_len] = '\0';

			row->size = E.buf->cx;
			row->chars[row->size] = '\0';
			row->render_valid = 0;
			E.buf->dirty = 1;
			editorClearMark();
		}
	}
}

void editorKillLineBackwards(void) {
	if (E.buf->cx == 0) {
		return;
	}

	erow *row = &E.buf->row[E.buf->cy];

	// Copy to kill ring
	free(E.kill);
	E.kill = xmalloc(E.buf->cx + 1);
	memcpy(E.kill, row->chars, E.buf->cx);
	E.kill[E.buf->cx] = '\0';

	clearRedos(E.buf);
	struct editorUndo *new = newUndo();
	new->starty = E.buf->cy;
	new->endy = E.buf->cy;
	new->startx = 0;
	new->endx = E.buf->cx;
	new->delete = 1;
	new->prev = E.buf->undo;
	E.buf->undo = new;

	new->datalen = E.buf->cx;
	if (new->datasize < new->datalen + 1) {
		new->datasize = new->datalen + 1;
		new->data = xrealloc(new->data, new->datasize);
	}
	for (int i = 0; i < E.buf->cx; i++) {
		new->data[i] = E.kill[E.buf->cx - i - 1];
	}
	new->data[E.buf->cx] = '\0';

	row->size -= E.buf->cx;
	memmove(row->chars, &row->chars[E.buf->cx], row->size);
	row->chars[row->size] = '\0';
	updateRow(row);
	E.buf->cx = 0;
	E.buf->dirty = 1;
}

/* Navigation */

void editorPageUp(int count) {
	struct editorWindow *win = E.windows[windowFocusedIdx()];
	int times = count ? count : 1;

	for (int n = 0; n < times; n++) {
		int scroll_lines = win->height - page_overlap;
		if (scroll_lines < 1) scroll_lines = 1;
		
		if (E.buf->truncate_lines) {
			/* Move view up by scroll_lines */
			win->rowoff -= scroll_lines;
			if (win->rowoff < 0) {
				win->rowoff = 0;
			}
			
			/* Ensure cursor is within visible window */
			if (E.buf->cy >= win->rowoff + win->height) {
				/* Cursor is below window - move it to bottom of window */
				E.buf->cy = win->rowoff + win->height - 1;
			}
			/* If cursor is above window, it's already visible */
		} else {
			/* In wrapped mode, need to handle variable line heights */
			int lines_scrolled = 0;
			int new_rowoff = win->rowoff;
			
			/* Scroll up by the desired number of screen lines */
			while (lines_scrolled < scroll_lines && new_rowoff > 0) {
				new_rowoff--;
				int line_height = (calculateLineWidth(&E.buf->row[new_rowoff]) / E.screencols) + 1;
				lines_scrolled += line_height;
			}
			win->rowoff = new_rowoff;
			
			/* Ensure cursor is visible - calculate screen position */
			int cursor_screen_line = getScreenLineForRow(E.buf, E.buf->cy);
			int window_start_screen_line = getScreenLineForRow(E.buf, win->rowoff);
			
			if (cursor_screen_line >= window_start_screen_line + win->height) {
				/* Cursor is below window - move it up to be within window */
				while (E.buf->cy > 0) {
					cursor_screen_line = getScreenLineForRow(E.buf, E.buf->cy);
					if (cursor_screen_line < window_start_screen_line + win->height)
						break;
					E.buf->cy--;
				}
			}
		}
		
		/* Ensure cursor column is valid for new row */
		if (E.buf->cy < E.buf->numrows && E.buf->cx > E.buf->row[E.buf->cy].size) {
			E.buf->cx = E.buf->row[E.buf->cy].size;
		}
	}
}

void editorPageDown(int count) {
	struct editorWindow *win = E.windows[windowFocusedIdx()];
	int times = count ? count : 1;

	for (int n = 0; n < times; n++) {
		int scroll_lines = win->height - page_overlap;
		if (scroll_lines < 1) scroll_lines = 1;
		
		if (E.buf->truncate_lines) {
			/* Move view down by scroll_lines */
			win->rowoff += scroll_lines;
			
			/* Don't scroll past end of file */
			if (win->rowoff + win->height > E.buf->numrows) {
				win->rowoff = E.buf->numrows - win->height;
				if (win->rowoff < 0) win->rowoff = 0;
			}
			
			/* Ensure cursor is within visible window */
			if (E.buf->cy < win->rowoff) {
				/* Cursor is above window - move it to top of window */
				E.buf->cy = win->rowoff;
			}
			/* If cursor is below window, it's already visible */
		} else {
			/* In wrapped mode, need to handle variable line heights */
			int lines_scrolled = 0;
			int new_rowoff = win->rowoff;
			
			/* Scroll down by the desired number of screen lines */
			while (lines_scrolled < scroll_lines && new_rowoff < E.buf->numrows) {
				int line_height = (calculateLineWidth(&E.buf->row[new_rowoff]) / E.screencols) + 1;
				lines_scrolled += line_height;
				new_rowoff++;
			}
			
			/* Don't scroll too far */
			if (new_rowoff > E.buf->numrows) {
				new_rowoff = E.buf->numrows;
			}
			win->rowoff = new_rowoff;
			
			/* Ensure cursor is visible - calculate screen position */
			int cursor_screen_line = getScreenLineForRow(E.buf, E.buf->cy);
			int window_start_screen_line = getScreenLineForRow(E.buf, win->rowoff);
			
			if (cursor_screen_line < window_start_screen_line) {
				/* Cursor is above window - move it down to be within window */
				E.buf->cy = win->rowoff;
			}
		}
		
		/* Ensure cursor column is valid for new row */
		if (E.buf->cy < E.buf->numrows && E.buf->cx > E.buf->row[E.buf->cy].size) {
			E.buf->cx = E.buf->row[E.buf->cy].size;
		}
	}
}

void editorBeginningOfLine(int count) {
	(void)count; // Not used
	E.buf->cx = 0;
}

void editorEndOfLine(int count) {
	(void)count; // Not used
	if (E.buf->row != NULL && E.buf->cy < E.buf->numrows) {
		E.buf->cx = E.buf->row[E.buf->cy].size;
	}
}

void editorQuit(void) {
	if (E.recording) {
		E.recording = 0;
	}
	// Check all buffers for unsaved changes, except the special buffers
	struct editorBuffer *current = E.headbuf;
	int hasUnsavedChanges = 0;
	while (current != NULL) {
		if (current->dirty && current->filename != NULL &&
		    !current->special_buffer) {
			hasUnsavedChanges = 1;
			break;
		}
		current = current->next;
	}

	if (hasUnsavedChanges) {
		editorSetStatusMessage(
			"There are unsaved changes. Really quit? (y or n)");
		refreshScreen();
		int c = editorReadKey();
		if (c == 'y' || c == 'Y') {
			exit(0);
		}
		editorSetStatusMessage("");
	} else {
		exit(0);
	}
}

void editorGotoLine(void) {
	uint8_t *nls;
	int nl;

	for (;;) {
		nls = editorPrompt(E.buf, "Goto line: %s", PROMPT_BASIC, NULL);
		if (!nls) {
			return;
		}

		nl = atoi((char *)nls);
		free(nls);

		if (nl) {
			E.buf->cx = 0;
			if (nl < 0) {
				E.buf->cy = 0;
			} else if (nl > E.buf->numrows) {
				E.buf->cy = E.buf->numrows;
			} else {
				E.buf->cy = nl;
			}
			return;
		}
	}
}
