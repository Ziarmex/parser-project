#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "display.h"
#include "font.h"
#include "parser.h"
#include "writer.h"

// Largest input file we accept (64 MiB). The whole file is loaded in memory,
// so without a cap a huge file would exhaust RAM.
#define MAX_FILE_SIZE ((off_t) 64 * 1024 * 1024)

// Generate mode: ./main --mpfy <LETTER> [--nvg|--c24]
//   without option: writes <LETTER>.mp      (black & white)
//   with --nvg:     writes <LETTER>_nvg.mp  (grayscale)
//   with --c24:     writes <LETTER>_c24.mp  (true colours)
static int run_mpfy(int argc, char* argv[])
{
    int gray = (argc == 4 && strcmp(argv[3], "--nvg") == 0);
    int color = (argc == 4 && strcmp(argv[3], "--c24") == 0);
    // argv[2] must be exactly one character: check [0] before [1], so an
    // empty string never makes us read past its terminating NUL.
    if (argc > 4 || (argc == 4 && !gray && !color) || argv[2][0] == '\0'
        || argv[2][1] != '\0') {
        fprintf(stderr, "Usage: %s --mpfy <LETTER> [--nvg|--c24]\n", argv[0]);
        return EXIT_FAILURE;
    }

    char letter = argv[2][0];
    char filename[16];
    char comment[32];
    const char* suffix = gray ? "_nvg.mp" : (color ? "_c24.mp" : ".mp");
    snprintf(filename, sizeof(filename), "%c%s", letter, suffix);
    snprintf(comment, sizeof(comment), "La lettre %c", letter);

    int rc;
    if (gray) {
        unsigned char bitmap[FONT_W * FONT_H];
        if (font_nvg(letter, bitmap) != 0) {
            fprintf(stderr, "Unsupported letter: %c\n", letter);
            return EXIT_FAILURE;
        }
        rc = mpng_write(filename, FONT_W, FONT_H, 1, bitmap, sizeof(bitmap), comment);
    } else if (color) {
        unsigned char bitmap[FONT_W * FONT_H * 3];
        if (font_c24(letter, bitmap) != 0) {
            fprintf(stderr, "Unsupported letter: %c\n", letter);
            return EXIT_FAILURE;
        }
        rc = mpng_write(filename, FONT_W, FONT_H, 2, bitmap, sizeof(bitmap), comment);
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

// Read a whole regular file into a freshly allocated buffer.
// Returns 0 and hands the buffer to the caller (who must free it), or -1
// after printing an error; nothing is left allocated or open on failure.
static int load_file(const char* path, unsigned char** buf_out, size_t* len_out)
{
    int rc = -1;
    FILE* file = NULL;
    unsigned char* buf = NULL;
    struct stat st;
    size_t size;

    file = fopen(path, "rb");
    if (!file) {
        perror(path);
        goto cleanup;
    }

    // Ask the OS what we opened. fopen() succeeds on a directory, and ftell()
    // on one gives a huge bogus size, so insist on a regular file.
    if (fstat(fileno(file), &st) != 0) {
        perror("fstat");
        goto cleanup;
    }
    if (!S_ISREG(st.st_mode)) {
        fprintf(stderr, "%s: not a regular file\n", path);
        goto cleanup;
    }
    if (st.st_size > MAX_FILE_SIZE) {
        fprintf(stderr, "%s: file too large (max %lld bytes)\n", path,
                (long long) MAX_FILE_SIZE);
        goto cleanup;
    }

    size = (size_t) st.st_size;

    // malloc(0) may return NULL, which would look like an allocation failure.
    // An empty buffer is fine: the parser rejects it as "not a Mini-PNG".
    buf = malloc(size > 0 ? size : 1);
    if (!buf) {
        perror("malloc");
        goto cleanup;
    }

    if (fread(buf, 1, size, file) != size) {
        perror("fread");
        goto cleanup;
    }

    *buf_out = buf;
    *len_out = size;
    buf = NULL; // ownership passed to the caller
    rc = 0;

cleanup:
    free(buf);
    if (file) {
        fclose(file);
    }
    return rc;
}

static void report_parse_error(enum mpng_status st, const struct mpng* img)
{
    switch (st) {
    case MPNG_OK:
        break;
    case MPNG_ERR_MAGIC:
        fprintf(stderr, "Not a Mini-PNG file\n");
        break;
    case MPNG_ERR_TRUNCATED:
        fprintf(stderr, "Truncated block '%c'\n", img->diag.block_type);
        break;
    case MPNG_ERR_HEADER_SHORT:
        fprintf(stderr, "H block too short\n");
        break;
    case MPNG_ERR_COMMENT_CHARS:
        fprintf(stderr, "C block contains a non-printable byte\n");
        break;
    case MPNG_ERR_TOO_LARGE:
        fprintf(stderr, "Image too large\n");
        break;
    case MPNG_ERR_PIXEL_COUNT:
        fprintf(stderr, "Wrong amount of pixel data: expected %llu bytes, got %zu\n",
                (unsigned long long) img->diag.expected, img->diag.got);
        break;
    case MPNG_ERR_NOMEM:
        fprintf(stderr, "Out of memory\n");
        break;
    }
}

static void print_image(const struct mpng* img)
{
    if (img->has_header) {
        printf("Largeur = %u\n", img->largeur);
        printf("Hauteur = %u\n", img->hauteur);
        printf("Catégorie de pixel = %u\n", img->categorie);
    }

    for (size_t i = 0; i < img->n_comments; i++) {
        printf("Commentaires = %.*s\n", (int) img->comments[i].len,
               (const char*) img->comments[i].text);
    }

    switch (img->categorie) {
    case MPNG_CAT_NEB:
        display_neb(img->largeur, img->hauteur, img->pixels, img->pixels_len);
        break;
    case MPNG_CAT_NVG:
        display_nvg(img->largeur, img->hauteur, img->pixels, img->pixels_len);
        break;
    case MPNG_CAT_C24:
        display_c24(img->largeur, img->hauteur, img->pixels, img->pixels_len);
        break;
    default:
        break;
    }
}

int main(int argc, char* argv[])
{
    if (argc >= 3 && strcmp(argv[1], "--mpfy") == 0) {
        return run_mpfy(argc, argv);
    }

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file.mp>\n", argv[0]);
        fprintf(stderr, "       %s --mpfy <LETTER> [--nvg|--c24]\n", argv[0]);
        return EXIT_FAILURE;
    }

    int rc = EXIT_FAILURE;
    unsigned char* buf = NULL;
    size_t len = 0;
    struct mpng img = {0}; // zeroed so mpng_free() is safe on every path
    enum mpng_status st;

    if (load_file(argv[1], &buf, &len) != 0) {
        goto cleanup;
    }

    st = mpng_parse(buf, len, &img);
    if (st != MPNG_OK) {
        report_parse_error(st, &img);
        goto cleanup;
    }

    print_image(&img);
    rc = EXIT_SUCCESS;

cleanup:
    mpng_free(&img);
    free(buf);
    return rc;
}