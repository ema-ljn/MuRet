// Los vectores de red de CI (Client/tests/vectors/net/README.md): paquetes sintéticos, demasiado
// largos para escribirlos en el test. Están en el repo, así que corren sin el cable del
// laboratorio.

#pragma once

#include "support/FileBytes.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace Client::Tests
{

// La carpeta de los vectores. CMake la pasa en MURET_CLIENT_NET_VECTORS_DIR.
inline std::filesystem::path NetVectorsDir()
{
    return MURET_CLIENT_NET_VECTORS_DIR;
}

// Los bytes del vector `name` (por ejemplo "c2-long.bin"). Si no está, el test falla y devuelve
// nada.
inline std::vector<std::byte> ReadNetVector(std::string_view name)
{
    const std::optional<std::vector<char>> file = ReadFileBytes(NetVectorsDir() / name);
    if (!file.has_value())
    {
        ADD_FAILURE() << "falta el vector " << name << " en " << NetVectorsDir();
        return {};
    }
    const std::span<const std::byte> bytes = std::as_bytes(std::span(*file));
    return {bytes.begin(), bytes.end()};
}

} // namespace Client::Tests
