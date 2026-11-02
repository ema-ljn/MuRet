#pragma once

#include "support/VectorFiles.h"

#include <cstddef>
#include <filesystem>
#include <string_view>
#include <vector>

// Los vectores de red de CI (Client/tests/vectors/net/README.md): paquetes sintéticos, demasiado
// largos para escribirlos en el test. Están en el repo, así que corren sin el cable del
// laboratorio.
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
    return ReadVectorFile(NetVectorsDir(), name);
}
} // namespace Client::Tests
