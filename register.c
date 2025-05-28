#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emsys.h"
#include "region.h"
#include "register.h"
#include "unicode.h"
#include "unused.h"

extern struct editorConfig E;

static int getRegisterName(char *prompt) {
	int key;
	int psize = stringWidth((uint8_t *)prompt);
	do {
		setStatusMessage("%s:", prompt);
		cursorBottomLine(psize + 2);
		refreshScreen();
		key = readKey();
	} while (key >= 127);
	recordKey(key);
	return key;
}

#define GET_REGISTER(vname, prompt)          \
	int vname = getRegisterName(prompt); \
	if (vname == 0x07) {                 \
		setStatusMessage("Quit");    \
		return;                      \
	}

static void registerMessage(char *msg, char reg) {
	char str[4];
	if (reg < 32) {
		sprintf(str, "C-%c", reg + '@');
	} else {
		str[0] = reg;
		str[1] = 0;
	}
	setStatusMessage(msg, str);
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

void jumpToRegister(void) {
	GET_REGISTER(reg, "Jump to register");
	switch (E.registers[reg].rtype) {
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
		if (E.buf == E.registers[reg].rdata.point->buf) {
			setMark();
		} else {
			E.buf = E.registers[reg].rdata.point->buf;
			for (int i = 0; i < E.nwindows; i++) {
				if (E.windows[i]->focused) {
					E.windows[i]->buf = E.buf;
				}
			}
			registerMessage("Jumped to point in register %s", reg);
		}
		struct editorBuffer *buf = E.buf;
		buf->cx = E.registers[reg].rdata.point->cx;
		buf->cy = E.registers[reg].rdata.point->cy;
		if (buf->cy >= buf->numrows) {
			buf->cy = buf->numrows > 0 ? buf->numrows - 1 : 0;
		}
		if (buf->cy < buf->numrows &&
		    buf->cx > buf->row[buf->cy].size) {
			buf->cx = buf->row[buf->cy].size;
		}
		break;
	case REGISTER_MACRO:
		registerMessage("Executing macro in register %s...", reg);
		execMacro(E.registers[reg].rdata.macro);
		break;
	case REGISTER_RECTANGLE:
		registerMessage("Cannot jump to rectangle in register %s", reg);
		break;
	}
}

void macroToRegister(void) {
	GET_REGISTER(reg, "Macro to register");
	clearRegister(&E, reg);
	E.registers[reg].rtype = REGISTER_MACRO;
	E.registers[reg].rdata.macro = xmalloc(sizeof(struct editorMacro));
	/* Copy data, creating a shallow copy. */
	memcpy(E.registers[reg].rdata.macro, &(E.macro),
	       sizeof(struct editorMacro));
	/* Now copy the keys too, to make a deep copy. */
	E.registers[reg].rdata.macro->keys =
		xmalloc(sizeof(int) * E.macro.skeys);
	memcpy(E.registers[reg].rdata.macro->keys, E.macro.keys,
	       E.macro.nkeys * sizeof(int));
	registerMessage("Saved macro to register %s", reg);
}

void pointToRegister(void) {
	GET_REGISTER(reg, "Point to register");
	clearRegister(&E, reg);
	E.registers[reg].rtype = REGISTER_POINT;
	E.registers[reg].rdata.point = xmalloc(sizeof(struct editorPoint));
	E.registers[reg].rdata.point->buf = E.buf;
	E.registers[reg].rdata.point->cx = E.buf->cx;
	E.registers[reg].rdata.point->cy = E.buf->cy;
	registerMessage("Saved point to register %s", reg);
}

void numberToRegister(int rept) {
	GET_REGISTER(reg, "Number to register");
	clearRegister(&E, reg);
	E.registers[reg].rtype = REGISTER_NUMBER;
	E.registers[reg].rdata.number = rept;
	registerMessage("Saved number to register %s", reg);
}

void regionToRegister(void) {
	if (markInvalid())
		return;
	GET_REGISTER(reg, "Region to register");
	clearRegister(&E, reg);
	uint8_t *tmp = E.kill;
	E.kill = NULL;
	copyRegion();
	E.registers[reg].rtype = REGISTER_REGION;
	E.registers[reg].rdata.region = E.kill;
	E.kill = tmp;
	registerMessage("Saved region to register %s", reg);
}

void rectRegister(void) {
	if (markInvalid())
		return;
	GET_REGISTER(reg, "Rectangle to register");
	clearRegister(&E, reg);
	uint8_t *tmp = E.rectKill;
	int rx = E.rx;
	int ry = E.ry;
	E.rectKill = NULL;
	copyRectangle();
	E.registers[reg].rtype = REGISTER_RECTANGLE;
	E.registers[reg].rdata.rect = xmalloc(sizeof(struct editorRectangle));
	E.registers[reg].rdata.rect->rect = E.rectKill;
	E.registers[reg].rdata.rect->rx = E.rx;
	E.registers[reg].rdata.rect->ry = E.ry;
	E.rectKill = tmp;
	E.rx = rx;
	E.ry = ry;
	registerMessage("Saved rectangle to register %s", reg);
}

void incrementRegister(void) {
	GET_REGISTER(reg, "Increment register");
	switch (E.registers[reg].rtype) {
	case REGISTER_NULL:
		registerMessage("Nothing in register %s", reg);
		break;
	case REGISTER_REGION:;
		uint8_t *tmp = E.kill;
		E.kill = NULL;
		copyRegion();
		int len = strlen((char *)E.kill) +
			  strlen((char *)E.registers[reg].rdata.region) + 1;
		E.registers[reg].rdata.region =
			xrealloc(E.registers[reg].rdata.region, len);
		strcat((char *)E.registers[reg].rdata.region, (char *)E.kill);
		E.kill = tmp;
		registerMessage("Added region to register %s", reg);
		break;
	case REGISTER_NUMBER:
		E.registers[reg].rdata.number++;
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

void insertRegister(void) {
	CHECK_READ_ONLY(E.buf);
	GET_REGISTER(reg, "Insert register");
	uint8_t *tmp = E.kill;
	switch (E.registers[reg].rtype) {
	case REGISTER_NULL:
		registerMessage("Nothing in register %s", reg);
		break;
	case REGISTER_REGION:
		E.kill = E.registers[reg].rdata.region;
		yank(1);
		E.kill = tmp;
		registerMessage("Inserted string register %s", reg);
		break;
	case REGISTER_NUMBER:;
		char str[32];
		snprintf(str, sizeof(str), "%lld",
			 (long long)E.registers[reg].rdata.number);
		E.kill = (uint8_t *)str;
		yank(1);
		E.kill = tmp;
		registerMessage("Inserted number register %s", reg);
		break;
	case REGISTER_POINT:
		registerMessage("Cannot insert point in register %s", reg);
		break;
	case REGISTER_MACRO:
		registerMessage("Cannot insert macro in register %s", reg);
		break;
	case REGISTER_RECTANGLE:;
		int ox = E.rx;
		int oy = E.ry;
		uint8_t *okill = E.rectKill;
		E.rectKill = E.registers[reg].rdata.rect->rect;
		E.rx = E.registers[reg].rdata.rect->rx;
		E.ry = E.registers[reg].rdata.rect->ry;
		yankRectangle();
		E.rectKill = okill;
		E.rx = ox;
		E.ry = oy;
		registerMessage("Inserted rectangle register %s", reg);
		break;
	}
}

void viewRegister(void) {
	GET_REGISTER(reg, "View register");
	char str[4];
	if (reg < 32) {
		sprintf(str, "C-%c", reg + '@');
	} else {
		str[0] = reg;
		str[1] = 0;
	}
	switch (E.registers[reg].rtype) {
	case REGISTER_NULL:
		setStatusMessage("Register %s is empty.", str);
		break;
	case REGISTER_REGION:
		setStatusMessage("%s (region): %.60s", str,
				 E.registers[reg].rdata.region);
		break;
	case REGISTER_NUMBER:
		setStatusMessage("%s (number): %ld", str,
				 (long)E.registers[reg].rdata.number);
		break;
	case REGISTER_POINT:;
		struct editorPoint *pt = E.registers[reg].rdata.point;
		setStatusMessage("%s (point): %.20s %d:%d", str,
				 pt->buf->filename, pt->cy + 1, pt->cx);
		break;
	case REGISTER_MACRO:
		setStatusMessage("Register %s contains a macro of length %d",
				 str, E.registers[reg].rdata.macro->nkeys);
		break;
	case REGISTER_RECTANGLE:
		setStatusMessage("%s (rect): w: %d h: %d \"%.50s\"", str,
				 E.registers[reg].rdata.rect->rx,
				 E.registers[reg].rdata.rect->ry,
				 E.registers[reg].rdata.rect->rect);
		break;
	}
}
