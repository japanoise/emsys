#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "emsys.h"
#include "region.h"
#include "pipe.h"
#include "subprocess.h"

static uint8_t *cmd;
static char *buf;

static uint8_t *transformerPipeCmd(uint8_t *input) {
	int bsiz = BUFSIZ+1;
	/* Using sh -c lets us use pipes and stuff and takes care of quoting. */
	const char *command_line[4] = {"/bin/sh", "-c", (char*)cmd, NULL};
	struct subprocess_s subprocess;
	int result = subprocess_create(
		command_line,
		subprocess_option_inherit_environment,
		&subprocess);
	if (result) {
		die("subprocess");
	}
	FILE* p_stdin = subprocess_stdin(&subprocess);
	FILE* p_stdout = subprocess_stdout(&subprocess);

	/* Write region to subprocess */
	for (int i = 0; input[i]; i++) {
		fputc(input[i], p_stdin);
	}

	/* Join process */
	int sub_ret;
	subprocess_join(&subprocess, &sub_ret);

	/* Read stdout of process into buffer */
	int c = fgetc(p_stdout);
	int i = 0;
	while (c != EOF) {
		buf[i++] = c;
		buf[i] = 0;
		if (i >= bsiz-10) {
			bsiz <<= 1;
			buf = realloc(buf, bsiz);
		}
		c = fgetc(p_stdout);
	}
	editorSetStatusMessage("Read %d bytes", i);

	/* Cleanup & return */
	subprocess_destroy(&subprocess);
	return (uint8_t*) buf;
}

uint8_t *editorPipe(struct editorConfig *ed, struct editorBuffer *bf) {
	buf = calloc(1, BUFSIZ + 1);
	cmd = NULL;
	cmd = editorPrompt(bf, (uint8_t*)"Shell command on region: %s", PROMPT_BASIC, NULL);

	if (cmd == NULL) {
	  editorSetStatusMessage("Canceled shell command.");
    } else {
        if (bf->uarg_active) {
            bf->uarg_active = 0;
            bf->uarg = 0;
            editorTransformRegion(ed, bf, transformerPipeCmd); 
                // unmark region
                bf->markx = -1;
                bf->marky = -1;
                free(cmd);
		return NULL;
        } else {
            // 1. Extract the selected region
            if (markInvalid(bf)) { 
                editorSetStatusMessage("Mark invalid.");
                free(cmd);
		free(buf);
		return NULL;
	}

	    editorCopyRegion(ed, bf); // ed->kill now holds the selected text

            // 2. Pass the extracted text to transformerPipeCmd
            uint8_t *result = transformerPipeCmd(ed->kill); 

	free(cmd);
            return result; 
        }
    }

    free(cmd);
    free(buf); 
    return NULL; 
}
