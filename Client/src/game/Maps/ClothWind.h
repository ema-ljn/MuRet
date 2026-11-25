#pragma once

#include "Maps/MapId.h"
#include "Models/Cloth.h"

#include <cstdint>

namespace Client::Maps
{
// 0x4551f4.
[[nodiscard]] constexpr Render::Models::ClothWindSource ClothWindForMap(std::uint8_t map)
{
    using Render::Models::ClothWindSource;
    return map == LoginMap ? ClothWindSource::Randomized : ClothWindSource::Base;
}
} // namespace Client::Maps
