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
#include <string>
#include <string_view>

namespace Client::Tests
{

// El valor de MURET_CLIENT_GOLDEN_DIR, o std::nullopt si no está o está vacía.
std::optional<std::filesystem::path> GoldenDir();

// La regla, separada de la lectura del entorno para poder testearla (GoldenDirTest.cpp).
enum class GoldenDirState
{
    NotSet,  // sin variable, o vacía: el test se saltea
    Missing, // la variable apunta a algo que no es una carpeta: el test falla
    Ready,
};
std::optional<std::filesystem::path> GoldenDirFromValue(const std::optional<std::string>& value);
GoldenDirState CheckGoldenDir(const std::optional<std::filesystem::path>& dir);

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

// Los volcados de la memoria del main.exe original, con las tablas ya cargadas.
// MURET_CLIENT_MEMORY_DIR apunta a la carpeta de los s31_*.bin: en el repo de research,
// client-research/mu-golden/memoria-cliente/2026-10-09-devias, fuera de Git. Valen para la copia
// de Data\ del laboratorio, que tiene las mismas tablas (mismo SHA-256) que mu-golden.
//
// La regla es la de la carpeta golden. Un test de memoria necesita las dos: compara lo que MuRet
// carga de un archivo con lo que el original cargó del mismo archivo.
class MemoryDumpTest : public GoldenDataTest
{
protected:
    void SetUp() override;

    // La ruta del volcado `name`. Si no está, marca el test como fallado y devuelve std::nullopt.
    std::optional<std::filesystem::path> DumpFile(std::string_view name) const;

    std::filesystem::path dumpRoot;
};

} // namespace Client::Tests
