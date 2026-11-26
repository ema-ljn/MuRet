#pragma once

#include "Network/LayoutReader.h"

#include <cstddef>
#include <limits>
#include <span>
#include <type_traits>
#include <vector>

namespace Client::Network
{
// `count` records of `Size` bytes from `first`, the first `Read` bytes of each passed to `readOne`: the original reads
// the last record only up to its `Read`th byte. If they do not all fit, none is read and the reader records TooShort.
template <std::size_t Size, std::size_t Read = Size, typename ReadOne>
[[nodiscard]] auto ReadList(LayoutReader& reader, std::size_t first, std::size_t count, ReadOne readOne)
    -> std::vector<std::invoke_result_t<ReadOne&, std::span<const std::byte, Read>>>
{
    static_assert(Read > 0 && Read <= Size);
    constexpr std::size_t MaxLength = std::numeric_limits<std::size_t>::max();
    constexpr std::size_t MaxCount = (MaxLength - Read) / Size + 1;
    std::size_t length = 0;
    if (count > MaxCount)
    {
        length = MaxLength;
    }
    else if (count > 0)
    {
        length = (count - 1) * Size + Read;
    }
    const std::span<const std::byte> records = reader.Bytes(first, length);
    std::vector<std::invoke_result_t<ReadOne&, std::span<const std::byte, Read>>> list;
    if (records.size() != length)
    {
        return list;
    }
    list.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        list.push_back(readOne(records.subspan(index * Size).template first<Read>()));
    }
    return list;
}
} // namespace Client::Network
