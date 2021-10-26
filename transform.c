#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "bound.h"
#include "transform.h"
#include "unicode.h"

#define MKOUTPUT(in, l, o) int l = strlen(in); uint8_t *o = malloc(l+1)

uint8_t* transformerUpcase(uint8_t* input) {
	MKOUTPUT(input, len, output);

	for (int i = 0; i <= len; i++) {
		uint8_t c = input[i];
		if ('a' <= c && c <= 'z') {
			c &= 0x5f;
		}
		output[i] = c;
	}

	return output;
}

uint8_t* transformerDowncase(uint8_t* input) {
	MKOUTPUT(input, len, output);

	for (int i = 0; i <= len; i++) {
		uint8_t c = input[i];
		if ('A' <= c && c <= 'Z') {
			c |= 0x60;
		}
		output[i] = c;
	}

	return output;
}

uint8_t* transformerCapitalCase(uint8_t* input) {
	MKOUTPUT(input, len, output);

	int first = 1;

	for (int i = 0; i <= len; i++) {
		uint8_t c = input[i];
		if ((('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z')) && first) {
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

uint8_t* transformerTransposeChars(uint8_t* input) {
	MKOUTPUT(input, len, output);

	int endFirst=utf8_nBytes(input[0]);

	memcpy(output, input+endFirst, len-endFirst);
	memcpy(output+(len-endFirst), input, endFirst);

	output[len] = 0;

	return output;
}

uint8_t* transformerTransposeWords(uint8_t* input) {
	MKOUTPUT(input, len, output);

	int endFirst, startSecond = 0;
	int which = 0;
	for (int i = 0; i <= len; i++) {
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
	int offset = 0;
	memcpy(output, input+startSecond, len-startSecond);
	offset += len-startSecond;
	memcpy(output+offset, input+endFirst, startSecond-endFirst);
	offset += startSecond-endFirst;
	memcpy(output+offset, input, endFirst);

	output[len] = 0;

	return output;
}
