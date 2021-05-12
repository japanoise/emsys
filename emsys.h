#ifndef EMSYS_H
#define EMSYS_H 1

#include<stdint.h>
#include<termios.h>
#include<time.h>

/*** util ***/

#define EMSYS_TAB_STOP 8
#define EMSYS_VERSION "git-main"

#define ESC "\033"
#define CSI ESC"["
#define CRLF "\r\n"
#define ISCTRL(c) ((0 < c && c < 0x20) || c == 0x7f)

enum editorKey {
	BACKSPACE = 127,
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
	UNICODE_ERROR,
	END_OF_FILE,
	BEG_OF_FILE,
	QUIT,
	SAVE,
	COPY,
	REDO,
	FORWARD_WORD,
	BACKWARD_WORD,
	FORWARD_PARA,
	BACKWARD_PARA,
	SWITCH_BUFFER,
	DELETE_WORD,
	BACKSPACE_WORD,
	OTHER_WINDOW,
	CREATE_WINDOW,
	DESTROY_WINDOW,
	DESTROY_OTHER_WINDOWS,
	MACRO_RECORD,
	MACRO_END,
	MACRO_EXEC
};

/*** data ***/

typedef struct erow {
	int size;
	int rsize;
	int renderwidth;
	uint8_t *chars;
	uint8_t *render;
} erow;

struct editorUndo {
        struct editorUndo *prev;
        int startx;
        int starty;
        int endx;
        int endy;
        int append;
        int datalen;
        int datasize;
        int delete;
        uint8_t *data;
};

struct editorBuffer {
	int cx, cy;
	int markx, marky;
	int scx, scy;
	int numrows;
	int rowoff;
	int end;
	int dirty;
	erow *row;
	char *filename;
        struct editorUndo *undo;
        struct editorUndo *redo;
	struct editorBuffer *next;
};

struct editorWindow {
	int focused;
	struct editorBuffer *buf;
};

struct editorMacro {
	int *keys;
	int nkeys;
	int skeys;
};

struct editorConfig {
	uint8_t *kill;
	int screenrows;
	int screencols;
	uint8_t unicode[4];
	int nunicode;
	char minibuffer[80];
	time_t statusmsg_time;
	struct termios orig_termios;
	struct editorBuffer *firstBuf;
	struct editorBuffer *focusBuf;
	int nwindows;
	struct editorWindow **windows;
	int recording;
	struct editorMacro macro;
	int micro;
};

/*** prototypes ***/

void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
uint8_t *editorPrompt(struct editorBuffer *bufr, uint8_t *prompt, void (*callback)(struct editorBuffer *, uint8_t *, int));
void editorUpdateBuffer(struct editorBuffer *buf);
void editorInsertNewline(struct editorBuffer *bufr);
void editorInsertChar(struct editorBuffer *bufr, int c);
void die(const char *s);

#endif
