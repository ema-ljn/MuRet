#include "Crypto/K16.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace Client::Crypto
{
// Arithmetic in int, as everything in C++, and each result back to 8 bits with an explicit cast: that is what wraps
// (0x10 - 0x20 gives 0xF0, 0xF0 + 0x3D gives 0x2D).
void DecryptK16(std::span<std::byte> bytes)
{
    std::uint8_t previous = K16Seed;
    for (std::size_t i = 0; i < bytes.size(); ++i)
    {
        const auto encrypted = std::to_integer<std::uint8_t>(bytes[i]);
        const auto key = std::to_integer<std::uint8_t>(K16Key[i % K16Key.size()]);
        bytes[i] = std::byte{static_cast<std::uint8_t>((key ^ encrypted) - previous)};
        previous = static_cast<std::uint8_t>(encrypted + K16Step);
    }
}

void EncryptK16(std::span<std::byte> bytes)
{
    std::uint8_t previous = K16Seed;
    for (std::size_t i = 0; i < bytes.size(); ++i)
    {
        const auto plain = std::to_integer<std::uint8_t>(bytes[i]);
        const auto key = std::to_integer<std::uint8_t>(K16Key[i % K16Key.size()]);
        const auto encrypted = static_cast<std::uint8_t>(static_cast<std::uint8_t>(plain + previous) ^ key);
        bytes[i] = std::byte{encrypted};
        previous = static_cast<std::uint8_t>(encrypted + K16Step);
    }
}
} // namespace Client::Crypto
