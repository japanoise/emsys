#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "emsys.h"
#include "row.h"
#include "unicode.h"

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

	bufr->numrows++;
	bufr->dirty = 1;
	invalidateScreenCache(bufr);
}

void editorFreeRow(erow *row) {
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
	bufr->dirty = 1;
	invalidateScreenCache(bufr);
}

void editorRowAppendString(struct editorBuffer *bufr, erow *row, char *s,
			   size_t len) {
	row->chars = realloc(row->chars, row->size + len + 1);
	memcpy(&row->chars[row->size], s, len);
	row->size += len;
	row->chars[row->size] = '\0';
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
	bufr->dirty = 1;
	invalidateScreenCache(bufr);
}

int calculateLineWidth(erow *row) {
	int width = 0;
	for (int i = 0; i < row->size; i++) {
		if (row->chars[i] == '\t') {
			width = (width + EMSYS_TAB_STOP) / EMSYS_TAB_STOP *
				EMSYS_TAB_STOP;
		} else if (ISCTRL(row->chars[i])) {
			width += 2;
		} else {
			width += charInStringWidth(row->chars, i);
		}
		i += utf8_nBytes(row->chars[i]) - 1;
	}
	return width;
}

int charsToDisplayColumn(erow *row, int chars_idx) {
	if (!row || chars_idx < 0)
		return 0;
	if (chars_idx >= row->size) {
		return calculateLineWidth(row);
	}

	int col = 0;
	for (int i = 0; i < chars_idx && i < row->size; i++) {
		if (row->chars[i] == '\t') {
			col = (col + EMSYS_TAB_STOP) / EMSYS_TAB_STOP *
			      EMSYS_TAB_STOP;
		} else if (ISCTRL(row->chars[i])) {
			col += 2;
		} else {
			col += charInStringWidth(row->chars, i);
		}
		i += utf8_nBytes(row->chars[i]) - 1;
	}
	return col;
}

int displayColumnToChars(erow *row, int display_col) {
	if (!row || display_col <= 0)
		return 0;

	int current_col = 0;
	for (int chars_idx = 0; chars_idx < row->size; chars_idx++) {
		if (current_col >= display_col) {
			return chars_idx;
		}

		if (row->chars[chars_idx] == '\t') {
			int next_tab_stop = (current_col + EMSYS_TAB_STOP) /
					    EMSYS_TAB_STOP * EMSYS_TAB_STOP;
			if (next_tab_stop > display_col) {
				return chars_idx;
			}
			current_col = next_tab_stop;
		} else if (ISCTRL(row->chars[chars_idx])) {
			current_col += 2;
		} else {
			current_col += charInStringWidth(row->chars, chars_idx);
		}

		chars_idx += utf8_nBytes(row->chars[chars_idx]) - 1;
	}
	return row->size;
}
