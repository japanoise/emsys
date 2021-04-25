#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include<errno.h>
#include<signal.h>
#include<stdint.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/ioctl.h>
#include<termios.h>
#include<unistd.h>
#include"unicode.h"

/*** util ***/

#define EMSYS_TAB_STOP 8

#define ESC "\033"
#define CSI ESC"["
#define CRLF "\r\n"
#define ISCTRL(c) ((0 < c && c < 0x20) || c == 0x7f)
#define PANIC CTRL('c')

enum editorKey {
	ARROW_LEFT = 1000,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
        HOME_KEY,
        DEL_KEY,
        END_KEY,
        PAGE_UP,
        PAGE_DOWN,
	UNICODE,
	UNICODE_ERROR
};

void die(const char *s) {
	write(STDOUT_FILENO, CSI"2J", 4);
	write(STDOUT_FILENO, CSI"H", 3);
	perror(s);
	write(STDOUT_FILENO, CRLF, 2);
	write(STDOUT_FILENO, "sleeping 5s", 11);
	sleep(5);
	exit(1);
}

/*** data ***/

typedef struct erow {
	int size;
	int rsize;
	int renderwidth;
	uint8_t *chars;
	uint8_t *render;
} erow;

struct editorBuffer {
	int cx, cy;
	int scx, scy;
	int numrows;
	int rowoff;
	erow *row;
};

struct editorConfig {
	int screenrows;
	int screencols;
	uint8_t unicode[4];
	int nunicode;
	struct termios orig_termios;
	struct editorBuffer buf;
};

struct editorConfig E;

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
	if (c == PANIC) {
		errno = EINTR;
		die("Panic key");
	}

	if (c == 033) {
		char seq[4];
		if (read(STDIN_FILENO, &seq[0], 1) != 1) return 033;
		if (read(STDIN_FILENO, &seq[1], 1) != 1) return 033;

		if (seq[0] == '[') {
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
				}
			} else {
				switch (seq[1]) {
				case 'A': return ARROW_UP;
				case 'B': return ARROW_DOWN;
				case 'C': return ARROW_RIGHT;
				case 'D': return ARROW_LEFT;
				}
			}
		}

		return 033;
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

/*** row operations ***/

void editorUpdateRow(erow *row) {
	int tabs = 0;
	int extra = 0;
	int j;
	for (j = 0; j < row->size; j++) {
		if (row->chars[j] == '\t') {
			tabs++;
		} else if (ISCTRL(row->chars[j])) {
			/* 
			 * These need an extra few bytes to display
			 * CSI 7 m - 4 bytes
			 * CSI m - 3 bytes 
			 * preceding ^ - 1 byte
			 */
			extra+=8;
		}
	}
	
	free(row->render);
	row->render = malloc(row->size + tabs*(EMSYS_TAB_STOP - 1) + extra + 1);
	row->renderwidth = 0;

	int idx = 0;
	for (j = 0; j < row->size; j++) {
		if (row->chars[j] == '\t') {
			row->renderwidth += EMSYS_TAB_STOP;
			row->render[idx++] = ' ';
			while (idx % EMSYS_TAB_STOP != 0) row->render[idx++] = ' ';
		} else if (row->chars[j] == 0x7f) {
			row->renderwidth += 2;
			row->render[idx++] = 0x1b;
			row->render[idx++] = '[';
			row->render[idx++] = '7';
			row->render[idx++] = 'm';
			row->render[idx++] = '^';
			row->render[idx++] = '?';
			row->render[idx++] = 0x1b;
			row->render[idx++] = '[';
			row->render[idx++] = 'm';
		} else if (ISCTRL(row->chars[j])) {
			row->renderwidth += 2;
			row->render[idx++] = 0x1b;
			row->render[idx++] = '[';
			row->render[idx++] = '7';
			row->render[idx++] = 'm';
			row->render[idx++] = '^';
			row->render[idx++] = row->chars[j]|0x40;
			row->render[idx++] = 0x1b;
			row->render[idx++] = '[';
			row->render[idx++] = 'm';
		} else if (row->chars[j] > 0x7f) {
			int width = charInStringWidth(row->chars, idx);
			row->render[idx++] = row->chars[j];
			row->renderwidth += width;
		} else if (utf8_isCont(row->chars[j])) {
			row->render[idx++] = row->chars[j];
		} else {
			row->renderwidth += 1;
			row->render[idx++] = row->chars[j];
		}
	}
	row->render[idx] = 0;
	row->rsize=idx;
}

void editorAppendRow(char *s, size_t len) {
	E.buf.row = realloc(E.buf.row, sizeof(erow) * (E.buf.numrows + 1));

	int at = E.buf.numrows;
	E.buf.row[at].size = len;
	E.buf.row[at].chars = malloc(len + 1);
	memcpy(E.buf.row[at].chars, s, len);
	E.buf.row[at].chars[len] = '\0';

	E.buf.row[at].rsize = 0;
	E.buf.row[at].render = NULL;
	editorUpdateRow(&E.buf.row[at]);

	E.buf.numrows++;
}

/*** file i/o ***/

void editorOpen(char *filename) {
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
		editorAppendRow(line, linelen);
	}
	free(line);
	fclose(fp);
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
	for (y=0; y<screenrows; y++) {
		if (filerow >= E.buf.numrows) {
			abAppend(ab, CSI"34m~"CSI"0m", 10);
		} else {
			y += (E.buf.row[filerow].renderwidth/screencols);
			abAppend(ab, E.buf.row[filerow].render,
					 E.buf.row[filerow].rsize);
			filerow++;
		}
		abAppend(ab, "\x1b[K", 3);
		if (y<screenrows - 1) {
			abAppend(ab, CRLF, 2);
		}
	}
}

void editorRefreshScreen() {
	struct abuf ab = ABUF_INIT;

	/* Hide cursor and move to 1,1 */
	abAppend(&ab, CSI"?25l", 6);
	abAppend(&ab, CSI"H", 3);

	editorScroll(E.screenrows-2, E.screencols);
	editorDrawRows(&ab, E.screenrows-2, E.screencols);

	/* Status line */
	abAppend(&ab, CRLF, 2);
	char statusBuf[32];
	snprintf(statusBuf, sizeof(statusBuf), "cx %d cy %d scx %d scy %d", E.buf.cx, E.buf.cy, E.buf.scx, E.buf.scy);
	abAppend(&ab, statusBuf, strlen(statusBuf));
	abAppend(&ab, "\x1b[K", 3);
	abAppend(&ab, CRLF, 2);
	/* Minibuffer */
	abAppend(&ab, "\x1b[K", 3);

	abAppend(&ab, CSI"H", 3);

	/* move to scy, scx; show cursor */
	char buf[32];
	snprintf(buf, sizeof(buf), CSI"%d;%dH", E.buf.scy + 1, E.buf.scx + 1);
	abAppend(&ab, buf, strlen(buf));
	abAppend(&ab, CSI"?25h", 6);

	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

void editorResizeScreen(int sig) {
	if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
	editorRefreshScreen();
}

/*** input ***/

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
	case CTRL('q'):
		exit(0);
		break;
	case ARROW_LEFT:
	case ARROW_RIGHT:
	case ARROW_UP:
	case ARROW_DOWN:
		editorMoveCursor(c);
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
	}
}

/*** init ***/

void initEditor() {
	E.buf.cx = 0;
	E.buf.cy = 0;
	E.buf.scx = 0;
	E.buf.scy = 0;
	E.buf.rowoff = 0;
	E.buf.numrows = 0;
	E.buf.row = NULL;

	if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main(int argc, char *argv[]) {
	enableRawMode();
	initEditor();
	if (argc >= 2) {
		editorOpen(argv[1]);
	}

	signal (SIGWINCH, editorResizeScreen);

	while (1) {
		editorRefreshScreen();
		editorProcessKeypress();
	}
	return 0;
}

