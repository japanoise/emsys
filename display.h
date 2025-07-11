#ifndef DISPLAY_H
#define DISPLAY_H

#include <stddef.h>

/* Forward declarations */
struct editorWindow;
struct editorBuffer;
struct editorConfig;

/* Append buffer for efficient screen updates */
struct abuf {
	char *b;
	int len;
	int capacity;
};

#define ABUF_INIT { NULL, 0, 0 }

/* Display constants */
extern const int minibuffer_height;
extern const int statusbar_height;

/* Append buffer operations */
void abAppend(struct abuf *ab, const char *s, int len);
void abFree(struct abuf *ab);

/* Display functions */
void refreshScreen(void);
void drawRows(struct editorWindow *win, struct abuf *ab, int screenrows,
	      int screencols);
void drawStatusBar(struct editorWindow *win, struct abuf *ab, int line);
void drawMinibuffer(struct abuf *ab);
void scroll(void);
void setScxScy(struct editorWindow *win);
int calculateRowsToScroll(struct editorBuffer *buf, struct editorWindow *win,
			  int direction);
void cursorBottomLine(int curs);
void cursorBottomLineLong(long curs);
void editorSetStatusMessage(const char *fmt, ...);
void editorResizeScreen(int sig);
void recenter(struct editorWindow *win);
void editorToggleTruncateLines(void);
void editorVersion(void);
/* Wrappers for command table */
void editorVersionWrapper(struct editorConfig *ed, struct editorBuffer *buf);
void editorToggleTruncateLinesWrapper(struct editorConfig *ed,
				      struct editorBuffer *buf);

/* Window management functions */
int windowFocusedIdx(void);
int findBufferWindow(struct editorBuffer *buf);
void editorSwitchWindow(void);
void synchronizeBufferCursor(struct editorBuffer *buf,
			     struct editorWindow *win);
void editorCreateWindow(void);
void editorDestroyWindow(int window_idx);
void editorDestroyOtherWindows(void);
void editorWhatCursor(void);

#endif /* DISPLAY_H */
