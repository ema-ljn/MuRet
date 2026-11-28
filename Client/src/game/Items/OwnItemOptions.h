#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace Client::Player
{
// A borrowed view of Kernel::ItemState. optionBytes keeps the tag8/value0 overlap of the canonical producer 0x54bf40.
// Every reached read must fit in the slice, also if count changes through aliasing; it is not artificially limited
// to the ordinary producer's nine options.
struct OwnItemOptionsView
{
    const std::uint16_t& id;
    const std::uint8_t& durability;
    const std::uint8_t& count;
    std::span<const std::uint8_t> optionBytes;
};
// A borrowed little-endian word destination, even misaligned or overlapping the source.
// Closed contracts 0x54e540 / 0x54e590: stable frame, no concurrent writers.
void AddOwnItemOptionValues(std::span<std::byte, 2> destination, std::uint32_t selector, OwnItemOptionsView options);
void ApplyOwnItemOptionPercent(std::span<std::byte, 2> destination, std::uint32_t selector, OwnItemOptionsView options,
                               std::uint32_t factor);
} // namespace Client::Player
