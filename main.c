#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include<errno.h>
#include<fcntl.h>
#include<signal.h>
#include<stdarg.h>
#include<stdint.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/ioctl.h>
#include<termios.h>
#include<time.h>
#include<unistd.h>
#include"emsys.h"
#include"region.h"
#include"row.h"
#include"undo.h"
#include"unicode.h"

struct editorConfig E;

void editorMoveCursor(int key);

void die(const char *s) {
	write(STDOUT_FILENO, CSI"2J", 4);
	write(STDOUT_FILENO, CSI"H", 3);
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

/*** terminal ***/

void disableRawMode() {
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios)==-1)
		die("disableRawMode tcsetattr");
	if(write(STDOUT_FILENO, CSI"?1049l", 8)==-1)
		die("disableRawMode write");
}

void enableRawMode() {
	/* Saves the screen and switches to an alt screen */
	if(write(STDOUT_FILENO, CSI"?1049h", 8)==-1)
		die("enableRawMode write");
	/* 
	 * I looked into it. It's possible, but not easy, to do it
	 * without termios. Basically you'd have to hand-hack and send
	 * off your own bits. Check out busybox vi and that rabbithole
	 * for an implementation. 
	 */
	if(tcgetattr(STDIN_FILENO, &E.orig_termios)==-1)
		die("tcgetattr");
	atexit(disableRawMode);

	struct termios raw = E.orig_termios;
	raw.c_iflag &= ~(BRKINT|ICRNL|INPCK|ISTRIP|IXON);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO|ICANON|IEXTEN|ISIG);
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw)==-1)
		die("enableRawMode tcsetattr");
}

/* Raw reading a keypress */
int editorReadKey() {
	int nread;
	uint8_t c;
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if (nread == -1 && errno != EAGAIN) die("read");
	}

	if (c == 033) {
		char seq[4];
		if (read(STDIN_FILENO, &seq[0], 1) != 1) return 033;

		if (seq[0] == '[') {
			if (read(STDIN_FILENO, &seq[1], 1) != 1) return 033;
			if (seq[1] >= '0' && seq[1] <= '9') {
				if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
				if (seq[2] == '~') {
					switch (seq[1]) {
					case '1': return HOME_KEY;
					case '3': return DEL_KEY;
					case '4': return END_KEY;
					case '5': return PAGE_UP;
					case '6': return PAGE_DOWN;
					case '7': return HOME_KEY;
					case '8': return END_KEY;
					}
				} else if (seq[2] == '4') {
					if (read(STDIN_FILENO, &seq[3], 1) != 1) return '\x1b';
					if (seq[3] == '~') {
						errno = EINTR;
						die("Panic key");
					}
				}
			} else {
				switch (seq[1]) {
				case 'A': return ARROW_UP;
				case 'B': return ARROW_DOWN;
				case 'C': return ARROW_RIGHT;
				case 'D': return ARROW_LEFT;
				}
			}
		} else if (seq[0]=='v' || seq[0] =='V') {
			return PAGE_UP;
		} else if (seq[0]=='<') {
			return BEG_OF_FILE;
		} else if (seq[0]=='>') {
			return END_OF_FILE;
		} else if (seq[0]=='w' || seq[0] == 'W') {
			return COPY;
		}

		return 033;
	} else if (c == CTRL('x')) {
		/* Welcome to Emacs! */
		char seq[4];
		if (read(STDIN_FILENO, &seq[0], 1) != 1) return CTRL('x');
		if (seq[0] == CTRL('c')) {
			return QUIT;
		} else if (seq[0] == CTRL('s')) {
			return SAVE;
		} else if (seq[0] == CTRL('_')) {
			return REDO;
		}
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
	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
	while (i < sizeof(buf) - 1) {
		if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
		if (buf[i] == 'R') break;
		i++;
	}
	buf[i] = '\0';
	if (buf[0] != '\x1b' || buf[1] != '[') return -1;
	if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;
	return 0;
}

int getWindowSize(int *rows, int *cols) {
	struct winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
		return getCursorPosition(rows, cols);
	} else {
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
	}
}

/*** editor operations ***/

void editorInsertChar(int c) {
	if (E.buf.cy == E.buf.numrows) {
		editorInsertRow(&E, E.buf.numrows, "", 0);
	}
	editorRowInsertChar(&E, &E.buf.row[E.buf.cy], E.buf.cx, c);
	E.buf.cx++;
}

void editorInsertUnicode() {
	if (E.buf.cy == E.buf.numrows) {
		editorInsertRow(&E, E.buf.numrows, "", 0);
	}
	editorRowInsertUnicode(&E, &E.buf.row[E.buf.cy], E.buf.cx);
	E.buf.cx += E.nunicode;
}

void editorInsertNewline() {
	if (E.buf.cx == 0) {
		editorInsertRow(&E, E.buf.cy, "", 0);
	} else {
		erow *row = &E.buf.row[E.buf.cy];
		editorInsertRow(&E, E.buf.cy + 1, &row->chars[E.buf.cx], row->size - E.buf.cx);
		row = &E.buf.row[E.buf.cy];
		row->size = E.buf.cx;
		row->chars[row->size] = '\0';
		editorUpdateRow(row);
	}
	E.buf.cy++;
	E.buf.cx = 0;
}

void editorDelChar() {
	if (E.buf.cy == E.buf.numrows) return;
	if (E.buf.cy == 0 && E.buf.cx == 0) return;

	erow *row = &E.buf.row[E.buf.cy];
	if (E.buf.cx > 0) {
		do {
			E.buf.cx--;
			editorUndoDelChar(&E.buf, row->chars[E.buf.cx]);
		} while (utf8_isCont(row->chars[E.buf.cx]));
		editorRowDelChar(&E, row, E.buf.cx);
	} else {
		editorUndoDelChar(&E.buf, '\n');
		E.buf.cx = E.buf.row[E.buf.cy-1].size;
		editorRowAppendString(&E, &E.buf.row[E.buf.cy-1], row->chars,
				      row->size);
		editorDelRow(&E, E.buf.cy);
		E.buf.cy--;
	}
}

void editorKillLine() {
	erow *row = &E.buf.row[E.buf.cy];

	if (E.buf.cx == E.buf.row->size) {
		editorMoveCursor(ARROW_RIGHT);
		editorDelChar();
	} else {
		clearRedos(&E.buf);
		struct editorUndo *new = newUndo();
		new->starty = E.buf.cy;
		new->endy = E.buf.cy;
		new->startx = E.buf.cx;
		new->endx = row->size;
		new->delete = 1;
		new->prev = E.buf.undo;
		E.buf.undo = new;
		int i = 0;
		for (int j = row->size-1; j >= E.buf.cx; j--) {
			new->data[i++] = row->chars[j];
			new->datalen++;
			if (new->datalen >= new->datasize - 2) {
				new->datasize *= 2;
				new->data = realloc(new->data, new->datasize);
			}
		}
		new->data[i] = 0;

		row->chars[E.buf.cx] == 0;
		row->size = E.buf.cx;
		editorUpdateRow(row);
	}
}

/*** file i/o ***/

char *editorRowsToString(int *buflen) {
	int totlen = 0;
	int j;
	for (j = 0; j < E.buf.numrows; j++) {
		totlen += E.buf.row[j].size + 1;
	}
	*buflen = totlen;

	char *buf = malloc(totlen);
	char *p = buf;
	for (j = 0; j < E.buf.numrows; j++) {
		memcpy(p, E.buf.row[j].chars, E.buf.row[j].size);
		p+=E.buf.row[j].size;
		*p = '\n';
		p++;
	}

	return buf;
}

void editorOpen(char *filename) {
	free(E.buf.filename);
	E.buf.filename = strdup(filename);
	FILE *fp = fopen(filename, "r");
	if (!fp) die("fopen");

	char * line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	/* Doesn't handle null bytes */
	while ((linelen = getline(&line, &linecap, fp)) != -1) {
		while (linelen > 0 && (line[linelen - 1] == '\n' ||
				       line[linelen - 1] == '\r'))
			linelen--;
		editorInsertRow(&E, E.buf.numrows, line, linelen);
	}
	free(line);
	fclose(fp);
	E.buf.dirty = 0;
}

void editorSave() {
	if (E.buf.filename==NULL) {
		E.buf.filename = editorPrompt("Save as: %s", NULL);
		if (E.buf.filename==NULL) {
			editorSetStatusMessage("Save aborted.");
			return;
		}
	}

	int len;
	char *buf = editorRowsToString(&len);

	int fd = open(E.buf.filename, O_RDWR | O_CREAT, 0644);
	if (fd != -1) {
		if (ftruncate(fd, len) != -1) {
			if (write(fd, buf, len)) {
				close(fd);
				free(buf);
				E.buf.dirty = 0;
				editorSetStatusMessage("Wrote %d bytes to %s",
						       len, E.buf.filename);
				return;
			}
		}
		close(fd);
	}

	free(buf);
	editorSetStatusMessage("Save failed: %s", strerror(errno));
}

/*** find ***/

void editorFindCallback(uint8_t *query, int key) {
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
	for (int i = 0; i < E.buf.numrows; i++) {
		current += direction;
		if(current == -1) current = E.buf.numrows - 1;
		else if (current == E.buf.numrows) current = 0;
		
		erow *row = &E.buf.row[current];
		uint8_t *match = strstr(row->chars, query);
		if (match) {
			last_match = current;
			E.buf.cy = current;
			E.buf.cx = match - row->chars;
			E.buf.rowoff = E.buf.numrows;
			break;
		}
	}
}

void editorFind() {
	int saved_cx = E.buf.cx;
	int saved_cy = E.buf.cy;
	int saved_rowoff = E.buf.rowoff;
		
	uint8_t *query = editorPrompt("Search (C-g to cancel): %s", editorFindCallback);
	
	if (query) {
		free(query);
	} else {
		E.buf.cx = saved_cx;
		E.buf.cy = saved_cy;
		E.buf.rowoff = saved_rowoff;
	}
}

/*** append buffer ***/

struct abuf {
	char *b;
	int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len) {
	char *new = realloc(ab->b, ab->len + len);

	if (new == NULL) return;
	memcpy(&new[ab->len], s, len);
	ab->b = new;
	ab->len += len;
}

void abFree(struct abuf *ab) {
	free(ab->b);
}

/*** output ***/

void editorSetScxScy(int screenrows, int screencols) {
	erow *row = &E.buf.row[E.buf.cy];
	int i;

start:
	i = E.buf.rowoff;
	E.buf.scy = 0;
	E.buf.scx = 0;
	while (i < E.buf.cy) {
		E.buf.scy+=(E.buf.row[i].renderwidth / screencols);
		E.buf.scy++;
		i++;
	}

	if (E.buf.cy >= E.buf.numrows) {
		goto end;
	}

	E.buf.scx = 0;
	for (i = 0; i < E.buf.cx; i+=(utf8_nBytes(row->chars[i]))) {
		if (row->chars[i] == '\t') {
			E.buf.scx += (EMSYS_TAB_STOP - 1) - (E.buf.scx % EMSYS_TAB_STOP);
			E.buf.scx++;
		} else {
			E.buf.scx+=charInStringWidth(row->chars, i);
		}
		if (E.buf.scx>=screencols) {
			E.buf.scx = 0;
			E.buf.scy++;
		}
	}

end:
	if (E.buf.scy >= screenrows) {
		/* Dumb, but it should work */
		E.buf.rowoff++;
		goto start;
	}
}

void editorScroll(int screenrows, int screencols) {
	if (E.buf.cy < E.buf.rowoff) {
		E.buf.rowoff = E.buf.cy;
	}
	if (E.buf.cy >= E.buf.rowoff + screenrows) {
		E.buf.rowoff = E.buf.cy - screenrows + 1;
	}

	editorSetScxScy(screenrows, screencols);
}

void editorDrawRows(struct abuf *ab, int screenrows, int screencols) {
	int y;
	int filerow = E.buf.rowoff;
	E.buf.end = 0;
	for (y=0; y<screenrows; y++) {
		if (filerow >= E.buf.numrows) {
			E.buf.end = 1;
			abAppend(ab, CSI"34m~"CSI"0m", 10);
		} else {
			y += (E.buf.row[filerow].renderwidth/screencols);
			abAppend(ab, E.buf.row[filerow].render,
					 E.buf.row[filerow].rsize);
			filerow++;
		}
		abAppend(ab, "\x1b[K", 3);
		abAppend(ab, CRLF, 2);
	}
}

void editorDrawStatusBar(struct abuf *ab, int line) {
	/* XXX: It's actually possible for the status bar to end up
	 * outside where it should be, so set it explicitly. */
	char buf[32];
	snprintf(buf, sizeof(buf), CSI"%d;%dH", line, 1);
	abAppend(ab, buf, strlen(buf));

	abAppend(ab, "\x1b[7m", 4);
	char status[80];
	int len = snprintf(status, sizeof(status), "-- %.20s %c%c %2d:%2d --",
			   E.buf.filename ? E.buf.filename : "[untitled]",
			   E.buf.dirty ? '*': '-', E.buf.dirty ? '*': '-',
			   E.buf.cy+1, E.buf.cx);
#ifdef EMSYS_DEBUG_UNDO
	if (E.buf.undo != NULL) {
		int i = 0;
		for (i = 0; i < E.buf.undo->datalen; i++) {
			status[i] = E.buf.undo->data[i];
			if (E.buf.undo->data[i] == '\n')
				status[i] = '#';
		}
		status[i++] = '"';
		i += sprintf(&status[i], "sx %d sy %d ex %d ey %d",
			E.buf.undo->startx,
			E.buf.undo->starty,
			E.buf.undo->endx,
			E.buf.undo->endy);
	}
#endif

	char perc[8] = " xxx --";
	if (E.buf.numrows == 0) {
		perc[1] = 'E';
		perc[2] = 'm';
		perc[3] = 'p';
	} else if (E.buf.end) {
		if (E.buf.rowoff == 0) {
			perc[1] = 'A';
			perc[2] = 'l';
			perc[3] = 'l';
		} else {
			perc[1] = 'B';
			perc[2] = 'o';
			perc[3] = 't';
		}
	} else if(E.buf.rowoff == 0) {
		perc[1] = 'T';
		perc[2] = 'o';
		perc[3] = 'p';
	} else {
		snprintf(perc, sizeof(perc), " %2d%% --",
			 (E.buf.rowoff*100)/E.buf.numrows);
	}

	if (len > E.screencols) len = E.screencols;
	abAppend(ab, status, len);
	while (len < E.screencols) {
		if (E.screencols - len == 7) {
			abAppend(ab, perc, 7);
			break;
		} else {
			abAppend(ab, "-", 1);
			len++;
		}
	}
	abAppend(ab, "\x1b[m"CRLF, 5);
}

void editorDrawMinibuffer(struct abuf *ab) {
	abAppend(ab, "\x1b[K", 3);
	int msglen = strlen(E.minibuffer);
	if (msglen > E.screencols) msglen = E.screencols;
	if (msglen && time(NULL) - E.statusmsg_time < 5)
		abAppend(ab, E.minibuffer, msglen);
}

void editorRefreshScreen() {
	struct abuf ab = ABUF_INIT;

	/* Hide cursor and move to 1,1 */
	abAppend(&ab, CSI"?25l", 6);
	abAppend(&ab, CSI"H", 3);

	editorScroll(E.screenrows-2, E.screencols);
	editorDrawRows(&ab, E.screenrows-2, E.screencols);
	editorDrawStatusBar(&ab, E.screenrows-1);
	editorDrawMinibuffer(&ab);

	/* move to scy, scx; show cursor */
	char buf[32];
	snprintf(buf, sizeof(buf), CSI"%d;%dH", E.buf.scy + 1, E.buf.scx + 1);
	abAppend(&ab, buf, strlen(buf));
	abAppend(&ab, CSI"?25h", 6);

	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(E.minibuffer, sizeof(E.minibuffer), fmt, ap);
	va_end(ap);
	E.statusmsg_time = time(NULL);
}

void editorResizeScreen(int sig) {
	if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
	editorRefreshScreen();
}

/*** input ***/

uint8_t *editorPrompt(uint8_t *prompt, void(*callback)(uint8_t *, int)) {
	size_t bufsize = 128;
	uint8_t *buf = malloc(bufsize);

	int promptlen = strlen(prompt) - 2;

	size_t buflen = 0;
	size_t bufwidth = 0;
	buf[0] = 0;
	char cbuf[32];

	for (;;) {
		editorSetStatusMessage(prompt, buf);
		editorRefreshScreen();
		snprintf(cbuf, sizeof(cbuf), CSI"%d;%dH", E.screenrows,
			 promptlen + bufwidth + 1);
		write(STDOUT_FILENO, cbuf, strlen(cbuf));

		int c = editorReadKey();
		switch (c) {
		case '\r':
			if (buflen != 0) {
				editorSetStatusMessage("");
				if (callback) callback(buf, c);
				return buf;
			}
			break;
		case CTRL('g'):
		case CTRL('c'):
			editorSetStatusMessage("");
			if (callback) callback(buf, c);
			free(buf);
			return NULL;
			break;
		case CTRL('h'):
		case BACKSPACE:
		case DEL_KEY:
			if (buflen==0) {
				break;
			}
			while (utf8_isCont(buf[buflen-1])) {
				buf[--buflen] = 0;
			}
			buf[--buflen] = 0;
			bufwidth = stringWidth(buf);
			break;
		case UNICODE:;
			buflen += E.nunicode;
			if (buflen >= (bufsize-1)) {
				bufsize *= 2;
				buf = realloc(buf, bufsize);
			}
			for (int i = 0; i < E.nunicode; i++) {
				buf[(buflen-E.nunicode)+i] = E.unicode[i];
			}
			buf[buflen] = 0;
			bufwidth = stringWidth(buf);
		default:
			if (!ISCTRL(c) && c < 256) {
				if (buflen >= bufsize - 1) {
					bufsize *= 2;
					buf = realloc(buf, bufsize);
				}
				buf[buflen++] = c;
				buf[buflen] = 0;
				bufwidth++;
			}
		}

		if (callback) callback(buf, c);
	}
}

void editorMoveCursor(int key) {
	erow *row = (E.buf.cy >= E.buf.numrows) ? NULL : &E.buf.row[E.buf.cy];
	
	switch (key) {
	case ARROW_LEFT:
		if (E.buf.cx != 0) {
			do E.buf.cx--; while (E.buf.cx != 0 && utf8_isCont(row->chars[E.buf.cx]));
		} else if (E.buf.cy > 0) {
			E.buf.cy--;
			E.buf.cx = E.buf.row[E.buf.cy].size;
		}
		break;
	case ARROW_RIGHT:
		if (row && E.buf.cx < row->size) {
			E.buf.cx+=utf8_nBytes(row->chars[E.buf.cx]);
		} else if (row && E.buf.cx == row -> size) {
			E.buf.cy++;
			E.buf.cx=0;
		}
		break;
	case ARROW_UP:
		if (E.buf.cy > 0) {
			E.buf.cy--;
			while (utf8_isCont(E.buf.row[E.buf.cy].chars[E.buf.cx]))
				E.buf.cx++;
		}
		break;
	case ARROW_DOWN:
		if (E.buf.cy < E.buf.numrows) {
			E.buf.cy++;
			if (E.buf.cy < E.buf.numrows) {
				while (utf8_isCont(E.buf.row[E.buf.cy].chars[E.buf.cx]))
					E.buf.cx++;
			}
		}
		break;
	}

	row = (E.buf.cy >= E.buf.numrows) ? NULL : &E.buf.row[E.buf.cy];
	int rowlen = row ? row->size : 0;
	if (E.buf.cx > rowlen) {
		E.buf.cx = rowlen;
	}
}

/* Where the magic happens */
void editorProcessKeypress() {
	int c = editorReadKey();

	switch (c) {
	case '\r':
		editorUndoAppendChar(&E.buf, '\n');
		editorInsertNewline();
		break;
	case BACKSPACE:
	case CTRL('h'):
		editorDelChar();
		break;
	case DEL_KEY:
	case CTRL('d'):
		editorMoveCursor(ARROW_RIGHT);
		editorDelChar();
		break;
	case CTRL('l'):
		E.buf.rowoff = E.buf.cy - (E.screenrows/2);
		if (E.buf.rowoff < 0) {
			E.buf.rowoff = 0;
		}
		break;
	case QUIT:
		if (E.buf.dirty) {
			editorSetStatusMessage(
				"%.20s has unsaved changes, really quit? Y/N",
				E.buf.filename ? E.buf.filename : "[untitled]");
			editorRefreshScreen();
			int c = editorReadKey();
			if (c == 'y' || c == 'Y') {
				exit(0);
			}
		} else {
			exit(0);
		}
		break;
	case ARROW_LEFT:
	case ARROW_RIGHT:
	case ARROW_UP:
	case ARROW_DOWN:
		editorMoveCursor(c);
		break;
	case PAGE_UP:
	case CTRL('z'):
		E.buf.cy = E.buf.rowoff;
		int times = E.screenrows;
		while (times--)
			editorMoveCursor(ARROW_UP);
		break;
	case PAGE_DOWN:
	case CTRL('v'):
		E.buf.cy = E.buf.rowoff + E.screenrows - 1;
		if (E.buf.cy > E.buf.numrows) E.buf.cy = E.buf.numrows;
		times = E.screenrows;
		while (times--)
			editorMoveCursor(ARROW_DOWN);
		break;
	case BEG_OF_FILE:
		E.buf.cy = 0;
		E.buf.cx = 0;
		E.buf.rowoff = 0;
		break;
	case END_OF_FILE:
		E.buf.cy = E.buf.numrows;
		break;
	case HOME_KEY:
	case CTRL('a'):
		E.buf.cx = 0;
		break;
	case END_KEY:
	case CTRL('e'):
		if (E.buf.row != NULL) {
			E.buf.cx = E.buf.row[E.buf.cy].size;
		}
		break;
	case CTRL('s'):
		editorFind();
		break;
	case UNICODE_ERROR:
		editorSetStatusMessage("Bad UTF-8 sequence");
		break;
	case UNICODE:
		editorUndoAppendUnicode(&E, &E.buf);
		editorInsertUnicode();
		break;
	case SAVE:
		editorSave();
		break;
	case COPY:
		editorCopyRegion(&E, &E.buf);
		break;
	case CTRL('@'):
		editorSetMark(&E.buf);
		break;
	case CTRL('y'):
		editorYank(&E, &E.buf);
		break;
	case CTRL('w'):
		editorKillRegion(&E, &E.buf);
		break;
	case CTRL('i'):
		editorUndoAppendChar(&E.buf, c);
		editorInsertChar(c);
		break;
	case CTRL('_'):
		editorDoUndo(&E, &E.buf);
		break;
	case CTRL('k'):
		editorKillLine();
		break;
	case REDO:
		editorDoRedo(&E, &E.buf);
		break;
	default:
		if (ISCTRL(c)) {
			editorSetStatusMessage("Unknown command C-%c", c|0x60);
		} else {
			editorUndoAppendChar(&E.buf, c);
			editorInsertChar(c);
		}
		break;
	}
}

/*** init ***/

void initEditor() {
	E.buf.markx = -1;
	E.buf.marky = -1;
	E.buf.cx = 0;
	E.buf.cy = 0;
	E.buf.scx = 0;
	E.buf.scy = 0;
	E.buf.rowoff = 0;
	E.buf.numrows = 0;
	E.buf.row = NULL;
	E.buf.filename = NULL;
	E.buf.dirty = 0;
	E.buf.undo = NULL;
	E.buf.redo = NULL;
	E.minibuffer[0] = 0;
	E.kill = NULL;

	if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main(int argc, char *argv[]) {
	enableRawMode();
	initEditor();
	if (argc >= 2) {
		editorOpen(argv[1]);
	}

	editorSetStatusMessage("emsys "EMSYS_VERSION" - C-x C-c to quit");
	signal (SIGWINCH, editorResizeScreen);

	while (1) {
		editorRefreshScreen();
		editorProcessKeypress();
	}
	return 0;
}

