#ifndef EMSYS_COMMAND_H
#define EMSYS_COMMAND_H
#include "emsys.h"
void editorToggleTruncateLines(struct editorConfig *ed,
			       struct editorBuffer *buf);
void editorToggleReadOnly(void);
void editorQueryReplace(void);
void editorHelpForHelp(void);
void editorViewManPage(void);
void setupCommands(void);
void runCommand(char *);
#endif
