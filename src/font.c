#include "font.h"

/* 'Z': top bar, descending diagonal, bottom bar. 1 = black, 0 = white. */
static const unsigned char Z_BITMAP[FONT_H] = {
    0xff, /* XXXXXXXX */
    0x01, /* .......X */
    0x02, /* ......X. */
    0x04, /* .....X.. */
    0x08, /* ....X... */
    0x10, /* ...X.... */
    0x20, /* ..X..... */
    0x40, /* .X...... */
    0x80, /* X....... */
    0xff, /* XXXXXXXX */
};

const unsigned char* font_neb(char letter)
{
    switch (letter) {
    case 'Z':
        return Z_BITMAP;
    default:
        return NULL;
    }
}
