#pragma once

#include <cstddef>
#include <initializer_list>
#include <span>
#include <string_view>
#include <vector>

// Bytes written by hand, as in the vector tables: Bytes({0xC1, 0x04, 0x20, 0xAA}). Each value is taken modulo 256, as
// static_cast to std::byte does. And a text's bytes, to compare with a field: BytesOf("10209").
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

// Without copying: the text must stay alive, like a literal.
inline std::span<const std::byte> BytesOf(std::string_view text)
{
    return std::as_bytes(std::span(text));
}
} // namespace Client::Tests
