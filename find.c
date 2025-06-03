#include "find.h"
#include "emsys.h"
#include <string.h>
#include <stdlib.h>
#include <regex.h>

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
	static int last_match = -1;
	static int direction = 1;
	bufr->query = query;
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
	if (current >= 0) {
		erow *row = &bufr->row[current];
		uint8_t *match;
		if (regex_mode) {
			match = regexSearch(&(row->chars[bufr->cx + 1]), query);
		} else {
			match = strstr(&(row->chars[bufr->cx + 1]), query);
		}
		if (match) {
			last_match = current;
			bufr->cy = current;
			bufr->cx = match - row->chars;
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
			match = strstr(row->chars, query);
		}
		if (match) {
			last_match = current;
			bufr->cy = current;
			bufr->cx = match - row->chars;
			scroll();
			bufr->match = 1;
			break;
		}
	}
}

void find(struct editorBuffer *bufr) {
	regex_mode = 0; /* Start in normal mode */
	int saved_cx = bufr->cx;
	int saved_cy = bufr->cy;
	//	int saved_rowoff = bufr->rowoff;

	uint8_t *query = promptUser(bufr, "Search (C-g to cancel): %s",
				    PROMPT_BASIC, findCallback);

	bufr->query = NULL;
	if (query) {
		free(query);
	} else {
		bufr->cx = saved_cx;
		bufr->cy = saved_cy;
		//		bufr->rowoff = saved_rowoff;
	}
}

void regexFind(struct editorBuffer *bufr) {
	regex_mode = 1; /* Start in regex mode */
	int saved_cx = bufr->cx;
	int saved_cy = bufr->cy;

	uint8_t *query = promptUser(bufr, "Regex search (C-g to cancel): %s",
				    PROMPT_BASIC, findCallback);

	bufr->query = NULL;
	regex_mode = 0; /* Reset after search */
	if (query) {
		free(query);
	} else {
		bufr->cx = saved_cx;
		bufr->cy = saved_cy;
	}
}
