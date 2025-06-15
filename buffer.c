#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "emsys.h"
#include "buffer.h"
#include "unicode.h"
#include "undo.h"
#include "prompt.h"
#include "display.h"
#include "util.h"
#include "terminal.h"

extern struct editorConfig E;

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
}

void editorRowAppendString(struct editorBuffer *bufr, erow *row, char *s,
			   size_t len) {
	row->chars = realloc(row->chars, row->size + len + 1);
	memcpy(&row->chars[row->size], s, len);
	row->size += len;
	row->chars[row->size] = '\0';
	editorUpdateRow(row);
	bufr->dirty = 1;
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
}

struct editorBuffer *newBuffer() {
	struct editorBuffer *ret = malloc(sizeof(struct editorBuffer));
	ret->indent = 0;
	ret->markx = -1;
	ret->marky = -1;
	ret->cx = 0;
	ret->cy = 0;
	ret->numrows = 0;
	ret->row = NULL;
	ret->filename = NULL;
	ret->query = NULL;
	ret->dirty = 0;
	ret->special_buffer = 0;
	ret->undo = newUndo();
	ret->redo = NULL;
	ret->next = NULL;
	ret->truncate_lines = 0;
	ret->single_line = 0;
	return ret;
}

void destroyBuffer(struct editorBuffer *buf) {
	clearUndosAndRedos(buf);
	free(buf->filename);
	free(buf);
}

void editorUpdateBuffer(struct editorBuffer *buf) {
	for (int i = 0; i < buf->numrows; i++) {
		editorUpdateRow(&buf->row[i]);
	}
}

void editorSwitchToNamedBuffer(struct editorConfig *ed,
			       struct editorBuffer *current) {
	char prompt[512];
	const char *defaultBufferName = NULL;

	if (ed->lastVisitedBuffer && ed->lastVisitedBuffer != current) {
		defaultBufferName = ed->lastVisitedBuffer->filename ?
					    ed->lastVisitedBuffer->filename :
					    "*scratch*";
	} else {
		// Find the first buffer that isn't the current one
		struct editorBuffer *defaultBuffer = ed->firstBuf;
		while (defaultBuffer == current && defaultBuffer->next) {
			defaultBuffer = defaultBuffer->next;
		}
		if (defaultBuffer != current) {
			defaultBufferName = defaultBuffer->filename ?
						    defaultBuffer->filename :
						    "*scratch*";
		}
	}

	if (defaultBufferName) {
		snprintf(prompt, sizeof(prompt),
			 "Switch to buffer (default %s): %%s",
			 defaultBufferName);
	} else {
		snprintf(prompt, sizeof(prompt), "Switch to buffer: %%s");
	}

	uint8_t *buffer_name =
		editorPrompt(current, (uint8_t *)prompt, PROMPT_BASIC, NULL);

	if (buffer_name == NULL) {
		// User canceled the prompt
		editorSetStatusMessage("Buffer switch canceled");
		return;
	}

	struct editorBuffer *targetBuffer = NULL;

	if (buffer_name[0] == '\0') {
		// User pressed Enter without typing anything
		if (defaultBufferName) {
			// Find the default buffer
			for (struct editorBuffer *buf = ed->firstBuf;
			     buf != NULL; buf = buf->next) {
				if (buf == current)
					continue;
				if ((buf->filename &&
				     strcmp(buf->filename, defaultBufferName) ==
					     0) ||
				    (!buf->filename &&
				     strcmp("*scratch*", defaultBufferName) ==
					     0)) {
					targetBuffer = buf;
					break;
				}
			}
		}
		if (!targetBuffer) {
			editorSetStatusMessage("No buffer to switch to");
			free(buffer_name);
			return;
		}
	} else {
		for (struct editorBuffer *buf = ed->firstBuf; buf != NULL;
		     buf = buf->next) {
			if (buf == current)
				continue;

			const char *bufName = buf->filename ? buf->filename :
							      "*scratch*";
			if (strcmp((char *)buffer_name, bufName) == 0) {
				targetBuffer = buf;
				break;
			}
		}

		if (!targetBuffer) {
			editorSetStatusMessage("No buffer named '%s'",
					       buffer_name);
			free(buffer_name);
			return;
		}
	}

	if (targetBuffer) {
		ed->lastVisitedBuffer =
			current; // Update the last visited buffer
		ed->buf = targetBuffer;

		const char *switchedBufferName =
			ed->buf->filename ? ed->buf->filename : "*scratch*";
		editorSetStatusMessage("Switched to buffer %s",
				       switchedBufferName);

		for (int i = 0; i < ed->nwindows; i++) {
			if (ed->windows[i]->focused) {
				ed->windows[i]->buf = ed->buf;
			}
		}
	}

	free(buffer_name);
}

void editorNextBuffer(void) {
	E.buf = E.buf->next;
	if (E.buf == NULL) {
		E.buf = E.firstBuf;
	}
	for (int i = 0; i < E.nwindows; i++) {
		if (E.windows[i]->focused) {
			E.windows[i]->buf = E.buf;
		}
	}
}

void editorPreviousBuffer(void) {
	if (E.buf == E.firstBuf) {
		// If we're at the first buffer, go to the last buffer
		E.buf = E.firstBuf;
		while (E.buf->next != NULL) {
			E.buf = E.buf->next;
		}
	} else {
		// Otherwise, go to the previous buffer
		struct editorBuffer *temp = E.firstBuf;
		while (temp->next != E.buf) {
			temp = temp->next;
		}
		E.buf = temp;
	}
	// Update the focused buffer in all windows
	for (int i = 0; i < E.nwindows; i++) {
		if (E.windows[i]->focused) {
			E.windows[i]->buf = E.buf;
		}
	}
}

void editorKillBuffer(void) {
	struct editorBuffer *bufr = E.buf;
	
	// Bypass confirmation for special buffers
	if (bufr->dirty && bufr->filename != NULL &&
	    !bufr->special_buffer) {
		editorSetStatusMessage(
			"Buffer %.20s modified; kill anyway? (y or n)",
			bufr->filename);
		editorRefreshScreen();
		int c = editorReadKey();
		if (c != 'y' && c != 'Y') {
			editorSetStatusMessage("");
			return;
		}
	}

	// Find the previous buffer (if any)
	struct editorBuffer *prevBuf = NULL;
	if (E.buf != E.firstBuf) {
		prevBuf = E.firstBuf;
		while (prevBuf->next != E.buf) {
			prevBuf = prevBuf->next;
		}
	}

	// Update window focus
	for (int i = 0; i < E.nwindows; i++) {
		if (E.windows[i]->buf == bufr) {
			// If it's the last buffer, create a new scratch buffer
			if (bufr->next == NULL && prevBuf == NULL) {
				E.windows[i]->buf = newBuffer();
				E.windows[i]->buf->filename =
					stringdup("*scratch*");
				E.windows[i]->buf->special_buffer = 1;
				E.firstBuf = E.windows[i]->buf;
				E.buf = E.firstBuf; // Ensure E.buf is updated
			} else if (bufr->next == NULL) {
				E.windows[i]->buf = E.firstBuf;
				E.buf = E.firstBuf; // Ensure E.buf is updated
			} else {
				E.windows[i]->buf = bufr->next;
				E.buf = bufr->next; // Ensure E.buf is updated
			}
		}
	}

	// Update the main buffer list
	if (E.firstBuf == bufr) {
		E.firstBuf = bufr->next;
	} else if (prevBuf != NULL) {
		prevBuf->next = bufr->next;
	}

	// Update the focused buffer
	if (E.buf == bufr) {
		E.buf = (bufr->next != NULL) ? bufr->next : prevBuf;
	}

	destroyBuffer(bufr);
}
