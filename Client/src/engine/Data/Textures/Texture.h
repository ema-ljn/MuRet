#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Una textura como la sube el original a OpenGL, desde un OZT o un OZJ (Client/docs/research/
// 05-formatos-de-datos.md, "Texturas").
//
// El original copia la imagen a un buffer cuyos lados son potencias de 2, sin escalarla: la imagen
// queda en la esquina de la fila 0 y la columna 0, y el resto del buffer no se toca. Ese buffer es
// el que sube con glTexImage2D y el que guarda en su tabla de texturas (0x07D3A198, +0x34).
namespace Client::Data::Textures
{
// El lado más grande de una textura: los dos cargadores rechazan una imagen más grande, y el lado
// de la textura no pasa de acá (0x63bd00 y 0x63b870).
inline constexpr int TextureMaxSide = 512;

// El lado de la textura para una imagen de `side` píxeles: la potencia de 2 que lo cubre, desde 1 y
// con tope en TextureMaxSide (los bucles de 0x63bb34 y 0x63bf81). Con cero o un lado negativo,
// que el OZT deja pasar, da 1.
constexpr int TextureSide(int side)
{
    int textureSide = 1;
    while (textureSide < side && textureSide < TextureMaxSide)
    {
        textureSide *= 2;
    }
    return textureSide;
}

// Cómo vienen los píxeles: el formato con el que el original llama a glTexImage2D.
enum class PixelFormat : std::uint8_t
{
    Rgb,  // la de un OZJ: rojo, verde y azul, un byte cada uno (GL_RGB, 0x63b870)
    Rgba, // la de un OZT: rojo, verde, azul y alfa (GL_RGBA, 0x63bd00)
};

// Los bytes de un píxel en ese formato: lo que el original guarda en +0x28 de su tabla.
constexpr std::size_t BytesPerPixel(PixelFormat format)
{
    return format == PixelFormat::Rgb ? 3 : 4;
}

struct Texture
{
    // El tamaño de la imagen, como lo lee el original. En un OZT puede ser cero o negativo: el
    // original lo deja pasar y no copia ningún píxel.
    int width = 0;
    int height = 0;
    // El tamaño del buffer: TextureSide del de la imagen.
    int textureWidth = 1;
    int textureHeight = 1;
    PixelFormat format = PixelFormat::Rgba;
    // textureWidth × textureHeight píxeles en `format`, fila por fila, en el orden en que se suben
    // a OpenGL. Lo que no es imagen queda en cero; en el original queda lo que había en el heap
    // (Q-C30).
    std::vector<std::uint8_t> pixels;
};
} // namespace Client::Data::Textures
