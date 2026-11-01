// La carpeta golden: una copia del Data\ del cliente original, fuera del repo.
//
// MURET_CLIENT_GOLDEN_DIR apunta a una carpeta con el contenido de Data\ (Enc1.dat, Dec2.dat,
// Player\, World1\…). La del laboratorio es analysis/decompilacion-maxima/assets/original-data
// de EmaNLJN/mu-online-reverse-lab. La regla, como en el repo de research:
//   - si la variable no está o está vacía, el test se saltea;
//   - si está pero la carpeta no existe, o le falta el archivo que el test pide, el test falla.

#pragma once

#include <gtest/gtest.h>

#include <filesystem>
#include <optional>
#include <string_view>

namespace Client::Tests
{

// El valor de MURET_CLIENT_GOLDEN_DIR, o std::nullopt si no está o está vacía.
std::optional<std::filesystem::path> GoldenDir();

// Base para los tests que leen archivos reales. SetUp() saltea el test sin carpeta golden y lo
// hace fallar si la carpeta no existe.
class GoldenDataTest : public ::testing::Test
{
protected:
    void SetUp() override;

    // La ruta del archivo, resuelta como la pide el original (sin distinguir mayúsculas). Si no
    // está, marca el test como fallado y devuelve std::nullopt.
    std::optional<std::filesystem::path> DataFile(std::string_view originalPath) const;

    std::filesystem::path dataRoot;
};

} // namespace Client::Tests
