#include <stdlib.h>
#include <string.h>
#include "emsys.h"
#include "command.h"
#include "uthash.h"
#include "unused.h"

void editorVersion(struct editorConfig *UNUSED(ed),
		   struct editorBuffer *UNUSED(buf)) {
	editorSetStatusMessage(
		"emsys version "EMSYS_VERSION", built "EMSYS_BUILD_DATE);
}

void setupCommands(struct editorConfig *ed) {
	ed->cmd = NULL;
	struct editorCommand *newCmd;
	char *newCmdName;

	newCmd = malloc(sizeof *newCmd);
	newCmdName = "version";
	newCmd->cmd = editorVersion;
	HASH_ADD_KEYPTR(hh, ed->cmd, newCmdName, strlen(newCmdName), newCmd);
}

void runCommand(char * cmd, struct editorConfig *ed, struct editorBuffer *buf) {	for (int i = 0; cmd[i]; i++) {
		uint8_t c = cmd[i];
		if ('A' <= c && c <= 'Z') {
			c |= 0x60;
		}
		cmd[i] = c;
	}
	struct editorCommand *found;
	HASH_FIND_STR(ed->cmd, cmd, found);
	if (found) {
		found->cmd(ed, buf);
	} else {
		editorSetStatusMessage("No command found");
	}
}
