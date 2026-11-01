/*
 * Lo que da la IJG libjpeg 6b de un JPEG, decodificado como lo hace el original (0x63b870 y
 * 0x63b490): jpeg_stdio_src, los parámetros por defecto y jpeg_read_scanlines de a una fila, sin
 * jpeg_finish_decompress, porque el original ya usó la imagen cuando lo llama (Q-C32).
 *
 * Uso: hash6b [-skip N] [-raw salida] archivo...
 *   -skip N      saltea los N primeros bytes de cada archivo (24 en un OZJ, como el fseek de 0x18)
 *   -raw salida  escribe los píxeles del único archivo en `salida`, fila por fila, sin cabecera
 *
 * Por cada archivo imprime una línea: "archivo ancho alto componentes fnv" (el FNV-1a de 64 bits
 * de los píxeles, en hexadecimal), o "archivo ERR código" (el msg_code de jerror.h).
 */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jpeglib.h"

/* FNV-1a de 64 bits: la base y el primo del autor. */
#define FNV_OFFSET_BASIS 0xcbf29ce484222325ULL
#define FNV_PRIME 0x100000001b3ULL

struct errors {
    struct jpeg_error_mgr manager;
    jmp_buf jump;
};

static void jump_on_error(j_common_ptr info)
{
    longjmp(((struct errors *)info->err)->jump, 1);
}

static void discard_message(j_common_ptr info)
{
    (void)info;
}

static int decode(const char *path, long skip, const char *raw)
{
    struct jpeg_decompress_struct info;
    struct errors errors;
    unsigned char *volatile pixels = NULL;
    FILE *file = fopen(path, "rb");
    if (file == NULL || fseek(file, skip, SEEK_SET) != 0) {
        fprintf(stderr, "no se pudo abrir %s\n", path);
        return 1;
    }
    info.err = jpeg_std_error(&errors.manager);
    errors.manager.error_exit = jump_on_error;
    errors.manager.output_message = discard_message;
    if (setjmp(errors.jump)) {
        printf("%s ERR %d\n", path, errors.manager.msg_code);
        jpeg_destroy_decompress(&info);
        fclose(file);
        free(pixels);
        return 0;
    }
    jpeg_create_decompress(&info);
    jpeg_stdio_src(&info, file);
    jpeg_read_header(&info, TRUE);
    jpeg_start_decompress(&info);
    {
        size_t row_size = (size_t)info.output_width * (size_t)info.output_components;
        size_t size = row_size * info.output_height;
        unsigned long long hash = FNV_OFFSET_BASIS;
        size_t i;
        pixels = malloc(size);
        while (info.output_scanline < info.output_height) {
            JSAMPROW row = pixels + row_size * info.output_scanline;
            jpeg_read_scanlines(&info, &row, 1);
        }
        for (i = 0; i < size; ++i) {
            hash = (hash ^ pixels[i]) * FNV_PRIME;
        }
        printf("%s %u %u %d %016llx\n", path, info.output_width, info.output_height,
               info.output_components, hash);
        if (raw != NULL) {
            FILE *out = fopen(raw, "wb");
            if (out == NULL || fwrite(pixels, 1, size, out) != size || fclose(out) != 0) {
                fprintf(stderr, "no se pudo escribir %s\n", raw);
                return 1;
            }
        }
    }
    jpeg_destroy_decompress(&info);
    fclose(file);
    free(pixels);
    return 0;
}

int main(int argc, char **argv)
{
    long skip = 0;
    const char *raw = NULL;
    int first = 1;
    int failed = 0;
    while (first + 1 < argc && argv[first][0] == '-') {
        if (strcmp(argv[first], "-skip") == 0) {
            skip = strtol(argv[first + 1], NULL, 10);
        } else if (strcmp(argv[first], "-raw") == 0) {
            raw = argv[first + 1];
        } else {
            break;
        }
        first += 2;
    }
    if (first >= argc || (raw != NULL && argc - first != 1)) {
        fprintf(stderr, "uso: hash6b [-skip N] [-raw salida] archivo...\n");
        return 2;
    }
    for (; first < argc; ++first) {
        failed |= decode(argv[first], skip, raw);
    }
    return failed;
}
