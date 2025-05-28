#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "emsys.h"
#include "row.h"
#include "unicode.h"

void editorUpdateRow(erow *row) {
	int tabs = 0;
	int extra = 0;
	int j;
	for (j = 0; j < row->size; j++) {
		if (row->chars[j] == '\t') {
			tabs++;
		} else if (ISCTRL(row->chars[j])) {
			/*
			 * These need an extra few bytes to display
			 * CSI 7 m - 4 bytes
			 * CSI m - 3 bytes
			 * preceding ^ - 1 byte
			 */
			extra += 8;
		}
	}

	free(row->render);
	row->render =
		malloc(row->size + tabs * (EMSYS_TAB_STOP - 1) + extra + 1);
	row->renderwidth = 0;

	int idx = 0;
	for (j = 0; j < row->size; j++) {
		if (row->chars[j] == '\t') {
			row->renderwidth += EMSYS_TAB_STOP;
			row->render[idx++] = ' ';
			while (idx % EMSYS_TAB_STOP != 0)
				row->render[idx++] = ' ';
		} else if (row->chars[j] == 0x7f) {
			row->renderwidth += 2;
			row->render[idx++] = 0x1b;
			row->render[idx++] = '[';
			row->render[idx++] = '7';
			row->render[idx++] = 'm';
			row->render[idx++] = '^';
			row->render[idx++] = '?';
			row->render[idx++] = 0x1b;
			row->render[idx++] = '[';
			row->render[idx++] = 'm';
		} else if (ISCTRL(row->chars[j])) {
			row->renderwidth += 2;
			row->render[idx++] = 0x1b;
			row->render[idx++] = '[';
			row->render[idx++] = '7';
			row->render[idx++] = 'm';
			row->render[idx++] = '^';
			row->render[idx++] = row->chars[j] | 0x40;
			row->render[idx++] = 0x1b;
			row->render[idx++] = '[';
			row->render[idx++] = 'm';
		} else if (row->chars[j] > 0x7f) {
			int width = charInStringWidth(row->chars, idx);
			row->render[idx++] = row->chars[j];
			row->renderwidth += width;
		} else if (utf8_isCont(row->chars[j])) {
			row->render[idx++] = row->chars[j];
		} else {
			row->renderwidth += 1;
			row->render[idx++] = row->chars[j];
		}
	}
	row->render[idx] = 0;
	row->rsize = idx;
}

void editorInsertRow(struct editorBuffer *bufr, int at, char *s, size_t len) {
	if (at < 0 || at > bufr->numrows)
		return;

	bufr->row = realloc(bufr->row, sizeof(erow) * (bufr->numrows + 1));
	memmove(&bufr->row[at + 1], &bufr->row[at],
		sizeof(erow) * (bufr->numrows - at));

	bufr->row[at].size = len;
	bufr->row[at].chars = malloc(len + 1);
	memcpy(bufr->row[at].chars, s, len);
	bufr->row[at].chars[len] = '\0';

	bufr->row[at].rsize = 0;
	bufr->row[at].render = NULL;
	editorUpdateRow(&bufr->row[at]);

	bufr->numrows++;
	bufr->dirty = 1;
	invalidateScreenCache(bufr);
}

void editorFreeRow(erow *row) {
	free(row->render);
	free(row->chars);
}

void editorDelRow(struct editorBuffer *bufr, int at) {
	if (at < 0 || at >= bufr->numrows)
		return;
	editorFreeRow(&bufr->row[at]);
	memmove(&bufr->row[at], &bufr->row[at + 1],
		sizeof(erow) * (bufr->numrows - at - 1));
	bufr->numrows--;
	bufr->dirty = 1;
	invalidateScreenCache(bufr);
}

void editorRowInsertChar(struct editorBuffer *bufr, erow *row, int at, int c) {
	if (at < 0 || at > row->size)
		at = row->size;
	row->chars = realloc(row->chars, row->size + 2);
	memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
	row->size++;
	row->chars[at] = c;
	editorUpdateRow(row);
	bufr->dirty = 1;
	invalidateScreenCache(bufr);
}

void editorRowInsertUnicode(struct editorConfig *ed, struct editorBuffer *bufr,
			    erow *row, int at) {
	if (at < 0 || at > row->size)
		at = row->size;
	row->chars = realloc(row->chars, row->size + 1 + ed->nunicode);
	memmove(&row->chars[at + ed->nunicode], &row->chars[at],
		row->size - at + 1);
	row->size += ed->nunicode;
	memcpy(&row->chars[at], ed->unicode, ed->nunicode);
	editorUpdateRow(row);
	bufr->dirty = 1;
	invalidateScreenCache(bufr);
}

void editorRowAppendString(struct editorBuffer *bufr, erow *row, char *s,
			   size_t len) {
	row->chars = realloc(row->chars, row->size + len + 1);
	memcpy(&row->chars[row->size], s, len);
	row->size += len;
	row->chars[row->size] = '\0';
	editorUpdateRow(row);
	bufr->dirty = 1;
	invalidateScreenCache(bufr);
}

void editorRowDelChar(struct editorBuffer *bufr, erow *row, int at) {
	if (at < 0 || at >= row->size)
		return;
	int size = utf8_nBytes(row->chars[at]);
	memmove(&row->chars[at], &row->chars[at + size],
		row->size - ((at + size) - 1));
	row->size -= size;
	editorUpdateRow(row);
	bufr->dirty = 1;
	invalidateScreenCache(bufr);
}

int charsToRenderIndex(erow *row, int chars_idx) {
	if (!row || chars_idx < 0)
		return 0;
	if (chars_idx >= row->size)
		return row->rsize;

	int render_idx = 0;
	for (int i = 0; i < chars_idx && i < row->size; i++) {
		if (row->chars[i] == '\t') {
			render_idx++;
			while (render_idx % EMSYS_TAB_STOP != 0) {
				render_idx++;
			}
		} else if (row->chars[i] == 0x7f) {
			render_idx += 9; // ESC[7m^?ESC[m
		} else if (ISCTRL(row->chars[i])) {
			render_idx += 9; // ESC[7m^XESC[m
		} else if (row->chars[i] > 0x7f) {
			render_idx++;
		} else if (utf8_isCont(row->chars[i])) {
			render_idx++;
		} else {
			render_idx++;
		}
	}
	return render_idx;
}

int renderToCharsIndex(erow *row, int render_idx) {
	if (!row || render_idx <= 0)
		return 0;
	if (render_idx >= row->rsize)
		return row->size;

	int current_render_idx = 0;
	for (int chars_idx = 0; chars_idx < row->size; chars_idx++) {
		if (current_render_idx >= render_idx) {
			return chars_idx;
		}

		if (row->chars[chars_idx] == '\t') {
			current_render_idx++;
			while (current_render_idx % EMSYS_TAB_STOP != 0) {
				current_render_idx++;
				if (current_render_idx >= render_idx) {
					return chars_idx;
				}
			}
		} else if (ISCTRL(row->chars[chars_idx])) {
			current_render_idx += 9;
		} else if (row->chars[chars_idx] > 0x7f) {
			current_render_idx++;
		} else if (utf8_isCont(row->chars[chars_idx])) {
			current_render_idx++;
		} else {
			current_render_idx++;
		}
	}
	return row->size;
}

int charsToScreenWidth(erow *row, int chars_idx) {
	if (!row || chars_idx < 0)
		return 0;
	if (chars_idx >= row->size)
		return row->renderwidth;

	int screen_width = 0;
	for (int i = 0; i < chars_idx && i < row->size; i++) {
		if (row->chars[i] == '\t') {
			screen_width++;
			while (screen_width % EMSYS_TAB_STOP != 0) {
				screen_width++;
			}
		} else if (row->chars[i] == 0x7f) {
			screen_width += 2;
		} else if (ISCTRL(row->chars[i])) {
			screen_width += 2;
		} else if (row->chars[i] > 0x7f) {
			int width = charInStringWidth(row->chars, i);
			screen_width += width;
		} else if (utf8_isCont(row->chars[i])) {
		} else {
			screen_width += 1;
		}
	}
	return screen_width;
}
