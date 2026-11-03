#pragma once

#include "GlName.h"
#include "LabGl.h"

#include "Core/Expected.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

// Subir una imagen a una textura de GL (CL5.0, actividad 10 de la guía): glTexImage2D lee las filas
// del buffer de a una, desde la primera, que queda en v = 0; y cada fila empieza en un múltiplo de
// GL_UNPACK_ALIGNMENT, que vale 4 por defecto. Una fila RGB de 2 píxeles mide 6 bytes: con el 4, GL
// salta a la siguiente en el byte 8 ("Lo que más puede morder", en el plan de la CL5).
namespace Labs
{
// Una imagen en memoria: `channels` es 3 (GL_RGB, como un OZJ) o 4 (GL_RGBA, como un OZT).
struct TextureImage
{
    int width = 0;
    int height = 0;
    int channels = 3;
    std::span<const std::uint8_t> pixels;
};

// Los bytes que lee glTexImage2D con ese GL_UNPACK_ALIGNMENT: cada fila menos la última ocupa su
// largo redondeado hacia arriba al múltiplo de `unpackAlignment`, y la última, su largo.
[[nodiscard]] std::size_t RequiredUnpackBytes(int width, int height, int channels, int unpackAlignment);

// Sube la imagen con GL_UNPACK_ALIGNMENT en `unpackAlignment` (y lo deja en 4 al terminar, para no
// cambiar la subida siguiente), filtro GL_NEAREST y wrap GL_CLAMP_TO_EDGE. Si `pixels` es más corto
// que lo que GL va a leer, no llama a GL y da el error: GL leería fuera del buffer.
[[nodiscard]] Client::Core::Expected<GlName, std::string> UploadTexture(const LabGl& gl, const TextureImage& image,
                                                                        int unpackAlignment);
} // namespace Labs
