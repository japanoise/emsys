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
#include"bound.h"
#include"command.h"
#include"emsys.h"
#include"find.h"
#include"region.h"
#include"row.h"
#include"transform.h"
#include"undo.h"
#include"unicode.h"
#include"unused.h"

struct editorConfig E;

void editorMoveCursor(struct editorBuffer *bufr, int key);
void setupHandlers();

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

int windowFocusedIdx(struct editorConfig *ed) {
	for(int i = 0; i < E.nwindows; i++) {
		if (ed->windows[i]->focused) {
			return i;
		}
	}
	/* You're in trouble m80 */
	return 0;
}

void editorSwitchWindow(struct editorConfig *ed) {
	if (ed->nwindows == 1) {
		editorSetStatusMessage("No other windows to select");
		return;
	}
	int idx = windowFocusedIdx(ed);
	ed->windows[idx++]->focused = 0;
	if (idx >= ed->nwindows) {
		idx = 0;
	}
	ed->windows[idx]->focused = 1;
	ed->focusBuf = ed->windows[idx]->buf;
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
		} else if (seq[0]=='f' || seq[0]=='F') {
			return FORWARD_WORD;
		} else if (seq[0]=='b' || seq[0]=='B') {
			return BACKWARD_WORD;
		} else if (seq[0]=='p' || seq[0]=='P') {
			return BACKWARD_PARA;
		} else if (seq[0]=='n' || seq[0]=='N') {
			return FORWARD_PARA;
		} else if (seq[0]=='h' || seq[0]=='H' || seq[0]==127) {
			return BACKSPACE_WORD;
		} else if (seq[0]=='d' || seq[0]=='D') {
			return DELETE_WORD;
		} else if ('0' <= seq[0] && seq[0] <= '9') {
			return ALT_0 + (seq[0] - '0');
		} else if (seq[0]=='u' || seq[0]=='U') {
			return UPCASE_WORD;
		} else if (seq[0]=='l' || seq[0]=='L') {
			return DOWNCASE_WORD;
		} else if (seq[0]=='t' || seq[0]=='T') {
			return TRANSPOSE_WORDS;
		} else if (seq[0]=='x' || seq[0]=='X') {
			return EXEC_CMD;
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
		} else if (seq[0] == 'b' || seq[0] == 'B' || seq[0] == CTRL('b')) {
			return SWITCH_BUFFER;
		} else if (seq[0]=='o' || seq[0]=='O') {
			return OTHER_WINDOW;
		} else if (seq[0]=='2') {
			return CREATE_WINDOW;
		} else if (seq[0]=='0') {
			return DESTROY_WINDOW;
		} else if (seq[0]=='1') {
			return DESTROY_OTHER_WINDOWS;
		} else if (seq[0]=='(') {
			return MACRO_RECORD;
		} else if (seq[0]=='e' || seq[0]=='E') {
			return MACRO_EXEC;
		} else if (seq[0]==')') {
			return MACRO_END;
		} else if (seq[0]=='z' || seq[0]=='Z' || seq[0]==CTRL('z')) {
			return SUSPEND;
		} else if (seq[0]=='u' || seq[0]=='U' || seq[0]==CTRL('u')) {
			return UPCASE_REGION;
		} else if (seq[0]=='l' || seq[0]=='L' || seq[0]==CTRL('l')) {
			return DOWNCASE_REGION;
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
		editorInsertRow(bufr, bufr->cy + 1, &row->chars[bufr->cx], row->size - bufr->cx);
		row = &bufr->row[bufr->cy];
		row->size = bufr->cx;
		row->chars[row->size] = '\0';
		editorUpdateRow(row);
	}
	bufr->cy++;
	bufr->cx = 0;
}

void editorInsertNewlineAndIndent(struct editorBuffer *bufr) {
	editorUndoAppendChar(bufr, '\n');
	editorInsertNewline(bufr);
	int i = 0;
	uint8_t c = bufr->row[bufr->cy-1].chars[i];
	while (c == ' ' || c == CTRL('i')) {
		editorUndoAppendChar(bufr, c);
		editorInsertChar(bufr, c);
		c = bufr->row[bufr->cy-1].chars[++i];
	}
}

void editorDelChar(struct editorBuffer *bufr) {
	if (bufr->cy == bufr->numrows) return;
	if (bufr->cy == bufr->numrows - 1 &&
	    bufr->cx == bufr->row[bufr->cy].size)
		return;

	erow *row = &bufr->row[bufr->cy];
	editorUndoDelChar(bufr, row);
	if (bufr->cx == row->size) {
		row = &bufr->row[bufr->cy+1];
		editorRowAppendString(bufr, &bufr->row[bufr->cy], row->chars,
				      row->size);
		editorDelRow(bufr, bufr->cy+1);
	} else {
		editorRowDelChar(bufr, row, bufr->cx);
	}
}

void editorBackSpace(struct editorBuffer *bufr) {
	if (bufr->cy == bufr->numrows) return;
	if (bufr->cy == 0 && bufr->cx == 0) return;

	erow *row = &bufr->row[bufr->cy];
	if (bufr->cx > 0) {
		do {
			bufr->cx--;
			editorUndoBackSpace(bufr, row->chars[bufr->cx]);
		} while (utf8_isCont(row->chars[bufr->cx]));
		editorRowDelChar(bufr, row, bufr->cx);
	} else {
		editorUndoBackSpace(bufr, '\n');
		bufr->cx = bufr->row[bufr->cy-1].size;
		editorRowAppendString(bufr, &bufr->row[bufr->cy-1], row->chars,
				      row->size);
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
		for (int j = row->size-1; j >= bufr->cx; j--) {
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
	for (int j = bufr->cx-1; j >= 0; j--) {
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
		p+=bufr->row[j].size;
		*p = '\n';
		p++;
	}

	return buf;
}

void editorOpen(struct editorBuffer *bufr, char *filename) {
	free(bufr->filename);
	bufr->filename = strdup(filename);
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
		editorInsertRow(bufr, bufr->numrows, line, linelen);
	}
	free(line);
	fclose(fp);
	bufr->dirty = 0;
}

void editorSave(struct editorBuffer *bufr) {
	if (bufr->filename==NULL) {
		bufr->filename = editorPrompt(bufr, "Save as: %s", NULL);
		if (bufr->filename==NULL) {
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

void editorSetScxScy(struct editorBuffer *bufr, int screenrows, int screencols) {
	erow *row = &bufr->row[bufr->cy];
	int i;

start:
	i = bufr->rowoff;
	bufr->scy = 0;
	bufr->scx = 0;
	while (i < bufr->cy) {
		bufr->scy+=(bufr->row[i].renderwidth / screencols);
		bufr->scy++;
		i++;
	}

	if (bufr->cy >= bufr->numrows) {
		goto end;
	}

	bufr->scx = 0;
	for (i = 0; i < bufr->cx; i+=(utf8_nBytes(row->chars[i]))) {
		if (row->chars[i] == '\t') {
			bufr->scx += (EMSYS_TAB_STOP - 1) - (bufr->scx % EMSYS_TAB_STOP);
			bufr->scx++;
		} else {
			bufr->scx+=charInStringWidth(row->chars, i);
		}
		if (bufr->scx>=screencols) {
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

void editorDrawRows(struct editorBuffer *bufr, struct abuf *ab, int screenrows, int screencols) {
	int y;
	int filerow = bufr->rowoff;
	bufr->end = 0;
	for (y=0; y<screenrows; y++) {
		if (filerow >= bufr->numrows) {
			bufr->end = 1;
			abAppend(ab, CSI"34m~"CSI"0m", 10);
		} else {
			y += (bufr->row[filerow].renderwidth/screencols);
			abAppend(ab, bufr->row[filerow].render,
					 bufr->row[filerow].rsize);
			filerow++;
		}
		abAppend(ab, "\x1b[K", 3);
		abAppend(ab, CRLF, 2);
	}
}

void editorDrawStatusBar(struct editorWindow *win, struct abuf *ab, int line) {
	/* XXX: It's actually possible for the status bar to end up
	 * outside where it should be, so set it explicitly. */
	char buf[32];
	snprintf(buf, sizeof(buf), CSI"%d;%dH", line, 1);
	abAppend(ab, buf, strlen(buf));

	struct editorBuffer *bufr = win->buf;

	abAppend(ab, "\x1b[7m", 4);
	char status[80];
	int len = 0;
	if (win->focused) {
		len = snprintf(status, sizeof(status),
			       "-- %.20s %c%c %2d:%2d --",
			       bufr->filename ? bufr->filename : "[untitled]",
			       bufr->dirty ? '*': '-', bufr->dirty ? '*': '-',
			       bufr->cy+1, bufr->cx);
	} else {
		len = snprintf(status, sizeof(status),
			       "   %.20s %c%c %2d:%2d   ",
			       bufr->filename ? bufr->filename : "[untitled]",
			       bufr->dirty ? '*': '-', bufr->dirty ? '*': '-',
			       bufr->cy+1, bufr->cx);
	}
#ifdef EMSYS_DEBUG_UNDO
	if (bufr->undo != NULL) {
		len = 0;
		for (len = 0; len < bufr->undo->datalen; len++) {
			status[len] = bufr->undo->data[len];
			if (bufr->undo->data[len] == '\n')
				status[len] = '#';
		}
		status[len++] = '"';
		len += sprintf(&status[len], "sx %d sy %d ex %d ey %d cx %d cy %d",
			bufr->undo->startx,
			bufr->undo->starty,
			bufr->undo->endx,
			bufr->undo->endy,
			bufr->cx,
			bufr->cy);
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
	} else if(bufr->rowoff == 0) {
		perc[1] = 'T';
		perc[2] = 'o';
		perc[3] = 'p';
	} else {
		snprintf(perc, sizeof(perc), " %2d%% --",
			 (bufr->rowoff*100)/bufr->numrows);
	}


	char fill[2] = "-";
	if (!win->focused) {
		perc[5] = ' ';
		perc[6] = ' ';
		fill[0] = ' ';
	}

	if (len > E.screencols) len = E.screencols;
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

	int idx = windowFocusedIdx(&E);
	int windowSize = E.screenrows/E.nwindows;

	if (E.nwindows == 1) {
			struct editorWindow *win = E.windows[0];
			struct editorBuffer *bufr = win->buf;
			editorScroll(bufr, E.screenrows-2, E.screencols);
			editorDrawRows(bufr, &ab, E.screenrows-2, E.screencols);
			editorDrawStatusBar(win, &ab, E.screenrows-1);
	} else {
		for (int i = 0; i < E.nwindows; i++) {
			struct editorWindow *win = E.windows[i];
			struct editorBuffer *bufr = win->buf;
			editorScroll(bufr, windowSize-1, E.screencols);
			editorDrawRows(bufr, &ab, windowSize-1, E.screencols);
			editorDrawStatusBar(win, &ab, ((i+1)*windowSize));
		}
	}
	editorDrawMinibuffer(&ab);

	/* move to scy, scx; show cursor */
	char buf[32];
	snprintf(buf, sizeof(buf), CSI"%d;%dH",
		 E.windows[idx]->buf->scy + 1 + (windowSize*idx),
		 E.windows[idx]->buf->scx + 1);
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

void editorResizeScreen(int UNUSED(sig)) {
	if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
	editorRefreshScreen();
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

uint8_t *editorPrompt(struct editorBuffer *bufr, uint8_t *prompt, void(*callback)(struct editorBuffer *, uint8_t *, int)) {
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
		snprintf(cbuf, sizeof(cbuf), CSI"%d;%ldH", E.screenrows,
			 promptlen + bufwidth + 1);
		write(STDOUT_FILENO, cbuf, strlen(cbuf));

		int c = editorReadKey();
		switch (c) {
		case '\r':
			if (buflen != 0) {
				editorSetStatusMessage("");
				if (callback) callback(bufr, buf, c);
				return buf;
			}
			break;
		case CTRL('g'):
		case CTRL('c'):
			editorSetStatusMessage("");
			if (callback) callback(bufr, buf, c);
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
			break;
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

		if (callback) callback(bufr, buf, c);
	}
}

void editorMoveCursor(struct editorBuffer *bufr, int key) {
	erow *row = (bufr->cy >= bufr->numrows) ? NULL : &bufr->row[bufr->cy];

	switch (key) {
	case ARROW_LEFT:
		if (bufr->cx != 0) {
			do bufr->cx--; while (bufr->cx != 0 && utf8_isCont(row->chars[bufr->cx]));
		} else if (bufr->cy > 0) {
			bufr->cy--;
			bufr->cx = bufr->row[bufr->cy].size;
		}
		break;
	case ARROW_RIGHT:
		if (row && bufr->cx < row->size) {
			bufr->cx+=utf8_nBytes(row->chars[bufr->cx]);
		} else if (row && bufr->cx == row -> size) {
			bufr->cy++;
			bufr->cx=0;
		}
		break;
	case ARROW_UP:
		if (bufr->cy > 0) {
			bufr->cy--;
			while (utf8_isCont(bufr->row[bufr->cy].chars[bufr->cx]))
				bufr->cx++;
		}
		break;
	case ARROW_DOWN:
		if (bufr->cy < bufr->numrows) {
			bufr->cy++;
			if (bufr->cy < bufr->numrows) {
				while (utf8_isCont(bufr->row[bufr->cy].chars[bufr->cx]))
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
			uint8_t c = buf->row[cy].chars[cx-1];
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

void editorUpcaseWord(struct editorConfig *ed, struct editorBuffer *bufr, int times) {
	int icx = bufr->cx;
	int icy = bufr->cy;
	for (int i = 0; i < times; i++) {
		bufferEndOfForwardWord(bufr, &bufr->cx, &bufr->cy);
	}
	bufr->markx = icx;
	bufr->marky = icy;
	editorTransformRegion(ed, bufr, transformerUpcase);
}

void editorDowncaseWord(struct editorConfig *ed, struct editorBuffer *bufr, int times) {
	int icx = bufr->cx;
	int icy = bufr->cy;
	for (int i = 0; i < times; i++) {
		bufferEndOfForwardWord(bufr, &bufr->cx, &bufr->cy);
	}
	bufr->markx = icx;
	bufr->marky = icy;
	editorTransformRegion(ed, bufr, transformerDowncase);
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

void editorTransposeWords(struct editorConfig *ed, struct editorBuffer *bufr) {
	if (bufr->numrows == 0) {
		editorSetStatusMessage("Buffer is empty");
		return;
	}

	if (bufr->cx == 0 && bufr->cy == 0) {
		editorSetStatusMessage("Beginning of buffer");
		return;
	} else if (bufr->cy >= bufr->numrows ||
		   (bufr->cy == bufr->numrows-1 &&
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
		   (bufr->cy == bufr->numrows-1 &&
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

/* Where the magic happens */
void editorProcessKeypress(int c) {
	struct editorBuffer *bufr = E.focusBuf;
	int idx;
	struct editorWindow **windows;

	if (E.micro) {
		if (E.micro == REDO && c == CTRL('_')) {
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
		bufr->rowoff = bufr->cy - ((E.screenrows/E.nwindows)/2);
		if (bufr->rowoff < 0) {
			bufr->rowoff = 0;
		}
		break;
	case QUIT:
		if (bufr->dirty) {
			editorSetStatusMessage(
				"%.20s has unsaved changes, really quit? Y/N",
				bufr->filename ? bufr->filename : "[untitled]");
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
	case CTRL('z'):
		for (int i = 0; i < rept; i++) {
			bufr->cy = bufr->rowoff;
			int times = (E.screenrows/E.nwindows)-4;
			while (times--)
				editorMoveCursor(bufr, ARROW_UP);
		}
		break;
	case PAGE_DOWN:
	case CTRL('v'):
		for (int i = 0; i < rept; i++) {
			bufr->cy = bufr->rowoff + E.screenrows - 1;
			if (bufr->cy > bufr->numrows) bufr->cy = bufr->numrows;
			int times = (E.screenrows/E.nwindows)-4;
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
		if (bufr->row != NULL) {
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
		editorUndoAppendUnicode(&E, bufr);
		editorInsertUnicode(bufr);
		break;
	case SAVE:
		editorSave(bufr);
		break;
	case COPY:
		editorCopyRegion(&E, bufr);
		break;
	case CTRL('@'):
		editorSetMark(bufr);
		break;
	case CTRL('y'):
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
		for (int i = 0; i < rept; i++) {
			editorUndoAppendChar(bufr, c);
			editorInsertChar(bufr, c);
		}
		break;
	case CTRL('_'):
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
		E.focusBuf = E.focusBuf->next;
		if (E.focusBuf == NULL) {
			E.focusBuf = E.firstBuf;
		}
		for (int i = 0; i < E.nwindows; i++) {
			if (E.windows[i]->focused) {
				E.windows[i]->buf = E.focusBuf;
			}
		}
		break;

	case OTHER_WINDOW:
		editorSwitchWindow(&E);
		break;

	case CREATE_WINDOW:
		E.windows = realloc(
			E.windows,sizeof(struct editorWindow *)*(++E.nwindows));
		E.windows[E.nwindows-1] = malloc(sizeof(struct editorWindow));
		E.windows[E.nwindows-1]->focused = 0;
		E.windows[E.nwindows-1]->buf = E.focusBuf;
		break;

	case DESTROY_WINDOW:
		if (E.nwindows == 1) {
			editorSetStatusMessage("Can't kill last window");
			break;
		}
		idx = windowFocusedIdx(&E);
		editorSwitchWindow(&E);
		free(E.windows[idx]);
		windows =
			malloc(sizeof(struct editorWindow *)*(--E.nwindows));
		int j = 0;
		for (int i = 0; i <= E.nwindows; i++) {
			if (i != idx) {
				windows[j++] = E.windows[i];
				if (windows[j]->focused) {
					E.focusBuf = windows[j]->buf;
				}
			}
		}
		free(E.windows);
		E.windows = windows;
		break;

	case DESTROY_OTHER_WINDOWS:
		if (E.nwindows == 1) {
			editorSetStatusMessage("No other windows to delete");
			break;
		}
		idx = windowFocusedIdx(&E);
		windows = malloc(sizeof(struct editorWindow *));
		for (int i = 0; i < E.nwindows; i++) {
			if (i != idx) {
				free(E.windows[i]);
			}
		}
		windows[0] = E.windows[idx];
		windows[0]->focused = 1;
		E.focusBuf = windows[0]->buf;
		E.nwindows = 1;
		free(E.windows);
		E.windows = windows;
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

	case UPCASE_REGION:
		editorTransformRegion(&E, bufr, transformerUpcase);
		break;

	case DOWNCASE_REGION:
		editorTransformRegion(&E, bufr, transformerDowncase);
		break;

	case TRANSPOSE_WORDS:
		editorTransposeWords(&E, bufr);
		break;

	case CTRL('t'):
		editorTransposeChars(&E, bufr);
		break;

	case EXEC_CMD:;
		uint8_t *cmd = editorPrompt(bufr, "cmd: %s", NULL);
		if (cmd != NULL) {
			runCommand(cmd, &E, bufr);
			free(cmd);
		}
		break;

	default:
		if (ISCTRL(c)) {
			editorSetStatusMessage("Unknown command C-%c", c|0x60);
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
	struct editorBuffer *ret = malloc(sizeof (struct editorBuffer));
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
	ret->dirty = 0;
	ret->undo = NULL;
	ret->redo = NULL;
	ret->next = NULL;
	ret->uarg = 0;
	ret->uarg_active = 0;
	return ret;
}

void initEditor() {
	E.minibuffer[0] = 0;
	E.kill = NULL;
	E.windows = malloc(sizeof(struct editorWindow *)*1);
	E.windows[0] = malloc(sizeof(struct editorWindow));
	E.windows[0]->focused = 1;
	E.nwindows = 1;
	E.recording = 0;
	E.macro.nkeys = 0;
	E.macro.keys = NULL;
	E.micro = 0;
	setupCommands(&E);

	if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main(int argc, char *argv[]) {
	enableRawMode();
	initEditor();
	if (argc >= 2) {
		int i = 1;
		int linum = -1;
		E.focusBuf = NULL;
		if (argv[1][0] == '+' && argc > 2) {
			linum = atoi(argv[1]+1);
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
	} else {
		E.firstBuf = newBuffer();
		E.focusBuf = E.firstBuf;
	}
	E.windows[0]->buf = E.focusBuf;

	editorSetStatusMessage("emsys "EMSYS_VERSION" - C-x C-c to quit");
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
				E.macro.keys = malloc(
					E.macro.skeys * sizeof (int));
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
		} else if (c == MACRO_EXEC || (E.micro == MACRO_EXEC &&
					       (c == 'e' || c == 'E'))) {
			if (E.recording) {
				editorSetStatusMessage(
					"Keyboard macro defined");
				E.recording = 0;
			}
			for (int idx = 0; idx < E.macro.nkeys; idx++) {
				editorProcessKeypress(E.macro.keys[idx]);
			}
			E.micro = MACRO_EXEC;
		} else {
			if (E.recording) {
				E.macro.keys[E.macro.nkeys++] = c;
				if (E.macro.nkeys>=E.macro.skeys) {
					E.macro.skeys *= 2;
					E.macro.keys = realloc(
						E.macro.keys,
						E.macro.skeys * sizeof (int));
				}
			}
			editorProcessKeypress(c);
		}
	}
	return 0;
}
