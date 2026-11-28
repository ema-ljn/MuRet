#include "Scenes/CastleBattleActors.h"

#include "Actors/ActionReply.h"
#include "Core/Expected.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Models/Cloth.h"
#include "Models/LogicalAccess.h"
#include "Kernel/ActorState.h"
#include "Scenes/GuildState.h"
#include "Monsters/MonsterCreation.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Pathfinding/TerrainWalls.h"
#include "Terrain/WorldTerrain.h"
#include "Audio/SoundId.h"

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <span>

namespace Client::Scenes
{
namespace
{
constexpr std::uint32_t Defender = 0x40000;
constexpr std::uint32_t FirstAttacker = 0x80000;
constexpr std::uint32_t SecondAttacker = 0x400000;
constexpr std::uint32_t ThirdAttacker = 0x800000;
constexpr std::uint32_t OpenGate = 0x20000;
constexpr Audio::SoundId GateSound{0x1ed};

Core::Expected<void, std::string> ChangeTheGateWall(Kernel::ActorState& actor, bool open,
                                                    const WorldEntryResources& resources)
{
    std::int32_t x = 0, y = 0;
    // El orden de los cierres de 4 bytes es Y, X; no se comparte con accesos exteriores.
    {
        const Models::LogicalAccess access(actor.cellYAccesses, resources.cloth.random,
                                           Models::SmallLogicalCloseRandoms, actor.cellYLeft);
        y = actor.cellY;
    }
    {
        const Models::LogicalAccess access(actor.cellXAccesses, resources.cloth.random,
                                           Models::SmallLogicalCloseRandoms, actor.cellXLeft);
        x = actor.cellX;
    }
    for (const Pathfinding::GateCell& gate : Pathfinding::GateCells)
    {
        if (x != gate.x || y != gate.y)
            continue;
        const auto& attributes = TerrainOf(resources).attributes;
        if (!attributes)
            return Core::Unexpected(std::string("0x42a110: faltan los atributos de las puertas del castillo"));
        constexpr int GateWidth = 4;
        for (int dx = -1; dx < GateWidth - 1; ++dx)
        {
            auto& cell = attributes->cells[Data::Terrain::CellIndex(static_cast<std::uint8_t>(x + dx),
                                                                    static_cast<std::uint8_t>(y + 1))];
            cell = open ? static_cast<std::uint16_t>(cell & ~Pathfinding::BlockedWallBit)
                        : static_cast<std::uint16_t>(cell | Pathfinding::BlockedWallBit);
        }
        break;
    }
    return {};
}
} // namespace

Core::Expected<void, std::string> SetTheCastleGateState(Kernel::ActorState& actor, bool open,
                                                        const WorldEntryResources& resources)
{
    if (auto action = Actors::SetMonsterAction(actor, open ? 1 : 0, 0x4ea030, resources); !action.HasValue())
        return action;
    return ChangeTheGateWall(actor, open, resources);
}

Core::Expected<void, std::string> SetTheCastleGate(Kernel::ActorState& cellsActor, Kernel::ActorState& gate, bool open,
                                                   const WorldEntryResources& resources)
{
    if (auto action = Actors::SetMonsterAction(gate, open ? 1 : 0, 0x4ea030, resources); !action.HasValue())
        return action;
    gate.flags78 = open ? OpenGate : 0;
    if (auto wall = ChangeTheGateWall(cellsActor, open, resources); !wall.HasValue())
        return wall;
    resources.world.Runtime().RequestSound(GateSound);
    return {};
}

Core::Expected<void, std::string> ApplyTheCastleOwner(std::string_view name, Kernel::WorldState& world)
{
    return ApplyTheCastleOwner(name, world.Actors().first<CastleBattleActorCount>(), world.Runtime().guild);
}

Core::Expected<void, std::string> ApplyTheCastleOwner(std::string_view name,
                                                      std::span<Kernel::ActorState, CastleBattleActorCount> actors,
                                                      const GuildState& guild)
{
    for (Kernel::ActorState& actor : actors)
    {
        if (!actor.alive || !actor.seen)
            continue;
        Kernel::ReleaseTheLayer(actor);
        const auto unionName = UnionNameAt(guild, static_cast<std::int16_t>(actor.word1f2));
        if (!unionName)
            return Core::Unexpected(std::format("0x42aea2: la alianza del índice {} sale de la tabla del gremio",
                                                static_cast<std::int16_t>(actor.word1f2)));
        if (*unionName == name)
        {
            actor.flags78 &= ~(FirstAttacker | SecondAttacker | ThirdAttacker);
            actor.byte360 = 12;
            actor.flags78 |= Defender;
        }
        else if ((actor.flags78 & (FirstAttacker | Defender)) != 0)
        {
            actor.flags78 &= ~Defender;
            actor.byte360 = 6;
            actor.flags78 |= FirstAttacker;
        }
        else if ((actor.flags78 & SecondAttacker) != 0)
            actor.byte360 = 7;
        else if ((actor.flags78 & ThirdAttacker) != 0)
            actor.byte360 = 8;
    }
    return {};
}
} // namespace Client::Scenes
