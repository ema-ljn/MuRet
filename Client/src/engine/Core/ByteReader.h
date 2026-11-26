#pragma once

#include "EngineApi.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <span>

// Data\ files store integers little-endian, as the original's x86 left them; a C2/C4 size is big-endian
// (Client/docs/research/03-red-y-cifrado.md, "Cadena de recepción"). Built with shifts, not by casting the buffer to
// std::uint32_t*: a cast ignores alignment and aliasing rules, and its result would depend on the machine's byte order.
namespace Client::Core
{
class ENGINE_API ByteReader
{
public:
    // Does not copy the bytes: `buffer` must stay alive while reading.
    explicit ByteReader(std::span<const std::byte> buffer);

    // A std::span<const T> accepts a temporary, so `ByteReader reader(LoadFile());` would dangle on the first read.
    // This rejects the temporaries that own their bytes (a std::vector, a std::array); views and lvalues use the one
    // above.
    template <typename Range>
        requires std::ranges::range<Range> && (!std::ranges::borrowed_range<Range>)
    explicit ByteReader(Range&& temporary) = delete;

    // Each read advances past what it read. With too few bytes left it returns std::nullopt, reading and advancing
    // nothing.
    [[nodiscard]] std::optional<std::uint8_t> ReadU8();
    [[nodiscard]] std::optional<std::uint16_t> ReadU16Le();
    [[nodiscard]] std::optional<std::uint32_t> ReadU32Le();
    [[nodiscard]] std::optional<std::uint16_t> ReadU16Be();

    // Not copied: a view into the same buffer.
    [[nodiscard]] std::optional<std::span<const std::byte>> ReadBytes(std::size_t count);

    [[nodiscard]] std::size_t Remaining() const;

private:
    std::optional<std::uint32_t> ReadUnsigned(std::size_t size, std::endian order);

    std::span<const std::byte> m_buffer;
    std::size_t m_position = 0;
};
} // namespace Client::Core
