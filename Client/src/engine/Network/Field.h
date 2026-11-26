#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace Client::Network
{
template <typename T> struct IsByteArray : std::false_type
{
};

template <std::size_t Size> struct IsByteArray<std::array<std::byte, Size>> : std::true_type
{
};

template <typename T>
concept FieldType = std::is_same_v<T, std::uint8_t> || std::is_same_v<T, std::uint16_t> ||
                    std::is_same_v<T, std::uint32_t> || IsByteArray<T>::value;

// A field of a packet at a fixed offset from pkt[0], the way the research cites it (`+7`).
template <FieldType T, std::size_t At, std::endian ByteOrder = std::endian::little> struct Field
{
    using Type = T;
    static constexpr std::size_t Offset = At;
    static constexpr std::endian Order = ByteOrder;
    static constexpr std::size_t End = At + sizeof(T);
};
} // namespace Client::Network
