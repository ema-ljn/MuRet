// Bytes escritos a mano en un test, como en las tablas de vectores: Bytes({0xC1, 0x04, 0x20, 0xAA}).
// Cada valor se toma módulo 256, como lo convierte static_cast a std::byte.

#pragma once

#include <cstddef>
#include <initializer_list>
#include <vector>

namespace Client::Tests
{

inline std::vector<std::byte> Bytes(std::initializer_list<int> values)
{
    std::vector<std::byte> bytes;
    bytes.reserve(values.size());
    for (const int value : values)
    {
        bytes.push_back(static_cast<std::byte>(value));
    }
    return bytes;
}

} // namespace Client::Tests
