#include <glob.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "emsys.h"
#include "tab.h"
#include "unicode.h"

uint8_t *tabCompleteFiles(struct editorConfig *ed, uint8_t *prompt) {
	glob_t globlist;
	uint8_t *ret = prompt;

	/*
	 * Define this to do manual globbing. It does mean you'll have
	 * to add the *s yourself. However, it will let you tab
	 * complete for more interesting scenarios, like
	 * *dir/other*dir/file.*.gz -> mydir/otherFOOdir/file.tar.gz
	 */
#ifndef EMSYS_NO_SIMPLE_GLOB
	int end = strlen((char*)prompt);
	prompt[end] = '*';
	prompt[end+1] = 0;
#endif

	if (glob((char*)prompt, 0, NULL, &globlist))
		goto TC_FILES_CLEANUP;

	size_t cur = 0;

	if (globlist.gl_pathc < 1)
		goto TC_FILES_CLEANUP;

	if (globlist.gl_pathc == 1)
		goto TC_FILES_ACCEPT;

	char cbuf[32];
	int curw = stringWidth((uint8_t*)globlist.gl_pathv[cur]);

	for (;;) {
		editorSetStatusMessage("Multiple options: %s",
				       globlist.gl_pathv[cur]);
		editorRefreshScreen();

		if (ed->nwindows == 1) {
			snprintf(cbuf, sizeof(cbuf), CSI"%d;%dH", ed->screenrows,
				 curw + 19);
		} else {
			int windowSize = (ed->screenrows-1)/ed->nwindows;
			snprintf(cbuf, sizeof(cbuf), CSI"%d;%dH",
				 (windowSize*ed->nwindows)+1,
				 curw + 19);
		}
		write(STDOUT_FILENO, cbuf, strlen(cbuf));

		int c = editorReadKey();
		switch (c) {
		case '\r':;
		TC_FILES_ACCEPT:;
			ret = calloc(strlen(globlist.gl_pathv[cur])+1, 1);
			strcpy((char*)ret, globlist.gl_pathv[cur]);
			goto TC_FILES_CLEANUP;
			break;
		case CTRL('i'):
			cur++;
			if (cur >= globlist.gl_pathc) {
				cur = 0;
			}
			curw = stringWidth((uint8_t*)globlist.gl_pathv[cur]);
			break;
		case CTRL('g'):
			goto TC_FILES_CLEANUP;
			break;
		}
	}

TC_FILES_CLEANUP:
#ifndef EMSYS_NO_SIMPLE_GLOB
	prompt[end] = 0;
#endif
	return ret;
}
