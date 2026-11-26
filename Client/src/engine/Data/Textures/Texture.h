#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// As the original uploads it to OpenGL, from an OZT or an OZJ (Client/docs/research/05-formatos-de-datos.md,
// "Texturas"). The original copies the image unscaled into a buffer with power-of-2 sides: the image sits at row 0,
// column 0, and the rest of the buffer is untouched. That buffer is what glTexImage2D uploads and what it keeps in
// its texture table (0x07D3A198, +0x34).
namespace Client::Data::Textures
{
// Both loaders reject a larger image, and a texture side never exceeds it (0x63bd00 and 0x63b870).
inline constexpr int TextureMaxSide = 512;

// The power of 2 that covers `side`, from 1 and capped at TextureMaxSide (the loops at 0x63bb34 and 0x63bf81). Zero
// or a negative side, which the OZT lets through, gives 1.
constexpr int TextureSide(int side)
{
    int textureSide = 1;
    while (textureSide < side && textureSide < TextureMaxSide)
    {
        textureSide *= 2;
    }
    return textureSide;
}

// The format the original passes to glTexImage2D.
enum class PixelFormat : std::uint8_t
{
    Rgb,  // an OZJ's: red, green and blue, a byte each (GL_RGB, 0x63b870)
    Rgba, // an OZT's: red, green, blue and alpha (GL_RGBA, 0x63bd00)
};

// What the original stores at +0x28 of its table.
constexpr std::size_t BytesPerPixel(PixelFormat format)
{
    return format == PixelFormat::Rgb ? 3 : 4;
}

struct Texture
{
    // As the original reads it. In an OZT it may be zero or negative: the original lets it through and copies no pixel.
    int width = 0;
    int height = 0;
    // TextureSide of the image's size.
    int textureWidth = 1;
    int textureHeight = 1;
    PixelFormat format = PixelFormat::Rgba;
    // Row by row, in upload order. What is not image is zero; in the original it is whatever was on the heap (Q-C30).
    std::vector<std::uint8_t> pixels;
};
} // namespace Client::Data::Textures
