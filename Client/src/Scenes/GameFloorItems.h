#pragma once

#include "Core/Expected.h"
#include "Scenes/GameDrawTail.h"

#include <span>
#include <string>

namespace Client::Data::Terrain
{
struct TerrainAttributes;
struct TerrainHeights;
} // namespace Client::Data::Terrain
namespace Client::World
{
class Particles;
}
namespace Client::Scenes
{
struct FloorItemStepContext
{
    const Data::Terrain::TerrainHeights& heights;
    const Data::Terrain::TerrainAttributes& attributes;
    Render::Models::CrtRandom& random;
    World::Particles& particles;
    float worldClock = 0;
    float fixedHeight = 1200;
};
// 637078→608880 (no608f70): todos los vivos, caída/contacto607fb0 y chispas608780.
// El caller omite el paso si DAT_07d36899!=0. Un fallo conserva el prefijo del slot y los anteriores.
[[nodiscard]] Core::Expected<void, std::string> StepFloorItems(std::span<FloorItemState> items,
                                                               const FloorItemStepContext& context);
} // namespace Client::Scenes
