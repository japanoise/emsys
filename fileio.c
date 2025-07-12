#include "util.h"

#include "emsys.h"
#include "fileio.h"
#include "buffer.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/select.h>
#include "display.h"
#include "prompt.h"
#include "util.h"
#include "undo.h"
#include "keymap.h"
#include "unused.h"

/* Access global editor state */
extern struct editorConfig E;

/* External functions we need */
extern void die(const char *s);

/*** file i/o ***/

char *editorRowsToString(struct editorBuffer *bufr, int *buflen) {
	int totlen = 0;
	int j;
	for (j = 0; j < bufr->numrows; j++) {
		totlen += bufr->row[j].size + 1;
	}
	*buflen = totlen;

	char *buf = xmalloc(totlen);
	char *p = buf;
	for (j = 0; j < bufr->numrows; j++) {
		memcpy(p, bufr->row[j].chars, bufr->row[j].size);
		p += bufr->row[j].size;
		*p = '\n';
		p++;
	}

	return buf;
}

void editorOpen(struct editorBuffer *bufr, char *filename) {
	free(bufr->filename);
	bufr->filename = xstrdup(filename);

	FILE *fp = fopen(filename, "r");
	if (!fp) {
		if (errno == ENOENT) {
			editorSetStatusMessage("(New file)", bufr->filename);
			return;
		}
		editorSetStatusMessage("Can't open file: %s", strerror(errno));
		return;
	}

	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;

	while ((linelen = emsys_getline(&line, &linecap, fp)) != -1) {
		while (linelen > 0 &&
		       (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
			linelen--;
		editorInsertRow(bufr, bufr->numrows, line, linelen);
	}

	free(line);
	fclose(fp);
	bufr->dirty = 0;
}

void editorRevert(struct editorConfig *ed, struct editorBuffer *buf) {
	struct editorBuffer *new = newBuffer();
	editorOpen(new, buf->filename);
	new->next = buf->next;
	ed->buf = new;
	if (ed->headbuf == buf) {
		ed->headbuf = new;
	}
	struct editorBuffer *cur = ed->headbuf;
	while (cur != NULL) {
		if (cur->next == buf) {
			cur->next = new;
			break;
		}
		cur = cur->next;
	}
	for (int i = 0; i < ed->nwindows; i++) {
		if (ed->windows[i]->buf == buf) {
			ed->windows[i]->buf = new;
		}
	}
	new->indent = buf->indent;
	new->cx = buf->cx;
	new->cy = buf->cy;
	if (new->numrows == 0) {
		new->cy = 0;
		new->cx = 0;
	} else if (new->cy >= new->numrows) {
		new->cy = new->numrows - 1;
		new->cx = 0;
	} else if (new->cx > new->row[new->cy].size) {
		new->cx = new->row[new->cy].size;
	}
	destroyBuffer(buf);
}

void editorSave(struct editorBuffer *bufr) {
	if (bufr->filename == NULL) {
		bufr->filename = (char *)editorPrompt(
			bufr, (uint8_t *)"Save as: %s", PROMPT_FILES, NULL);
		if (bufr->filename == NULL) {
			editorSetStatusMessage("Save aborted.");
			return;
		}
	}

	int len;
	char *buf = editorRowsToString(bufr, &len);

	int fd = open(bufr->filename, O_RDWR | O_CREAT, 0644);
	if (fd != -1) {
		if (ftruncate(fd, len) != -1) {
			if (write(fd, buf, len)) {
				close(fd);
				free(buf);
				bufr->dirty = 0;

				// Clear undo/redo on successful save
				clearUndosAndRedos(bufr);

				editorSetStatusMessage(
					"Wrote %d bytes to %s (undo history cleared)",
					len, bufr->filename);
				return;
			}
		}
		close(fd);
	}

	free(buf);
	editorSetStatusMessage("Save failed: %s", strerror(errno));
}

void findFile(void) {
	struct editorConfig *E_ptr = &E;
	uint8_t *prompt =
		editorPrompt(E_ptr->buf, "Find File: %s", PROMPT_FILES, NULL);

	if (prompt == NULL) {
		editorSetStatusMessage("Canceled.");
		return;
	}

	if (prompt[strlen(prompt) - 1] == '/') {
		editorSetStatusMessage("Directory editing not supported.");
		free(prompt);
		return;
	}

	// Check if a buffer with the same filename already exists
	struct editorBuffer *buf = E_ptr->headbuf;
	while (buf != NULL) {
		if (buf->filename != NULL &&
		    strcmp(buf->filename, (char *)prompt) == 0) {
			editorSetStatusMessage(
				"File '%s' already open in a buffer.", prompt);
			free(prompt);
			E_ptr->buf = buf; // Switch to the existing buffer

			// Update the focused window to display the found buffer
			int idx = windowFocusedIdx();
			E_ptr->windows[idx]->buf = E_ptr->buf;

			refreshScreen(); // Refresh to reflect the change
			return;
		}
		buf = buf->next;
	}

	// Create new buffer for the file
	struct editorBuffer *newBuf = newBuffer();
	editorOpen(newBuf, (char *)prompt);
	free(prompt);

	newBuf->next = E_ptr->headbuf;
	E_ptr->headbuf = newBuf;
	E_ptr->buf = newBuf;
	int idx = windowFocusedIdx();
	E_ptr->windows[idx]->buf = E_ptr->buf;
}

void editorInsertFile(struct editorConfig *UNUSED(ed),
		      struct editorBuffer *buf) {
	uint8_t *filename =
		editorPrompt(buf, "Insert file: %s", PROMPT_FILES, NULL);
	if (filename == NULL) {
		return;
	}

	FILE *fp = fopen((char *)filename, "r");
	if (!fp) {
		if (errno == ENOENT) {
			editorSetStatusMessage("File not found: %s", filename);
		} else {
			editorSetStatusMessage("Error opening file: %s",
					       strerror(errno));
		}
		free(filename);
		return;
	}

	int saved_cy = buf->cy;

	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	int lines_inserted = 0;

	while ((linelen = emsys_getline(&line, &linecap, fp)) != -1) {
		while (linelen > 0 && (line[linelen - 1] == '\n' ||
				       line[linelen - 1] == '\r')) {
			linelen--;
		}

		editorInsertRow(buf, saved_cy + lines_inserted, line, linelen);
		lines_inserted++;
	}

	free(line);
	fclose(fp);

	if (lines_inserted > 0) {
		buf->cy = saved_cy + lines_inserted - 1;
		buf->cx = buf->row[buf->cy].size;
	}

	editorSetStatusMessage("Inserted %d lines from %s", lines_inserted,
			       filename);
	free(filename);

	buf->dirty++;
}
