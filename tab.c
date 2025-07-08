#include "util.h"
#include <glob.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "emsys.h"
#include <regex.h>
#include "buffer.h"
#include "tab.h"
#include "util.h"
#include "undo.h"
#include "unicode.h"
#include "display.h"
#include "terminal.h"

uint8_t *tabCompleteBufferNames(struct editorConfig *ed, uint8_t *input,
				struct editorBuffer *currentBuffer) {
	char **completions = NULL;
	int count = 0;
	int capacity = 8; // Initial capacity
	uint8_t *ret = input;

	// Allocate initial memory
	completions = xmalloc(capacity * sizeof(char *));
	if (completions == NULL) {
		// Handle allocation failure
		return ret;
	}

	// Collect matching buffer names
	for (struct editorBuffer *b = ed->headbuf; b != NULL; b = b->next) {
		if (b == currentBuffer)
			continue;

		char *name = b->filename ? b->filename : "*scratch*";
		if (strncmp(name, (char *)input, strlen((char *)input)) == 0) {
			if (count + 1 >= capacity) {
				// Double capacity and reallocate
				if (capacity > INT_MAX / 2 ||
				    (size_t)capacity >
					    SIZE_MAX / (2 * sizeof(char *))) {
					die("buffer size overflow");
				}
				capacity *= 2;
				completions = xrealloc(
					completions, capacity * sizeof(char *));
			}
			completions[count++] = xstrdup(name);
		}
	}

	if (count < 1) {
		goto cleanup;
	}

	if (count == 1) {
		ret = (uint8_t *)xstrdup(completions[0]);
		goto cleanup;
	}

	// Multiple matches, allow cycling through them
	int cur = 0;
	for (;;) {
		editorSetStatusMessage("Multiple options: %s",
				       completions[cur]);
		refreshScreen();
		cursorBottomLine(strlen(completions[cur]) + 19);

		int c = editorReadKey();
		switch (c) {
		case '\r':
			ret = (uint8_t *)xstrdup(completions[cur]);
			goto cleanup;
		case CTRL('i'):
			cur = (cur + 1) % count;
			break;
		case BACKTAB:
			cur = (cur == 0) ? count - 1 : cur - 1;
			break;
		case CTRL('g'):
			goto cleanup;
		}
	}

cleanup:
	for (int i = 0; i < count; i++) {
		free(completions[i]);
	}
	free(completions);

	return ret;
}

uint8_t *tabCompleteFiles(uint8_t *prompt) {
	glob_t globlist;
	uint8_t *ret = prompt;
	uint8_t *allocated_prompt = NULL;
	uint8_t *tilde_expanded = NULL;

	/* Manual tilde expansion - works on all systems */
	if (*prompt == '~') {
		char *home_dir = getenv("HOME");
		if (!home_dir) {
			// Handle error: HOME environment variable not found
			return prompt;
		}

		size_t home_len = strlen(home_dir);
		size_t prompt_len = strlen((char *)prompt);
		char *new_prompt = xmalloc(
			home_len + prompt_len - 1 +
			1); // -1 for removed '~', +1 for null terminator
		if (!new_prompt) {
			// Handle memory allocation failure
			return prompt;
		}

		emsys_strlcpy(new_prompt, home_dir, home_len + prompt_len);
		emsys_strlcpy(new_prompt + home_len, (char *)(prompt + 1), prompt_len); // Skip the '~'
		tilde_expanded = (uint8_t *)new_prompt;
		prompt = tilde_expanded;
	}

	/*
	 * Define this to do manual globbing. It does mean you'll have
	 * to add the *s yourself. However, it will let you tab
	 * complete for more interesting scenarios, like
	 * *dir/other*dir/file.*.gz -> mydir/otherFOOdir/file.tar.gz
	 */
#ifndef EMSYS_NO_SIMPLE_GLOB
	int end = strlen((char *)prompt);
	/* Need to allocate a new string with room for the '*' */
	char *glob_pattern = xmalloc(end + 2);
	if (!glob_pattern) {
		if (tilde_expanded)
			free(tilde_expanded);
		return ret; /* Return original prompt, not modified one */
	}
	emsys_strlcpy(glob_pattern, (char *)prompt, end + 2);
	glob_pattern[end] = '*';
	glob_pattern[end + 1] = 0;
	allocated_prompt = (uint8_t *)glob_pattern;
	prompt = allocated_prompt;
#endif

	if (glob((char *)prompt, GLOB_MARK, NULL, &globlist))
		goto TC_FILES_CLEANUP;

	size_t cur = 0;

	if (globlist.gl_pathc < 1)
		goto TC_FILES_CLEANUP;

	if (globlist.gl_pathc == 1)
		goto TC_FILES_ACCEPT;

	int curw = stringWidth((uint8_t *)globlist.gl_pathv[cur]);

	for (;;) {
		editorSetStatusMessage("Multiple options: %s",
				       globlist.gl_pathv[cur]);
		refreshScreen();
		cursorBottomLine(curw + 19);

		int c = editorReadKey();
		switch (c) {
		case '\r':;
TC_FILES_ACCEPT:;
			ret = xcalloc(strlen(globlist.gl_pathv[cur]) + 1, 1);
			emsys_strlcpy((char *)ret, globlist.gl_pathv[cur], strlen(globlist.gl_pathv[cur]) + 1);
			goto TC_FILES_CLEANUP;
			break;
		case CTRL('i'):
			cur++;
			if (cur >= globlist.gl_pathc) {
				cur = 0;
			}
			curw = stringWidth((uint8_t *)globlist.gl_pathv[cur]);
			break;
		case BACKTAB:
			if (cur == 0) {
				cur = globlist.gl_pathc - 1;
			} else {
				cur--;
			}
			curw = stringWidth((uint8_t *)globlist.gl_pathv[cur]);
			break;
		case CTRL('g'):
			goto TC_FILES_CLEANUP;
			break;
		}
	}

TC_FILES_CLEANUP:
	globfree(&globlist);
#ifndef EMSYS_NO_SIMPLE_GLOB
	if (allocated_prompt) {
		free(allocated_prompt);
	}
#endif
	if (tilde_expanded) {
		free(tilde_expanded);
	}
	return ret;
}

uint8_t *tabCompleteCommands(struct editorConfig *ed, uint8_t *input) {
	char **completions = NULL;
	int count = 0;
	int capacity = 8; // Initial capacity
	uint8_t *ret = input;

	// Allocate initial memory
	completions = xmalloc(capacity * sizeof(char *));
	if (completions == NULL) {
		return ret;
	}

	// Convert input to lowercase for case-insensitive matching
	int input_len = strlen((char *)input);
	char *lower_input = xmalloc(input_len + 1);
	if (lower_input == NULL) {
		free(completions);
		return ret;
	}
	for (int i = 0; i <= input_len; i++) {
		uint8_t c = input[i];
		if ('A' <= c && c <= 'Z') {
			c |= 0x60;
		}
		lower_input[i] = c;
	}

	// Collect matching command names
	for (int i = 0; i < ed->cmd_count; i++) {
		if (strncmp(ed->cmd[i].key, lower_input, input_len) == 0) {
			if (count >= capacity) {
				if (capacity > INT_MAX / 2 ||
				    (size_t)capacity >
					    SIZE_MAX / (2 * sizeof(char *))) {
					die("buffer size overflow");
				}
				capacity *= 2;
				completions = xrealloc(
					completions, capacity * sizeof(char *));
			}
			completions[count++] = xstrdup(ed->cmd[i].key);
		}
	}

	free(lower_input);

	if (count < 1) {
		goto cleanup;
	}

	if (count == 1) {
		ret = (uint8_t *)xstrdup(completions[0]);
		goto cleanup;
	}

	// Multiple matches, allow cycling through them
	int cur = 0;
	for (;;) {
		editorSetStatusMessage("cmd: %s", completions[cur]);
		refreshScreen();

		// Position cursor after the command text
		int prompt_width = 5; // "cmd: "
		cursorBottomLine(prompt_width + strlen(completions[cur]));

		int c = editorReadKey();
		switch (c) {
		case '\r':
			ret = (uint8_t *)xstrdup(completions[cur]);
			goto cleanup;
		case CTRL('i'):
			cur = (cur + 1) % count;
			break;
		case BACKTAB:
			cur = (cur == 0) ? count - 1 : cur - 1;
			break;
		case CTRL('g'):
			goto cleanup;
		}
	}

cleanup:
	for (int i = 0; i < count; i++) {
		free(completions[i]);
	}
	free(completions);

	return ret;
}

static int alnum(uint8_t c) {
	return c > 127 || ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') ||
	       ('0' <= c && c <= '9') || c == '_';
}

static int sortstring(const void *str1, const void *str2) {
	char *const *pp1 = str1;
	char *const *pp2 = str2;
	return strcmp(*pp1, *pp2);
}

void editorCompleteWord(struct editorConfig *ed, struct editorBuffer *bufr) {
	if (bufr->cy >= bufr->numrows || bufr->cx == 0) {
		editorSetStatusMessage("Nothing to complete here.");
		return;
	}
	struct erow *row = &bufr->row[bufr->cy];
	if (!row->chars) {
		editorSetStatusMessage("Nothing to complete here.");
		return;
	}
	int wordStart = bufr->cx;
	for (int i = bufr->cx - 1; i >= 0; i--) {
		if (!alnum(row->chars[i]))
			break;
		wordStart = i;
	}
	if (wordStart == bufr->cx) {
		editorSetStatusMessage("Nothing to complete here.");
		return;
	}

	char rpattern[] = "[A-Za-z0-9\200-\377_]+";
	char *word = xcalloc(bufr->cx - wordStart + 1 + sizeof(rpattern), 1);
	strncpy(word, (char *)&row->chars[wordStart], bufr->cx - wordStart);
	emsys_strlcat(word, rpattern, bufr->cx - wordStart + 1 + sizeof(rpattern));
	int ncand = 0;
	int scand = 32;
	char **candidates = xmalloc(sizeof(uint8_t *) * scand);

	regex_t pattern;
	regmatch_t matches[1];
	if (regcomp(&pattern, word, REG_EXTENDED) != 0) {
		free(word);
		free(candidates);
		editorSetStatusMessage("Invalid regex pattern");
		return;
	}

	/* This is a deeply naive algorithm. */
	/* First, find every word that starts with the word to complete */
	for (struct editorBuffer *buf = ed->headbuf; buf; buf = buf->next) {
		for (int i = 0; i < buf->numrows; i++) {
			if (buf == bufr && buf->cy == i)
				continue;
			struct erow *row = &buf->row[i];
			if (!row->chars)
				continue;
			if (regexec(&pattern, (char *)row->chars, 1, matches,
				    0) == 0) {
				int match_idx = matches[0].rm_so;
				int match_length =
					matches[0].rm_eo - matches[0].rm_so;
				candidates[ncand] = xcalloc(match_length + 1, 1);
				strncpy(candidates[ncand],
					(char *)&row->chars[match_idx],
					match_length);
				ncand++;
				if (ncand >= scand) {
					scand <<= 1;
					candidates = xrealloc(candidates,
							      sizeof(char *) *
								      scand);
				}
			}
		}
	}
	/* Dunmatchin'. Restore word to non-regex contents. */
	word[bufr->cx - wordStart] = 0;

	/* No matches? Cleanup. */
	if (ncand == 0) {
		editorSetStatusMessage("No match for %s", word);
		goto COMPLETE_WORD_CLEANUP;
	}

	int sel = 0;
	/* Only one candidate? Take it. */
	if (ncand == 1) {
		goto COMPLETE_WORD_DONE;
	}

	/* Next, sort the list */
	qsort(candidates, ncand, sizeof(char *), sortstring);

	/* Finally, uniq' it. We now have our candidate list. */
	int newlen = 1;
	char *prev = NULL;
	for (int i = 0; i < ncand; i++) {
		if (prev == NULL) {
			prev = candidates[i];
			continue;
		}
		if (strcmp(prev, candidates[i])) {
			/* Nonduplicate, copy it over. */
			prev = candidates[i];
			candidates[newlen++] = prev;
		} else {
			/* We don't need the memory for duplicates any more. */
			free(candidates[i]);
		}
	}
	ncand = newlen;

	/* If after all that mess we only have one candidate, use it. */
	if (ncand == 1) {
		goto COMPLETE_WORD_DONE;
	}

	/* Otherwise, standard tab complete interface. */
	int selw = stringWidth((uint8_t *)candidates[sel]);
	for (;;) {
		editorSetStatusMessage("Multiple options: %s", candidates[sel]);
		refreshScreen();
		cursorBottomLine(selw + 19);

		int c = editorReadKey();
		switch (c) {
		case '\r':
			goto COMPLETE_WORD_DONE;
			break;
		case EXPAND:
		case CTRL('i'):
			sel++;
			if (sel >= ncand) {
				sel = 0;
			}
			selw = stringWidth((uint8_t *)candidates[sel]);
			break;
		case BACKTAB:
			if (sel == 0) {
				sel = ncand - 1;
			} else {
				sel--;
			}
			selw = stringWidth((uint8_t *)candidates[sel]);
			break;
		case CTRL('g'):
			editorSetStatusMessage("Canceled");
			goto COMPLETE_WORD_CLEANUP;
			break;
		}
	}

	/* Finally, make the modification to the row, setup undos, and
	 * cleanup. */
COMPLETE_WORD_DONE:;
	/* Length of the rest of the completed word */
	int completelen = strlen(candidates[sel]) - (bufr->cx - wordStart);
	struct editorUndo *new = newUndo();
	new->prev = bufr->undo;
	new->startx = bufr->cx;
	new->starty = bufr->cy;
	new->endx = bufr->cx + completelen;
	new->endy = bufr->cy;
	new->datalen = completelen;
	if (new->datasize < completelen + 1) {
		new->data = xrealloc(new->data, new->datalen + 1);
		new->datasize = new->datalen + 1;
	}
	new->append = 0;
	new->delete = 0;
	new->data[0] = 0;
	emsys_strlcat((char *)new->data, &candidates[sel][bufr->cx - wordStart], new->datasize);
	bufr->undo = new;

	row->chars = xrealloc(row->chars, row->size + 1 + completelen);
	memcpy(&row->chars[bufr->cx + completelen], &row->chars[bufr->cx],
	       row->size - bufr->cx);
	memcpy(&row->chars[bufr->cx], &candidates[sel][bufr->cx - wordStart],
	       completelen);
	row->size += completelen;
	row->chars[row->size] = 0;
	row->render_valid = 0;

	editorSetStatusMessage("Expanded %.30s to %.30s", word,
			       candidates[sel]);
	bufr->cx += completelen;

COMPLETE_WORD_CLEANUP:
	regfree(&pattern);
	for (int i = 0; i < ncand; i++) {
		free(candidates[i]);
	}
	free(candidates);
	free(word);
}
