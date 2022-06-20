#ifndef EMSYS_COMMAND_H
#define EMSYS_COMMAND_H
#include "emsys.h"
void editorQueryReplace(struct editorConfig *ed, struct editorBuffer *buf);
void setupCommands(struct editorConfig *);
void runCommand(char *, struct editorConfig *, struct editorBuffer *);
#endif
