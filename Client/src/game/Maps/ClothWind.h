#pragma once

#include "Maps/MapId.h"
#include "Models/Cloth.h"

#include <cstdint>

namespace Client::Maps
{
// 0x4551f4.
[[nodiscard]] constexpr Models::ClothWindSource ClothWindForMap(std::uint8_t map)
{
    using Models::ClothWindSource;
    return map == LoginMap ? ClothWindSource::Randomized : ClothWindSource::Base;
}
} // namespace Client::Maps
