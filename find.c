#include "find.h"
#include "emsys.h"
#include "unicode.h"
#include <string.h>
#include <stdlib.h>
#include <regex.h>

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

void findCallback(struct editorBuffer *bufr, uint8_t *query, int key) {
	extern struct editorConfig E;
	struct editorBuffer *target_bufr = E.edbuf;
	static int last_match = -1;
	static int direction = 1;
	static struct editorBuffer *last_buffer = NULL;

	/* Reset search state when switching buffers */
	if (last_buffer != target_bufr) {
		last_match = -1;
		direction = 1;
		last_buffer = target_bufr;
	}

	target_bufr->query = query;
	target_bufr->match = 0;

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
	if (current >= 0 && current < target_bufr->numrows) {
		erow *row = &target_bufr->row[current];
		uint8_t *match;
		if (target_bufr->cx + 1 >= row->size) {
			match = NULL;
		} else {
			if (regex_mode) {
				match = regexSearch(
					&(row->chars[target_bufr->cx + 1]),
					query);
			} else {
				match = strstr(
					&(row->chars[target_bufr->cx + 1]),
					query);
			}
		}
		if (match) {
			last_match = current;
			target_bufr->cy = current;
			target_bufr->cx = match - row->chars;
			/* Ensure we're at a character boundary */
			while (target_bufr->cx > 0 &&
			       utf8_isCont(row->chars[target_bufr->cx])) {
				target_bufr->cx--;
			}
			scroll();
			target_bufr->match = 1;
			return;
		}
	}
	for (int i = 0; i < target_bufr->numrows; i++) {
		current += direction;
		if (current == -1)
			current = target_bufr->numrows - 1;
		else if (current == target_bufr->numrows)
			current = 0;

		erow *row = &target_bufr->row[current];
		uint8_t *match;
		if (regex_mode) {
			match = regexSearch(row->chars, query);
		} else {
			match = strstr(row->chars, query);
		}
		if (match) {
			last_match = current;
			target_bufr->cy = current;
			target_bufr->cx = match - row->chars;
			/* Ensure we're at a character boundary */
			while (target_bufr->cx > 0 &&
			       utf8_isCont(row->chars[target_bufr->cx])) {
				target_bufr->cx--;
			}
			scroll();
			target_bufr->match = 1;
			break;
		}
	}
}

void find(struct editorBuffer *buf) {
	regex_mode = 0; /* Start in normal mode */
	int saved_cx = buf->cx;
	int saved_cy = buf->cy;
	//	int saved_rowoff = buf->rowoff;

	uint8_t *query = promptUser(buf, "Search (C-g to cancel): %s",
				    PROMPT_BASIC, findCallback);

	buf->query = NULL;
	if (query) {
		free(query);
	} else {
		buf->cx = saved_cx;
		buf->cy = saved_cy;
		//		buf->rowoff = saved_rowoff;
	}
}

void regexFind(struct editorBuffer *buf) {
	regex_mode = 1; /* Start in regex mode */
	int saved_cx = buf->cx;
	int saved_cy = buf->cy;

	uint8_t *query = promptUser(buf, "Regex search (C-g to cancel): %s",
				    PROMPT_BASIC, findCallback);

	buf->query = NULL;
	regex_mode = 0; /* Reset after search */
	if (query) {
		free(query);
	} else {
		buf->cx = saved_cx;
		buf->cy = saved_cy;
	}
}
