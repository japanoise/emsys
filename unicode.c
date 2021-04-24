#include<stdio.h>
#include<stdint.h>
#include<wchar.h>
#include"wcwidth.h"
#include"unicode.h"

/* The UCS format used by wcwidth.c. NOT a general purpose function. */
static int utf8ToUCS(uint8_t *str, int idx) {
	int ret = 0;
	uint8_t ch = str[idx];
	if (utf8_is2Char(ch)) {
		ret = (ch&0x1F)<<6;
		ret |= (str[idx+1]&0x3F);
	} else if (utf8_is3Char(ch)) {
		ret = (ch&0x0F)<<12;
		ret |= ((str[idx+1]&0x3F)<<6);
		ret |= (str[idx+2]&0x3F);
	} else if (utf8_is4Char(ch)) {
		/* This currently won't work because wchar is too small */
		ret = (ch&0x07)<<18;
		ret |= ((str[idx+1]&0x3F)<<12);
		ret |= ((str[idx+2]&0x3F)<<6);
		ret |= (str[idx+3]&0x3F);
	} else {
		/* Why did you even call this, man */
		ret = str[idx];
	}
	return ret;
}

static int testCaseUCS(char *testCh, int expected) {
	int ucs = utf8ToUCS(testCh, 0);
	printf("%s\tgot %04x\texpected %04x\n", testCh, ucs, expected);
	return expected != ucs;
}

static int testCaseStringWidth(char *str, int expected) {
	int actual = stringWidth(str);
	printf("%s\tgot %i\texpected %i\n", str, actual, expected);
	return actual != expected;
}

int unicodeTest() {
	printf("UTF8 -> UCS conversion test");
	int retval = testCaseUCS("$", 0x24);
	retval = retval + testCaseUCS("\xC2\xA2", 0xA2);
	retval = retval + testCaseUCS("\xE0\xA4\xB9", 0x939);
	retval = retval + testCaseUCS("\xE2\x82\xAC", 0x20AC);
	retval = retval + testCaseUCS("\xED\x95\x9C", 0xD55C);
	retval = retval + testCaseUCS("\xF0\x90\x8D\x88", 0x10348);
	printf("Rune width test");
	retval += testCaseStringWidth("bruh", 4);
	retval += testCaseStringWidth("生存戦略", 8);
	return retval;
}

int stringWidth(uint8_t *str) {
	int idx = 0;
	int width = 0;

	while(str[idx] != 0) {
		width += charInStringWidth(str, idx);
		idx += utf8_nBytes(str[idx]);
	}

	return width;
}

int charInStringWidth(uint8_t *str, int idx) {
	if (str[idx] < 0x20) {
		return 2;
	} else if (str[idx] < 0x7f) {
		return 1;
	} else if (str[idx] == 0x7f) {
		/* The canonical way to display DEL is ^? */
		return 2;
	} else if (utf8_is4Char(str[idx])) {
		/* HACK: assume 1 width for high unicode. 
		 * Reason being wcwidth uses wchar, which is too small. */
		return 1;
		
	} else {
		wchar_t rune = utf8ToUCS(str, idx);
		return mk_wcwidth(rune);
	}
	
}

int utf8_is2Char(uint8_t ch) {
	return (0xC2 <= ch && ch <= 0xDF);
}

int utf8_is3Char(uint8_t ch) {
	return (0xE0 <= ch && ch <= 0xEF);
}

int utf8_is4Char(uint8_t ch) {
	return (0xF0 <= ch && ch <= 0xF4);
}

int utf8_nBytes(uint8_t ch) {
	if (ch < 0x80) {
		return 1;
	} else if (utf8_is4Char(ch)) {
		return 4;
	} else if (utf8_is3Char(ch)) {
		return 3;
	} else if (utf8_is2Char(ch)) {
		return 2;
	} else {
		return 1;
	}
}
