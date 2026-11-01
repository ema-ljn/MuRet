// Bytes de prueba que no son todos iguales: el byte i vale i * multiplier + offset, módulo 256.
// Es fill_pattern de mu-client/tests/assets/test_bmd_tables.c, de donde salen los vectores del
// checksum: (1, 0) da 00 01 02…, y (37, 11) es su "patrón".

#pragma once

#include <cstddef>
#include <vector>

namespace Client::Tests
{

inline std::vector<std::byte> BytePattern(std::size_t size, std::size_t multiplier, std::size_t offset)
{
    std::vector<std::byte> bytes(size);
    for (std::size_t i = 0; i < size; ++i)
    {
        bytes[i] = static_cast<std::byte>(i * multiplier + offset);
    }
    return bytes;
}

} // namespace Client::Tests
