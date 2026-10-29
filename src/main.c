#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "display.h"
#include "font.h"
#include "writer.h"

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

int main(int argc, char* argv[])
{
    // Generate mode: ./main --mpfy <LETTER> [--nvg]
    //   without --nvg: writes <LETTER>.mp      (black & white)
    //   with --nvg:    writes <LETTER>_nvg.mp  (grayscale)
    if (argc >= 3 && strcmp(argv[1], "--mpfy") == 0) {
        int gray = (argc == 4 && strcmp(argv[3], "--nvg") == 0);
        if (argc > 4 || (argc == 4 && !gray) || argv[2][1] != '\0') {
            fprintf(stderr, "Usage: %s --mpfy <LETTER> [--nvg]\n", argv[0]);
            return EXIT_FAILURE;
        }

        char letter = argv[2][0];
        char filename[16];
        char comment[32];
        snprintf(filename, sizeof(filename), gray ? "%c_nvg.mp" : "%c.mp", letter);
        snprintf(comment, sizeof(comment), "La lettre %c", letter);

        int rc;
        if (gray) {
            unsigned char bitmap[FONT_W * FONT_H];
            if (font_nvg(letter, bitmap) != 0) {
                fprintf(stderr, "Unsupported letter: %c\n", letter);
                return EXIT_FAILURE;
            }
            rc = mpng_write(filename, FONT_W, FONT_H, 1, bitmap, sizeof(bitmap), comment);
        } else {
            const unsigned char* bitmap = font_neb(letter);
            if (bitmap == NULL) {
                fprintf(stderr, "Unsupported letter: %c\n", letter);
                return EXIT_FAILURE;
            }
            rc = mpng_write_neb(filename, FONT_W, FONT_H, bitmap, FONT_H, comment);
        }

        if (rc != 0) {
            fprintf(stderr, "Failed to write %s\n", filename);
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file.mp>\n", argv[0]);
        fprintf(stderr, "       %s --mpfy <LETTER> [--nvg]\n", argv[0]);
        return EXIT_FAILURE;
    }

    FILE* file = fopen(argv[1], "rb");
    if (!file) {
        perror(argv[1]);
        return EXIT_FAILURE;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        perror("fseek");
        fclose(file);
        return EXIT_FAILURE;
    }

    long file_size = ftell(file);
    if (file_size < 0) {
        perror("ftell");
        fclose(file);
        return EXIT_FAILURE;
    }

    rewind(file);

    void* data = malloc((size_t) file_size);
    if (!data) {
        perror("malloc");
        fclose(file);
        return EXIT_FAILURE;
    }

    if (fread(data, 1, (size_t) file_size, file) != (size_t) file_size) {
        perror("fread");
        free(data);
        fclose(file);
        return EXIT_FAILURE;
    }

    fclose(file);

    // 8-byte magic number, must be "Mini-PNG"
    if (file_size < 8 || memcmp(data, "Mini-PNG", 8) != 0) {
        fprintf(stderr, "Not a Mini-PNG file\n");
        free(data);
        return EXIT_FAILURE;
    }

    uint32_t largeur = 0;
    uint32_t hauteur = 0;
    uint8_t categorie = 0;

    // Pixels of every D block, concatenated.
    unsigned char* pixels = NULL;
    size_t pixels_len = 0;

    // Walk the blocks one by one.
    // Each block is [type:1 byte][lambda:2 bytes][content:lambda bytes].
    // The first block starts right after the 8-byte magic number.
    size_t offset = 8;

    while (offset + 3 <= (size_t) file_size) {
        const unsigned char* block = (const unsigned char*) data + offset;
        char type = (char) block[0];
        uint16_t lambda = read_le16(block + 1);
        const unsigned char* content = block + 3;

        // The block must fit inside the file, otherwise we would read
        // (and copy) bytes that are past the end of the buffer.
        if ((size_t) lambda > (size_t) file_size - offset - 3) {
            fprintf(stderr, "Truncated block '%c'\n", type);
            free(pixels);
            free(data);
            return EXIT_FAILURE;
        }

        if (type == 'H') {
            // The H block holds 4 + 4 + 1 = 9 bytes.
            if (lambda < 9) {
                fprintf(stderr, "H block too short\n");
                free(pixels);
                free(data);
                return EXIT_FAILURE;
            }

            largeur = read_le32(content);
            hauteur = read_le32(content + 4);
            categorie = content[8];

            printf("Largeur = %u\n", largeur);
            printf("Hauteur = %u\n", hauteur);
            printf("Catégorie de pixel = %u\n", categorie);
        } else if (type == 'C') {
            printf("Commentaires = %.*s\n", (int) lambda, (char*) content);
        } else if (type == 'D') {
            unsigned char* tmp = realloc(pixels, pixels_len + lambda);
            if (!tmp) {
                perror("realloc");
                free(pixels);
                free(data);
                return EXIT_FAILURE;
            }
            pixels = tmp;
            memcpy(pixels + pixels_len, content, lambda);
            pixels_len += lambda;
        }

        offset += 3 + lambda;
    }

    // Check the amount of pixel data against the header (uint64_t so that
    // largeur * hauteur cannot overflow).
    //   NEB: 1 bit per pixel  -> ceil(w * h / 8) bytes
    //   NVG: 1 byte per pixel -> w * h bytes
    if (categorie == 0 || categorie == 1) {
        uint64_t nb_pixels = (uint64_t) largeur * hauteur;
        uint64_t expected = categorie == 0 ? (nb_pixels + 7) / 8 : nb_pixels;

        if (pixels_len != expected) {
            fprintf(stderr, "Wrong amount of pixel data: expected %llu bytes, got %zu\n",
                    (unsigned long long) expected, pixels_len);
            free(pixels);
            free(data);
            return EXIT_FAILURE;
        }
    }

    if (categorie == 0) {
        display_neb(largeur, hauteur, pixels, pixels_len);
    } else if (categorie == 1) {
        display_nvg(largeur, hauteur, pixels, pixels_len);
    }

    free(pixels);
    free(data);
    return EXIT_SUCCESS;
}