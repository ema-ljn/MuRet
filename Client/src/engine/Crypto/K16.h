#pragma once

#include "EngineApi.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

// K16, the cipher of the terrain files EncTerrain<N>.map, .att and .obj (Client/docs/research/05-formatos-de-datos.md,
// "Terreno de un mundo"; inlined in each original loader: 0x5f87b0, 0x5f8390 and 0x607370). Stateful, unlike XOR3:
//   plain[i] = (K16Key[i % 16] ^ cipher[i]) - previous
//   previous = cipher[i] + K16Step          (for the first byte, K16Seed)
// Unsigned 8-bit arithmetic that wraps. One stream over the whole file: `previous` does not restart between the
// header and the layers, and the key byte follows the file position, not the layer's.
namespace Client::Crypto
{
// The 16 bytes each original loader builds on the stack, one by one.
inline constexpr std::array<std::byte, 16> K16Key = {
    std::byte{0xD1}, std::byte{0x73}, std::byte{0x52}, std::byte{0xF6}, std::byte{0xD2}, std::byte{0x9A},
    std::byte{0xCB}, std::byte{0x27}, std::byte{0x3E}, std::byte{0xAF}, std::byte{0x59}, std::byte{0x31},
    std::byte{0x37}, std::byte{0xB3}, std::byte{0xE7}, std::byte{0xA2},
};
// The first byte's `previous` (0x5f84a9 in the .att loader, 0x5f886e in the .map's).
inline constexpr std::uint8_t K16Seed = 0x5E;
// Added to an encrypted byte to make the next one's `previous` (0x5f84d3 and 0x5f88a0).
inline constexpr std::uint8_t K16Step = 0x3D;

// In place. `bytes` must be the file from its first byte.
ENGINE_API void DecryptK16(std::span<std::byte> bytes);
} // namespace Client::Crypto
