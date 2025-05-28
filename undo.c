#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "emsys.h"
#include "region.h"
#include "row.h"
#include "undo.h"
#include "unicode.h"

extern struct editorConfig E;

void doUndo(int times) {
	struct editorBuffer *buf = E.buf;

	for (int t = 0; t < times; t++) {
		if (buf->undo == NULL) {
			if (t == 0) {
				setStatusMessage(
					"No further undo information.");
			}
			return;
		}
		int paired = buf->undo->paired;

		if (buf->undo->delete) {
			if (buf->undo->starty < 0 ||
			    buf->undo->starty >= buf->numrows ||
			    buf->undo->endy < 0 ||
			    buf->undo->endy > buf->numrows) {
				setStatusMessage(
					"Undo data corrupted - invalid coordinates");
				return;
			}
			buf->cx = buf->undo->startx;
			buf->cy = buf->undo->starty;
			for (int i = buf->undo->datalen - 1; i >= 0; i--) {
				if (buf->undo->data[i] == '\n') {
					insertNewline(1);
				} else {
					insertChar(buf->undo->data[i]);
				}
			}
			buf->cx = buf->undo->endx;
			buf->cy = buf->undo->endy;
		} else {
			if (buf->undo->starty < 0 ||
			    buf->undo->starty >= buf->numrows) {
				setStatusMessage(
					"Undo data corrupted - invalid row coordinates");
				return;
			}
			struct erow *row = &buf->row[buf->undo->starty];
			if (buf->undo->starty == buf->undo->endy) {
				rowDeleteRange(row, buf->undo->startx,
					       buf->undo->endx);
			} else {
				for (int i = buf->undo->starty + 1;
				     i < buf->undo->endy; i++) {
					delRow(buf, buf->undo->starty + 1);
				}
				struct erow *last =
					&buf->row[buf->undo->starty + 1];
				rowDeleteRange(row, buf->undo->startx,
					       row->size);
				rowInsertString(row, row->size,
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
			doUndo(1);
		}
	}
}

void doRedo(int times) {
	struct editorBuffer *buf = E.buf;

	for (int t = 0; t < times; t++) {
		if (buf->redo == NULL) {
			if (t == 0) {
				setStatusMessage(
					"No further redo information.");
			}
			return;
		}

		if (buf->redo->delete) {
			if (buf->redo->starty < 0 ||
			    buf->redo->starty >= buf->numrows) {
				setStatusMessage(
					"Redo data corrupted - invalid row coordinates");
				return;
			}
			struct erow *row = &buf->row[buf->redo->starty];
			if (buf->redo->starty == buf->redo->endy) {
				rowDeleteRange(row, buf->redo->startx,
					       buf->redo->endx);
			} else {
				for (int i = buf->redo->starty + 1;
				     i < buf->redo->endy; i++) {
					delRow(buf, buf->redo->starty + 1);
				}
				struct erow *last =
					&buf->row[buf->redo->starty + 1];
				rowDeleteRange(row, buf->redo->startx,
					       row->size);
				rowInsertString(row, row->size,
						&last->chars[buf->redo->endx],
						last->size - buf->redo->endx);
				delRow(buf, buf->redo->starty + 1);
			}
			buf->cx = buf->redo->startx;
			buf->cy = buf->redo->starty;
		} else {
			if (buf->redo->starty < 0 ||
			    buf->redo->starty >= buf->numrows ||
			    buf->redo->endy < 0 ||
			    buf->redo->endy > buf->numrows) {
				setStatusMessage(
					"Redo data corrupted - invalid coordinates");
				return;
			}
			buf->cx = buf->redo->startx;
			buf->cy = buf->redo->starty;
			for (int i = 0; i < buf->redo->datalen; i++) {
				if (buf->redo->data[i] == '\n') {
					insertNewline(1);
				} else {
					insertChar(buf->redo->data[i]);
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
			doRedo(1);
		}
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

void clearRedos(void) {
	freeUndos(E.buf->redo);
	E.buf->redo = NULL;
}

void clearUndosAndRedos(void) {
	freeUndos(E.buf->undo);
	E.buf->undo = NULL;
	clearRedos();
}

#define ALIGNED(x1, y1, x2, y2) ((x1 == x2) && (y1 == y2))

void undoAppendChar(uint8_t c) {
	clearRedos();
	if (E.buf->undo == NULL || !(E.buf->undo->append) ||
	    E.buf->undo->delete ||
	    !ALIGNED(E.buf->undo->endx, E.buf->undo->endy, E.buf->cx,
		     E.buf->cy)) {
		if (E.buf->undo != NULL)
			E.buf->undo->append = 0;
		struct editorUndo *new = newUndo();
		new->prev = E.buf->undo;
		new->startx = E.buf->cx;
		new->starty = E.buf->cy;
		new->endx = E.buf->cx;
		new->endy = E.buf->cy;
		E.buf->undo = new;
	}
	E.buf->undo->data[E.buf->undo->datalen++] = c;
	E.buf->undo->data[E.buf->undo->datalen] = 0;
	E.buf->undo->append =
		!(E.buf->undo->datalen >= E.buf->undo->datasize - 2);
	if (c == '\n') {
		E.buf->undo->endx = 0;
		E.buf->undo->endy++;
	} else {
		E.buf->undo->endx++;
	}
}

void undoAppendUnicode(void) {
	struct editorBuffer *buf = E.buf;
	clearRedos();
	if (buf->undo == NULL || !(buf->undo->append) ||
	    (buf->undo->datalen + E.nunicode >= buf->undo->datasize) ||
	    buf->undo->delete ||
	    !ALIGNED(buf->undo->endx, buf->undo->endy, buf->cx, buf->cy)) {
		if (buf->undo != NULL)
			E.buf->undo->append = 0;
		struct editorUndo *new = newUndo();
		new->prev = buf->undo;
		new->startx = buf->cx;
		new->starty = buf->cy;
		new->endx = buf->cx;
		new->endy = buf->cy;
		buf->undo = new;
	}
	for (int i = 0; i < E.nunicode; i++) {
		E.buf->undo->data[buf->undo->datalen++] = E.unicode[i];
	}
	buf->undo->data[buf->undo->datalen] = 0;
	buf->undo->append = !(buf->undo->datalen >= buf->undo->datasize - 2);
	buf->undo->endx += E.nunicode;
}

void undoBackSpace(uint8_t c) {
	clearRedos();
	if (E.buf->undo == NULL || !(E.buf->undo->append) ||
	    !(E.buf->undo->delete) ||
	    !((c == '\n' && E.buf->undo->startx == 0 &&
	       E.buf->undo->starty == E.buf->cy) ||
	      (E.buf->cx + 1 == E.buf->undo->startx &&
	       E.buf->cy == E.buf->undo->starty))) {
		if (E.buf->undo != NULL)
			E.buf->undo->append = 0;
		struct editorUndo *new = newUndo();
		new->prev = E.buf->undo;
		new->endx = E.buf->cx;
		if (c != '\n')
			new->endx++;
		new->endy = E.buf->cy;
		new->startx = new->endx;
		new->starty = E.buf->cy;
		new->delete = 1;
		E.buf->undo = new;
	}
	E.buf->undo->data[E.buf->undo->datalen++] = c;
	E.buf->undo->data[E.buf->undo->datalen] = 0;
	if (E.buf->undo->datalen >= E.buf->undo->datasize - 2) {
		E.buf->undo->datasize *= 2;
		E.buf->undo->data =
			xrealloc(E.buf->undo->data, E.buf->undo->datasize);
	}
	if (c == '\n') {
		E.buf->undo->starty--;
		if (E.buf->undo->starty >= 0) {
			E.buf->undo->startx =
				E.buf->row[E.buf->undo->starty].size;
		} else {
			E.buf->undo->startx = 0;
		}
	} else {
		E.buf->undo->startx--;
	}
}

void undoDelChar(erow *row) {
	clearRedos();
	if (E.buf->undo == NULL || !(E.buf->undo->append) ||
	    !(E.buf->undo->delete) ||
	    !(E.buf->undo->startx == E.buf->cx &&
	      E.buf->undo->starty == E.buf->cy)) {
		if (E.buf->undo != NULL)
			E.buf->undo->append = 0;
		struct editorUndo *new = newUndo();
		new->prev = E.buf->undo;
		new->endx = E.buf->cx;
		new->endy = E.buf->cy;
		new->startx = E.buf->cx;
		new->starty = E.buf->cy;
		new->delete = 1;
		E.buf->undo = new;
	}

	if (E.buf->cx == row->size) {
		E.buf->undo->datalen++;
		if (E.buf->undo->datalen >= E.buf->undo->datasize - 2) {
			E.buf->undo->datasize *= 2;
			E.buf->undo->data = xrealloc(E.buf->undo->data,
						     E.buf->undo->datasize);
		}
		memmove(&E.buf->undo->data[1], E.buf->undo->data,
			E.buf->undo->datalen - 1);
		E.buf->undo->data[0] = '\n';
		E.buf->undo->endy++;
		E.buf->undo->endx = 0;
	} else {
		int n = utf8_nBytes(row->chars[E.buf->cx]);
		E.buf->undo->datalen += n;
		if (E.buf->undo->datalen >= E.buf->undo->datasize - 2) {
			E.buf->undo->datasize *= 2;
			E.buf->undo->data = xrealloc(E.buf->undo->data,
						     E.buf->undo->datasize);
		}
		memmove(&E.buf->undo->data[n], E.buf->undo->data,
			E.buf->undo->datalen - n);
		for (int i = 0; i < n; i++) {
			int char_idx = E.buf->cx + n - i - 1;
			if (char_idx >= 0 && char_idx < row->size) {
				E.buf->undo->data[i] = row->chars[char_idx];
			} else {
				E.buf->undo->data[i] = ' ';
			}
			E.buf->undo->endx++;
		}
	}
}
