#include "Scenes/OwnItemOptions.h"

#include "World/ItemState.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Client::Scenes
{
namespace
{
std::uint32_t ReadOptionDestination(std::span<std::byte, 2> destination)
{
    return std::to_integer<std::uint32_t>(destination[0]) | (std::to_integer<std::uint32_t>(destination[1]) << 8);
}

void WriteOptionDestination(std::span<std::byte, 2> destination, std::uint32_t value)
{
    destination[0] = static_cast<std::byte>(value & 0xffu);
    destination[1] = static_cast<std::byte>((value >> 8) & 0xffu);
}
} // namespace

void AddOwnItemOptionValues(std::span<std::byte, 2> destination, std::uint32_t selector, OwnItemOptionsView options)
{
    constexpr std::uint16_t EmptyItem = 0xffff;
    if (options.id == EmptyItem)
        return;
    for (std::size_t index = 0; index < options.count; ++index)
    {
        if (options.optionBytes[index] != selector || options.durability == 0)
            continue;
        const std::uint32_t increment = options.optionBytes[World::ItemOptionValueOffset + index];
        WriteOptionDestination(destination, ReadOptionDestination(destination) + increment);
    }
}

void ApplyOwnItemOptionPercent(std::span<std::byte, 2> destination, std::uint32_t selector, OwnItemOptionsView options,
                               std::uint32_t factor)
{
    constexpr std::uint16_t EmptyItem = 0xffff;
    constexpr std::int32_t PercentScale = 100;
    if (options.id == EmptyItem)
        return;
    for (std::size_t index = 0; index < options.count; ++index)
    {
        if (options.optionBytes[index] != selector)
            continue;
        const std::uint32_t previous = ReadOptionDestination(destination);
        // IMUL low32 signed y división truncada: no signed16 ni producto ampliado.
        const std::uint32_t product = previous * (factor & 0xffffu);
        const std::int32_t adjustment = std::bit_cast<std::int32_t>(product) / PercentScale;
        WriteOptionDestination(destination, previous + static_cast<std::uint32_t>(adjustment));
    }
}
} // namespace Client::Scenes
