/*
 * Una captura de ancho × alto con las llamadas de 0x63b150, la que escribe el JPEG de Impr Pant en
 * el main.exe: jpeg_std_error, jpeg_CreateCompress con la versión 62, fopen("wb"),
 * jpeg_stdio_dest, RGB de 3 componentes, jpeg_set_defaults, jpeg_set_quality(calidad, TRUE),
 * jpeg_start_compress(TRUE) y jpeg_write_scanlines de a una fila, la fila (alto − 1 − n) del
 * buffer para la fila n del JPEG: el buffer viene de glReadPixels, desde la fila de abajo.
 * Después jpeg_finish_compress, fclose y jpeg_destroy_compress.
 *
 * El píxel (x, y), contado desde arriba, es el de la fórmula `detailed` de make_vectors.py: rojo
 * (47x + 13y) & 255, verde (x² + 3y²) & 255 y azul (7xy + 91) & 255.
 *
 * Uso: shot6b ancho alto calidad salida.jpg
 */
#include <stdio.h>
#include <stdlib.h>

#include "jpeglib.h"

int main(int argc, char** argv)
{
    struct jpeg_compress_struct info;
    struct jpeg_error_mgr errors;
    unsigned char* pixels;
    JSAMPROW row;
    FILE* out;
    int width, height, quality, x, y;
    if (argc != 5)
    {
        fprintf(stderr, "uso: shot6b ancho alto calidad salida.jpg\n");
        return 2;
    }
    width = atoi(argv[1]);
    height = atoi(argv[2]);
    quality = atoi(argv[3]);
    if (width <= 0 || height <= 0)
    {
        fprintf(stderr, "el tamaño tiene que ser mayor que 0\n");
        return 2;
    }
    /* El buffer como lo deja glReadPixels: la fila de abajo de la imagen primero. */
    pixels = malloc((size_t)width * (size_t)height * 3);
    if (pixels == NULL)
    {
        fprintf(stderr, "no hay memoria\n");
        return 1;
    }
    for (y = 0; y < height; ++y)
    {
        unsigned char* line = pixels + (size_t)(height - 1 - y) * (size_t)width * 3;
        for (x = 0; x < width; ++x)
        {
            line[3 * x] = (unsigned char)((47 * x + 13 * y) & 255);
            line[3 * x + 1] = (unsigned char)((x * x + 3 * y * y) & 255);
            line[3 * x + 2] = (unsigned char)((7 * x * y + 91) & 255);
        }
    }
    info.err = jpeg_std_error(&errors);
    jpeg_CreateCompress(&info, 62, sizeof(info));
    out = fopen(argv[4], "wb");
    if (out == NULL)
    {
        fprintf(stderr, "no se pudo escribir %s\n", argv[4]);
        return 1;
    }
    jpeg_stdio_dest(&info, out);
    info.image_width = (JDIMENSION)width;
    info.image_height = (JDIMENSION)height;
    info.input_components = 3;
    info.in_color_space = JCS_RGB;
    jpeg_set_defaults(&info);
    jpeg_set_quality(&info, quality, TRUE);
    jpeg_start_compress(&info, TRUE);
    while (info.next_scanline < info.image_height)
    {
        row = pixels + (size_t)(height - (int)info.next_scanline - 1) * (size_t)width * 3;
        jpeg_write_scanlines(&info, &row, 1);
    }
    jpeg_finish_compress(&info);
    fclose(out);
    jpeg_destroy_compress(&info);
    free(pixels);
    return 0;
}
