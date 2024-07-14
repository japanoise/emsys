#include <stdint.h>

int unicodeTest();

int stringWidth(uint8_t *str);

int charInStringWidth(uint8_t *str, int idx);

int utf8_is2Char(uint8_t ch);

int utf8_is3Char(uint8_t ch);

int utf8_is4Char(uint8_t ch);

int utf8_nBytes(uint8_t ch);

int utf8_isCont(uint8_t ch);
