// La carpeta golden: una copia del Data\ del cliente original, fuera del repo.
//
// MURET_CLIENT_GOLDEN_DIR apunta a una carpeta con el contenido de Data\ (Enc1.dat, Dec2.dat,
// Player\, World1\…). La del laboratorio es analysis/decompilacion-maxima/assets/original-data
// de EmaNLJN/mu-online-reverse-lab. La regla, como en el repo de research:
//   - si la variable no está o está vacía, el test se saltea;
//   - si está pero la carpeta no existe, o le falta el archivo que el test pide, el test falla.

#pragma once

#include <gtest/gtest.h>

#include <cstdint>
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

// Una captura de la memoria del main.exe original: su carpeta dentro de memoria-cliente/ (el
// README de mu-golden dice qué tiene cada una) y el mundo que estaba cargado, como el N de la
// carpeta World<N> que reciben los cargadores. Valen para la copia de Data\ del laboratorio, que
// tiene los mismos archivos (mismo SHA-256) que los que estaban cargados.
struct MemoryCapture
{
    std::string_view folder;
    std::uint8_t world = 0;
};

// Con el personaje en Devias (World3, g_world = 2): las tablas, el terreno y la luz (s31_*.bin).
inline constexpr MemoryCapture DeviasCapture{"2026-10-09-devias", 3};
// En Lorencia (World1, g_world = 0), en la celda (136, 130): el terreno, la luz y las texturas
// (lor_*.bin y lor2_*.bin).
inline constexpr MemoryCapture LorenciaCapture{"2026-10-09-lorencia", 1};
// En Lorencia, en otra lectura y en la escena 5: los 2.870 objetos del mapa en sus 256 listas
// (obj_*.bin).
inline constexpr MemoryCapture LorenciaObjectsCapture{"2026-10-09-lorencia-obj", 1};

// Si el volcado `name` de la captura `folder` está en la carpeta de volcados `root`.
enum class DumpState
{
    Found,
    Missing,
    // Falta, pero está en `root` mismo: la variable apunta a una captura y no a la carpeta que las
    // contiene. Hasta la CL1.4 apuntaba a la de Devias.
    RootIsACapture,
};
DumpState CheckDump(const std::filesystem::path& root, std::string_view folder, std::string_view name);

// Los volcados de memoria. MURET_CLIENT_MEMORY_DIR apunta a la carpeta de las capturas: en el
// repo de research, client-research/mu-golden/memoria-cliente, fuera de Git.
//
// La regla es la de la carpeta golden. Un test de memoria necesita las dos: compara lo que MuRet
// carga de un archivo con lo que el original cargó del mismo archivo.
class MemoryDumpTest : public GoldenDataTest
{
protected:
    void SetUp() override;

    // La ruta del volcado `name` de `capture`. Si no está, marca el test como fallado y devuelve
    // std::nullopt.
    std::optional<std::filesystem::path> DumpFile(const MemoryCapture& capture, std::string_view name) const;

    std::filesystem::path dumpRoot;
};

} // namespace Client::Tests
