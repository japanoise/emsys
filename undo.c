#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "emsys.h"
#include "region.h"
#include "buffer.h"
#include "undo.h"
#include "unicode.h"
#include "display.h"
#include "unused.h"

void editorDoUndo(struct editorBuffer *buf, int count) {
	int times = count ? count : 1;
	for (int j = 0; j < times; j++) {
	if (buf->undo == NULL) {
		editorSetStatusMessage("No further undo information.");
		return;
	}
	int paired = buf->undo->paired;

	if (buf->undo->delete) {
		buf->cx = buf->undo->startx;
		buf->cy = buf->undo->starty;
		for (int i = buf->undo->datalen - 1; i >= 0; i--) {
			if (buf->undo->data[i] == '\n') {
				editorInsertNewline(buf, 1);
			} else {
				editorInsertChar(buf, buf->undo->data[i], 1);
			}
		}
		buf->cx = buf->undo->endx;
		buf->cy = buf->undo->endy;
	} else {
		if (buf->numrows == 0 || buf->undo->starty >= buf->numrows) {
			return;
		}
		struct erow *row = &buf->row[buf->undo->starty];
		if (buf->undo->starty == buf->undo->endy) {
			memmove(&row->chars[buf->undo->startx],
				&row->chars[buf->undo->endx],
				row->size - buf->undo->endx);
			row->size -= buf->undo->endx - buf->undo->startx;
			row->chars[row->size] = 0;
		} else {
			for (int i = buf->undo->starty + 1; i < buf->undo->endy;
			     i++) {
				editorDelRow(buf, buf->undo->starty + 1);
			}
			if (buf->undo->starty + 1 >= buf->numrows) {
				return;
			}
			struct erow *last = &buf->row[buf->undo->starty + 1];
			row->size = buf->undo->startx;
			row->size += last->size - buf->undo->endx;
			row->chars = realloc(row->chars, row->size);
			memcpy(&row->chars[buf->undo->startx],
			       &last->chars[buf->undo->endx],
			       last->size - buf->undo->endx);
			editorDelRow(buf, buf->undo->starty + 1);
		}
		buf->cx = buf->undo->startx;
		buf->cy = buf->undo->starty;
	}

	editorUpdateBuffer(buf);

	struct editorUndo *orig = buf->redo;
	buf->redo = buf->undo;
	buf->undo = buf->undo->prev;
	buf->redo->prev = orig;

	if (paired) {
		editorDoUndo(buf, 1);
	}
	}
}

#ifdef EMSYS_DEBUG_UNDO
void debugUnpair(struct editorConfig *UNUSED(ed), struct editorBuffer *buf) {
	int undos = 0;
	int redos = 0;
	for (struct editorUndo *i = buf->undo; i; i = i->prev) {
		i->paired = 0;
		undos++;
	}
	for (struct editorUndo *i = buf->redo; i; i = i->prev) {
		i->paired = 0;
		redos++;
	}
	editorSetStatusMessage("Unpaired %d undos, %d redos.", undos, redos);
}
#endif

void editorDoRedo(struct editorBuffer *buf, int count) {
	int times = count ? count : 1;
	for (int j = 0; j < times; j++) {
	if (buf->redo == NULL) {
		editorSetStatusMessage("No further redo information.");
		return;
	}

	if (buf->redo->delete) {
		struct erow *row = &buf->row[buf->redo->starty];
		if (buf->redo->starty == buf->redo->endy) {
			memmove(&row->chars[buf->redo->startx],
				&row->chars[buf->redo->endx],
				row->size - buf->redo->endx);
			row->size -= buf->redo->endx - buf->redo->startx;
			row->chars[row->size] = 0;
		} else {
			for (int i = buf->redo->starty + 1; i < buf->redo->endy;
			     i++) {
				editorDelRow(buf, buf->redo->starty + 1);
			}
			struct erow *last = &buf->row[buf->redo->starty + 1];
			row->size = buf->redo->startx;
			row->size += last->size - buf->redo->endx;
			row->chars = realloc(row->chars, row->size);
			memcpy(&row->chars[buf->redo->startx],
			       &last->chars[buf->redo->endx],
			       last->size - buf->redo->endx);
			editorDelRow(buf, buf->redo->starty + 1);
		}
		buf->cx = buf->redo->startx;
		buf->cy = buf->redo->starty;
	} else {
		buf->cx = buf->redo->startx;
		buf->cy = buf->redo->starty;
		for (int i = 0; i < buf->redo->datalen; i++) {
			if (buf->redo->data[i] == '\n') {
				editorInsertNewline(buf, 1);
			} else {
				editorInsertChar(buf, buf->redo->data[i], 1);
			}
		}
		buf->cx = buf->redo->endx;
		buf->cy = buf->redo->endy;
	}

	editorUpdateBuffer(buf);

	struct editorUndo *orig = buf->undo;
	buf->undo = buf->redo;
	buf->redo = buf->redo->prev;
	buf->undo->prev = orig;

	if (buf->redo != NULL && buf->redo->paired) {
		editorDoRedo(buf, 1);
	}
	}
}

struct editorUndo *newUndo() {
	struct editorUndo *ret = malloc(sizeof(*ret));
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
	ret->data = malloc(ret->datasize);
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

void editorUndoAppendChar(struct editorBuffer *buf, uint8_t c) {
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
	if (buf->undo->datalen >= buf->undo->datasize - 2) {
		buf->undo->datasize *= 2;
		buf->undo->data = realloc(buf->undo->data, buf->undo->datasize);
	}
	buf->undo->append = !(buf->undo->datalen >= buf->undo->datasize - 2);
	if (c == '\n') {
		buf->undo->endx = 0;
		buf->undo->endy++;
	} else {
		buf->undo->endx++;
	}
}

void editorUndoAppendUnicode(struct editorConfig *ed,
			     struct editorBuffer *buf) {
	clearRedos(buf);
	if (buf->undo == NULL || !(buf->undo->append) ||
	    (buf->undo->datalen + ed->nunicode >= buf->undo->datasize) ||
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
	for (int i = 0; i < ed->nunicode; i++) {
		buf->undo->data[buf->undo->datalen++] = ed->unicode[i];
	}
	buf->undo->data[buf->undo->datalen] = 0;
	buf->undo->append = !(buf->undo->datalen >= buf->undo->datasize - 2);
	buf->undo->endx += ed->nunicode;
}

void editorUndoBackSpace(struct editorBuffer *buf, uint8_t c) {
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
		buf->undo->data = realloc(buf->undo->data, buf->undo->datasize);
	}
	if (c == '\n') {
		buf->undo->starty--;
		buf->undo->startx = buf->row[buf->undo->starty].size;
	} else {
		buf->undo->startx--;
	}
}

void editorUndoDelChar(struct editorBuffer *buf, erow *row) {
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
				realloc(buf->undo->data, buf->undo->datasize);
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
				realloc(buf->undo->data, buf->undo->datasize);
		}
		memmove(&buf->undo->data[n], buf->undo->data,
			buf->undo->datalen - n);
		for (int i = 0; i < n; i++) {
			buf->undo->data[i] = row->chars[buf->cx + n - i - 1];
			buf->undo->endx++;
		}
	}
}
