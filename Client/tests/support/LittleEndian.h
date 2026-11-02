#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Para armar archivos sintéticos: los enteros como los escribe el x86 del original.
namespace Client::Tests
{
// Agrega los `size` bytes de abajo de `value` a `bytes`, el menos significativo primero.
inline void AppendLittleEndian(std::vector<std::byte>& bytes, std::uint32_t value, std::size_t size)
{
    constexpr unsigned BitsPerByte = 8;
    for (std::size_t byte = 0; byte < size; ++byte)
    {
        bytes.push_back(static_cast<std::byte>(value >> (BitsPerByte * byte)));
    }
}
} // namespace Client::Tests
