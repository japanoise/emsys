#ifndef EMSYS_BOUND_H
#define EMSYS_BOUND_H
#include <stdint.h>
#include "emsys.h"
int isWordBoundary(uint8_t c);
int isParaBoundary(erow *row);
#endif
