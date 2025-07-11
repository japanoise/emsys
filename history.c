#include <stdlib.h>
#include <string.h>
#include "emsys.h"
#include "history.h"
#include "util.h"

extern struct editorConfig E;

void initHistory(struct editorHistory *hist) {
	hist->head = NULL;
	hist->tail = NULL;
	hist->current = NULL;
	hist->count = 0;
}

void addHistory(struct editorHistory *hist, const char *str) {
	if (!str || strlen(str) == 0) {
		return;
	}
	
	/* Don't add duplicates of the most recent entry */
	if (hist->tail && strcmp(hist->tail->str, str) == 0) {
		return;
	}
	
	/* Create new entry */
	struct historyEntry *entry = xmalloc(sizeof(struct historyEntry));
	entry->str = xstrdup(str);
	entry->next = NULL;
	entry->prev = hist->tail;
	
	/* Add to list */
	if (hist->tail) {
		hist->tail->next = entry;
	} else {
		hist->head = entry;
	}
	hist->tail = entry;
	hist->count++;
	
	/* Remove oldest entries if we exceed the limit */
	while (hist->count > HISTORY_MAX_ENTRIES) {
		struct historyEntry *old = hist->head;
		hist->head = old->next;
		if (hist->head) {
			hist->head->prev = NULL;
		}
		free(old->str);
		free(old);
		hist->count--;
	}
	
	/* Reset current position */
	hist->current = NULL;
}

char *getPrevHistory(struct editorHistory *hist) {
	if (!hist->head) {
		return NULL;
	}
	
	if (!hist->current) {
		/* Start from the most recent */
		hist->current = hist->tail;
	} else if (hist->current->prev) {
		/* Move to previous */
		hist->current = hist->current->prev;
	}
	
	return hist->current ? hist->current->str : NULL;
}

char *getNextHistory(struct editorHistory *hist) {
	if (!hist->current) {
		return NULL;
	}
	
	hist->current = hist->current->next;
	return hist->current ? hist->current->str : NULL;
}

void resetHistoryPosition(struct editorHistory *hist) {
	hist->current = NULL;
}

void freeHistory(struct editorHistory *hist) {
	struct historyEntry *entry = hist->head;
	while (entry) {
		struct historyEntry *next = entry->next;
		free(entry->str);
		free(entry);
		entry = next;
	}
	initHistory(hist);
}