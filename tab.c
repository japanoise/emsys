#include <glob.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "emsys.h"
#include "re.h"
#include "row.h"
#include "tab.h"
#include "undo.h"
#include "unicode.h"

uint8_t *tabCompleteBufferNames(struct editorConfig *ed, uint8_t *input, struct editorBuffer *currentBuffer) {
    editorSetStatusMessage("tabCompleteBufferNames called with input: %s", input);
    editorRefreshScreen();
    usleep(1000000);  // Sleep for 0.5 seconds

    char **completions = NULL;
    int count = 0;
    uint8_t *ret = input;

    // Collect matching buffer names
    for (struct editorBuffer *b = ed->firstBuf; b != NULL; b = b->next) {
        if (b == currentBuffer) continue;

        char *name = b->filename ? b->filename : "*scratch*";
        if (strncmp(name, (char*)input, strlen((char*)input)) == 0) {
            completions = realloc(completions, (count + 1) * sizeof(char*));
            completions[count++] = strdup(name);
        }
    }

    editorSetStatusMessage("Found %d completions", count);
    editorRefreshScreen();
    usleep(1000000);

    if (count < 1) {
        goto cleanup;
    }

    if (count == 1) {
        ret = (uint8_t*)strdup(completions[0]);
        goto cleanup;
    }

    // Multiple matches, allow cycling through them
    int cur = 0;
    for (;;) {
        editorSetStatusMessage("Multiple options: %s", completions[cur]);
        editorRefreshScreen();
        editorCursorBottomLine(strlen(completions[cur]) + 19);

        int c = editorReadKey();
        switch (c) {
            case '\r':
                ret = (uint8_t*)strdup(completions[cur]);
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

    editorSetStatusMessage("Returning from tabCompleteBufferNames");
    editorRefreshScreen();
    usleep(1000000);
    return ret;
}

uint8_t *tabCompleteFiles(uint8_t *prompt) {
	glob_t globlist;
	uint8_t *ret = prompt;

	/*
	 * Define this to do manual globbing. It does mean you'll have
	 * to add the *s yourself. However, it will let you tab
	 * complete for more interesting scenarios, like
	 * *dir/other*dir/file.*.gz -> mydir/otherFOOdir/file.tar.gz
	 */
#ifndef EMSYS_NO_SIMPLE_GLOB
	int end = strlen((char*)prompt);
	prompt[end] = '*';
	prompt[end+1] = 0;
#endif

#ifndef GLOB_TILDE
	/* This isn't in POSIX, so define a fallback. */
#define GLOB_TILDE 0
#endif

	if (glob((char*)prompt, GLOB_TILDE|GLOB_MARK, NULL, &globlist))
		goto TC_FILES_CLEANUP;

	size_t cur = 0;

	if (globlist.gl_pathc < 1)
		goto TC_FILES_CLEANUP;

	if (globlist.gl_pathc == 1)
		goto TC_FILES_ACCEPT;

	int curw = stringWidth((uint8_t*)globlist.gl_pathv[cur]);

	for (;;) {
		editorSetStatusMessage("Multiple options: %s",
				       globlist.gl_pathv[cur]);
		editorRefreshScreen();
		editorCursorBottomLine(curw+19);

		int c = editorReadKey();
		switch (c) {
		case '\r':;
		TC_FILES_ACCEPT:;
			ret = calloc(strlen(globlist.gl_pathv[cur])+1, 1);
			strcpy((char*)ret, globlist.gl_pathv[cur]);
			goto TC_FILES_CLEANUP;
			break;
		case CTRL('i'):
			cur++;
			if (cur >= globlist.gl_pathc) {
				cur = 0;
			}
			curw = stringWidth((uint8_t*)globlist.gl_pathv[cur]);
			break;
		case BACKTAB:
			if (cur == 0) {
				cur = globlist.gl_pathc - 1;
			} else {
				cur--;
			}
			curw = stringWidth((uint8_t*)globlist.gl_pathv[cur]);
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
	for (int i = bufr->cx-1; i >= 0; i--) {
		if (!alnum(row->chars[i]))
			break;
		wordStart = i;
	}
	if (wordStart == bufr->cx) {
		editorSetStatusMessage("Nothing to complete here.");
		return;
	}

	char rpattern[] = "[A-Za-z0-9\200-\377_]+";
	char *word = calloc(bufr->cx-wordStart+1+sizeof(rpattern), 1);
	strncpy(word, (char*)&row->chars[wordStart], bufr->cx-wordStart);
	strcat(word, rpattern);
	int ncand = 0;
	int scand = 32;
	char **candidates = malloc(sizeof(uint8_t*)*scand);
	re_t pattern = re_compile(word);

	/* This is a deeply naive algorithm. */
	/* First, find every word that starts with the word to complete */
	for (struct editorBuffer *buf = ed->firstBuf; buf; buf=buf->next) {
		for (int i = 0; i < buf->numrows; i++) {
			if (buf == bufr && buf->cy == i)
				continue;
			struct erow *row = &bufr->row[i];
			int match_length;
			int match_idx = re_matchp(pattern, (char*)row->chars,
						  &match_length);
			if (match_idx >= 0) {
				candidates[ncand] = calloc(match_length+1, 1);
				strncpy(candidates[ncand],
					(char*)&row->chars[match_idx],
					match_length);
				ncand++;
				if (ncand >= scand) {
					scand <<= 1;
					candidates =
						realloc(candidates,
							sizeof(char*)*scand);
				}
			}
		}
	}
	/* Dunmatchin'. Restore word to non-regex contents. */
	word[bufr->cx-wordStart] = 0;

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
	qsort(candidates, ncand, sizeof(char*), sortstring);

	/* Finally, uniq' it. We now have our candidate list. */
	int newlen = 1;
	char* prev = NULL;
	for (int i = 0; i < ncand; i++) {
		if (prev==NULL) {
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
	int selw = stringWidth((uint8_t*) candidates[sel]);
	for (;;) {
		editorSetStatusMessage("Multiple options: %s",
				       candidates[sel]);
		editorRefreshScreen();
		editorCursorBottomLine(selw+19);

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
			selw = stringWidth((uint8_t*) candidates[sel]);
			break;
		case BACKTAB:
			if (sel == 0) {
				sel = ncand - 1;
			} else {
				sel--;
			}
			selw = stringWidth((uint8_t*) candidates[sel]);
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
	int completelen = strlen(candidates[sel]) - (bufr->cx-wordStart);
	struct editorUndo *new = newUndo();
	new->prev = bufr->undo;
	new->startx = bufr->cx;
	new->starty = bufr->cy;
	new->endx = bufr->cx+completelen;
	new->endy = bufr->cy;
	new->datalen = completelen;
	if (new->datasize < completelen+1) {
		new->data = realloc(new->data, new->datalen+1);
		new->datasize = new->datalen+1;
	}
	new->append = 0;
	new->delete = 0;
	new->data[0] = 0;
	strcat((char*)new->data, &candidates[sel][bufr->cx-wordStart]);
	bufr->undo = new;

	row->chars = realloc(row->chars, row->size+1+completelen);
	memcpy(&row->chars[bufr->cx+completelen], &row->chars[bufr->cx],
	       row->size-bufr->cx);
	memcpy(&row->chars[bufr->cx], &candidates[sel][bufr->cx-wordStart],
	       completelen);
	row->size += completelen;
	row->chars[row->size] = 0;
	editorUpdateRow(row);

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
