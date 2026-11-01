#include "Data/Tables/Xor3.h"

#include <array>

namespace
{

// 05 § Tablas .bmd: los tres bytes de 0x6c8c74.
constexpr std::array<std::byte, 3> Xor3Key = {std::byte{0xFC}, std::byte{0xCF}, std::byte{0xAB}};

} // namespace

namespace Client::Data::Tables
{

void Xor3(std::span<std::byte> bytes)
{
    for (std::size_t i = 0; i < bytes.size(); ++i)
    {
        bytes[i] ^= Xor3Key[i % Xor3Key.size()];
    }
}

} // namespace Client::Data::Tables
