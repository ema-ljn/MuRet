#pragma once

#include "support/VectorFiles.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

// Los vectores JPEG de CI (Client/tests/vectors/jpeg/README.md): JPEG sintéticos chicos y lo que
// da decodificarlos con la IJG libjpeg 6b, la del original. Están en el repo, así que corren sin
// la carpeta golden.
namespace Client::Tests
{
// La carpeta de los vectores. CMake la pasa en MURET_CLIENT_JPEG_VECTORS_DIR.
inline std::filesystem::path JpegVectorsDir()
{
    return MURET_CLIENT_JPEG_VECTORS_DIR;
}

// Los bytes del vector `name` (por ejemplo "gray.jpg"). Si no está, el test falla y devuelve nada.
inline std::vector<std::byte> ReadJpegVector(std::string_view name)
{
    return ReadVectorFile(JpegVectorsDir(), name);
}

// Los OZJ de la carpeta golden del laboratorio, contando los 7 TGA (05-formatos-de-datos.md, la
// tabla de formatos): las líneas de lab-6b.txt.
inline constexpr std::size_t OzjFilesOfTheLab = 2387;

// Los bytes que tiene un OZJ antes del JPEG: los 24 primeros del mismo JPEG, que repite el
// exportador del original (0x63b280).
inline constexpr std::size_t OzjRepeatedBytes = 24;

// Un OZJ con `jpeg` adentro, como lo arma el exportador: los OzjRepeatedBytes primeros bytes del
// JPEG y después el JPEG entero.
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
