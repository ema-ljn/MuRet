#include "Scenes/CharacterPicking.h"

#include "Scenes/ActorPickingBox.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainMapping.h"
#include "Models/RayPicking.h"
#include "Scenes/ActorState.h"
#include "Scenes/PartyActors.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/TerrainPickingSegment.h"
#include "World/WorldTerrain.h"

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace Client::Scenes
{
namespace
{
constexpr std::size_t PickedActors = 400;
constexpr std::int32_t NoPick = -1;

bool Matches(const ActorState& actor, std::uint8_t mask)
{
    return (actor.byte88 & mask) == actor.byte88;
}

bool Visible(const ActorState& actor)
{
    return actor.alive && actor.seen && actor.alpha > 0.0f;
}

// 0x59e737..0x59e7e6: +0xe8 en 1 antes de la tabla de color por el WORD de +0x1d6 y el PK.
void Tint(ActorState& actor)
{
    actor.byteE8 = 1;
    constexpr float Gray = 0.2f;    // 0x3e4ccccd
    constexpr float Dark = -0.4f;   // 0xbecccccd
    constexpr float Darker = -0.6f; // 0xbf19999a
    constexpr float Bright = 1.5f;  // 0x3fc00000
    switch (actor.level)
    {
    case 0:
        actor.light = glm::vec3(Gray);
        break;
    case 1:
        actor.light = glm::vec3(Dark);
        break;
    case 2:
        actor.light = {Gray, Darker, Darker};
        break;
    case 3:
        actor.light = glm::vec3(Bright);
        break;
    case 4:
        actor.light = {0.3f, Gray, -0.5f};
        break;
    default:
        break;
    }
    constexpr std::uint8_t MurdererPk = 6;
    if (actor.pk >= MurdererPk)
        actor.light = glm::vec3(Dark);
}

// 0x59e9ec..0x59ea6d: en Lorencia la capa 4 y en Devias la 3 (los techos y los pisos de arriba) solo con la capa de
// la celda del héroe. La celda sale de la posición truncada (0x69fd3c) y dividida por 100, Y · 256 + X. Una celda
// fuera de la capa, que el original leería de la memoria de al lado, MuRet la toma como de otra capa.
bool OnTheHeroLayer(const ActorState& actor, const WorldState& world)
{
    const WorldRuntime& runtime = world.Runtime();
    constexpr std::int32_t Lorencia = 0;
    constexpr std::int32_t Devias = 2;
    if (runtime.map != Lorencia && runtime.map != Devias)
        return true;
    const auto& mapping = world.Map().terrain.mapping;
    if (!mapping)
        return true;
    constexpr std::int32_t CellSize = 100;
    constexpr std::int32_t RowCells = 256;
    const std::int32_t index = static_cast<std::int32_t>(actor.position.y) / CellSize * RowCells +
                               static_cast<std::int32_t>(actor.position.x) / CellSize;
    if (index < 0 || static_cast<std::size_t>(index) >= mapping->layer1.size())
        return true;
    const std::uint8_t layer = mapping->layer1[static_cast<std::size_t>(index)];
    constexpr std::uint8_t LorenciaUpper = 4;
    constexpr std::uint8_t DeviasUpper = 3;
    const std::uint8_t upper = runtime.map == Lorencia ? LorenciaUpper : DeviasUpper;
    return layer != upper || static_cast<std::int32_t>(layer) == runtime.heroCellLayer;
}

// 0x59e9ad..0x59e9dc: (dz² + dy²) + dx² en x87 con el PC53 del startup de main.exe (0x6a4b73..0x6adf2c); MuRet
// conserva el orden de sumas y double, sin suponer PC64.
double DistanceSquared(const glm::vec3& position, const glm::vec3& distanceOrigin)
{
    const double dx = static_cast<double>(position.x) - static_cast<double>(distanceOrigin.x);
    const double dy = static_cast<double>(position.y) - static_cast<double>(distanceOrigin.y);
    const double dz = static_cast<double>(position.z) - static_cast<double>(distanceOrigin.z);
    return (dz * dz + dy * dy) + dx * dx;
}
} // namespace

std::int32_t PickTheCharacter(WorldState& world, std::uint8_t mask, const glm::vec3& distanceOrigin,
                              const std::optional<World::TerrainPickingSegment>& ray,
                              std::span<const ActorPickingBox> pickingBoxes)
{
    const auto actors = world.Actors();
    const std::size_t hero = world.Runtime().hero;
    // 0x59e737..0x59e7f6.
    for (std::size_t slot = 0; slot < PickedActors; ++slot)
    {
        ActorState& actor = actors[slot];
        if (Matches(actor, mask) && Visible(actor))
            Tint(actor);
    }
    // 0x59e7fc..0x59ea95. 0x5368d4a5: el mejor de arranque, ~1e12.
    std::int32_t picked = NoPick;
    float best = std::bit_cast<float>(0x5368d4a5u);
    constexpr std::uint32_t Hidden = 0x10000000;
    constexpr std::uint8_t PlayerKind = 1;
    for (std::size_t slot = 0; slot < PickedActors; ++slot)
    {
        const ActorState& actor = actors[slot];
        if (!Visible(actor) || actor.dying != 0 || (actor.flags7c & Hidden) == Hidden)
            continue;
        // 0x59e86a..0x59e948: un jugador busca su lugar en la party antes de las demás condiciones (CL9 hito 7).
        if (actor.byte88 == PlayerKind)
            MatchThePartyMembers(world, slot);
        // Sin segmento (antes del primer dibujo) no hay caja que tocar.
        if (!ray)
            continue;
        if (slot == hero || actor.byte42c == 0 || !Matches(actor, mask))
            continue;
        const ActorPickingBox storedBox = slot < pickingBoxes.size() ? pickingBoxes[slot] : PickingBoxOf(actor);
        if (!Models::SegmentHitsBox(ray->origin, ray->end, AxisAlignedBoxOf(storedBox)))
            continue;
        const double distance = DistanceSquared(actor.position, distanceOrigin);
        if (distance >= static_cast<double>(best) || !OnTheHeroLayer(actor, world))
            continue;
        best = static_cast<float>(distance);
        picked = static_cast<std::int32_t>(slot);
    }
    // 0x59ea9b..0x59eb66.
    MarkTheMissingMembers(world);
    return picked;
}
} // namespace Client::Scenes
