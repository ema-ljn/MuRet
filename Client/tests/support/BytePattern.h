#pragma once

#include <cstddef>
#include <vector>

// Byte i is i * multiplier + offset, modulo 256: mu-client/tests/assets/test_bmd_tables.c's fill_pattern, where the
// checksum vectors come from. (1, 0) gives 00 01 02…, and (37, 11) is its "pattern".
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
