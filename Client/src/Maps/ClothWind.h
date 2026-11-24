#pragma once

#include "Render/Models/Cloth.h"

#include <cstdint>

namespace Client::Maps
{
inline constexpr std::uint8_t LoginMap = 0x37;

// 0x4551f4.
[[nodiscard]] constexpr Render::Models::ClothWindSource ClothWindForMap(std::uint8_t map)
{
    using Render::Models::ClothWindSource;
    return map == LoginMap ? ClothWindSource::Randomized : ClothWindSource::Base;
}
} // namespace Client::Maps
