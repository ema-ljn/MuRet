#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Textures/Texture.h"
#include "EngineApi.h"

#include <cstddef>
#include <cstdint>
#include <span>

// OZT, a 32-bit TGA texture (Client/docs/research/05-formatos-de-datos.md, "Texturas"; the loader at 0x63bd00). The
// original asks for "<name>.tga" and opens Data\<name>.OZT. Unencrypted: a TGA with 4 leading bytes, the same TGA's
// first 4, repeated by its exporter (0x63b280). Of the TGA header the original reads only width, height and bits per
// pixel; it ignores the image type, the id length and the descriptor, always reads uncompressed pixels from 0x16 and
// always flips the rows. The lab's 663 32-bit OZTs are uncompressed, id-less and bottom-origin, so that is right for
// all of them. Not stb_image: what is read of the TGA is a few lines, the rest (the power-of-2 buffer, the rejection)
// would be written anyway (CODING_RULES §10), and stb_image loads Monster\wolfa02.OZT (24 bits), which the original
// rejects.
namespace Client::Data::Textures
{
// The 4 the exporter adds (0x63b280).
inline constexpr std::size_t OztPrefixSize = 4;
// TGA header fields at bytes 12 (width), 14 (height) and 16 (bits per pixel), shifted by the prefix.
inline constexpr std::size_t OztWidthOffset = OztPrefixSize + 12;
static_assert(OztWidthOffset == 0x10, "el offset que mide el research (0063bd00-ozt-loader.md)");
inline constexpr std::size_t OztHeightOffset = OztPrefixSize + 14;
static_assert(OztHeightOffset == 0x12, "el offset que mide el research (0063bd00-ozt-loader.md)");
inline constexpr std::size_t OztBitsPerPixelOffset = OztPrefixSize + 16;
static_assert(OztBitsPerPixelOffset == 0x14, "el offset que mide el research (0063bd00-ozt-loader.md)");
inline constexpr std::size_t OztHeaderSize = OztBitsPerPixelOffset + 1;
// After the TGA's 18-byte header, ignoring the id length.
inline constexpr std::size_t OztPixelsOffset = OztPrefixSize + 18;
static_assert(OztPixelsOffset == 0x16, "el offset que mide el research (0063bd00-ozt-loader.md)");
// cmp al,0x20
inline constexpr std::uint8_t OztBitsPerPixel = 32;

// RGBA pixels with the rows flipped: file row r is texture row height - 1 - r, as in the original. A `file` short of
// OztHeaderSize is DataError::FileTooShort. Bits per pixel other than 32, or a width or height over TextureMaxSide,
// are DataError::InvalidData, like the original, which skips the texture silently (Monster\wolfa02.OZT, 24 bits). A
// zero or negative width or height passes, as in the original (signed compare): the texture has no image pixels and
// a side of 1 that way. Missing pixels are DataError::FileTooShort; the original reads past the end (Q-C24). What
// follows the pixels is ignored: in the lab, a TGA 2.0's 26-byte footer.
ENGINE_API Core::Expected<Texture, DataError> LoadOztTexture(std::span<const std::byte> file);
} // namespace Client::Data::Textures
