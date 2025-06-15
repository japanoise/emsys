#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

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
#include "emsys.h"
#include "fileio.h"
#include "find.h"
#include "pipe.h"
#include "region.h"
#include "register.h"
#include "buffer.h"
#include "tab.h"
#include "transform.h"
#include "undo.h"
#include "unicode.h"
#include "unused.h"
#include "terminal.h"
#include "display.h"
#include "keymap.h"
#include "util.h"

const int page_overlap = 2;

struct editorConfig E;
void setupHandlers();

/*** output ***/

void editorSuspend(int UNUSED(sig)) {
	signal(SIGTSTP, SIG_DFL);
	disableRawMode();
	raise(SIGTSTP);
}

void editorResume(int UNUSED(sig)) {
	setupHandlers();
	enableRawMode();
	editorResizeScreen();
}

void sigwinchHandler(int UNUSED(sig)) {
	editorResizeScreen();
}

/*** init ***/

void setupHandlers() {
	signal(SIGWINCH, sigwinchHandler);
	signal(SIGCONT, editorResume);
	signal(SIGTSTP, editorSuspend);
}

void initEditor() {
	E.statusmsg[0] = 0;
	E.kill = NULL;
	E.rectKill = NULL;
	E.windows = malloc(sizeof(struct editorWindow *) * 1);
	E.windows[0] = malloc(sizeof(struct editorWindow));
	E.windows[0]->focused = 1;
	E.nwindows = 1;
	E.recording = 0;
	E.macro.nkeys = 0;
	E.macro.keys = NULL;
	E.micro = 0;
	E.playback = 0;
	E.firstBuf = NULL;
	memset(E.registers, 0, sizeof(E.registers));
	setupCommands(&E);
	E.lastVisitedBuffer = NULL;

	if (getWindowSize(&E.screenrows, &E.screencols) == -1)
		die("getWindowSize");
}

int main(int argc, char *argv[]) {
	// Check for --version flag before entering raw mode
	if (argc >= 2 && strcmp(argv[1], "--version") == 0) {
		printf("emsys %s\n", EMSYS_VERSION);
		return 0;
	}

	enableRawMode();
	initEditor();
	E.firstBuf = newBuffer();
	E.buf = E.firstBuf;
	if (argc >= 2) {
		int i = 1;
		int linum = -1;
		if (argv[1][0] == '+' && argc > 2) {
			linum = atoi(argv[1] + 1);
			i++;
		}
		for (; i < argc; i++) {
			E.firstBuf = newBuffer();
			editorOpen(E.firstBuf, argv[i]);
			E.firstBuf->next = E.buf;
			if (linum > 0) {
				E.firstBuf->cy = linum - 1;
				linum = -1;
				if (E.firstBuf->cy > E.firstBuf->numrows) {
					E.firstBuf->cy = E.firstBuf->numrows;
				}
			}
			E.buf = E.firstBuf;
		}
	}
	E.windows[0]->buf = E.buf;

	/* Initialize minibuffer */
	E.minibuf = newBuffer();
	E.minibuf->single_line = 1;
	E.minibuf->truncate_lines = 1;
	E.minibuf->filename = stringdup("*minibuffer*");
	E.edbuf = E.buf;

	editorSetStatusMessage("emsys " EMSYS_VERSION " - C-x C-c to quit");
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
				E.macro.keys =
					malloc(E.macro.skeys * sizeof(int));
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
		} else if (c == MACRO_EXEC ||
			   (E.micro == MACRO_EXEC && (c == 'e' || c == 'E'))) {
			if (E.recording) {
				editorSetStatusMessage(
					"Keyboard macro defined");
				E.recording = 0;
			}
			editorExecMacro(&E.macro);
			E.micro = MACRO_EXEC;
		} else {
			editorRecordKey(c);
			executeCommand(c);
		}
	}
	return 0;
}
