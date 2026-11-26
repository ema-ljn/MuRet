#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Textures/Texture.h"
#include "EngineApi.h"

#include <cstddef>
#include <span>

// OZJ, a JPEG texture (Client/docs/research/05-formatos-de-datos.md, "Texturas"; the loader at 0x63b870). The original
// asks for "<name>.jpg" and opens Data\<name>.OZJ. Unencrypted: a JPEG with 24 leading bytes, the same JPEG's first 24,
// repeated by its exporter (0x63b280); the original skips them with fseek and passes the rest to libjpeg unread. The
// image sits in the corner of a power-of-2 texture, like the OZT's, but its rows are not flipped: texture row 0 is the
// JPEG's first. Seven lab OZJs are really TGA (Q-C14), and libjpeg rejects them: "Not a JPEG file".
namespace Client::Data::Textures
{
// The original skips them with fseek(0x18) before passing the file to libjpeg: the 24 the exporter adds (0x63b280).
inline constexpr std::size_t OzjPrefixSize = 24;
static_assert(OzjPrefixSize == 0x18, "el fseek que mide el research (0063b870-ozj-loader.md)");

// RGB pixels, rows in JPEG order. A `file` no longer than OzjPrefixSize has no JPEG: DataError::FileTooShort. If
// libjpeg stops before the last row, or the width or height exceeds TextureMaxSide, DataError::InvalidData; the
// original returns false and uploads nothing, as with the 7 TGAs of Q-C14 and the lab's only OZJ over 512,
// Skill\null.OZJ (800 × 600). In a single-scan JPEG what follows the last row is not read: when the original reads it,
// the texture is uploaded (Q-C32). Each JPEG row is copied whole to the start of its texture row, as in the original:
// a 1-byte grayscale JPEG fills a third of the image's width per row, and a 4-byte CMYK one fits if its width is at
// most 3/4 of the texture's (Q-C31). A row that does not fit its texture row is DataError::InvalidData.
ENGINE_API Core::Expected<Texture, DataError> LoadOzjTexture(std::span<const std::byte> file);
} // namespace Client::Data::Textures
