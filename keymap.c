#include "util.h"

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
#include <limits.h>
#include "emsys.h"
#include "util.h"
#include "fileio.h"
#include "find.h"
#include "pipe.h"
#include "region.h"
#include "register.h"
#include "buffer.h"
#include "completion.h"
#include "transform.h"
#include "undo.h"
#include "unicode.h"
#include "unused.h"
#include "terminal.h"
#include "display.h"
#include "keymap.h"
#include "edit.h"
#include "region.h"
#include "prompt.h"

extern struct editorConfig E;

/* Helper functions for state machine */
void showPrefix(const char *prefix) {
	editorSetStatusMessage("%s", prefix);
}

// Forward declarations for command functions

static int compare_commands(const void *a, const void *b) {
	return strcmp(((struct editorCommand *)a)->key,
		      ((struct editorCommand *)b)->key);
}

void setupCommands(struct editorConfig *ed) {
	static struct editorCommand commands[] = {
		{ "capitalize-region", editorCapitalizeRegion },
		{ "indent-spaces", editorIndentSpaces },
		{ "indent-tabs", editorIndentTabs },
		{ "insert-file", editorInsertFile },
		{ "isearch-forward-regexp", editorRegexFindWrapper },
		{ "kanaya", editorCapitalizeRegion },
		{ "query-replace", editorQueryReplace },
		{ "replace-regexp", editorReplaceRegex },
		{ "replace-string", editorReplaceString },
		{ "revert", editorRevert },
		{ "toggle-truncate-lines", editorToggleTruncateLinesWrapper },
		{ "version", editorVersionWrapper },
		{ "view-register", editorViewRegister },
		{ "whitespace-cleanup", editorWhitespaceCleanup },
#ifdef EMSYS_DEBUG_UNDO
		{ "debug-unpair", debugUnpair },
#endif
	};

	ed->cmd = commands;
	ed->cmd_count = sizeof(commands) / sizeof(commands[0]);

	// Sort the commands array
	qsort(ed->cmd, ed->cmd_count, sizeof(struct editorCommand),
	      compare_commands);
}

void runCommand(char *cmd, struct editorConfig *ed, struct editorBuffer *buf) {
	for (int i = 0; cmd[i]; i++) {
		uint8_t c = cmd[i];
		if ('A' <= c && c <= 'Z') {
			c |= 0x60;
		} else if (c == ' ') {
			c = '-';
		}
		cmd[i] = c;
	}

	struct editorCommand key = { cmd, NULL };
	struct editorCommand *found = bsearch(&key, ed->cmd, ed->cmd_count,
					      sizeof(struct editorCommand),
					      compare_commands);

	if (found) {
		found->cmd(ed, buf);
	} else {
		editorSetStatusMessage("No command found");
	}
}

/*** editor operations ***/

void editorRecordKey(int c) {
	if (E.recording) {
		E.macro.keys[E.macro.nkeys++] = c;
		if (E.macro.nkeys >= E.macro.skeys) {
			if (E.macro.skeys > INT_MAX / 2 ||
			    (size_t)E.macro.skeys >
				    SIZE_MAX / (2 * sizeof(int))) {
				die("buffer size overflow");
			}
			E.macro.skeys *= 2;
			E.macro.keys = xrealloc(E.macro.keys,
						E.macro.skeys * sizeof(int));
		}
		if (c == UNICODE) {
			for (int i = 0; i < E.nunicode; i++) {
				E.macro.keys[E.macro.nkeys++] = E.unicode[i];
				if (E.macro.nkeys >= E.macro.skeys) {
					if (E.macro.skeys > INT_MAX / 2 ||
					    (size_t)E.macro.skeys >
						    SIZE_MAX /
							    (2 * sizeof(int))) {
						die("buffer size overflow");
					}
					E.macro.skeys *= 2;
					E.macro.keys = xrealloc(
						E.macro.keys,
						E.macro.skeys * sizeof(int));
				}
			}
		}
	}
}

/*** append buffer ***/

/*** output ***/

/*** input ***/

/* Command execution with prefix state machine */
void executeCommand(int key) {
	static enum PrefixState prefix = PREFIX_NONE;
	/* Handle prefix state transitions and commands */
	switch (key) {
	case CTRL('x'):
#ifdef EMSYS_CUA
		/* CUA mode: if region marked, cut instead of prefix */
		if (E.buf->markx != -1 && E.buf->marky != -1) {
			/* Let the regular processing handle the cut */
			prefix = PREFIX_NONE;
			editorProcessKeypress(CUT);
			return;
		}
#endif
		prefix = PREFIX_CTRL_X;
		showPrefix("C-x ");
		return;

	case CTRL('g'):
		/* Cancel prefix */
		if (prefix != PREFIX_NONE) {
			prefix = PREFIX_NONE;
			editorSetStatusMessage("");
			return;
		}
		/* Otherwise let regular processing handle it */
		break;
	}

	/* Handle commands based on current prefix state */
	if (prefix == PREFIX_CTRL_X) {
		/* Handle C-x prefixed commands */
		prefix = PREFIX_NONE; /* Clear prefix first */

		switch (key) {
		case CTRL('c'):
			editorProcessKeypress(QUIT);
			return;
		case CTRL('s'):
			editorProcessKeypress(SAVE);
			return;
		case CTRL('f'): // Fixed: terminal.c no longer interferes
			findFile();
			return;
		case CTRL('_'):
			editorProcessKeypress(REDO);
			return;
		case CTRL('x'):
			editorProcessKeypress(SWAP_MARK);
			return;
		case 'b':
		case 'B':
		case CTRL('b'):
			editorProcessKeypress(SWITCH_BUFFER);
			return;
		case 'h':
			editorProcessKeypress(MARK_BUFFER);
			return;
		case 'i':
			editorProcessKeypress(INSERT_FILE);
			return;
		case 'o':
		case 'O':
			editorProcessKeypress(OTHER_WINDOW);
			return;
		case '0':
			editorProcessKeypress(DESTROY_WINDOW);
			return;
		case '1':
			editorProcessKeypress(DESTROY_OTHER_WINDOWS);
			return;
		case '2':
			editorProcessKeypress(CREATE_WINDOW);
			return;
		case 'k':
			editorProcessKeypress(KILL_BUFFER);
			return;
		case '(':
			editorProcessKeypress(MACRO_RECORD);
			return;
		case ')':
			editorProcessKeypress(MACRO_END);
			return;
		case 'e':
		case 'E':
			editorProcessKeypress(MACRO_EXEC);
			return;
		case 'z':
		case 'Z':
		case CTRL('z'):
			editorProcessKeypress(SUSPEND);
			return;
		case 'u':
		case 'U':
		case CTRL('u'):
			editorProcessKeypress(UPCASE_REGION);
			return;
		case 'l':
		case 'L':
		case CTRL('l'):
			editorProcessKeypress(DOWNCASE_REGION);
			return;
		case '=':
			editorProcessKeypress(WHAT_CURSOR);
			return;
		case 'x':
			/* Need to read another key for C-x x sequences */
			{
				int nextkey = editorReadKey();
				if (nextkey == 't') {
					editorProcessKeypress(
						TOGGLE_TRUNCATE_LINES);
				} else {
					editorSetStatusMessage(
						"Unknown command C-x x %c",
						nextkey);
				}
			}
			return;
		case 'r':
		case 'R':
			/* C-x r prefix */
			prefix = PREFIX_CTRL_X_R;
			showPrefix("C-x r");
			return;
		case '\x1b': /* ESC - handle arrow keys after C-x */
		{
			int seq[2];
			if (read(STDIN_FILENO, &seq[0], 1) != 1) {
				editorSetStatusMessage(
					"Unknown command C-x ESC");
				return;
			}
			if (read(STDIN_FILENO, &seq[1], 1) != 1) {
				editorSetStatusMessage(
					"Unknown command C-x ESC");
				return;
			}
			if (seq[0] == '[') {
				switch (seq[1]) {
				case 'C': /* Right arrow */
					editorProcessKeypress(NEXT_BUFFER);
					return;
				case 'D': /* Left arrow */
					editorProcessKeypress(PREVIOUS_BUFFER);
					return;
				}
			} else if (seq[0] == 'O') {
				switch (seq[1]) {
				case 'C': /* C-right */
					editorProcessKeypress(NEXT_BUFFER);
					return;
				case 'D': /* C-left */
					editorProcessKeypress(PREVIOUS_BUFFER);
					return;
				}
			}
			editorSetStatusMessage(
				"Unknown command C-x ESC [%c %c]", seq[0],
				seq[1]);
		}
			return;
		case ARROW_LEFT:
			editorProcessKeypress(PREVIOUS_BUFFER);
			return;
		case ARROW_RIGHT:
			editorProcessKeypress(NEXT_BUFFER);
			return;
		default:
			/* Unknown C-x sequence */
			if (key < ' ') {
				editorSetStatusMessage(
					"Unknown command C-x C-%c", key + '`');
			} else {
				editorSetStatusMessage("Unknown command C-x %c",
						       key);
			}
			return;
		}
	} else if (prefix == PREFIX_CTRL_X_R) {
		/* Handle C-x r prefixed commands */
		prefix = PREFIX_NONE; /* Clear prefix first */

		switch (key) {
		case '\x1b': /* ESC for M-w (copy rectangle) */
		{
			int nextkey = editorReadKey();
			if (nextkey == 'W' || nextkey == 'w') {
				editorProcessKeypress(COPY_RECT);
			} else {
				editorSetStatusMessage(
					"Unknown command C-x r ESC %c",
					nextkey);
			}
		}
			return;
		case 'j':
		case 'J':
			editorProcessKeypress(JUMP_REGISTER);
			return;
		case 'a':
		case 'A':
			editorProcessKeypress(MACRO_REGISTER);
			return;
		case 'm':
		case 'M':
			editorToggleRectangleMode();
			return;
		case CTRL('@'):
		case ' ':
			editorProcessKeypress(POINT_REGISTER);
			return;
		case 'n':
		case 'N':
			editorProcessKeypress(NUMBER_REGISTER);
			return;
		case 'r':
		case 'R':
			editorProcessKeypress(RECT_REGISTER);
			return;
		case 's':
		case 'S':
			editorProcessKeypress(REGION_REGISTER);
			return;
		case 't':
		case 'T':
			editorProcessKeypress(STRING_RECT);
			return;
		case '+':
			editorProcessKeypress(INC_REGISTER);
			return;
		case 'i':
		case 'I':
			editorProcessKeypress(INSERT_REGISTER);
			return;
		case 'k':
		case 'K':
		case CTRL('W'):
			editorProcessKeypress(KILL_RECT);
			return;
		case 'v':
		case 'V':
			editorProcessKeypress(VIEW_REGISTER);
			return;
		case 'y':
		case 'Y':
			editorProcessKeypress(YANK_RECT);
			return;
		default:
			/* Unknown C-x r sequence */
			if (key < ' ') {
				editorSetStatusMessage(
					"Unknown command C-x r C-%c",
					key + '`');
			} else {
				editorSetStatusMessage(
					"Unknown command C-x r %c", key);
			}
			return;
		}
	}

	prefix = PREFIX_NONE;
	editorProcessKeypress(key);
}

/* Where the magic happens */
void editorProcessKeypress(int c) {
	// Record key if we're recording a macro (but not the macro commands themselves)
	if (E.recording && c != MACRO_END && c != MACRO_RECORD &&
	    c != MACRO_EXEC) {
		editorRecordKey(c);
	}

	if (c != CTRL('y') && c != YANK_POP
#ifdef EMSYS_CUA
	    && c != CTRL('v')
#endif
	) {
		E.kill_ring_pos = -1;
	}

	int windowIdx = windowFocusedIdx();
	struct editorWindow *win = E.windows[windowIdx];

	if (E.micro) {
#ifdef EMSYS_CUA
		if (E.micro == REDO && (c == CTRL('_') || c == CTRL('z'))) {
#else
		if (E.micro == REDO && c == CTRL('_')) {
#endif //EMSYS_CUA
			editorDoRedo(E.buf, 1);
			return;
		} else {
			E.micro = 0;
		}
	} else {
		E.micro = 0;
	}

	if (ALT_0 <= c && c <= ALT_9) {
		if (!E.uarg) {
			E.uarg = 0;
		}
		E.uarg *= 10;
		E.uarg += c - ALT_0;
		editorSetStatusMessage("uarg: %i", E.uarg);
		return;
	}

#ifdef EMSYS_CU_UARG
	// Handle C-u (Universal Argument)
	if (c == UNIVERSAL_ARGUMENT) {
		if (!E.uarg) {
			E.uarg = 4; // Default value for C-u is 4
		} else {
			E.uarg *= 4; // C-u C-u = 16, C-u C-u C-u = 64, etc.
		}
		editorSetStatusMessage("C-u %d", E.uarg);
		return;
	}

	// Handle numeric input after C-u
	if (E.uarg && c >= '0' && c <= '9') {
		if (E.uarg == 4) { // If it's the first digit after C-u
			E.uarg = c - '0';
		} else {
			E.uarg = E.uarg * 10 + (c - '0');
		}
		editorSetStatusMessage("C-u %d", E.uarg);
		return;
	}
#endif //EMSYS_CU_UARG

	// Handle PIPE_CMD
	if (c == PIPE_CMD) {
		editorPipeCmd(&E, E.buf);
		E.uarg = 0;
		return;
	}

	// Store uarg value and reset it after command execution
	int uarg = E.uarg;

	switch (c) {
	case '\r':
		editorInsertNewline(E.buf, uarg);
		break;
	case BACKSPACE:
	case CTRL('h'):
		editorBackSpace(E.buf, uarg);
		break;
	case DEL_KEY:
	case CTRL('d'):
		editorDelChar(E.buf, uarg);
		break;
	case CTRL('l'):
		recenter(win);
		break;
	case QUIT:
		editorQuit();
		break;
	case ARROW_LEFT:
	case CTRL('b'): /* C-b = backward-char */
		editorMoveCursor(ARROW_LEFT, uarg);
		break;
	case ARROW_RIGHT:
		editorMoveCursor(ARROW_RIGHT, uarg);
		break;
	case CTRL('f'): /* C-f = forward-char (when not after C-x) */
		/* C-f is handled in executeCommand for C-x C-f */
		editorMoveCursor(ARROW_RIGHT, uarg);
		break;
	case ARROW_UP:
	case CTRL('p'): /* C-p = previous-line */
		editorMoveCursor(ARROW_UP, uarg);
		break;
	case ARROW_DOWN:
	case CTRL('n'): /* C-n = next-line */
		editorMoveCursor(ARROW_DOWN, uarg);
		break;
	case PAGE_UP:
#ifndef EMSYS_CUA
	case CTRL('z'):
#endif //EMSYS_CUA
		editorPageUp(uarg);
		break;

	case PAGE_DOWN:
#ifndef EMSYS_CUA
	case CTRL('v'):
#endif //EMSYS_CUA
		editorPageDown(uarg);
		break;
	case BEG_OF_FILE:
		E.buf->cy = 0;
		E.buf->cx = 0;
		break;
	case CUSTOM_INFO_MESSAGE: {
		int winIdx = windowFocusedIdx();
		struct editorWindow *win = E.windows[winIdx];
		struct editorBuffer *buf = win->buf;

		editorSetStatusMessage(
			"(buf->cx%d,cy%d) (win->scx%d,scy%d) win->height=%d screenrows=%d, rowoff=%d",
			buf->cx, buf->cy, win->scx, win->scy, win->height,
			E.screenrows, win->rowoff);
	} break;
	case END_OF_FILE:
		E.buf->cy = E.buf->numrows;
		E.buf->cx = 0;
		break;
	case HOME_KEY:
	case CTRL('a'):
		editorBeginningOfLine(uarg);
		break;
	case END_KEY:
	case CTRL('e'):
		editorEndOfLine(uarg);
		break;
	case CTRL('s'):
		editorFind(E.buf);
		break;
	case REGEX_SEARCH_FORWARD:
		editorRegexFind(E.buf);
		break;
	case REGEX_SEARCH_BACKWARD:
		editorBackwardRegexFind(E.buf);
		break;
	case UNICODE_ERROR:
		editorSetStatusMessage("Bad UTF-8 sequence");
		break;
	case UNICODE:
		editorInsertUnicode(E.buf, uarg);
		break;
#ifdef EMSYS_CUA
	case CUT:
		editorKillRegion(&E, E.buf);
		editorClearMark();
		break;
#endif //EMSYS_CUA
	case SAVE:
		editorSave(E.buf);
		break;
	case COPY:
		editorCopyRegion(&E, E.buf);
		editorClearMark();
		break;
#ifdef EMSYS_CUA
	case CTRL('C'):
		editorCopyRegion(&E, E.buf);
		editorClearMark();
		break;
#endif //EMSYS_CUA
	case CTRL('@'):
		editorSetMark();
		break;
	case CTRL('y'):
#ifdef EMSYS_CUA
	case CTRL('v'):
#endif //EMSYS_CUA
		editorYank(&E, E.buf, uarg ? uarg : 1);
		break;
	case YANK_POP:
		editorYankPop(&E, E.buf);
		break;
	case CTRL('w'):
		editorKillRegion(&E, E.buf);
		editorClearMark();
		break;
	case CTRL('_'):
#ifdef EMSYS_CUA
	case CTRL('z'):
#endif //EMSYS_CUA
		editorDoUndo(E.buf, uarg);
		break;
	case CTRL('k'):
		editorKillLine(uarg);
		break;
#ifndef EMSYS_CU_UARG
	case CTRL('u'):
		editorKillLineBackwards();
		break;
#endif
	case CTRL('j'):
		editorInsertNewlineAndIndent(E.buf, uarg ? uarg : 1);
		break;
	case CTRL('o'):
		editorOpenLine(E.buf, uarg ? uarg : 1);
		break;
	case CTRL('q'):;
		int nread;
		while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
			if (nread == -1 && errno != EAGAIN)
				die("read");
		}
		int count = uarg ? uarg : 1;
		for (int i = 0; i < count; i++) {
			editorUndoAppendChar(E.buf, c);
		}
		editorInsertChar(E.buf, c, count);
		break;
	case FORWARD_WORD:
		editorForwardWord(uarg);
		break;
	case BACKWARD_WORD:
		editorBackWord(uarg);
		break;
	case FORWARD_PARA:
		editorForwardPara(uarg);
		break;
	case BACKWARD_PARA:
		editorBackPara(uarg);
		break;
	case REDO:
		editorDoRedo(E.buf, uarg);
		if (E.buf->redo != NULL) {
			editorSetStatusMessage(
				"Press C-_ or C-/ to redo again");
			E.micro = REDO;
		}
		break;
	case SWITCH_BUFFER:
		editorSwitchToNamedBuffer(&E, E.buf);
		break;
	case NEXT_BUFFER:
		editorNextBuffer();
		break;
	case PREVIOUS_BUFFER:
		editorPreviousBuffer();
		break;
	case MARK_BUFFER:
		editorMarkBuffer();
		break;

	case OTHER_WINDOW:
		if (E.buf == E.minibuf) {
			editorSetStatusMessage(
				"Command attempted to use minibuffer while in minibuffer");
		} else {
			editorSwitchWindow();
		}
		break;

	case CREATE_WINDOW:
		if (E.buf == E.minibuf) {
			editorSetStatusMessage(
				"Command attempted to use minibuffer while in minibuffer");
		} else {
			editorCreateWindow();
		}
		break;

	case DESTROY_WINDOW:
		if (E.buf == E.minibuf) {
			editorSetStatusMessage(
				"Command attempted to use minibuffer while in minibuffer");
		} else {
			editorDestroyWindow(windowFocusedIdx());
		}
		break;

	case DESTROY_OTHER_WINDOWS:
		if (E.buf == E.minibuf) {
			editorSetStatusMessage(
				"Command attempted to use minibuffer while in minibuffer");
		} else {
			editorDestroyOtherWindows();
		}
		break;
	case KILL_BUFFER:
		editorKillBuffer();
		break;

	case SUSPEND:
		raise(SIGTSTP);
		break;

	case DELETE_WORD:
		editorDeleteWord(E.buf, uarg);
		break;
	case BACKSPACE_WORD:
		editorBackspaceWord(E.buf, uarg);
		break;

	case UPCASE_WORD:
		editorUpcaseWord(E.buf, uarg ? uarg : 1);
		break;

	case DOWNCASE_WORD:
		editorDowncaseWord(E.buf, uarg ? uarg : 1);
		break;

	case CAPCASE_WORD:
		editorCapitalCaseWord(E.buf, uarg ? uarg : 1);
		break;

	case UPCASE_REGION:
		editorTransformRegion(&E, E.buf, transformerUpcase);
		break;

	case DOWNCASE_REGION:
		editorTransformRegion(&E, E.buf, transformerDowncase);
		break;
	case TOGGLE_TRUNCATE_LINES:
		editorToggleTruncateLines();
		break;

	case WHAT_CURSOR:
		editorWhatCursor();
		break;

	case TRANSPOSE_WORDS:
		editorTransposeWords(E.buf);
		break;

	case CTRL('t'):
		editorTransposeChars(E.buf);
		break;

	case EXEC_CMD:;
		uint8_t *cmd =
			editorPrompt(E.buf, "cmd: %s", PROMPT_COMMAND, NULL);
		if (cmd != NULL) {
			runCommand(cmd, &E, E.buf);
			free(cmd);
		}
		break;
	case QUERY_REPLACE:
		editorQueryReplace(&E, E.buf);
		break;

	case GOTO_LINE:
		editorGotoLine();
		break;

	case INSERT_FILE:
		editorInsertFile(&E, E.buf);
		break;

	case CTRL('x'):
	case 033:
		/* These take care of their own error messages */
		break;

	case CTRL('g'):
		editorClearMark();
		editorSetStatusMessage("Quit");
		break;

	case UNIVERSAL_ARGUMENT:
		/* Handled before switch statement */
		break;

	case BACKTAB:
		editorUnindent(E.buf, uarg);
		break;

	case SWAP_MARK:
		if (0 <= E.buf->markx &&
		    (0 <= E.buf->marky && E.buf->marky < E.buf->numrows)) {
			int swapx = E.buf->cx;
			int swapy = E.buf->cy;
			E.buf->cx = E.buf->markx;
			E.buf->cy = E.buf->marky;
			E.buf->markx = swapx;
			E.buf->marky = swapy;
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
		editorNumberToRegister(&E, uarg);
		break;
	case REGION_REGISTER:
		editorRegionToRegister(&E, E.buf);
		break;
	case INC_REGISTER:
		editorIncrementRegister(&E, E.buf);
		break;
	case INSERT_REGISTER:
		editorInsertRegister(&E, E.buf);
		break;
	case VIEW_REGISTER:
		editorViewRegister(&E, E.buf);
		break;

	case STRING_RECT:
		editorStringRectangle(&E, E.buf);
		break;

	case COPY_RECT:
		editorCopyRectangle(&E, E.buf);
		editorClearMark();
		break;

	case KILL_RECT:
		editorKillRectangle(&E, E.buf);
		editorClearMark();
		break;

	case YANK_RECT:
		editorYankRectangle(&E, E.buf);
		break;

	case RECT_REGISTER:
		editorRectRegister(&E, E.buf);
		break;

	case EXPAND:
		editorCompleteWord(&E, E.buf);
		break;

	case MACRO_RECORD:
		if (!E.recording) {
			E.recording = 1;
			E.macro.nkeys = 0;
			E.macro.skeys = 32; // Initial size
			if (E.macro.keys) {
				free(E.macro.keys);
			}
			E.macro.keys = xmalloc(E.macro.skeys * sizeof(int));
			editorSetStatusMessage("Recording macro...");
		} else {
			editorSetStatusMessage("Already recording");
		}
		break;

	case MACRO_END:
		if (E.recording) {
			E.recording = 0;
			editorSetStatusMessage("Macro recorded (%d keys)",
					       E.macro.nkeys);
		} else {
			editorSetStatusMessage("Not recording");
		}
		break;

	case MACRO_EXEC:
		if (E.macro.nkeys > 0) {
			for (int i = 0; i < (uarg ? uarg : 1); i++) {
				editorExecMacro(&E.macro);
			}
		} else {
			editorSetStatusMessage("No macro recorded");
		}
		break;

	default:
		if (c == '\t') {
			// Handle TAB character
			// In minibuffer, tab should NOT be processed here
			// It's handled by the prompt function for completion
			if (E.buf == E.minibuf) {
				// Do nothing - let prompt handle it
				break;
			}
			int count = uarg ? uarg : 1;
			for (int i = 0; i < count; i++) {
				editorUndoAppendChar(E.buf, '\t');
			}
			editorInsertChar(E.buf, '\t', count);
		} else if (ISCTRL(c)) {
			editorSetStatusMessage("Unknown command C-%c",
					       c | 0x60);
		} else {
			int count = uarg ? uarg : 1;
			for (int i = 0; i < count; i++) {
				editorUndoAppendChar(E.buf, c);
			}
			editorInsertChar(E.buf, c, count);
		}
		break;
	}

	// Always reset universal argument after command execution
	E.uarg = 0;
}

/*** init ***/

void editorExecMacro(struct editorMacro *macro) {
	const int MAX_MACRO_DEPTH = 100;
	if (E.macro_depth >= MAX_MACRO_DEPTH) {
		editorSetStatusMessage("Macro recursion depth exceeded");
		return;
	}

	E.macro_depth++;

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

	E.macro_depth--;
}
