#pragma once

#include "support/VectorFiles.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

// The CI JPEG vectors (Client/tests/vectors/jpeg/README.md): small synthetic JPEGs and what decoding them with IJG
// libjpeg 6b, the original's, gives. They are in the repo, so they run without the golden folder.
namespace Client::Tests
{
// CMake passes it in MU_CLIENT_JPEG_VECTORS_DIR.
inline std::filesystem::path JpegVectorsDir()
{
    return MU_CLIENT_JPEG_VECTORS_DIR;
}

// `name` is like "gray.jpg". If it is missing, the test fails and it returns nothing.
inline std::vector<std::byte> ReadJpegVector(std::string_view name)
{
    return ReadVectorFile(JpegVectorsDir(), name);
}

// The OZJ files of the lab's golden folder, counting the 7 TGAs (05-formatos-de-datos.md, the formats table): the
// lines of lab-6b.txt.
inline constexpr std::size_t OzjFilesOfTheLab = 2387;

// The bytes an OZJ has before the JPEG: the first 24 of the same JPEG, which the original's exporter repeats
// (0x63b280).
inline constexpr std::size_t OzjRepeatedBytes = 24;

// An OZJ with `jpeg` inside, as the exporter builds it: the JPEG's first OzjRepeatedBytes bytes and then the whole
// JPEG.
inline std::vector<std::byte> OzjOf(std::span<const std::byte> jpeg)
{
    if (jpeg.size() < OzjRepeatedBytes)
    {
        ADD_FAILURE() << "un JPEG de " << jpeg.size() << " bytes no alcanza para armar un OZJ";
        return {};
    }
    std::vector<std::byte> ozj(jpeg.begin(), jpeg.begin() + static_cast<std::ptrdiff_t>(OzjRepeatedBytes));
    ozj.insert(ozj.end(), jpeg.begin(), jpeg.end());
    return ozj;
}
} // namespace Client::Tests
