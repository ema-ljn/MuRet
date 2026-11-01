// Ayudas para los tests del terreno: cifrar un archivo sintético como lo hace el exportador del
// original, para que los cargadores lo descifren.

#pragma once

#include "Data/Terrain/K16.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Client::Tests
{

// La inversa de DecryptK16, como la que usa el exportador del original (0x5f8960):
// cifrado[i] = (plano[i] + previo) ^ K16Key[i % 16], con el mismo `previo` que el descifrado.
inline std::vector<std::byte> EncryptK16(std::vector<std::byte> bytes)
{
    using Data::Terrain::K16Key;
    std::uint8_t previous = Data::Terrain::K16Seed;
    for (std::size_t i = 0; i < bytes.size(); ++i)
    {
        const auto plain = std::to_integer<std::uint8_t>(bytes[i]);
        const auto key = std::to_integer<std::uint8_t>(K16Key[i % K16Key.size()]);
        const auto encrypted = static_cast<std::uint8_t>(static_cast<std::uint8_t>(plain + previous) ^ key);
        bytes[i] = std::byte{encrypted};
        previous = static_cast<std::uint8_t>(encrypted + Data::Terrain::K16Step);
    }
    return bytes;
}

} // namespace Client::Tests
