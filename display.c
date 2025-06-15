#include "display.h"
#include "emsys.h"
#include "terminal.h"
#include "unicode.h"
#include "unused.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

extern struct editorConfig E;

const int minibuffer_height = 1;
const int statusbar_height = 1;

/* Append buffer implementation */
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

/* Window management functions */
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
	// Ensure the cursor is within the buffer's bounds
	if (win->cy >= buf->numrows) {
		win->cy = buf->numrows > 0 ? buf->numrows - 1 : 0;
	}
	if (win->cy < buf->numrows && win->cx > buf->row[win->cy].size) {
		win->cx = buf->row[win->cy].size;
	}

	// Update the buffer's cursor position
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

	// Store the current buffer's cursor position in the current window
	currentWindow->cx = currentBuffer->cx;
	currentWindow->cy = currentBuffer->cy;

	// Switch to the next window
	currentWindow->focused = 0;
	int nextIdx = (currentIdx + 1) % ed->nwindows;
	struct editorWindow *nextWindow = ed->windows[nextIdx];
	nextWindow->focused = 1;

	// Update the focused buffer
	ed->focusBuf = nextWindow->buf;

	// Set the buffer's cursor position from the new window
	ed->focusBuf->cx = nextWindow->cx;
	ed->focusBuf->cy = nextWindow->cy;

	// Synchronize the buffer's cursor with the new window's cursor
	synchronizeBufferCursor(ed->focusBuf, nextWindow);
}

/* Display functions */
void editorSetScxScy(struct editorWindow *win) {
	struct editorBuffer *buf = win->buf;
	erow *row = (buf->cy < buf->numrows) ? &buf->row[buf->cy] : NULL;
	int i;

start:
	i = win->rowoff;
	win->scy = 0;
	win->scx = 0;

	if (buf->truncate_lines) {
		// Truncated mode
		win->scy = buf->cy - win->rowoff;
		if (row) {
			for (int j = 0; j < buf->cx; j++) {
				if (row->chars[j] == '\t')
					win->scx += (EMSYS_TAB_STOP - 1) -
						    (win->scx % EMSYS_TAB_STOP);
				win->scx++;
			}
			win->scx -= win->coloff;
		}
	} else {
		// Wrapped mode
		while (i < buf->cy) {
			win->scy += (buf->row[i].renderwidth / E.screencols);
			win->scy++;
			i++;
		}
		if (buf->cy >= buf->numrows) {
			goto end;
		}
		for (i = 0; i < buf->cx; i += utf8_nBytes(row->chars[i])) {
			if (row->chars[i] == '\t') {
				win->scx += (EMSYS_TAB_STOP - 1) -
					    (win->scx % EMSYS_TAB_STOP);
				win->scx++;
			} else {
				win->scx += charInStringWidth(row->chars, i);
			}
			if (win->scx >= E.screencols) {
				win->scx = 0;
				win->scy++;
			}
		}
	}

end:
	if (win->scy >= win->height) {
		win->rowoff++;
		goto start;
	}

	// Ensure cursor is within window bounds
	if (win->scy < 0)
		win->scy = 0;
	if (win->scy >= win->height)
		win->scy = win->height - 1;
	if (win->scx >= E.screencols)
		win->scx = E.screencols - 1;
}

void editorScroll(void) {
	struct editorWindow *win = E.windows[windowFocusedIdx(&E)];
	struct editorBuffer *buf = win->buf;
	if (buf->cy > buf->numrows) {
		buf->cy = buf->numrows;
	}
	if (buf->cy < buf->numrows && buf->cx > buf->row[buf->cy].size) {
		buf->cx = buf->row[buf->cy].size;
	}

	if (buf->truncate_lines) {
		// Truncated mode scrolling
		if (buf->cy < win->rowoff) {
			win->rowoff = buf->cy;
		}
		if (buf->cy >= win->rowoff + win->height) {
			win->rowoff = buf->cy - win->height + 1;
		}
		if (buf->cx < win->coloff) {
			win->coloff = buf->cx;
		}
		if (buf->cx >= win->coloff + E.screencols) {
			win->coloff = buf->cx - E.screencols + 1;
		}
	} else {
		// Wrapped mode scrolling
		if (buf->cy < win->rowoff) {
			win->rowoff = buf->cy;
		}
		// The vertical scrolling for wrapped mode is handled in editorSetScxScy
	}

	editorSetScxScy(win);
}

void editorDrawRows(struct editorWindow *win, struct abuf *ab, int screenrows,
		    int screencols) {
	struct editorBuffer *buf = win->buf;
	int y;
	int filerow = win->rowoff;

	for (y = 0; y < screenrows; y++) {
		if (filerow >= buf->numrows) {
			abAppend(ab, CSI "34m~" CSI "0m", 10);
		} else {
			erow *row = &buf->row[filerow];
			if (buf->truncate_lines) {
				// Truncated mode
				int len = row->rsize - win->coloff;
				if (len < 0)
					len = 0;
				if (len > screencols)
					len = screencols;
				abAppend(ab, &row->render[win->coloff], len);
				filerow++;
			} else {
				// Wrapped mode
				int len = row->rsize;
				int start = 0;
				while (len > 0) {
					int chunk = len > screencols ?
							    screencols :
							    len;
					abAppend(ab, &row->render[start],
						 chunk);
					start += chunk;
					len -= chunk;
					if (len > 0) {
						abAppend(ab, "\r\n", 2);
						y++;
						if (y >= screenrows)
							break;
					}
				}
				filerow++;
			}
		}
		abAppend(ab, "\x1b[K", 3);
		if (y < screenrows - 1) {
			abAppend(ab, "\r\n", 2);
		}
	}
}

void editorDrawStatusBar(struct abuf *ab, struct editorWindow *win) {
	/* XXX: It's actually possible for the status bar to end up
	 * outside where it should be, so set it explicitly. */
	char buf[32];
	int line = 0;
	// Calculate line position based on window position
	for (int i = 0; i < E.nwindows; i++) {
		if (E.windows[i] == win) {
			for (int j = 0; j < i; j++) {
				line += E.windows[j]->height + statusbar_height;
			}
			line += win->height + statusbar_height;
			break;
		}
	}
	snprintf(buf, sizeof(buf), CSI "%d;%dH", line, 1);
	abAppend(ab, buf, strlen(buf));

	struct editorBuffer *bufr = win->buf;

	abAppend(ab, "\x1b[7m", 4);
	char status[80];
	int len = 0;
	if (win->focused) {
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
			       win->cy + 1, win->cx);
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
		if (win->rowoff == 0) {
			perc[1] = 'A';
			perc[2] = 'l';
			perc[3] = 'l';
		} else {
			perc[1] = 'B';
			perc[2] = 'o';
			perc[3] = 't';
		}
	} else if (win->rowoff == 0) {
		perc[1] = 'T';
		perc[2] = 'o';
		perc[3] = 'p';
	} else {
		snprintf(perc, sizeof(perc), " %2d%% --",
			 (win->rowoff * 100) / bufr->numrows);
	}

	char fill[2] = "-";
	if (!win->focused) {
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

void editorRefreshScreen(void) {
	struct abuf ab = ABUF_INIT;
	abAppend(&ab, "\x1b[2J", 4);   // Clear screen
	abAppend(&ab, "\x1b[?25l", 6); // Hide cursor
	abAppend(&ab, "\x1b[H", 3);    // Move cursor to top-left corner

	int focusedIdx = windowFocusedIdx(&E);

	int cumulative_height = 0;
	int total_height = E.screenrows - minibuffer_height -
			   (statusbar_height * E.nwindows);
	int window_height = total_height / E.nwindows;
	int remaining_height = total_height % E.nwindows;

	for (int i = 0; i < E.nwindows; i++) {
		struct editorWindow *win = E.windows[i];
		win->height = window_height;
		if (i == E.nwindows - 1)
			win->height += remaining_height;

		if (win->focused)
			editorScroll();
		editorDrawRows(win, &ab, win->height, E.screencols);
		cumulative_height += win->height + statusbar_height;
		editorDrawStatusBar(&ab, win);
	}

	editorDrawMinibuffer(&ab);

	// Position the cursor for the focused window
	struct editorWindow *focusedWin = E.windows[focusedIdx];
	struct editorBuffer *focusedBuf = focusedWin->buf;
	char buf[32];

	int cursor_y = focusedWin->scy + 1; // 1-based index
	for (int i = 0; i < focusedIdx; i++) {
		cursor_y += E.windows[i]->height + statusbar_height;
	}

	// Ensure cursor doesn't go beyond the window's bottom
	if (cursor_y > cumulative_height) {
		cursor_y = cumulative_height - statusbar_height;
	}

	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", cursor_y,
		 focusedWin->scx + 1);
	abAppend(&ab, buf, strlen(buf));

	// Add back the reverse video effect for search
	if (focusedBuf->query && focusedBuf->match) {
		abAppend(&ab, "\x1b[7m", 4);
		abAppend(&ab, focusedBuf->query, strlen(focusedBuf->query));
		abAppend(&ab, "\x1b[0m", 4);
		abAppend(&ab, buf,
			 strlen(buf)); // Reposition cursor after highlighting
	}

	abAppend(&ab, "\x1b[?25h", 6); // Show cursor
	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

void editorCursorBottomLine(int curs) {
	char cbuf[32];
	snprintf(cbuf, sizeof(cbuf), CSI "%d;%dH", E.screenrows, curs);
	write(STDOUT_FILENO, cbuf, strlen(cbuf));
}

void editorCursorBottomLineLong(long curs) {
	char cbuf[32];
	snprintf(cbuf, sizeof(cbuf), CSI "%d;%ldH", E.screenrows, curs);
	write(STDOUT_FILENO, cbuf, strlen(cbuf));
}

void editorSetStatusMessage(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(E.minibuffer, sizeof(E.minibuffer), fmt, ap);
	va_end(ap);
	E.statusmsg_time = time(NULL);
}

void editorResizeScreen(void) {
	if (getWindowSize(&E.screenrows, &E.screencols) == -1)
		die("getWindowSize");
	editorRefreshScreen();
}

void editorRecenter(struct editorWindow *win) {
	win->rowoff = win->buf->cy - (win->height / 2);
	if (win->rowoff < 0) {
		win->rowoff = 0;
	}
}

void editorToggleTruncateLines(struct editorConfig *UNUSED(ed),
			       struct editorBuffer *buf) {
	buf->truncate_lines = !buf->truncate_lines;
	editorSetStatusMessage(buf->truncate_lines ?
				       "Truncate long lines enabled" :
				       "Truncate long lines disabled");
}

void editorVersion(struct editorConfig *UNUSED(ed),
		   struct editorBuffer *UNUSED(buf)) {
	editorSetStatusMessage("emsys version " EMSYS_VERSION
			       ", built " EMSYS_BUILD_DATE);
}