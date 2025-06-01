#include "platform.h"
#include "compat.h"
#include <glob.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include "emsys.h"
#include "row.h"
#include "tab.h"
#include "undo.h"
#include "unicode.h"

uint8_t *tabCompleteBufferNames(struct editorConfig *ed, uint8_t *input,
				struct editorBuffer *currentBuffer) {
	char **completions = NULL;
	int count = 0;
	int capacity = 8; // Initial capacity
	uint8_t *ret = input;

	completions = xmalloc(capacity * sizeof(char *));
	if (completions == NULL) {
		return ret;
	}

	for (struct editorBuffer *b = ed->firstBuf; b != NULL; b = b->next) {
		if (b == currentBuffer)
			continue;

		char *name = b->filename ? b->filename : "*scratch*";
		if (strncmp(name, (char *)input, strlen((char *)input)) == 0) {
			if (count + 1 >= capacity) {
				capacity *= 2;
				char **new_completions = xrealloc(
					completions, capacity * sizeof(char *));
				if (new_completions == NULL) {
					for (int i = 0; i < count; i++) {
						free(completions[i]);
					}
					free(completions);
					return ret;
				}
				completions = new_completions;
			}
			completions[count++] = stringdup(name);
		}
	}

	if (count < 1) {
		goto cleanup;
	}

	if (count == 1) {
		ret = (uint8_t *)stringdup(completions[0]);
		goto cleanup;
	}

	int cur = 0;
	for (;;) {
		editorSetStatusMessage("Multiple options: %s",
				       completions[cur]);
		editorRefreshScreen();
		editorCursorBottomLine(strlen(completions[cur]) + 19);

		int c = editorReadKey();
		switch (c) {
		case '\r':
			ret = (uint8_t *)stringdup(completions[cur]);
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

	if (*prompt == '~') {
		char *home_dir = getenv("HOME");
		if (!home_dir) {
			return prompt;
		}

		size_t home_len = strlen(home_dir);
		size_t prompt_len = strlen((char *)prompt);
		char *new_prompt =
			xmalloc(home_len + prompt_len - 1 +
			       1); // -1 for removed '~', +1 for null terminator
		if (!new_prompt) {
			return prompt;
		}

		strcpy(new_prompt, home_dir);
		strcpy(new_prompt + home_len, prompt + 1); // Skip the '~'
		prompt = (uint8_t *)new_prompt;
	}

	/*
	 * Define this to do manual globbing. It does mean you'll have
	 * to add the *s yourself. However, it will let you tab
	 * complete for more interesting scenarios, like
	 * *dir/other*dir/file.*.gz -> mydir/otherFOOdir/file.tar.gz
	 */
#ifndef EMSYS_NO_SIMPLE_GLOB
	int end = strlen((char *)prompt);
	prompt[end] = '*';
	prompt[end + 1] = 0;
#endif

#if HAVE_GLOB_TILDE
	if (glob((char *)prompt, GLOB_TILDE | GLOB_MARK, NULL, &globlist))
		goto TC_FILES_CLEANUP;
#else
	char *expanded_prompt = NULL;
	int tilde_result =
		portable_glob_tilde_expand((char *)prompt, &expanded_prompt);
	if (tilde_result < 0) {
		goto TC_FILES_CLEANUP;
	}

	const char *final_prompt = tilde_result > 0 ? expanded_prompt :
						      (char *)prompt;
	int glob_result = glob(final_prompt, GLOB_MARK, NULL, &globlist);

	if (expanded_prompt) {
		portable_glob_tilde_free(expanded_prompt);
	}

	if (glob_result)
		goto TC_FILES_CLEANUP;
#endif

	size_t cur = 0;

	if (globlist.gl_pathc < 1)
		goto TC_FILES_CLEANUP;

	if (globlist.gl_pathc == 1)
		goto TC_FILES_ACCEPT;

	int curw = stringWidth((uint8_t *)globlist.gl_pathv[cur]);

	for (;;) {
		editorSetStatusMessage("Multiple options: %s",
				       globlist.gl_pathv[cur]);
		editorRefreshScreen();
		editorCursorBottomLine(curw + 19);

		int c = editorReadKey();
		switch (c) {
		case '\r':;
TC_FILES_ACCEPT:;
			ret = xcalloc(strlen(globlist.gl_pathv[cur]) + 1, 1);
			strcpy((char *)ret, globlist.gl_pathv[cur]);
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
	prompt[end] = 0;
#endif
	return ret;
}

uint8_t *tabCompleteCommands(struct editorConfig *ed, uint8_t *input) {
	char **completions = NULL;
	int count = 0;
	int capacity = 8; // Initial capacity
	uint8_t *ret = input;

	completions = xmalloc(capacity * sizeof(char *));
	if (completions == NULL) {
		return ret;
	}

	for (int i = 0; i < ed->cmd_count; i++) {
		const char *cmd_name = ed->cmd[i].key;
		if (strncmp(cmd_name, (char *)input, strlen((char *)input)) ==
		    0) {
			if (count + 1 >= capacity) {
				capacity *= 2;
				char **new_completions = xrealloc(
					completions, capacity * sizeof(char *));
				if (new_completions == NULL) {
					for (int j = 0; j < count; j++) {
						free(completions[j]);
					}
					free(completions);
					return ret;
				}
				completions = new_completions;
			}
			completions[count++] = stringdup(cmd_name);
		}
	}

	if (count < 1) {
		goto cleanup;
	}

	if (count == 1) {
		ret = (uint8_t *)stringdup(completions[0]);
		goto cleanup;
	}

	int cur = 0;
	for (;;) {
		editorSetStatusMessage("Multiple options: %s",
				       completions[cur]);
		editorRefreshScreen();
		editorCursorBottomLine(strlen(completions[cur]) + 19);

		int c = editorReadKey();
		switch (c) {
		case '\r':
			ret = (uint8_t *)stringdup(completions[cur]);
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
	strcat(word, rpattern);
	int ncand = 0;
	int scand = 32;
	char **candidates = xmalloc(sizeof(uint8_t *) * scand);
	regex_t pattern;
	regmatch_t matches[1];
	int regcomp_result = regcomp(&pattern, word, REG_EXTENDED);
	if (regcomp_result != 0) {
		char error_msg[256];
		regerror(regcomp_result, &pattern, error_msg,
			 sizeof(error_msg));
		editorSetStatusMessage("Regex error: %s", error_msg);
		free(word);
		free(candidates);
		return;
	}

	/* This is a deeply naive algorithm. */
	/* First, find every word that starts with the word to complete */
	for (struct editorBuffer *buf = ed->firstBuf; buf; buf = buf->next) {
		for (int i = 0; i < buf->numrows; i++) {
			if (buf == bufr && buf->cy == i)
				continue;
			struct erow *row = &bufr->row[i];
			int regexec_result = regexec(
				&pattern, (char *)row->chars, 1, matches, 0);
			int match_idx =
				(regexec_result == 0) ? matches[0].rm_so : -1;
			int match_length =
				(regexec_result == 0) ?
					(matches[0].rm_eo - matches[0].rm_so) :
					0;
			if (match_idx >= 0) {
				candidates[ncand] = xcalloc(match_length + 1, 1);
				strncpy(candidates[ncand],
					(char *)&row->chars[match_idx],
					match_length);
				ncand++;
				if (ncand >= scand) {
					scand <<= 1;
					candidates =
						xrealloc(candidates,
							sizeof(char *) * scand);
				}
			}
		}
	}
	regfree(&pattern);
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
		editorRefreshScreen();
		editorCursorBottomLine(selw + 19);

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
	strcat((char *)new->data, &candidates[sel][bufr->cx - wordStart]);
	bufr->undo = new;

	editorRowInsertString(bufr, row, bufr->cx,
			      &candidates[sel][bufr->cx - wordStart],
			      completelen);

	editorSetStatusMessage("Expanded %.30s to %.30s", word,
			       candidates[sel]);
	bufr->cx += completelen;

COMPLETE_WORD_CLEANUP:
	for (int i = 0; i < ncand; i++) {
		free(candidates[i]);
	}
	free(candidates);
	free(word);
}
