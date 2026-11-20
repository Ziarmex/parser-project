#ifndef PARSER_H
#define PARSER_H

#include <stddef.h>
#include <stdint.h>

/*
 * Mini-PNG parser.
 *
 * This module only looks at bytes already in memory: it does no file I/O and
 * prints nothing. The caller reads the file, calls mpng_parse(), then prints
 * or displays the result.
 */

/* Pixel categories stored in the H block. */
enum mpng_category {
    MPNG_CAT_NEB = 0, /* black & white, 1 bit per pixel   */
    MPNG_CAT_NVG = 1, /* grayscale,     1 byte per pixel  */
    MPNG_CAT_C24 = 2  /* true colour,   3 bytes per pixel */
};

enum mpng_status {
    MPNG_OK = 0,
    MPNG_ERR_MAGIC,        /* missing or wrong "Mini-PNG" marker            */
    MPNG_ERR_TRUNCATED,    /* a block runs past the end of the buffer       */
    MPNG_ERR_HEADER_SHORT, /* H block has fewer than 9 bytes                */
    MPNG_ERR_COMMENT_CHARS,/* C block holds a byte outside 0x20..0x7E       */
    MPNG_ERR_TOO_LARGE,    /* width * height * bytes-per-pixel overflows    */
    MPNG_ERR_PIXEL_COUNT,  /* D data does not match the header dimensions   */
    MPNG_ERR_NOMEM         /* out of memory                                 */
};

/* One C block. `text` points INTO the buffer given to mpng_parse(). */
struct mpng_comment {
    const unsigned char* text;
    uint16_t len;
};

/* Extra detail about the last failure; only meaningful when mpng_parse()
 * returned the matching error. */
struct mpng_diag {
    char block_type;    /* MPNG_ERR_TRUNCATED: type of the cut block, or '?'
                           if it is not printable (safe to print as is)     */
    uint64_t expected;  /* MPNG_ERR_PIXEL_COUNT: bytes the header calls for */
    size_t got;         /* MPNG_ERR_PIXEL_COUNT: bytes the D blocks hold    */
};

struct mpng {
    int has_header;     /* 1 if at least one H block was found */
    uint32_t largeur;   /* width,  from the last H block       */
    uint32_t hauteur;   /* height, from the last H block       */
    uint8_t categorie;  /* enum mpng_category value            */

    struct mpng_comment* comments; /* every C block, in file order */
    size_t n_comments;

    unsigned char* pixels;         /* all D blocks concatenated (owned) */
    size_t pixels_len;

    struct mpng_diag diag;
};

/*
 * Parse the Mini-PNG held in buf[0..len).
 *
 * Returns MPNG_OK, or an error code. `out` is always overwritten:
 *  - on success it owns allocated memory: release it with mpng_free();
 *  - on failure it is already empty (nothing to free) and only `diag` is
 *    meaningful.
 *
 * `buf` must stay alive and unchanged for as long as `out->comments[i].text`
 * is used, because those pointers refer into it.
 */
enum mpng_status mpng_parse(const unsigned char* buf, size_t len,
                            struct mpng* out);

/* Release what mpng_parse() allocated. Safe on a zeroed struct, on a struct
 * from a failed parse, and when called twice. */
void mpng_free(struct mpng* img);

#endif /* PARSER_H */