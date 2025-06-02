#include "platform.h"
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
#ifndef __ANDROID__
#include "subprocess.h"
#endif

static uint8_t *cmd;
static char *buf;

static uint8_t *transformerPipeCmd(uint8_t *input) {
	int bsiz = BUFSIZ + 1;
	char temp_input_file[] = "/tmp/emsys_pipe_input_XXXXXX";
	char temp_output_file[] = "/tmp/emsys_pipe_output_XXXXXX";

	int input_fd = mkstemp(temp_input_file);
	if (input_fd == -1) {
		die("mkstemp input");
	}

	int output_fd = mkstemp(temp_output_file);
	if (output_fd == -1) {
		close(input_fd);
		unlink(temp_input_file);
		die("mkstemp output");
	}

	FILE *input_file = fdopen(input_fd, "w");
	if (!input_file) {
		close(input_fd);
		close(output_fd);
		unlink(temp_input_file);
		unlink(temp_output_file);
		die("fdopen input");
	}

	for (int i = 0; input[i]; i++) {
		fputc(input[i], input_file);
	}
	fclose(input_file);

	char full_cmd[2048];
	snprintf(full_cmd, sizeof(full_cmd), "%s < %s > %s", (char *)cmd,
		 temp_input_file, temp_output_file);

	int exit_code = system(full_cmd);
	if (exit_code != 0) {
		close(output_fd);
		unlink(temp_input_file);
		unlink(temp_output_file);
		editorSetStatusMessage("Command failed with exit code %d",
				       exit_code);
		return (uint8_t *)buf;
	}

	FILE *output_file = fdopen(output_fd, "r");
	if (!output_file) {
		close(output_fd);
		unlink(temp_input_file);
		unlink(temp_output_file);
		die("fdopen output");
	}

	int c = fgetc(output_file);
	int i = 0;
	while (c != EOF) {
		buf[i++] = c;
		buf[i] = 0;
		if (i >= bsiz - 10) {
			bsiz <<= 1;
			buf = xrealloc(buf, bsiz);
		}
		c = fgetc(output_file);
	}
	editorSetStatusMessage("Read %d bytes", i);

	fclose(output_file);
	unlink(temp_input_file);
	unlink(temp_output_file);
	return (uint8_t *)buf;
}

uint8_t *editorPipe(struct editorConfig *ed, struct editorBuffer *bf) {
	buf = xcalloc(1, BUFSIZ + 1);
	cmd = NULL;
	cmd = editorPrompt(
		bf,
		(uint8_t *)"Shell command on region (WARNING: executed via sh -c): %s",
		PROMPT_BASIC, NULL);

	if (cmd == NULL) {
		editorSetStatusMessage("Canceled shell command.");
	} else {
		if (bf->uarg_active) {
			bf->uarg_active = 0;
			bf->uarg = 0;
			editorTransformRegion(ed, bf, transformerPipeCmd);
			bf->markx = -1;
			bf->marky = -1;
			free(cmd);
			return NULL;
		} else {
			if (markInvalid(bf)) {
				editorSetStatusMessage("Mark invalid.");
				free(cmd);
				free(buf);
				return NULL;
			}

			editorCopyRegion(
				ed, bf); // ed->kill now holds the selected text

			uint8_t *result = transformerPipeCmd(ed->kill);

			free(cmd);
			return result;
		}
	}

	free(cmd);
	free(buf);
	return NULL;
}
