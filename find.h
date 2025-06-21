#ifndef EMSYS_FIND_H
#define EMSYS_FIND_H
#include <stdint.h>
#include "emsys.h"
char *str_replace(char *orig, char *rep, char *with);
void editorFindCallback(struct editorBuffer *bufr, uint8_t *query, int key);
void editorFind(struct editorBuffer *bufr);
void editorRegexFind(struct editorBuffer *bufr);
void editorRegexFindWrapper(struct editorConfig *ed, struct editorBuffer *buf);
void editorBackwardRegexFind(struct editorBuffer *bufr);
void editorBackwardRegexFindWrapper(struct editorConfig *ed,
				    struct editorBuffer *buf);
uint8_t *transformerReplaceString(uint8_t *input);
void editorReplaceString(struct editorConfig *ed, struct editorBuffer *buf);
void editorQueryReplace(struct editorConfig *ed, struct editorBuffer *buf);
#endif
