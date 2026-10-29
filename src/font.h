#ifndef FONT_H_INCLUDED
#define FONT_H_INCLUDED

#include <stddef.h>
#include <stdint.h>

#define FONT_W 8
#define FONT_H 10

/*
 * Return the 8x10 black & white (NEB) bitmap of an uppercase letter:
 * FONT_H bytes, 1 bit per pixel, most significant bit first, one row after
 * another. Returns NULL if the letter is not supported.
 */
const unsigned char* font_neb(char letter);

int font_nvg(char letter, unsigned char* out);

#endif /* FONT_H_INCLUDED */
