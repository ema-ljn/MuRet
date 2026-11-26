#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// For building synthetic files: integers as the original's x86 writes them.
namespace Client::Tests
{
// The low `size` bytes of `value`, least significant first.
inline void AppendLittleEndian(std::vector<std::byte>& bytes, std::uint32_t value, std::size_t size)
{
    constexpr unsigned BitsPerByte = 8;
    for (std::size_t byte = 0; byte < size; ++byte)
    {
        bytes.push_back(static_cast<std::byte>(value >> (BitsPerByte * byte)));
    }
}
} // namespace Client::Tests
