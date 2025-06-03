#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include "emsys.h"
#include "region.h"
#include "row.h"
#include "undo.h"

extern struct editorConfig E;

static void ensureRowWidth(erow *row, int target_width, int extra_space,
			   struct editorUndo *undo) {
	if (row->size < target_width) {
		char *newchars = xrealloc(row->chars, target_width + 1);
		if (newchars == NULL) {
			return;
		}
		row->chars = newchars;
		memset(&row->chars[row->size], ' ', target_width - row->size);
		row->size = target_width;
		row->width_valid = 0;
		if (undo) {
			undo->datasize += row->size + 1;
			char *newdata = xrealloc(undo->data, undo->datasize);
			if (newdata == NULL) {
				return;
			}
			undo->data = newdata;
		}
	}
	if (extra_space > 0) {
		char *newchars =
			xrealloc(row->chars, row->size + 1 + extra_space);
		if (newchars == NULL) {
			return;
		}
		row->chars = newchars;
	}
}

static void extractRectangleData(struct editorBuffer *buf, int topx, int topy,
				 int botx, int boty, int rx, int ry,
				 struct editorUndo *undo, int rectKill) {
	int idx = 0;

	for (int line = topy; line <= boty; line++) {
		struct erow *row = &buf->row[line];

		if (row->size < botx) {
			memset(&E.rectKill[idx * rx], ' ', rx);
			if (row->size > botx - rx) {
				strncpy((char *)&E.rectKill[idx * rx],
					(char *)&row->chars[botx - rx],
					row->size - (botx - rx));
			}
		} else {
			strncpy((char *)&E.rectKill[idx * rx],
				(char *)&row->chars[botx - rx], rx);
		}

		if (rectKill) {
			if (idx > 0 && undo) {
				strcat((char *)undo->data, "\n");
			}

			if (row->size < botx) {
				if (row->size > botx - rx) {
					row->size -= (row->size - (botx - rx));
					row->chars[row->size] = 0;
					row->width_valid = 0;
				}
			} else {
				memcpy(&row->chars[topx], &row->chars[botx],
				       row->size - botx);
				row->size -= rx;
				row->chars[row->size] = 0;
				row->width_valid = 0;
			}

			if (undo) {
				if (line == topy && boty != topy) {
					strcat((char *)undo->data,
					       (char *)&row->chars[topx]);
				} else if (line > topy && line < boty) {
					strcat((char *)undo->data,
					       (char *)row->chars);
				} else if (line == boty && boty != topy) {
					strncat((char *)undo->data,
						(char *)row->chars, topx);
				}
			}
		}

		idx++;
	}
}

void setMark(struct editorBuffer *buf) {
	buf->markx = buf->cx;
	buf->marky = buf->cy;
	setStatusMessage("Mark set.");
	if (buf->marky >= buf->numrows) {
		buf->marky = buf->numrows - 1;
		buf->markx = buf->row[buf->marky].size;
	}
}

void clearMark(struct editorBuffer *buf) {
	buf->markx = -1;
	buf->marky = -1;
	buf->rectangle_mode = 0;
	setStatusMessage("Mark Cleared");
}

void markRectangle(struct editorBuffer *buf) {
	buf->markx = buf->cx;
	buf->marky = buf->cy;
	buf->rectangle_mode = 1;
	setStatusMessage("Rectangle mark set.");
	if (buf->marky >= buf->numrows) {
		buf->marky = buf->numrows - 1;
		buf->markx = buf->row[buf->marky].size;
	}
}

int markInvalidSilent(struct editorBuffer *buf) {
	return (buf->markx < 0 || buf->marky < 0 || buf->numrows == 0 ||
		buf->marky >= buf->numrows ||
		buf->markx > (buf->row[buf->marky].size) ||
		(buf->markx == buf->cx && buf->cy == buf->marky));
}

int markInvalid(struct editorBuffer *buf) {
	int ret = markInvalidSilent(buf);

	if (ret) {
		setStatusMessage("Mark invalid.");
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

void killRegion(void) {
	struct editorBuffer *buf = E.focusBuf;
	if (markInvalid(buf))
		return;
	copyRegion();
	normalizeRegion(buf);

	clearRedos(buf);

	struct editorUndo *new = newUndo();
	new->startx = buf->cx;
	new->starty = buf->cy;
	new->endx = buf->markx;
	new->endy = buf->marky;
	free(new->data);
	new->datalen = strlen(E.kill);
	new->datasize = new->datalen + 1;
	new->data = xmalloc(new->datasize);
	/* XXX: have to copy kill to undo in reverse */
	for (int i = 0; i < new->datalen; i++) {
		new->data[i] = E.kill[new->datalen - i - 1];
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
		row->width_valid = 0;
	} else {
		for (int i = buf->cy + 1; i < buf->marky; i++) {
			delRow(buf, buf->cy + 1);
		}
		struct erow *last = &buf->row[buf->cy + 1];
		row->size = buf->cx;
		row->size += last->size - buf->markx;
		char *newchars = xrealloc(row->chars, row->size);
		if (newchars == NULL) {
			return;
		}
		row->chars = newchars;
		memcpy(&row->chars[buf->cx], &last->chars[buf->markx],
		       last->size - buf->markx);
		row->width_valid = 0;
		delRow(buf, buf->cy + 1);
	}

	buf->dirty = 1;
}

void copyRegion(void) {
	struct editorBuffer *buf = E.focusBuf;
	if (markInvalid(buf))
		return;
	int origCx = buf->cx;
	int origCy = buf->cy;
	int origMarkx = buf->markx;
	int origMarky = buf->marky;
	normalizeRegion(buf);
	free(E.kill);
	int regionSize = 32;
	E.kill = xmalloc(regionSize);

	int killpos = 0;
	while (!(buf->cy == buf->marky && buf->cx == buf->markx)) {
		uint8_t c = buf->row[buf->cy].chars[buf->cx];
		if (buf->cx >= buf->row[buf->cy].size) {
			buf->cy++;
			buf->cx = 0;
			E.kill[killpos++] = '\n';
		} else {
			E.kill[killpos++] = c;
			buf->cx++;
		}

		if (killpos >= regionSize - 2) {
			regionSize *= 2;
			E.kill = xrealloc(E.kill, regionSize);
		}
	}
	E.kill[killpos] = 0;

	buf->cx = origCx;
	buf->cy = origCy;
	buf->markx = origMarkx;
	buf->marky = origMarky;
}

void yank(void) {
	struct editorBuffer *buf = E.focusBuf;
	if (E.kill == NULL) {
		setStatusMessage("Kill ring empty.");
		return;
	}

	clearRedos(buf);

	struct editorUndo *new = newUndo();
	new->startx = buf->cx;
	new->starty = buf->cy;
	free(new->data);
	new->datalen = strlen(E.kill);
	new->datasize = new->datalen + 1;
	new->data = xmalloc(new->datasize);
	memcpy(new->data, E.kill, new->datasize);
	new->append = 0;

	for (int i = 0; E.kill[i] != 0; i++) {
		if (E.kill[i] == '\n') {
			insertNewline(buf);
		} else {
			bufferInsertChar(buf, E.kill[i]);
		}
	}

	new->endx = buf->cx;
	new->endy = buf->cy;
	new->prev = buf->undo;
	buf->undo = new;

	buf->dirty = 1;
}

void transformRegion(uint8_t *(*transformer)(uint8_t *)) {
	struct editorBuffer *buf = E.focusBuf;
	if (markInvalid(buf))
		return;
	normalizeRegion(buf);

	uint8_t *okill = NULL;
	if (E.kill != NULL) {
		okill = xmalloc(strlen(E.kill) + 1);
		strcpy(okill, E.kill);
	}
	killRegion();

	uint8_t *input = E.kill;
	if (input == NULL) {
		E.kill = okill;
		return;
	}

	uint8_t *result = transformer(input);
	if (result == NULL) {
		E.kill = okill;
		return;
	}

	E.kill = result;
	yank();
	buf->undo->paired = 1;

	if (input != result && input != NULL) {
		free(input);
	}

	if (result != okill && result != NULL) {
		free(result);
	}

	E.kill = okill;
}

void replaceRegex(void) {
	struct editorBuffer *buf = E.focusBuf;
	if (markInvalid(buf))
		return;
	normalizeRegion(buf);

	const char *cancel = "Canceled regex-replace.";
	int madeReplacements = 0;

	uint8_t *regex =
		promptUser(buf, "Regex replace: %s", PROMPT_BASIC, NULL);
	if (regex == NULL) {
		setStatusMessage("%s", cancel);
		return;
	}

	/* Validate regex immediately */
	regex_t test_pattern;
	int test_result = regcomp(&test_pattern, (char *)regex, REG_EXTENDED);
	if (test_result != 0) {
		char error_msg[256];
		regerror(test_result, &test_pattern, error_msg,
			 sizeof(error_msg));
		setStatusMessage("Regex error: %s", error_msg);
		free(regex);
		return;
	}
	regfree(&test_pattern);

	char prompt[64];
	snprintf(prompt, sizeof(prompt), "Regex replace %.35s with: %%s",
		 regex);
	uint8_t *repl =
		promptUser(buf, (uint8_t *)prompt, PROMPT_BASIC, NULL);
	if (repl == NULL) {
		free(regex);
		setStatusMessage("%s", cancel);
		return;
	}
	int replen = strlen((char *)repl);

	uint8_t *okill = NULL;
	if (E.kill != NULL) {
		okill = xmalloc(strlen((char *)E.kill) + 1);
		strcpy((char *)okill, (char *)E.kill);
	}
	copyRegion();

	/* This is a transformation, so create a delete undo. However, we're not
	 * actually doing any deletion yet in this case. */
	struct editorUndo *new = newUndo();
	new->startx = buf->cx;
	new->starty = buf->cy;
	new->endx = buf->markx;
	new->endy = buf->marky;
	new->datalen = strlen((char *)E.kill);
	if (new->datasize < new->datalen + 1) {
		new->datasize = new->datalen + 1;
		new->data = xrealloc(new->data, new->datasize);
	}
	for (int i = 0; i < new->datalen; i++) {
		new->data[i] = E.kill[new->datalen - i - 1];
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
		setStatusMessage("Regex error: %s", error_msg);
		free(regex);
		free(repl);
		E.kill = okill;
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
			char *newchars =
				xrealloc(row->chars, row->size + 1 + extra);
			if (newchars == NULL) {
				return;
			}
			row->chars = newchars;
			new->datasize += extra;
			char *newdata = xrealloc(new->data, new->datasize);
			if (newdata == NULL) {
				return;
			}
			new->data = newdata;
		}
		memmove(&row->chars[match_idx + replen],
			&row->chars[match_idx + match_length],
			row->size - (match_idx + match_length));
		memcpy(&row->chars[match_idx], repl, replen);
		row->size += extra;
		row->chars[row->size] = 0;
		row->width_valid = 0;
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

	regfree(&pattern);
	free(regex);
	free(repl);

	E.kill = okill;

	setStatusMessage("Replaced %d instances", madeReplacements);
}

/*** Rectangles ***/

void stringRectangle(void) {
	struct editorBuffer *buf = E.focusBuf;
	if (markInvalid(buf))
		return;

	uint8_t *string = promptUser(buf, (uint8_t *)"String rectangle: %s",
				       PROMPT_BASIC, NULL);
	if (string == NULL) {
		setStatusMessage("Canceled.");
		return;
	}

	uint8_t *okill = NULL;
	if (E.kill != NULL) {
		okill = xmalloc(strlen((char *)E.kill) + 1);
		strcpy((char *)okill, (char *)E.kill);
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
	copyRegion();
	clearRedos(buf);

	/* This is mostly a normal kill-region type undo. */
	struct editorUndo *new = newUndo();
	new->startx = buf->cx;
	new->starty = buf->cy;
	new->endx = buf->markx;
	new->endy = buf->marky;
	free(new->data);
	new->datalen = strlen((char *)E.kill);
	new->datasize = new->datalen + 1;
	new->data = xmalloc(new->datasize);
	for (int i = 0; i < new->datalen; i++) {
		new->data[i] = E.kill[new->datalen - i - 1];
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
		new->datasize = strlen((char *)E.kill) +
				(extra * ((boty - topy) + 1)) + 1;
	} else {
		new->datasize = strlen((char *)E.kill);
	}
	new->data = xmalloc(new->datasize);
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
	ensureRowWidth(row, botx, extra, new);
	memcpy(&row->chars[topx + slen], &row->chars[botx], row->size - botx);
	memcpy(&row->chars[topx], string, slen);
	row->size += extra;
	row->chars[row->size] = 0;
	row->width_valid = 0;
	if (boty == topy) {
		strcat((char *)new->data, (char *)string);
	} else {
		strcat((char *)new->data, (char *)&row->chars[topx]);
	}

	for (int i = topy + 1; i < boty; i++) {
		strcat((char *)new->data, "\n");
		/* Next, middle lines */
		row = &buf->row[i];
		ensureRowWidth(row, botx, extra, new);
		memcpy(&row->chars[topx + slen], &row->chars[botx],
		       row->size - botx);
		memcpy(&row->chars[topx], string, slen);
		row->size += extra;
		row->chars[row->size] = 0;
		row->width_valid = 0;
		strcat((char *)new->data, (char *)row->chars);
	}

	/* Finally, end line */
	if (topy != boty) {
		strcat((char *)new->data, "\n");
		row = &buf->row[boty];
		ensureRowWidth(row, botx, extra, new);
		memcpy(&row->chars[topx + slen], &row->chars[botx],
		       row->size - botx);
		memcpy(&row->chars[topx], string, slen);
		row->size += extra;
		row->chars[row->size] = 0;
		row->width_valid = 0;
		strncat((char *)new->data, (char *)row->chars, botx + extra);
	}
	new->datalen = strlen((char *)new->data);

	buf->dirty = 1;
	E.kill = okill;
}

void copyRectangle(void) {
	struct editorBuffer *buf = E.focusBuf;
	if (markInvalid(buf))
		return;
	normalizeRegion(buf);

	free(E.rectKill);

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
	E.rx = botx - topx;
	E.ry = (boty - topy) + 1;

	buf->cx = topx;
	buf->cy = topy;
	buf->marky = boty;
	if (botx > buf->row[boty].size) {
		buf->markx = buf->row[boty].size;
	} else {
		buf->markx = botx;
	}

	E.rectKill = xcalloc((E.rx * E.ry) + 1, 1);

	extractRectangleData(buf, topx, topy, botx, boty, E.rx, E.ry,
			     NULL, 0);
}

void killRectangle(void) {
	struct editorBuffer *buf = E.focusBuf;
	if (markInvalid(buf))
		return;
	normalizeRegion(buf);

	uint8_t *okill = NULL;
	if (E.kill != NULL) {
		okill = xmalloc(strlen((char *)E.kill) + 1);
		strcpy((char *)okill, (char *)E.kill);
	}
	free(E.rectKill);

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
	E.rx = botx - topx;
	E.ry = (boty - topy) + 1;

	buf->cx = topx;
	buf->cy = topy;
	buf->marky = boty;
	if (botx > buf->row[boty].size) {
		buf->markx = buf->row[boty].size;
	} else {
		buf->markx = botx;
	}
	copyRegion();
	clearRedos(buf);

	E.rectKill = xcalloc((E.rx * E.ry) + 1, 1);

	struct editorUndo *new = newUndo();
	new->startx = buf->cx;
	new->starty = buf->cy;
	new->endx = buf->markx;
	new->endy = buf->marky;
	free(new->data);
	new->datalen = strlen((char *)E.kill);
	new->datasize = new->datalen + 1;
	new->data = xmalloc(new->datasize);
	for (int i = 0; i < new->datalen; i++) {
		new->data[i] = E.kill[new->datalen - i - 1];
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
	new->endx = botx - E.rx;
	new->endy = boty;
	free(new->data);
	new->datalen = strlen((char *)E.kill) - (E.rx * E.ry);
	new->datasize = 1 + new->datalen;
	new->data = xmalloc(new->datasize);
	new->data[0] = 0;
	new->append = 0;
	new->paired = 1;
	buf->undo = new;

	extractRectangleData(buf, topx, topy, botx, boty, E.rx, E.ry,
			     new, 1);
	new->datalen = strlen((char *)new->data);

	buf->dirty = 1;
	E.kill = okill;
}

void markWholeBuffer(struct editorBuffer *buf) {
	buf->cy = 0;
	buf->cx = 0;
	if (buf->numrows > 0) {
		buf->marky = buf->numrows - 1;
		buf->markx = buf->row[buf->marky].size;
	} else {
		buf->marky = 0;
		buf->markx = 0;
	}
	setStatusMessage("Mark set at beginning of buffer");
}

void yankRectangle(void) {
	struct editorBuffer *buf = E.focusBuf;
	uint8_t *okill = NULL;
	if (E.kill != NULL) {
		okill = xmalloc(strlen((char *)E.kill) + 1);
		strcpy((char *)okill, (char *)E.kill);
	}

	int topx, topy, botx, boty;
	topx = buf->cx;
	topy = buf->cy;
	botx = topx;
	boty = topy + E.ry - 1;
	char *string = xcalloc(E.rx + 1, 1);

	buf->marky = boty;
	if (botx > buf->row[boty].size) {
		buf->markx = buf->row[boty].size;
	} else {
		buf->markx = botx;
	}

	int extralines = 0;
	while (boty >= buf->numrows) {
		insertRow(buf, buf->numrows, "", 0);
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

	copyRegion();
	clearRedos(buf);

	struct editorUndo *new = newUndo();
	new->startx = buf->cx;
	new->starty = buf->cy;
	new->endx = buf->markx;
	new->endy = buf->marky;
	free(new->data);
	if (E.kill == NULL) {
		new->datalen = 0;
	} else {
		new->datalen = strlen((char *)E.kill);
	}
	new->datasize = new->datalen + 1;
	new->data = xmalloc(new->datasize);
	if (E.kill == NULL) {
		new->data[0] = 0;
	} else {
		for (int i = 0; i < new->datalen; i++) {
			new->data[i] = E.kill[new->datalen - i - 1];
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
	new->endx = botx + E.rx;
	new->endy = boty;
	free(new->data);
	new->datalen = 0;
	if (E.rx > 0) {
		new->datasize = strlen((char *)E.kill) +
				(E.rx * ((boty - topy) + 1)) + 1;
	} else {
		new->datasize = strlen((char *)E.kill);
	}
	new->data = xmalloc(new->datasize);
	new->data[0] = 0;
	new->append = 0;
	new->paired = 1;
	buf->undo = new;

	/* First, topy */
	int idx = 0;
	struct erow *row = &buf->row[topy];
	strncpy(string, (char *)&E.rectKill[idx * E.rx], E.rx);
	ensureRowWidth(row, botx, E.rx, new);
	memcpy(&row->chars[topx + E.rx], &row->chars[botx], row->size - botx);
	memcpy(&row->chars[topx], string, E.rx);
	row->size += E.rx;
	row->chars[row->size] = 0;
	row->width_valid = 0;
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
		strncpy(string, (char *)&E.rectKill[idx * E.rx], E.rx);
		ensureRowWidth(row, botx, E.rx, new);
		memcpy(&row->chars[topx + E.rx], &row->chars[botx],
		       row->size - botx);
		memcpy(&row->chars[topx], string, E.rx);
		row->size += E.rx;
		row->chars[row->size] = 0;
		row->width_valid = 0;
		strcat((char *)new->data, (char *)row->chars);
		idx++;
	}

	/* Finally, end line */
	if (topy != boty) {
		strcat((char *)new->data, "\n");
		strncpy(string, (char *)&E.rectKill[idx * E.rx], E.rx);
		row = &buf->row[boty];
		ensureRowWidth(row, botx, E.rx, new);
		memcpy(&row->chars[topx + E.rx], &row->chars[botx],
		       row->size - botx);
		memcpy(&row->chars[topx], string, E.rx);
		row->size += E.rx;
		row->chars[row->size] = 0;
		row->width_valid = 0;
		strncat((char *)new->data, (char *)row->chars, botx + E.rx);
	}
	new->datalen = strlen((char *)new->data);

	buf->dirty = 1;
	E.kill = okill;
}
