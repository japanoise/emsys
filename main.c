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
	char *new_str = malloc(len);
	if (new_str == NULL) {
		return NULL; // malloc failed
	}
	return memcpy(new_str, s, len);
}

struct editorConfig E;
void editorMoveCursor(struct editorBuffer *bufr, int key);
void setupHandlers();

void die(const char *s) {
	write(STDOUT_FILENO, CSI "2J", 4);
	write(STDOUT_FILENO, CSI "H", 3);
	perror(s);
	write(STDOUT_FILENO, CRLF, 2);
	write(STDOUT_FILENO, "sleeping 5s", 11);
	sleep(5);
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

void editorSwitchWindow(struct editorConfig *ed) {
	if (ed->nwindows == 1) {
		editorSetStatusMessage("No other windows to select");
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

void editorInsertChar(struct editorBuffer *bufr, int c) {
	if (bufr->cy == bufr->numrows) {
		editorInsertRow(bufr, bufr->numrows, "", 0);
	}
	editorRowInsertChar(bufr, &bufr->row[bufr->cy], bufr->cx, c);
	bufr->cx++;
}

void editorInsertUnicode(struct editorBuffer *bufr) {
	if (bufr->cy == bufr->numrows) {
		editorInsertRow(bufr, bufr->numrows, "", 0);
	}
	editorRowInsertUnicode(&E, bufr, &bufr->row[bufr->cy], bufr->cx);
	bufr->cx += E.nunicode;
}

void editorInsertNewline(struct editorBuffer *bufr) {
	if (bufr->cx == 0) {
		editorInsertRow(bufr, bufr->cy, "", 0);
	} else {
		erow *row = &bufr->row[bufr->cy];
		editorInsertRow(bufr, bufr->cy + 1, &row->chars[bufr->cx],
				row->size - bufr->cx);
		row = &bufr->row[bufr->cy];
		row->size = bufr->cx;
		row->chars[row->size] = '\0';
	}
	bufr->cy++;
	bufr->cx = 0;
}

void editorOpenLine(struct editorBuffer *bufr) {
	int ccx = bufr->cx;
	int ccy = bufr->cy;
	editorInsertNewline(bufr);
	bufr->cx = ccx;
	bufr->cy = ccy;
}

void editorInsertNewlineAndIndent(struct editorBuffer *bufr) {
	editorUndoAppendChar(bufr, '\n');
	editorInsertNewline(bufr);
	int i = 0;
	uint8_t c = bufr->row[bufr->cy - 1].chars[i];
	while (c == ' ' || c == CTRL('i')) {
		editorUndoAppendChar(bufr, c);
		editorInsertChar(bufr, c);
		c = bufr->row[bufr->cy - 1].chars[++i];
	}
}

void editorIndent(struct editorBuffer *bufr, int rept) {
	int ocx = bufr->cx;
	int indWidth = 1;
	if (bufr->indent) {
		indWidth = bufr->indent;
	}
	bufr->cx = 0;
	for (int i = 0; i < rept; i++) {
		if (bufr->indent) {
			for (int i = 0; i < bufr->indent; i++) {
				editorUndoAppendChar(bufr, ' ');
				editorInsertChar(bufr, ' ');
			}
		} else {
			editorUndoAppendChar(bufr, '\t');
			editorInsertChar(bufr, '\t');
		}
	}
	bufr->cx = ocx + indWidth * rept;
}

void editorUnindent(struct editorBuffer *bufr, int rept) {
	if (bufr->cy >= bufr->numrows) {
		editorSetStatusMessage("End of buffer.");
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
		new->data = realloc(new->data, new->datasize);
	}
	memset(new->data, indCh, trunc);
	new->data[trunc] = 0;
	new->datalen = trunc;

	/* Perform row operation & dirty buffer */
	memmove(&row->chars[0], &row->chars[trunc], row->size - trunc);
	row->size -= trunc;
	bufr->cx -= trunc;
	bufr->dirty = 1;
}

void editorDelChar(struct editorBuffer *bufr) {
	if (bufr->cy == bufr->numrows)
		return;
	if (bufr->cy == bufr->numrows - 1 &&
	    bufr->cx == bufr->row[bufr->cy].size)
		return;

	erow *row = &bufr->row[bufr->cy];
	editorUndoDelChar(bufr, row);
	if (bufr->cx == row->size) {
		row = &bufr->row[bufr->cy + 1];
		editorRowAppendString(bufr, &bufr->row[bufr->cy], row->chars,
				      row->size);
		editorDelRow(bufr, bufr->cy + 1);
	} else {
		editorRowDelChar(bufr, row, bufr->cx);
	}
}

void editorBackSpace(struct editorBuffer *bufr) {
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
			editorUndoBackSpace(bufr, row->chars[bufr->cx]);
		} while (utf8_isCont(row->chars[bufr->cx]));
		editorRowDelChar(bufr, row, bufr->cx);
	} else {
		editorUndoBackSpace(bufr, '\n');
		bufr->cx = bufr->row[bufr->cy - 1].size;
		editorRowAppendString(bufr, &bufr->row[bufr->cy - 1],
				      row->chars, row->size);
		editorDelRow(bufr, bufr->cy);
		bufr->cy--;
	}
}

void editorKillLine(struct editorBuffer *buf) {
	if (buf->numrows <= 0) {
		return;
	}

	erow *row = &buf->row[buf->cy];

	if (buf->cx == row->size) {
		editorDelChar(buf);
	} else {
		int kill_len = row->size - buf->cx;
		free(E.kill);
		E.kill = malloc(kill_len + 1);
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
			new->data = realloc(new->data, new->datasize);
		}
		for (int i = 0; i < kill_len; i++) {
			new->data[i] = E.kill[kill_len - i - 1];
		}
		new->data[kill_len] = '\0';

		row->size = buf->cx;
		row->chars[row->size] = '\0';
		buf->dirty = 1;
		editorClearMark(buf);
	}
}

void editorKillLineBackwards(struct editorBuffer *buf) {
	if (buf->cx == 0) {
		return;
	}

	erow *row = &buf->row[buf->cy];

	free(E.kill);
	E.kill = malloc(buf->cx + 1);
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
		new->data = realloc(new->data, new->datasize);
	}
	for (int i = 0; i < buf->cx; i++) {
		new->data[i] = E.kill[buf->cx - i - 1];
	}
	new->data[buf->cx] = '\0';

	row->size -= buf->cx;
	memmove(row->chars, &row->chars[buf->cx], row->size);
	row->chars[row->size] = '\0';
	buf->cx = 0;
	buf->dirty = 1;
}

void editorRecordKey(int c) {
	if (E.recording) {
		E.macro.keys[E.macro.nkeys++] = c;
		if (E.macro.nkeys >= E.macro.skeys) {
			E.macro.skeys *= 2;
			E.macro.keys = realloc(E.macro.keys,
					       E.macro.skeys * sizeof(int));
		}
		if (c == UNICODE) {
			for (int i = 0; i < E.nunicode; i++) {
				E.macro.keys[E.macro.nkeys++] = E.unicode[i];
				if (E.macro.nkeys >= E.macro.skeys) {
					E.macro.skeys *= 2;
					E.macro.keys = realloc(
						E.macro.keys,
						E.macro.skeys * sizeof(int));
				}
			}
		}
	}
}

/*** file i/o ***/

char *editorRowsToString(struct editorBuffer *bufr, int *buflen) {
	int totlen = 0;
	int j;
	for (j = 0; j < bufr->numrows; j++) {
		totlen += bufr->row[j].size + 1;
	}
	*buflen = totlen;

	char *buf = malloc(totlen);
	char *p = buf;
	for (j = 0; j < bufr->numrows; j++) {
		memcpy(p, bufr->row[j].chars, bufr->row[j].size);
		p += bufr->row[j].size;
		*p = '\n';
		p++;
	}

	return buf;
}

void editorOpen(struct editorBuffer *bufr, char *filename) {
	free(bufr->filename);
	bufr->filename = stringdup(filename);
	FILE *fp = fopen(filename, "r");
	if (!fp) {
		if (errno == ENOENT) {
			editorSetStatusMessage("(New file)", bufr->filename);
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
		editorInsertRow(bufr, bufr->numrows, line, linelen);
	}
	free(line);
	fclose(fp);
	bufr->dirty = 0;
}

void editorSave(struct editorBuffer *bufr) {
	if (bufr->filename == NULL) {
		bufr->filename =
			editorPrompt(bufr, "Save as: %s", PROMPT_FILES, NULL);
		if (bufr->filename == NULL) {
			editorSetStatusMessage("Save aborted.");
			return;
		}
	}

	int len;
	char *buf = editorRowsToString(bufr, &len);

	int fd = open(bufr->filename, O_RDWR | O_CREAT, 0644);
	if (fd != -1) {
		if (ftruncate(fd, len) != -1) {
			if (write(fd, buf, len)) {
				close(fd);
				free(buf);
				bufr->dirty = 0;
				editorSetStatusMessage("Wrote %d bytes to %s",
						       len, bufr->filename);
				return;
			}
		}
		close(fd);
	}

	free(buf);
	editorSetStatusMessage("Save failed: %s", strerror(errno));
}

void editorSetStatusMessage(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(E.minibuffer, sizeof(E.minibuffer), fmt, ap);
	va_end(ap);
	E.statusmsg_time = time(NULL);
}

void editorRecenterCommand(struct editorConfig *ed, struct editorBuffer *buf) {
	int winIdx = windowFocusedIdx(ed);
	editorRecenter(ed->windows[winIdx]);
}

void editorSuspend(int UNUSED(sig)) {
	signal(SIGTSTP, SIG_DFL);
	disableRawMode();
	raise(SIGTSTP);
}

void editorResume(int sig) {
	setupHandlers();
	enableRawMode();
	editorResizeScreen(sig);
}

/*** input ***/

uint8_t *editorPrompt(struct editorBuffer *bufr, uint8_t *prompt,
		      enum promptType t,
		      void (*callback)(struct editorBuffer *, uint8_t *, int)) {
	size_t bufsize = 128;
	uint8_t *buf = malloc(bufsize);

	int promptlen = stringWidth(prompt) - 2;

	size_t buflen = 0;
	size_t bufwidth = 0;
	size_t curs = 0;
	size_t cursScr = 0;
	buf[0] = 0;

	for (;;) {
		editorSetStatusMessage(prompt, buf);
		editorRefreshScreen();
#ifdef EMSYS_DEBUG_PROMPT
		char dbg[32];
		snprintf(dbg, sizeof(dbg), CSI "%d;%dHc: %ld cs: %ld", 0, 0,
			 curs, cursScr);
		write(STDOUT_FILENO, dbg, strlen(dbg));
#endif
		editorCursorBottomLineLong(promptlen + cursScr + 1);

		int c = editorReadKey();
		editorRecordKey(c);
		switch (c) {
		case '\r':
			editorSetStatusMessage("");
			if (callback)
				callback(bufr, buf, c);
			return buf;
		case CTRL('g'):
		case CTRL('c'):
			editorSetStatusMessage("");
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
				buf = realloc(buf, bufsize);
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
					buf = realloc(buf, bufsize);
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

void editorMoveCursor(struct editorBuffer *bufr, int key) {
	erow *row = (bufr->cy >= bufr->numrows) ? NULL : &bufr->row[bufr->cy];

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
			if (bufr->cy < bufr->numrows) {
				if (bufr->row[bufr->cy].chars == NULL)
					break;
				while (bufr->cx < bufr->row[bufr->cy].size &&
				       utf8_isCont(bufr->row[bufr->cy]
							   .chars[bufr->cx]))
					bufr->cx++;
			} else {
				bufr->cx = 0;
			}
		}
		break;
	}
	row = (bufr->cy >= bufr->numrows) ? NULL : &bufr->row[bufr->cy];
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

void editorForwardWord(struct editorBuffer *bufr) {
	bufferEndOfForwardWord(bufr, &bufr->cx, &bufr->cy);
}

void editorBackWord(struct editorBuffer *bufr) {
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
	editorTransformRegion(ed, bufr, transformer);
}

void editorUpcaseWord(struct editorConfig *ed, struct editorBuffer *bufr,
		      int times) {
	wordTransform(ed, bufr, times, transformerUpcase);
}

void editorDowncaseWord(struct editorConfig *ed, struct editorBuffer *bufr,
			int times) {
	wordTransform(ed, bufr, times, transformerDowncase);
}

void editorCapitalCaseWord(struct editorConfig *ed, struct editorBuffer *bufr,
			   int times) {
	wordTransform(ed, bufr, times, transformerCapitalCase);
}

void editorDeleteWord(struct editorBuffer *bufr) {
	int origMarkx = bufr->markx;
	int origMarky = bufr->marky;
	bufferEndOfForwardWord(bufr, &bufr->markx, &bufr->marky);
	editorKillRegion(&E, bufr);
	bufr->markx = origMarkx;
	bufr->marky = origMarky;
}

void editorBackspaceWord(struct editorBuffer *bufr) {
	int origMarkx = bufr->markx;
	int origMarky = bufr->marky;
	bufferEndOfBackwardWord(bufr, &bufr->markx, &bufr->marky);
	editorKillRegion(&E, bufr);
	bufr->markx = origMarkx;
	bufr->marky = origMarky;
}

void editorBackPara(struct editorBuffer *bufr) {
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

void editorForwardPara(struct editorBuffer *bufr) {
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

void editorPipeCmd(struct editorConfig *ed, struct editorBuffer *bufr) {
	uint8_t *pipeOutput = editorPipe(ed, bufr);
	if (pipeOutput != NULL) {
		size_t outputLen = strlen((char *)pipeOutput);
		if (outputLen < sizeof(E.minibuffer) - 1) {
			editorSetStatusMessage("%s", pipeOutput);
		} else {
			struct editorBuffer *newBuf = newBuffer();
			newBuf->filename = stringdup("*Shell Output*");
			newBuf->special_buffer = 1;

			size_t rowStart = 0;
			size_t rowLen = 0;
			for (size_t i = 0; i < outputLen; i++) {
				if (pipeOutput[i] == '\n' ||
				    i == outputLen - 1) {
					editorInsertRow(
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
			editorRefreshScreen();
		}
		free(pipeOutput);
	}
}

void editorGotoLine(struct editorBuffer *bufr) {
	uint8_t *nls;
	int nl;

	for (;;) {
		nls = editorPrompt(bufr, "Goto line: %s", PROMPT_BASIC, NULL);
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

void editorTransposeWords(struct editorBuffer *bufr) {
	struct editorConfig *ed = &E;
	if (bufr->numrows == 0) {
		editorSetStatusMessage("Buffer is empty");
		return;
	}

	if (bufr->cx == 0 && bufr->cy == 0) {
		editorSetStatusMessage("Beginning of buffer");
		return;
	} else if (bufr->cy >= bufr->numrows ||
		   (bufr->cy == bufr->numrows - 1 &&
		    bufr->cx == bufr->row[bufr->cy].size)) {
		editorSetStatusMessage("End of buffer");
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
		editorSetStatusMessage("Invalid buffer position");
		return;
	}

	if ((startcx == bufr->cx && bufr->cy == startcy) ||
	    (endcx == bufr->cx && bufr->cy == endcy)) {
		editorSetStatusMessage("Cannot transpose here");
		return;
	}

	if (startcy == endcy && startcx >= endcx) {
		editorSetStatusMessage("No words to transpose");
		return;
	}

	if (startcy == endcy) {
		struct erow *row = &bufr->row[startcy];
		if (startcx < 0 || startcx > row->size || endcx < 0 ||
		    endcx > row->size) {
			editorSetStatusMessage("Invalid word boundaries");
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
		regionText = malloc(len + 1);
		if (regionText) {
			memcpy(regionText, &row->chars[startcx], len);
			regionText[len] = '\0';
		}
	} else {
		editorSetStatusMessage(
			"Multi-line word transpose not supported");
		return;
	}

	if (!regionText) {
		editorSetStatusMessage("Failed to extract words");
		return;
	}

	uint8_t *result = transformerTransposeWords(regionText);
	if (!result) {
		free(regionText);
		editorSetStatusMessage("Transpose failed");
		return;
	}

	bufr->cx = startcx;
	bufr->cy = startcy;
	bufr->markx = endcx;
	bufr->marky = endcy;

	editorKillRegion(ed, bufr);

	for (int i = 0; result[i] != '\0'; i++) {
		editorInsertChar(bufr, result[i]);
	}

	free(regionText);
	free(result);
}

void editorTransposeChars(struct editorConfig *ed, struct editorBuffer *bufr) {
	if (bufr->numrows == 0) {
		editorSetStatusMessage("Buffer is empty");
		return;
	}

	if (bufr->cx == 0 && bufr->cy == 0) {
		editorSetStatusMessage("Beginning of buffer");
		return;
	} else if (bufr->cy >= bufr->numrows ||
		   (bufr->cy == bufr->numrows - 1 &&
		    bufr->cx == bufr->row[bufr->cy].size)) {
		editorSetStatusMessage("End of buffer");
		return;
	}

	int startcx, startcy;
	editorMoveCursor(bufr, ARROW_LEFT);
	startcx = bufr->cx;
	startcy = bufr->cy;
	editorMoveCursor(bufr, ARROW_RIGHT);
	editorMoveCursor(bufr, ARROW_RIGHT);
	bufr->markx = startcx;
	bufr->marky = startcy;
	editorTransformRegion(ed, bufr, transformerTransposeChars);
}

void editorSwitchToNamedBuffer(struct editorConfig *ed,
			       struct editorBuffer *current) {
	char prompt[512];
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
		snprintf(prompt, sizeof(prompt),
			 "Switch to buffer (default %s): %%s",
			 defaultBufferName);
	} else {
		snprintf(prompt, sizeof(prompt), "Switch to buffer: %%s");
	}

	uint8_t *buffer_name =
		editorPrompt(current, (uint8_t *)prompt, PROMPT_BASIC, NULL);

	if (buffer_name == NULL) {
		editorSetStatusMessage("Buffer switch canceled");
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
			editorSetStatusMessage("No buffer to switch to");
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
			editorSetStatusMessage("No buffer named '%s'",
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
		editorSetStatusMessage("Switched to buffer %s",
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
void editorProcessKeypress(int c) {
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
	/* Safely restore terminal without calling die() */
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios);
	write(STDOUT_FILENO, CSI "?1049l", 8);
	fprintf(stderr, "\nProgram terminated by signal %d\n", sig);
	exit(sig);
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
	struct editorBuffer *ret = malloc(sizeof(struct editorBuffer));
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
	E.kill = NULL;
	E.rectKill = NULL;
	E.windows = malloc(sizeof(struct editorWindow *) * 1);
	E.windows[0] = malloc(sizeof(struct editorWindow));
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

void editorExecMacro(struct editorMacro *macro) {
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
			editorDeserializeUnicode();
		}
		editorProcessKeypress(key);
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
			editorOpen(E.firstBuf, argv[i]);
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

	editorSetStatusMessage("emsys " EMSYS_VERSION " - C-x C-c to quit");
	setupHandlers();

	for (;;) {
		editorRefreshScreen();
		int c = editorReadKey();
		if (c == MACRO_RECORD) {
			if (E.recording) {
				editorSetStatusMessage(
					"Already defining keyboard macro");
			} else {
				editorSetStatusMessage(
					"Defining keyboard macro...");
				E.recording = 1;
				E.macro.nkeys = 0;
				E.macro.skeys = 0x10;
				free(E.macro.keys);
				E.macro.keys =
					malloc(E.macro.skeys * sizeof(int));
			}
		} else if (c == MACRO_END) {
			if (E.recording) {
				editorSetStatusMessage(
					"Keyboard macro defined");
				E.recording = 0;
			} else {
				editorSetStatusMessage(
					"Not defining keyboard macro");
			}
		} else if (c == MACRO_EXEC ||
			   (E.micro == MACRO_EXEC && (c == 'e' || c == 'E'))) {
			if (E.recording) {
				editorSetStatusMessage(
					"Keyboard macro defined");
				E.recording = 0;
			}
			editorExecMacro(&E.macro);
			E.micro = MACRO_EXEC;
		} else {
			editorRecordKey(c);
			editorProcessKeypress(c);
		}
	}
	return 0;
}
