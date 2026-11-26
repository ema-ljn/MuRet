#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "EngineApi.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// Decodes a JPEG as the original does, with its statically linked IJG libjpeg 6b
// (Client/docs/research/05-formatos-de-datos.md, "Texturas"), for both OZJ loaders: textures (0x63b870,
// OzjTexture.h) and the terrain light (0x63b490, TerrainLight.h). Both call jpeg_CreateDecompress with version 62,
// jpeg_read_header, jpeg_start_decompress and jpeg_read_scanlines one row at a time, touching no parameter. With
// those parameters libjpeg-turbo gives 6b's bytes on the lab's 2,380 JPEG OZJs (client roadmap question 1); CI checks
// the vectors in Client/tests/vectors/jpeg, and the golden folder the 6b hash of each of the 2,380. Outside the lab
// turbo differs from 6b in some ways (Q-C33): what can be brought to 6b's behavior is (what 6b cannot read is
// rejected, 4:4:0 is upsampled as in 6b); the rest is written in Q-C33. The original reads the file with
// jpeg_stdio_src; here jpeg_mem_src, which behaves the same when the data runs out.
namespace Client::Data::Textures
{
struct JpegImage
{
    // As libjpeg gives them (output_width, output_height and output_components): 3 for a color JPEG, which comes out
    // RGB, and 1 for grayscale.
    int width = 0;
    int height = 0;
    int components = 0;
    // height rows of width × components bytes, top to bottom, as jpeg_read_scanlines gives them.
    std::vector<std::uint8_t> pixels;
};

// With libjpeg's default parameters, as the original. DataError::InvalidData if libjpeg stops with an error before
// the last row (the original returns false); for a JPEG that 6b cannot read but turbo can (arithmetic, lossless, or
// sequential with a scan asking for a missing Huffman table, Q-C33), as the original; for a precision other than 8
// bits; and if the width or height exceeds `maxSide`, without decoding (with `maxSide` 0 or less nothing fits). In a
// single-scan JPEG what follows the last row is not read: the original has used the image when it reads it, so an
// error there changes nothing (Q-C32); in a multi-scan one both libjpegs read everything before the first row, and an
// error there is DataError::InvalidData, as in the original. A libjpeg warning does not stop: if the data ends early
// libjpeg completes the image and the original uses it, but turbo does not always complete it like 6b (Q-C33).
ENGINE_API Core::Expected<JpegImage, DataError> DecodeJpeg(std::span<const std::byte> jpeg, int maxSide);
} // namespace Client::Data::Textures
