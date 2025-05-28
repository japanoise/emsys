#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "emsys.h"
#include "row.h"
#include "unicode.h"

extern struct editorConfig E;

void insertRow(struct editorBuffer *buf, int at, char *s, size_t len) {
	if (at < 0 || at > buf->numrows)
		return;

	buf->row = xrealloc(buf->row, sizeof(erow) * (buf->numrows + 1));
	memmove(&buf->row[at + 1], &buf->row[at],
		sizeof(erow) * (buf->numrows - at));

	buf->row[at].size = len;
	buf->row[at].chars = xmalloc(len + 1);
	memcpy(buf->row[at].chars, s, len);
	buf->row[at].chars[len] = '\0';
	buf->row[at].cached_width = 0;
	buf->row[at].width_valid = 0;

	buf->numrows++;
	buf->dirty = 1;
	invalidateScreenCache(buf);
}

void freeRow(erow *row) {
	free(row->chars);
}

void delRow(struct editorBuffer *buf, int at) {
	if (at < 0 || at >= buf->numrows)
		return;
	freeRow(&buf->row[at]);
	memmove(&buf->row[at], &buf->row[at + 1],
		sizeof(erow) * (buf->numrows - at - 1));
	buf->numrows--;
	buf->dirty = 1;
	invalidateScreenCache(buf);
}

void rowInsertChar(erow *row, int at, int c) {
	if (at < 0 || at > row->size)
		at = row->size;
	row->chars = xrealloc(row->chars, row->size + 2);
	memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
	row->size++;
	row->chars[at] = c;
	row->width_valid = 0;
	E.buf->dirty = 1;
	invalidateScreenCache(E.buf);
}

void rowInsertUnicode(erow *row, int at) {
	if (at < 0 || at > row->size)
		at = row->size;
	row->chars = xrealloc(row->chars, row->size + 1 + E.nunicode);
	memmove(&row->chars[at + E.nunicode], &row->chars[at],
		row->size - at + 1);
	row->size += E.nunicode;
	memcpy(&row->chars[at], E.unicode, E.nunicode);
	row->width_valid = 0;
	E.buf->dirty = 1;
	invalidateScreenCache(E.buf);
}

void rowDelChar(erow *row, int at) {
	if (at < 0 || at >= row->size)
		return;
	int size = utf8_nBytes(row->chars[at]);
	memmove(&row->chars[at], &row->chars[at + size],
		row->size - ((at + size) - 1));
	row->size -= size;
	row->width_valid = 0;
	E.buf->dirty = 1;
	invalidateScreenCache(E.buf);
}

void rowInsertString(erow *row, int at, char *s, size_t len) {
	if (at < 0 || at > row->size)
		at = row->size;
	row->chars = xrealloc(row->chars, row->size + len + 1);
	memmove(&row->chars[at + len], &row->chars[at], row->size - at + 1);
	memcpy(&row->chars[at], s, len);
	row->size += len;
	row->width_valid = 0;
	E.buf->dirty = 1;
	invalidateScreenCache(E.buf);
}

void rowDeleteRange(erow *row, int start, int end) {
	if (start < 0 || end > row->size || start >= end)
		return;
	memmove(&row->chars[start], &row->chars[end], row->size - end);
	row->size -= (end - start);
	row->chars[row->size] = '\0';
	row->width_valid = 0;
	E.buf->dirty = 1;
	invalidateScreenCache(E.buf);
}

void rowReplaceRange(erow *row, int start, int end, char *s, size_t len) {
	if (start < 0 || end > row->size || start > end)
		return;
	int delete_len = end - start;
	int size_change = len - delete_len;

	if (size_change > 0) {
		row->chars = xrealloc(row->chars, row->size + size_change + 1);
	}

	memmove(&row->chars[start + len], &row->chars[end],
		row->size - end + 1);
	memcpy(&row->chars[start], s, len);
	row->size += size_change;
	row->width_valid = 0;
	E.buf->dirty = 1;
	invalidateScreenCache(E.buf);
}

int calculateLineWidth(erow *row) {
	if (row->width_valid) {
		return row->cached_width;
	}

	int screen_x = 0;
	for (int i = 0; i < row->size;) {
		screen_x = nextScreenX(row->chars, &i, screen_x);
		i++;
	}

	row->cached_width = screen_x;
	row->width_valid = 1;
	return screen_x;
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
