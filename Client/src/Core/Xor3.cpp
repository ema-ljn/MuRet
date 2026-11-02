#include "Core/Xor3.h"

#include <array>

namespace
{

// 05 § Tablas .bmd: los tres bytes de 0x6c8c74, los mismos que los de 0x6c82e4 del login.
constexpr std::array<std::byte, 3> Xor3Key = {std::byte{0xFC}, std::byte{0xCF}, std::byte{0xAB}};

} // namespace

namespace Client::Core
{

void Xor3(std::span<std::byte> bytes)
{
    for (std::size_t i = 0; i < bytes.size(); ++i)
    {
        bytes[i] ^= Xor3Key[i % Xor3Key.size()];
    }
}

} // namespace Client::Core
