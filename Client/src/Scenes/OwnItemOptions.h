#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace Client::Scenes
{
// Vista prestada de World::ItemState: {id, durability, optionCount, optionBytes}.
// optionBytes conserva el solapamiento tag8/valor0 del productor canónico 54bf40.
// Cada lectura alcanzada debe caber en el slice, también si count cambia por alias;
// no se limita artificialmente a las nueve opciones del productor ordinario.
struct OwnItemOptionsView
{
    const std::uint16_t& id;
    const std::uint8_t& durability;
    const std::uint8_t& count;
    std::span<const std::uint8_t> optionBytes;
};
// Destino word little endian prestado, incluso desalineado o solapado con la fuente.
// Contratos cerrados 0054e540 / 0054e590: frame estable, sin escritores concurrentes.
void AddOwnItemOptionValues(std::span<std::byte, 2> destination, std::uint32_t selector, OwnItemOptionsView options);
void ApplyOwnItemOptionPercent(std::span<std::byte, 2> destination, std::uint32_t selector, OwnItemOptionsView options,
                               std::uint32_t factor);
} // namespace Client::Scenes
