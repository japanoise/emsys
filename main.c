#include<errno.h>
#include<signal.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/ioctl.h>
#include<termios.h>
#include<unistd.h>

/*** util ***/

#define ESC "\033"
#define CSI ESC"["
#define CRLF "\r\n"
#define ISCTRL(c) ((0 < c && c < 0x20) || c == 0x7f)
#define CTRL_KEY(c) ((c)&0x1f)
#define PANIC CTRL_KEY('p')

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

struct editorConfig {
	int screenrows;
	int screencols;
	struct termios orig_termios;
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
	char c;
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if (nread == -1 && errno != EAGAIN) die("read");
	}
	if (c == PANIC) {
		errno = EINTR;
		die("Panic key");
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

void editorDrawRows(struct abuf *ab) {
	int y;
	for (y=0; y<E.screenrows; y++) {
		abAppend(ab, CSI"34m~"CSI"0m", 10);

		abAppend(ab, "\x1b[K", 3);
		if (y<E.screenrows - 1) {
			abAppend(ab, CRLF, 2);
		}
	}
}

void editorRefreshScreen() {
	struct abuf ab = ABUF_INIT;

	/* Hide cursor and move to 1,1 */
	abAppend(&ab, CSI"?25l", 6);
	abAppend(&ab, CSI"H", 3);

	editorDrawRows(&ab);

	abAppend(&ab, CSI"H", 3);

	/* show cursor */
	abAppend(&ab, CSI"?25h", 6);

	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

void editorResizeScreen(int sig) {
	if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
	editorRefreshScreen();
}

/*** input ***/

/* Where the magic happens */
void editorProcessKeypress() {
	int c = editorReadKey();

	switch (c) {
		case CTRL('q'):
			exit(0);
			break;
	}
}

/*** init ***/

void initEditor() {
	if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main(int argc, char *argv[]) {
	enableRawMode();
	initEditor();
	signal (SIGWINCH, editorResizeScreen);

	char c;
	while (1) {
		editorRefreshScreen();
		editorProcessKeypress();
	}
	return 0;
}

