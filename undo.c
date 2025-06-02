#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "emsys.h"
#include "region.h"
#include "row.h"
#include "undo.h"
#include "unicode.h"

extern struct editorConfig E;

void doUndo(struct editorBuffer *buf) {
	if (buf->undo == NULL) {
		setStatusMessage("No further undo information.");
		return;
	}
	int paired = buf->undo->paired;

	if (buf->undo->delete) {
		buf->cx = buf->undo->startx;
		buf->cy = buf->undo->starty;
		for (int i = buf->undo->datalen - 1; i >= 0; i--) {
			if (buf->undo->data[i] == '\n') {
				insertNewline(buf);
			} else {
				bufferInsertChar(buf, buf->undo->data[i]);
			}
		}
		buf->cx = buf->undo->endx;
		buf->cy = buf->undo->endy;
	} else {
		struct erow *row = &buf->row[buf->undo->starty];
		if (buf->undo->starty == buf->undo->endy) {
			rowDeleteRange(buf, row, buf->undo->startx,
					     buf->undo->endx);
		} else {
			for (int i = buf->undo->starty + 1; i < buf->undo->endy;
			     i++) {
				delRow(buf, buf->undo->starty + 1);
			}
			struct erow *last = &buf->row[buf->undo->starty + 1];
			rowDeleteRange(buf, row, buf->undo->startx,
					     row->size);
			rowInsertString(buf, row, row->size,
					      &last->chars[buf->undo->endx],
					      last->size - buf->undo->endx);
			delRow(buf, buf->undo->starty + 1);
		}
		buf->cx = buf->undo->startx;
		buf->cy = buf->undo->starty;
	}

	struct editorUndo *orig = buf->redo;
	buf->redo = buf->undo;
	buf->undo = buf->undo->prev;
	buf->redo->prev = orig;

	if (paired) {
		doUndo(buf);
	}
}

void doRedo(struct editorBuffer *buf) {
	if (buf->redo == NULL) {
		setStatusMessage("No further redo information.");
		return;
	}

	if (buf->redo->delete) {
		struct erow *row = &buf->row[buf->redo->starty];
		if (buf->redo->starty == buf->redo->endy) {
			rowDeleteRange(buf, row, buf->redo->startx,
					     buf->redo->endx);
		} else {
			for (int i = buf->redo->starty + 1; i < buf->redo->endy;
			     i++) {
				delRow(buf, buf->redo->starty + 1);
			}
			struct erow *last = &buf->row[buf->redo->starty + 1];
			rowDeleteRange(buf, row, buf->redo->startx,
					     row->size);
			rowInsertString(buf, row, row->size,
					      &last->chars[buf->redo->endx],
					      last->size - buf->redo->endx);
			delRow(buf, buf->redo->starty + 1);
		}
		buf->cx = buf->redo->startx;
		buf->cy = buf->redo->starty;
	} else {
		buf->cx = buf->redo->startx;
		buf->cy = buf->redo->starty;
		for (int i = 0; i < buf->redo->datalen; i++) {
			if (buf->redo->data[i] == '\n') {
				insertNewline(buf);
			} else {
				bufferInsertChar(buf, buf->redo->data[i]);
			}
		}
		buf->cx = buf->redo->endx;
		buf->cy = buf->redo->endy;
	}

	struct editorUndo *orig = buf->undo;
	buf->undo = buf->redo;
	buf->redo = buf->redo->prev;
	buf->undo->prev = orig;

	if (buf->redo != NULL && buf->redo->paired) {
		doRedo(buf);
	}
}

struct editorUndo *newUndo() {
	struct editorUndo *ret = xmalloc(sizeof(*ret));
	ret->prev = NULL;
	ret->paired = 0;
	ret->startx = 0;
	ret->starty = 0;
	ret->endx = 0;
	ret->endy = 0;
	ret->append = 1;
	ret->delete = 0;
	ret->datalen = 0;
	ret->datasize = 22;
	ret->data = xmalloc(ret->datasize);
	ret->data[0] = 0;
	return ret;
}

static void freeUndos(struct editorUndo *first) {
	struct editorUndo *cur = first;
	struct editorUndo *prev;

	while (cur != NULL) {
		free(cur->data);
		prev = cur;
		cur = prev->prev;
		free(prev);
	}
}

void clearRedos(struct editorBuffer *buf) {
	freeUndos(buf->redo);
	buf->redo = NULL;
}

void clearUndosAndRedos(struct editorBuffer *buf) {
	freeUndos(buf->undo);
	buf->undo = NULL;
	clearRedos(buf);
}

#define ALIGNED(x1, y1, x2, y2) ((x1 == x2) && (y1 == y2))

void undoAppendChar(struct editorBuffer *buf, uint8_t c) {
	clearRedos(buf);
	if (buf->undo == NULL || !(buf->undo->append) || buf->undo->delete ||
	    !ALIGNED(buf->undo->endx, buf->undo->endy, buf->cx, buf->cy)) {
		if (buf->undo != NULL)
			buf->undo->append = 0;
		struct editorUndo *new = newUndo();
		new->prev = buf->undo;
		new->startx = buf->cx;
		new->starty = buf->cy;
		new->endx = buf->cx;
		new->endy = buf->cy;
		buf->undo = new;
	}
	buf->undo->data[buf->undo->datalen++] = c;
	buf->undo->data[buf->undo->datalen] = 0;
	buf->undo->append = !(buf->undo->datalen >= buf->undo->datasize - 2);
	if (c == '\n') {
		buf->undo->endx = 0;
		buf->undo->endy++;
	} else {
		buf->undo->endx++;
	}
}

void undoAppendUnicode(void) {
	struct editorBuffer *buf = E.focusBuf;
	clearRedos(buf);
	if (buf->undo == NULL || !(buf->undo->append) ||
	    (buf->undo->datalen + E.nunicode >= buf->undo->datasize) ||
	    buf->undo->delete ||
	    !ALIGNED(buf->undo->endx, buf->undo->endy, buf->cx, buf->cy)) {
		if (buf->undo != NULL)
			buf->undo->append = 0;
		struct editorUndo *new = newUndo();
		new->prev = buf->undo;
		new->startx = buf->cx;
		new->starty = buf->cy;
		new->endx = buf->cx;
		new->endy = buf->cy;
		buf->undo = new;
	}
	for (int i = 0; i < E.nunicode; i++) {
		buf->undo->data[buf->undo->datalen++] = E.unicode[i];
	}
	buf->undo->data[buf->undo->datalen] = 0;
	buf->undo->append = !(buf->undo->datalen >= buf->undo->datasize - 2);
	buf->undo->endx += E.nunicode;
}

void undoBackSpace(struct editorBuffer *buf, uint8_t c) {
	clearRedos(buf);
	if (buf->undo == NULL || !(buf->undo->append) || !(buf->undo->delete) ||
	    !((c == '\n' && buf->undo->startx == 0 &&
	       buf->undo->starty == buf->cy) ||
	      (buf->cx + 1 == buf->undo->startx &&
	       buf->cy == buf->undo->starty))) {
		if (buf->undo != NULL)
			buf->undo->append = 0;
		struct editorUndo *new = newUndo();
		new->prev = buf->undo;
		new->endx = buf->cx;
		if (c != '\n')
			new->endx++;
		new->endy = buf->cy;
		new->startx = new->endx;
		new->starty = buf->cy;
		new->delete = 1;
		buf->undo = new;
	}
	buf->undo->data[buf->undo->datalen++] = c;
	buf->undo->data[buf->undo->datalen] = 0;
	if (buf->undo->datalen >= buf->undo->datasize - 2) {
		buf->undo->datasize *= 2;
		buf->undo->data = xrealloc(buf->undo->data, buf->undo->datasize);
	}
	if (c == '\n') {
		buf->undo->starty--;
		buf->undo->startx = buf->row[buf->undo->starty].size;
	} else {
		buf->undo->startx--;
	}
}

void undoDelChar(struct editorBuffer *buf, erow *row) {
	clearRedos(buf);
	if (buf->undo == NULL || !(buf->undo->append) || !(buf->undo->delete) ||
	    !(buf->undo->startx == buf->cx && buf->undo->starty == buf->cy)) {
		if (buf->undo != NULL)
			buf->undo->append = 0;
		struct editorUndo *new = newUndo();
		new->prev = buf->undo;
		new->endx = buf->cx;
		new->endy = buf->cy;
		new->startx = buf->cx;
		new->starty = buf->cy;
		new->delete = 1;
		buf->undo = new;
	}

	if (buf->cx == row->size) {
		buf->undo->datalen++;
		if (buf->undo->datalen >= buf->undo->datasize - 2) {
			buf->undo->datasize *= 2;
			buf->undo->data =
				xrealloc(buf->undo->data, buf->undo->datasize);
		}
		memmove(&buf->undo->data[1], buf->undo->data,
			buf->undo->datalen - 1);
		buf->undo->data[0] = '\n';
		buf->undo->endy++;
		buf->undo->endx = 0;
	} else {
		int n = utf8_nBytes(row->chars[buf->cx]);
		buf->undo->datalen += n;
		if (buf->undo->datalen >= buf->undo->datasize - 2) {
			buf->undo->datasize *= 2;
			buf->undo->data =
				xrealloc(buf->undo->data, buf->undo->datasize);
		}
		memmove(&buf->undo->data[n], buf->undo->data,
			buf->undo->datalen - n);
		for (int i = 0; i < n; i++) {
			int char_idx = buf->cx + n - i - 1;
			if (char_idx >= 0 && char_idx < row->size) {
				buf->undo->data[i] = row->chars[char_idx];
			} else {
				buf->undo->data[i] = ' ';
			}
			buf->undo->endx++;
		}
	}
}
