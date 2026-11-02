#include "Data/Terrain/K16.h"

namespace Client::Data::Terrain
{
// Las cuentas se hacen en int, como todo en C++, y cada resultado vuelve a 8 bits con un cast
// explícito: eso es lo que hace dar la vuelta (0x10 - 0x20 da 0xF0, 0xF0 + 0x3D da 0x2D).
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
} // namespace Client::Data::Terrain
