#ifndef DISPLAY_H
#define DISPLAY_H

#include <stddef.h>
#include <stdint.h>

/*
 * Display a black & white (NEB) image on stdout.
 *
 * pixels holds the concatenated contents of the D blocks: one bit per pixel,
 * most significant bit first, pixels stored left to right then top to bottom.
 * A bit set to 1 is a black pixel (printed as 'X'), 0 is white (a space).
 */
void display_neb(uint32_t largeur, uint32_t hauteur,
                 const unsigned char* pixels, size_t pixels_len);

#endif /* DISPLAY_H */
