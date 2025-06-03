#ifndef EMSYS_H
#define EMSYS_H 1

#include <stdint.h>
#include <termios.h>
#include <time.h>
#include "config.h"

/*** util ***/

#define EMSYS_TAB_STOP 8
#define HISTORY_SIZE 50

#ifndef EMSYS_VERSION
#define EMSYS_VERSION "1.0.0"
#endif

#ifndef EMSYS_BUILD_DATE
#define EMSYS_BUILD_DATE "unknown"
#endif

#define ESC "\033"
#define CSI ESC "["
#define CRLF "\r\n"
#define ISCTRL(c) ((0 < c && c < 0x20) || c == 0x7f)
#if !defined(CTRL)
#define CTRL(x) ((x) & 0x1f)
#endif

#define CHECK_READ_ONLY(buf)                                     \
	do {                                                     \
		if ((buf)->read_only) {                          \
			setStatusMessage("Buffer is read-only"); \
			return;                                  \
		}                                                \
	} while (0)

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
	CUT,
	REDO,
	FORWARD_WORD,
	BACKWARD_WORD,
	FORWARD_PARA,
	BACKWARD_PARA,
	SWITCH_BUFFER,
	NEXT_BUFFER,
	PREVIOUS_BUFFER,
	MARK_BUFFER,
	MARK_RECTANGLE,
	DELETE_WORD,
	BACKSPACE_WORD,
	OTHER_WINDOW,
	CREATE_WINDOW,
	DESTROY_WINDOW,
	DESTROY_OTHER_WINDOWS,
	KILL_BUFFER,
	MACRO_RECORD,
	MACRO_END,
	MACRO_EXEC,
	ALT_0,
	ALT_1,
	ALT_2,
	ALT_3,
	ALT_4,
	ALT_5,
	ALT_6,
	ALT_7,
	ALT_8,
	ALT_9,
	SUSPEND,
	UPCASE_WORD,
	DOWNCASE_WORD,
	CAPCASE_WORD,
	UPCASE_REGION,
	DOWNCASE_REGION,
	TOGGLE_TRUNCATE_LINES,
	TRANSPOSE_WORDS,
	EXEC_CMD,
	FIND_FILE,
	WHAT_CURSOR,
	PIPE_CMD,
	CUSTOM_INFO_MESSAGE,
	ISEARCH_FORWARD_REGEXP,
	QUERY_REPLACE,
	GOTO_LINE,
	BACKTAB,
	SWAP_MARK,
	JUMP_REGISTER,
	MACRO_REGISTER,
	POINT_REGISTER,
	NUMBER_REGISTER,
	REGION_REGISTER,
	INC_REGISTER,
	INSERT_REGISTER,
	VIEW_REGISTER,
	STRING_RECT,
	COPY_RECT,
	KILL_RECT,
	YANK_RECT,
	RECT_REGISTER,
	EXPAND,
	UNIVERSAL_ARGUMENT,
};

enum promptType {
	PROMPT_BASIC,
	PROMPT_FILES,
	PROMPT_COMMANDS,
};
/*** data ***/

typedef struct erow {
	int size;
	uint8_t *chars;
	int cached_width;
	int width_valid;
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
	int paired;
	uint8_t *data;
};

struct editorBuffer {
	int indent;
	int cx, cy;
	int markx, marky;
	int numrows;
	int end;
	int dirty;
	int uarg;
	int uarg_active;
	int special_buffer;
	int truncate_lines;
	int word_wrap;
	int rectangle_mode;
	int read_only;
	erow *row;
	char *filename;
	uint8_t *query;
	uint8_t match;
	struct editorUndo *undo;
	struct editorUndo *redo;
	struct editorBuffer *next;
	int *screen_line_start;
	int screen_line_cache_size;
	int screen_line_cache_valid;
};

struct editorWindow {
	int focused;
	struct editorBuffer *buf;
	int scx, scy;
	int cx, cy;
	int rowoff;
	int coloff;
	int height;
};

struct editorMacro {
	int *keys;
	int nkeys;
	int skeys;
};

struct editorConfig;

struct editorCommand {
	const char *key;
	void (*cmd)(struct editorConfig *, struct editorBuffer *);
};

enum registerType {
	REGISTER_NULL,
	REGISTER_REGION,
	REGISTER_NUMBER,
	REGISTER_POINT,
	REGISTER_MACRO,
	REGISTER_RECTANGLE,
};

struct editorPoint {
	int cx;
	int cy;
	struct editorBuffer *buf;
};

struct editorRectangle {
	int rx;
	int ry;
	uint8_t *rect;
};

union registerData {
	uint8_t *region;
	int64_t number;
	struct editorMacro *macro;
	struct editorPoint *point;
	struct editorRectangle *rect;
};

struct editorRegister {
	enum registerType rtype;
	union registerData rdata;
};

struct editorConfig {
	uint8_t *kill;
	uint8_t *rectKill;
	int rx;
	int ry;
	int screenrows;
	int screencols;
	uint8_t unicode[4];
	int nunicode;
	char minibuffer[256];
	char prefix_display[32];
	time_t statusmsg_time;
	struct termios orig_termios;
	struct editorBuffer *firstBuf;
	struct editorBuffer *focusBuf;
	int nwindows;
	struct editorWindow **windows;
	int recording;
	struct editorMacro macro;
	int playback;
	int micro;
	struct editorCommand *cmd;
	int cmd_count;
	struct editorRegister registers[127];
	struct editorBuffer *lastVisitedBuffer;
	int describe_key_mode;
};

/*** prototypes ***/

void crashHandler(int sig);
void setupHandlers();
void setStatusMessage(const char *fmt, ...);
void refreshScreen();
uint8_t *promptUser(struct editorBuffer *bufr, uint8_t *prompt,
		    enum promptType t,
		    void (*callback)(struct editorBuffer *, uint8_t *, int));
void cursorBottomLine(int);
void cursorBottomLineLong(long);
void insertNewline(struct editorBuffer *bufr);
void editorInsertChar(int c);
void bufferInsertChar(struct editorBuffer *bufr, int c);
void editorMoveCursor(int key);
void bufferMoveCursor(struct editorBuffer *bufr, int key);
void editorOpenFile(struct editorBuffer *bufr, char *filename);
void die(const char *s);
void editorResume(int sig);
void editorSuspend(int sig);

/* Safe memory allocation wrappers */
void *xmalloc(size_t size);
void *xrealloc(void *ptr, size_t size);
void *xcalloc(size_t nmemb, size_t size);

/* Cursor validation functions */
erow *safeGetRow(struct editorBuffer *buf, int row_index);
int nextScreenX(char *chars, int *i, int current_screen_x);

struct editorBuffer *newBuffer();
void invalidateScreenCache(struct editorBuffer *buf);
void buildScreenCache(struct editorBuffer *buf);
int getScreenLineForRow(struct editorBuffer *buf, int row);
void destroyBuffer(struct editorBuffer *);
int readKey(void);
void recordKey(int c);
void recenter(struct editorWindow *win);
void recenterCommand(struct editorConfig *ed, struct editorBuffer *buf);
void execMacro(struct editorMacro *macro);
char *stringdup(const char *s);
int windowFocusedIdx(struct editorConfig *ed);
void scroll();

#endif
