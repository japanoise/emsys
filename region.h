#ifndef EMSYS_REGION_H
#define EMSYS_REGION_H 1

#include<stdint.h>
#include"emsys.h"

int markInvalid(struct editorBuffer *buf);

void editorSetMark(struct editorBuffer *buf);

void editorKillRegion(struct editorConfig *ed, struct editorBuffer *buf);

void editorCopyRegion(struct editorConfig *ed, struct editorBuffer *buf);

void editorYank(struct editorConfig *ed, struct editorBuffer *buf);

void editorTransformRegion(struct editorConfig *ed, struct editorBuffer *buf,
			   uint8_t *(*transformer)(uint8_t*));

void editorStringRectangle(struct editorConfig *ed, struct editorBuffer *buf);

void editorCopyRectangle(struct editorConfig *ed, struct editorBuffer *buf);

void editorKillRectangle(struct editorConfig *ed, struct editorBuffer *buf);

void editorYankRectangle(struct editorConfig *ed, struct editorBuffer *buf);

#endif
