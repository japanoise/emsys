#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

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

void editorUpdateBuffer(struct editorBuffer *buf) {
	for (int i = 0; i < buf->numrows; i++) {
		editorUpdateRow(&buf->row[i]);
	}
}

int windowFocusedIdx(struct editorConfig *ed) {
	for (int i = 0; i < E.nwindows; i++) {
		if (ed->windows[i]->_focused) {
			return i;
		}
	}
	/* You're in trouble m80 */
	return 0;
}

void setWindowFocus(struct editorConfig *config, struct editorWindow *window) {
	if (config->focusWin) {
		config->focusWin->_focused = 0;
	}
	config->focusWin = window;
	window->_focused = 1;
	config->focusBuf = window->buf;
}

void editorSwitchWindow(struct editorConfig *ed) {
	if (ed->nwindows == 1) {
		editorSetStatusMessage("No other windows to select");
		return;
	}

	// Find the index of the next window
	int currentIdx = -1;
	for (int i = 0; i < ed->nwindows; i++) {
		if (ed->windows[i] == ed->focusWin) {
			currentIdx = i;
			break;
		}
	}

	int nextIdx = (currentIdx + 1) % ed->nwindows;

	// Set focus to the next window
	setWindowFocus(ed, ed->windows[nextIdx]);
}

/*** terminal ***/

void disableRawMode() {
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
		die("disableRawMode tcsetattr");
	if (write(STDOUT_FILENO, CSI "?1049l", 8) == -1)
		die("disableRawMode write");
}

void enableRawMode() {
	/* Saves the screen and switches to an alt screen */
	if (write(STDOUT_FILENO, CSI "?1049h", 8) == -1)
		die("enableRawMode write");
	/*
	 * I looked into it. It's possible, but not easy, to do it
	 * without termios. Basically you'd have to hand-hack and send
	 * off your own bits. Check out busybox vi and that rabbithole
	 * for an implementation.
	 */
	if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
		die("tcgetattr");
	atexit(disableRawMode);

	struct termios raw = E.orig_termios;
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
		die("enableRawMode tcsetattr");
}

void editorDeserializeUnicode() {
	E.unicode[0] = E.macro.keys[E.playback++];
	E.nunicode = utf8_nBytes(E.unicode[0]);
	for (int i = 1; i < E.nunicode; i++) {
		E.unicode[i] = E.macro.keys[E.playback++];
	}
}

/* Raw reading a keypress */
int editorReadKey() {
	if (E.playback) {
		int ret = E.macro.keys[E.playback++];
		if (ret == UNICODE) {
			editorDeserializeUnicode();
		}
		return ret;
	}
	int nread;
	uint8_t c;
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if (nread == -1 && errno != EAGAIN)
			die("read");
	}
#ifdef EMSYS_CU_UARG
	if (c == CTRL('u')) {
		return UNIVERSAL_ARGUMENT;
	}
#endif //EMSYS_CU_UARG
	if (c == 033) {
		char seq[5] = { 0, 0, 0, 0, 0 };
		if (read(STDIN_FILENO, &seq[0], 1) != 1)
			goto ESC_UNKNOWN;

		if (seq[0] == '[') {
			if (read(STDIN_FILENO, &seq[1], 1) != 1)
				goto ESC_UNKNOWN;
			if (seq[1] >= '0' && seq[1] <= '9') {
				if (read(STDIN_FILENO, &seq[2], 1) != 1)
					goto ESC_UNKNOWN;
				if (seq[2] == '~') {
					switch (seq[1]) {
					case '1':
						return HOME_KEY;
					case '3':
						return DEL_KEY;
					case '4':
						return END_KEY;
					case '5':
						return PAGE_UP;
					case '6':
						return PAGE_DOWN;
					case '7':
						return HOME_KEY;
					case '8':
						return END_KEY;
					}
				} else if (seq[2] == '4') {
					if (read(STDIN_FILENO, &seq[3], 1) != 1)
						goto ESC_UNKNOWN;
					if (seq[3] == '~') {
						errno = EINTR;
						die("Panic key");
					}
				}
			} else {
				switch (seq[1]) {
				case 'A':
					return ARROW_UP;
				case 'B':
					return ARROW_DOWN;
				case 'C':
					return ARROW_RIGHT;
				case 'D':
					return ARROW_LEFT;
				case 'F':
					return END_KEY;
				case 'H':
					return HOME_KEY;
				case 'Z':
					return BACKTAB;
				}
			}
		} else if ('0' <= seq[0] && seq[0] <= '9') {
			return ALT_0 + (seq[0] - '0');
		} else if (seq[0] == '<') {
			return BEG_OF_FILE;
		} else if (seq[0] == '>') {
			return END_OF_FILE;
		} else if (seq[0] == '|') {
			return PIPE_CMD;
		} else if (seq[0] == '%') {
			return QUERY_REPLACE;
		} else if (seq[0] == '/') {
			return EXPAND;
		} else if (seq[0] == 127) {
			return BACKSPACE_WORD;
		} else {
			switch ((seq[0] & 0x1f) | 0x40) {
			case 'B':
				return BACKWARD_WORD;
			case 'C':
				return CAPCASE_WORD;
			case 'D':
				return DELETE_WORD;
			case 'F':
				return FORWARD_WORD;
			case 'G':
				return GOTO_LINE;
			case 'H':
				return BACKSPACE_WORD;
			case 'L':
				return DOWNCASE_WORD;
			case 'N':
				return FORWARD_PARA;
			case 'P':
				return BACKWARD_PARA;
			case 'T':
				return TRANSPOSE_WORDS;
			case 'U':
				return UPCASE_WORD;
			case 'V':
				return PAGE_UP;
			case 'W':
				return COPY;
			case 'X':
				return EXEC_CMD;
			}
		}

ESC_UNKNOWN:;
		char seqR[32];
		seqR[0] = 0;
		char buf[8];
		for (int i = 0; seq[i]; i++) {
			if (seq[i] < ' ') {
				sprintf(buf, "C-%c ", seq[i] + '`');
			} else {
				sprintf(buf, "%c ", seq[i]);
			}
			strcat(seqR, buf);
		}
		editorSetStatusMessage("Unknown command M-%s", seqR);
		return 033;
	} else if (c == CTRL('x')) {
		/* Welcome to Emacs! */
#ifdef EMSYS_CUA
		// CUA mode: if the region is marked, C-x means 'cut' region.
		// Otherwise, proceed.
		if (E.focusBuf->markx != -1 && E.focusBuf->marky != -1) {
			return CUT;
		}
#endif //EMSYS_CUA
		char seq[5] = { 0, 0, 0, 0, 0 };
		if (read(STDIN_FILENO, &seq[0], 1) != 1)
			goto CX_UNKNOWN;
		if (seq[0] == CTRL('c')) {
			return QUIT;
		} else if (seq[0] == CTRL('s')) {
			return SAVE;
		} else if (seq[0] == CTRL('f')) {
			return FIND_FILE;
		} else if (seq[0] == CTRL('_')) {
			return REDO;
		} else if (seq[0] == CTRL('x')) {
			return SWAP_MARK;
		} else if (seq[0] == 'b' || seq[0] == 'B' ||
			   seq[0] == CTRL('b')) {
			return SWITCH_BUFFER;
		} else if (seq[0] == '\x1b') {
			// C-x left and C-x right
			if (read(STDIN_FILENO, &seq[1], 1) != 1)
				goto CX_UNKNOWN;
			if (read(STDIN_FILENO, &seq[2], 1) != 1)
				goto CX_UNKNOWN;
			if (seq[1] == '[') {
				switch (seq[2]) {
				case 'C':
					return NEXT_BUFFER;
				case 'D':
					return PREVIOUS_BUFFER;
				}
			} else if (seq[1] ==
				   'O') { // Check for C-x C-right/left
				switch (seq[2]) {
				case 'C':
					return NEXT_BUFFER; // C-x C-right
				case 'D':
					return PREVIOUS_BUFFER; // C-x C-left
				}
			}
		} else if (seq[0] == 'h') {
			return MARK_BUFFER;
		} else if (seq[0] == 'o' || seq[0] == 'O') {
			return OTHER_WINDOW;
		} else if (seq[0] == '2') {
			return CREATE_WINDOW;
		} else if (seq[0] == '0') {
			return DESTROY_WINDOW;
		} else if (seq[0] == '1') {
			return DESTROY_OTHER_WINDOWS;
		} else if (seq[0] == 'k') {
			return KILL_BUFFER;
		} else if (seq[0] == '(') {
			return MACRO_RECORD;
		} else if (seq[0] == 'e' || seq[0] == 'E') {
			return MACRO_EXEC;
		} else if (seq[0] == ')') {
			return MACRO_END;
		} else if (seq[0] == 'z' || seq[0] == 'Z' ||
			   seq[0] == CTRL('z')) {
			return SUSPEND;
		} else if (seq[0] == 'u' || seq[0] == 'U' ||
			   seq[0] == CTRL('u')) {
			return UPCASE_REGION;
		} else if (seq[0] == 'l' || seq[0] == 'L' ||
			   seq[0] == CTRL('l')) {
			return DOWNCASE_REGION;
		} else if (seq[0] == 'r' || seq[0] == 'R') {
			if (read(STDIN_FILENO, &seq[1], 1) != 1)
				goto CX_UNKNOWN;
			switch (seq[1]) {
			case 033:
				if (read(STDIN_FILENO, &seq[2], 1) != 1)
					goto CX_UNKNOWN;
				if (seq[2] == 'W' || seq[2] == 'w') {
					return COPY_RECT;
				}
				goto CX_UNKNOWN;
			case 'j':
			case 'J':
				return JUMP_REGISTER;
			case 'a':
			case 'A':
			case 'm':
			case 'M':
				return MACRO_REGISTER;
			case CTRL('@'):
			case ' ':
				return POINT_REGISTER;
			case 'n':
			case 'N':
				return NUMBER_REGISTER;
			case 'r':
			case 'R':
				return RECT_REGISTER;
			case 's':
			case 'S':
				return REGION_REGISTER;
			case 't':
			case 'T':
				return STRING_RECT;
			case '+':
				return INC_REGISTER;
			case 'i':
			case 'I':
				return INSERT_REGISTER;
			case 'k':
			case 'K':
			case CTRL('W'):
				return KILL_RECT;
			case 'v':
			case 'V':
				return VIEW_REGISTER;
			case 'y':
			case 'Y':
				return YANK_RECT;
			}
		} else if (seq[0] == '=') {
			return WHAT_CURSOR;
		}

CX_UNKNOWN:;
		char seqR[32];
		seqR[0] = 0;
		char buf[8];
		for (int i = 0; seq[i]; i++) {
			if (seq[i] < ' ') {
				sprintf(buf, "C-%c ", seq[i] + '`');
			} else {
				sprintf(buf, "%c ", seq[i]);
			}
			strcat(seqR, buf);
		}
		editorSetStatusMessage("Unknown command C-x %s", seqR);
		return CTRL('x');
	} else if (c == CTRL('p')) {
		return ARROW_UP;
	} else if (c == CTRL('n')) {
		return ARROW_DOWN;
	} else if (c == CTRL('b')) {
		return ARROW_LEFT;
	} else if (c == CTRL('f')) {
		return ARROW_RIGHT;
	} else if (utf8_is2Char(c)) {
		/* 2-byte UTF-8 sequence */
		E.nunicode = 2;

		E.unicode[0] = c;
		if (read(STDIN_FILENO, &E.unicode[1], 1) != 1)
			return UNICODE_ERROR;
		return UNICODE;
	} else if (utf8_is3Char(c)) {
		/* 3-byte UTF-8 sequence */
		E.nunicode = 3;

		E.unicode[0] = c;
		if (read(STDIN_FILENO, &E.unicode[1], 1) != 1)
			return UNICODE_ERROR;
		if (read(STDIN_FILENO, &E.unicode[2], 1) != 1)
			return UNICODE_ERROR;
		return UNICODE;
	} else if (utf8_is4Char(c)) {
		/* 4-byte UTF-8 sequence */
		E.nunicode = 4;

		E.unicode[0] = c;
		if (read(STDIN_FILENO, &E.unicode[1], 1) != 1)
			return UNICODE_ERROR;
		if (read(STDIN_FILENO, &E.unicode[2], 1) != 1)
			return UNICODE_ERROR;
		if (read(STDIN_FILENO, &E.unicode[3], 1) != 1)
			return UNICODE_ERROR;
		return UNICODE;
	}
	return c;
}

int getCursorPosition(int *rows, int *cols) {
	char buf[32];
	unsigned int i = 0;
	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
		return -1;
	while (i < sizeof(buf) - 1) {
		if (read(STDIN_FILENO, &buf[i], 1) != 1)
			break;
		if (buf[i] == 'R')
			break;
		i++;
	}
	buf[i] = '\0';
	if (buf[0] != '\x1b' || buf[1] != '[')
		return -1;
	if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
		return -1;
	return 0;
}

int getWindowSize(int *rows, int *cols) {
	struct winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
			return -1;
		return getCursorPosition(rows, cols);
	} else {
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
	}
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
		editorUpdateRow(row);
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
	editorUpdateRow(row);
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

void editorKillLine(struct editorBuffer *bufr) {
	erow *row = &bufr->row[bufr->cy];

	if (bufr->cx == row->size) {
		editorDelChar(bufr);
	} else {
		clearRedos(bufr);
		struct editorUndo *new = newUndo();
		new->starty = bufr->cy;
		new->endy = bufr->cy;
		new->startx = bufr->cx;
		new->endx = row->size;
		new->delete = 1;
		new->prev = bufr->undo;
		bufr->undo = new;
		int i = 0;
		for (int j = row->size - 1; j >= bufr->cx; j--) {
			new->data[i++] = row->chars[j];
			new->datalen++;
			if (new->datalen >= new->datasize - 2) {
				new->datasize *= 2;
				new->data = realloc(new->data, new->datasize);
			}
		}
		new->data[i] = 0;

		row->chars[bufr->cx] = 0;
		row->size = bufr->cx;
		editorUpdateRow(row);
		bufr->dirty = 1;
		bufr->markx = -1;
		bufr->marky = -1;
	}
}

void editorKillLineBackwards(struct editorBuffer *bufr) {
	if (bufr->cx == 0) {
		return;
	}

	erow *row = &bufr->row[bufr->cy];

	clearRedos(bufr);
	struct editorUndo *new = newUndo();
	new->starty = bufr->cy;
	new->endy = bufr->cy;
	new->startx = 0;
	new->endx = bufr->cx;
	new->delete = 1;
	new->prev = bufr->undo;
	bufr->undo = new;
	int i = 0;
	for (int j = bufr->cx - 1; j >= 0; j--) {
		new->data[i++] = row->chars[j];
		new->datalen++;
		if (new->datalen >= new->datasize - 2) {
			new->datasize *= 2;
			new->data = realloc(new->data, new->datasize);
		}
	}
	new->data[i] = 0;

	row->size -= bufr->cx;
	memmove(row->chars, &row->chars[bufr->cx], row->size);
	row->chars[row->size] = 0;
	editorUpdateRow(row);
	bufr->cx = 0;
	bufr->dirty = 1;
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
	bufr->filename = strdup(filename);
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

/*** append buffer ***/

struct abuf {
	char *b;
	int len;
};

#define ABUF_INIT { NULL, 0 }

void abAppend(struct abuf *ab, const char *s, int len) {
	char *new = realloc(ab->b, ab->len + len);

	if (new == NULL)
		return;
	memcpy(&new[ab->len], s, len);
	ab->b = new;
	ab->len += len;
}

void abFree(struct abuf *ab) {
	free(ab->b);
}

/*** output ***/

void editorSetScxScy(struct editorBuffer *bufr, int screenrows,
		     int screencols) {
	erow *row = &bufr->row[bufr->cy];
	int i;

start:
	i = bufr->rowoff;
	bufr->scy = 0;
	bufr->scx = 0;
	while (i < bufr->cy) {
		bufr->scy += (bufr->row[i].renderwidth / screencols);
		bufr->scy++;
		i++;
	}

	if (bufr->cy >= bufr->numrows) {
		goto end;
	}

	bufr->scx = 0;
	for (i = 0; i < bufr->cx; i += (utf8_nBytes(row->chars[i]))) {
		if (row->chars[i] == '\t') {
			bufr->scx += (EMSYS_TAB_STOP - 1) -
				     (bufr->scx % EMSYS_TAB_STOP);
			bufr->scx++;
		} else {
			bufr->scx += charInStringWidth(row->chars, i);
		}
		if (bufr->scx >= screencols) {
			bufr->scx = 0;
			bufr->scy++;
		}
	}

end:
	if (bufr->scy >= screenrows) {
		/* Dumb, but it should work */
		bufr->rowoff++;
		goto start;
	}
}

void editorScroll(struct editorBuffer *bufr, int screenrows, int screencols) {
	if (bufr->cy < bufr->rowoff) {
		bufr->rowoff = bufr->cy;
	}
	if (bufr->cy >= bufr->rowoff + screenrows) {
		bufr->rowoff = bufr->cy - screenrows + 1;
	}

	editorSetScxScy(bufr, screenrows, screencols);
}

void editorDrawRows(struct editorBuffer *bufr, struct abuf *ab, int screenrows,
		    int screencols) {
	int y;
	int filerow = bufr->rowoff;
	bufr->end = 0;
	for (y = 0; y < screenrows; y++) {
		if (filerow >= bufr->numrows) {
			bufr->end = 1;
			abAppend(ab, CSI "34m~" CSI "0m", 10);
		} else {
			y += (bufr->row[filerow].renderwidth / screencols);
			abAppend(ab, bufr->row[filerow].render,
				 bufr->row[filerow].rsize);
			if (bufr->row[filerow].renderwidth > 0 &&
			    bufr->row[filerow].renderwidth % screencols == 0) {
				abAppend(ab, CRLF, 2);
			}
			filerow++;
		}
		abAppend(ab, CRLF, 2);
	}
}

void editorDrawStatusBar(struct editorWindow *win, struct abuf *ab, int line) {
	/* XXX: It's actually possible for the status bar to end up
	 * outside where it should be, so set it explicitly. */
	char buf[32];
	snprintf(buf, sizeof(buf), CSI "%d;%dH", line, 1);
	abAppend(ab, buf, strlen(buf));

	struct editorBuffer *bufr = win->buf;

	abAppend(ab, "\x1b[7m", 4);
	char status[80];
	int len = 0;
	if (win->_focused) {
		len = snprintf(status, sizeof(status),
			       "-- %.20s %c%c %2d:%2d --",
			       bufr->filename ? bufr->filename : "*scratch*",
			       bufr->dirty ? '*' : '-', bufr->dirty ? '*' : '-',
			       bufr->cy + 1, bufr->cx);
	} else {
		len = snprintf(status, sizeof(status),
			       "   %.20s %c%c %2d:%2d   ",
			       bufr->filename ? bufr->filename : "*scratch*",
			       bufr->dirty ? '*' : '-', bufr->dirty ? '*' : '-',
			       bufr->cy + 1, bufr->cx);
	}
#ifdef EMSYS_DEBUG_UNDO
#ifdef EMSYS_DEBUG_REDO
#define DEBUG_UNDO bufr->redo
#else
#define DEBUG_UNDO bufr->undo
#endif
	if (DEBUG_UNDO != NULL) {
		len = 0;
		for (len = 0; len < DEBUG_UNDO->datalen; len++) {
			status[len] = DEBUG_UNDO->data[len];
			if (DEBUG_UNDO->data[len] == '\n')
				status[len] = '#';
		}
		status[len++] = '"';
		len += sprintf(&status[len],
			       "sx %d sy %d ex %d ey %d cx %d cy %d",
			       DEBUG_UNDO->startx, DEBUG_UNDO->starty,
			       DEBUG_UNDO->endx, DEBUG_UNDO->endy, bufr->cx,
			       bufr->cy);
	}
#endif
#ifdef EMSYS_DEBUG_MACROS
	/* This can get quite wide, you may want to boost the size of status */
	for (int i = 0; i < E.macro.nkeys; i++) {
		len += sprintf(&status[len], "%d: %d ", i, E.macro.keys[i]);
	}
#endif

	char perc[8] = " xxx --";
	if (bufr->numrows == 0) {
		perc[1] = 'E';
		perc[2] = 'm';
		perc[3] = 'p';
	} else if (bufr->end) {
		if (bufr->rowoff == 0) {
			perc[1] = 'A';
			perc[2] = 'l';
			perc[3] = 'l';
		} else {
			perc[1] = 'B';
			perc[2] = 'o';
			perc[3] = 't';
		}
	} else if (bufr->rowoff == 0) {
		perc[1] = 'T';
		perc[2] = 'o';
		perc[3] = 'p';
	} else {
		snprintf(perc, sizeof(perc), " %2d%% --",
			 (bufr->rowoff * 100) / bufr->numrows);
	}

	char fill[2] = "-";
	if (!win->_focused) {
		perc[5] = ' ';
		perc[6] = ' ';
		fill[0] = ' ';
	}

	if (len > E.screencols)
		len = E.screencols;
	abAppend(ab, status, len);
	while (len < E.screencols) {
		if (E.screencols - len == 7) {
			abAppend(ab, perc, 7);
			break;
		} else {
			abAppend(ab, fill, 1);
			len++;
		}
	}
	abAppend(ab, "\x1b[m" CRLF, 5);
}

void editorDrawMinibuffer(struct abuf *ab) {
	abAppend(ab, "\x1b[K", 3);
	int msglen = strlen(E.minibuffer);
	if (msglen > E.screencols)
		msglen = E.screencols;
	if (msglen && time(NULL) - E.statusmsg_time < 5) {
		if (E.focusBuf->query && !E.focusBuf->match) {
			abAppend(ab, "\x1b[91m", 5);
		}
		abAppend(ab, E.minibuffer, msglen);
		abAppend(ab, "\x1b[0m", 4);
	}
}

void editorRefreshScreen() {
	struct abuf ab = ABUF_INIT;

	/* Clear screen */
	abAppend(&ab, "\x1b[2J", 4);

	/* Hide cursor and move to 1,1 */
	abAppend(&ab, CSI "?25l", 6);
	abAppend(&ab, CSI "H", 3);

	int idx = windowFocusedIdx(&E);
	int windowSize = (E.screenrows - 1) / E.nwindows;

	if (E.nwindows == 1) {
		struct editorWindow *win = E.windows[0];
		struct editorBuffer *bufr = win->buf;
		editorScroll(bufr, E.screenrows - 2, E.screencols);
		editorDrawRows(bufr, &ab, E.screenrows - 2, E.screencols);
		editorDrawStatusBar(win, &ab, E.screenrows - 1);
	} else {
		for (int i = 0; i < E.nwindows; i++) {
			struct editorWindow *win = E.windows[i];
			struct editorBuffer *bufr = win->buf;
			editorScroll(bufr, windowSize - 1, E.screencols);
			editorDrawRows(bufr, &ab, windowSize - 1, E.screencols);
			editorDrawStatusBar(win, &ab, ((i + 1) * windowSize));
		}
	}
	editorDrawMinibuffer(&ab);

	/* move to scy, scx; show cursor */
	char buf[32];
	snprintf(buf, sizeof(buf), CSI "%d;%dH",
		 E.windows[idx]->buf->scy + 1 + (windowSize * idx),
		 E.windows[idx]->buf->scx + 1);
	abAppend(&ab, buf, strlen(buf));
	if (E.focusBuf->query && E.focusBuf->match) {
		abAppend(&ab, "\x1b[7m", 4);
		abAppend(&ab, E.focusBuf->query, strlen(E.focusBuf->query));
		abAppend(&ab, "\x1b[0m", 4);
		abAppend(&ab, buf, strlen(buf));
	}
	abAppend(&ab, CSI "?25h", 6);

	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

void editorCursorBottomLine(int curs) {
	char cbuf[32];
	if (E.nwindows == 1) {
		snprintf(cbuf, sizeof(cbuf), CSI "%d;%dH", E.screenrows, curs);
	} else {
		int windowSize = (E.screenrows - 1) / E.nwindows;
		snprintf(cbuf, sizeof(cbuf), CSI "%d;%dH",
			 (windowSize * E.nwindows) + 1, curs);
	}
	write(STDOUT_FILENO, cbuf, strlen(cbuf));
}

void editorCursorBottomLineLong(long curs) {
	char cbuf[32];
	if (E.nwindows == 1) {
		snprintf(cbuf, sizeof(cbuf), CSI "%d;%ldH", E.screenrows, curs);
	} else {
		int windowSize = (E.screenrows - 1) / E.nwindows;
		snprintf(cbuf, sizeof(cbuf), CSI "%d;%ldH",
			 (windowSize * E.nwindows) + 1, curs);
	}
	write(STDOUT_FILENO, cbuf, strlen(cbuf));
}

void editorSetStatusMessage(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(E.minibuffer, sizeof(E.minibuffer), fmt, ap);
	va_end(ap);
	E.statusmsg_time = time(NULL);
}

void editorResizeScreen(int UNUSED(sig)) {
	if (getWindowSize(&E.screenrows, &E.screencols) == -1)
		die("getWindowSize");
	editorRefreshScreen();
}

void editorRecenter(struct editorBuffer *bufr) {
	bufr->rowoff = bufr->cy - ((E.screenrows / E.nwindows) / 2);
	if (bufr->rowoff < 0) {
		bufr->rowoff = 0;
	}
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
			if (buflen != 0) {
				editorSetStatusMessage("");
				if (callback)
					callback(bufr, buf, c);
				return buf;
			}
			break;
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
			} else if (t == PROMPT_BASIC) { // For buffer switching
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
				while (utf8_isCont(
					bufr->row[bufr->cy].chars[bufr->cx]))
					bufr->cx++;
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
			newBuf->filename = strdup("*Shell Output*");
			newBuf->special_buffer = 1;

			// Use a temporary buffer to build each row
			size_t rowStart = 0;
			size_t rowLen = 0;
			for (size_t i = 0; i < outputLen; i++) {
				if (pipeOutput[i] == '\n' ||
				    i == outputLen - 1) {
					// Found a newline or end of output, insert the row
					editorInsertRow(
						newBuf, newBuf->numrows,
						(char *)&pipeOutput[rowStart],
						rowLen);
					rowStart =
						i + 1; // Start of the next row
					rowLen = 0;    // Reset row length
				} else {
					rowLen++;
				}
			}

			// Link the new buffer and update focus
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

			// Update the focused window
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

void editorTransposeWords(struct editorConfig *ed, struct editorBuffer *bufr) {
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

	int scx, scy, ecx, ecy;
	bufferEndOfBackwardWord(bufr, &scx, &scy);
	bufferEndOfForwardWord(bufr, &ecx, &ecy);
	if ((scx == bufr->cx && bufr->cy == scy) ||
	    (ecx == bufr->cx && bufr->cy == ecy)) {
		editorSetStatusMessage("Cannot transpose here");
		return;
	}
	bufr->cx = scx;
	bufr->cy = scy;
	bufr->markx = ecx;
	bufr->marky = ecy;

	editorTransformRegion(ed, bufr, transformerTransposeWords);
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

	int scx, scy;
	editorMoveCursor(bufr, ARROW_LEFT);
	scx = bufr->cx;
	scy = bufr->cy;
	editorMoveCursor(bufr, ARROW_RIGHT);
	editorMoveCursor(bufr, ARROW_RIGHT);
	bufr->markx = scx;
	bufr->marky = scy;
	editorTransformRegion(ed, bufr, transformerTransposeChars);
}

void editorSwitchToNamedBuffer(struct editorConfig *ed,
			       struct editorBuffer *current) {
	char prompt[512];
	snprintf(prompt, sizeof(prompt), "Switch to buffer: %%s");
	uint8_t *buffer_name =
		editorPrompt(current, (uint8_t *)prompt, PROMPT_BASIC, NULL);
	if (!buffer_name) {
		editorSetStatusMessage("Buffer switch canceled");
		return;
	}
	if (buffer_name[0] == '\0') {
		editorSetStatusMessage("No buffer name entered");
		free(buffer_name);
		return;
	}

	struct editorBuffer *found = NULL;
	for (struct editorBuffer *buf = ed->firstBuf; buf != NULL;
	     buf = buf->next) {
		if (buf == current)
			continue; // Skip the current buffer

		char *name = buf->filename ? buf->filename : "*scratch*";
		if (strcmp((char *)buffer_name, name) == 0) {
			found = buf;
			break;
		}
	}

	if (found) {
		ed->focusBuf = found;
		editorSetStatusMessage("Switched to buffer %s",
				       (char *)buffer_name);

		for (int i = 0; i < ed->nwindows; i++) {
			if (ed->windows[i]->_focused) {
				ed->windows[i]->buf = ed->focusBuf;
			}
		}
	} else {
		editorSetStatusMessage("No buffer named '%s'", buffer_name);
	}

	free(buffer_name);
}

/* Where the magic happens */
void editorProcessKeypress(int c) {
	struct editorBuffer *bufr = E.focusBuf;
	int idx;
	struct editorWindow **windows;
	uint8_t *prompt;

	if (E.micro) {
#ifdef EMSYS_CUA
		if (E.micro == REDO && (c == CTRL('_') || c == CTRL('z'))) {
#else
		if (E.micro == REDO && c == CTRL('_')) {
#endif //EMSYS_CUA
			editorDoRedo(bufr);
			return;
		} else {
			E.micro = 0;
		}
	} else {
		E.micro = 0;
	}

	if (ALT_0 <= c && c <= ALT_9) {
		if (!bufr->uarg_active) {
			bufr->uarg_active = 1;
			bufr->uarg = 0;
		}
		bufr->uarg *= 10;
		bufr->uarg += c - ALT_0;
		editorSetStatusMessage("uarg: %i", bufr->uarg);
		return;
	}

#ifdef EMSYS_CU_UARG
	// Handle C-u (Universal Argument)
	if (c == UNIVERSAL_ARGUMENT) {
		bufr->uarg_active = 1;
		bufr->uarg = 4; // Default value for C-u is 4
		editorSetStatusMessage("C-u");
		return;
	}

	// Handle numeric input after C-u
	if (bufr->uarg_active && c >= '0' && c <= '9') {
		if (bufr->uarg == 4) { // If it's the first digit after C-u
			bufr->uarg = c - '0';
		} else {
			bufr->uarg = bufr->uarg * 10 + (c - '0');
		}
		editorSetStatusMessage("C-u %d", bufr->uarg);
		return;
	}
#endif //EMSYS_CU_UARG

	// Handle PIPE_CMD before resetting uarg_active
	if (c == PIPE_CMD) {
		editorPipeCmd(&E, bufr);
		bufr->uarg_active = 0;
		bufr->uarg = 0;
		return;
	}

	int rept = 1;
	if (bufr->uarg_active) {
		bufr->uarg_active = 0;
		rept = bufr->uarg;
	}

	switch (c) {
	case '\r':
		for (int i = 0; i < rept; i++) {
			editorUndoAppendChar(bufr, '\n');
			editorInsertNewline(bufr);
		}
		break;
	case BACKSPACE:
	case CTRL('h'):
		for (int i = 0; i < rept; i++) {
			editorBackSpace(bufr);
		}
		break;
	case DEL_KEY:
	case CTRL('d'):
		for (int i = 0; i < rept; i++) {
			editorDelChar(bufr);
		}
		break;
	case CTRL('l'):
		editorRecenter(bufr);
		break;
	case QUIT:
		if (E.recording) {
			E.recording = 0;
		}
		// Check all buffers for unsaved changes, except the special buffers
		struct editorBuffer *current = E.firstBuf;
		int hasUnsavedChanges = 0;
		while (current != NULL) {
			if (current->dirty && current->filename != NULL &&
			    !current->special_buffer) {
				hasUnsavedChanges = 1;
				break;
			}
			current = current->next;
		}

		if (hasUnsavedChanges) {
			editorSetStatusMessage(
				"There are unsaved changes. Really quit? (y or n)");
			editorRefreshScreen();
			int c = editorReadKey();
			if (c == 'y' || c == 'Y') {
				exit(0);
			}
			editorSetStatusMessage("");
		} else {
			exit(0);
		}
		break;
	case ARROW_LEFT:
	case ARROW_RIGHT:
	case ARROW_UP:
	case ARROW_DOWN:
		for (int i = 0; i < rept; i++) {
			editorMoveCursor(bufr, c);
		}
		break;
	case PAGE_UP:
#ifndef EMSYS_CUA
	case CTRL('z'):
#endif //EMSYS_CUA
		for (int i = 0; i < rept; i++) {
			bufr->cy = bufr->rowoff;
			int times = (E.screenrows / E.nwindows) - 4;
			while (times--)
				editorMoveCursor(bufr, ARROW_UP);
		}
		break;
	case PAGE_DOWN:
#ifndef EMSYS_CUA
	case CTRL('v'):
#endif //EMSYS_CUA
		for (int i = 0; i < rept; i++) {
			bufr->cy = bufr->rowoff + E.screenrows - 1;
			if (bufr->cy > bufr->numrows)
				bufr->cy = bufr->numrows;
			int times = (E.screenrows / E.nwindows) - 4;
			while (times--)
				editorMoveCursor(bufr, ARROW_DOWN);
		}
		break;
	case BEG_OF_FILE:
		bufr->cy = 0;
		bufr->cx = 0;
		bufr->rowoff = 0;
		break;
	case END_OF_FILE:
		bufr->cy = bufr->numrows;
		break;
	case HOME_KEY:
	case CTRL('a'):
		bufr->cx = 0;
		break;
	case END_KEY:
	case CTRL('e'):
		if (bufr->row != NULL && bufr->cy < bufr->numrows) {
			bufr->cx = bufr->row[bufr->cy].size;
		}
		break;
	case CTRL('s'):
		editorFind(bufr);
		break;
	case UNICODE_ERROR:
		editorSetStatusMessage("Bad UTF-8 sequence");
		break;
	case UNICODE:
		for (int i = 0; i < rept; i++) {
			editorUndoAppendUnicode(&E, bufr);
			editorInsertUnicode(bufr);
		}
		break;
#ifdef EMSYS_CUA
	case CUT:
		editorKillRegion(&E, bufr);
		// unmark region
		bufr->markx = -1;
		bufr->marky = -1;
		break;
#endif //EMSYS_CUA
	case SAVE:
		editorSave(bufr);
		break;
	case COPY:
		editorCopyRegion(&E, bufr);
		break;
#ifdef EMSYS_CUA
	case CTRL('C'):
		editorCopyRegion(&E, bufr);
		// unmark region
		bufr->markx = -1;
		bufr->marky = -1;
		break;
#endif //EMSYS_CUA
	case CTRL('@'):
		editorSetMark(bufr);
		break;
	case CTRL('y'):
#ifdef EMSYS_CUA
	case CTRL('v'):
#endif //EMSYS_CUA
		for (int i = 0; i < rept; i++) {
			editorYank(&E, bufr);
		}
		break;
	case CTRL('w'):
		for (int i = 0; i < rept; i++) {
			editorKillRegion(&E, bufr);
		}
		break;
	case CTRL('i'):
		editorIndent(bufr, rept);
		break;
	case CTRL('_'):
#ifdef EMSYS_CUA
	case CTRL('z'):
#endif //EMSYS_CUA
		for (int i = 0; i < rept; i++) {
			editorDoUndo(bufr);
		}
		break;
	case CTRL('k'):
		for (int i = 0; i < rept; i++) {
			editorKillLine(bufr);
		}
		break;
	case CTRL('u'):
		editorKillLineBackwards(bufr);
		break;
	case CTRL('j'):
		for (int i = 0; i < rept; i++) {
			editorInsertNewlineAndIndent(bufr);
		}
		break;
	case CTRL('o'):
		for (int i = 0; i < rept; i++) {
			editorOpenLine(bufr);
		}
		break;
	case CTRL('q'):;
		int nread;
		while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
			if (nread == -1 && errno != EAGAIN)
				die("read");
		}
		for (int i = 0; i < rept; i++) {
			editorUndoAppendChar(bufr, c);
			editorInsertChar(bufr, c);
		}
		break;
	case FORWARD_WORD:
		for (int i = 0; i < rept; i++) {
			editorForwardWord(bufr);
		}
		break;
	case BACKWARD_WORD:
		for (int i = 0; i < rept; i++) {
			editorBackWord(bufr);
		}
		break;
	case FORWARD_PARA:
		for (int i = 0; i < rept; i++) {
			editorForwardPara(bufr);
		}
		break;
	case BACKWARD_PARA:
		for (int i = 0; i < rept; i++) {
			editorBackPara(bufr);
		}
		break;
	case REDO:
		for (int i = 0; i < rept; i++) {
			editorDoRedo(bufr);
			if (bufr->redo != NULL) {
				editorSetStatusMessage(
					"Press C-_ or C-/ to redo again");
				E.micro = REDO;
			}
		}
		break;
	case SWITCH_BUFFER:
		editorSwitchToNamedBuffer(&E, E.focusBuf);
		break;
	case NEXT_BUFFER:
		E.focusBuf = E.focusBuf->next;
		if (E.focusBuf == NULL) {
			E.focusBuf = E.firstBuf;
		}
		for (int i = 0; i < E.nwindows; i++) {
			if (E.windows[i]->_focused) {
				E.windows[i]->buf = E.focusBuf;
			}
		}
		break;
	case PREVIOUS_BUFFER:
		if (E.focusBuf == E.firstBuf) {
			// If we're at the first buffer, go to the last buffer
			E.focusBuf = E.firstBuf;
			while (E.focusBuf->next != NULL) {
				E.focusBuf = E.focusBuf->next;
			}
		} else {
			// Otherwise, go to the previous buffer
			struct editorBuffer *temp = E.firstBuf;
			while (temp->next != E.focusBuf) {
				temp = temp->next;
			}
			E.focusBuf = temp;
		}
		// Update the focused buffer in all windows
		for (int i = 0; i < E.nwindows; i++) {
			if (E.windows[i]->_focused) {
				E.windows[i]->buf = E.focusBuf;
			}
		}
		break;
	case MARK_BUFFER:
		if (bufr->numrows > 0) {
			bufr->cy = bufr->numrows;
			bufr->cx = bufr->row[--bufr->cy].size;
			editorSetMark(bufr);
			bufr->cy = 0;
			bufr->cx = 0;
			bufr->rowoff = 0;
		}
		break;

	case FIND_FILE:
		prompt = editorPrompt(E.focusBuf, "Find File: %s", PROMPT_FILES,
				      NULL);
		if (prompt == NULL) {
			editorSetStatusMessage("Canceled.");
			break;
		}
		if (prompt[strlen(prompt) - 1] == '/') {
			editorSetStatusMessage(
				"Directory editing not supported.");
			break;
		}

		// Check if a buffer with the same filename already exists
		struct editorBuffer *buf = E.firstBuf;
		while (buf != NULL) {
			if (buf->filename != NULL &&
			    strcmp(buf->filename, (char *)prompt) == 0) {
				editorSetStatusMessage(
					"File '%s' already open in a buffer.",
					prompt);
				free(prompt);
				E.focusBuf =
					buf; // Switch to the existing buffer

				// Update the focused window to display the found buffer
				idx = windowFocusedIdx(&E);
				E.windows[idx]->buf = E.focusBuf;

				editorRefreshScreen(); // Refresh to reflect the change
				break; // Exit the loop and the case
			}
			buf = buf->next;
		}

		// If a buffer with the same filename was found, don't create a new one
		if (buf != NULL) {
			break;
		}

		E.firstBuf = newBuffer();
		editorOpen(E.firstBuf, prompt);
		free(prompt);
		E.firstBuf->next = E.focusBuf;
		E.focusBuf = E.firstBuf;
		idx = windowFocusedIdx(&E);
		E.windows[idx]->buf = E.focusBuf;
		break;

	case OTHER_WINDOW:
		editorSwitchWindow(&E);
		break;

	case CREATE_WINDOW:
		E.windows = realloc(E.windows, sizeof(struct editorWindow *) *
						       (++E.nwindows));
		E.windows[E.nwindows - 1] = malloc(sizeof(struct editorWindow));
		E.windows[E.nwindows - 1]->_focused = 0;
		E.windows[E.nwindows - 1]->buf = E.focusBuf;
		break;

	case DESTROY_WINDOW:
		if (E.nwindows == 1) {
			editorSetStatusMessage("Can't kill last window");
			break;
		}

		// Find the index of the window to be destroyed
		int destroyIdx = -1;
		for (int i = 0; i < E.nwindows; i++) {
			if (E.windows[i] == E.focusWin) {
				destroyIdx = i;
				break;
			}
		}

		// Switch focus to another window before destroying
		int newFocusIdx = (destroyIdx + 1) % E.nwindows;
		if (newFocusIdx == destroyIdx)
			newFocusIdx =
				(destroyIdx - 1 + E.nwindows) % E.nwindows;
		setWindowFocus(&E, E.windows[newFocusIdx]);

		// Free the window to be destroyed
		free(E.windows[destroyIdx]);

		// Create a new array of windows, excluding the destroyed one
		windows =
			malloc(sizeof(struct editorWindow *) * (--E.nwindows));
		int j = 0;
		for (int i = 0; i < E.nwindows + 1; i++) {
			if (i != destroyIdx) {
				windows[j] = E.windows[i];
				j++;
			}
		}

		// Update E.windows
		free(E.windows);
		E.windows = windows;

		// Ensure focusWin is pointing to a window in the new array
		for (int i = 0; i < E.nwindows; i++) {
			if (E.windows[i] == E.focusWin) {
				break;
			}
			if (i == E.nwindows - 1) {
				// If we got here, focusWin is not in the new array
				setWindowFocus(&E, E.windows[0]);
			}
		}

		break;

	case DESTROY_OTHER_WINDOWS:
		if (E.nwindows == 1) {
			editorSetStatusMessage("No other windows to delete");
			break;
		}

		// Allocate new array for the single remaining window
		windows = malloc(sizeof(struct editorWindow *));

		// Free all windows except the focused one
		for (int i = 0; i < E.nwindows; i++) {
			if (E.windows[i] != E.focusWin) {
				free(E.windows[i]);
			}
		}

		// Set the single remaining window
		windows[0] = E.focusWin;

		// Update E.windows and E.nwindows
		free(E.windows);
		E.windows = windows;
		E.nwindows = 1;

		// Ensure focus is set correctly
		setWindowFocus(&E, E.windows[0]);

		break;
	case KILL_BUFFER:
		// Bypass confirmation for special buffers
		if (bufr->dirty && bufr->filename != NULL &&
		    !bufr->special_buffer) {
			editorSetStatusMessage(
				"Buffer %.20s modified; kill anyway? (y or n)",
				bufr->filename);
			editorRefreshScreen();
			int c = editorReadKey();
			if (c != 'y' && c != 'Y') {
				editorSetStatusMessage("");
				break;
			}
		}

		// Find the previous buffer (if any)
		struct editorBuffer *prevBuf = NULL;
		if (E.focusBuf != E.firstBuf) {
			prevBuf = E.firstBuf;
			while (prevBuf->next != E.focusBuf) {
				prevBuf = prevBuf->next;
			}
		}

		// Update window focus
		for (int i = 0; i < E.nwindows; i++) {
			if (E.windows[i]->buf == bufr) {
				// If it's the last buffer, create a new scratch buffer
				if (bufr->next == NULL && prevBuf == NULL) {
					E.windows[i]->buf = newBuffer();
					E.windows[i]->buf->filename =
						strdup("*scratch*");
					E.windows[i]->buf->special_buffer = 1;
					E.firstBuf = E.windows[i]->buf;
					E.focusBuf =
						E.firstBuf; // Ensure E.focusBuf is updated
				} else if (bufr->next == NULL) {
					E.windows[i]->buf = E.firstBuf;
					E.focusBuf =
						E.firstBuf; // Ensure E.focusBuf is updated
				} else {
					E.windows[i]->buf = bufr->next;
					E.focusBuf =
						bufr->next; // Ensure E.focusBuf is updated
				}
			}
		}

		// Update the main buffer list
		if (E.firstBuf == bufr) {
			E.firstBuf = bufr->next;
		} else if (prevBuf != NULL) {
			prevBuf->next = bufr->next;
		}

		// Update the focused buffer
		if (E.focusBuf == bufr) {
			E.focusBuf = (bufr->next != NULL) ? bufr->next :
							    prevBuf;
		}

		destroyBuffer(bufr);
		break;

	case SUSPEND:
		raise(SIGTSTP);
		break;

	case DELETE_WORD:
		for (int i = 0; i < rept; i++) {
			editorDeleteWord(bufr);
		}
		break;
	case BACKSPACE_WORD:
		for (int i = 0; i < rept; i++) {
			editorBackspaceWord(bufr);
		}
		break;

	case UPCASE_WORD:
		editorUpcaseWord(&E, bufr, rept);
		break;

	case DOWNCASE_WORD:
		editorDowncaseWord(&E, bufr, rept);
		break;

	case CAPCASE_WORD:
		editorCapitalCaseWord(&E, bufr, rept);
		break;

	case UPCASE_REGION:
		editorTransformRegion(&E, bufr, transformerUpcase);
		break;

	case DOWNCASE_REGION:
		editorTransformRegion(&E, bufr, transformerDowncase);
		break;

	case WHAT_CURSOR:
		c = 0;
		if (bufr->cy >= bufr->numrows) {
			editorSetStatusMessage("End of buffer");
			break;
		} else if (bufr->row[bufr->cy].size <= bufr->cx) {
			c = (uint8_t)'\n';
		} else {
			c = (uint8_t)bufr->row[bufr->cy].chars[bufr->cx];
		}

		int npoint = 0, point;
		for (int y = 0; y < bufr->numrows; y++) {
			for (int x = 0; x <= bufr->row[y].size; x++) {
				npoint++;
				if (x == bufr->cx && y == bufr->cy) {
					point = npoint;
				}
			}
		}
		int perc = ((point - 1) * 100) / npoint;

		if (c == 127) {
			editorSetStatusMessage("char: ^? (%d #o%03o #x%02X)"
					       " point=%d of %d (%d%%)",
					       c, c, c, point, npoint, perc);
		} else if (c < ' ') {
			editorSetStatusMessage("char: ^%c (%d #o%03o #x%02X)"
					       " point=%d of %d (%d%%)",
					       c + 0x40, c, c, c, point, npoint,
					       perc);
		} else {
			editorSetStatusMessage("char: %c (%d #o%03o #x%02X)"
					       " point=%d of %d (%d%%)",
					       c, c, c, c, point, npoint, perc);
		}
		break;

	case TRANSPOSE_WORDS:
		editorTransposeWords(&E, bufr);
		break;

	case CTRL('t'):
		editorTransposeChars(&E, bufr);
		break;

	case EXEC_CMD:;
		uint8_t *cmd =
			editorPrompt(bufr, "cmd: %s", PROMPT_BASIC, NULL);
		if (cmd != NULL) {
			runCommand(cmd, &E, bufr);
			free(cmd);
		}
		break;
	case QUERY_REPLACE:
		editorQueryReplace(&E, bufr);
		break;

	case GOTO_LINE:
		editorGotoLine(bufr);
		break;

	case CTRL('x'):
	case 033:
		/* These take care of their own error messages */
		break;

	case CTRL('g'):
		/* Expected behavior */
#ifdef EMSYS_CUA
		bufr->markx = -1;
		bufr->marky = -1;
#endif //EMSYS_CUA
		editorSetStatusMessage("Quit");
		break;

	case BACKTAB:
		editorUnindent(bufr, rept);
		break;

	case SWAP_MARK:
		if (0 <= bufr->markx &&
		    (0 <= bufr->marky && bufr->marky < bufr->numrows)) {
			int swapx = bufr->cx;
			int swapy = bufr->cy;
			bufr->cx = bufr->markx;
			bufr->cy = bufr->marky;
			bufr->markx = swapx;
			bufr->marky = swapy;
		}
		break;

	case JUMP_REGISTER:
		editorJumpToRegister(&E);
		break;
	case MACRO_REGISTER:
		editorMacroToRegister(&E);
		break;
	case POINT_REGISTER:
		editorPointToRegister(&E);
		break;
	case NUMBER_REGISTER:
		editorNumberToRegister(&E, rept);
		break;
	case REGION_REGISTER:
		editorRegionToRegister(&E, bufr);
		break;
	case INC_REGISTER:
		editorIncrementRegister(&E, bufr);
		break;
	case INSERT_REGISTER:
		editorInsertRegister(&E, bufr);
		break;
	case VIEW_REGISTER:
		editorViewRegister(&E, bufr);
		break;

	case STRING_RECT:
		editorStringRectangle(&E, bufr);
		break;

	case COPY_RECT:
		editorCopyRectangle(&E, bufr);
		break;

	case KILL_RECT:
		editorKillRectangle(&E, bufr);
		break;

	case YANK_RECT:
		editorYankRectangle(&E, bufr);
		break;

	case RECT_REGISTER:
		editorRectRegister(&E, bufr);
		break;

	case EXPAND:
		editorCompleteWord(&E, bufr);
		break;

	default:
		if (ISCTRL(c)) {
			editorSetStatusMessage("Unknown command C-%c",
					       c | 0x60);
		} else {
			for (int i = 0; i < rept; i++) {
				editorUndoAppendChar(bufr, c);
				editorInsertChar(bufr, c);
			}
		}
		break;
	}
}

/*** init ***/

void setupHandlers() {
	signal(SIGWINCH, editorResizeScreen);
	signal(SIGCONT, editorResume);
	signal(SIGTSTP, editorSuspend);
}

struct editorBuffer *newBuffer() {
	struct editorBuffer *ret = malloc(sizeof(struct editorBuffer));
	ret->indent = 0;
	ret->markx = -1;
	ret->marky = -1;
	ret->cx = 0;
	ret->cy = 0;
	ret->scx = 0;
	ret->scy = 0;
	ret->rowoff = 0;
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
	return ret;
}

void destroyBuffer(struct editorBuffer *buf) {
	clearUndosAndRedos(buf);
	free(buf->filename);
	free(buf);
}

void initEditor() {
	E.minibuffer[0] = 0;
	E.kill = NULL;
	E.rectKill = NULL;
	E.windows = malloc(sizeof(struct editorWindow *) * 1);
	E.windows[0] = malloc(sizeof(struct editorWindow));
	setWindowFocus(&E, E.windows[0]);
	E.nwindows = 1;
	E.recording = 0;
	E.macro.nkeys = 0;
	E.macro.keys = NULL;
	E.micro = 0;
	E.playback = 0;
	E.firstBuf = NULL;
	memset(E.registers, 0, sizeof(E.registers));
	setupCommands(&E);

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
