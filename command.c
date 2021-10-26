#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "emsys.h"
#include "command.h"
#include "region.h"
#include "transform.h"
#include "uthash.h"
#include "unused.h"

// https://stackoverflow.com/a/779960
// You must free the result if result is non-NULL.
char *str_replace(char *orig, char *rep, char *with) {
	char *result; // the return string
	char *ins;    // the next insert point
	char *tmp;    // varies
	int len_rep;  // length of rep (the string to remove)
	int len_with; // length of with (the string to replace rep with)
	int len_front; // distance between rep and end of last rep
	int count;    // number of replacements

	// sanity checks and initialization
	if (!orig || !rep)
		return NULL;
	len_rep = strlen(rep);
	if (len_rep == 0)
		return NULL; // empty rep causes infinite loop during count
	if (!with)
		with = "";
	len_with = strlen(with);

	// count the number of replacements needed
	ins = orig;
	for (count = 0; (tmp = strstr(ins, rep)); ++count) {
		ins = tmp + len_rep;
	}

	tmp = result = malloc(strlen(orig) + (len_with - len_rep) * count + 1);

	if (!result)
		return NULL;

	// first time through the loop, all the variable are set correctly
	// from here on,
	//    tmp points to the end of the result string
	//    ins points to the next occurrence of rep in orig
	//    orig points to the remainder of orig after "end of rep"
	while (count--) {
		ins = strstr(orig, rep);
		len_front = ins - orig;
		tmp = strncpy(tmp, orig, len_front) + len_front;
		tmp = strcpy(tmp, with) + len_with;
		orig += len_front + len_rep; // move to next "end of rep"
	}
	strcpy(tmp, orig);
	return result;
}

void editorVersion(struct editorConfig *UNUSED(ed),
		   struct editorBuffer *UNUSED(buf)) {
	editorSetStatusMessage(
		"emsys version "EMSYS_VERSION", built "EMSYS_BUILD_DATE);
}

void editorIndentTabs(struct editorConfig *UNUSED(ed),
		      struct editorBuffer *buf) {
	buf->indent = 0;
	editorSetStatusMessage("Indentation set to tabs");
}

void editorIndentSpaces(struct editorConfig *UNUSED(ed),
		   struct editorBuffer *buf) {
	uint8_t *indentS = editorPrompt(buf, "Set indentation to: %s", NULL);
	if (indentS == NULL) {
		goto cancel;
	}
	int indent = atoi((char *)indentS);
	free(indentS);
	if (indent <= 0) {
	cancel:
		editorSetStatusMessage("Canceled.");
		return;
	}
	buf->indent = indent;
	editorSetStatusMessage("Indentation set to %i spaces", indent);
}

void editorRevert(struct editorConfig *ed,
		   struct editorBuffer *buf) {
	struct editorBuffer *new = newBuffer();
	editorOpen(new, buf->filename);
	new->next = buf->next;
	ed->focusBuf = new;
	if (ed->firstBuf == buf) {
		ed->firstBuf = new;
	}
	struct editorBuffer *cur = ed->firstBuf;
	while (cur != NULL) {
		if (cur->next == buf) {
			cur->next = new;
			break;
		}
		cur = cur->next;
	}
	for (int i = 0; i < ed->nwindows; i++) {
		if (ed->windows[i]->buf == buf) {
			ed->windows[i]->buf = new;
		}
	}
	new->indent = buf->indent;
	new->cx = buf->cx;
	new->cy = buf->cy;
	if (new->cy > new->numrows) {
		new->cy = new->numrows;
		new->cx = 0;
	} else if (new->cx > new->row[new->cy].size) {
		new->cx = new->row[new->cy].size;
	}
	destroyBuffer(buf);
}

uint8_t *orig;
uint8_t *repl;

uint8_t *transformerReplaceString(uint8_t *input) {
	return str_replace(input, orig, repl);
}

void editorReplaceString(struct editorConfig *ed,
		   struct editorBuffer *buf) {
	orig = NULL;
	repl = NULL;
	orig = editorPrompt(buf, "Replace: %s", NULL);
	if (orig == NULL) {
		editorSetStatusMessage("Canceled replace-string.");
		return;
	}

	uint8_t *prompt = malloc(strlen(orig)+20);
	sprintf(prompt, "Replace %s with: %%s", orig);
	repl = editorPrompt(buf, prompt, NULL);
	free(prompt);
	if (repl == NULL) {
		free(orig);
		editorSetStatusMessage("Canceled replace-string.");
		return;
	}
	
	editorTransformRegion(ed, buf, transformerReplaceString);

	free(orig);
	free(repl);
}

void editorCapitalizeRegion(struct editorConfig *ed,
		   struct editorBuffer *buf) {
	editorTransformRegion(ed, buf, transformerCapitalCase);
}

#define ADDCMD(name, func) newCmd = malloc(sizeof *newCmd); newCmdName = name ; newCmd->cmd = func ; HASH_ADD_KEYPTR(hh, ed->cmd, newCmdName, strlen(newCmdName), newCmd)

void setupCommands(struct editorConfig *ed) {
	ed->cmd = NULL;
	struct editorCommand *newCmd;
	char *newCmdName;

	ADDCMD("version", editorVersion);
	ADDCMD("replace-string", editorReplaceString);
	ADDCMD("kanaya", editorCapitalizeRegion); /* egg! */
	ADDCMD("capitalize-region", editorCapitalizeRegion);
	ADDCMD("indent-spaces", editorIndentSpaces);
	ADDCMD("indent-tabs", editorIndentTabs);
	ADDCMD("revert", editorRevert);
}

void runCommand(char * cmd, struct editorConfig *ed, struct editorBuffer *buf) {
	for (int i = 0; cmd[i]; i++) {
		uint8_t c = cmd[i];
		if ('A' <= c && c <= 'Z') {
			c |= 0x60;
		} else if (c == ' ') {
			c = '-';
		}
		cmd[i] = c;
	}
	struct editorCommand *found;
	HASH_FIND_STR(ed->cmd, cmd, found);
	if (found) {
		found->cmd(ed, buf);
	} else {
		editorSetStatusMessage("No command found");
	}
}
