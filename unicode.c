#include <sys/types.h>
#include <stdio.h>
#include <stdint.h>
#include <wchar.h>
#include "wcwidth.h"
#include "unicode.h"
#include "emsys.h"

/* The UCS format used by wcwidth.c. NOT a general purpose function. */
static int utf8ToUCS(uint8_t *str, int idx) {
	int ret = 0;
	uint8_t ch = str[idx];
	if (utf8_is2Char(ch)) {
		ret = (ch & 0x1F) << 6;
		ret |= (str[idx + 1] & 0x3F);
	} else if (utf8_is3Char(ch)) {
		ret = (ch & 0x0F) << 12;
		ret |= ((str[idx + 1] & 0x3F) << 6);
		ret |= (str[idx + 2] & 0x3F);
	} else if (utf8_is4Char(ch)) {
		/* This currently won't work because wchar is too small */
		ret = (ch & 0x07) << 18;
		ret |= ((str[idx + 1] & 0x3F) << 12);
		ret |= ((str[idx + 2] & 0x3F) << 6);
		ret |= (str[idx + 3] & 0x3F);
	} else {
		/* Why did you even call this, man */
		ret = str[idx];
	}
	return ret;
}

/* Convert a 32 bit value to UTF-8, assuming that dest is big enough
 * to store it. returns number of bytes (1-4) written. */
static ssize_t rune_to_utf8(uint8_t *dest, uint32_t ru) {
	/*
	 * for continuation bytes
	 * 00111111 = 3F
	 * 10000000 = 80
	 * 10111111 = BF
	 *
	 * for 2-bytes
	 * 00011111 = 1F
	 * 11000000 = C0
	 * 11011111 = DF
	 *
	 * for 3-bytes
	 * 00001111 = 0F
	 * 11100000 = E0
	 * 11101111 = EF
	 *
	 * for 4-bytes
	 * 00000111 = 07
	 * 11110000 = F0
	 * 11110111 = F7
	 */
	if (ru < 0x80) {
		/* ASCII */
		dest[0] = (uint8_t)ru;
		return 1;
	} else if (ru < 0x0800) {
		/* 2 bytes */
		dest[0] = ((uint8_t)(ru >> 6) & 0x1F) | 0xC0;
		dest[1] = ((uint8_t)ru & 0x3F) | 0x80;
		return 2;
	} else if (ru < 0x10000) {
		/* 3 bytes */
		dest[0] = ((uint8_t)(ru >> 12) & 0x0F) | 0xE0;
		dest[1] = ((uint8_t)(ru >> 6) & 0x3F) | 0x80;
		dest[2] = ((uint8_t)ru & 0x3F) | 0x80;
		return 3;
	} else {
		/* 4 bytes */
		dest[0] = ((uint8_t)(ru >> 18) & 0x07) | 0xF0;
		dest[1] = ((uint8_t)(ru >> 12) & 0x3F) | 0x80;
		dest[2] = ((uint8_t)(ru >> 6) & 0x3F) | 0x80;
		dest[3] = ((uint8_t)ru & 0x3F) | 0x80;
		return 4;
	}
}

static int testCaseUCS(char *testCh, int expected) {
	int ucs = utf8ToUCS(testCh, 0);
	printf("%s\tgot %04x\texpected %04x\n", testCh, ucs, expected);
	return expected != ucs;
}

static int testCaseReverseUCS(char *expectedChars, int expectedWidth,
			      int input) {
	uint8_t result[4];
	ssize_t actualWidth;
	int resultsNotMatch = 0;
	int i = 0;

	actualWidth = rune_to_utf8(result, input);

	while (expectedChars[i] != 0) {
		printf("%i actual: %02x expected: %02x\n", i, result[i],
		       (uint8_t)expectedChars[i]);
		resultsNotMatch += result[i] != (uint8_t)expectedChars[i];
		i++;
	}
	printf("expected width %i actual %zd\n", expectedWidth, actualWidth);

	return (actualWidth != expectedWidth) + resultsNotMatch;
}

static int testCaseStringWidth(char *str, int expected) {
	int actual = stringWidth(str);
	printf("%s\tgot %i\texpected %i\n", str, actual, expected);
	return actual != expected;
}

int unicodeTest() {
	printf("UTF8 -> UCS conversion test\n");
	int retval = testCaseUCS("$", 0x24);
	retval = retval + testCaseUCS("\xC2\xA2", 0xA2);
	retval = retval + testCaseUCS("\xE0\xA4\xB9", 0x939);
	retval = retval + testCaseUCS("\xE2\x82\xAC", 0x20AC);
	retval = retval + testCaseUCS("\xED\x95\x9C", 0xD55C);
	retval = retval + testCaseUCS("\xF0\x90\x8D\x88", 0x10348);
	retval = retval + testCaseUCS("\xF0\x9f\x98\x87", 0x1f607);
	printf("Rune width test\n");
	retval += testCaseStringWidth("bruh", 4);
	retval += testCaseStringWidth("生存戦略", 8);
	retval += testCaseStringWidth("😇", 2);
	printf("UCS -> UTF8 conversion test\n");
	retval = retval + testCaseReverseUCS("\xC2\xA2", 2, 0xA2);
	retval = retval + testCaseReverseUCS("\xE0\xA4\xB9", 3, 0x939);
	retval = retval + testCaseReverseUCS("\xE2\x82\xAC", 3, 0x20AC);
	retval = retval + testCaseReverseUCS("\xED\x95\x9C", 3, 0xD55C);
	retval = retval + testCaseReverseUCS("\xF0\x90\x8D\x88", 4, 0x10348);
	retval = retval + testCaseReverseUCS("\xF0\x9f\x98\x87", 4, 0x1f607);
	return retval;
}

int stringWidth(uint8_t *str) {
	int idx = 0;
	int width = 0;

	while (str[idx] != 0) {
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
	} else {
		int rune = utf8ToUCS(str, idx);
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

int utf8_isCont(uint8_t ch) {
	return (0x80 <= ch && ch <= 0xBF);
}

int nextScreenX(uint8_t *str, int *idx, int screen_x) {
	uint8_t ch = str[*idx];

	if (ch == '\t') {
		/* Move to next tab stop */
		screen_x = ((screen_x / EMSYS_TAB_STOP) + 1) * EMSYS_TAB_STOP;
	} else if (ch < 0x20 || ch == 0x7f) {
		/* Control characters display as ^X */
		screen_x += 2;
	} else if (ch < 0x80) {
		/* Regular ASCII */
		screen_x += 1;
	} else {
		/* Unicode character */
		int width = charInStringWidth(str, *idx);
		screen_x += width;
		/* Skip continuation bytes */
		int nbytes = utf8_nBytes(ch);
		*idx += nbytes - 1;
	}

	return screen_x;
}
