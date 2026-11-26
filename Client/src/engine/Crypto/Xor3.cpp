#include "Crypto/Xor3.h"

#include <array>
#include <cstddef>
#include <span>

namespace
{
// 05 § Tablas .bmd: the three bytes at 0x6c8c74, the same as the login's at 0x6c82e4.
constexpr std::array<std::byte, 3> Xor3Key = {std::byte{0xFC}, std::byte{0xCF}, std::byte{0xAB}};
} // namespace

namespace Client::Crypto
{
void Xor3(std::span<std::byte> bytes)
{
    for (std::size_t i = 0; i < bytes.size(); ++i)
    {
        bytes[i] ^= Xor3Key[i % Xor3Key.size()];
    }
}
} // namespace Client::Crypto
