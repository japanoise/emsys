#ifndef DISPLAY_H
#define DISPLAY_H

#include "emsys.h"

struct abuf {
	char *b;
	int len;
	int capacity;
};

#define ABUF_INIT { NULL, 0, 0 }

void abAppend(struct abuf *ab, const char *s, int len);
void abFree(struct abuf *ab);
int calculateRowsToScroll(struct editorBuffer *buf, struct editorWindow *win,
			  int direction);
void setScxScy(struct editorWindow *win);
void scroll(void);
void drawRows(struct editorWindow *win, struct abuf *ab, int screenrows,
		    int screencols);
void drawStatusBar(struct editorWindow *win, struct abuf *ab, int line);
void drawMinibuffer(struct abuf *ab);
void refreshScreen(void);
void cursorBottomLine(int curs);
void cursorBottomLineLong(long curs);
void editorResizeScreen(int sig);
void recenter(struct editorWindow *win);

#endif /* DISPLAY_H */