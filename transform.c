#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "transform.h"

uint8_t* transformerUpcase(uint8_t* input) {
	int len = strlen((char *)input);
	uint8_t *output = malloc(len+1);

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
	int len = strlen((char *)input);
	uint8_t *output = malloc(len+1);

	for (int i = 0; i <= len; i++) {
		uint8_t c = input[i];
		if ('A' <= c && c <= 'Z') {
			c |= 0x60;
		}
		output[i] = c;
	}

	return output;
}
