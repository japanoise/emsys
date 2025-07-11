#include <glob.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>
#include "emsys.h"
#include "completion.h"
#include "buffer.h"
#include "util.h"
#include "display.h"
#include "terminal.h"
#include "fileio.h"
#include "prompt.h"
#include "edit.h"
#include "unicode.h"
#include "undo.h"
#include <regex.h>

extern struct editorConfig E;

void resetCompletionState(struct completion_state *state) {
	free(state->last_completed_text);
	state->last_completed_text = NULL;
	state->completion_start_pos = 0;
	state->successive_tabs = 0;
	state->last_completion_count = 0;
	state->preserve_message = 0;
}

void freeCompletionResult(struct completion_result *result) {
	if (result->matches) {
		for (int i = 0; i < result->n_matches; i++) {
			free(result->matches[i]);
		}
		free(result->matches);
	}
	free(result->common_prefix);
	result->matches = NULL;
	result->common_prefix = NULL;
	result->n_matches = 0;
	result->prefix_len = 0;
}

char *findCommonPrefix(char **strings, int count) {
	if (count == 0) return NULL;
	if (count == 1) return xstrdup(strings[0]);
	
	int prefix_len = 0;
	while (1) {
		char ch = strings[0][prefix_len];
		if (ch == '\0') break;
		
		int all_match = 1;
		for (int i = 1; i < count; i++) {
			if (strings[i][prefix_len] != ch) {
				all_match = 0;
				break;
			}
		}
		
		if (!all_match) break;
		prefix_len++;
	}
	
	char *prefix = xmalloc(prefix_len + 1);
	strncpy(prefix, strings[0], prefix_len);
	prefix[prefix_len] = '\0';
	return prefix;
}

void getFileCompletions(const char *prefix, struct completion_result *result) {
	glob_t globlist;
	result->matches = NULL;
	result->n_matches = 0;
	result->common_prefix = NULL;
	result->prefix_len = strlen(prefix);
	
	char *glob_pattern = NULL;
	const char *pattern_to_use = prefix;
	
	/* Manual tilde expansion */
	if (*prefix == '~') {
		char *home_dir = getenv("HOME");
		if (!home_dir) {
			return;
		}
		
		size_t home_len = strlen(home_dir);
		size_t prefix_len = strlen(prefix);
		char *expanded = xmalloc(home_len + prefix_len);
		strcpy(expanded, home_dir);
		strcat(expanded, prefix + 1);
		pattern_to_use = expanded;
	}
	
#ifndef EMSYS_NO_SIMPLE_GLOB
	/* Add * for globbing */
	int len = strlen(pattern_to_use);
	glob_pattern = xmalloc(len + 2);
	strcpy(glob_pattern, pattern_to_use);
	glob_pattern[len] = '*';
	glob_pattern[len + 1] = '\0';
	
	if (pattern_to_use != prefix) {
		free((void *)pattern_to_use);
	}
	pattern_to_use = glob_pattern;
#endif
	
	int glob_result = glob(pattern_to_use, GLOB_MARK, NULL, &globlist);
	if (glob_result == 0) {
		if (globlist.gl_pathc > 0) {
			result->matches = xmalloc(globlist.gl_pathc * sizeof(char *));
			result->n_matches = globlist.gl_pathc;
			
			for (size_t i = 0; i < globlist.gl_pathc; i++) {
				result->matches[i] = xstrdup(globlist.gl_pathv[i]);
			}
			
			result->common_prefix = findCommonPrefix(result->matches, result->n_matches);
		}
		globfree(&globlist);
	} else if (glob_result == GLOB_NOMATCH) {
		/* No matches found */
		result->n_matches = 0;
	}
	
	if (glob_pattern) {
		free(glob_pattern);
	}
	if (pattern_to_use != prefix && pattern_to_use != glob_pattern) {
		free((void *)pattern_to_use);
	}
}

void getBufferCompletions(struct editorConfig *ed, const char *prefix,
                         struct editorBuffer *currentBuffer,
                         struct completion_result *result) {
	result->matches = NULL;
	result->n_matches = 0;
	result->common_prefix = NULL;
	result->prefix_len = strlen(prefix);
	
	int capacity = 8;
	result->matches = xmalloc(capacity * sizeof(char *));
	
	for (struct editorBuffer *b = ed->headbuf; b != NULL; b = b->next) {
		if (b == currentBuffer) continue;
		
		/* Skip the *Completions* buffer */
		if (b->filename && strcmp(b->filename, "*Completions*") == 0) continue;
		
		char *name = b->filename ? b->filename : "*scratch*";
		if (strncmp(name, prefix, strlen(prefix)) == 0) {
			if (result->n_matches >= capacity) {
				if (capacity > INT_MAX / 2 ||
				    (size_t)capacity > SIZE_MAX / (2 * sizeof(char *))) {
					die("buffer size overflow");
				}
				capacity *= 2;
				result->matches = xrealloc(result->matches,
				                          capacity * sizeof(char *));
			}
			result->matches[result->n_matches++] = xstrdup(name);
		}
	}
	
	if (result->n_matches > 0) {
		result->common_prefix = findCommonPrefix(result->matches, result->n_matches);
	} else {
		free(result->matches);
		result->matches = NULL;
	}
}

void getCommandCompletions(struct editorConfig *ed, const char *prefix,
                          struct completion_result *result) {
	result->matches = NULL;
	result->n_matches = 0;
	result->common_prefix = NULL;
	result->prefix_len = strlen(prefix);
	
	int capacity = 8;
	result->matches = xmalloc(capacity * sizeof(char *));
	
	/* Convert prefix to lowercase for case-insensitive matching */
	int prefix_len = strlen(prefix);
	char *lower_prefix = xmalloc(prefix_len + 1);
	for (int i = 0; i <= prefix_len; i++) {
		char c = prefix[i];
		if ('A' <= c && c <= 'Z') {
			c |= 0x60;
		}
		lower_prefix[i] = c;
	}
	
	for (int i = 0; i < ed->cmd_count; i++) {
		if (strncmp(ed->cmd[i].key, lower_prefix, prefix_len) == 0) {
			if (result->n_matches >= capacity) {
				if (capacity > INT_MAX / 2 ||
				    (size_t)capacity > SIZE_MAX / (2 * sizeof(char *))) {
					die("buffer size overflow");
				}
				capacity *= 2;
				result->matches = xrealloc(result->matches,
				                          capacity * sizeof(char *));
			}
			result->matches[result->n_matches++] = xstrdup(ed->cmd[i].key);
		}
	}
	
	free(lower_prefix);
	
	if (result->n_matches > 0) {
		result->common_prefix = findCommonPrefix(result->matches, result->n_matches);
	} else {
		free(result->matches);
		result->matches = NULL;
	}
}

static void replaceMinibufferText(struct editorBuffer *minibuf, const char *text) {
	/* Clear current content */
	while (minibuf->numrows > 0) {
		editorDelRow(minibuf, 0);
	}
	
	/* Insert new text */
	editorInsertRow(minibuf, 0, (char *)text, strlen(text));
	minibuf->cx = strlen(text);
	minibuf->cy = 0;
}

static struct editorBuffer *findOrCreateBuffer(const char *name) {
	/* Search for existing buffer */
	for (struct editorBuffer *b = E.headbuf; b != NULL; b = b->next) {
		if (b->filename && strcmp(b->filename, name) == 0) {
			return b;
		}
	}
	
	/* Create new buffer */
	struct editorBuffer *new_buf = newBuffer();
	new_buf->filename = xstrdup(name);
	new_buf->special_buffer = 1;
	new_buf->next = E.headbuf;
	E.headbuf = new_buf;
	return new_buf;
}

static void clearBuffer(struct editorBuffer *buf) {
	while (buf->numrows > 0) {
		editorDelRow(buf, 0);
	}
}


static void showCompletionsBuffer(char **matches, int n_matches) {
	
	/* Find or create completions buffer */
	struct editorBuffer *comp_buf = findOrCreateBuffer("*Completions*");
	clearBuffer(comp_buf);
	comp_buf->read_only = 1;
	
	/* Add header */
	char header[100];
	snprintf(header, sizeof(header), "Possible completions (%d):", n_matches);
	editorInsertRow(comp_buf, 0, header, strlen(header));
	editorInsertRow(comp_buf, 1, "", 0);
	
	/* Find max width */
	int max_width = 0;
	for (int i = 0; i < n_matches; i++) {
		int width = stringWidth((uint8_t *)matches[i]);
		if (width > max_width) {
			max_width = width;
		}
	}
	
	/* Calculate columns */
	int term_width = E.screencols;
	int col_width = max_width + 2;
	int columns = term_width / col_width;
	if (columns < 1) columns = 1;
	
	/* Format matches in columns */
	int rows = (n_matches + columns - 1) / columns;
	for (int row = 0; row < rows; row++) {
		char line[1024] = {0};
		int line_pos = 0;
		
		for (int col = 0; col < columns; col++) {
			int idx = row + col * rows;
			if (idx >= n_matches) break;
			
			int written = snprintf(line + line_pos, sizeof(line) - line_pos,
			                      "%-*s", col_width, matches[idx]);
			if (written > 0) {
				line_pos += written;
			}
		}
		
		/* Trim trailing spaces */
		while (line_pos > 0 && line[line_pos - 1] == ' ') {
			line_pos--;
		}
		line[line_pos] = '\0';
		
		editorInsertRow(comp_buf, comp_buf->numrows, line, line_pos);
	}
	
	/* Display in window if not already visible */
	int comp_window = findBufferWindow(comp_buf);
	if (comp_window == -1) {
		/* Not visible - create new window at bottom */
		if (E.nwindows >= 1) {
			/* Create new window for completions */
			int new_window_idx = E.nwindows;
			editorCreateWindow();
			
			/* Set the new window to show completions buffer */
			E.windows[new_window_idx]->buf = comp_buf;
			E.windows[new_window_idx]->focused = 0;
			
			/* Keep focus on the first window */
			for (int i = 0; i < E.nwindows; i++) {
				E.windows[i]->focused = (i == 0);
			}
			
			comp_window = new_window_idx;
			
		}
	} else {
	}
	
	
	/* Adjust window sizes for completions display */
	if (E.nwindows >= 2 && comp_window >= 0) {
		/* Calculate desired height for completions window */
		int comp_height = comp_buf->numrows + 2; /* +2 for padding */
		
		/* Calculate total available height */
		int total_height = E.screenrows - minibuffer_height - 
		                   (statusbar_height * E.nwindows);
		
		/* Calculate minimum space needed for non-completion windows */
		int non_comp_windows = E.nwindows - 1;
		int min_space_for_others = non_comp_windows * 3; /* 3 lines minimum each */
		
		/* Maximum height for completions is what's left after ensuring minimums */
		int max_comp_height = total_height - min_space_for_others;
		if (comp_height > max_comp_height) {
			comp_height = max_comp_height;
		}
		
		/* Ensure completions window itself gets at least 3 lines */
		if (comp_height < 3) {
			comp_height = 3;
		}
		
		/* Distribute remaining space to other windows */
		int remaining_height = total_height - comp_height;
		int height_per_window = remaining_height / non_comp_windows;
		
		/* Set window heights */
		for (int i = 0; i < E.nwindows; i++) {
			if (i == comp_window) {
				E.windows[i]->height = comp_height;
			} else {
				E.windows[i]->height = height_per_window;
			}
		}
	}
	
	refreshScreen();
}

void closeCompletionsBuffer(void) {
	struct editorBuffer *comp_buf = NULL;
	struct editorBuffer *prev_buf = NULL;
	
	/* Find the completions buffer and its predecessor */
	for (struct editorBuffer *b = E.headbuf; b != NULL; prev_buf = b, b = b->next) {
		if (b->filename && strcmp(b->filename, "*Completions*") == 0) {
			comp_buf = b;
			break;
		}
	}
	
	if (comp_buf) {
		int comp_window = findBufferWindow(comp_buf);
		if (comp_window >= 0 && E.nwindows > 1) {
			editorDestroyWindow(comp_window);
		}
		
		/* Remove the buffer from the buffer list */
		if (prev_buf) {
			prev_buf->next = comp_buf->next;
		} else {
			E.headbuf = comp_buf->next;
		}
		
		/* Update E.buf if it pointed to the completions buffer */
		if (E.buf == comp_buf) {
			E.buf = comp_buf->next ? comp_buf->next : E.headbuf;
		}
		
		/* Update lastVisitedBuffer if it pointed to completions buffer */
		if (E.lastVisitedBuffer == comp_buf) {
			E.lastVisitedBuffer = NULL;
		}
		
		/* Destroy the buffer */
		destroyBuffer(comp_buf);
	}
}

/* Removed - using showCompletionsBuffer instead */

static int alnum(uint8_t c) {
	return ('0' <= c && c <= '9') || ('a' <= c && c <= 'z') ||
	       ('A' <= c && c <= 'Z') || (c == '_');
}

static int sortstring(const void *str1, const void *str2) {
	return strcasecmp(*(char **)str1, *(char **)str2);
}

void editorCompleteWord(struct editorConfig *ed, struct editorBuffer *bufr) {
	/* TODO Finish implementing this sometime */
	if (bufr->cy >= bufr->numrows || bufr->cx == 0) {
		editorSetStatusMessage("Nothing to complete here.");
		return;
	}

	/* Don't attempt word completion within special buffers */
	if (bufr->special_buffer) {
		return;
	}

	/* Check whether there's a word here to complete */
	struct erow *row = &bufr->row[bufr->cy];
	int wordStart = bufr->cx;
	while (wordStart > 0 && alnum(row->chars[wordStart - 1])) {
		wordStart--;
	}
	int wordLen = bufr->cx - wordStart;
	if (wordLen == 0) {
		editorSetStatusMessage("Nothing to complete here.");
		return;
	}

	/* Copy the word and escape regex characters*/
	char word[wordLen + 1];
	word[wordLen] = 0;
	char regexWord[2 * wordLen + 2]; /* worst case, we escape everything and
					     add .* */
	for (int i = 0; i < wordLen; i++) {
		word[i] = row->chars[wordStart + i];
	}
	int regexPos = 0;
	for (int i = 0; i < wordLen; i++) {
		switch (word[i]) {
		case '.':
		case '[':
		case '{':
		case '}':
		case '(':
		case ')':
		case '\\':
		case '*':
		case '+':
		case '?':
		case '|':
		case '^':
		case '$':
			regexWord[regexPos++] = '\\';
			/* fall through */
		default:
			regexWord[regexPos++] = word[i];
		}
	}
	regexWord[regexPos++] = '.';
	regexWord[regexPos++] = '*';
	regexWord[regexPos] = 0;

	/* Compile a regex out of it. This is slow, but Russ Cox said
	 * regexes are fast, so he's probably got a C regex library
	 * just lying around that we could use. */
	regex_t regex;
	int reti = regcomp(&regex, regexWord, REG_EXTENDED | REG_NEWLINE);
	if (reti) {
		editorSetStatusMessage("Could not compile regex: %s", regexWord);
		return;
	}

	/* Ok. Now, search for matches. Note that we're searching for
	 * matches that look like a C identifier, then taking everything
	 * up to the end of the identifier. So a search for "ed" will
	 * match "editor", not "ed". */
	char **candidates = NULL;
	int ncand = 0;
	int scand = 0;

	for (struct editorBuffer *scanbuf = ed->headbuf; scanbuf != NULL;
	     scanbuf = scanbuf->next) {
		/* Don't scan special buffers */
		if (scanbuf->special_buffer) {
			continue;
		}
		for (int rownum = 0; rownum < scanbuf->numrows; rownum++) {
			struct erow *scanrow = &scanbuf->row[rownum];
			regmatch_t pmatch;
			char *line = (char *)scanrow->chars;
			char *cursor = line;

			while (regexec(&regex, cursor, 1, &pmatch, 0) == 0) {
				/* Did we match at the beginning of the
				 * string or is the previous character not
				 * alnum? If not, then we didn't match the
				 * beginning of a word. */
				if (!((cursor == line ||
				       !alnum(*(cursor + pmatch.rm_so - 1))))) {
					cursor += pmatch.rm_eo;
					continue;
				}

				/* Copy the whole word */
				int candidateLen = pmatch.rm_eo - pmatch.rm_so;
				while (candidateLen + pmatch.rm_so <
					       scanrow->size &&
				       alnum(scanrow->chars[cursor - line +
							    pmatch.rm_so +
							    candidateLen])) {
					candidateLen++;
				}
				/* Make the copy */
				if (ncand >= scand) {
					/* Out of space, add more. */
					if (scand == 0) {
						scand = 32;
						candidates =
							xmalloc(sizeof(char *) *
								scand);
					} else {
						if (scand > INT_MAX / 2 ||
						    (size_t)scand >
							    SIZE_MAX /
								    (2 * sizeof(
									     char *))) {
							die("buffer size overflow");
						}
						scand *= 2;
						candidates = xrealloc(
							candidates,
							sizeof(char *) * scand);
					}
				}
				candidates[ncand] =
					xmalloc(candidateLen + 1);
				candidates[ncand][candidateLen] = 0;
				strncpy(candidates[ncand],
					(char *)&scanrow
						->chars[cursor - line +
							pmatch.rm_so],
					candidateLen);
				ncand++;

				/* Onward! */
				cursor += pmatch.rm_so + candidateLen;
			}

			/* Also add keywords if they match. */
			const char *keywords[] = {
				"auto",	    "break",	"case",	   "char",
				"const",    "continue", "default", "do",
				"double",   "else",	"enum",	   "extern",
				"float",    "for",	"goto",	   "if",
				"inline",   "int",	"long",	   "register",
				"restrict", "return",	"short",   "signed",
				"sizeof",   "static",	"struct",  "switch",
				"typedef",  "union",	"unsigned", "void",
				"volatile", "while",	"_Alignas", "_Alignof",
				"_Atomic",  "_Bool",	"_Complex", "_Generic",
				"_Imaginary", "_Noreturn", "_Static_assert",
				"_Thread_local", NULL
			};

			/* This is pretty dumb. We should be able to
			 * reduce the number of regex invocations to one,
			 * by searching for the regex only at the
			 * beginning of the string. */
			regmatch_t pmatch2;
			for (int i = 0; keywords[i] != NULL; i++) {
				if (regexec(&regex, keywords[i], 1, &pmatch2,
					    0) == 0 &&
				    pmatch2.rm_so == 0) {
					/* Copy it. */
					if (ncand >= scand) {
						/* Out of space, add more. */
						if (scand == 0) {
							scand = 32;
							candidates = xmalloc(
								sizeof(char *) *
								scand);
						} else {
							if (scand > INT_MAX / 2 ||
							    (size_t)scand >
								    SIZE_MAX /
									    (2 * sizeof(
										     char *))) {
								die("buffer size overflow");
							}
							scand *= 2;
							candidates = xrealloc(
								candidates,
								sizeof(char *) *
									scand);
						}
					}
					candidates[ncand] =
						xstrdup(keywords[i]);
					ncand++;
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

COMPLETE_WORD_DONE:;
	/* Replace the stem with the new word. */
	int newWordLen = strlen(candidates[sel]);
	if (wordLen < newWordLen) {
		/* Insert the additional characters */
		for (int i = wordLen; i < newWordLen; i++) {
			editorInsertChar(bufr, candidates[sel][i], 1);
		}
	} else if (wordLen > newWordLen) {
		for (int i = wordLen; i > newWordLen; i--) {
			editorDelChar(bufr, 1);
		}
	}
	/* No else clause, they're equal, nothing to do. */

COMPLETE_WORD_CLEANUP:
	regfree(&regex);
	for (int i = 0; i < ncand; i++) {
		free(candidates[i]);
	}
	free(candidates);
}

void handleMinibufferCompletion(struct editorBuffer *minibuf, enum promptType type) {
	/* Get current buffer text */
	char *current_text = minibuf->numrows > 0 ? 
	                    (char *)minibuf->row[0].chars : "";
	
	
	/* Check if text changed since last completion */
	if (minibuf->completion_state.last_completed_text == NULL ||
	    strcmp(current_text, minibuf->completion_state.last_completed_text) != 0) {
		/* Text changed - reset completion state */
		resetCompletionState(&minibuf->completion_state);
	}
	
	/* Get matches based on type */
	struct completion_result result;
	switch (type) {
		case PROMPT_FILES:
			getFileCompletions(current_text, &result);
			break;
		case PROMPT_BASIC:
			getBufferCompletions(&E, current_text, E.edbuf, &result);
			break;
		case PROMPT_COMMAND:
			getCommandCompletions(&E, current_text, &result);
			break;
	}
	
	/* Handle based on number of matches */
	if (result.n_matches == 0) {
		editorSetStatusMessage("[No match]");
		minibuf->completion_state.preserve_message = 1;
	} else if (result.n_matches == 1) {
		/* Complete fully */
		replaceMinibufferText(minibuf, result.matches[0]);
		closeCompletionsBuffer();
	} else {
		/* Multiple matches */
		if (result.common_prefix && strlen(result.common_prefix) > strlen(current_text)) {
			/* Can extend to common prefix */
			replaceMinibufferText(minibuf, result.common_prefix);
			closeCompletionsBuffer();
		} else {
			/* Already at common prefix (or no common prefix found) */
			if (minibuf->completion_state.successive_tabs > 0) {
				showCompletionsBuffer(result.matches, result.n_matches);
			} else {
				editorSetStatusMessage("[Complete, but not unique]");
				minibuf->completion_state.preserve_message = 1;
			}
		}
	}
	
	/* Update state BEFORE cleanup */
	minibuf->completion_state.successive_tabs++;
	free(minibuf->completion_state.last_completed_text);
	minibuf->completion_state.last_completed_text = xstrdup(
		minibuf->numrows > 0 ? (char *)minibuf->row[0].chars : "");
	
	/* Cleanup */
	freeCompletionResult(&result);
}