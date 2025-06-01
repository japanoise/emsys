#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>
#include "keybindings.h"
#include "emsys.h"
#include "region.h"
#include "find.h"
#include "command.h"
#include "pipe.h"
#include "register.h"
#include "tab.h"
#include "undo.h"
#include "transform.h"

extern const int page_overlap;
extern void editorMoveCursor(struct editorBuffer *buf, int key);
extern void editorInsertChar(struct editorBuffer *buf, int c);
extern void editorInsertNewline(struct editorBuffer *buf);
extern void editorBackSpace(struct editorBuffer *buf);
extern void editorDelChar(struct editorBuffer *buf);
extern void editorSave(struct editorBuffer *buf);
extern void editorKillLine(struct editorBuffer *buf);
extern void editorOpenLine(struct editorBuffer *buf);
extern void editorRecenter(struct editorWindow *win);
extern void editorForwardWord(struct editorBuffer *buf);
extern void editorBackWord(struct editorBuffer *buf);
extern void editorForwardPara(struct editorBuffer *buf);
extern void editorBackPara(struct editorBuffer *buf);
extern void editorDeleteWord(struct editorBuffer *buf);
extern void editorBackspaceWord(struct editorBuffer *buf);
extern void editorUpcaseWord(struct editorConfig *ed, struct editorBuffer *buf,
			     int times);
extern void editorDowncaseWord(struct editorConfig *ed,
			       struct editorBuffer *buf, int times);
extern void editorCapitalCaseWord(struct editorConfig *ed,
				  struct editorBuffer *buf, int times);
extern void editorTransposeChars(struct editorConfig *ed,
				 struct editorBuffer *buf);
extern void editorTransposeWords(struct editorBuffer *buf);
extern void editorGotoLine(struct editorBuffer *buf);
extern void editorQueryReplace(struct editorConfig *ed,
			       struct editorBuffer *buf);
extern void editorKillLineBackwards(struct editorBuffer *buf);
extern void editorToggleTruncateLines(struct editorConfig *ed,
				      struct editorBuffer *buf);
extern void editorPipeCmd(struct editorConfig *ed, struct editorBuffer *buf);
extern void runCommand(char *cmd, struct editorConfig *ed,
		       struct editorBuffer *buf);
extern void editorCompleteWord(struct editorConfig *ed,
			       struct editorBuffer *buf);
extern void editorUndoAppendChar(struct editorBuffer *buf, uint8_t c);
extern void editorUndoAppendUnicode(struct editorConfig *ed,
				    struct editorBuffer *buf);
extern void editorInsertUnicode(struct editorBuffer *buf);
extern void editorExecMacro(struct editorMacro *macro);
extern int windowFocusedIdx(struct editorConfig *ed);
extern void editorSetStatusMessage(const char *fmt, ...);
extern void editorRefreshScreen();
extern int editorReadKey(struct editorBuffer *buf);
extern void editorRecordKey(int c);
extern void editorOpen(struct editorBuffer *buf, char *filename);
extern struct editorBuffer *newBuffer();
extern void editorStringRectangle(struct editorConfig *ed,
				  struct editorBuffer *buf);
extern int calculateRowsToScroll(struct editorBuffer *buf,
				 struct editorWindow *win, int direction);
extern void editorCopyRectangle(struct editorConfig *ed,
				struct editorBuffer *buf);
extern void editorKillRectangle(struct editorConfig *ed,
				struct editorBuffer *buf);
extern void editorYankRectangle(struct editorConfig *ed,
				struct editorBuffer *buf);
extern void editorSwitchToNamedBuffer(struct editorConfig *ed,
				      struct editorBuffer *buf);
extern void editorSwitchWindow(struct editorConfig *ed);
extern uint8_t *
editorPrompt(struct editorBuffer *buf, uint8_t *prompt, enum promptType t,
	     void (*callback)(struct editorBuffer *, uint8_t *, int));
extern void destroyBuffer(struct editorBuffer *buf);
extern void editorWhatCursor(struct editorBuffer *buf);
extern void editorDescribeKey(struct editorConfig *ed,
			      struct editorBuffer *buf);
extern void editorViewManPage(struct editorConfig *ed,
			      struct editorBuffer *buf);
extern void editorHelpForHelp(struct editorConfig *ed,
			      struct editorBuffer *buf);

extern struct editorConfig E;

static enum {
	PREFIX_NONE,
	PREFIX_CTRL_X,
	PREFIX_CTRL_X_R,
	PREFIX_META
} prefix_state = PREFIX_NONE;

static int is_recording = 0;
static void handle_newline(struct editorConfig *ed, struct editorBuffer *buf,
			   int rept) {
	for (int i = 0; i < rept; i++) {
		editorUndoAppendChar(buf, '\n');
		insertNewline();
	}
}

static void handle_open_line(struct editorConfig *ed, struct editorBuffer *buf,
			     int rept) {
	for (int i = 0; i < rept; i++) {
		openLine();
	}
}

static void handle_beginning_of_line(struct editorConfig *ed,
				     struct editorBuffer *buf, int rept) {
	buf->cx = 0;
}

static void handle_end_of_line(struct editorConfig *ed,
			       struct editorBuffer *buf, int rept) {
	if (buf->cy < buf->numrows)
		buf->cx = buf->row[buf->cy].size;
}

static void handle_forward_char(struct editorConfig *ed,
				struct editorBuffer *buf, int rept) {
	for (int i = 0; i < rept; i++)
		moveCursor(ARROW_RIGHT);
}

static void handle_backward_char(struct editorConfig *ed,
				 struct editorBuffer *buf, int rept) {
	for (int i = 0; i < rept; i++)
		moveCursor(ARROW_LEFT);
}

static void handle_next_line(struct editorConfig *ed, struct editorBuffer *buf,
			     int rept) {
	for (int i = 0; i < rept; i++)
		moveCursor(ARROW_DOWN);
}

static void handle_previous_line(struct editorConfig *ed,
				 struct editorBuffer *buf, int rept) {
	for (int i = 0; i < rept; i++)
		moveCursor(ARROW_UP);
}

static void handle_kill_line(struct editorConfig *ed, struct editorBuffer *buf,
			     int rept) {
	for (int i = 0; i < rept; i++)
		killLine();
}

static void handle_kill_region(struct editorConfig *ed,
			       struct editorBuffer *buf, int rept) {
	editorKillRegion(ed, buf);
	clearMark();
}

static void handle_yank(struct editorConfig *ed, struct editorBuffer *buf,
			int rept) {
	for (int i = 0; i < rept; i++)
		editorYank(ed, buf);
}

static void handle_undo(struct editorConfig *ed, struct editorBuffer *buf,
			int rept) {
	for (int i = 0; i < rept; i++)
		doUndo();
}

static void handle_set_mark(struct editorConfig *ed, struct editorBuffer *buf,
			    int rept) {
	setMark();
}

static void handle_mark_rectangle(struct editorConfig *ed,
				  struct editorBuffer *buf, int rept) {
	editorMarkRectangle(buf);
}

static void handle_isearch_forward(struct editorConfig *ed,
				   struct editorBuffer *buf, int rept) {
	find();
}

static void handle_isearch_forward_regexp(struct editorConfig *ed,
					  struct editorBuffer *buf, int rept) {
	editorRegexFind(buf);
}

static void handle_keyboard_quit(struct editorConfig *ed,
				 struct editorBuffer *buf, int rept) {
	clearMark();
	editorSetStatusMessage("Quit");
}

static void handle_recenter(struct editorConfig *ed, struct editorBuffer *buf,
			    int rept) {
	int winIdx = windowFocusedIdx(ed);
	editorRecenter(ed->windows[winIdx]);
}

static void handle_transpose_chars(struct editorConfig *ed,
				   struct editorBuffer *buf, int rept) {
	for (int i = 0; i < rept; i++)
		editorTransposeChars(ed, buf);
}

static void handle_upcase_word(struct editorConfig *ed,
			       struct editorBuffer *buf, int rept) {
	editorUpcaseWord(ed, buf, rept);
}

static void handle_downcase_word(struct editorConfig *ed,
				 struct editorBuffer *buf, int rept) {
	editorDowncaseWord(ed, buf, rept);
}

static void handle_capitalize_word(struct editorConfig *ed,
				   struct editorBuffer *buf, int rept) {
	editorCapitalCaseWord(ed, buf, rept);
}

static void handle_backward_delete_char(struct editorConfig *ed,
					struct editorBuffer *buf, int rept) {
	for (int i = 0; i < rept; i++)
		backSpace();
}

static void handle_delete_char(struct editorConfig *ed,
			       struct editorBuffer *buf, int rept) {
	for (int i = 0; i < rept; i++)
		delChar();
}

static void handle_quoted_insert(struct editorConfig *ed,
				 struct editorBuffer *buf, int rept) {
	int c;
	while (read(STDIN_FILENO, &c, 1) != 1) {
		if (errno != EAGAIN)
			die("read");
	}
	for (int i = 0; i < rept; i++) {
		editorUndoAppendChar(buf, c);
		insertChar(c);
	}
}

static void handle_tab(struct editorConfig *ed, struct editorBuffer *buf,
		       int rept) {
	for (int i = 0; i < rept; i++) {
		if (buf->indent > 0) {
			int origx = buf->cx;
			for (int j = 0; j < buf->indent; j++) {
				editorUndoAppendChar(buf, ' ');
				insertChar(' ');
			}
			buf->cx = origx + buf->indent;
		} else {
			editorUndoAppendChar(buf, '\t');
			insertChar('\t');
		}
	}
}

static void handle_kill_line_backwards(struct editorConfig *ed,
				       struct editorBuffer *buf, int rept) {
	for (int i = 0; i < rept; i++)
		editorKillLineBackwards(buf);
}

static void handle_suspend(struct editorConfig *ed, struct editorBuffer *buf,
			   int rept) {
	raise(SIGTSTP);
}

static void handle_page_up(struct editorConfig *ed, struct editorBuffer *buf,
			   int rept) {
	int winIdx = windowFocusedIdx(ed);
	struct editorWindow *win = ed->windows[winIdx];

	int page_size = win->height - page_overlap;
	if (page_size < 1)
		page_size = 1;

	if (win->rowoff == 0) {
		buf->cy = 0;
		buf->cx = 0;
		win->rowoff = 0;
		win->coloff = 0;
		return;
	}

	int cursor_in_window = buf->cy - win->rowoff;

	win->rowoff -= page_size;
	if (win->rowoff < 0)
		win->rowoff = 0;

	if (cursor_in_window >= win->height - page_overlap) {
		buf->cy = win->rowoff + win->height - 1;
	} else {
		buf->cy = win->rowoff + win->height - 1;
	}

	validateCursorPosition(buf);
}

static void handle_page_down(struct editorConfig *ed, struct editorBuffer *buf,
			     int rept) {
	int winIdx = windowFocusedIdx(ed);
	struct editorWindow *win = ed->windows[winIdx];

	int page_size = win->height - page_overlap;
	if (page_size < 1)
		page_size = 1;

	int max_rowoff = buf->numrows - win->height;
	if (max_rowoff < 0)
		max_rowoff = 0;

	if (win->rowoff >= max_rowoff) {
		if (buf->numrows > 0) {
			buf->cy = buf->numrows - 1;
			buf->cx = buf->row[buf->cy].size;

			win->rowoff = buf->numrows - win->height;
			if (win->rowoff < 0) {
				win->rowoff = 0;
			}
			win->coloff = 0;
		} else {
			buf->cy = 0;
			buf->cx = 0;
		}
		return;
	}

	int cursor_in_window = buf->cy - win->rowoff;

	win->rowoff += page_size;

	if (win->rowoff > max_rowoff) {
		win->rowoff = max_rowoff;
	}

	if (cursor_in_window < page_overlap) {
		buf->cy = win->rowoff;
	} else {
		buf->cy = win->rowoff;
	}

	if (buf->cy >= buf->numrows) {
		validateCursorY(buf);
		buf->cx = 0;
	} else {
		validateCursorX(buf);
	}
}

static void handle_quit(struct editorConfig *ed, struct editorBuffer *buf,
			int rept) {
	struct editorBuffer *b = ed->firstBuf;
	while (b) {
		if (b->dirty && b->filename && !b->special_buffer) {
			editorSetStatusMessage(
				"There are unsaved changes. Really quit? (y or n)");
			editorRefreshScreen();
			if (editorReadKey(ed->focusBuf) == 'y')
				exit(0);
			editorSetStatusMessage("");
			return;
		}
		b = b->next;
	}
	exit(0);
}

static void handle_unicode(struct editorConfig *ed, struct editorBuffer *buf,
			   int rept) {
	for (int i = 0; i < rept; i++) {
		editorUndoAppendUnicode(ed, buf);
		editorInsertUnicode(buf);
	}
}

static void handle_unicode_error(struct editorConfig *ed,
				 struct editorBuffer *buf, int rept) {
	editorSetStatusMessage("Bad UTF-8 sequence");
}

static void handle_beginning_of_buffer(struct editorConfig *ed,
				       struct editorBuffer *buf, int rept) {
	buf->cy = 0;
	buf->cx = 0;

	int winIdx = windowFocusedIdx(ed);
	ed->windows[winIdx]->rowoff = 0;
	ed->windows[winIdx]->coloff = 0;
}

static void handle_end_of_buffer(struct editorConfig *ed,
				 struct editorBuffer *buf, int rept) {
	if (buf->numrows > 0) {
		buf->cy = buf->numrows - 1;
		buf->cx = buf->row[buf->cy].size;

		int winIdx = windowFocusedIdx(ed);
		struct editorWindow *win = ed->windows[winIdx];
		win->rowoff = buf->numrows - win->height;
		if (win->rowoff < 0) {
			win->rowoff = 0;
		}
		win->coloff = 0;
	} else {
		buf->cy = 0;
		buf->cx = 0;
	}
}

static void handle_forward_word(struct editorConfig *ed,
				struct editorBuffer *buf, int rept) {
	for (int i = 0; i < rept; i++)
		editorForwardWord(buf);
}

static void handle_backward_word(struct editorConfig *ed,
				 struct editorBuffer *buf, int rept) {
	for (int i = 0; i < rept; i++)
		editorBackWord(buf);
}

static void handle_forward_para(struct editorConfig *ed,
				struct editorBuffer *buf, int rept) {
	for (int i = 0; i < rept; i++)
		editorForwardPara(buf);
}

static void handle_backward_para(struct editorConfig *ed,
				 struct editorBuffer *buf, int rept) {
	for (int i = 0; i < rept; i++)
		editorBackPara(buf);
}

static void handle_delete_word(struct editorConfig *ed,
			       struct editorBuffer *buf, int rept) {
	for (int i = 0; i < rept; i++)
		editorDeleteWord(buf);
}

static void handle_backspace_word(struct editorConfig *ed,
				  struct editorBuffer *buf, int rept) {
	for (int i = 0; i < rept; i++)
		editorBackspaceWord(buf);
}

static void handle_upcase_region(struct editorConfig *ed,
				 struct editorBuffer *buf, int rept) {
	editorTransformRegion(ed, buf, transformerUpcase);
}

static void handle_downcase_region(struct editorConfig *ed,
				   struct editorBuffer *buf, int rept) {
	editorTransformRegion(ed, buf, transformerDowncase);
}

static void handle_transpose_words(struct editorConfig *ed,
				   struct editorBuffer *buf, int rept) {
	for (int i = 0; i < rept; i++)
		editorTransposeWords(buf);
}

static void handle_goto_line(struct editorConfig *ed, struct editorBuffer *buf,
			     int rept) {
	editorGotoLine(buf);
}

static void handle_backtab(struct editorConfig *ed, struct editorBuffer *buf,
			   int rept) {
	editorCompleteWord(ed, buf);
}

static void handle_what_cursor(struct editorConfig *ed,
			       struct editorBuffer *buf, int rept) {
	editorSetStatusMessage("What cursor position not implemented");
}

static void handle_expand(struct editorConfig *ed, struct editorBuffer *buf,
			  int rept) {
	editorCompleteWord(ed, buf);
}

static void handle_redo(struct editorConfig *ed, struct editorBuffer *buf,
			int rept) {
	ed->micro = REDO;
}

static void handle_save_buffer(struct editorConfig *ed,
			       struct editorBuffer *buf, int rept) {
	editorSave(buf);
}

static void handle_copy_region(struct editorConfig *ed,
			       struct editorBuffer *buf, int rept) {
	editorCopyRegion(ed, buf);
	clearMark();
}

static void handle_swap_mark(struct editorConfig *ed, struct editorBuffer *buf,
			     int rept) {
	if (buf->markx >= 0 && buf->marky >= 0) {
		int tx = buf->cx, ty = buf->cy;
		buf->cx = buf->markx;
		buf->cy = buf->marky;
		buf->markx = tx;
		buf->marky = ty;
	}
}

static void handle_find_file(struct editorConfig *ed, struct editorBuffer *buf,
			     int rept) {
	uint8_t *prompt =
		editorPrompt(ed->focusBuf, "Find File: %s", PROMPT_FILES, NULL);
	if (!prompt)
		return;

	char expanded[512];
	char *filename = (char *)prompt;
	if (prompt[0] == '~' && prompt[1] == '/') {
		char *home = getenv("HOME");
		if (home) {
			int ret = snprintf(expanded, sizeof(expanded), "%s/%s",
					   home, prompt + 2);
			if (ret >= (int)sizeof(expanded)) {
				editorSetStatusMessage("Path too long");
				free(prompt);
				return;
			}
			filename = expanded;
		}
	}

	struct editorBuffer *b = ed->firstBuf;
	while (b) {
		if (b->filename && strcmp(b->filename, filename) == 0) {
			ed->focusBuf = b;
			ed->windows[windowFocusedIdx(ed)]->buf = b;
			free(prompt);
			return;
		}
		b = b->next;
	}

	struct editorBuffer *newBuf = newBuffer();
	editorOpen(newBuf, filename);
	newBuf->next = ed->focusBuf->next;
	ed->focusBuf->next = newBuf;
	ed->focusBuf = newBuf;
	ed->windows[windowFocusedIdx(ed)]->buf = newBuf;
	free(prompt);
}

static void handle_other_window(struct editorConfig *ed,
				struct editorBuffer *buf, int rept) {
	editorSwitchWindow(ed);
}

static void handle_macro_start(struct editorConfig *ed,
			       struct editorBuffer *buf, int rept) {
	if (ed->recording) {
		editorSetStatusMessage("Already recording.");
	} else {
		ed->macro.nkeys = 0;
		ed->recording = 1;
		editorSetStatusMessage("Recording...");
	}
}

static void handle_macro_end(struct editorConfig *ed, struct editorBuffer *buf,
			     int rept) {
	if (ed->recording) {
		ed->recording = 0;
		editorSetStatusMessage("Recorded %d keys.", ed->macro.nkeys);
	} else {
		editorSetStatusMessage("Not recording.");
	}
}

static void handle_macro_exec(struct editorConfig *ed, struct editorBuffer *buf,
			      int rept) {
	for (int i = 0; i < rept; i++)
		editorExecMacro(&ed->macro);
}

static void handle_toggle_truncate_lines(struct editorConfig *ed,
					 struct editorBuffer *buf, int rept) {
	editorToggleTruncateLines(ed, buf);
}

static void handle_query_replace(struct editorConfig *ed,
				 struct editorBuffer *buf, int rept) {
	editorQueryReplace(ed, buf);
}

static void handle_pipe_cmd(struct editorConfig *ed, struct editorBuffer *buf,
			    int rept) {
	editorPipeCmd(ed, buf);
}

static void handle_switch_buffer(struct editorConfig *ed,
				 struct editorBuffer *buf, int rept) {
	editorSwitchToNamedBuffer(ed, buf);
}

static void handle_string_rectangle(struct editorConfig *ed,
				    struct editorBuffer *buf, int rept) {
	editorStringRectangle(ed, buf);
}

static void handle_copy_rectangle(struct editorConfig *ed,
				  struct editorBuffer *buf, int rept) {
	editorCopyRectangle(ed, buf);
}

static void handle_kill_rectangle(struct editorConfig *ed,
				  struct editorBuffer *buf, int rept) {
	editorKillRectangle(ed, buf);
}

static void handle_yank_rectangle(struct editorConfig *ed,
				  struct editorBuffer *buf, int rept) {
	editorYankRectangle(ed, buf);
}

static void handle_mark_whole_buffer(struct editorConfig *ed,
				     struct editorBuffer *buf, int rept) {
	editorMarkWholeBuffer(buf);
}

static void handle_custom_info(struct editorConfig *ed,
			       struct editorBuffer *buf, int rept) {
	struct editorWindow *win = ed->windows[windowFocusedIdx(ed)];
	char msg[256];
	snprintf(
		msg, sizeof(msg),
		"Win:%d/%d Buf:%s cy:%d cx:%d rowoff:%d coloff:%d height:%d trunc:%d",
		windowFocusedIdx(ed) + 1, ed->nwindows,
		buf->filename ? buf->filename : "*scratch*", buf->cy, buf->cx,
		win->rowoff, win->coloff, win->height, buf->truncate_lines);
	editorSetStatusMessage(msg);
}

static void showCommandsOnEmpty(struct editorBuffer *buf, uint8_t *input,
				int key) {
	// Only show commands on TAB with empty input
	if (key == CTRL('i') && strlen((char *)input) == 0) {
		// Build list of all commands
		char msg[512];
		strcpy(msg, "Commands: ");
		int first = 1;

		for (int i = 0; i < E.cmd_count; i++) {
			// Stop if we're running out of space
			if (strlen(msg) + strlen(E.cmd[i].key) + 3 >
			    sizeof(msg) - 20) {
				strcat(msg, "...");
				break;
			}

			if (!first)
				strcat(msg, ", ");
			strcat(msg, E.cmd[i].key);
			first = 0;
		}

		editorSetStatusMessage("%s", msg);
	}
}

static void handle_execute_extended_command(struct editorConfig *ed,
					    struct editorBuffer *buf,
					    int rept) {
	uint8_t *cmd = editorPrompt(buf, "M-x %s", PROMPT_COMMANDS,
				    showCommandsOnEmpty);
	if (cmd == NULL)
		return;
	runCommand((char *)cmd, ed, buf);
	free(cmd);
}

static void handle_create_window(struct editorConfig *ed,
				 struct editorBuffer *buf, int rept) {
	ed->windows = xrealloc(ed->windows,
			       sizeof(struct editorWindow *) * (++ed->nwindows));
	ed->windows[ed->nwindows - 1] = xmalloc(sizeof(struct editorWindow));
	ed->windows[ed->nwindows - 1]->focused = 0;
	ed->windows[ed->nwindows - 1]->buf = ed->focusBuf;
	ed->windows[ed->nwindows - 1]->rowoff = 0;
	ed->windows[ed->nwindows - 1]->coloff = 0;
	ed->windows[ed->nwindows - 1]->cx = ed->focusBuf->cx;
	ed->windows[ed->nwindows - 1]->cy = ed->focusBuf->cy;
	ed->windows[ed->nwindows - 1]->scx = 0;
	ed->windows[ed->nwindows - 1]->scy = 0;
}

static void handle_destroy_window(struct editorConfig *ed,
				  struct editorBuffer *buf, int rept) {
	if (ed->nwindows == 1) {
		editorSetStatusMessage("Can't delete the only window");
		return;
	}

	int winIdx = windowFocusedIdx(ed);
	free(ed->windows[winIdx]);

	for (int i = winIdx; i < ed->nwindows - 1; i++) {
		ed->windows[i] = ed->windows[i + 1];
	}

	ed->nwindows--;
	ed->windows = xrealloc(ed->windows,
			       sizeof(struct editorWindow *) * ed->nwindows);

	if (winIdx > 0) {
		ed->windows[winIdx - 1]->focused = 1;
	} else {
		ed->windows[0]->focused = 1;
	}
}

static void handle_destroy_other_windows(struct editorConfig *ed,
					 struct editorBuffer *buf, int rept) {
	int winIdx = windowFocusedIdx(ed);
	struct editorWindow *focusedWin = ed->windows[winIdx];

	for (int i = 0; i < ed->nwindows; i++) {
		if (i != winIdx) {
			free(ed->windows[i]);
		}
	}

	ed->windows[0] = focusedWin;
	ed->nwindows = 1;
	ed->windows = xrealloc(ed->windows, sizeof(struct editorWindow *));
}

static void handle_kill_buffer(struct editorConfig *ed,
			       struct editorBuffer *buf, int rept) {
	struct editorBuffer *target = buf;

	if (target->dirty && target->filename && !target->special_buffer) {
		editorSetStatusMessage(
			"Buffer has unsaved changes. Kill anyway? (y or n)");
		editorRefreshScreen();
		int c = editorReadKey(buf);
		if (c != 'y' && c != 'Y') {
			editorSetStatusMessage("");
			return;
		}
		editorSetStatusMessage("");
	}

	int bufferCount = 0;
	struct editorBuffer *b = ed->firstBuf;
	while (b != NULL) {
		bufferCount++;
		b = b->next;
	}

	struct editorBuffer *prev = NULL;
	b = ed->firstBuf;
	while (b != NULL && b != target) {
		prev = b;
		b = b->next;
	}

	if (prev == NULL) {
		ed->firstBuf = target->next;
	} else {
		prev->next = target->next;
	}

	struct editorBuffer *replacement = NULL;
	if (bufferCount == 1) {
		replacement = newBuffer();
		replacement->special_buffer = 1;
		replacement->filename = stringdup("*scratch*");
		ed->firstBuf = replacement;
	} else {
		if (target->next) {
			replacement = target->next;
		} else if (prev) {
			replacement = prev;
		} else {
			replacement = ed->firstBuf;
		}
	}

	for (int i = 0; i < ed->nwindows; i++) {
		if (ed->windows[i]->buf == target) {
			ed->windows[i]->buf = replacement;
		}
	}

	if (ed->focusBuf == target) {
		ed->focusBuf = replacement;
	}

	if (ed->lastVisitedBuffer == target) {
		ed->lastVisitedBuffer = NULL;
	}

	destroyBuffer(target);
}

static void handle_previous_buffer(struct editorConfig *ed,
				   struct editorBuffer *buf, int rept) {
	struct editorBuffer *b = ed->firstBuf;
	struct editorBuffer *prev = NULL;

	while (b != NULL && b != buf) {
		prev = b;
		b = b->next;
	}

	if (prev != NULL) {
		ed->focusBuf = prev;
		ed->windows[windowFocusedIdx(ed)]->buf = prev;
	}
}

static void handle_next_buffer(struct editorConfig *ed,
			       struct editorBuffer *buf, int rept) {
	if (buf->next != NULL) {
		ed->focusBuf = buf->next;
		ed->windows[windowFocusedIdx(ed)]->buf = buf->next;
	}
}

static KeyBinding basic_bindings[] = {
	{ '\r', handle_newline, "newline" },
	{ CTRL('o'), handle_open_line, "open-line" },
	{ CTRL('a'), handle_beginning_of_line, "beginning-of-line" },
	{ CTRL('e'), handle_end_of_line, "end-of-line" },
	{ CTRL('f'), handle_forward_char, "forward-char" },
	{ CTRL('b'), handle_backward_char, "backward-char" },
	{ CTRL('n'), handle_next_line, "next-line" },
	{ CTRL('p'), handle_previous_line, "previous-line" },
	{ CTRL('k'), handle_kill_line, "kill-line" },
	{ CTRL('w'), handle_kill_region, "kill-region" },
	{ CTRL('y'), handle_yank, "yank" },
	{ CTRL('_'), handle_undo, "undo" },
	{ CTRL('@'), handle_set_mark, "set-mark" },
	{ CTRL('s'), handle_isearch_forward, "isearch-forward" },
	{ ISEARCH_FORWARD_REGEXP, handle_isearch_forward_regexp,
	  "isearch-forward-regexp" },
	{ CTRL('g'), handle_keyboard_quit, "keyboard-quit" },
	{ CTRL('l'), handle_recenter, "recenter" },
	{ CTRL('t'), handle_transpose_chars, "transpose-chars" },
	{ CTRL('h'), NULL, "help-prefix" },
	{ CTRL('d'), handle_delete_char, "delete-char" },
	{ CTRL('q'), handle_quoted_insert, "quoted-insert" },
	{ CTRL('i'), handle_tab, "tab" },
	{ CTRL('u'), handle_kill_line_backwards, "kill-line-backwards" },
	{ CTRL('z'), handle_suspend, "suspend-emacs" },
	{ CTRL('x'), NULL, "ctrl-x-prefix" },
	{ 033, NULL, "meta-prefix" },
	{ BACKSPACE, handle_backward_delete_char, "backward-delete-char" },
	{ DEL_KEY, handle_delete_char, "delete-char" },
	{ HOME_KEY, handle_beginning_of_line, "beginning-of-line" },
	{ END_KEY, handle_end_of_line, "end-of-line" },
	{ PAGE_UP, handle_page_up, "scroll-down" },
	{ PAGE_DOWN, handle_page_down, "scroll-up" },
	{ ARROW_LEFT, handle_backward_char, "backward-char" },
	{ ARROW_RIGHT, handle_forward_char, "forward-char" },
	{ ARROW_UP, handle_previous_line, "previous-line" },
	{ ARROW_DOWN, handle_next_line, "next-line" },
	{ QUIT, handle_quit, "quit" },
	{ UNICODE, handle_unicode, "unicode" },
	{ UNICODE_ERROR, handle_unicode_error, "unicode-error" },
	{ BEG_OF_FILE, handle_beginning_of_buffer, "beginning-of-buffer" },
	{ END_OF_FILE, handle_end_of_buffer, "end-of-buffer" },
	{ FORWARD_WORD, handle_forward_word, "forward-word" },
	{ BACKWARD_WORD, handle_backward_word, "backward-word" },
	{ FORWARD_PARA, handle_forward_para, "forward-para" },
	{ BACKWARD_PARA, handle_backward_para, "backward-para" },
	{ DELETE_WORD, handle_delete_word, "delete-word" },
	{ BACKSPACE_WORD, handle_backspace_word, "backward-kill-word" },
	{ UPCASE_WORD, handle_upcase_word, "upcase-word" },
	{ DOWNCASE_WORD, handle_downcase_word, "downcase-word" },
	{ CAPCASE_WORD, handle_capitalize_word, "capitalize-word" },
	{ UPCASE_REGION, handle_upcase_region, "upcase-region" },
	{ DOWNCASE_REGION, handle_downcase_region, "downcase-region" },
	{ TOGGLE_TRUNCATE_LINES, handle_toggle_truncate_lines,
	  "toggle-truncate" },
	{ TRANSPOSE_WORDS, handle_transpose_words, "transpose-words" },
	{ QUERY_REPLACE, handle_query_replace, "query-replace" },
	{ GOTO_LINE, handle_goto_line, "goto-line" },
	{ BACKTAB, handle_backtab, "complete-word" },
	{ WHAT_CURSOR, handle_what_cursor, "what-cursor" },
	{ PIPE_CMD, handle_pipe_cmd, "pipe-command" },
	{ EXPAND, handle_expand, "expand" },
	{ SUSPEND, handle_suspend, "suspend" },
	{ REDO, handle_redo, "redo" },
	{ SAVE, handle_save_buffer, "save-buffer" },
	{ COPY, handle_copy_region, "copy-region" },
	{ CUT, handle_kill_region, "kill-region" },
	{ SWAP_MARK, handle_swap_mark, "swap-mark" },
	{ EXEC_CMD, handle_execute_extended_command,
	  "execute-extended-command" },
};

static KeyBinding ctrl_x_bindings[] = {
	{ CTRL('c'), handle_quit, "save-buffers-kill-emacs" },
	{ CTRL('s'), handle_save_buffer, "save-buffer" },
	{ CTRL('f'), handle_find_file, "find-file" },
	{ CTRL('x'), handle_swap_mark, "exchange-point-and-mark" },
	{ 'b', handle_switch_buffer, "switch-to-buffer" },
	{ 'k', handle_kill_buffer, "kill-buffer" },
	{ CTRL('b'), handle_switch_buffer, "list-buffers" },
	{ 'o', handle_other_window, "other-window" },
	{ '2', handle_create_window, "split-window-vertically" },
	{ '0', handle_destroy_window, "delete-window" },
	{ '1', handle_destroy_other_windows, "delete-other-windows" },
	{ ARROW_LEFT, handle_previous_buffer, "previous-buffer" },
	{ ARROW_RIGHT, handle_next_buffer, "next-buffer" },
	{ 'r', NULL, "ctrl-x-r-prefix" },
	{ '(', handle_macro_start, "start-kbd-macro" },
	{ ')', handle_macro_end, "end-kbd-macro" },
	{ 'e', handle_macro_exec, "call-last-kbd-macro" },
	{ ' ', handle_mark_rectangle, "rectangle-mark-mode" },
	{ STRING_RECT, handle_string_rectangle, "string-rectangle" },
	{ COPY_RECT, handle_copy_rectangle, "copy-rectangle-to-register" },
	{ KILL_RECT, handle_kill_rectangle, "kill-rectangle" },
	{ YANK_RECT, handle_yank_rectangle, "yank-rectangle" },
	{ 'h', handle_mark_whole_buffer, "mark-whole-buffer" },
	{ '?', handle_custom_info, "custom-info" },
};

static KeyBinding meta_bindings[] = {
	{ 'f', handle_forward_word, "forward-word" },
	{ 'b', handle_backward_word, "backward-word" },
	{ 'd', handle_delete_word, "kill-word" },
	{ BACKSPACE, handle_backspace_word, "backward-kill-word" },
	{ '<', handle_beginning_of_buffer, "beginning-of-buffer" },
	{ '>', handle_end_of_buffer, "end-of-buffer" },
	{ 'w', handle_copy_region, "copy" },
	{ CTRL('x'), handle_swap_mark, "exchange-point-and-mark" },
	{ '{', handle_backward_para, "backward-para" },
	{ '}', handle_forward_para, "forward-para" },
	{ 'u', handle_upcase_word, "upcase-word" },
	{ 'l', handle_downcase_word, "downcase-word" },
	{ 'c', handle_capitalize_word, "capitalize-word" },
	{ '%', handle_query_replace, "query-replace" },
	{ 'g', handle_goto_line, "goto-line" },
	{ 't', handle_transpose_words, "transpose-words" },
	{ 'x', handle_execute_extended_command, "execute-extended-command" },
};

KeyTable global_keys = { "global", basic_bindings,
			 sizeof(basic_bindings) / sizeof(basic_bindings[0]) };

KeyTable ctrl_x_keys = { "ctrl-x", ctrl_x_bindings,
			 sizeof(ctrl_x_bindings) / sizeof(ctrl_x_bindings[0]) };

KeyTable meta_keys = { "meta", meta_bindings,
		       sizeof(meta_bindings) / sizeof(meta_bindings[0]) };

KeyTable ctrl_x_r_keys = { "ctrl-x-r", NULL, 0 };

void initKeyBindings(void) {
	prefix_state = PREFIX_NONE;
	is_recording = 0;
}

int getRepeatCount(struct editorBuffer *buf) {
	int rept = 1;
	if (buf->uarg_active) {
		buf->uarg_active = 0;
		rept = buf->uarg;
		buf->uarg = 0;
	}
	return rept;
}

int handleUniversalArgument(int key, struct editorBuffer *buf) {
	if (ALT_0 <= key && key <= ALT_9) {
		if (!buf->uarg_active) {
			buf->uarg_active = 1;
			buf->uarg = 0;
		}
		buf->uarg = buf->uarg * 10 + (key - ALT_0);
		editorSetStatusMessage("uarg: %i", buf->uarg);
		return 1;
	}
#ifdef EMSYS_CU_UARG
	if (key == UNIVERSAL_ARGUMENT) {
		buf->uarg_active = 1;
		buf->uarg = 4;
		editorSetStatusMessage("C-u");
		return 1;
	}
	if (buf->uarg_active && key >= '0' && key <= '9') {
		buf->uarg = (buf->uarg == 4) ? key - '0' :
					       buf->uarg * 10 + (key - '0');
		editorSetStatusMessage("C-u %d", buf->uarg);
		return 1;
	}
#endif
	return 0;
}

static void describeKey(int key) {
	// Build key description
	char keydesc[64];
	if (ISCTRL(key) && key < 32) {
		snprintf(keydesc, sizeof(keydesc), "C-%c", key + '@');
	} else if (key == 127) {
		strcpy(keydesc, "DEL");
	} else if (key < 256) {
		snprintf(keydesc, sizeof(keydesc), "%c", key);
	} else {
		// Special keys
		switch (key) {
		case ARROW_UP:
			strcpy(keydesc, "<up>");
			break;
		case ARROW_DOWN:
			strcpy(keydesc, "<down>");
			break;
		case ARROW_LEFT:
			strcpy(keydesc, "<left>");
			break;
		case ARROW_RIGHT:
			strcpy(keydesc, "<right>");
			break;
		default:
			snprintf(keydesc, sizeof(keydesc), "<%d>", key);
			break;
		}
	}

	// Find binding
	const char *command = "is undefined";

	// Check global keys
	for (int i = 0; i < global_keys.count; i++) {
		if (global_keys.bindings[i].key == key) {
			command = global_keys.bindings[i].name;
			break;
		}
	}

	editorSetStatusMessage("%s %s", keydesc, command);
}

void processKeySequence(int key) {
	struct editorBuffer *buf = E.focusBuf;

	// Handle describe-key mode
	if (E.describe_key_mode) {
		E.describe_key_mode = 0;
		describeKey(key);
		return;
	}

	if (E.micro == REDO) {
#ifdef EMSYS_CUA
		if (key == CTRL('_') || key == CTRL('z')) {
#else
		if (key == CTRL('_')) {
#endif
			editorDoRedo(buf);
			return;
		}
		E.micro = 0;
	}

	if (handleUniversalArgument(key, buf)) {
		return;
	}

	if (key == PIPE_CMD) {
		editorPipeCmd(&E, buf);
		buf->uarg_active = 0;
		return;
	}

	int rept = getRepeatCount(buf);

	if (E.recording && key != CTRL('x')) {
		editorRecordKey(key);
	}

	if (key == CTRL('x') && prefix_state == PREFIX_NONE) {
		prefix_state = PREFIX_CTRL_X;
		strcpy(E.prefix_display, "C-x ");
		return;
	}
	if (key == 033 && prefix_state == PREFIX_NONE) {
		prefix_state = PREFIX_META;
		strcpy(E.prefix_display, "M-");
		return;
	}
	if (key == 'r' && prefix_state == PREFIX_CTRL_X) {
		prefix_state = PREFIX_CTRL_X_R;
		strcpy(E.prefix_display, "C-x r ");
		return;
	}

	// Handle C-h prefix commands
	if (key == CTRL('h') && prefix_state == PREFIX_NONE) {
		int help_key = editorReadKey(buf);
		switch (help_key) {
		case 'k':
			editorDescribeKey(&E, E.focusBuf);
			break;
		case 'b':
		case 'i':
		case 'm':
			editorViewManPage(&E, E.focusBuf);
			break;
		case '?':
		default:
			editorHelpForHelp(&E, E.focusBuf);
			break;
		}
		return;
	}

	KeyTable *table = prefix_state == PREFIX_CTRL_X	  ? &ctrl_x_keys :
			  prefix_state == PREFIX_META	  ? &meta_keys :
			  prefix_state == PREFIX_CTRL_X_R ? &ctrl_x_r_keys :
							    &global_keys;

	KeyBinding *binding = NULL;
	for (int i = 0; i < table->count; i++) {
		if (table->bindings[i].key == key) {
			binding = &table->bindings[i];
			break;
		}
	}

	if (!binding) {
		if (!ISCTRL(key) && key < 256 && prefix_state == PREFIX_NONE) {
			for (int i = 0; i < rept; i++) {
				editorUndoAppendChar(buf, key);
				insertChar(key);
			}
		} else {
			editorSetStatusMessage("Unknown key sequence");
		}
		prefix_state = PREFIX_NONE;
		E.prefix_display[0] = '\0';
		return;
	}

	if (!binding->handler) {
		prefix_state = PREFIX_NONE;
		E.prefix_display[0] = '\0';
		return;
	}

	if (prefix_state != PREFIX_NONE) {
		prefix_state = PREFIX_NONE;
		E.prefix_display[0] = '\0';
	}

	binding->handler(&E, buf, rept);
}