#ifndef EMSYS_COMMAND_H
#define EMSYS_COMMAND_H
#include "emsys.h"
void editorToggleTruncateLines(struct editorConfig *ed,
			       struct editorBuffer *buf);
void editorQueryReplace(struct editorConfig *ed, struct editorBuffer *buf);
void editorHelpForHelp(struct editorConfig *ed, struct editorBuffer *buf);
void editorViewManPage(struct editorConfig *ed, struct editorBuffer *buf);
void setupCommands(struct editorConfig *);
void runCommand(char *, struct editorConfig *, struct editorBuffer *);
#endif
