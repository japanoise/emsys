#ifndef EMSYS_PIPE_H
#define EMSYS_PIPE_H
#include "emsys.h"

uint8_t *editorPipe(struct editorConfig *ed, struct editorBuffer *buf);
void editorPipeCmd(struct editorConfig *ed, struct editorBuffer *bufr);

#endif
