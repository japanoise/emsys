#ifndef EMSYS_COMMAND_H
#define EMSYS_COMMAND_H
#include "emsys.h"
void setupCommands(struct editorConfig *);
void runCommand(char *, struct editorConfig *, struct editorBuffer *);
#endif
