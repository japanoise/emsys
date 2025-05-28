#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "emsys.h"
#include "command.h"
#include "find.h"
#include "region.h"
#include "register.h"
#include "row.h"
#include "transform.h"
#include "undo.h"
#include "unicode.h"
#include "unused.h"

extern struct editorConfig E;

static void regexFindCommand(void) {
	regexFind(E.buf);
}

// https://stackoverflow.com/a/779960
// You must free the result if result is non-NULL.
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

	size_t base_len = strlen(orig);
	size_t diff = (len_with > len_rep) ? (len_with - len_rep) : 0;

	// Check for overflow
	if (count > 0 && diff > 0 && count > SIZE_MAX / diff) {
		return NULL;
	}
	if (base_len > SIZE_MAX - (diff * count) - 1) {
		return NULL;
	}

	tmp = result = xmalloc(base_len + (diff * count) + 1);

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

void version(void) {
	setStatusMessage("emsys version " EMSYS_VERSION
			 ", built " EMSYS_BUILD_DATE);
}

void indentTabs(void) {
	struct editorBuffer *buf = E.buf;
	buf->indent = 0;
	setStatusMessage("Indentation set to tabs");
}

void indentSpaces(void) {
	struct editorBuffer *buf = E.buf;
	uint8_t *indentS =
		promptUser(buf, "Set indentation to: %s", PROMPT_BASIC, NULL);
	if (indentS == NULL) {
		goto cancel;
	}
	int indent = atoi((char *)indentS);
	free(indentS);
	if (indent <= 0) {
cancel:
		setStatusMessage("Canceled.");
		return;
	}
	buf->indent = indent;
	setStatusMessage("Indentation set to %i spaces", indent);
}

void revert(void) {
	struct editorBuffer *buf = E.buf;
	if (!buf->filename) {
		setStatusMessage("Buffer has no associated file to revert to");
		return;
	}

	struct editorBuffer *new = newBuffer();
	editorOpenFile(new, buf->filename);

	/* Check if file loading failed - if so, don't lose user's changes */
	if (new->numrows == 0 && !new->dirty) {
		/* File might not exist or be unreadable */
		FILE *test_fp = fopen(buf->filename, "r");
		if (!test_fp) {
			destroyBuffer(new);
			setStatusMessage(
				"Cannot revert - file no longer exists: %s",
				buf->filename);
			return;
		}
		fclose(test_fp);
	}

	new->next = buf->next;
	E.buf = new;
	if (E.headbuf == buf) {
		E.headbuf = new;
	}
	struct editorBuffer *cur = E.headbuf;
	while (cur != NULL) {
		if (cur->next == buf) {
			cur->next = new;
			break;
		}
		cur = cur->next;
	}
	for (int i = 0; i < E.nwindows; i++) {
		if (E.windows[i]->buf == buf) {
			E.windows[i]->buf = new;
		}
	}
	new->indent = buf->indent;
	new->cx = buf->cx;
	new->cy = buf->cy;
	if (new->cy > new->numrows) {
		new->cy = new->numrows;
		new->cx = 0;
	} else if (new->cx > new->row[new->cy].size) {
		new->cx = new->row[new->cy].size;
	}
	destroyBuffer(buf);
}

uint8_t *orig;
uint8_t *repl;

uint8_t *transformerReplaceString(uint8_t *input) {
	return str_replace(input, orig, repl);
}

void replaceString(void) {
	struct editorBuffer *buf = E.buf;
	orig = NULL;
	repl = NULL;
	orig = promptUser(buf, "Replace: %s", PROMPT_BASIC, NULL);
	if (orig == NULL) {
		setStatusMessage("Canceled replace-string.");
		return;
	}

	size_t prompt_size = strlen(orig) + 20;
	uint8_t *prompt = xmalloc(prompt_size);
	snprintf(prompt, prompt_size, "Replace %s with: %%s", orig);
	repl = promptUser(buf, prompt, PROMPT_BASIC, NULL);
	free(prompt);
	if (repl == NULL) {
		free(orig);
		setStatusMessage("Canceled replace-string.");
		return;
	}

	transformRegion(transformerReplaceString);

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
		uint8_t *match = NULL;
		if (buf->cx < row->size) {
			match = strstr(&(row->chars[buf->cx]), needle);
		}
		if (match) {
			if (!(buf->cx == ox && buf->cy == oy)) {
				buf->cx = match - row->chars;
				buf->marky = buf->cy;
				buf->markx = buf->cx + strlen(needle);
				/* buf->rowoff = buf->numrows; */
				return 1;
			}
			buf->cx++;
		} else {
			buf->cx = 0;
			buf->cy++;
		}
	}
	return 0;
}

void editorQueryReplace(void) {
	struct editorBuffer *buf = E.buf;
	orig = NULL;
	repl = NULL;
	orig = promptUser(buf, "Query replace: %s", PROMPT_BASIC, NULL);
	if (orig == NULL) {
		setStatusMessage("Canceled query-replace.");
		return;
	}

	size_t prompt_size = strlen(orig) + 25;
	uint8_t *prompt = xmalloc(prompt_size);
	snprintf(prompt, prompt_size, "Query replace %s with: %%s", orig);
	repl = promptUser(buf, prompt, PROMPT_BASIC, NULL);
	free(prompt);
	if (repl == NULL) {
		free(orig);
		setStatusMessage("Canceled query-replace.");
		return;
	}

	prompt_size = strlen(orig) + strlen(repl) + 32;
	prompt = xmalloc(prompt_size);
	snprintf(prompt, prompt_size, "Query replacing %s with %s:", orig,
		 repl);
	int bufwidth = stringWidth(prompt);
	int savedMx = buf->markx;
	int savedMy = buf->marky;
	struct editorUndo *first = buf->undo;
	uint8_t *newStr = NULL;
	buf->query = orig;
	int currentIdx = windowFocusedIdx(&E);
	struct editorWindow *currentWindow = E.windows[currentIdx];

#define NEXT_OCCUR(ocheck)                 \
	if (!nextOccur(buf, orig, ocheck)) \
	goto QR_CLEANUP

	NEXT_OCCUR(false);

	for (;;) {
		setStatusMessage("%s", prompt);
		refreshScreen();
		cursorBottomLine(bufwidth + 2);

		int c = readKey();
		recordKey(c);
		switch (c) {
		case ' ':
		case 'y':
			transformRegion(transformerReplaceString);
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
			transformRegion(transformerReplaceString);
			goto QR_CLEANUP;
			break;
		case '!':
		case 'Y':
			buf->marky = buf->numrows - 1;
			buf->markx = buf->row[buf->marky].size;
			transformRegion(transformerReplaceString);
			goto QR_CLEANUP;
			break;
		case 'u':
			doUndo(1);
			buf->markx = buf->cx;
			buf->marky = buf->cy;
			buf->cx -= strlen(orig);
			break;
		case 'U':
			while (buf->undo != first)
				doUndo(1);
			buf->markx = buf->cx;
			buf->marky = buf->cy;
			buf->cx -= strlen(orig);
			break;
		case CTRL('r'):
			prompt_size = strlen(orig) + 25;
			prompt = xmalloc(prompt_size);
			snprintf(prompt, prompt_size,
				 "Replace this %.60s with: ", orig);
			newStr = promptUser(buf, prompt, PROMPT_BASIC, NULL);
			free(prompt);
			if (newStr == NULL) {
				goto RESET_PROMPT;
			}
			uint8_t *tmp = repl;
			repl = newStr;
			transformRegion(transformerReplaceString);
			repl = tmp;
			NEXT_OCCUR(true);
			goto RESET_PROMPT;
			break;
		case 'e':
		case 'E':
			prompt_size = strlen(orig) + 25;
			prompt = xmalloc(prompt_size);
			snprintf(prompt, prompt_size,
				 "Query replace %.60s with: ", orig);
			newStr = promptUser(buf, prompt, PROMPT_BASIC, NULL);
			free(prompt);
			if (newStr == NULL) {
				goto RESET_PROMPT;
			}
			free(repl);
			repl = newStr;
			transformRegion(transformerReplaceString);
			NEXT_OCCUR(true);
RESET_PROMPT:
			prompt_size = strlen(orig) + strlen(repl) + 32;
			prompt = xmalloc(prompt_size);
			snprintf(prompt, prompt_size,
				 "Query replacing %.60s with %.60s:", orig,
				 repl);
			bufwidth = stringWidth(prompt);
			break;
		case CTRL('l'):
			recenter(currentWindow);
			break;
		}
	}

QR_CLEANUP:
	setStatusMessage("");
	buf->query = NULL;
	buf->markx = savedMx;
	buf->marky = savedMy;
	if (orig) {
		free(orig);
		orig = NULL;
	}
	if (repl) {
		free(repl);
		repl = NULL;
	}
	if (prompt) {
		free(prompt);
		prompt = NULL;
	}
}

void capitalizeRegion(void) {
	transformRegion(transformerCapitalCase);
}

void whitespaceCleanup(void) {
	struct editorBuffer *buf = E.buf;
	unsigned int trailing = 0;
	for (int i = 0; i < buf->numrows; i++) {
		erow *row = &buf->row[i];
		if (row->size == 0)
			continue;
		for (int j = row->size - 1; j >= 0; j--) {
			if (row->chars[j] == ' ' || row->chars[j] == '\t') {
				row->size--;
				trailing++;
			} else {
				break;
			}
		}
	}

	if (buf->cy < buf->numrows && buf->cx > buf->row[buf->cy].size) {
		buf->cx = buf->row[buf->cy].size;
	}

	if (trailing > 0) {
		clearUndosAndRedos();
		setStatusMessage("%d trailing characters removed", trailing);
	} else {
		setStatusMessage("No change.");
	}
}

void toggleTruncateLines(void) {
	struct editorBuffer *buf = E.buf;
	buf->truncate_lines = !buf->truncate_lines;
	setStatusMessage(buf->truncate_lines ? "Truncate long lines enabled" :
					       "Truncate long lines disabled");
}

void editorToggleReadOnly(void) {
	struct editorBuffer *buf = E.buf;
	buf->read_only = !buf->read_only;
	setStatusMessage(buf->read_only ? "Read-only mode enabled" :
					  "Read-only mode disabled");
}

void describeKey(void) {
	setStatusMessage("Describe key: ");
	E.describe_key_mode = 1;
}

void viewManPage(void) {
	FILE *fp = popen("man -w emsys 2>/dev/null", "r");
	if (!fp) {
		setStatusMessage("Cannot check for man page");
		return;
	}

	char path[256];
	if (!fgets(path, sizeof(path), fp) || strlen(path) < 2) {
		pclose(fp);
		setStatusMessage("No man page found for emsys");
		return;
	}
	pclose(fp);

	struct editorBuffer *manpage = newBuffer();
	manpage->filename = stringdup("*man emsys*");
	manpage->special_buffer = 1;
	manpage->read_only = 1;

	fp = popen("man emsys 2>/dev/null | col -b", "r");
	if (!fp) {
		destroyBuffer(manpage);
		setStatusMessage("Cannot run man command");
		return;
	}

	char line[1024];
	while (fgets(line, sizeof(line), fp)) {
		int len = strlen(line);
		if (len > 0 && line[len - 1] == '\n') {
			line[len - 1] = '\0';
			len--;
		}
		insertRow(manpage, manpage->numrows, line, len);
	}
	pclose(fp);

	if (manpage->numrows == 0) {
		destroyBuffer(manpage);
		setStatusMessage("Man page is empty");
		return;
	}

	E.buf = manpage;
	for (int i = 0; i < E.nwindows; i++) {
		if (E.windows[i]->focused) {
			E.windows[i]->buf = manpage;
		}
	}
}

void helpForHelp(void) {
	setStatusMessage(
		"C-h k: describe key, C-h m: view man page, M-x TAB: list commands");
}

/* No more wrapper functions needed - all commands use global state */

#define ADDCMD(name, func)                \
	newCmd = xmalloc(sizeof *newCmd); \
	newCmdName = name;                \
	newCmd->cmd = func;               \
	HASH_ADD_KEYPTR(hh, ed->cmd, newCmdName, strlen(newCmdName), newCmd)

#ifdef EMSYS_DEBUG_UNDO
void debugUnpair(void) {
	struct editorBuffer *buf = E.buf;
	int undos = 0;
	int redos = 0;
	for (struct editorUndo *i = buf->undo; i; i = i->prev) {
		i->paired = 0;
		undos++;
	}
	for (struct editorUndo *i = buf->redo; i; i = i->prev) {
		i->paired = 0;
		redos++;
	}
	setStatusMessage("Unpaired %d undos, %d redos.", undos, redos);
}
#endif

// Comparison function for qsort and bsearch
static int compare_commands(const void *a, const void *b) {
	return strcmp(((struct editorCommand *)a)->key,
		      ((struct editorCommand *)b)->key);
}

void setupCommands(void) {
	static struct editorCommand commands[] = {
		{ "capitalize-region", capitalizeRegion },
		{ "describe-key", describeKey },
		{ "help", viewManPage },
		{ "help-for-help", helpForHelp },
		{ "indent-spaces", indentSpaces },
		{ "indent-tabs", indentTabs },
		{ "isearch-forward-regexp", regexFindCommand },
		{ "kanaya", capitalizeRegion },
		{ "man", viewManPage },
		{ "query-replace", editorQueryReplace },
		{ "recenter", recenterCommand },
		{ "replace-regexp", replaceRegex },
		{ "replace-string", replaceString },
		{ "revert", revert },
		{ "toggle-truncate-lines", toggleTruncateLines },
		{ "version", version },
		{ "view-man-page", viewManPage },
		{ "view-register", viewRegister },
		{ "whitespace-cleanup", whitespaceCleanup },
#ifdef EMSYS_DEBUG_UNDO
		{ "debug-unpair", debugUnpair },
#endif
	};

	E.cmd = commands;
	E.cmd_count = sizeof(commands) / sizeof(commands[0]);

	// Sort the commands array
	qsort(E.cmd, E.cmd_count, sizeof(struct editorCommand),
	      compare_commands);
}

void runCommand(char *cmd) {
	for (int i = 0; cmd[i]; i++) {
		uint8_t c = cmd[i];
		if ('A' <= c && c <= 'Z') {
			c |= 0x60;
		} else if (c == ' ') {
			c = '-';
		}
		cmd[i] = c;
	}

	struct editorCommand key = { cmd, NULL };
	struct editorCommand *found = bsearch(&key, E.cmd, E.cmd_count,
					      sizeof(struct editorCommand),
					      compare_commands);

	if (found) {
		found->cmd();
	} else {
		setStatusMessage("No command found");
	}
}
