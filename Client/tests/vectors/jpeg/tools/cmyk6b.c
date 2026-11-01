/*
 * Un JPEG CMYK de ancho × alto con el compresor de la 6b: cuatro componentes y el marcador de
 * Adobe que escribe jpeg_set_colorspace, calidad 75. cjpeg no escribe CMYK.
 * El byte i de la fila y es (15 i + 40 y) & 255.
 *
 * Uso: cmyk6b ancho alto salida.jpg
 */
#include <stdio.h>
#include <stdlib.h>

#include "jpeglib.h"

int main(int argc, char** argv)
{
    struct jpeg_compress_struct info;
    struct jpeg_error_mgr errors;
    JSAMPROW row;
    FILE* out;
    int width, height, x, y;
    if (argc != 4)
    {
        fprintf(stderr, "uso: cmyk6b ancho alto salida.jpg\n");
        return 2;
    }
    width = atoi(argv[1]);
    height = atoi(argv[2]);
    out = fopen(argv[3], "wb");
    if (out == NULL || width <= 0 || height <= 0)
    {
        fprintf(stderr, "no se pudo escribir %s\n", argv[3]);
        return 1;
    }
    info.err = jpeg_std_error(&errors);
    jpeg_create_compress(&info);
    jpeg_stdio_dest(&info, out);
    info.image_width = (JDIMENSION)width;
    info.image_height = (JDIMENSION)height;
    info.input_components = 4;
    info.in_color_space = JCS_CMYK;
    jpeg_set_defaults(&info);
    jpeg_set_quality(&info, 75, TRUE);
    jpeg_start_compress(&info, TRUE);
    row = malloc((size_t)width * 4);
    for (y = 0; y < height; ++y)
    {
        for (x = 0; x < width * 4; ++x)
        {
            row[x] = (JSAMPLE)((15 * x + 40 * y) & 255);
        }
        jpeg_write_scanlines(&info, &row, 1);
    }
    jpeg_finish_compress(&info);
    jpeg_destroy_compress(&info);
    free(row);
    return fclose(out) == 0 ? 0 : 1;
}
