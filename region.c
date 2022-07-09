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
	if (buf->marky >= buf->numrows) {
		buf->marky = buf->numrows - 1;
		buf->markx = buf->row[buf->marky].size;
	}
}

int markInvalid(struct editorBuffer *buf) {
	int ret = (buf->markx < 0 || buf->marky < 0 || buf->numrows == 0 ||
		   buf->marky >= buf->numrows ||
		   buf->markx > (buf->row[buf->marky].size) ||
		   (buf->markx == buf->cx && buf->cy == buf->marky));

	if (ret) {
		editorSetStatusMessage("Mark invalid.");
	}

	return ret;
}

static void validateRegion(struct editorBuffer *buf) {
	/* Put cx,cy first */
	if (buf->cy > buf->marky || (buf->cy == buf->marky && buf->cx > buf->markx)) {
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
	int regionSize = 32;
	ed->kill = malloc(regionSize);

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

		if (killpos >= regionSize - 2) {
			regionSize *= 2;
			ed->kill = realloc(ed->kill, regionSize);
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

void editorTransformRegion(struct editorConfig *ed, struct editorBuffer *buf,
			   uint8_t *(*transformer)(uint8_t*)) {
	if (markInvalid(buf)) return;
	validateRegion(buf);

	uint8_t *okill = NULL;
	if (ed->kill != NULL) {
		okill = malloc(strlen(ed->kill) + 1);
		strcpy(okill, ed->kill);
	}
	editorKillRegion(ed, buf);

	uint8_t *input = ed->kill;
	ed->kill = transformer(input);
	editorYank(ed, buf);
	buf->undo->paired = 1;

	if (input == ed->kill) {
		editorSetStatusMessage("Shouldn't free input here");
	} else {
		free(ed->kill);
	}
	ed->kill = okill;
}

/*** Rectangles ***/

void editorStringRectangle(struct editorConfig *ed, struct editorBuffer *buf) {
	if (markInvalid(buf)) return;

	uint8_t *string = editorPrompt(buf, (uint8_t*)"String rectangle: %s",
				       PROMPT_BASIC, NULL);
	if (string == NULL) {
		editorSetStatusMessage("Canceled.");
		return;
	}

	uint8_t *okill = NULL;
	if (ed->kill != NULL) {
		okill = malloc(strlen((char*)ed->kill) + 1);
		strcpy((char*)okill, (char*)ed->kill);
	}

	/* Do all the bookkeeping for killing the region. */
	editorCopyRegion(ed, buf);
	validateRegion(buf);

	/* With a little extra for rectangles :) */
	int slen = strlen((char*) string);
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
	int rwidth = botx - topx;
	int extra = slen - rwidth;

	clearRedos(buf);

	struct editorUndo *new = newUndo();
	new->startx = topx;
	new->starty = topy;
	new->endx = botx;
	new->endy = boty;
	free(new->data);
	new->datalen = strlen((char*)ed->kill);
	new->datasize = new->datalen + 1;
	new->data = malloc(new->datasize);
	for (int i = 0; i < new->datalen; i++) {
		new->data[i] = ed->kill[new->datalen-i-1];
	}
	new->data[new->datalen] = 0;
	new->append = 0;
	new->delete = 1;
	new->prev = buf->undo;
	buf->undo = new;

	/* However, we actually want to make a rectangle change. */
	/* Bookkeeping for a yank region */
	new = newUndo();
	new->prev = buf->undo;
	new->startx = topx;
	new->starty = topy;
	new->endx = botx+extra;
	new->endy = boty;
	free(new->data);
	new->datalen = 0;
	if (extra > 0) {
		new->datasize =
			strlen((char*)ed->kill)+(extra*((boty-topy)+1)) + 1;
	} else {
		new->datasize =
			strlen((char*)ed->kill);
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
		row->chars = realloc(row->chars, botx+1);
		memset(&row->chars[row->size], ' ', botx-row->size);
		row->size = botx;
		/* Better safe than sorry */
		new->datasize+=row->size+1;
		new->data = realloc(new->data, new->datasize);
	}
	if (extra > 0) {
		row->chars = realloc(row->chars, row->size+1+extra);
	}
	memcpy(&row->chars[topx+slen], &row->chars[botx],
	       row->size-botx);
	memcpy(&row->chars[topx], string, slen);
	row->size += extra;
	row->chars[row->size] = 0;
	strcat((char*)new->data, (char*)&row->chars[topx]);
	for (int i = topy+1; i < boty; i++) {
		strcat((char*)new->data, "\n");
		/* Next, middle lines */
		row = &buf->row[i];
		if (row->size < botx) {
			row->chars = realloc(row->chars, botx+1);
			memset(&row->chars[row->size], ' ', botx-row->size);
			row->size = botx;
			new->datasize+=row->size+1;
			new->data = realloc(new->data, new->datasize);
		}
		if (extra > 0) {
			row->chars = realloc(row->chars,
					     row->size+1+extra);
		}
		memcpy(&row->chars[topx+slen], &row->chars[botx],
		       row->size-botx);
		memcpy(&row->chars[topx], string, slen);
		row->size += extra;
		row->chars[row->size] = 0;
		strcat((char*)new->data, (char*)row->chars);
	}
	/* Finally, end line */
	if (topy != boty) {
		strcat((char*)new->data, "\n");
		row = &buf->row[boty];
		if (row->size < botx) {
			row->chars = realloc(row->chars, botx+1);
			memset(&row->chars[row->size], ' ', botx-row->size);
			row->size = botx;
			new->datasize+=row->size+1;
			new->data = realloc(new->data, new->datasize);
		}
		if (extra > 0) {
			row->chars = realloc(row->chars,
					     row->size+1+extra);
		}
		memcpy(&row->chars[topx+slen], &row->chars[botx],
		       row->size-botx);
		memcpy(&row->chars[topx], string, slen);
		row->size += extra;
		row->chars[row->size] = 0;
		strncat((char*)new->data, (char*)row->chars, botx+extra);
	}
	new->datalen = strlen((char*)new->data);
	
	/* if (buf->cy == buf->marky) { */
	/* 	memmove(&row->chars[buf->cx], &row->chars[buf->markx], */
	/* 		row->size-buf->markx); */
	/* 	row->size -= buf->markx - buf->cx; */
	/* 	row->chars[row->size] = 0; */
	/* } else { */
	/* 	for (int i = buf->cy + 1; i < buf->marky; i++) { */
	/* 		editorDelRow(buf, buf->cy+1); */
	/* 	} */
	/* 	struct erow *last = &buf->row[buf->cy+1]; */
	/* 	row->size = buf->cx; */
	/* 	row->size += last->size - buf->markx; */
	/* 	row->chars = realloc(row->chars, row->size); */
	/* 	memcpy(&row->chars[buf->cx], &last->chars[buf->markx], */
	/* 	       last->size-buf->markx); */
	/* 	editorDelRow(buf, buf->cy+1); */
	/* } */

	buf->dirty = 1;
	editorUpdateBuffer(buf);
	ed->kill = okill;
}

void editorCopyRectangle(struct editorConfig *ed, struct editorBuffer *buf) {
	if (markInvalid(buf)) return;
	validateRegion(buf);
}

void editorKillRectangle(struct editorConfig *ed, struct editorBuffer *buf) {
	if (markInvalid(buf)) return;
	validateRegion(buf);
}

void editorYankRectangle(struct editorConfig *ed, struct editorBuffer *buf) {
	if (markInvalid(buf)) return;
	validateRegion(buf);
}
