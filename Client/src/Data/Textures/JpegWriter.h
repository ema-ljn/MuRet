#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

// Escribir la captura de pantalla en JPEG como 0x63b150 (CL8 hito 6; Client/docs/research/06-render-escenas-y-ui.md,
// "La captura de pantalla"), con las llamadas de la IJG libjpeg 6b que enlaza el original: jpeg_CreateCompress con la
// versión 62, RGB de 3 componentes, jpeg_set_defaults, jpeg_set_quality con la calidad y forzando el baseline,
// jpeg_start_compress(TRUE) y jpeg_write_scanlines de a una fila. Con esos parámetros, libjpeg-turbo 3.2.0 da los
// mismos bytes que la 6b, con su SIMD y sin él: lo prueban en CI los vectores screenshot-*.6b.jpg, que escribió la 6b
// con las mismas llamadas (Client/tests/vectors/jpeg/README.md).
//
// La diferencia es adónde escribe: el original, con jpeg_stdio_dest sobre un FILE de fopen("wb"); acá, un destino
// propio sobre un std::ofstream, que escribe de a 4.096 bytes como el de stdio y deja los mismos bytes en el archivo.
namespace Client::Data::Textures
{
enum class JpegWrite
{
    // El archivo, entero.
    Written,
    // 0x63b188..0x63b19f: fopen(nombre, "wb") dio NULL. El original devuelve 0 sin escribir nada, y quien lo llamó
    // (0x6202e0) no lo mira.
    NotOpened,
};

// 0x63b150(nombre, ancho, alto, píxeles, calidad): `bottomUpRgb` son `height` filas de `width` × 3 bytes (rojo, verde
// y azul), desde la de abajo, como las deja glReadPixels; la fila n del JPEG es la (height − 1 − n) del buffer. El
// archivo se abre antes de comprimir, como el fopen del original: si no abre, JpegWrite::NotOpened. Un ancho o un
// alto de 0 o menos, o un buffer de otro tamaño, son el error, sin abrir nada. Si libjpeg corta (no puede escribir,
// se queda sin memoria), también el error: el error_exit de jpeg_std_error del original sale del juego con exit(1),
// y el archivo queda como lo dejó.
[[nodiscard]] Core::Expected<JpegWrite, std::string> WriteJpegFile(const std::filesystem::path& file, int width,
                                                                   int height,
                                                                   std::span<const std::uint8_t> bottomUpRgb,
                                                                   int quality);
} // namespace Client::Data::Textures
