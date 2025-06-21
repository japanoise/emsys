#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emsys.h"
#include "region.h"
#include "register.h"
#include "unicode.h"
#include "unused.h"
#include "display.h"
#include "terminal.h"
#include "util.h"

static int getRegisterName(char *prompt) {
	int key;
	int psize = stringWidth((uint8_t *)prompt);
	do {
		editorSetStatusMessage("%s:", prompt);
		cursorBottomLine(psize + 2);
		refreshScreen();
		key = editorReadKey();
	} while (key > 127);
	editorRecordKey(key);
	return key;
}

#define GET_REGISTER(vname, prompt)             \
	int vname = getRegisterName(prompt);    \
	if (vname == 0x07) {                    \
		editorSetStatusMessage("Quit"); \
		return;                         \
	}

static void registerMessage(char *msg, char reg) {
	char str[4];
	if (reg < 32) {
		snprintf(str, sizeof(str), "C-%c", reg + '@');
	} else {
		str[0] = reg;
		str[1] = 0;
	}
	editorSetStatusMessage(msg, str);
}

static void clearRegister(struct editorConfig *ed, int reg) {
	switch (ed->registers[reg].rtype) {
	case REGISTER_NULL:
		/* Do nothing, register already clear */
		break;
	case REGISTER_REGION:
		free(ed->registers[reg].rdata.region);
		ed->registers[reg].rdata.region = NULL;
		break;
	case REGISTER_NUMBER:
		ed->registers[reg].rdata.number = 0;
		break;
	case REGISTER_POINT:
		free(ed->registers[reg].rdata.point);
		ed->registers[reg].rdata.point = NULL;
		break;
	case REGISTER_MACRO:
		free(ed->registers[reg].rdata.macro);
		ed->registers[reg].rdata.macro = NULL;
		break;
	case REGISTER_RECTANGLE:
		free(ed->registers[reg].rdata.rect->rect);
		free(ed->registers[reg].rdata.rect);
		ed->registers[reg].rdata.rect = NULL;
		break;
	}
	ed->registers[reg].rtype = REGISTER_NULL;
}

void editorJumpToRegister(struct editorConfig *ed) {
	GET_REGISTER(reg, "Jump to register");
	switch (ed->registers[reg].rtype) {
	case REGISTER_NULL:
		registerMessage("Nothing in register %s", reg);
		break;
	case REGISTER_REGION:
		registerMessage("Cannot jump to region in register %s", reg);
		break;
	case REGISTER_NUMBER:
		registerMessage("Cannot jump to number in register %s", reg);
		break;
	case REGISTER_POINT:
		if (ed->buf == ed->registers[reg].rdata.point->buf) {
			editorSetMark();
		} else {
			ed->buf = ed->registers[reg].rdata.point->buf;
			for (int i = 0; i < ed->nwindows; i++) {
				if (ed->windows[i]->focused) {
					ed->windows[i]->buf = ed->buf;
				}
			}
			registerMessage("Jumped to point in register %s", reg);
		}
		struct editorBuffer *buf = ed->buf;
		buf->cx = ed->registers[reg].rdata.point->cx;
		buf->cy = ed->registers[reg].rdata.point->cy;
		if (buf->cy >= buf->numrows)
			buf->cy = buf->numrows - 1;
		if (buf->cy < 0)
			buf->cy = 0;
		if (buf->cx > buf->row[buf->cy].size)
			buf->cx = buf->row[buf->cy].size;
		break;
	case REGISTER_MACRO:
		registerMessage("Executing macro in register %s...", reg);
		editorExecMacro(ed->registers[reg].rdata.macro);
		break;
	case REGISTER_RECTANGLE:
		registerMessage("Cannot jump to rectangle in register %s", reg);
		break;
	}
}

void editorMacroToRegister(struct editorConfig *ed) {
	GET_REGISTER(reg, "Macro to register");
	clearRegister(ed, reg);
	ed->registers[reg].rtype = REGISTER_MACRO;
	ed->registers[reg].rdata.macro = malloc(sizeof(struct editorMacro));
	/* Copy data, creating a shallow copy. */
	memcpy(ed->registers[reg].rdata.macro, &(ed->macro),
	       sizeof(struct editorMacro));
	/* Now copy the keys too, to make a deep copy. */
	ed->registers[reg].rdata.macro->keys =
		malloc(sizeof(int) * ed->macro.skeys);
	memcpy(ed->registers[reg].rdata.macro->keys, ed->macro.keys,
	       ed->macro.nkeys * sizeof(int));
	registerMessage("Saved macro to register %s", reg);
}

void editorPointToRegister(struct editorConfig *ed) {
	GET_REGISTER(reg, "Point to register");
	clearRegister(ed, reg);
	ed->registers[reg].rtype = REGISTER_POINT;
	ed->registers[reg].rdata.point = malloc(sizeof(struct editorPoint));
	ed->registers[reg].rdata.point->buf = ed->buf;
	ed->registers[reg].rdata.point->cx = ed->buf->cx;
	ed->registers[reg].rdata.point->cy = ed->buf->cy;
	registerMessage("Saved point to register %s", reg);
}

void editorNumberToRegister(struct editorConfig *ed, int rept) {
	GET_REGISTER(reg, "Number to register");
	clearRegister(ed, reg);
	ed->registers[reg].rtype = REGISTER_NUMBER;
	ed->registers[reg].rdata.number = rept;
	registerMessage("Saved number to register %s", reg);
}

void editorRegionToRegister(struct editorConfig *ed,
			    struct editorBuffer *bufr) {
	if (markInvalid())
		return;
	GET_REGISTER(reg, "Region to register");
	clearRegister(ed, reg);
	uint8_t *tmp = ed->kill;
	ed->kill = NULL;
	editorCopyRegion(ed, bufr);
	ed->registers[reg].rtype = REGISTER_REGION;
	ed->registers[reg].rdata.region = ed->kill;
	ed->kill = tmp;
	registerMessage("Saved region to register %s", reg);
}

void editorRectRegister(struct editorConfig *ed, struct editorBuffer *bufr) {
	if (markInvalid())
		return;
	GET_REGISTER(reg, "Rectangle to register");
	clearRegister(ed, reg);
	uint8_t *tmp = ed->rectKill;
	int rx = ed->rx;
	int ry = ed->ry;
	ed->rectKill = NULL;
	editorCopyRectangle(ed, bufr);
	ed->registers[reg].rtype = REGISTER_RECTANGLE;
	ed->registers[reg].rdata.rect = malloc(sizeof(struct editorRectangle));
	ed->registers[reg].rdata.rect->rect = ed->rectKill;
	ed->registers[reg].rdata.rect->rx = ed->rx;
	ed->registers[reg].rdata.rect->ry = ed->ry;
	ed->rectKill = tmp;
	ed->rx = rx;
	ed->ry = ry;
	registerMessage("Saved rectangle to register %s", reg);
}

void editorIncrementRegister(struct editorConfig *ed,
			     struct editorBuffer *bufr) {
	GET_REGISTER(reg, "Increment register");
	switch (ed->registers[reg].rtype) {
	case REGISTER_NULL:
		registerMessage("Nothing in register %s", reg);
		break;
	case REGISTER_REGION:;
		uint8_t *tmp = ed->kill;
		ed->kill = NULL;
		editorCopyRegion(ed, bufr);
		size_t kill_len = strlen((char *)ed->kill);
		size_t region_len =
			strlen((char *)ed->registers[reg].rdata.region);
		size_t new_len = region_len + kill_len + 1;
		ed->registers[reg].rdata.region =
			xrealloc(ed->registers[reg].rdata.region, new_len);
		/* Use memcpy instead of strcat to be explicit about lengths */
		memcpy(ed->registers[reg].rdata.region + region_len, ed->kill,
		       kill_len + 1);
		ed->kill = tmp;
		registerMessage("Added region to register %s", reg);
		break;
	case REGISTER_NUMBER:
		ed->registers[reg].rdata.number++;
		registerMessage("Incremented number in register %s", reg);
		break;
	case REGISTER_POINT:
		registerMessage("Cannot increment point in register %s", reg);
		break;
	case REGISTER_MACRO:
		registerMessage("Cannot increment macro in register %s", reg);
		break;
	case REGISTER_RECTANGLE:
		registerMessage("Cannot increment rectangle in register %s",
				reg);
		break;
	}
}

void editorInsertRegister(struct editorConfig *ed, struct editorBuffer *bufr) {
	GET_REGISTER(reg, "Insert register");
	uint8_t *tmp = ed->kill;
	switch (ed->registers[reg].rtype) {
	case REGISTER_NULL:
		registerMessage("Nothing in register %s", reg);
		break;
	case REGISTER_REGION:
		ed->kill = ed->registers[reg].rdata.region;
		editorYank(ed, bufr, 1);
		ed->kill = tmp;
		registerMessage("Inserted string register %s", reg);
		break;
	case REGISTER_NUMBER:;
		char str[32];
		snprintf(str, sizeof(str), "%ld",
			 ed->registers[reg].rdata.number);
		ed->kill = (uint8_t *)str;
		editorYank(ed, bufr, 1);
		ed->kill = tmp;
		registerMessage("Inserted number register %s", reg);
		break;
	case REGISTER_POINT:
		registerMessage("Cannot insert point in register %s", reg);
		break;
	case REGISTER_MACRO:
		registerMessage("Cannot insert macro in register %s", reg);
		break;
	case REGISTER_RECTANGLE:;
		int ox = ed->rx;
		int oy = ed->ry;
		uint8_t *okill = ed->rectKill;
		ed->rectKill = ed->registers[reg].rdata.rect->rect;
		ed->rx = ed->registers[reg].rdata.rect->rx;
		ed->ry = ed->registers[reg].rdata.rect->ry;
		editorYankRectangle(ed, bufr);
		ed->rectKill = okill;
		ed->rx = ox;
		ed->ry = oy;
		registerMessage("Inserted rectangle register %s", reg);
		break;
	}
}

void editorViewRegister(struct editorConfig *ed,
			struct editorBuffer *UNUSED(bufr)) {
	GET_REGISTER(reg, "View register");
	char str[4];
	if (reg < 32) {
		snprintf(str, sizeof(str), "C-%c", reg + '@');
	} else {
		str[0] = reg;
		str[1] = 0;
	}
	switch (ed->registers[reg].rtype) {
	case REGISTER_NULL:
		editorSetStatusMessage("Register %s is empty.", str);
		break;
	case REGISTER_REGION:
		editorSetStatusMessage("%s (region): %.60s", str,
				       ed->registers[reg].rdata.region);
		break;
	case REGISTER_NUMBER:
		editorSetStatusMessage(
			"%s (number): %lld", str,
			(long long)ed->registers[reg].rdata.number);
		break;
	case REGISTER_POINT:;
		struct editorPoint *pt = ed->registers[reg].rdata.point;
		editorSetStatusMessage("%s (point): %.20s %d:%d", str,
				       pt->buf->filename, pt->cy + 1, pt->cx);
		break;
	case REGISTER_MACRO:
		editorSetStatusMessage(
			"Register %s contains a macro of length %d", str,
			ed->registers[reg].rdata.macro->nkeys);
		break;
	case REGISTER_RECTANGLE:
		editorSetStatusMessage("%s (rect): w: %d h: %d \"%.50s\"", str,
				       ed->registers[reg].rdata.rect->rx,
				       ed->registers[reg].rdata.rect->ry,
				       ed->registers[reg].rdata.rect->rect);
		break;
	}
}
