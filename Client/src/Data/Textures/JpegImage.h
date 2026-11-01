// Decodificar un JPEG como lo hace el original, que enlaza estática la IJG libjpeg 6b
// (Client/docs/research/05-formatos-de-datos.md, "Texturas"). Lo usan los dos cargadores de OZJ:
// el de las texturas (0x63b870, OzjTexture.h) y el de la luz del terreno (0x63b490,
// TerrainLight.h).
//
// Los dos llaman a libjpeg igual: jpeg_CreateDecompress con la versión 62, jpeg_read_header,
// jpeg_start_decompress y jpeg_read_scanlines de a una fila, sin tocar ningún parámetro. Acá se
// hace lo mismo con libjpeg-turbo, que con esos parámetros da los mismos bytes que la 6b en los
// 2.380 OZJ que son JPEG en el laboratorio (pregunta 1 del roadmap del cliente). Los vectores de
// Client/tests/vectors/jpeg lo prueban en CI.
//
// La única diferencia es de dónde lee: el original, del archivo con jpeg_stdio_src; acá, de los
// bytes con jpeg_mem_src, que se comporta igual cuando se acaban los datos.

#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Client::Data::Textures
{

struct JpegImage
{
    // El tamaño de la imagen y los bytes por píxel, como los da libjpeg (output_width,
    // output_height y output_components): 3 en un JPEG en color, que sale en RGB, y 1 en uno en
    // grises.
    int width = 0;
    int height = 0;
    int components = 0;
    // height filas de width × components bytes, de arriba hacia abajo, como las da
    // jpeg_read_scanlines.
    std::vector<std::uint8_t> pixels;
};

// Decodifica `jpeg` con los parámetros por defecto de libjpeg, como el original.
//   - Si libjpeg corta con un error (no es un JPEG, le faltan tablas, un marcador que no sabe
//     leer), devuelve DataError::InvalidData. El original devuelve false.
//   - Si el ancho o el alto pasan de `maxSide`, devuelve DataError::InvalidData sin decodificar las
//     filas.
//   - Un aviso de libjpeg no corta: si los datos se acaban antes de tiempo, libjpeg completa la
//     imagen y avisa, y el original la usa igual. Los mensajes no se muestran: en el original van
//     al stderr de un programa de Windows, que no se ve.
Core::Expected<JpegImage, DataError> DecodeJpeg(std::span<const std::byte> jpeg, int maxSide);

} // namespace Client::Data::Textures
