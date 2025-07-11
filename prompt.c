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
#include "edit.h"
#include "buffer.h"
#include "util.h"
#include "completion.h"
#include "history.h"

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
		if (!E.minibuf->completion_state.preserve_message) {
			editorSetStatusMessage((char *)prompt, content);
		}
		E.minibuf->completion_state.preserve_message = 0;
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
				result = (uint8_t *)xstrdup(
					(char *)E.minibuf->row[0].chars);
			} else {
				result = (uint8_t *)xstrdup("");
			}
			goto done;

		case CTRL('g'):
		case CTRL('c'):
			result = NULL;
			goto done;

		case '\t':
			handleMinibufferCompletion(E.minibuf, t);
			break;

		case HISTORY_PREV:
		case HISTORY_NEXT:
		{
			struct editorHistory *hist = NULL;
			char *history_str = NULL;
			
			switch (t) {
			case PROMPT_FILES:
				hist = &E.file_history;
				break;
			case PROMPT_COMMAND:
				hist = &E.command_history;
				break;
			case PROMPT_BASIC:
				hist = &E.shell_history;
				break;
			}
			
			if (hist) {
				if (c == HISTORY_PREV) {
					history_str = getPrevHistory(hist);
				} else {
					history_str = getNextHistory(hist);
				}
				
				if (history_str) {
					while (E.minibuf->numrows > 0) {
						editorDelRow(E.minibuf, 0);
					}
					editorInsertRow(E.minibuf, 0, history_str, strlen(history_str));
					E.minibuf->cx = strlen(history_str);
					E.minibuf->cy = 0;
				}
			}
			break;
		}

		default:
			if (E.minibuf->completion_state.last_completed_text != NULL) {
				resetCompletionState(&E.minibuf->completion_state);
			}
			
			editorProcessKeypress(c);

			/* Ensure single line */
			if (E.minibuf->numrows > 1) {
				/* Join all rows into first row */
				int total_len = 0;
				for (int i = 0; i < E.minibuf->numrows; i++) {
					total_len += E.minibuf->row[i].size;
				}

				char *joined = xmalloc(total_len + 1);
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

		if (callback) {
			char *text = E.minibuf->numrows > 0 ?
					     (char *)E.minibuf->row[0].chars :
					     "";
			callback(bufr, (uint8_t *)text, c);
		}
	}

done:
	if (result && strlen((char *)result) > 0) {
		struct editorHistory *hist = NULL;
		switch (t) {
		case PROMPT_FILES:
			hist = &E.file_history;
			break;
		case PROMPT_COMMAND:
			hist = &E.command_history;
			break;
		case PROMPT_BASIC:
			hist = &E.shell_history;
			break;
		}
		if (hist) {
			addHistory(hist, (char *)result);
		}
	}
	
	closeCompletionsBuffer();
	
	/* Destroy the completions buffer entirely */
	struct editorBuffer *comp_buf = NULL;
	for (struct editorBuffer *b = E.headbuf; b != NULL; b = b->next) {
		if (b->filename && strcmp(b->filename, "*Completions*") == 0) {
			comp_buf = b;
			break;
		}
	}
	if (comp_buf) {
		destroyBuffer(comp_buf);
	}
	
	/* Restore previous buffer */
	E.buf = saved_edbuf;
	E.edbuf = saved_edbuf;

	editorSetStatusMessage("");
	
	return result;
}
