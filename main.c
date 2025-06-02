#include "platform.h"
#include "compat.h"
#include "terminal.h"
#include "display.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include "bound.h"
#include "command.h"
#include "emsys.h"
#include "find.h"
#include "pipe.h"
#include "region.h"
#include "register.h"
#include "row.h"
#include "tab.h"
#include "transform.h"
#include "undo.h"
#include "unicode.h"
#include "unused.h"
#include "keybindings.h"

const int minibuffer_height = 1;
const int statusbar_height = 1;
const int page_overlap = 2;

// POSIX 2001 compliant alternative to strdup
char *stringdup(const char *s) {
	size_t len = strlen(s) + 1; // +1 for the null terminator
	char *new_str = xmalloc(len);
	return memcpy(new_str, s, len);
}

struct editorConfig E;
void moveCursor(struct editorBuffer *bufr, int key);
void setupHandlers();

void die(const char *s) {
	write(STDOUT_FILENO, CSI "2J", 4);
	write(STDOUT_FILENO, CSI "H", 3);
	perror(s);
	exit(1);
}

/* Safe memory allocation wrappers */
void *xmalloc(size_t size) {
	void *ptr = malloc(size);
	if (!ptr && size > 0) {
		die("xmalloc: out of memory");
	}
	return ptr;
}

void *xrealloc(void *ptr, size_t size) {
	void *new_ptr = realloc(ptr, size);
	if (!new_ptr && size > 0) {
		die("xrealloc: out of memory");
	}
	return new_ptr;
}

void *xcalloc(size_t nmemb, size_t size) {
	void *ptr = calloc(nmemb, size);
	if (!ptr && nmemb > 0 && size > 0) {
		die("xcalloc: out of memory");
	}
	return ptr;
}

int windowFocusedIdx(struct editorConfig *ed) {
	for (int i = 0; i < E.nwindows; i++) {
		if (ed->windows[i]->focused) {
			return i;
		}
	}
	/* You're in trouble m80 */
	return 0;
}

void validateCursorY(struct editorBuffer *buf) {
	if (buf->cy >= buf->numrows) {
		buf->cy = buf->numrows > 0 ? buf->numrows - 1 : 0;
	}
}

void validateCursorX(struct editorBuffer *buf) {
	if (buf->cy < buf->numrows && buf->cx > buf->row[buf->cy].size) {
		buf->cx = buf->row[buf->cy].size;
	}
}

void validateCursorPosition(struct editorBuffer *buf) {
	validateCursorY(buf);
	validateCursorX(buf);
}

erow *safeGetRow(struct editorBuffer *buf, int row_index) {
	return (row_index >= buf->numrows) ? NULL : &buf->row[row_index];
}

int nextScreenX(char *chars, int *i, int current_screen_x) {
	if (chars[*i] == '\t') {
		current_screen_x = (current_screen_x + EMSYS_TAB_STOP) /
				   EMSYS_TAB_STOP * EMSYS_TAB_STOP;
	} else if (ISCTRL(chars[*i])) {
		current_screen_x += 2;
	} else {
		current_screen_x += charInStringWidth(chars, *i);
	}
	*i += utf8_nBytes(chars[*i]) - 1;
	return current_screen_x;
}

void synchronizeBufferCursor(struct editorBuffer *buf,
			     struct editorWindow *win) {
	if (win->cy >= buf->numrows) {
		win->cy = buf->numrows > 0 ? buf->numrows - 1 : 0;
	}
	if (win->cy < buf->numrows && win->cx > buf->row[win->cy].size) {
		win->cx = buf->row[win->cy].size;
	}

	buf->cx = win->cx;
	buf->cy = win->cy;
}

void switchWindow(struct editorConfig *ed) {
	if (ed->nwindows == 1) {
		setStatusMessage("No other windows to select");
		return;
	}

	int currentIdx = windowFocusedIdx(ed);
	struct editorWindow *currentWindow = ed->windows[currentIdx];
	struct editorBuffer *currentBuffer = currentWindow->buf;

	currentWindow->cx = currentBuffer->cx;
	currentWindow->cy = currentBuffer->cy;

	currentWindow->focused = 0;
	int nextIdx = (currentIdx + 1) % ed->nwindows;
	struct editorWindow *nextWindow = ed->windows[nextIdx];
	nextWindow->focused = 1;

	ed->focusBuf = nextWindow->buf;

	ed->focusBuf->cx = nextWindow->cx;
	ed->focusBuf->cy = nextWindow->cy;

	synchronizeBufferCursor(ed->focusBuf, nextWindow);
}

/*** editor operations ***/

void editorInsertChar(int c) {
	struct editorBuffer *bufr = E.focusBuf;
	if (bufr->cy == bufr->numrows) {
		insertRow(bufr, bufr->numrows, "", 0);
	}
	rowInsertChar(bufr, &bufr->row[bufr->cy], bufr->cx, c);
	bufr->cx++;
}

void bufferInsertChar(struct editorBuffer *bufr, int c) {
	if (bufr->cy == bufr->numrows) {
		insertRow(bufr, bufr->numrows, "", 0);
	}
	rowInsertChar(bufr, &bufr->row[bufr->cy], bufr->cx, c);
	bufr->cx++;
}

void insertUnicode(struct editorBuffer *bufr) {
	if (bufr->cy == bufr->numrows) {
		insertRow(bufr, bufr->numrows, "", 0);
	}
	rowInsertUnicode(&bufr->row[bufr->cy], bufr->cx);
	bufr->cx += E.nunicode;
}

void insertNewline(struct editorBuffer *bufr) {
	if (bufr->cx == 0) {
		insertRow(bufr, bufr->cy, "", 0);
	} else {
		erow *row = &bufr->row[bufr->cy];
		insertRow(bufr, bufr->cy + 1, &row->chars[bufr->cx],
				row->size - bufr->cx);
		row = &bufr->row[bufr->cy];
		rowDeleteRange(bufr, row, bufr->cx, row->size);
	}
	bufr->cy++;
	bufr->cx = 0;
}

void openLine(struct editorBuffer *bufr) {
	int ccx = bufr->cx;
	int ccy = bufr->cy;
	insertNewline(bufr);
	bufr->cx = ccx;
	bufr->cy = ccy;
}

void insertNewlineAndIndent(struct editorBuffer *bufr) {
	undoAppendChar(bufr, '\n');
	insertNewline(bufr);
	int i = 0;
	uint8_t c = bufr->row[bufr->cy - 1].chars[i];
	while (c == ' ' || c == CTRL('i')) {
		undoAppendChar(bufr, c);
		bufferInsertChar(bufr, c);
		c = bufr->row[bufr->cy - 1].chars[++i];
	}
}

void indent(struct editorBuffer *bufr, int rept) {
	int ocx = bufr->cx;
	int indWidth = 1;
	if (bufr->indent) {
		indWidth = bufr->indent;
	}
	bufr->cx = 0;
	for (int i = 0; i < rept; i++) {
		if (bufr->indent) {
			for (int i = 0; i < bufr->indent; i++) {
				undoAppendChar(bufr, ' ');
				bufferInsertChar(bufr, ' ');
			}
		} else {
			undoAppendChar(bufr, '\t');
			bufferInsertChar(bufr, '\t');
		}
	}
	bufr->cx = ocx + indWidth * rept;
}

void unindent(struct editorBuffer *bufr, int rept) {
	if (bufr->cy >= bufr->numrows) {
		setStatusMessage("End of buffer.");
		return;
	}

	/* Setup for indent mode */
	int indWidth = 1;
	char indCh = '\t';
	struct erow *row = &bufr->row[bufr->cy];
	if (bufr->indent) {
		indWidth = bufr->indent;
		indCh = ' ';
	}

	/* Calculate size of unindent */
	int trunc = 0;
	for (int i = 0; i < rept; i++) {
		for (int j = 0; j < indWidth; j++) {
			if (row->chars[trunc] != indCh)
				goto UNINDENT_PERFORM;
			trunc++;
		}
	}

UNINDENT_PERFORM:
	if (trunc == 0)
		return;

	/* Create undo */
	struct editorUndo *new = newUndo();
	new->prev = bufr->undo;
	new->startx = 0;
	new->starty = bufr->cy;
	new->endx = trunc;
	new->endy = bufr->cy;
	new->delete = 1;
	new->append = 0;
	bufr->undo = new;
	if (new->datasize < trunc - 1) {
		new->datasize = trunc + 1;
		new->data = xrealloc(new->data, new->datasize);
	}
	memset(new->data, indCh, trunc);
	new->data[trunc] = 0;
	new->datalen = trunc;

	/* Perform row operation & dirty buffer */
	rowDeleteRange(bufr, row, 0, trunc);
	bufr->cx -= trunc;
	bufr->dirty = 1;
}

void delChar(struct editorBuffer *bufr) {
	if (bufr->cy == bufr->numrows)
		return;
	if (bufr->cy == bufr->numrows - 1 &&
	    bufr->cx == bufr->row[bufr->cy].size)
		return;

	erow *row = &bufr->row[bufr->cy];
	undoDelChar(bufr, row);
	if (bufr->cx == row->size) {
		row = &bufr->row[bufr->cy + 1];
		rowInsertString(bufr, &bufr->row[bufr->cy],
				      bufr->row[bufr->cy].size, row->chars,
				      row->size);
		delRow(bufr, bufr->cy + 1);
	} else {
		rowDelChar(bufr, row, bufr->cx);
	}
}

void backSpace(struct editorBuffer *bufr) {
	if (!bufr->numrows)
		return;
	if (bufr->cy == bufr->numrows) {
		bufr->cx = bufr->row[--bufr->cy].size;
		return;
	}
	if (bufr->cy == 0 && bufr->cx == 0)
		return;

	erow *row = &bufr->row[bufr->cy];
	if (bufr->cx > 0) {
		do {
			bufr->cx--;
			undoBackSpace(bufr, row->chars[bufr->cx]);
		} while (utf8_isCont(row->chars[bufr->cx]));
		rowDelChar(bufr, row, bufr->cx);
	} else {
		undoBackSpace(bufr, '\n');
		bufr->cx = bufr->row[bufr->cy - 1].size;
		rowInsertString(bufr, &bufr->row[bufr->cy - 1],
				      bufr->row[bufr->cy - 1].size, row->chars,
				      row->size);
		delRow(bufr, bufr->cy);
		bufr->cy--;
	}
}

void killLine(struct editorBuffer *buf) {
	if (buf->numrows <= 0) {
		return;
	}

	erow *row = &buf->row[buf->cy];

	if (buf->cx == row->size) {
		delChar(buf);
	} else {
		int kill_len = row->size - buf->cx;
		free(E.kill);
		E.kill = xmalloc(kill_len + 1);
		memcpy(E.kill, &row->chars[buf->cx], kill_len);
		E.kill[kill_len] = '\0';

		clearRedos(buf);
		struct editorUndo *new = newUndo();
		new->starty = buf->cy;
		new->endy = buf->cy;
		new->startx = buf->cx;
		new->endx = row->size;
		new->delete = 1;
		new->prev = buf->undo;
		buf->undo = new;

		new->datalen = kill_len;
		if (new->datasize < new->datalen + 1) {
			new->datasize = new->datalen + 1;
			new->data = xrealloc(new->data, new->datasize);
		}
		for (int i = 0; i < kill_len; i++) {
			new->data[i] = E.kill[kill_len - i - 1];
		}
		new->data[kill_len] = '\0';

		rowDeleteRange(buf, row, buf->cx, row->size);
		buf->dirty = 1;
		clearMark(buf);
	}
}

void killLineBackwards(struct editorBuffer *buf) {
	if (buf->cx == 0) {
		return;
	}

	erow *row = &buf->row[buf->cy];

	free(E.kill);
	E.kill = xmalloc(buf->cx + 1);
	memcpy(E.kill, row->chars, buf->cx);
	E.kill[buf->cx] = '\0';

	clearRedos(buf);
	struct editorUndo *new = newUndo();
	new->starty = buf->cy;
	new->endy = buf->cy;
	new->startx = 0;
	new->endx = buf->cx;
	new->delete = 1;
	new->prev = buf->undo;
	buf->undo = new;

	new->datalen = buf->cx;
	if (new->datasize < new->datalen + 1) {
		new->datasize = new->datalen + 1;
		new->data = xrealloc(new->data, new->datasize);
	}
	for (int i = 0; i < buf->cx; i++) {
		new->data[i] = E.kill[buf->cx - i - 1];
	}
	new->data[buf->cx] = '\0';

	rowDeleteRange(buf, row, 0, buf->cx);
	buf->cx = 0;
	buf->dirty = 1;
}


void recordKey(int c) {
	if (E.recording) {
		E.macro.keys[E.macro.nkeys++] = c;
		if (E.macro.nkeys >= E.macro.skeys) {
			E.macro.skeys *= 2;
			E.macro.keys = xrealloc(E.macro.keys,
					       E.macro.skeys * sizeof(int));
		}
		if (c == UNICODE) {
			for (int i = 0; i < E.nunicode; i++) {
				E.macro.keys[E.macro.nkeys++] = E.unicode[i];
				if (E.macro.nkeys >= E.macro.skeys) {
					E.macro.skeys *= 2;
					E.macro.keys = xrealloc(
						E.macro.keys,
						E.macro.skeys * sizeof(int));
				}
			}
		}
	}
}

/*** file i/o ***/

char *rowsToString(struct editorBuffer *bufr, int *buflen) {
	int totlen = 0;
	int j;
	for (j = 0; j < bufr->numrows; j++) {
		totlen += bufr->row[j].size + 1;
	}
	*buflen = totlen;

	char *buf = xmalloc(totlen);
	char *p = buf;
	for (j = 0; j < bufr->numrows; j++) {
		memcpy(p, bufr->row[j].chars, bufr->row[j].size);
		p += bufr->row[j].size;
		*p = '\n';
		p++;
	}

	return buf;
}

void editorOpenFile(struct editorBuffer *bufr, char *filename) {
	free(bufr->filename);
	bufr->filename = stringdup(filename);
	FILE *fp = fopen(filename, "r");
	if (!fp) {
		if (errno == ENOENT) {
			setStatusMessage("(New file)", bufr->filename);
			return;
		}
		die("fopen");
	}

	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	/* Doesn't handle null bytes */
	while ((linelen = getline(&line, &linecap, fp)) != -1) {
		while (linelen > 0 &&
		       (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
			linelen--;
		insertRow(bufr, bufr->numrows, line, linelen);
	}
	free(line);
	fclose(fp);
	bufr->dirty = 0;
}

void save(struct editorBuffer *bufr) {
	if (bufr->filename == NULL) {
		bufr->filename = (char *)
			promptUser(bufr, (uint8_t *)"Save as: %s", PROMPT_FILES, NULL);
		if (bufr->filename == NULL) {
			setStatusMessage("Save aborted.");
			return;
		}
	}

	int len;
	char *buf = rowsToString(bufr, &len);

	int fd = open(bufr->filename, O_RDWR | O_CREAT, 0644);
	if (fd != -1) {
		if (ftruncate(fd, len) != -1) {
			if (write(fd, buf, len) == len) {
				close(fd);
				free(buf);
				bufr->dirty = 0;
				setStatusMessage("Wrote %d bytes to %s",
						       len, bufr->filename);
				return;
			}
		}
		close(fd);
	}

	free(buf);
	setStatusMessage("Save failed: %s", strerror(errno));
}

void setStatusMessage(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	int ret = vsnprintf(E.minibuffer, sizeof(E.minibuffer), fmt, ap);
	va_end(ap);
	if (ret >= (int)sizeof(E.minibuffer)) {
		strcpy(E.minibuffer + sizeof(E.minibuffer) - 4, "...");
	}
	E.statusmsg_time = time(NULL);
}

void recenterCommand(struct editorConfig *ed,
			   struct editorBuffer *UNUSED(buf)) {
	int winIdx = windowFocusedIdx(ed);
	recenter(ed->windows[winIdx]);
}

void suspend(int UNUSED(sig)) {
	signal(SIGTSTP, SIG_DFL);
	disableRawMode();
	raise(SIGTSTP);
}

void resume(int sig) {
	setupHandlers();
	enableRawMode();
	editorResizeScreen(sig);
}

/*** input ***/

uint8_t *promptUser(struct editorBuffer *bufr, uint8_t *prompt,
		      enum promptType t,
		      void (*callback)(struct editorBuffer *, uint8_t *, int)) {
	size_t bufsize = 128;
	uint8_t *buf = xmalloc(bufsize);

	int promptlen = stringWidth(prompt) - 2;

	size_t buflen = 0;
	size_t bufwidth = 0;
	size_t curs = 0;
	size_t cursScr = 0;
	buf[0] = 0;

	for (;;) {
		setStatusMessage(prompt, buf);
		refreshScreen();
#ifdef EMSYS_DEBUG_PROMPT
		char dbg[32];
		snprintf(dbg, sizeof(dbg), CSI "%d;%dHc: %ld cs: %ld", 0, 0,
			 curs, cursScr);
		write(STDOUT_FILENO, dbg, strlen(dbg));
#endif
		cursorBottomLineLong(promptlen + cursScr + 1);

		int c = readKey();
		recordKey(c);
		switch (c) {
		case '\r':
			setStatusMessage("");
			if (callback)
				callback(bufr, buf, c);
			return buf;
		case CTRL('g'):
		case CTRL('c'):
			setStatusMessage("");
			if (callback)
				callback(bufr, buf, c);
			free(buf);
			return NULL;
			break;
		case CTRL('h'):
		case BACKSPACE:
PROMPT_BACKSPACE:
			if (curs <= 0)
				break;
			if (buflen == 0) {
				break;
			}
			int w = 1;
			curs--;
			while (utf8_isCont(buf[curs])) {
				curs--;
				w++;
			}
			cursScr -= charInStringWidth(buf, curs);
			memmove(&(buf[curs]), &(buf[curs + w]),
				bufsize - (curs + w));
			buflen -= w;
			bufwidth = stringWidth(buf);
			break;
		case CTRL('i'):
			if (t == PROMPT_FILES) {
				uint8_t *tc = tabCompleteFiles(buf);
				if (tc != buf) {
					free(buf);
					buf = tc;
					buflen = strlen((char *)buf);
					bufsize = buflen + 1;
					bufwidth = stringWidth(buf);
					curs = buflen;
					cursScr = bufwidth;
				}
			} else if (t == PROMPT_COMMANDS) {
				uint8_t *tc = tabCompleteCommands(&E, buf);
				if (tc && tc != buf) {
					free(buf);
					buf = tc;
					buflen = strlen((char *)buf);
					bufsize = buflen + 1;
					bufwidth = stringWidth(buf);
					curs = buflen;
					cursScr = bufwidth;
				}
			} else if (t == PROMPT_BASIC) {
				uint8_t *tc =
					tabCompleteBufferNames(&E, buf, bufr);
				if (tc && tc != buf) {
					free(buf);
					buf = tc;
					buflen = strlen((char *)buf);
					bufsize = buflen + 1;
					bufwidth = stringWidth(buf);
					curs = buflen;
					cursScr = bufwidth;
				}
			}
			break;
		case CTRL('a'):
		case HOME_KEY:
			curs = 0;
			cursScr = 0;
			break;
		case CTRL('e'):
		case END_KEY:
			curs = buflen;
			cursScr = bufwidth;
			break;
		case CTRL('k'):
			buf[curs] = 0;
			buflen = curs;
			bufwidth = stringWidth(buf);
			break;
		case CTRL('u'):
			if (curs == buflen) {
				buflen = 0;
				bufwidth = 0;
				buf[0] = 0;
			} else {
				memmove(buf, &(buf[curs]), bufsize - curs);
				buflen = strlen(buf);
				bufwidth = stringWidth(buf);
			}
			cursScr = 0;
			curs = 0;
			break;
		case ARROW_LEFT:
			if (curs <= 0)
				break;
			curs--;
			while (utf8_isCont(buf[curs]))
				curs--;
			cursScr -= charInStringWidth(buf, curs);
			break;
		case DEL_KEY:
		case CTRL('d'):
		case ARROW_RIGHT:
			if (curs >= buflen)
				break;
			cursScr += charInStringWidth(buf, curs);
			curs++;
			while (utf8_isCont(buf[curs]))
				curs++;
			if (c == CTRL('d') || c == DEL_KEY) {
				goto PROMPT_BACKSPACE;
			}
			break;
		case UNICODE:;
			buflen += E.nunicode;
			if (buflen >= (bufsize - 5)) {
				bufsize *= 2;
				char *newbuf = xrealloc(buf, bufsize);
				if (newbuf == NULL) {
					free(buf);
					return NULL;
				}
				buf = newbuf;
			}
			if (curs == buflen) {
				for (int i = 0; i < E.nunicode; i++) {
					buf[(buflen - E.nunicode) + i] =
						E.unicode[i];
				}
				buf[buflen] = 0;
			} else {
				memmove(&(buf[curs + E.nunicode]), &(buf[curs]),
					bufsize - (curs + E.nunicode));
				for (int i = 0; i < E.nunicode; i++) {
					buf[curs + i] = E.unicode[i];
				}
			}
			cursScr += charInStringWidth(buf, curs);
			curs += E.nunicode;
			bufwidth = stringWidth(buf);
			break;
		default:
			if (!ISCTRL(c) && c < 256) {
				if (buflen >= bufsize - 5) {
					bufsize *= 2;
					char *newbuf = xrealloc(buf, bufsize);
					if (newbuf == NULL) {
						free(buf);
						return NULL;
					}
					buf = newbuf;
				}
				if (curs == buflen) {
					buf[buflen++] = c;
					buf[buflen] = 0;
				} else {
					memmove(&(buf[curs + 1]), &(buf[curs]),
						bufsize - 1);
					buf[curs] = c;
					buflen++;
				}
				bufwidth++;
				curs++;
				cursScr++;
			}
		}

		if (callback)
			callback(bufr, buf, c);
	}
}

void editorMoveCursor(int key) {
	struct editorBuffer *bufr = E.focusBuf;
	erow *row = safeGetRow(bufr, bufr->cy);

	switch (key) {
	case ARROW_LEFT:
		if (bufr->cx != 0) {
			do
				bufr->cx--;
			while (bufr->cx != 0 &&
			       utf8_isCont(row->chars[bufr->cx]));
		} else if (bufr->cy > 0) {
			bufr->cy--;
			bufr->cx = bufr->row[bufr->cy].size;
		}
		break;

	case ARROW_RIGHT:
		if (row && bufr->cx < row->size) {
			bufr->cx += utf8_nBytes(row->chars[bufr->cx]);
		} else if (row && bufr->cx == row->size) {
			bufr->cy++;
			bufr->cx = 0;
		}
		break;
	case ARROW_UP:
		if (bufr->cy > 0) {
			bufr->cy--;
			if (bufr->row[bufr->cy].chars == NULL)
				break;
			while (utf8_isCont(bufr->row[bufr->cy].chars[bufr->cx]))
				bufr->cx++;
		}
		break;
	case ARROW_DOWN:
		if (bufr->cy < bufr->numrows) {
			bufr->cy++;
			if (bufr->cy == bufr->numrows) {
				bufr->cx = 0;
				break;
			}
			if (bufr->row[bufr->cy].chars == NULL)
				break;
			while (bufr->cx < bufr->row[bufr->cy].size &&
			       utf8_isCont(bufr->row[bufr->cy].chars[bufr->cx]))
				bufr->cx++;
		}
		break;
	}
	row = safeGetRow(bufr, bufr->cy);
	int rowlen = row ? row->size : 0;
	if (bufr->cx > rowlen) {
		bufr->cx = rowlen;
	}
}

void bufferMoveCursor(struct editorBuffer *bufr, int key) {
	erow *row = safeGetRow(bufr, bufr->cy);

	switch (key) {
	case ARROW_LEFT:
		if (bufr->cx != 0) {
			do
				bufr->cx--;
			while (bufr->cx != 0 &&
			       utf8_isCont(row->chars[bufr->cx]));
		} else if (bufr->cy > 0) {
			bufr->cy--;
			bufr->cx = bufr->row[bufr->cy].size;
		}
		break;

	case ARROW_RIGHT:
		if (row && bufr->cx < row->size) {
			bufr->cx += utf8_nBytes(row->chars[bufr->cx]);
		} else if (row && bufr->cx == row->size) {
			bufr->cy++;
			bufr->cx = 0;
		}
		break;
	case ARROW_UP:
		if (bufr->cy > 0) {
			bufr->cy--;
			if (bufr->row[bufr->cy].chars == NULL)
				break;
			while (utf8_isCont(bufr->row[bufr->cy].chars[bufr->cx]))
				bufr->cx++;
		}
		break;
	case ARROW_DOWN:
		if (bufr->cy < bufr->numrows) {
			bufr->cy++;
			if (bufr->cy == bufr->numrows) {
				bufr->cx = 0;
				break;
			}
			if (bufr->row[bufr->cy].chars == NULL)
				break;
			while (bufr->cx < bufr->row[bufr->cy].size &&
			       utf8_isCont(bufr->row[bufr->cy].chars[bufr->cx]))
				bufr->cx++;
		}
		break;
	}
	row = safeGetRow(bufr, bufr->cy);
	int rowlen = row ? row->size : 0;
	if (bufr->cx > rowlen) {
		bufr->cx = rowlen;
	}
}

void bufferEndOfForwardWord(struct editorBuffer *buf, int *dx, int *dy) {
	int cx = buf->cx;
	int icy = buf->cy;
	if (icy >= buf->numrows) {
		*dx = cx;
		*dy = icy;
		return;
	}
	int pre = 1;
	for (int cy = icy; cy < buf->numrows; cy++) {
		int l = buf->row[cy].size;
		while (cx < l) {
			uint8_t c = buf->row[cy].chars[cx];
			if (isWordBoundary(c) && !pre) {
				*dx = cx;
				*dy = cy;
				return;
			} else if (!isWordBoundary(c)) {
				pre = 0;
			}
			cx++;
		}
		if (!pre) {
			*dx = cx;
			*dy = cy;
			return;
		}
		cx = 0;
	}
	*dx = cx;
	*dy = icy;
}

void bufferEndOfBackwardWord(struct editorBuffer *buf, int *dx, int *dy) {
	int cx = buf->cx;
	int icy = buf->cy;

	if (icy >= buf->numrows) {
		return;
	}

	int pre = 1;

	for (int cy = icy; cy >= 0; cy--) {
		if (cy != icy) {
			cx = buf->row[cy].size;
		}
		while (cx > 0) {
			uint8_t c = buf->row[cy].chars[cx - 1];
			if (isWordBoundary(c) && !pre) {
				*dx = cx;
				*dy = cy;
				return;
			} else if (!isWordBoundary(c)) {
				pre = 0;
			}
			cx--;
		}
		if (!pre) {
			*dx = cx;
			*dy = cy;
			return;
		}
	}

	*dx = cx;
	*dy = 0;
}

void forwardWord(struct editorBuffer *bufr) {
	bufferEndOfForwardWord(bufr, &bufr->cx, &bufr->cy);
}

void backWord(struct editorBuffer *bufr) {
	bufferEndOfBackwardWord(bufr, &bufr->cx, &bufr->cy);
}

void wordTransform(struct editorConfig *ed, struct editorBuffer *bufr,
		   int times, uint8_t *(*transformer)(uint8_t *)) {
	int icx = bufr->cx;
	int icy = bufr->cy;
	for (int i = 0; i < times; i++) {
		bufferEndOfForwardWord(bufr, &bufr->cx, &bufr->cy);
	}
	bufr->markx = icx;
	bufr->marky = icy;
	transformRegion(transformer);
}

void upcaseWord(struct editorConfig *ed, struct editorBuffer *bufr,
		      int times) {
	wordTransform(ed, bufr, times, transformerUpcase);
}

void downcaseWord(struct editorConfig *ed, struct editorBuffer *bufr,
			int times) {
	wordTransform(ed, bufr, times, transformerDowncase);
}

void capitalCaseWord(struct editorConfig *ed, struct editorBuffer *bufr,
			   int times) {
	wordTransform(ed, bufr, times, transformerCapitalCase);
}

void deleteWord(struct editorBuffer *bufr) {
	int origMarkx = bufr->markx;
	int origMarky = bufr->marky;
	bufferEndOfForwardWord(bufr, &bufr->markx, &bufr->marky);
	killRegion();
	bufr->markx = origMarkx;
	bufr->marky = origMarky;
}

void backspaceWord(struct editorBuffer *bufr) {
	int origMarkx = bufr->markx;
	int origMarky = bufr->marky;
	bufferEndOfBackwardWord(bufr, &bufr->markx, &bufr->marky);
	killRegion();
	bufr->markx = origMarkx;
	bufr->marky = origMarky;
}

void backPara(struct editorBuffer *bufr) {
	bufr->cx = 0;
	int icy = bufr->cy;

	if (icy >= bufr->numrows) {
		icy--;
	}

	if (bufr->numrows == 0) {
		return;
	}

	int pre = 1;

	for (int cy = icy; cy >= 0; cy--) {
		erow *row = &bufr->row[cy];
		if (isParaBoundary(row) && !pre) {
			bufr->cy = cy;
			return;
		} else if (!isParaBoundary(row)) {
			pre = 0;
		}
	}

	bufr->cy = 0;
}

void forwardPara(struct editorBuffer *bufr) {
	bufr->cx = 0;
	int icy = bufr->cy;

	if (icy >= bufr->numrows) {
		return;
	}

	if (bufr->numrows == 0) {
		return;
	}

	int pre = 1;

	for (int cy = icy; cy < bufr->numrows; cy++) {
		erow *row = &bufr->row[cy];
		if (isParaBoundary(row) && !pre) {
			bufr->cy = cy;
			return;
		} else if (!isParaBoundary(row)) {
			pre = 0;
		}
	}

	bufr->cy = bufr->numrows;
}

void pipeCmd(struct editorConfig *ed, struct editorBuffer *bufr) {
	uint8_t *pipeOutput = editorPipe();
	if (pipeOutput != NULL) {
		size_t outputLen = strlen((char *)pipeOutput);
		if (outputLen < sizeof(E.minibuffer) - 1) {
			setStatusMessage("%s", pipeOutput);
		} else {
			struct editorBuffer *newBuf = newBuffer();
			newBuf->filename = stringdup("*Shell Output*");
			newBuf->special_buffer = 1;

			size_t rowStart = 0;
			size_t rowLen = 0;
			for (size_t i = 0; i < outputLen; i++) {
				if (pipeOutput[i] == '\n' ||
				    i == outputLen - 1) {
					insertRow(
						newBuf, newBuf->numrows,
						(char *)&pipeOutput[rowStart],
						rowLen);
					rowStart = i + 1;
					rowLen = 0;
				} else {
					rowLen++;
				}
			}

			if (E.firstBuf == NULL) {
				E.firstBuf = newBuf;
			} else {
				struct editorBuffer *temp = E.firstBuf;
				while (temp->next != NULL) {
					temp = temp->next;
				}
				temp->next = newBuf;
			}
			E.focusBuf = newBuf;

			int idx = windowFocusedIdx(&E);
			E.windows[idx]->buf = E.focusBuf;
			refreshScreen();
		}
		free(pipeOutput);
	}
}

void gotoLine(struct editorBuffer *bufr) {
	uint8_t *nls;
	int nl;

	for (;;) {
		nls = promptUser(bufr, (uint8_t *)"Goto line: %s", PROMPT_BASIC, NULL);
		if (!nls) {
			return;
		}

		nl = atoi((char *)nls);
		free(nls);

		if (nl) {
			bufr->cx = 0;
			if (nl < 0) {
				bufr->cy = 0;
			} else if (nl > bufr->numrows) {
				bufr->cy = bufr->numrows;
			} else {
				bufr->cy = nl;
			}
			return;
		}
	}
}

void transposeWords(struct editorBuffer *bufr) {
	if (bufr->numrows == 0) {
		setStatusMessage("Buffer is empty");
		return;
	}

	if (bufr->cx == 0 && bufr->cy == 0) {
		setStatusMessage("Beginning of buffer");
		return;
	} else if (bufr->cy >= bufr->numrows ||
		   (bufr->cy == bufr->numrows - 1 &&
		    bufr->cx == bufr->row[bufr->cy].size)) {
		setStatusMessage("End of buffer");
		return;
	}

	int startcx, startcy, endcx, endcy;

	startcx = bufr->cx;
	startcy = bufr->cy;
	endcx = bufr->cx;
	endcy = bufr->cy;

	bufferEndOfBackwardWord(bufr, &startcx, &startcy);
	bufferEndOfForwardWord(bufr, &endcx, &endcy);

	if (startcy < 0 || startcy >= bufr->numrows || endcy < 0 ||
	    endcy >= bufr->numrows) {
		setStatusMessage("Invalid buffer position");
		return;
	}

	if ((startcx == bufr->cx && bufr->cy == startcy) ||
	    (endcx == bufr->cx && bufr->cy == endcy)) {
		setStatusMessage("Cannot transpose here");
		return;
	}

	if (startcy == endcy && startcx >= endcx) {
		setStatusMessage("No words to transpose");
		return;
	}

	if (startcy == endcy) {
		struct erow *row = &bufr->row[startcy];
		if (startcx < 0 || startcx > row->size || endcx < 0 ||
		    endcx > row->size) {
			setStatusMessage("Invalid word boundaries");
			return;
		}
	}

	bufr->cx = startcx;
	bufr->cy = startcy;
	bufr->markx = endcx;
	bufr->marky = endcy;

	uint8_t *regionText = NULL;

	if (startcy == endcy) {
		struct erow *row = &bufr->row[startcy];
		int len = endcx - startcx;
		regionText = xmalloc(len + 1);
		if (regionText) {
			memcpy(regionText, &row->chars[startcx], len);
			regionText[len] = '\0';
		}
	} else {
		setStatusMessage(
			"Multi-line word transpose not supported");
		return;
	}

	if (!regionText) {
		setStatusMessage("Failed to extract words");
		return;
	}

	uint8_t *result = transformerTransposeWords(regionText);
	if (!result) {
		free(regionText);
		setStatusMessage("Transpose failed");
		return;
	}

	bufr->cx = startcx;
	bufr->cy = startcy;
	bufr->markx = endcx;
	bufr->marky = endcy;

	killRegion();

	for (int i = 0; result[i] != '\0'; i++) {
		bufferInsertChar(bufr, result[i]);
	}

	free(regionText);
	free(result);
}

void transposeChars(struct editorConfig *ed, struct editorBuffer *bufr) {
	if (bufr->numrows == 0) {
		setStatusMessage("Buffer is empty");
		return;
	}

	if (bufr->cx == 0 && bufr->cy == 0) {
		setStatusMessage("Beginning of buffer");
		return;
	} else if (bufr->cy >= bufr->numrows ||
		   (bufr->cy == bufr->numrows - 1 &&
		    bufr->cx == bufr->row[bufr->cy].size)) {
		setStatusMessage("End of buffer");
		return;
	}

	int startcx, startcy;
	bufferMoveCursor(bufr, ARROW_LEFT);
	startcx = bufr->cx;
	startcy = bufr->cy;
	bufferMoveCursor(bufr, ARROW_RIGHT);
	bufferMoveCursor(bufr, ARROW_RIGHT);
	bufr->markx = startcx;
	bufr->marky = startcy;
	transformRegion(transformerTransposeChars);
}

void switchToNamedBuffer(struct editorConfig *ed,
			       struct editorBuffer *current) {
	char promptMsg[512];
	const char *defaultBufferName = NULL;

	if (ed->lastVisitedBuffer && ed->lastVisitedBuffer != current) {
		defaultBufferName = ed->lastVisitedBuffer->filename ?
					    ed->lastVisitedBuffer->filename :
					    "*scratch*";
	} else {
		struct editorBuffer *defaultBuffer = ed->firstBuf;
		while (defaultBuffer == current && defaultBuffer->next) {
			defaultBuffer = defaultBuffer->next;
		}
		if (defaultBuffer != current) {
			defaultBufferName = defaultBuffer->filename ?
						    defaultBuffer->filename :
						    "*scratch*";
		}
	}

	if (defaultBufferName) {
		snprintf(promptMsg, sizeof(promptMsg),
			 "Switch to buffer (default %s): %%s",
			 defaultBufferName);
	} else {
		snprintf(promptMsg, sizeof(promptMsg), "Switch to buffer: %%s");
	}

	uint8_t *buffer_name =
		promptUser(current, (uint8_t *)promptMsg, PROMPT_BASIC, NULL);

	if (buffer_name == NULL) {
		setStatusMessage("Buffer switch canceled");
		return;
	}

	struct editorBuffer *targetBuffer = NULL;

	if (buffer_name[0] == '\0') {
		if (defaultBufferName) {
			for (struct editorBuffer *buf = ed->firstBuf;
			     buf != NULL; buf = buf->next) {
				if (buf == current)
					continue;
				if ((buf->filename &&
				     strcmp(buf->filename, defaultBufferName) ==
					     0) ||
				    (!buf->filename &&
				     strcmp("*scratch*", defaultBufferName) ==
					     0)) {
					targetBuffer = buf;
					break;
				}
			}
		}
		if (!targetBuffer) {
			setStatusMessage("No buffer to switch to");
			free(buffer_name);
			return;
		}
	} else {
		for (struct editorBuffer *buf = ed->firstBuf; buf != NULL;
		     buf = buf->next) {
			if (buf == current)
				continue;

			const char *bufName = buf->filename ? buf->filename :
							      "*scratch*";
			if (strcmp((char *)buffer_name, bufName) == 0) {
				targetBuffer = buf;
				break;
			}
		}

		if (!targetBuffer) {
			setStatusMessage("No buffer named '%s'",
					       buffer_name);
			free(buffer_name);
			return;
		}
	}

	if (targetBuffer) {
		ed->lastVisitedBuffer = current;
		ed->focusBuf = targetBuffer;

		const char *switchedBufferName =
			ed->focusBuf->filename ? ed->focusBuf->filename :
						 "*scratch*";
		setStatusMessage("Switched to buffer %s",
				       switchedBufferName);

		for (int i = 0; i < ed->nwindows; i++) {
			if (ed->windows[i]->focused) {
				ed->windows[i]->buf = ed->focusBuf;
			}
		}
	}

	free(buffer_name);
}

/* Where the magic happens */
void processKeypress(int c) {
	/* Initialize keybindings on first use */
	static int initialized = 0;
	if (!initialized) {
		initKeyBindings();
		initialized = 1;
	}

	/* Process the key using the new system */
	processKeySequence(c);
}
/*** init ***/

void crashHandler(int sig) {
	/* Use only async-signal-safe functions */
	write(STDOUT_FILENO, CSI "?1049l", 8);
	write(STDERR_FILENO, "\nProgram terminated by signal\n", 29);
	_exit(sig);
}

void editorResume(int sig) {
	/* Simple resume handler - just refresh the screen */
	refreshScreen();
}

void editorSuspend(int sig) {
	/* Restore terminal and suspend */
	disableRawMode();
	signal(SIGTSTP, SIG_DFL);
	raise(SIGTSTP);
}

void setupHandlers() {
	signal(SIGWINCH, editorResizeScreen);
	signal(SIGCONT, editorResume);
	signal(SIGTSTP, editorSuspend);

	/* Graceful crash handling */
	signal(SIGINT, crashHandler);
	signal(SIGTERM, crashHandler);
	signal(SIGSEGV, crashHandler);
	signal(SIGABRT, crashHandler);
	signal(SIGQUIT, crashHandler);
}

struct editorBuffer *newBuffer() {
	struct editorBuffer *ret = xmalloc(sizeof(struct editorBuffer));
	ret->indent = 0;
	ret->markx = -1;
	ret->marky = -1;
	ret->cx = 0;
	ret->cy = 0;
	ret->numrows = 0;
	ret->row = NULL;
	ret->filename = NULL;
	ret->query = NULL;
	ret->dirty = 0;
	ret->special_buffer = 0;
	ret->undo = NULL;
	ret->redo = NULL;
	ret->next = NULL;
	ret->uarg = 0;
	ret->uarg_active = 0;
	ret->truncate_lines = 0;
	ret->rectangle_mode = 0;
	ret->screen_line_start = NULL;
	ret->screen_line_cache_size = 0;
	ret->screen_line_cache_valid = 0;
	return ret;
}

void invalidateScreenCache(struct editorBuffer *buf) {
	buf->screen_line_cache_valid = 0;
}

void buildScreenCache(struct editorBuffer *buf) {
	if (buf->screen_line_cache_valid)
		return;

	if (buf->screen_line_cache_size < buf->numrows) {
		buf->screen_line_cache_size = buf->numrows + 100;
		buf->screen_line_start =
			realloc(buf->screen_line_start,
				buf->screen_line_cache_size * sizeof(int));
	}

	if (!buf->screen_line_start)
		return;

	int screen_line = 0;
	for (int i = 0; i < buf->numrows; i++) {
		buf->screen_line_start[i] = screen_line;
		if (buf->truncate_lines) {
			screen_line += 1;
		} else {
			int width = calculateLineWidth(&buf->row[i]);
			int lines_used = (width / E.screencols) + 1;
			screen_line += lines_used;
		}
	}

	buf->screen_line_cache_valid = 1;
}

int getScreenLineForRow(struct editorBuffer *buf, int row) {
	if (!buf->screen_line_cache_valid) {
		buildScreenCache(buf);
	}
	if (row >= buf->numrows || row < 0)
		return 0;
	return buf->screen_line_start[row];
}

void destroyBuffer(struct editorBuffer *buf) {
	clearUndosAndRedos(buf);
	free(buf->filename);
	free(buf->screen_line_start);
	free(buf);
}

void initEditor() {
	E.minibuffer[0] = 0;
	E.prefix_display[0] = '\0';
	E.describe_key_mode = 0;
	E.kill = NULL;
	E.rectKill = NULL;
	E.windows = xmalloc(sizeof(struct editorWindow *) * 1);
	E.windows[0] = xmalloc(sizeof(struct editorWindow));
	E.windows[0]->focused = 1;
	E.windows[0]->cx = 0;
	E.windows[0]->cy = 0;
	E.windows[0]->scx = 0;
	E.windows[0]->scy = 0;
	E.windows[0]->rowoff = 0;
	E.windows[0]->coloff = 0;
	E.nwindows = 1;
	E.recording = 0;
	E.macro.nkeys = 0;
	E.macro.keys = NULL;
	E.micro = 0;
	E.playback = 0;
	E.firstBuf = NULL;
	memset(E.registers, 0, sizeof(E.registers));
	setupCommands(&E);
	E.lastVisitedBuffer = NULL;

	if (getWindowSize(&E.screenrows, &E.screencols) == -1)
		die("getWindowSize");
}

void execMacro(struct editorMacro *macro) {
	struct editorMacro tmp;
	tmp.keys = NULL;
	if (macro != &E.macro) {
		/* HACK: Annoyance here with readkey needs us to futz
		 * around with E.macro */
		memcpy(&tmp, &E.macro, sizeof(struct editorMacro));
		memcpy(&E.macro, macro, sizeof(struct editorMacro));
	}
	E.playback = 0;
	while (E.playback < E.macro.nkeys) {
		/* HACK: increment here, so that
		 * readkey sees playback != 0 */
		int key = E.macro.keys[E.playback++];
		if (key == UNICODE) {
			insertUnicode(E.focusBuf);
		}
		processKeypress(key);
	}
	E.playback = 0;
	if (tmp.keys != NULL) {
		memcpy(&E.macro, &tmp, sizeof(struct editorMacro));
	}
}

int main(int argc, char *argv[]) {
	enableRawMode();
	initEditor();
	E.firstBuf = newBuffer();
	E.focusBuf = E.firstBuf;
	if (argc >= 2) {
		int i = 1;
		int linum = -1;
		if (argv[1][0] == '+' && argc > 2) {
			linum = atoi(argv[1] + 1);
			i++;
		}
		for (; i < argc; i++) {
			E.firstBuf = newBuffer();
			editorOpenFile(E.firstBuf, argv[i]);
			E.firstBuf->next = E.focusBuf;
			if (linum > 0) {
				E.firstBuf->cy = linum - 1;
				linum = -1;
				if (E.firstBuf->cy > E.firstBuf->numrows) {
					E.firstBuf->cy = E.firstBuf->numrows;
				}
			}
			E.focusBuf = E.firstBuf;
		}
	}
	E.windows[0]->buf = E.focusBuf;

	setStatusMessage("emsys " EMSYS_VERSION " - C-x C-c to quit");
	setupHandlers();
	refreshScreen();

	for (;;) {
		int c = readKey();
		if (c == MACRO_RECORD) {
			if (E.recording) {
				setStatusMessage(
					"Already defining keyboard macro");
			} else {
				setStatusMessage(
					"Defining keyboard macro...");
				E.recording = 1;
				E.macro.nkeys = 0;
				E.macro.skeys = 0x10;
				free(E.macro.keys);
				E.macro.keys =
					xmalloc(E.macro.skeys * sizeof(int));
			}
		} else if (c == MACRO_END) {
			if (E.recording) {
				setStatusMessage(
					"Keyboard macro defined");
				E.recording = 0;
			} else {
				setStatusMessage(
					"Not defining keyboard macro");
			}
		} else if (c == MACRO_EXEC ||
			   (E.micro == MACRO_EXEC && (c == 'e' || c == 'E'))) {
			if (E.recording) {
				setStatusMessage(
					"Keyboard macro defined");
				E.recording = 0;
			}
			execMacro(&E.macro);
			E.micro = MACRO_EXEC;
		} else {
			recordKey(c);
			processKeypress(c);
		}
		refreshScreen();
	}
	return 0;
}
