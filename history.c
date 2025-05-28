#include <stdlib.h>
#include <string.h>
#include "emsys.h"

void addToHistory(char **history, int *pos, const char *entry) {
	if (!entry || !*entry)
		return;

	if (history[*pos]) {
		free(history[*pos]);
	}

	history[*pos] = stringdup(entry);
	*pos = (*pos + 1) % HISTORY_SIZE;
}

char *getHistoryEntry(char **history, int pos, int offset) {
	int index = (pos - 1 - offset + HISTORY_SIZE) % HISTORY_SIZE;
	return history[index];
}