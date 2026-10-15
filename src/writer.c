#include "writer.h"

#include <stdio.h>
#include <string.h>

/* Write a 16-bit little-endian value. Returns 0 on success, -1 on error. */
static int put_le16(FILE* f, uint16_t v)
{
    unsigned char b[2] = {
        (unsigned char) (v & 0xff),
        (unsigned char) ((v >> 8) & 0xff),
    };
    return fwrite(b, 1, sizeof(b), f) == sizeof(b) ? 0 : -1;
}

/* Write a block: [type:1][length:2][content:length]. */
static int put_block(FILE* f, char type, const unsigned char* content, size_t len)
{
    unsigned char t = (unsigned char) type;

    if (fwrite(&t, 1, 1, f) != 1) {
        return -1;
    }
    if (put_le16(f, (uint16_t) len) != 0) {
        return -1;
    }
    if (len > 0 && fwrite(content, 1, len, f) != len) {
        return -1;
    }
    return 0;
}

int mpng_write_neb(const char* filename, uint32_t largeur, uint32_t hauteur,
                   const unsigned char* pixels, size_t pixels_len,
                   const char* comment)
{
    FILE* f = fopen(filename, "wb");
    if (!f) {
        perror(filename);
        return -1;
    }

    int rc = -1;

    if (fwrite("Mini-PNG", 1, 8, f) != 8) {
        goto done;
    }

    /* H block: largeur (4), hauteur (4), categorie (1) = NEB. */
    unsigned char h[9];
    h[0] = (unsigned char) (largeur & 0xff);
    h[1] = (unsigned char) ((largeur >> 8) & 0xff);
    h[2] = (unsigned char) ((largeur >> 16) & 0xff);
    h[3] = (unsigned char) ((largeur >> 24) & 0xff);
    h[4] = (unsigned char) (hauteur & 0xff);
    h[5] = (unsigned char) ((hauteur >> 8) & 0xff);
    h[6] = (unsigned char) ((hauteur >> 16) & 0xff);
    h[7] = (unsigned char) ((hauteur >> 24) & 0xff);
    h[8] = 0;
    if (put_block(f, 'H', h, sizeof(h)) != 0) {
        goto done;
    }

    if (comment != NULL) {
        if (put_block(f, 'C', (const unsigned char*) comment, strlen(comment)) != 0) {
            goto done;
        }
    }

    if (put_block(f, 'D', pixels, pixels_len) != 0) {
        goto done;
    }

    rc = 0;

done:
    if (fclose(f) != 0) {
        rc = -1;
    }
    return rc;
}
