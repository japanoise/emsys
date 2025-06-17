#ifndef EMSYS_REGION_H
#define EMSYS_REGION_H 1

#include <stdint.h>
#include "emsys.h"

int markInvalid(void);
int markInvalidSilent(void);

void editorSetMark(void);

void editorClearMark(void);

void editorToggleRectangleMode(void);

void editorMarkBuffer(void);

void editorKillRegion(struct editorConfig *ed, struct editorBuffer *buf);

void editorCopyRegion(struct editorConfig *ed, struct editorBuffer *buf);

void editorYank(struct editorConfig *ed, struct editorBuffer *buf, int count);

void editorTransformRegion(struct editorConfig *ed, struct editorBuffer *buf,
			   uint8_t *(*transformer)(uint8_t *));

void editorReplaceRegex(struct editorConfig *ed, struct editorBuffer *buf);

void editorStringRectangle(struct editorConfig *ed, struct editorBuffer *buf);

void editorCopyRectangle(struct editorConfig *ed, struct editorBuffer *buf);

void editorKillRectangle(struct editorConfig *ed, struct editorBuffer *buf);

void editorYankRectangle(struct editorConfig *ed, struct editorBuffer *buf);

#endif
