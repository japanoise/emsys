#include "platform.h"
#include "compat.h"
#include "display.h"
#include "terminal.h"
#include "unicode.h"
#include "unused.h"
#include "region.h"
#include "row.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

extern struct editorConfig E;
extern const int minibuffer_height;
extern const int statusbar_height;

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

static int isRenderPosInRegion(struct editorBuffer *buf, int row,
			       int render_pos) {
	if (markInvalid(buf))
		return 0;

	erow *erow_ptr = &buf->row[row];
	if (!erow_ptr)
		return 0;

	if (buf->rectangle_mode) {
		int top_row = buf->cy < buf->marky ? buf->cy : buf->marky;
		int bottom_row = buf->cy > buf->marky ? buf->cy : buf->marky;
		int left_col = buf->cx < buf->markx ? buf->cx : buf->markx;
		int right_col = buf->cx > buf->markx ? buf->cx : buf->markx;

		if (row < top_row || row > bottom_row)
			return 0;

		int left_render = charsToDisplayColumn(erow_ptr, left_col);
		int right_render = charsToDisplayColumn(erow_ptr, right_col);

		return (render_pos >= left_render && render_pos < right_render);
	} else {
		int start_row = buf->cy < buf->marky ? buf->cy : buf->marky;
		int end_row = buf->cy > buf->marky ? buf->cy : buf->marky;
		int start_col =
			(buf->cy < buf->marky ||
			 (buf->cy == buf->marky && buf->cx <= buf->markx)) ?
				buf->cx :
				buf->markx;
		int end_col =
			(buf->cy > buf->marky ||
			 (buf->cy == buf->marky && buf->cx >= buf->markx)) ?
				buf->cx :
				buf->markx;

		if (row < start_row || row > end_row)
			return 0;

		if (row == start_row && row == end_row) {
			int start_render =
				charsToDisplayColumn(erow_ptr, start_col);
			int end_render =
				charsToDisplayColumn(erow_ptr, end_col);
			return (render_pos >= start_render &&
				render_pos < end_render);
		}
		if (row == start_row) {
			int start_render =
				charsToDisplayColumn(erow_ptr, start_col);
			return (render_pos >= start_render);
		}
		if (row == end_row) {
			int end_render =
				charsToDisplayColumn(erow_ptr, end_col);
			return (render_pos < end_render);
		}
		return 1;
	}
}

static int isRenderPosCurrentSearchMatch(struct editorBuffer *buf, int row,
					 int render_pos) {
	if (!buf->query || !buf->query[0] || !buf->match)
		return 0;
	if (row != buf->cy)
		return 0;

	erow *erow_ptr = &buf->row[row];
	if (!erow_ptr)
		return 0;

	int match_len = strlen((char *)buf->query);
	int start_render = charsToDisplayColumn(erow_ptr, buf->cx);
	int end_render = charsToDisplayColumn(erow_ptr, buf->cx + match_len);

	return (render_pos >= start_render && render_pos < end_render);
}

int calculateRowsToScroll(struct editorBuffer *buf, struct editorWindow *win,
			  int direction) {
	int rendered_lines = 0;
	int rows_to_scroll = 0;
	int start_row = (direction > 0) ? win->rowoff : win->rowoff - 1;

	while (rendered_lines < win->height) {
		if (start_row < 0 || start_row >= buf->numrows)
			break;
		erow *row = &buf->row[start_row];
		int line_height =
			buf->truncate_lines ?
				1 :
				((calculateLineWidth(row) / E.screencols) + 1);
		if (rendered_lines + line_height > win->height && direction < 0)
			break;
		rendered_lines += line_height;
		rows_to_scroll++;
		start_row += direction;
	}

	return rows_to_scroll;
}

void editorSetScxScy(struct editorWindow *win) {
	struct editorBuffer *buf = win->buf;
	erow *row = (buf->cy >= buf->numrows) ? NULL : &buf->row[buf->cy];

	win->scy = 0;
	win->scx = 0;

	if (!buf->truncate_lines) {
		if (buf->cy >= buf->numrows) {
			win->scy = 0;
		} else {
			int cursor_screen_line =
				getScreenLineForRow(buf, buf->cy);
			int rowoff_screen_line =
				getScreenLineForRow(buf, win->rowoff);
			win->scy = cursor_screen_line - rowoff_screen_line;
		}
	} else {
		win->scy = buf->cy - win->rowoff;
	}

	if (buf->cy >= buf->numrows) {
		return;
	}

	int total_width = charsToScreenWidth(row, buf->cx);

	if (buf->truncate_lines) {
		win->scx = total_width - win->coloff;
	} else {
		int render_pos = charsToDisplayColumn(row, buf->cx);
		win->scy += render_pos / E.screencols;
		win->scx = render_pos % E.screencols;
	}

	if (win->scy < 0)
		win->scy = 0;
	if (win->scy >= win->height)
		win->scy = win->height - 1;
	if (win->scx < 0)
		win->scx = 0;
	if (win->scx >= E.screencols)
		win->scx = E.screencols - 1;
}

void editorScroll(void) {
	struct editorWindow *win = E.windows[windowFocusedIdx(&E)];
	struct editorBuffer *buf = win->buf;

	if (buf->cy + 1 > buf->numrows) {
		buf->cy = buf->numrows;
		buf->cx = 0;
	} else if (buf->cx > buf->row[buf->cy].size) {
		buf->cx = buf->row[buf->cy].size;
	}

	if (!buf->truncate_lines) {
		if (buf->cy < win->rowoff) {
			win->rowoff = buf->cy;
		} else {
			int cursor_screen_row = 0;

			for (int i = win->rowoff;
			     i < buf->cy && i < buf->numrows; i++) {
				int line_height =
					(calculateLineWidth(&buf->row[i]) /
					 E.screencols) +
					1;
				cursor_screen_row += line_height;
			}

			if (buf->cy < buf->numrows) {
				erow *row = &buf->row[buf->cy];
				int cursor_x = 0;
				for (int j = 0; j < buf->cx;) {
					int char_width;
					if (j < row->size &&
					    row->chars[j] == '\t') {
						char_width = EMSYS_TAB_STOP -
							     (cursor_x %
							      EMSYS_TAB_STOP);
					} else {
						char_width = charInStringWidth(
							row->chars, j);
					}
					cursor_x += char_width;
					j += utf8_nBytes(row->chars[j]);
				}
				cursor_screen_row += cursor_x / E.screencols;
			}

			if (cursor_screen_row >= win->height) {
				int visible_rows = 0;
				for (int i = buf->cy; i >= 0; i--) {
					if (i < buf->numrows) {
						int line_height =
							(calculateLineWidth(
								 &buf->row[i]) /
							 E.screencols) +
							1;
						if (visible_rows + line_height >
						    win->height) {
							win->rowoff = i + 1;
							break;
						}
						visible_rows += line_height;
					}
					if (i == 0) {
						win->rowoff = 0;
						break;
					}
				}
			}
		}
	} else {
		if (buf->cy < win->rowoff) {
			win->rowoff = buf->cy;
		} else if (buf->cy >= win->rowoff + win->height) {
			win->rowoff = buf->cy - win->height + 1;
		}
	}

	if (buf->truncate_lines) {
		int rx = 0;
		if (buf->cy < buf->numrows) {
			for (int j = 0; j < buf->cx; j++) {
				if (buf->row[buf->cy].chars[j] == '\t')
					rx += (EMSYS_TAB_STOP - 1) -
					      (rx % EMSYS_TAB_STOP);
				rx++;
			}
		}
		if (rx < win->coloff) {
			win->coloff = rx;
		} else if (rx >= win->coloff + E.screencols) {
			win->coloff = rx - E.screencols + 1;
		}
	} else {
		win->coloff = 0;
	}

	editorSetScxScy(win);
}

static void renderLineWithHighlighting(erow *row, struct abuf *ab,
				       int start_col, int end_col,
				       struct editorBuffer *buf, int filerow) {
	int render_x = 0;
	int char_idx = 0;
	int current_highlight = 0;

	while (char_idx < row->size && render_x < start_col) {
		if (row->chars[char_idx] == '\t') {
			render_x = (render_x + EMSYS_TAB_STOP) /
				   EMSYS_TAB_STOP * EMSYS_TAB_STOP;
		} else if (ISCTRL(row->chars[char_idx])) {
			render_x += 2;
		} else if (row->chars[char_idx] < 0x80) {
			render_x += 1;
		} else {
			render_x += charInStringWidth(row->chars, char_idx);
		}
		char_idx += utf8_nBytes(row->chars[char_idx]);
	}

	while (char_idx < row->size && render_x < end_col) {
		uint8_t c = row->chars[char_idx];

		int in_region = isRenderPosInRegion(buf, filerow, render_x);
		int is_current_match =
			isRenderPosCurrentSearchMatch(buf, filerow, render_x);
		int new_highlight = (in_region || is_current_match) ? 1 : 0;

		if (new_highlight != current_highlight) {
			if (current_highlight > 0) {
				abAppend(ab, "\x1b[0m", 4);
			}
			if (new_highlight == 1) {
				abAppend(ab, "\x1b[7m", 4);
			}
			current_highlight = new_highlight;
		}

		if (c == '\t') {
			int next_tab_stop = (render_x + EMSYS_TAB_STOP) /
					    EMSYS_TAB_STOP * EMSYS_TAB_STOP;
			while (render_x < next_tab_stop && render_x < end_col) {
				if (render_x >= start_col) {
					abAppend(ab, " ", 1);
				}
				render_x++;
			}
		} else if (ISCTRL(c)) {
			if (render_x >= start_col) {
				abAppend(ab, "^", 1);
				if (c == 0x7f) {
					abAppend(ab, "?", 1);
				} else {
					char sym = c | 0x40;
					abAppend(ab, &sym, 1);
				}
			}
			render_x += 2;
		} else {
			int width = charInStringWidth(row->chars, char_idx);
			if (render_x >= start_col) {
				int bytes = utf8_nBytes(c);
				abAppend(ab, (char *)&row->chars[char_idx],
					 bytes);
			}
			render_x += width;
		}

		char_idx += utf8_nBytes(row->chars[char_idx]);
	}

	if (current_highlight > 0) {
		abAppend(ab, "\x1b[0m", 4);
	}
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
				renderLineWithHighlighting(
					row, ab, win->coloff,
					win->coloff + screencols, buf, filerow);
				filerow++;
			} else {
				int line_width = calculateLineWidth(row);
				int start_col = 0;

				while (start_col < line_width &&
				       y < screenrows) {
					int end_col = start_col + screencols;
					if (end_col > line_width) {
						end_col = line_width;
					}

					renderLineWithHighlighting(row, ab,
								   start_col,
								   end_col, buf,
								   filerow);

					start_col += screencols;
					if (start_col < line_width) {
						abAppend(ab, "\x1b[K", 3);
						if (y < screenrows - 1) {
							abAppend(ab, "\r\n", 2);
						}
						y++;
					} else {
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
		int max_data_len = sizeof(status) - 40;
		for (len = 0; len < DEBUG_UNDO->datalen && len < max_data_len;
		     len++) {
			status[len] = DEBUG_UNDO->data[len];
			if (DEBUG_UNDO->data[len] == '\n')
				status[len] = '#';
		}
		if (len < sizeof(status) - 1) {
			status[len++] = '"';
		}
		len += snprintf(&status[len], sizeof(status) - len,
				"sx %d sy %d ex %d ey %d cx %d cy %d",
				DEBUG_UNDO->startx, DEBUG_UNDO->starty,
				DEBUG_UNDO->endx, DEBUG_UNDO->endy, bufr->cx,
				bufr->cy);
	}
#endif
#ifdef EMSYS_DEBUG_MACROS
	/* This can get quite wide, you may want to boost the size of status */
	for (int i = 0; i < E.macro.nkeys && len < sizeof(status) - 20; i++) {
		len += snprintf(&status[len], sizeof(status) - len, "%d: %d ",
				i, E.macro.keys[i]);
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
	abAppend(&ab, "\x1b[2J", 4);
	abAppend(&ab, "\x1b[?25l", 6);
	abAppend(&ab, "\x1b[H", 3);

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
		editorDrawStatusBar(win, &ab, cumulative_height);
	}

	editorDrawMinibuffer(&ab);

	struct editorWindow *focusedWin = E.windows[focusedIdx];
	char buf[32];

	int cursor_y = focusedWin->scy + 1;
	for (int i = 0; i < focusedIdx; i++) {
		cursor_y += E.windows[i]->height + statusbar_height;
	}

	if (cursor_y > cumulative_height) {
		cursor_y = cumulative_height - statusbar_height;
	}

	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", cursor_y,
		 focusedWin->scx + 1);
	abAppend(&ab, buf, strlen(buf));

	abAppend(&ab, "\x1b[?25h", 6);
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

void editorResizeScreen(int UNUSED(sig)) {
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