// Cargar un archivo de clave real en un test golden: Enc1.dat y Dec2.dat de la carpeta golden, y
// Dec1.dat y Enc2.dat de la del cable (GoldenDir.h). Ningún mensaje muestra un valor de la clave.

#pragma once

#include "Core/Expected.h"
#include "Network/Crypto/CryptoError.h"

#include "support/FileBytes.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace Client::Tests
{

// La clave de `path`, cargada con `load` (LoadEncryptionKey o LoadDecryptionKey). Si la ruta no
// está, el test ya falló; si el archivo no se lee o no carga, falla acá. En los tres casos,
// std::nullopt.
template <typename Key>
std::optional<Key> LoadKeyFile(const std::optional<std::filesystem::path>& path,
                               Core::Expected<Key, Network::Crypto::CryptoError> (*load)(std::span<const std::byte>))
{
    if (!path)
    {
        return std::nullopt;
    }
    const std::optional<std::vector<char>> file = ReadFileBytes(*path);
    if (!file)
    {
        ADD_FAILURE() << "no se pudo leer " << *path;
        return std::nullopt;
    }
    auto loaded = load(std::as_bytes(std::span(*file)));
    if (!loaded.HasValue())
    {
        ADD_FAILURE() << "no carga " << path->filename();
        return std::nullopt;
    }
    return std::move(loaded).Value();
}

} // namespace Client::Tests
