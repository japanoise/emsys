#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include "emsys.h"
#include "region.h"
#include "buffer.h"
#include "undo.h"
#include "display.h"
#include "prompt.h"
#include "util.h"

extern struct editorConfig E;

void editorSetMark(void) {
	E.buf->markx = E.buf->cx;
	E.buf->marky = E.buf->cy;
	editorSetStatusMessage("Mark set.");
	if (E.buf->marky >= E.buf->numrows) {
		E.buf->marky = E.buf->numrows - 1;
		E.buf->markx = E.buf->row[E.buf->marky].size;
	}
}

void editorClearMark(void) {
	E.buf->markx = -1;
	E.buf->marky = -1;
	editorSetStatusMessage("Mark Cleared");
}

void editorToggleRectangleMode(void) {
	E.buf->rectangle_mode = !E.buf->rectangle_mode;
	if (E.buf->rectangle_mode) {
		editorSetStatusMessage("Rectangle mode ON");
	} else {
		editorSetStatusMessage("Rectangle mode OFF");
	}
}

void editorMarkBuffer(void) {
	if (E.buf->numrows > 0) {
		E.buf->cy = E.buf->numrows;
		E.buf->cx = E.buf->row[--E.buf->cy].size;
		editorSetMark();
		E.buf->cy = 0;
		E.buf->cx = 0;
	}
}

int markInvalidSilent(void) {
	return (E.buf->markx < 0 || E.buf->marky < 0 || E.buf->numrows == 0 ||
		E.buf->marky >= E.buf->numrows ||
		E.buf->markx > (E.buf->row[E.buf->marky].size) ||
		(E.buf->markx == E.buf->cx && E.buf->cy == E.buf->marky));
}

int markInvalid(void) {
	int ret = markInvalidSilent();

	if (ret) {
		editorSetStatusMessage("Mark invalid.");
	}

	return ret;
}

static void normalizeRegion(struct editorBuffer *buf) {
	/* Put cx,cy first */
	if (buf->cy > buf->marky ||
	    (buf->cy == buf->marky && buf->cx > buf->markx)) {
		int swapx, swapy;
		swapx = buf->cx;
		swapy = buf->cy;
		buf->cy = buf->marky;
		buf->cx = buf->markx;
		buf->markx = swapx;
		buf->marky = swapy;
	}
	/* Make sure mark is not outside buffer */
	if (buf->marky >= buf->numrows) {
		buf->marky = buf->numrows - 1;
		buf->markx = buf->row[buf->marky].size;
	}
}

void editorKillRegion(struct editorConfig *ed, struct editorBuffer *buf) {
	if (markInvalid())
		return;
	editorCopyRegion(ed, buf);
	normalizeRegion(buf);

	clearRedos(buf);

	struct editorUndo *new = newUndo();
	new->startx = buf->cx;
	new->starty = buf->cy;
	new->endx = buf->markx;
	new->endy = buf->marky;
	free(new->data);
	new->datalen = strlen(ed->kill);
	new->datasize = new->datalen + 1;
	new->data = malloc(new->datasize);
	/* XXX: have to copy kill to undo in reverse */
	for (int i = 0; i < new->datalen; i++) {
		new->data[i] = ed->kill[new->datalen - i - 1];
	}
	new->data[new->datalen] = 0;
	new->append = 0;
	new->delete = 1;
	new->prev = buf->undo;
	buf->undo = new;

	struct erow *row = &buf->row[buf->cy];
	if (buf->cy == buf->marky) {
		memmove(&row->chars[buf->cx], &row->chars[buf->markx],
			row->size - buf->markx);
		row->size -= buf->markx - buf->cx;
		row->chars[row->size] = 0;
	} else {
		for (int i = buf->cy + 1; i < buf->marky; i++) {
			editorDelRow(buf, buf->cy + 1);
		}
		struct erow *last = &buf->row[buf->cy + 1];
		row->size = buf->cx;
		row->size += last->size - buf->markx;
		row->chars = xrealloc(row->chars, row->size);
		memcpy(&row->chars[buf->cx], &last->chars[buf->markx],
		       last->size - buf->markx);
		editorDelRow(buf, buf->cy + 1);
	}

	buf->dirty = 1;
	editorUpdateBuffer(buf);
}

void editorCopyRegion(struct editorConfig *ed, struct editorBuffer *buf) {
	if (markInvalid())
		return;
	int origCx = buf->cx;
	int origCy = buf->cy;
	int origMarkx = buf->markx;
	int origMarky = buf->marky;
	normalizeRegion(buf);
	free(ed->kill);
	int regionSize = 32;
	ed->kill = malloc(regionSize);

	int killpos = 0;
	while (!(buf->cy == buf->marky && buf->cx == buf->markx)) {
		uint8_t c = buf->row[buf->cy].chars[buf->cx];
		if (buf->cx >= buf->row[buf->cy].size) {
			buf->cy++;
			buf->cx = 0;
			ed->kill[killpos++] = '\n';
		} else {
			ed->kill[killpos++] = c;
			buf->cx++;
		}

		if (killpos >= regionSize - 2) {
			regionSize *= 2;
			ed->kill = xrealloc(ed->kill, regionSize);
		}
	}
	ed->kill[killpos] = 0;

	buf->cx = origCx;
	buf->cy = origCy;
	buf->markx = origMarkx;
	buf->marky = origMarky;
}

void editorYank(struct editorConfig *ed, struct editorBuffer *buf, int count) {
	if (ed->kill == NULL) {
		editorSetStatusMessage("Kill ring empty.");
		return;
	}

	if (count <= 0)
		count = 1;

	// Check if this is a line yank (ends with newline)
	int killLen = strlen(ed->kill);
	int isLineYank = (killLen > 0 && ed->kill[killLen - 1] == '\n');

	for (int j = 0; j < count; j++) {
		clearRedos(buf);

		struct editorUndo *new = newUndo();
		new->startx = buf->cx;
		new->starty = buf->cy;
		free(new->data);
		new->datalen = killLen;
		new->datasize = new->datalen + 1;
		new->data = malloc(new->datasize);
		strcpy(new->data, ed->kill);
		new->append = 0;

		for (int i = 0; ed->kill[i] != 0; i++) {
			if (ed->kill[i] == '\n') {
				editorInsertNewline(buf, 1);
			} else {
				editorInsertChar(buf, ed->kill[i], 1);
			}
		}

		new->endx = buf->cx;
		new->endy = buf->cy;
		new->prev = buf->undo;
		buf->undo = new;

		// For line yanks with multiple repetitions, position cursor
		// at the beginning of the next line for the next yank
		if (isLineYank && j < count - 1) {
			buf->cx = 0;
			// Already on next line due to the newline
		}
	}

	buf->dirty = 1;
	editorUpdateBuffer(buf);
}

void editorTransformRegion(struct editorConfig *ed, struct editorBuffer *buf,
			   uint8_t *(*transformer)(uint8_t *)) {
	if (markInvalid())
		return;
	normalizeRegion(buf);

	uint8_t *okill = NULL;
	if (ed->kill != NULL) {
		okill = malloc(strlen(ed->kill) + 1);
		strcpy(okill, ed->kill);
	}
	editorKillRegion(ed, buf);

	uint8_t *input = ed->kill;
	ed->kill = transformer(input);
	editorYank(ed, buf, 1);
	buf->undo->paired = 1;

	if (input == ed->kill) {
		editorSetStatusMessage("Shouldn't free input here");
	} else {
		free(ed->kill);
	}
	ed->kill = okill;
}

void editorReplaceRegex(struct editorConfig *ed, struct editorBuffer *buf) {
	if (markInvalid())
		return;
	normalizeRegion(buf);

	const char *cancel = "Canceled regex-replace.";
	int madeReplacements = 0;

	uint8_t *regex =
		editorPrompt(buf, "Regex replace: %s", PROMPT_BASIC, NULL);
	if (regex == NULL) {
		editorSetStatusMessage(cancel);
		return;
	}

	char prompt[64];
	snprintf(prompt, sizeof(prompt), "Regex replace %.35s with: %%s",
		 regex);
	uint8_t *repl =
		editorPrompt(buf, (uint8_t *)prompt, PROMPT_BASIC, NULL);
	if (repl == NULL) {
		free(regex);
		editorSetStatusMessage(cancel);
		return;
	}
	int replen = strlen((char *)repl);

	uint8_t *okill = NULL;
	if (ed->kill != NULL) {
		okill = malloc(strlen((char *)ed->kill) + 1);
		strcpy((char *)okill, (char *)ed->kill);
	}
	editorCopyRegion(ed, buf);

	/* This is a transformation, so create a delete undo. However, we're not
	 * actually doing any deletion yet in this case. */
	struct editorUndo *new = newUndo();
	new->startx = buf->cx;
	new->starty = buf->cy;
	new->endx = buf->markx;
	new->endy = buf->marky;
	new->datalen = strlen((char *)ed->kill);
	if (new->datasize < new->datalen + 1) {
		new->datasize = new->datalen + 1;
		new->data = xrealloc(new->data, new->datasize);
	}
	for (int i = 0; i < new->datalen; i++) {
		new->data[i] = ed->kill[new->datalen - i - 1];
	}
	new->data[new->datalen] = 0;
	new->append = 0;
	new->delete = 1;
	new->prev = buf->undo;
	buf->undo = new;

	/* Create insert undo */
	new = newUndo();
	new->startx = buf->cx;
	new->starty = buf->cy;
	new->endy = buf->marky;
	new->datalen = buf->undo->datalen;
	if (new->datasize < new->datalen + 1) {
		new->datasize = new->datalen + 1;
		new->data = xrealloc(new->data, new->datasize);
	}
	new->prev = buf->undo;
	new->append = 0;
	new->delete = 0;
	new->paired = 1;
	buf->undo = new;

	/* Regex boilerplate & setup */
	regex_t pattern;
	regmatch_t matches[1];
	int regcomp_result = regcomp(&pattern, (char *)regex, REG_EXTENDED);
	if (regcomp_result != 0) {
		char error_msg[256];
		regerror(regcomp_result, &pattern, error_msg,
			 sizeof(error_msg));
		editorSetStatusMessage("Regex error: %s", error_msg);
		free(regex);
		free(repl);
		return;
	}

	for (int i = buf->cy; i <= buf->marky; i++) {
		struct erow *row = &buf->row[i];
		int regexec_result =
			regexec(&pattern, (char *)row->chars, 1, matches, 0);
		int match_idx = (regexec_result == 0) ? matches[0].rm_so : -1;
		int match_length = (regexec_result == 0) ? (matches[0].rm_eo -
							    matches[0].rm_so) :
							   0;
		if (i != 0)
			strcat((char *)new->data, "\n");
		if (match_idx < 0) {
			if (buf->cy == buf->marky) {
				strncat((char *)new->data,
					(char *)&row->chars[buf->cx],
					buf->markx - buf->cx);
			} else if (i == buf->cy) {
				strcat((char *)new->data,
				       (char *)&row->chars[buf->cx]);
			} else if (i == buf->marky) {
				strncat((char *)new->data, (char *)row->chars,
					buf->markx);
			} else {
				strcat((char *)new->data, (char *)row->chars);
			}
			continue;
		} else if (i == buf->cy && match_idx < buf->cx) {
			strcat((char *)new->data, (char *)&row->chars[buf->cx]);
			continue;
		} else if (i == buf->marky &&
			   match_idx + match_length > buf->markx) {
			strncat((char *)new->data, (char *)row->chars,
				buf->markx);
			continue;
		}
		madeReplacements++;
		/* Replace row data */
		row = &buf->row[i];
		int extra = replen - match_length;
		if (extra > 0) {
			row->chars =
				xrealloc(row->chars, row->size + 1 + extra);
			new->datasize += extra;
			new->data = xrealloc(new->data, new->datasize);
		}
		memmove(&row->chars[match_idx + replen],
			&row->chars[match_idx + match_length],
			row->size - (match_idx + match_length));
		memcpy(&row->chars[match_idx], repl, replen);
		row->size += extra;
		row->chars[row->size] = 0;
		if (buf->cy == buf->marky) {
			buf->markx += extra;
			strncat((char *)new->data, (char *)&row->chars[buf->cx],
				buf->markx - buf->cx);
		} else if (i == buf->cy) {
			strcat((char *)new->data, (char *)&row->chars[buf->cx]);
		} else if (i == buf->marky) {
			buf->markx += extra;
			strncat((char *)new->data, (char *)row->chars,
				buf->markx);
		} else {
			strcat((char *)new->data, (char *)row->chars);
		}
	}
	/* Now take care of insert undo */
	new->data[new->datasize - 1] = 0;
	new->datalen = strlen((char *)new->data);
	new->endx = buf->markx;

	buf->cx = new->endx;
	buf->cy = new->endy;

	editorUpdateBuffer(buf);
	regfree(&pattern);
	free(regex);
	free(repl);

	ed->kill = okill;

	editorSetStatusMessage("Replaced %d instances", madeReplacements);
}

/*** Rectangles ***/

void editorStringRectangle(struct editorConfig *ed, struct editorBuffer *buf) {
	if (markInvalid())
		return;

	uint8_t *string = editorPrompt(buf, (uint8_t *)"String rectangle: %s",
				       PROMPT_BASIC, NULL);
	if (string == NULL) {
		editorSetStatusMessage("Canceled.");
		return;
	}

	uint8_t *okill = NULL;
	if (ed->kill != NULL) {
		okill = malloc(strlen((char *)ed->kill) + 1);
		strcpy((char *)okill, (char *)ed->kill);
	}

	/* Do all the bookkeeping for killing the region, with a little extra
	 * for rectangles :) */

	/* Normalize the region (putting cx, cy before markx,marky) because this
	 * is a useful assumption to have. */
	normalizeRegion(buf);

	/* All the various dimensions we need for rectangles */
	int slen = strlen((char *)string);
	int topx, topy, botx, boty;
	boty = buf->marky;
	topy = buf->cy;
	/* If we don't do this, we end up creating undos that go out of the
	 * buffer. */
	if (buf->cx > buf->markx) {
		topx = buf->markx;
		botx = buf->cx;
	} else {
		botx = buf->markx;
		topx = buf->cx;
	}
	int rwidth = botx - topx;
	int extra = slen - rwidth; /* new bytes per line */

	buf->cx = topx;
	buf->cy = topy;
	buf->marky = boty;
	if (botx > buf->row[boty].size) {
		buf->markx = buf->row[boty].size;
	} else {
		buf->markx = botx;
	}
	editorCopyRegion(ed, buf);
	clearRedos(buf);

	/* This is mostly a normal kill-region type undo. */
	struct editorUndo *new = newUndo();
	new->startx = buf->cx;
	new->starty = buf->cy;
	new->endx = buf->markx;
	new->endy = buf->marky;
	free(new->data);
	new->datalen = strlen((char *)ed->kill);
	new->datasize = new->datalen + 1;
	new->data = malloc(new->datasize);
	for (int i = 0; i < new->datalen; i++) {
		new->data[i] = ed->kill[new->datalen - i - 1];
	}
	new->data[new->datalen] = 0;
	new->append = 0;
	new->delete = 1;
	new->prev = buf->undo;
	buf->undo = new;

	/* Undo for a yank region */
	new = newUndo();
	new->prev = buf->undo;
	new->startx = topx;
	new->starty = topy;
	new->endx = botx + extra;
	new->endy = boty;
	free(new->data);
	new->datalen = 0;
	if (extra > 0) {
		new->datasize = strlen((char *)ed->kill) +
				(extra * ((boty - topy) + 1)) + 1;
	} else {
		new->datasize = strlen((char *)ed->kill);
	}
	new->data = malloc(new->datasize);
	new->data[0] = 0;
	new->append = 0;
	new->paired = 1;
	buf->undo = new;

	/*
	 * We need to do the row modifying operation in three stages
	 * because the undo data we need to copy is slightly different:
	 * --RRRRXXX // Where - is don't copy,
	 * XXRRRRXXX // R is the replacement string,
	 * XXRRRR--- // and X is extra data.
	 */
	/* First, topy */
	struct erow *row = &buf->row[topy];
	if (row->size < botx) {
		row->chars = xrealloc(row->chars, botx + 1);
		memset(&row->chars[row->size], ' ', botx - row->size);
		row->size = botx;
		/* Better safe than sorry */
		new->datasize += row->size + 1;
		new->data = xrealloc(new->data, new->datasize);
	}
	if (extra > 0) {
		row->chars = xrealloc(row->chars, row->size + 1 + extra);
	}
	memcpy(&row->chars[topx + slen], &row->chars[botx], row->size - botx);
	memcpy(&row->chars[topx], string, slen);
	row->size += extra;
	row->chars[row->size] = 0;
	if (boty == topy) {
		strcat((char *)new->data, (char *)string);
	} else {
		strcat((char *)new->data, (char *)&row->chars[topx]);
	}

	for (int i = topy + 1; i < boty; i++) {
		strcat((char *)new->data, "\n");
		/* Next, middle lines */
		row = &buf->row[i];
		if (row->size < botx) {
			row->chars = xrealloc(row->chars, botx + 1);
			memset(&row->chars[row->size], ' ', botx - row->size);
			row->size = botx;
			new->datasize += row->size + 1;
			new->data = xrealloc(new->data, new->datasize);
		}
		if (extra > 0) {
			row->chars =
				xrealloc(row->chars, row->size + 1 + extra);
		}
		memcpy(&row->chars[topx + slen], &row->chars[botx],
		       row->size - botx);
		memcpy(&row->chars[topx], string, slen);
		row->size += extra;
		row->chars[row->size] = 0;
		strcat((char *)new->data, (char *)row->chars);
	}

	/* Finally, end line */
	if (topy != boty) {
		strcat((char *)new->data, "\n");
		row = &buf->row[boty];
		if (row->size < botx) {
			row->chars = xrealloc(row->chars, botx + 1);
			memset(&row->chars[row->size], ' ', botx - row->size);
			row->size = botx;
			new->datasize += row->size + 1;
			new->data = xrealloc(new->data, new->datasize);
		}
		if (extra > 0) {
			row->chars =
				xrealloc(row->chars, row->size + 1 + extra);
		}
		memcpy(&row->chars[topx + slen], &row->chars[botx],
		       row->size - botx);
		memcpy(&row->chars[topx], string, slen);
		row->size += extra;
		row->chars[row->size] = 0;
		strncat((char *)new->data, (char *)row->chars, botx + extra);
	}
	new->datalen = strlen((char *)new->data);

	buf->dirty = 1;
	editorUpdateBuffer(buf);
	ed->kill = okill;
}

void editorCopyRectangle(struct editorConfig *ed, struct editorBuffer *buf) {
	if (markInvalid())
		return;
	normalizeRegion(buf);

	free(ed->rectKill);

	int topx, topy, botx, boty;
	boty = buf->marky;
	topy = buf->cy;
	if (buf->cx > buf->markx) {
		topx = buf->markx;
		botx = buf->cx;
	} else {
		botx = buf->markx;
		topx = buf->cx;
	}
	ed->rx = botx - topx;
	ed->ry = (boty - topy) + 1;

	buf->cx = topx;
	buf->cy = topy;
	buf->marky = boty;
	if (botx > buf->row[boty].size) {
		buf->markx = buf->row[boty].size;
	} else {
		buf->markx = botx;
	}

	ed->rectKill = xcalloc((ed->rx * ed->ry) + 1, 1);

	/* First, topy */
	int idx = 0;
	struct erow *row = &buf->row[topy + idx];
	if (row->size < botx) {
		memset(&ed->rectKill[idx * ed->rx], ' ', ed->rx);
		if (row->size > botx - ed->rx) {
			strncpy((char *)&ed->rectKill[idx * ed->rx],
				(char *)&row->chars[botx - ed->rx],
				row->size - (botx - ed->rx));
		}
	} else {
		strncpy((char *)&ed->rectKill[idx * ed->rx],
			(char *)&row->chars[botx - ed->rx], ed->rx);
	}
	idx++;

	while ((topy + idx) < boty) {
		/* Middle lines */
		row = &buf->row[topy + idx];

		if (row->size < botx) {
			memset(&ed->rectKill[idx * ed->rx], ' ', ed->rx);
			if (row->size > botx - ed->rx) {
				strncpy((char *)&ed->rectKill[idx * +ed->rx],
					(char *)&row->chars[botx - ed->rx],
					row->size - (botx - ed->rx));
			}
		} else {
			strncpy((char *)&ed->rectKill[idx * ed->rx],
				(char *)&row->chars[botx - ed->rx], ed->rx);
		}

		idx++;
	}

	/* finally, end line */
	if (topy != boty) {
		row = &buf->row[topy + idx];

		if (row->size < botx) {
			memset(&ed->rectKill[idx * ed->rx], ' ', ed->rx);
			if (row->size > botx - ed->rx) {
				strncpy((char *)&ed->rectKill[idx * ed->rx],
					(char *)&row->chars[botx - ed->rx],
					row->size - (botx - ed->rx));
			}
		} else {
			strncpy((char *)&ed->rectKill[idx * ed->rx],
				(char *)&row->chars[botx - ed->rx], ed->rx);
		}
	}
}

void editorKillRectangle(struct editorConfig *ed, struct editorBuffer *buf) {
	if (markInvalid())
		return;
	normalizeRegion(buf);

	uint8_t *okill = NULL;
	if (ed->kill != NULL) {
		okill = malloc(strlen((char *)ed->kill) + 1);
		strcpy((char *)okill, (char *)ed->kill);
	}
	free(ed->rectKill);

	int topx, topy, botx, boty;
	boty = buf->marky;
	topy = buf->cy;
	if (buf->cx > buf->markx) {
		topx = buf->markx;
		botx = buf->cx;
	} else {
		botx = buf->markx;
		topx = buf->cx;
	}
	ed->rx = botx - topx;
	ed->ry = (boty - topy) + 1;

	buf->cx = topx;
	buf->cy = topy;
	buf->marky = boty;
	if (botx > buf->row[boty].size) {
		buf->markx = buf->row[boty].size;
	} else {
		buf->markx = botx;
	}
	editorCopyRegion(ed, buf);
	clearRedos(buf);

	ed->rectKill = xcalloc((ed->rx * ed->ry) + 1, 1);

	struct editorUndo *new = newUndo();
	new->startx = buf->cx;
	new->starty = buf->cy;
	new->endx = buf->markx;
	new->endy = buf->marky;
	free(new->data);
	new->datalen = strlen((char *)ed->kill);
	new->datasize = new->datalen + 1;
	new->data = malloc(new->datasize);
	for (int i = 0; i < new->datalen; i++) {
		new->data[i] = ed->kill[new->datalen - i - 1];
	}
	new->data[new->datalen] = 0;
	new->append = 0;
	new->delete = 1;
	new->prev = buf->undo;
	buf->undo = new;

	/* This is technically a transformation, so we need paired undos. */
	new = newUndo();
	new->prev = buf->undo;
	new->startx = topx;
	new->starty = topy;
	new->endx = botx - ed->rx;
	new->endy = boty;
	free(new->data);
	new->datalen = strlen((char *)ed->kill) - (ed->rx * ed->ry);
	new->datasize = 1 + new->datalen;
	new->data = malloc(new->datasize);
	new->data[0] = 0;
	new->append = 0;
	new->paired = 1;
	buf->undo = new;

	/* First, topy */
	int idx = 0;
	struct erow *row = &buf->row[topy + idx];
	if (row->size < botx) {
		memset(&ed->rectKill[idx * ed->rx], ' ', ed->rx);
		if (row->size > botx - ed->rx) {
			strncpy((char *)&ed->rectKill[idx * ed->rx],
				(char *)&row->chars[botx - ed->rx],
				row->size - (botx - ed->rx));
			row->size -= (row->size - (botx - ed->rx));
			row->chars[row->size] = 0;
			if (boty != topy) {
				strcat((char *)new->data,
				       (char *)&row->chars[botx - ed->rx]);
			}
		}
	} else {
		strncpy((char *)&ed->rectKill[idx * ed->rx],
			(char *)&row->chars[botx - ed->rx], ed->rx);
		memcpy(&row->chars[topx], &row->chars[botx], row->size - botx);
		row->size -= ed->rx;
		row->chars[row->size] = 0;
		if (boty != topy) {
			strcat((char *)new->data, (char *)&row->chars[topx]);
		}
	}
	idx++;

	while ((topy + idx) < boty) {
		/* Middle lines */
		strcat((char *)new->data, "\n");
		row = &buf->row[topy + idx];

		if (row->size < botx) {
			memset(&ed->rectKill[idx * ed->rx], ' ', ed->rx);
			if (row->size > botx - ed->rx) {
				strncpy((char *)&ed->rectKill[idx * ed->rx],
					(char *)&row->chars[botx - ed->rx],
					row->size - (botx - ed->rx));
				row->size -= (row->size - (botx - ed->rx));
				row->chars[row->size] = 0;
			}
		} else {
			strncpy((char *)&ed->rectKill[idx * ed->rx],
				(char *)&row->chars[botx - ed->rx], ed->rx);
			memcpy(&row->chars[topx], &row->chars[botx],
			       row->size - botx);
			row->size -= ed->rx;
			row->chars[row->size] = 0;
		}

		strcat((char *)new->data, (char *)row->chars);
		idx++;
	}

	/* Finally, end line */
	if (topy != boty) {
		strcat((char *)new->data, "\n");
		row = &buf->row[topy + idx];

		if (row->size < botx) {
			memset(&ed->rectKill[idx * ed->rx], ' ', ed->rx);
			if (row->size > botx - ed->rx) {
				strncpy((char *)&ed->rectKill[idx * ed->rx],
					(char *)&row->chars[botx - ed->rx],
					row->size - (botx - ed->rx));
				row->size -= (row->size - (botx - ed->rx));
				row->chars[row->size] = 0;
			}
		} else {
			strncpy((char *)&ed->rectKill[idx * ed->rx],
				(char *)&row->chars[botx - ed->rx], ed->rx);
			memcpy(&row->chars[topx], &row->chars[botx],
			       row->size - botx);
			row->size -= ed->rx;
			row->chars[row->size] = 0;
		}

		strncat((char *)new->data, (char *)row->chars, topx);
	}
	new->datalen = strlen((char *)new->data);

	buf->dirty = 1;
	editorUpdateBuffer(buf);
	ed->kill = okill;
}

void editorYankRectangle(struct editorConfig *ed, struct editorBuffer *buf) {
	uint8_t *okill = NULL;
	if (ed->kill != NULL) {
		okill = malloc(strlen((char *)ed->kill) + 1);
		strcpy((char *)okill, (char *)ed->kill);
	}

	int topx, topy, botx, boty;
	topx = buf->cx;
	topy = buf->cy;
	botx = topx;
	boty = topy + ed->ry - 1;
	char *string = xcalloc(ed->rx + 1, 1);

	buf->marky = boty;
	if (botx > buf->row[boty].size) {
		buf->markx = buf->row[boty].size;
	} else {
		buf->markx = botx;
	}

	int extralines = 0;
	while (boty >= buf->numrows) {
		editorInsertRow(buf, buf->numrows, "", 0);
		extralines++;
	}
	if (extralines) {
		struct editorUndo *new = newUndo();
		new->starty = buf->numrows - extralines - 1;
		new->startx = buf->row[new->starty].size;
		new->endx = 0;
		new->endy = buf->numrows - 1;
		if (extralines >= new->datasize) {
			new->datasize = extralines + 1;
			new->data = xrealloc(new->data, new->datasize);
		}
		memset(new->data, '\n', extralines);
		new->data[extralines] = 0;
		new->datalen = strlen((char *)new->data);
		new->prev = buf->undo;
		new->append = 0;
		new->delete = 0;
		buf->undo = new;
	}

	editorCopyRegion(ed, buf);
	clearRedos(buf);

	struct editorUndo *new = newUndo();
	new->startx = buf->cx;
	new->starty = buf->cy;
	new->endx = buf->markx;
	new->endy = buf->marky;
	free(new->data);
	if (ed->kill == NULL) {
		new->datalen = 0;
	} else {
		new->datalen = strlen((char *)ed->kill);
	}
	new->datasize = new->datalen + 1;
	new->data = malloc(new->datasize);
	if (ed->kill == NULL) {
		new->data[0] = 0;
	} else {
		for (int i = 0; i < new->datalen; i++) {
			new->data[i] = ed->kill[new->datalen - i - 1];
		}
	}
	new->data[new->datalen] = 0;
	new->append = 0;
	new->delete = 1;
	new->paired = extralines;
	new->prev = buf->undo;
	buf->undo = new;

	/* Transformation (insert) undo */
	new = newUndo();
	new->prev = buf->undo;
	new->startx = topx;
	new->starty = topy;
	new->endx = botx + ed->rx;
	new->endy = boty;
	free(new->data);
	new->datalen = 0;
	if (ed->rx > 0) {
		new->datasize = strlen((char *)ed->kill) +
				(ed->rx * ((boty - topy) + 1)) + 1;
	} else {
		new->datasize = strlen((char *)ed->kill);
	}
	new->data = malloc(new->datasize);
	new->data[0] = 0;
	new->append = 0;
	new->paired = 1;
	buf->undo = new;

	/* First, topy */
	int idx = 0;
	struct erow *row = &buf->row[topy];
	strncpy(string, (char *)&ed->rectKill[idx * ed->rx], ed->rx);
	if (row->size < botx) {
		row->chars = xrealloc(row->chars, botx + 1);
		memset(&row->chars[row->size], ' ', botx - row->size);
		row->size = botx;
		new->datasize += row->size + 1;
		new->data = xrealloc(new->data, new->datasize);
	}
	if (ed->rx > 0) {
		row->chars = xrealloc(row->chars, row->size + 1 + ed->rx);
	}
	memcpy(&row->chars[topx + ed->rx], &row->chars[botx], row->size - botx);
	memcpy(&row->chars[topx], string, ed->rx);
	row->size += ed->rx;
	row->chars[row->size] = 0;
	if (boty == topy) {
		strcat((char *)new->data, string);
	} else {
		strcat((char *)new->data, (char *)&row->chars[topx]);
	}
	idx++;

	while ((topy + idx) < boty) {
		strcat((char *)new->data, "\n");
		/* Next, middle lines */
		row = &buf->row[topy + idx];
		strncpy(string, (char *)&ed->rectKill[idx * ed->rx], ed->rx);
		if (row->size < botx) {
			row->chars = xrealloc(row->chars, botx + 1);
			memset(&row->chars[row->size], ' ', botx - row->size);
			row->size = botx;
			new->datasize += row->size + 1;
			new->data = xrealloc(new->data, new->datasize);
		}
		if (ed->rx > 0) {
			row->chars =
				xrealloc(row->chars, row->size + 1 + ed->rx);
		}
		memcpy(&row->chars[topx + ed->rx], &row->chars[botx],
		       row->size - botx);
		memcpy(&row->chars[topx], string, ed->rx);
		row->size += ed->rx;
		row->chars[row->size] = 0;
		strcat((char *)new->data, (char *)row->chars);
		idx++;
	}

	/* Finally, end line */
	if (topy != boty) {
		strcat((char *)new->data, "\n");
		strncpy(string, (char *)&ed->rectKill[idx * ed->rx], ed->rx);
		row = &buf->row[boty];
		if (row->size < botx) {
			row->chars = xrealloc(row->chars, botx + 1);
			memset(&row->chars[row->size], ' ', botx - row->size);
			row->size = botx;
			new->datasize += row->size + 1;
			new->data = xrealloc(new->data, new->datasize);
		}
		if (ed->rx > 0) {
			row->chars =
				xrealloc(row->chars, row->size + 1 + ed->rx);
		}
		memcpy(&row->chars[topx + ed->rx], &row->chars[botx],
		       row->size - botx);
		memcpy(&row->chars[topx], string, ed->rx);
		row->size += ed->rx;
		row->chars[row->size] = 0;
		strncat((char *)new->data, (char *)row->chars, botx + ed->rx);
	}
	new->datalen = strlen((char *)new->data);

	buf->dirty = 1;
	editorUpdateBuffer(buf);
	ed->kill = okill;
}
