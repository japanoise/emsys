#include "platform.h"
#include "compat.h"
#include "terminal.h"
#include "unicode.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

extern struct editorConfig E;

void disableRawMode(void) {
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
		die("disableRawMode tcsetattr");
	if (write(STDOUT_FILENO, CSI "?1049l", 8) == -1)
		die("disableRawMode write");
}

void enableRawMode(void) {
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

void deserializeUnicode(void) {
	if (E.playback >= E.macro.nkeys)
		return;
	E.unicode[0] = E.macro.keys[E.playback++];
	E.nunicode = utf8_nBytes(E.unicode[0]);
	for (int i = 1; i < E.nunicode; i++) {
		if (E.playback >= E.macro.nkeys)
			return;
		E.unicode[i] = E.macro.keys[E.playback++];
	}
}

/* Raw reading a keypress */
int readKey(void) {
	/* Check if we have a saved key from Meta processing */
	static int saved_key = 0;
	static int standalone_esc_count =
		0; // Declare here to share with ESC_UNKNOWN

	if (saved_key != 0) {
		int key = saved_key;
		saved_key = 0;
		// Reset ESC counter when returning a non-ESC key
		if (key != 033) {
			standalone_esc_count = 0;
		}
		return key;
	}

	if (E.playback && E.playback < E.macro.nkeys) {
		int ret = E.macro.keys[E.playback++];
		if (ret == UNICODE) {
			deserializeUnicode();
		}
		return ret;
	}
	int nread;
	uint8_t c;
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if (nread == -1 && errno != EAGAIN)
			die("read");
	}

	/* Reset ESC counter for non-ESC keys */
	if (c != 033) {
		standalone_esc_count = 0;
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
			/* M-0 through M-9 are special - they're universal argument */
			return ALT_0 + (seq[0] - '0');
		} else {
			/* For Meta combinations (ESC followed by a character),
			   we need to handle this as a two-step process:
			   1. Return ESC to set PREFIX_META state
			   2. Let the next readKey() return the character
			   
			   Check for triple ESC (ESC ESC ESC = C-g) */
			static int esc_count = 0;
			if (seq[0] == 033) {
				esc_count++;
				if (esc_count >=
				    2) { // Already saw 2 ESCs, this is the 3rd
					esc_count = 0;
					return CTRL(
						'g'); // Triple ESC = keyboard-quit
				}
				saved_key =
					033; // Save this ESC for next readKey
				return 033;  // Return ESC to continue the chain
			} else {
				esc_count = 0; // Reset counter if not ESC
			}

			/* Save the character that followed ESC */
			saved_key = seq[0];
			return 033; // Return ESC to set PREFIX_META state
		}

ESC_UNKNOWN:;
		/* This is reached when ESC is pressed alone (timeout on next char)
		   Need to check for triple ESC here too */
		standalone_esc_count++;
		if (standalone_esc_count >= 3) {
			standalone_esc_count = 0;
			return CTRL('g'); // Triple ESC = keyboard-quit
		}

		char seqR[32];
		seqR[0] = 0;
		char buf[8];
		for (int i = 0; seq[i] && strlen(seqR) < sizeof(seqR) - 8;
		     i++) {
			if (seq[i] < ' ') {
				sprintf(buf, "C-%c ", seq[i] + '`');
			} else {
				sprintf(buf, "%c ", seq[i]);
			}
			strncat(seqR, buf, sizeof(seqR) - strlen(seqR) - 1);
		}
		if (strlen(seqR) > 0) {
			setStatusMessage("Unknown command M-%s", seqR);
		}
		return 033;
	} else if (c == CTRL('x')) {
		/* Welcome to Emacs! */
#ifdef EMSYS_CUA
		// CUA mode: if the region is marked, C-x means 'cut' region.
		// Otherwise, proceed.
		if (E.buf->markx != -1 && E.buf->marky != -1) {
			return CUT;
		}
#endif //EMSYS_CUA
		/* Return immediately to let the new keybinding system handle prefix */
		return CTRL('x');
	} else if (c == CTRL('p')) {
		return c;
	} else if (c == CTRL('n')) {
		return c;
	} else if (c == CTRL('b')) {
		return c;
	} else if (c == CTRL('f')) {
		return c;
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