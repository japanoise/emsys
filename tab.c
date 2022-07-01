#include <glob.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "emsys.h"
#include "tab.h"
#include "unicode.h"

uint8_t *tabCompleteFiles(uint8_t *prompt) {
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

#ifndef GLOB_TILDE
	/* This isn't in POSIX, so define a fallback. */
#define GLOB_TILDE 0
#endif

	if (glob((char*)prompt, GLOB_TILDE|GLOB_MARK, NULL, &globlist))
		goto TC_FILES_CLEANUP;

	size_t cur = 0;

	if (globlist.gl_pathc < 1)
		goto TC_FILES_CLEANUP;

	if (globlist.gl_pathc == 1)
		goto TC_FILES_ACCEPT;

	int curw = stringWidth((uint8_t*)globlist.gl_pathv[cur]);

	for (;;) {
		editorSetStatusMessage("Multiple options: %s",
				       globlist.gl_pathv[cur]);
		editorRefreshScreen();
		editorCursorBottomLine(curw+19);

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
		case BACKTAB:
			if (cur == 0) {
				cur = globlist.gl_pathc - 1;
			} else {
				cur--;
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
