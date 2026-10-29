#ifndef WRITER_H
#define WRITER_H

#include <stddef.h>
#include <stdint.h>

/*
 * Write a black & white (NEB) Mini-PNG image to `filename`.
 *
 * `pixels` holds 1 bit per pixel, most significant bit first, stored left to
 * right then top to bottom (the concatenated D block contents).
 * `comment` is an optional NUL-terminated string (NULL to omit the C block).
 *
 * Returns 0 on success, non-zero on error.
 */

int mpng_write(const char* filename, uint32_t largeur, uint32_t hauteur,
               uint8_t categorie, const unsigned char* pixels,
               size_t pixels_len, const char* comment);

int mpng_write_neb(const char* filename, uint32_t largeur, uint32_t hauteur,
                   const unsigned char* pixels, size_t pixels_len,
                   const char* comment);

#endif /* WRITER_H */
