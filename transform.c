#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "bound.h"
#include "transform.h"
#include "unicode.h"

#define MKOUTPUT(in, l, o)  \
	int l = strlen(in); \
	uint8_t *o = malloc(l + 1)

uint8_t *transformerUpcase(uint8_t *input) {
	MKOUTPUT(input, len, output);

	for (int i = 0; i < len; i++) {
		uint8_t c = input[i];
		if ('a' <= c && c <= 'z') {
			c &= 0x5f;
		}
		output[i] = c;
	}

	return output;
}

uint8_t *transformerDowncase(uint8_t *input) {
	MKOUTPUT(input, len, output);

	for (int i = 0; i < len; i++) {
		uint8_t c = input[i];
		if ('A' <= c && c <= 'Z') {
			c |= 0x60;
		}
		output[i] = c;
	}

	return output;
}

uint8_t *transformerCapitalCase(uint8_t *input) {
	MKOUTPUT(input, len, output);

	int first = 1;

	for (int i = 0; i < len; i++) {
		uint8_t c = input[i];
		if ((('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z')) &&
		    first) {
			first = 0;
			c &= 0x5f;
		} else if ('A' <= c && c <= 'Z') {
			c |= 0x60;
		} else if (isWordBoundary(c)) {
			first = 1;
		}
		output[i] = c;
	}

	return output;
}

uint8_t *transformerTransposeChars(uint8_t *input) {
	MKOUTPUT(input, len, output);

	if (len == 0) {
		output[0] = 0;
		return output;
	}

	int endFirst = utf8_nBytes(input[0]);

	if (endFirst > len) {
		memcpy(output, input, len + 1);
		return output;
	}

	memcpy(output, input + endFirst, len - endFirst);
	memcpy(output + (len - endFirst), input, endFirst);

	output[len] = 0;

	return output;
}

uint8_t *transformerTransposeWords(uint8_t *input) {
	if (input == NULL) {
		return (uint8_t *)stringdup("");
	}

	int inputLen = strlen(input);
	if (inputLen == 0) {
		return (uint8_t *)stringdup("");
	}

	MKOUTPUT(input, len, output);

	if (output == NULL) {
		return NULL;
	}

	int endFirst = -1, startSecond = -1;
	int which = 0;
	for (int i = 0; i < len; i++) {
		if (!which) {
			if (isWordBoundary(input[i])) {
				which++;
				endFirst = i;
			}
		} else {
			if (!isWordBoundary(input[i])) {
				startSecond = i;
				break;
			}
		}
	}

	if (endFirst == -1 || startSecond == -1 || endFirst >= startSecond ||
	    startSecond > len || endFirst < 0) {
		memcpy(output, input, len + 1);
		return output;
	}

	if (startSecond > len || endFirst >= startSecond) {
		memcpy(output, input, len + 1);
		return output;
	}

	int part1_len = endFirst;
	int part2_len = startSecond - endFirst;
	int part3_len = len - startSecond;

	if (part1_len < 0 || part2_len < 0 || part3_len < 0 ||
	    part1_len + part2_len + part3_len != len) {
		memcpy(output, input, len + 1);
		return output;
	}

	int offset = 0;
	if (part3_len > 0) {
		memcpy(output + offset, input + startSecond, part3_len);
		offset += part3_len;
	}
	if (part2_len > 0) {
		memcpy(output + offset, input + endFirst, part2_len);
		offset += part2_len;
	}
	if (part1_len > 0) {
		memcpy(output + offset, input, part1_len);
		offset += part1_len;
	}

	output[len] = 0;

	return output;
}
