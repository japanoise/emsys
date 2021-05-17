#include "find.h"

void editorFindCallback(struct editorBuffer *bufr, uint8_t *query, int key) {
	static int last_match = -1;
	static int direction = 1;

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

	if (last_match == -1) direction = 1;
	int current = last_match;
	for (int i = 0; i < bufr->numrows; i++) {
		current += direction;
		if(current == -1) current = bufr->numrows - 1;
		else if (current == bufr->numrows) current = 0;
		
		erow *row = &bufr->row[current];
		uint8_t *match = strstr(row->chars, query);
		if (match) {
			last_match = current;
			bufr->cy = current;
			bufr->cx = match - row->chars;
			bufr->rowoff = bufr->numrows;
			break;
		}
	}
}

void editorFind(struct editorBuffer *bufr) {
	int saved_cx = bufr->cx;
	int saved_cy = bufr->cy;
	int saved_rowoff = bufr->rowoff;
		
	uint8_t *query = editorPrompt(bufr, "Search (C-g to cancel): %s", editorFindCallback);
	
	if (query) {
		free(query);
	} else {
		bufr->cx = saved_cx;
		bufr->cy = saved_cy;
		bufr->rowoff = saved_rowoff;
	}
}

