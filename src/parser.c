#include "parser.h"

#include <stdlib.h>
#include <string.h>

#define MPNG_MAGIC     "Mini-PNG"
#define MPNG_MAGIC_LEN 8
#define BLOCK_HEAD_LEN 3 /* type (1 byte) + length lambda (2 bytes) */
#define HEADER_LEN     9 /* H block: 4 + 4 + 1 bytes                */

// Read a 16-bit / 32-bit little-endian value byte by byte. Unlike a pointer
// cast, this is safe on unaligned addresses and gives the same result on
// every CPU (x86_64, ARM, big-endian...).
static uint16_t read_le16(const unsigned char* p)
{
    return (uint16_t) (p[0] | (p[1] << 8));
}

static uint32_t read_le32(const unsigned char* p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16)
           | ((uint32_t) p[3] << 24);
}

// 1 if the byte is printable ASCII (0x20..0x7E). Anything else (ESC, BEL,
// newline, NUL, bytes >= 0x80...) could drive the terminal if printed raw.
static int is_printable_ascii(unsigned char c)
{
    return c >= 0x20 && c <= 0x7E;
}

void mpng_free(struct mpng* img)
{
    free(img->pixels);
    free(img->comments);
    img->pixels = NULL;
    img->pixels_len = 0;
    img->comments = NULL;
    img->n_comments = 0;
}

// First pass: add up the D data and count the C blocks so each buffer can be
// allocated ONCE. (No realloc(ptr, 0): its result is implementation defined,
// and treating its NULL as "out of memory" caused a double free.)
// This walk stops silently at a truncated block; the real walk in
// mpng_parse() is the one that reports it, so errors still come out in file
// order.
static void measure(const unsigned char* buf, size_t len, size_t* pixels_total,
                    size_t* n_comments)
{
    *pixels_total = 0;
    *n_comments = 0;

    for (size_t pos = MPNG_MAGIC_LEN; pos + BLOCK_HEAD_LEN <= len;) {
        const unsigned char* b = buf + pos;
        size_t lambda = read_le16(b + 1);

        if (lambda > len - pos - BLOCK_HEAD_LEN) {
            return;
        }
        if (b[0] == 'D') {
            *pixels_total += lambda;
        } else if (b[0] == 'C') {
            (*n_comments)++;
        }
        pos += BLOCK_HEAD_LEN + lambda;
    }
}

// Check the amount of pixel data against the header (uint64_t so that
// largeur * hauteur cannot overflow).
//   NEB: 1 bit per pixel   -> ceil(w * h / 8) bytes
//   NVG: 1 byte per pixel  -> w * h bytes
//   C24: 3 bytes per pixel -> 3 * w * h bytes
// Unknown categories are not checked.
static enum mpng_status check_pixel_len(struct mpng* img)
{
    if (img->categorie > MPNG_CAT_C24) {
        return MPNG_OK;
    }

    uint64_t nb_pixels = (uint64_t) img->largeur * img->hauteur;
    uint64_t expected;

    if (img->categorie == MPNG_CAT_NEB) {
        expected = (nb_pixels + 7) / 8;
    } else if (img->categorie == MPNG_CAT_NVG) {
        expected = nb_pixels;
    } else {
        if (nb_pixels > UINT64_MAX / 3) {
            return MPNG_ERR_TOO_LARGE;
        }
        expected = nb_pixels * 3;
    }

    if (img->pixels_len != expected) {
        img->diag.expected = expected;
        img->diag.got = img->pixels_len;
        return MPNG_ERR_PIXEL_COUNT;
    }
    return MPNG_OK;
}

enum mpng_status mpng_parse(const unsigned char* buf, size_t len,
                            struct mpng* out)
{
    enum mpng_status st = MPNG_OK;
    size_t pixels_total = 0;
    size_t n_comments = 0;

    // Each block is [type:1 byte][lambda:2 bytes][content:lambda bytes].
    // The first block starts right after the 8-byte magic number.
    size_t offset = MPNG_MAGIC_LEN;

    memset(out, 0, sizeof(*out));

    if (len < MPNG_MAGIC_LEN || memcmp(buf, MPNG_MAGIC, MPNG_MAGIC_LEN) != 0) {
        st = MPNG_ERR_MAGIC;
        goto cleanup;
    }

    measure(buf, len, &pixels_total, &n_comments);

    // No D data (or no C block) keeps the pointer NULL with a length of 0.
    if (pixels_total > 0) {
        out->pixels = malloc(pixels_total);
        if (!out->pixels) {
            st = MPNG_ERR_NOMEM;
            goto cleanup;
        }
    }
    if (n_comments > 0) {
        out->comments = calloc(n_comments, sizeof(*out->comments));
        if (!out->comments) {
            st = MPNG_ERR_NOMEM;
            goto cleanup;
        }
    }

    // Walk the blocks one by one. The counters below cannot overrun the
    // buffers because measure() walked exactly the same blocks.
    while (offset + BLOCK_HEAD_LEN <= len) {
        const unsigned char* block = buf + offset;
        char type = (char) block[0];
        uint16_t lambda = read_le16(block + 1);
        const unsigned char* content = block + BLOCK_HEAD_LEN;

        // The block must fit inside the buffer, otherwise we would read
        // (and copy) bytes that are past its end.
        if ((size_t) lambda > len - offset - BLOCK_HEAD_LEN) {
            out->diag.block_type =
                is_printable_ascii((unsigned char) type) ? type : '?';
            st = MPNG_ERR_TRUNCATED;
            goto cleanup;
        }

        if (type == 'H') {
            if (lambda < HEADER_LEN) {
                st = MPNG_ERR_HEADER_SHORT;
                goto cleanup;
            }
            out->has_header = 1;
            out->largeur = read_le32(content);
            out->hauteur = read_le32(content + 4);
            out->categorie = content[8];
        } else if (type == 'C') {
            // The spec says a C block holds printable characters. Enforce
            // it so the caller can print the text without surprises.
            for (uint16_t i = 0; i < lambda; i++) {
                if (!is_printable_ascii(content[i])) {
                    st = MPNG_ERR_COMMENT_CHARS;
                    goto cleanup;
                }
            }
            out->comments[out->n_comments].text = content;
            out->comments[out->n_comments].len = lambda;
            out->n_comments++;
        } else if (type == 'D') {
            // Empty blocks are skipped so we never memcpy into a NULL buffer.
            if (lambda > 0) {
                memcpy(out->pixels + out->pixels_len, content, lambda);
                out->pixels_len += lambda;
            }
        }

        offset += BLOCK_HEAD_LEN + lambda;
    }

    st = check_pixel_len(out);

cleanup:
    if (st != MPNG_OK) {
        mpng_free(out);
    }
    return st;
}