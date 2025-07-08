#include "display.h"
#include "emsys.h"
#include "terminal.h"
#include "unicode.h"
#include "unused.h"
#include "region.h"
#include "buffer.h"
#include "util.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>

extern struct editorConfig E;
extern void updateRow(erow *row);

const int minibuffer_height = 1;
const int statusbar_height = 1;

/* Append buffer implementation */
void abAppend(struct abuf *ab, const char *s, int len) {
	if (ab->len + len > ab->capacity) {
		int new_capacity = ab->capacity == 0 ? 1024 : ab->capacity * 2;
		while (new_capacity < ab->len + len) {
			if (new_capacity > INT_MAX / 2) {
				die("buffer size overflow");
			}
			new_capacity *= 2;
		}
		ab->b = xrealloc(ab->b, new_capacity);
		ab->capacity = new_capacity;
	}
	memcpy(&ab->b[ab->len], s, len);
	ab->len += len;
}

void abFree(struct abuf *ab) {
	free(ab->b);
}

/* Check if a render position is within the marked region */
static int isRenderPosInRegion(struct editorBuffer *buf, int row,
			       int render_pos) {
	if (markInvalidSilent())
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
		return 1; /* Entire middle row is in region */
	}
}

/* Check if a render position is at the current search match */
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

/* Calculate number of rows to scroll for smooth scrolling */
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

/* Render a line with highlighting support */
static void renderLineWithHighlighting(erow *row, struct abuf *ab,
				       int start_col, int end_col,
				       struct editorBuffer *buf, int filerow) {
	int render_x = 0;
	int char_idx = 0;
	int current_highlight = 0;

	/* Skip to start column */
	while (char_idx < row->size && render_x < start_col) {
		if (row->chars[char_idx] < 0x80 &&
		    !ISCTRL(row->chars[char_idx])) {
			render_x += 1;
			char_idx++;
		} else {
			render_x = nextScreenX(row->chars, &char_idx, render_x);
			char_idx++;
		}
	}

	/* Render visible portion */
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
				abAppend(ab, "\x1b[7m", 4); /* Reverse video */
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

/* Window management functions */
int windowFocusedIdx(void) {
	for (int i = 0; i < E.nwindows; i++) {
		if (E.windows[i]->focused) {
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

void editorSwitchWindow(void) {
	if (E.nwindows == 1) {
		editorSetStatusMessage("No other windows to select");
		return;
	}

	int currentIdx = windowFocusedIdx();
	struct editorWindow *currentWindow = E.windows[currentIdx];
	struct editorBuffer *currentBuffer = currentWindow->buf;

	// Store the current buffer's cursor position in the current window
	currentWindow->cx = currentBuffer->cx;
	currentWindow->cy = currentBuffer->cy;

	// Switch to the next window
	currentWindow->focused = 0;
	int nextIdx = (currentIdx + 1) % E.nwindows;
	struct editorWindow *nextWindow = E.windows[nextIdx];
	nextWindow->focused = 1;

	// Update the focused buffer
	E.buf = nextWindow->buf;

	// Set the buffer's cursor position from the new window
	E.buf->cx = nextWindow->cx;
	E.buf->cy = nextWindow->cy;

	// Synchronize the buffer's cursor with the new window's cursor
	synchronizeBufferCursor(E.buf, nextWindow);
}

/* Display functions */
void setScxScy(struct editorWindow *win) {
	struct editorBuffer *buf = win->buf;
	erow *row = (buf->cy >= buf->numrows) ? NULL : &buf->row[buf->cy];

	win->scy = 0;
	win->scx = 0;

	if (!buf->truncate_lines) {
		if (buf->cy >= buf->numrows) {
			// For virtual line, calculate position as if there's a line at buf->numrows
			if (buf->numrows > 0) {
				int virtual_screen_line = getScreenLineForRow(
					buf, buf->numrows - 1);
				// Add one line for the virtual line position
				virtual_screen_line +=
					((calculateLineWidth(
						  &buf->row[buf->numrows - 1]) /
					  E.screencols) +
					 1);
				int rowoff_screen_line =
					getScreenLineForRow(buf, win->rowoff);
				win->scy = virtual_screen_line -
					   rowoff_screen_line;
			} else {
				// Empty buffer case - virtual line is at position 0
				win->scy = 0 - win->rowoff;
			}
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

	int total_width = charsToDisplayColumn(row, buf->cx);

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

void scroll(void) {
	struct editorWindow *win = E.windows[windowFocusedIdx()];
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
				if (buf->cy == buf->numrows) {
					visible_rows = 1;
				}
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
			erow *row = &buf->row[buf->cy];
			for (int j = 0; j < buf->cx;) {
				if (j < row->size) {
					if (row->chars[j] == '\t') {
						rx += EMSYS_TAB_STOP -
						      (rx % EMSYS_TAB_STOP);
					} else {
						int char_width =
							charInStringWidth(
								row->chars, j);
						rx += char_width;
					}
					j += utf8_nBytes(row->chars[j]);
				} else {
					break;
				}
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

	setScxScy(win);
}

void drawRows(struct editorWindow *win, struct abuf *ab, int screenrows,
	      int screencols) {
	struct editorBuffer *buf = win->buf;
	int y;
	int filerow = win->rowoff;

	for (y = 0; y < screenrows; y++) {
		if (filerow >= buf->numrows) {
			abAppend(ab, CSI "34m~" CSI "0m", 10);
		} else {
			erow *row = &buf->row[filerow];
			if (!row->render_valid) {
				updateRow(row);
			}
			if (buf->truncate_lines) {
				// Truncated mode with visual marking
				renderLineWithHighlighting(
					row, ab, win->coloff,
					win->coloff + screencols, buf, filerow);
				filerow++;
			} else {
				// Wrapped mode with visual marking support
				int render_x = 0;
				int char_idx = 0;
				int current_highlight = 0;
				int line_start_render_x = 0;

				while (char_idx < row->size && y < screenrows) {
					// Track start of current screen line
					line_start_render_x = render_x;

					// Render one screen line worth of content
					while (char_idx < row->size &&
					       render_x - line_start_render_x <
						       screencols) {
						uint8_t c =
							row->chars[char_idx];

						int in_region =
							isRenderPosInRegion(
								buf, filerow,
								render_x);
						int is_current_match =
							isRenderPosCurrentSearchMatch(
								buf, filerow,
								render_x);
						int new_highlight =
							(in_region ||
							 is_current_match) ?
								1 :
								0;

						if (new_highlight !=
						    current_highlight) {
							if (current_highlight >
							    0) {
								abAppend(
									ab,
									"\x1b[0m",
									4);
							}
							if (new_highlight ==
							    1) {
								abAppend(
									ab,
									"\x1b[7m",
									4); /* Reverse video */
							}
							current_highlight =
								new_highlight;
						}

						if (c == '\t') {
							int next_tab_stop =
								(render_x +
								 EMSYS_TAB_STOP) /
								EMSYS_TAB_STOP *
								EMSYS_TAB_STOP;
							int tab_end =
								next_tab_stop;
							if (tab_end -
								    line_start_render_x >
							    screencols) {
								tab_end =
									line_start_render_x +
									screencols;
							}
							while (render_x <
							       tab_end) {
								// Check highlighting for each space in tab
								int space_in_region = isRenderPosInRegion(
									buf,
									filerow,
									render_x);
								int space_is_match = isRenderPosCurrentSearchMatch(
									buf,
									filerow,
									render_x);
								int space_highlight =
									(space_in_region ||
									 space_is_match) ?
										1 :
										0;
								if (space_highlight !=
								    current_highlight) {
									if (current_highlight >
									    0) {
										abAppend(
											ab,
											"\x1b[0m",
											4);
									}
									if (space_highlight ==
									    1) {
										abAppend(
											ab,
											"\x1b[7m",
											4);
									}
									current_highlight =
										space_highlight;
								}
								abAppend(ab,
									 " ",
									 1);
								render_x++;
							}
						} else if (ISCTRL(c)) {
							abAppend(ab, "^", 1);
							if (c == 0x7f) {
								abAppend(ab,
									 "?",
									 1);
							} else {
								char sym = c |
									   0x40;
								abAppend(ab,
									 &sym,
									 1);
							}
							render_x += 2;
						} else {
							int width = charInStringWidth(
								row->chars,
								char_idx);
							int bytes =
								utf8_nBytes(c);
							abAppend(
								ab,
								(char *)&row->chars
									[char_idx],
								bytes);
							render_x += width;
						}

						char_idx += utf8_nBytes(
							row->chars[char_idx]);
					}

					// Fill rest of line with highlighted spaces if in region
					while (render_x - line_start_render_x <
					       screencols) {
						int space_in_region =
							isRenderPosInRegion(
								buf, filerow,
								render_x);
						int space_is_match =
							isRenderPosCurrentSearchMatch(
								buf, filerow,
								render_x);
						int space_highlight =
							(space_in_region ||
							 space_is_match) ?
								1 :
								0;
						if (space_highlight !=
						    current_highlight) {
							if (current_highlight >
							    0) {
								abAppend(
									ab,
									"\x1b[0m",
									4);
							}
							if (space_highlight ==
							    1) {
								abAppend(
									ab,
									"\x1b[7m",
									4);
							}
							current_highlight =
								space_highlight;
						}
						abAppend(ab, " ", 1);
						render_x++;
					}

					// Reset highlighting at end of screen line
					if (current_highlight > 0) {
						abAppend(ab, "\x1b[0m", 4);
						current_highlight = 0;
					}

					// Move to next screen line if there's more content
					if (char_idx < row->size &&
					    y < screenrows - 1) {
						abAppend(ab, "\r\n", 2);
						y++;
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

void drawStatusBar(struct editorWindow *win, struct abuf *ab, int line) {
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
			       "-- %.20s %c%c%c %2d:%2d --",
			       bufr->filename ? bufr->filename : "*scratch*",
			       bufr->dirty ? '*' : '-', bufr->dirty ? '*' : '-',
			       bufr->read_only ? '%' : ' ', bufr->cy + 1,
			       bufr->cx);
	} else {
		len = snprintf(status, sizeof(status),
			       "   %.20s %c%c%c %2d:%2d   ",
			       bufr->filename ? bufr->filename : "*scratch*",
			       bufr->dirty ? '*' : '-', bufr->dirty ? '*' : '-',
			       bufr->read_only ? '%' : ' ', win->cy + 1,
			       win->cx);
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

void drawMinibuffer(struct abuf *ab) {
	abAppend(ab, "\x1b[K", 3);

	// Show prefix first if active
	if (E.prefix_display[0]) {
		abAppend(ab, E.prefix_display, strlen(E.prefix_display));
	}

	// Then show message
	int msglen = strlen(E.statusmsg);
	if (msglen > E.screencols)
		msglen = E.screencols;
	if (msglen && time(NULL) - E.statusmsg_time < 5) {
		if (E.buf->query && !E.buf->match) {
			abAppend(ab, "\x1b[91m", 5);
		}
		abAppend(ab, E.statusmsg, msglen);
		abAppend(ab, "\x1b[0m", 4);
	}
}

void refreshScreen(void) {
	struct abuf ab = ABUF_INIT;
	abAppend(&ab, "\x1b[?25l", 6); // Hide cursor
	write(STDOUT_FILENO, ab.b, ab.len);
	ab.len = 0;
	abAppend(&ab, "\x1b[H", 3); // Move cursor to top-left corner

	int focusedIdx = windowFocusedIdx();

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
			scroll();
		drawRows(win, &ab, win->height, E.screencols);
		cumulative_height += win->height + statusbar_height;
		drawStatusBar(win, &ab, cumulative_height);
	}

	drawMinibuffer(&ab);

	// Clear any remaining lines below content
	abAppend(&ab, "\x1b[J", 3);

	// Position the cursor for the focused window
	struct editorWindow *focusedWin = E.windows[focusedIdx];
	char buf[32];

	int cursor_y = focusedWin->scy + 1; // 1-based index
	for (int i = 0; i < focusedIdx; i++) {
		cursor_y += E.windows[i]->height + statusbar_height;
	}

	// Ensure cursor doesn't go beyond the window's bottom
	if (cursor_y > cumulative_height) {
		struct editorBuffer *buf = focusedWin->buf;
		if (buf->cy >= buf->numrows) {
			cursor_y = cumulative_height;
		} else {
			cursor_y = cumulative_height - statusbar_height;
		}
	}

	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", cursor_y,
		 focusedWin->scx + 1);
	abAppend(&ab, buf, strlen(buf));

	abAppend(&ab, "\x1b[?25h", 6); // Show cursor
	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

void cursorBottomLine(int curs) {
	char cbuf[32];
	snprintf(cbuf, sizeof(cbuf), CSI "%d;%dH", E.screenrows, curs);
	write(STDOUT_FILENO, cbuf, strlen(cbuf));
}

void cursorBottomLineLong(long curs) {
	char cbuf[32];
	snprintf(cbuf, sizeof(cbuf), CSI "%d;%ldH", E.screenrows, curs);
	write(STDOUT_FILENO, cbuf, strlen(cbuf));
}

void editorSetStatusMessage(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
	va_end(ap);
	E.statusmsg_time = time(NULL);
}

void editorResizeScreen(int UNUSED(sig)) {
	if (getWindowSize(&E.screenrows, &E.screencols) == -1)
		die("getWindowSize");
	refreshScreen();
}

void editorCreateWindow(void) {
	E.windows = xrealloc(E.windows,
			     sizeof(struct editorWindow *) * (++E.nwindows));
	E.windows[E.nwindows - 1] = xcalloc(1, sizeof(struct editorWindow));
	E.windows[E.nwindows - 1]->focused = 0;
	E.windows[E.nwindows - 1]->buf = E.buf;
	E.windows[E.nwindows - 1]->cx = E.buf->cx;
	E.windows[E.nwindows - 1]->cy = E.buf->cy;
	E.windows[E.nwindows - 1]->rowoff = 0;
	E.windows[E.nwindows - 1]->coloff = 0;
	E.windows[E.nwindows - 1]->height =
		(E.screenrows - minibuffer_height) / E.nwindows -
		statusbar_height;
}

void editorDestroyWindow(void) {
	if (E.nwindows == 1) {
		editorSetStatusMessage("Can't kill last window");
		return;
	}
	int idx = windowFocusedIdx();
	editorSwitchWindow();
	free(E.windows[idx]);
	struct editorWindow **windows =
		xmalloc(sizeof(struct editorWindow *) * (--E.nwindows));
	int j = 0;
	for (int i = 0; i <= E.nwindows; i++) {
		if (i != idx) {
			windows[j] = E.windows[i];
			if (windows[j]->focused) {
				E.buf = windows[j]->buf;
			}
			j++;
		}
	}
	free(E.windows);
	E.windows = windows;
}

void editorDestroyOtherWindows(void) {
	if (E.nwindows == 1) {
		editorSetStatusMessage("No other windows to delete");
		return;
	}
	int idx = windowFocusedIdx();
	struct editorWindow **windows = xmalloc(sizeof(struct editorWindow *));
	for (int i = 0; i < E.nwindows; i++) {
		if (i != idx) {
			free(E.windows[i]);
		}
	}
	windows[0] = E.windows[idx];
	windows[0]->focused = 1;
	E.buf = windows[0]->buf;
	E.nwindows = 1;
	free(E.windows);
	E.windows = windows;
	refreshScreen();
}

void editorWhatCursor(void) {
	struct editorBuffer *bufr = E.buf;
	int c = 0;

	if (bufr->cy >= bufr->numrows) {
		editorSetStatusMessage("End of buffer");
		return;
	} else if (bufr->row[bufr->cy].size <= bufr->cx) {
		c = (uint8_t)'\n';
	} else {
		c = (uint8_t)bufr->row[bufr->cy].chars[bufr->cx];
	}

	int npoint = 0, point = 0;
	for (int y = 0; y < bufr->numrows; y++) {
		for (int x = 0; x <= bufr->row[y].size; x++) {
			npoint++;
			if (x == bufr->cx && y == bufr->cy) {
				point = npoint;
			}
		}
	}
	int perc = npoint > 0 ? ((point - 1) * 100) / npoint : 0;

	if (c == 127) {
		editorSetStatusMessage("char: ^? (%d #o%03o #x%02X)"
				       " point=%d of %d (%d%%)",
				       c, c, c, point, npoint, perc);
	} else if (c < ' ') {
		editorSetStatusMessage("char: ^%c (%d #o%03o #x%02X)"
				       " point=%d of %d (%d%%)",
				       c + 0x40, c, c, c, point, npoint, perc);
	} else {
		editorSetStatusMessage("char: %c (%d #o%03o #x%02X)"
				       " point=%d of %d (%d%%)",
				       c, c, c, c, point, npoint, perc);
	}
}

void recenter(struct editorWindow *win) {
	win->rowoff = win->buf->cy - (win->height / 2);
	if (win->rowoff < 0) {
		win->rowoff = 0;
	}
}

void editorToggleTruncateLines(void) {
	E.buf->truncate_lines = !E.buf->truncate_lines;
	editorSetStatusMessage(E.buf->truncate_lines ?
				       "Truncate long lines enabled" :
				       "Truncate long lines disabled");
}

void editorVersion(void) {
	editorSetStatusMessage("emsys version " EMSYS_VERSION
			       ", built " EMSYS_BUILD_DATE);
}

/* Wrapper for command table */
void editorVersionWrapper(struct editorConfig *UNUSED(ed),
			  struct editorBuffer *UNUSED(buf)) {
	editorVersion();
}

/* Wrapper for command table */
void editorToggleTruncateLinesWrapper(struct editorConfig *UNUSED(ed),
				      struct editorBuffer *UNUSED(buf)) {
	editorToggleTruncateLines();
}
