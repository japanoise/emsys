#include "find.h"
#include "emsys.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <regex.h>
#include <stdint.h>
#include "display.h"
#include "keymap.h"
#include "terminal.h"
#include "transform.h"
#include "undo.h"
#include "unicode.h"
#include "region.h"
#include "prompt.h"
#include "unused.h"
#include "util.h"

extern struct editorConfig E;
static int regex_mode = 0;

/* Helper function to search for regex match in a string */
static uint8_t *regexSearch(uint8_t *text, uint8_t *pattern) {
	if (!pattern || !text || strlen((char *)pattern) == 0) {
		return NULL;
	}

	regex_t regex;
	regmatch_t match[1];

	/* Try to compile regex, fall back to literal search if invalid */
	if (regcomp(&regex, (char *)pattern, REG_EXTENDED) != 0) {
		return strstr((char *)text, (char *)pattern);
	}

	/* Execute regex search */
	if (regexec(&regex, (char *)text, 1, match, 0) == 0) {
		regfree(&regex);
		return text + match[0].rm_so;
	}

	regfree(&regex);
	return NULL;
}

// https://stackoverflow.com/a/779960
// You must free the result if result is non-NULL.
uint8_t *orig;
uint8_t *repl;

char *str_replace(char *orig, char *rep, char *with) {
	char *result;	  // the return string
	char *ins;	  // the next insert point
	char *tmp;	  // varies
	size_t len_rep;	  // length of rep (the string to remove)
	size_t len_with;  // length of with (the string to replace rep with)
	size_t len_front; // distance between rep and end of last rep
	size_t count;	  // number of replacements

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

	// Check for potential overflow
	size_t orig_len = strlen(orig);
	size_t result_size;
	if (len_with > len_rep) {
		// Check if multiplication would overflow
		size_t diff = len_with - len_rep;
		if (count > 0 && diff > (SIZE_MAX - orig_len - 1) / count) {
			return NULL; // Overflow would occur
		}
		result_size = orig_len + diff * count + 1;
	} else if (len_with < len_rep) {
		// Shrinking - need to handle underflow
		size_t diff = len_rep - len_with;
		if (diff * count > orig_len) {
			// Would result in negative size
			return NULL;
		}
		result_size = orig_len - diff * count + 1;
	} else {
		// len_with == len_rep, no size change
		result_size = orig_len + 1;
	}
	tmp = result = xmalloc(result_size);

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
		size_t remaining = result_size - (tmp - result);
		memcpy(tmp, orig, len_front);
		tmp += len_front;
		remaining = result_size - (tmp - result);
		int written = snprintf(tmp, remaining, "%s", with);
		if (written >= 0 && (size_t)written < remaining) {
			tmp += written;
		}
		orig += len_front + len_rep; // move to next "end of rep"
	}
	size_t final_remaining = result_size - (tmp - result);
	snprintf(tmp, final_remaining, "%s", orig);
	return result;
}

void editorFindCallback(struct editorBuffer *bufr, uint8_t *query, int key) {
	static int last_match = -1;
	static int direction = 1;
	
	if (bufr->query != query) {
		free(bufr->query);
		bufr->query = query ? xstrdup((char *)query) : NULL;
	}
	bufr->match = 0;

	if (key == CTRL('g') || key == CTRL('c') || key == '\r') {
		last_match = -1;
		direction = 1;
		regex_mode = 0; /* Reset regex mode on exit */
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
	if (current >= 0 && current < bufr->numrows) {
		erow *row = &bufr->row[current];
		uint8_t *match;
		if (bufr->cx + 1 >= row->size) {
			match = NULL;
		} else {
			if (regex_mode) {
				match = regexSearch(&(row->chars[bufr->cx + 1]),
						    query);
			} else {
				match = strstr((char *)&(row->chars[bufr->cx + 1]),
					       (char *)query);
			}
		}
		if (match) {
			last_match = current;
			bufr->cy = current;
			bufr->cx = match - row->chars;
			/* Ensure we're at a character boundary */
			while (bufr->cx > 0 &&
			       utf8_isCont(row->chars[bufr->cx])) {
				bufr->cx--;
			}
			scroll();
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
		uint8_t *match;
		if (regex_mode) {
			match = regexSearch(row->chars, query);
		} else {
			match = strstr((char *)row->chars, (char *)query);
		}
		if (match) {
			last_match = current;
			bufr->cy = current;
			bufr->cx = match - row->chars;
			/* Ensure we're at a character boundary */
			while (bufr->cx > 0 &&
			       utf8_isCont(row->chars[bufr->cx])) {
				bufr->cx--;
			}
			scroll();
			bufr->match = 1;
			break;
		}
	}
}

void editorFind(struct editorBuffer *bufr) {
	regex_mode = 0; /* Start in normal mode */
	int saved_cx = bufr->cx;
	int saved_cy = bufr->cy;
	//	int saved_rowoff = bufr->rowoff;

	uint8_t *query = editorPrompt(bufr, "Search (C-g to cancel): %s",
				      PROMPT_BASIC, editorFindCallback);

	free(bufr->query);
	bufr->query = NULL;
	if (query) {
		free(query);
	} else {
		bufr->cx = saved_cx;
		bufr->cy = saved_cy;
		//		bufr->rowoff = saved_rowoff;
	}
}

void editorRegexFind(struct editorBuffer *bufr) {
	regex_mode = 1; /* Start in regex mode */
	int saved_cx = bufr->cx;
	int saved_cy = bufr->cy;

	uint8_t *query = editorPrompt(bufr, "Regex search (C-g to cancel): %s",
				      PROMPT_BASIC, editorFindCallback);

	free(bufr->query);
	bufr->query = NULL;
	regex_mode = 0; /* Reset after search */
	if (query) {
		free(query);
	} else {
		bufr->cx = saved_cx;
		bufr->cy = saved_cy;
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

	uint8_t *prompt = xmalloc(strlen(orig) + 20);
	snprintf(prompt, strlen(orig) + 20, "Replace %s with: %%s", orig);
	repl = editorPrompt(buf, prompt, PROMPT_BASIC, NULL);
	free(prompt);
	prompt = NULL;
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
		uint8_t *match = strstr((char *)&(row->chars[buf->cx]), (char *)needle);
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

	uint8_t *prompt = xmalloc(strlen(orig) + 25);
	snprintf(prompt, strlen(orig) + 25, "Query replace %s with: %%s", orig);
	repl = editorPrompt(buf, prompt, PROMPT_BASIC, NULL);
	free(prompt);
	if (repl == NULL) {
		free(orig);
		editorSetStatusMessage("Canceled query-replace.");
		return;
	}

	prompt = xmalloc(strlen(orig) + strlen(repl) + 32);
	snprintf(prompt, strlen(orig) + strlen(repl) + 32,
		 "Query replacing %s with %s:", orig, repl);
	int bufwidth = stringWidth(prompt);
	int savedMx = buf->markx;
	int savedMy = buf->marky;
	struct editorUndo *first = buf->undo;
	uint8_t *newStr = NULL;
	buf->query = orig;
	int currentIdx = windowFocusedIdx();
	struct editorWindow *currentWindow = ed->windows[currentIdx];

#define NEXT_OCCUR(ocheck)                 \
	if (!nextOccur(buf, orig, ocheck)) \
	goto QR_CLEANUP

	NEXT_OCCUR(false);

	for (;;) {
		editorSetStatusMessage(prompt);
		refreshScreen();
		cursorBottomLine(bufwidth + 2);

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
			editorDoUndo(buf, 1);
			buf->markx = buf->cx;
			buf->marky = buf->cy;
			buf->cx -= strlen(orig);
			break;
		case 'U':
			while (buf->undo != first)
				editorDoUndo(buf, 1);
			buf->markx = buf->cx;
			buf->marky = buf->cy;
			buf->cx -= strlen(orig);
			break;
		case CTRL('r'):
			prompt = xmalloc(strlen(orig) + 25);
			snprintf(prompt, strlen(orig) + 25,
				 "Replace this %s with: %%s", orig);
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
			prompt = xmalloc(strlen(orig) + 25);
			snprintf(prompt, strlen(orig) + 25,
				 "Query replace %s with: %%s", orig);
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
			prompt = xmalloc(strlen(orig) + strlen(repl) + 32);
			snprintf(prompt, strlen(orig) + strlen(repl) + 32,
				 "Query replacing %s with %s:", orig, repl);
			bufwidth = stringWidth(prompt);
			break;
		case CTRL('l'):
			recenter(currentWindow);
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
	if (prompt != NULL) {
		free(prompt);
	}
}

/* Wrapper for command table */
void editorRegexFindWrapper(struct editorConfig *UNUSED(ed),
			    struct editorBuffer *buf) {
	editorRegexFind(buf);
}

/* Backward regex find - not yet implemented */
void editorBackwardRegexFind(struct editorBuffer *bufr) {
	(void)bufr;	/* Unused parameter */
	regex_mode = 1; /* Start in regex mode */

	editorSetStatusMessage("Backward regex search not yet implemented");
	regex_mode = 0; /* Reset after search */
}

/* Wrapper for backward regex find */
void editorBackwardRegexFindWrapper(struct editorConfig *UNUSED(ed),
				    struct editorBuffer *buf) {
	editorBackwardRegexFind(buf);
}
