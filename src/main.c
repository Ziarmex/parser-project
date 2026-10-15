#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "display.h"
#include "font.h"
#include "writer.h"

int main(int argc, char* argv[])
{
    // Generate mode: ./main --mpfy <LETTER> writes <LETTER>.mp
    if (argc == 3 && strcmp(argv[1], "--mpfy") == 0) {
        char letter = argv[2][0];
        const unsigned char* bitmap = font_neb(letter);
        if (bitmap == NULL) {
            fprintf(stderr, "Unsupported letter: %c\n", letter);
            return EXIT_FAILURE;
        }

        char filename[16];
        snprintf(filename, sizeof(filename), "%c.mp", letter);

        char comment[32];
        snprintf(comment, sizeof(comment), "La lettre %c", letter);

        if (mpng_write_neb(filename, FONT_W, FONT_H, bitmap, FONT_H, comment) != 0) {
            fprintf(stderr, "Failed to write %s\n", filename);
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file.mp>\n", argv[0]);
        fprintf(stderr, "       %s --mpfy <LETTER>\n", argv[0]);
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
        char type = *((char *) (data + offset));
        uint16_t lambda = *((uint16_t *) (data + offset + 1));
        void* content = data + offset + 3;

        if (type == 'H') {
            largeur = *((uint32_t *) (content + 0));
            hauteur = *((uint32_t *) (content + 4));
            categorie = *((uint8_t *) (content + 8));

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

    // Category 0 is black & white (NEB).
    if (categorie == 0) {
        display_neb(largeur, hauteur, pixels, pixels_len);
    }

    free(pixels);
    free(data);
    return EXIT_SUCCESS;
}
