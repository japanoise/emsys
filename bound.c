#include <stdint.h>
#include "bound.h"
#include "emsys.h"

int isWordBoundary(uint8_t c) {
	return (c == ' ' || c == CTRL('i') || c == '_' || c == 10 || c == 0 ||
		c == '(' || c == ')' || c == '[' || c == ']' ||
		c == '{' || c == '}');
}

int isParaBoundary(erow *row) {
	return (row->size == 0);
}
