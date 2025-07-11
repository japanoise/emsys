#ifndef EMSYS_HISTORY_H
#define EMSYS_HISTORY_H

#include "emsys.h"

void initHistory(struct editorHistory *hist);
void addHistory(struct editorHistory *hist, const char *str);
char *getPrevHistory(struct editorHistory *hist);
char *getNextHistory(struct editorHistory *hist);
void resetHistoryPosition(struct editorHistory *hist);
void freeHistory(struct editorHistory *hist);

#endif