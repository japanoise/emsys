#ifndef EMSYS_HISTORY_H
#define EMSYS_HISTORY_H

#include "emsys.h"

void initHistory(struct editorHistory *hist);
void addHistory(struct editorHistory *hist, const char *str);
char *getHistoryAt(struct editorHistory *hist, int index);
void freeHistory(struct editorHistory *hist);
char *getLastHistory(struct editorHistory *hist);

#endif
