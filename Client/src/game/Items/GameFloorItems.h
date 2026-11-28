#pragma once

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Kernel/DrawTailState.h"

#include <span>
#include <string>

namespace Client::Data::Terrain
{
struct TerrainAttributes;
struct TerrainHeights;
} // namespace Client::Data::Terrain
namespace Client::Particles
{
class Particles;
}
namespace Client::Items
{
struct FloorItemStepContext
{
    const Data::Terrain::TerrainHeights& heights;
    const Data::Terrain::TerrainAttributes& attributes;
    Crt::CrtRandom& random;
    Particles::Particles& particles;
    float worldClock = 0;
    float fixedHeight = 1200;
};
// 0x607fb0: the angles and scale of the item at rest on the floor, by its model. It draws no rand().
void SettleFloorItem(Kernel::FloorItemState& item);

// 0x637078 → 0x608880 (not 0x608f70): every live one, fall/contact 0x607fb0 and sparks 0x608780.
// The caller skips the step if DAT_07d36899 != 0. A failure keeps the slot's prefix and the earlier ones.
[[nodiscard]] Core::Expected<void, std::string> StepFloorItems(std::span<Kernel::FloorItemState> items,
                                                               const FloorItemStepContext& context);
} // namespace Client::Items
