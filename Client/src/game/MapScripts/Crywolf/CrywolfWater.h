#pragma once

#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <functional>
#include <string>

namespace Client::Crt
{
class CrtRandom;
}
namespace Client::Terrain
{
struct TerrainView;
} // namespace Client::Terrain

namespace Client::Particles
{
class Particles;
} // namespace Client::Particles

namespace Client::MapScripts::Crywolf
{

// What 0x5fa5c0 reads outside the cell on map 34. The caller lends it during the pass.
struct CrywolfWaterContext
{
    // +0x18 and +0x1c of the hero (0x743a170): the bubble's z and its angles.
    float heroZ = 0.0f;
    glm::vec3 heroAngles{0.0f};
    Crt::CrtRandom* random = nullptr;
    Particles::Particles* particles = nullptr;
    std::reference_wrapper<const float> worldClock;
};

// 0x5fa695..0x5fa731: on the 34, each time 0x5fa830 asks 0x5fa5c0 for the water UVs (the bottom layer when layer 1 is
// at 5, with some corner of alpha below 1; and the alpha one when layer 2 is also 5 and some corner passes 0, like
// Terrain::DrawTerrainCell), a rand() and, with 0 modulo 50, the bubble 0x698/0 at (x · 100 + rand() % 100 + 1;
// y · 100 + rand() % 100 + 1; hero's z + 10), with the hero's angles, light (0.3; 0.4; 0.2), scale 1 and no owner. It
// goes before drawing the cell (x, y), which already passed bit 0x08: the draw takes no rand(). Cuts without the
// random source or the pool when it reads them.
[[nodiscard]] Core::Expected<void, std::string> StepCrywolfWaterCell(const Terrain::TerrainView& view, std::uint8_t x,
                                                                     std::uint8_t y,
                                                                     const CrywolfWaterContext& context);
} // namespace Client::MapScripts::Crywolf
