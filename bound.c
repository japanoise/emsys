#include <stdint.h>
#include "bound.h"
#include "emsys.h"

int isWordBoundary(uint8_t c) {
	return !(c > '~') && 	/* Anything outside ASCII is not a boundary */
		!('a' <= c && c <= 'z') && /* Lower case ASCII not boundaries */
		!('A' <= c && c <= 'Z') && /* Same with caps */
		!('0' <= c && c <= '9') && /* And numbers */
		((c < '$') || 	/* ctrl chars & some punctuation */
		 (c > '%'));	/* Rest of ascii outside $% & other ranges */
}

int isParaBoundary(erow *row) {
	return (row->size == 0);
}
