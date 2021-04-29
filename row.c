#include<stdint.h>
#include<stdlib.h>
#include<string.h>
#include"emsys.h"
#include"row.h"
#include"unicode.h"

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
			extra+=8;
		}
	}
	
	free(row->render);
	row->render = malloc(row->size + tabs*(EMSYS_TAB_STOP - 1) + extra + 1);
	row->renderwidth = 0;

	int idx = 0;
	for (j = 0; j < row->size; j++) {
		if (row->chars[j] == '\t') {
			row->renderwidth += EMSYS_TAB_STOP;
			row->render[idx++] = ' ';
			while (idx % EMSYS_TAB_STOP != 0) row->render[idx++] = ' ';
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
			row->render[idx++] = row->chars[j]|0x40;
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
	row->rsize=idx;
}

void editorInsertRow(struct editorConfig *ed, int at, char *s, size_t len) {
	if (at < 0 || at > ed->buf.numrows) return;

	ed->buf.row = realloc(ed->buf.row, sizeof(erow) * (ed->buf.numrows + 1));
	memmove(&ed->buf.row[at + 1], &ed->buf.row[at], sizeof(erow) * (ed->buf.numrows - at));

	ed->buf.row[at].size = len;
	ed->buf.row[at].chars = malloc(len + 1);
	memcpy(ed->buf.row[at].chars, s, len);
	ed->buf.row[at].chars[len] = '\0';

	ed->buf.row[at].rsize = 0;
	ed->buf.row[at].render = NULL;
	editorUpdateRow(&ed->buf.row[at]);

	ed->buf.numrows++;
	ed->buf.dirty = 1;
}

void editorFreeRow(erow *row) {
	free(row->render);
	free(row->chars);
}

void editorDelRow(struct editorConfig *ed, int at) {
	if (at < 0 || at >= ed->buf.numrows) return;
	editorFreeRow(&ed->buf.row[at]);
	memmove(&ed->buf.row[at], &ed->buf.row[at + 1],
		sizeof(erow) * (ed->buf.numrows - at - 1));
	ed->buf.numrows--;
	ed->buf.dirty = 1;
}

void editorRowInsertChar(struct editorConfig *ed, erow *row, int at, int c) {
	if (at < 0 || at > row->size) at = row->size;
	row->chars = realloc(row->chars, row->size +2);
	memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
	row->size++;
	row->chars[at] = c;
	editorUpdateRow(row);
	ed->buf.dirty = 1;
}

void editorRowInsertUnicode(struct editorConfig *ed, erow *row, int at) {
	if (at < 0 || at > row->size) at = row->size;
	row->chars = realloc(row->chars, row->size + 1 + ed->nunicode);
	memmove(&row->chars[at + ed->nunicode], &row->chars[at], row->size - at + ed->nunicode);
	row->size += ed->nunicode;
	for (int i = 0; i < ed->nunicode; i++) {
		row->chars[at+i] = ed->unicode[i];
	}
	editorUpdateRow(row);
   	ed->buf.dirty = 1;
}

void editorRowAppendString(struct editorConfig *ed, erow *row, char *s, size_t len) {
	row->chars = realloc(row->chars, row->size + len + 1);
	memcpy(&row->chars[row->size], s, len);
	row->size += len;
	row->chars[row->size] = '\0';
	editorUpdateRow(row);
	ed->buf.dirty = 1;
}

void editorRowDelChar(struct editorConfig *ed, erow *row, int at) {
	if (at < 0 || at >= row->size) return;
	int size = utf8_nBytes(row->chars[at]);
	memmove(&row->chars[at],
		&row->chars[at+size], row->size - ((at+size)-1));
	row->size -= size;
	editorUpdateRow(row);
	ed->buf.dirty = 1;
}
