#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "prompt.h"
#include "emsys.h"
#include "terminal.h"
#include "display.h"
#include "keymap.h"
#include "unicode.h"
#include "tab.h"
#include "edit.h"
#include "buffer.h"
#include "util.h"

extern struct editorConfig E;

uint8_t *editorPrompt(struct editorBuffer *bufr, uint8_t *prompt,
		      enum promptType t,
		      void (*callback)(struct editorBuffer *, uint8_t *, int)) {
	uint8_t *result = NULL;

	/* Save current buffer context */
	struct editorBuffer *saved_edbuf = E.edbuf;

	/* Clear minibuffer */
	while (E.minibuf->numrows > 0) {
		editorDelRow(E.minibuf, 0);
	}
	editorInsertRow(E.minibuf, 0, "", 0);
	E.minibuf->cx = 0;
	E.minibuf->cy = 0;

	/* Switch to minibuffer */
	E.edbuf = E.buf;
	E.buf = E.minibuf;

	while (1) {
		/* Display prompt with minibuffer content */
		char *content = E.minibuf->numrows > 0 ?
					(char *)E.minibuf->row[0].chars :
					"";
		editorSetStatusMessage((char *)prompt, content);
		refreshScreen();

		/* Position cursor on bottom line */
		int prompt_width = stringWidth(prompt) - 2;
		cursorBottomLineLong(prompt_width + E.minibuf->cx + 1);

		/* Read key */
		int c = editorReadKey();
		editorRecordKey(c);

		/* Handle special minibuffer keys */
		switch (c) {
		case '\r':
			if (E.minibuf->numrows > 0 &&
			    E.minibuf->row[0].size > 0) {
				result = (uint8_t *)stringdup(
					(char *)E.minibuf->row[0].chars);
			} else {
				result = (uint8_t *)stringdup("");
			}
			goto done;

		case CTRL('g'):
		case CTRL('c'):
			result = NULL;
			goto done;

		case CTRL('i'): /* Tab completion */
			if (t == PROMPT_FILES) {
				char *old_text =
					E.minibuf->numrows > 0 ?
						stringdup((char *)E.minibuf
								  ->row[0]
								  .chars) :
						stringdup("");
				uint8_t *tc =
					tabCompleteFiles((uint8_t *)old_text);
				if (tc && tc != (uint8_t *)old_text) {
					/* Replace minibuffer content */
					editorDelRow(E.minibuf, 0);
					editorInsertRow(E.minibuf, 0,
							(char *)tc,
							strlen((char *)tc));
					E.minibuf->cx = strlen((char *)tc);
					E.minibuf->cy = 0;
					free(tc);
				}
				free(old_text);
			} else if (t == PROMPT_BASIC) {
				char *old_text =
					E.minibuf->numrows > 0 ?
						stringdup((char *)E.minibuf
								  ->row[0]
								  .chars) :
						stringdup("");
				uint8_t *tc = tabCompleteBufferNames(
					&E, (uint8_t *)old_text, bufr);
				if (tc && tc != (uint8_t *)old_text) {
					/* Replace minibuffer content */
					editorDelRow(E.minibuf, 0);
					editorInsertRow(E.minibuf, 0,
							(char *)tc,
							strlen((char *)tc));
					E.minibuf->cx = strlen((char *)tc);
					E.minibuf->cy = 0;
					free(tc);
				}
				free(old_text);
			} else if (t == PROMPT_COMMAND) {
				char *old_text =
					E.minibuf->numrows > 0 ?
						stringdup((char *)E.minibuf
								  ->row[0]
								  .chars) :
						stringdup("");
				uint8_t *tc = tabCompleteCommands(
					&E, (uint8_t *)old_text);
				if (tc && tc != (uint8_t *)old_text) {
					/* Replace minibuffer content */
					editorDelRow(E.minibuf, 0);
					editorInsertRow(E.minibuf, 0,
							(char *)tc,
							strlen((char *)tc));
					E.minibuf->cx = strlen((char *)tc);
					E.minibuf->cy = 0;
					free(tc);
				}
				free(old_text);
			}
			break;

		default:
			/* Let normal key processing handle it */
			editorProcessKeypress(c);

			/* Ensure single line */
			if (E.minibuf->numrows > 1) {
				/* Join all rows into first row */
				int total_len = 0;
				for (int i = 0; i < E.minibuf->numrows; i++) {
					total_len += E.minibuf->row[i].size;
				}

				char *joined = malloc(total_len + 1);
				joined[0] = 0;
				for (int i = 0; i < E.minibuf->numrows; i++) {
					if (E.minibuf->row[i].chars) {
						strncat(joined,
							(char *)E.minibuf
								->row[i]
								.chars,
							E.minibuf->row[i].size);
					}
				}

				while (E.minibuf->numrows > 0) {
					editorDelRow(E.minibuf, 0);
				}
				editorInsertRow(E.minibuf, 0, joined,
						strlen(joined));
				E.minibuf->cx = strlen(joined);
				E.minibuf->cy = 0;
				free(joined);
			}
		}

		/* Callback if provided */
		if (callback) {
			char *text = E.minibuf->numrows > 0 ?
					     (char *)E.minibuf->row[0].chars :
					     "";
			callback(bufr, (uint8_t *)text, c);
		}
	}

done:
	/* Restore previous buffer */
	E.buf = saved_edbuf;
	E.edbuf = saved_edbuf;

	editorSetStatusMessage("");
	return result;
}