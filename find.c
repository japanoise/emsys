#include "find.h"
#include "emsys.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "display.h"
#include "keymap.h"
#include "terminal.h"
#include "transform.h"
#include "undo.h"
#include "unicode.h"
#include "region.h"

// https://stackoverflow.com/a/779960
// You must free the result if result is non-NULL.
uint8_t *orig;
uint8_t *repl;

char *str_replace(char *orig, char *rep, char *with) {
	char *result;  // the return string
	char *ins;     // the next insert point
	char *tmp;     // varies
	int len_rep;   // length of rep (the string to remove)
	int len_with;  // length of with (the string to replace rep with)
	int len_front; // distance between rep and end of last rep
	int count;     // number of replacements

	// sanity checks and initialization
	if (!orig || !rep)
		return NULL;
	len_rep = strlen(rep);
	if (len_rep == 0)
		return NULL; // empty rep causes infinite loop during count
	if (!with)
		with = "";
	len_with = strlen(with);

	// count the number of replacements needed
	ins = orig;
	for (count = 0; (tmp = strstr(ins, rep)); ++count) {
		ins = tmp + len_rep;
	}

	tmp = result = malloc(strlen(orig) + (len_with - len_rep) * count + 1);

	if (!result)
		return NULL;

	// first time through the loop, all the variable are set correctly
	// from here on,
	//    tmp points to the end of the result string
	//    ins points to the next occurrence of rep in orig
	//    orig points to the remainder of orig after "end of rep"
	while (count--) {
		ins = strstr(orig, rep);
		len_front = ins - orig;
		tmp = strncpy(tmp, orig, len_front) + len_front;
		tmp = strcpy(tmp, with) + len_with;
		orig += len_front + len_rep; // move to next "end of rep"
	}
	strcpy(tmp, orig);
	return result;
}

void editorFindCallback(struct editorBuffer *bufr, uint8_t *query, int key) {
	static int last_match = -1;
	static int direction = 1;
	bufr->query = query;
	bufr->match = 0;

	if (key == CTRL('g') || key == CTRL('c') || key == '\r') {
		last_match = -1;
		direction = 1;
		return;
	} else if (key == CTRL('s')) {
		direction = 1;
	} else if (key == CTRL('r')) {
		direction = -1;
	} else {
		last_match = -1;
		direction = 1;
	}

	if (last_match == -1)
		direction = 1;
	int current = last_match;
	if (current >= 0) {
		erow *row = &bufr->row[current];
		uint8_t *match = strstr(&(row->chars[bufr->cx + 1]), query);
		if (match) {
			last_match = current;
			bufr->cy = current;
			bufr->cx = match - row->chars;
			editorScroll();
			//			bufr->rowoff = bufr->numrows;
			bufr->match = 1;
			return;
		}
	}
	for (int i = 0; i < bufr->numrows; i++) {
		current += direction;
		if (current == -1)
			current = bufr->numrows - 1;
		else if (current == bufr->numrows)
			current = 0;

		erow *row = &bufr->row[current];
		uint8_t *match = strstr(row->chars, query);
		if (match) {
			last_match = current;
			bufr->cy = current;
			bufr->cx = match - row->chars;
			editorScroll();
			//			bufr->rowoff = bufr->numrows;
			bufr->match = 1;
			break;
		}
	}
}

void editorFind(struct editorBuffer *bufr) {
	int saved_cx = bufr->cx;
	int saved_cy = bufr->cy;
	//	int saved_rowoff = bufr->rowoff;

	uint8_t *query = editorPrompt(bufr, "Search (C-g to cancel): %s",
				      PROMPT_BASIC, editorFindCallback);

	bufr->query = NULL;
	if (query) {
		free(query);
	} else {
		bufr->cx = saved_cx;
		bufr->cy = saved_cy;
		//		bufr->rowoff = saved_rowoff;
	}
}

uint8_t *transformerReplaceString(uint8_t *input) {
	return str_replace(input, orig, repl);
}

void editorReplaceString(struct editorConfig *ed, struct editorBuffer *buf) {
	orig = NULL;
	repl = NULL;
	orig = editorPrompt(buf, "Replace: %s", PROMPT_BASIC, NULL);
	if (orig == NULL) {
		editorSetStatusMessage("Canceled replace-string.");
		return;
	}

	uint8_t *prompt = malloc(strlen(orig) + 20);
	sprintf(prompt, "Replace %s with: %%s", orig);
	repl = editorPrompt(buf, prompt, PROMPT_BASIC, NULL);
	free(prompt);
	if (repl == NULL) {
		free(orig);
		editorSetStatusMessage("Canceled replace-string.");
		return;
	}

	editorTransformRegion(ed, buf, transformerReplaceString);

	free(orig);
	free(repl);
}

static int nextOccur(struct editorBuffer *buf, uint8_t *needle, int ocheck) {
	int ox = buf->cx;
	int oy = buf->cy;
	if (!ocheck) {
		ox = -69;
	}
	while (buf->cy < buf->numrows) {
		erow *row = &buf->row[buf->cy];
		uint8_t *match = strstr(&(row->chars[buf->cx]), needle);
		if (match) {
			if (!(buf->cx == ox && buf->cy == oy)) {
				buf->cx = match - row->chars;
				buf->marky = buf->cy;
				buf->markx = buf->cx + strlen(needle);
				/* buf->rowoff = buf->numrows; */
				return 1;
			}
			buf->cx++;
		}
		buf->cx = 0;
		buf->cy++;
	}
	return 0;
}

void editorQueryReplace(struct editorConfig *ed, struct editorBuffer *buf) {
	orig = NULL;
	repl = NULL;
	orig = editorPrompt(buf, "Query replace: %s", PROMPT_BASIC, NULL);
	if (orig == NULL) {
		editorSetStatusMessage("Canceled query-replace.");
		return;
	}

	uint8_t *prompt = malloc(strlen(orig) + 25);
	sprintf(prompt, "Query replace %s with: %%s", orig);
	repl = editorPrompt(buf, prompt, PROMPT_BASIC, NULL);
	free(prompt);
	if (repl == NULL) {
		free(orig);
		editorSetStatusMessage("Canceled query-replace.");
		return;
	}

	prompt = malloc(strlen(orig) + strlen(repl) + 32);
	sprintf(prompt, "Query replacing %s with %s:", orig, repl);
	int bufwidth = stringWidth(prompt);
	int savedMx = buf->markx;
	int savedMy = buf->marky;
	struct editorUndo *first = buf->undo;
	uint8_t *newStr = NULL;
	buf->query = orig;
	int currentIdx = windowFocusedIdx(ed);
	struct editorWindow *currentWindow = ed->windows[currentIdx];

#define NEXT_OCCUR(ocheck)                 \
	if (!nextOccur(buf, orig, ocheck)) \
	goto QR_CLEANUP

	NEXT_OCCUR(false);

	for (;;) {
		editorSetStatusMessage(prompt);
		editorRefreshScreen();
		editorCursorBottomLine(bufwidth + 2);

		int c = editorReadKey();
		editorRecordKey(c);
		switch (c) {
		case ' ':
		case 'y':
			editorTransformRegion(ed, buf,
					      transformerReplaceString);
			NEXT_OCCUR(true);
			break;
		case CTRL('h'):
		case BACKSPACE:
		case DEL_KEY:
		case 'n':
			buf->cx++;
			NEXT_OCCUR(true);
			break;
		case '\r':
		case 'q':
		case 'N':
		case CTRL('g'):
			goto QR_CLEANUP;
			break;
		case '.':
			editorTransformRegion(ed, buf,
					      transformerReplaceString);
			goto QR_CLEANUP;
			break;
		case '!':
		case 'Y':
			buf->marky = buf->numrows - 1;
			buf->markx = buf->row[buf->marky].size;
			editorTransformRegion(ed, buf,
					      transformerReplaceString);
			goto QR_CLEANUP;
			break;
		case 'u':
			editorDoUndo(buf);
			buf->markx = buf->cx;
			buf->marky = buf->cy;
			buf->cx -= strlen(orig);
			break;
		case 'U':
			while (buf->undo != first)
				editorDoUndo(buf);
			buf->markx = buf->cx;
			buf->marky = buf->cy;
			buf->cx -= strlen(orig);
			break;
		case CTRL('r'):
			prompt = malloc(strlen(orig) + 25);
			sprintf(prompt, "Replace this %s with: %%s", orig);
			newStr = editorPrompt(buf, prompt, PROMPT_BASIC, NULL);
			free(prompt);
			if (newStr == NULL) {
				goto RESET_PROMPT;
			}
			uint8_t *tmp = repl;
			repl = newStr;
			editorTransformRegion(ed, buf,
					      transformerReplaceString);
			free(newStr);
			repl = tmp;
			NEXT_OCCUR(true);
			goto RESET_PROMPT;
			break;
		case 'e':
		case 'E':
			prompt = malloc(strlen(orig) + 25);
			sprintf(prompt, "Query replace %s with: %%s", orig);
			newStr = editorPrompt(buf, prompt, PROMPT_BASIC, NULL);
			free(prompt);
			if (newStr == NULL) {
				goto RESET_PROMPT;
			}
			free(repl);
			repl = newStr;
			editorTransformRegion(ed, buf,
					      transformerReplaceString);
			NEXT_OCCUR(true);
RESET_PROMPT:
			prompt = malloc(strlen(orig) + strlen(repl) + 32);
			sprintf(prompt, "Query replacing %s with %s:", orig,
				repl);
			bufwidth = stringWidth(prompt);
			break;
		case CTRL('l'):
			editorRecenter(currentWindow);
			break;
		}
	}

QR_CLEANUP:
	editorSetStatusMessage("");
	buf->query = NULL;
	buf->markx = savedMx;
	buf->marky = savedMy;
	free(orig);
	free(repl);
	free(prompt);
}
