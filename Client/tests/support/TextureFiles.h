#pragma once

#include "Data/Textures/OztTexture.h"
#include "support/LittleEndian.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// Helpers for the texture tests: build a synthetic OZT as the original's exporter leaves it, and the pixels expected
// from it.
namespace Client::Tests
{
// Uncompressed, without id or palette.
inline constexpr std::size_t TgaHeaderSize = 18;
// The image type of an uncompressed color TGA.
inline constexpr std::uint8_t TgaUncompressedColor = 2;
// The lab OZTs' descriptor: 8 alpha bits and the origin at the bottom left.
inline constexpr std::uint8_t TgaDescriptorOfTheLab = 0x08;

// The alpha of every FilePixel pixel: it comes from no other channel, so a crossing with one shows, and it is not 0xFF,
// so it shows if the loader makes it up.
inline constexpr std::uint8_t FileAlpha = 0x5A;

// The 4 bytes of a file BGRA pixel for the image's pixel (x, y), with y = 0 the file's first row. Up to 512 × 512,
// each pixel is different: blue and green are the low bytes of x and y, and red carries their high bits with bit 7
// set, so that no pixel is all zeros like the texture's spare area.
inline std::array<std::uint8_t, 4> FilePixel(int x, int y)
{
    const auto blue = static_cast<std::uint8_t>(x & 0xFF);
    const auto green = static_cast<std::uint8_t>(y & 0xFF);
    const auto red = static_cast<std::uint8_t>(0x80 | (x >> 8) | ((y >> 8) << 1));
    return {blue, green, red, FileAlpha};
}

// A `width` × `height` OZT with FilePixel and `bitsPerPixel` in the header: the TGA, with its first 4 bytes repeated in
// front, as the exporter builds it (0x63b280). The pixels are always 4 bytes, whatever the bits per pixel.
inline std::vector<std::byte> PlainOzt(std::int16_t width, std::int16_t height, std::uint8_t bitsPerPixel = 32)
{
    std::vector<std::byte> tga{std::byte{0}, std::byte{0}, std::byte{TgaUncompressedColor}};
    tga.resize(Data::Textures::OztWidthOffset - Data::Textures::OztPrefixSize); // the palette and the origin, at zero
    AppendLittleEndian(tga, static_cast<std::uint16_t>(width), sizeof(width));
    AppendLittleEndian(tga, static_cast<std::uint16_t>(height), sizeof(height));
    tga.push_back(std::byte{bitsPerPixel});
    tga.push_back(std::byte{TgaDescriptorOfTheLab});
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            for (const std::uint8_t channel : FilePixel(x, y))
            {
                tga.push_back(std::byte{channel});
            }
        }
    }
    std::vector<std::byte> ozt(tga.begin(), tga.begin() + Data::Textures::OztPrefixSize);
    ozt.insert(ozt.end(), tga.begin(), tga.end());
    return ozt;
}
} // namespace Client::Tests
