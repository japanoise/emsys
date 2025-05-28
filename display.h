#ifndef DISPLAY_H
#define DISPLAY_H

#include "emsys.h"

struct abuf {
	char *b;
	int len;
};

#define ABUF_INIT { NULL, 0 }

void abAppend(struct abuf *ab, const char *s, int len);
void abFree(struct abuf *ab);
int calculateRowsToScroll(struct editorBuffer *buf, struct editorWindow *win,
			  int direction);
void editorSetScxScy(struct editorWindow *win);
void editorScroll(void);
void editorDrawRows(struct editorWindow *win, struct abuf *ab, int screenrows,
		    int screencols);
void editorDrawStatusBar(struct editorWindow *win, struct abuf *ab, int line);
void editorDrawMinibuffer(struct abuf *ab);
void editorRefreshScreen(void);
void editorCursorBottomLine(int curs);
void editorCursorBottomLineLong(long curs);
void editorResizeScreen(int sig);
void editorRecenter(struct editorWindow *win);

#endif /* DISPLAY_H */