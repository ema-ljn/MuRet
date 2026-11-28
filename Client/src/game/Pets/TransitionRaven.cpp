#include "Pets/TransitionRaven.h"

#include "Core/Expected.h"
#include "Kernel/ActorState.h"
#include "Maps/MapId.h"
#include "Pets/DarkSpirit.h"
#include "Terrain/TerrainDefines.h"
#include "Terrain/TerrainHeight.h"
#include "Terrain/WorldTerrain.h"

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace Client::Pets
{
namespace
{
// 0x4167b0 from 0x420d60 or 0x420cf0: the new raven at +0x430, at the centre of its owner's cell and at the terrain's
// height there (0x5f93c0).
Core::Expected<void, std::string> CreateTheRaven(Kernel::ActorState& actor, const RavenPlacement& placement,
                                                 std::string_view where)
{
    const Terrain::WorldTerrain& terrain = placement.terrain;
    if (!terrain.heights || !terrain.attributes)
        return Core::Unexpected(std::format("{}: faltan alturas o atributos para el cuervo", where));
    using Terrain::CellSize;
    constexpr float Center = 50.0f;
    const double height = Terrain::TerrainHeightAt(
        *terrain.heights, *terrain.attributes, static_cast<float>(actor.cellX) * CellSize + Center,
        static_cast<float>(actor.cellY) * CellSize + Center, placement.fixedHeight);
    actor.darkSpirit = CreateDarkSpirit(actor.cellX, actor.cellY, height, placement.random,
                                        {actor.cellXAccesses, actor.cellYAccesses, placement.openCounterAccesses});
    return {};
}
} // namespace

Core::Expected<void, std::string> RecreateTheRaven(Kernel::ActorState& actor, const RavenPlacement& placement)
{
    constexpr std::uint16_t Raven = 0x1c17;
    if (actor.hands[1].model != Raven)
        return {};
    actor.darkSpirit.reset();
    return CreateTheRaven(actor, placement, "0x420d60");
}

Core::Expected<void, std::string> ReplaceTheRaven(Kernel::ActorState& actor, std::int32_t map,
                                                  const RavenPlacement& placement)
{
    actor.darkSpirit.reset();
    // 0x420d1a..0x420d22: 0x420cf0 does not create the raven on maps 18 to 23 (Chaos Castle); 0x420d60 does not look at
    // the map.
    if (map >= Maps::FirstChaosCastleMap && map <= Maps::LastChaosCastleMap)
        return {};
    return CreateTheRaven(actor, placement, "0x420cf0");
}
} // namespace Client::Pets
