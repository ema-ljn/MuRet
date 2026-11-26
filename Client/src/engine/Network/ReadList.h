#pragma once

#include "Network/LayoutReader.h"

#include <cstddef>
#include <limits>
#include <span>
#include <type_traits>
#include <vector>

namespace Client::Network
{
// `count` records of `Size` bytes from `first`, each passed to `readOne`. If they do not all fit, none is read and
// the reader records TooShort.
template <std::size_t Size, typename ReadOne>
[[nodiscard]] auto ReadList(LayoutReader& reader, std::size_t first, std::size_t count, ReadOne readOne)
    -> std::vector<std::invoke_result_t<ReadOne&, std::span<const std::byte, Size>>>
{
    static_assert(Size > 0);
    constexpr std::size_t MaxCount = std::numeric_limits<std::size_t>::max() / Size;
    const std::size_t length = count > MaxCount ? std::numeric_limits<std::size_t>::max() : count * Size;
    const std::span<const std::byte> records = reader.Bytes(first, length);
    std::vector<std::invoke_result_t<ReadOne&, std::span<const std::byte, Size>>> list;
    if (records.size() != length)
    {
        return list;
    }
    list.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        list.push_back(readOne(records.subspan(index * Size).template first<Size>()));
    }
    return list;
}
} // namespace Client::Network
