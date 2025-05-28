#ifndef EMSYS_KEYBINDINGS_H
#define EMSYS_KEYBINDINGS_H

#include "emsys.h"

/* Key handler function type */
typedef void (*KeyHandler)(struct editorConfig *, struct editorBuffer *,
			   int rept);

/* Single key binding */
typedef struct {
	int key;
	KeyHandler handler;
	const char *name; /* Emacs command name for help/debugging */
} KeyBinding;

/* Prefix key table */
typedef struct {
	const char *name;
	KeyBinding *bindings;
	int count;
} KeyTable;

/* Global key tables */
extern KeyTable global_keys;
extern KeyTable ctrl_x_keys;
extern KeyTable ctrl_x_r_keys;
extern KeyTable meta_keys;

/* Initialize key binding tables */
void initKeyBindings(void);

/* Get current repeat count and reset */
int getRepeatCount(struct editorBuffer *buf);

/* Handle universal argument keys */
int handleUniversalArgument(int key, struct editorBuffer *buf);

/* Process a complete key sequence */
void processKeySequence(int key);

#endif /* EMSYS_KEYBINDINGS_H */
