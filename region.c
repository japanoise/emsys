#include<stdint.h>
#include<stdlib.h>
#include<string.h>
#include"emsys.h"
#include"region.h"
#include"row.h"
#include"undo.h"

void editorSetMark(struct editorBuffer *buf) {
	buf->markx = buf->cx;
	buf->marky = buf->cy;
	editorSetStatusMessage("Mark set.");
}

static int markInvalid(struct editorBuffer *buf) {
	int ret = (buf->markx < 0 || buf->marky < 0 || buf->numrows == 0 ||
		   buf->marky >= buf->numrows ||
		   buf->markx > (buf->row[buf->marky].size) ||
		   (buf->markx == buf->cx && buf->cy == buf->marky));

	if (ret) {
		editorSetStatusMessage("Mark invalid.");
	}

	return ret;
}

/* put cx,cy first */
static void validateRegion(struct editorBuffer *buf) {
	if (buf->cy > buf->marky || (buf->cy == buf->marky && buf->cx > buf->markx)) {
		int swapx, swapy;
		swapx = buf->cx;
		swapy = buf->cy;
		buf->cy = buf->marky;
		buf->cx = buf->markx;
		buf->markx = swapx;
		buf->marky = swapy;
	}
}

static size_t regionSize(struct editorBuffer *buf) {
	if (buf->cy == buf->marky) {
		return (buf->marky - buf->cy) + 1;
	} else {
		size_t ret = buf->row[buf->cy].size - buf->cx;
		for (int i = buf->cy + 1; i < buf->marky; i++) {
			ret += buf->row[i].size;
		}
		ret += buf->row[buf->marky].size - buf->markx;
		ret++;
		return ret;
	}
}

void editorKillRegion(struct editorConfig *ed, struct editorBuffer *buf) {
	if (markInvalid(buf)) return;
	editorCopyRegion(ed, buf);
	validateRegion(buf);

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
		new->data[i] = ed->kill[new->datalen-i-1];
	}
	new->data[new->datalen] = 0;
	new->append = 0;
	new->delete = 1;
	new->prev = buf->undo;
	buf->undo = new;

	struct erow *row = &buf->row[buf->cy];
	if (buf->cy == buf->marky) {
		memmove(&row->chars[buf->cx], &row->chars[buf->markx],
			row->size-buf->markx);
		row->size -= buf->markx - buf->cx;
		row->chars[row->size] = 0;
	} else {
		for (int i = buf->cy + 1; i < buf->marky; i++) {
			editorDelRow(buf, buf->cy+1);
		}
		struct erow *last = &buf->row[buf->cy+1];
		row->size = buf->cx;
		row->size += last->size - buf->markx;
		row->chars = realloc(row->chars, row->size);
		memcpy(&row->chars[buf->cx], &last->chars[buf->markx],
		       last->size-buf->markx);
		editorDelRow(buf, buf->cy+1);
	}

	buf->dirty = 1;
	editorUpdateBuffer(buf);
}

void editorCopyRegion(struct editorConfig *ed, struct editorBuffer *buf) {
	if (markInvalid(buf)) return;
	int origCx = buf->cx;
	int origCy = buf->cy;
	int origMarkx = buf->markx;
	int origMarky = buf->marky;
	validateRegion(buf);
	free(ed->kill);
	ed->kill = malloc(regionSize(buf));

	int killpos = 0;
	while (!(buf->cy == buf->marky && buf->cx == buf->markx)) {
		uint8_t c = buf->row[buf->cy].chars[buf->cx];
		if (buf->cx >= buf->row[buf->cy].size) {
			buf->cy++;
			buf->cx=0;
			ed->kill[killpos++] = '\n';
		} else {
			ed->kill[killpos++] = c;
			buf->cx++;
		}
	}
	ed->kill[killpos] = 0;

	buf->cx = origCx;
	buf->cy = origCy;
	buf->markx = origMarkx;
	buf->marky = origMarky;
}

void editorYank(struct editorConfig *ed, struct editorBuffer *buf) {
	if (ed->kill == NULL) {
		editorSetStatusMessage("Kill ring empty.");
		return;
	}

	clearRedos(buf);

	struct editorUndo *new = newUndo();
	new->startx = buf->cx;
	new->starty = buf->cy;
	free(new->data);
	new->datalen = strlen(ed->kill);
	new->datasize = new->datalen + 1;
	new->data = malloc(new->datasize);
	strcpy(new->data, ed->kill);
	new->append = 0;

	for (int i = 0; ed->kill[i] != 0; i++) {
		if (ed->kill[i] == '\n') {
			editorInsertNewline(buf);
		} else {
			editorInsertChar(buf, ed->kill[i]);
		}
	}

	new->endx = buf->cx;
	new->endy = buf->cy;
	new->prev = buf->undo;
	buf->undo = new;

	buf->dirty = 1;
	editorUpdateBuffer(buf);
}

void editorTransformRegion(struct editorBuffer *buf, uint8_t (*transformer)(uint8_t*)) {
	if (markInvalid(buf)) return;
	validateRegion(buf);
}
