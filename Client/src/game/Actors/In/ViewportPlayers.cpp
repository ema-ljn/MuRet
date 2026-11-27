#include "Actors/In/ViewportPlayers.h"

#include "Actors/ActorCharacterSet.h"
#include "Actors/ActorConstruction.h"
#include "Actors/ActorCreation.h"
#include "Actors/ActorEffectFlags.h"
#include "Actors/ActorsState.h"
#include "Actors/ViewportHooks.h"
#include "Actors/ViewportPacketRing.h"
#include "Actors/ViewportRecord.h"
#include "Camera/GameCamera.h"
#include "Core/Expected.h"
#include "Kernel/ActorSearch.h"
#include "Kernel/ActorState.h"
#include "Kernel/CharacterClass.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Maps/MapId.h"
#include "Models/Animation.h"
#include "Protocol/Actors/Viewport.h"
#include "Session/WorldEntry.h"
#include "Tables/PlayerModels.h"
#include "Terrain/TerrainCulling.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>

namespace Client::Actors::In
{
namespace
{
// 0x4c8a36: states 2 to 4 with their action (the second one for class 2: & 7, the elf).
std::optional<std::uint8_t> StateAction(std::uint8_t state, std::uint8_t characterClass)
{
    const bool elf = Kernel::BaseClassOf(characterClass) == Kernel::CharacterClass::Elf;
    switch (state)
    {
    case 2:
        return elf ? std::uint8_t{0xc7} : std::uint8_t{0xc5};
    case 3:
        return elf ? std::uint8_t{0xcc} : std::uint8_t{0xcb};
    case 4:
        return elf ? std::uint8_t{0xca} : std::uint8_t{0xc9};
    default:
        return std::nullopt;
    }
}

// What state 1 keeps from the live character with the key (0x4c8919..0x4c8965).
struct Kept
{
    std::uint8_t byte1f2 = 0;
    std::uint8_t guildStatus = 0;
    std::uint8_t byte369 = 0;
    std::uint8_t byte370 = 0;
    std::uint8_t byte360 = 0;
    std::uint32_t flags78 = 0;
    std::uint8_t byte372 = 0;
};

Kept KeepFrom(const Kernel::ActorState& actor)
{
    return {.byte1f2 = static_cast<std::uint8_t>(actor.word1f2),
            .guildStatus = actor.guildStatus,
            .byte369 = actor.byte369,
            .byte370 = actor.byte370,
            .byte360 = actor.byte360,
            .flags78 = actor.flags78,
            .byte372 = actor.byte372};
}

// 0x4c89b4..0x4c89ed: the MOVZX leaves +0x1f2 with the low byte and the high one at 0.
void GiveBack(Kernel::ActorState& actor, const Kept& kept)
{
    actor.word1f2 = kept.byte1f2;
    actor.guildStatus = kept.guildStatus;
    actor.byte369 = kept.byte369;
    actor.byte370 = kept.byte370;
    actor.byte360 = kept.byte360;
    actor.flags78 = kept.flags78;
    actor.byte372 = kept.byte372;
}

using Maps::IsKalimaMap;

// 0x4c89f3..0x4c8a04: ((b & 0x10) | (b >> 4)) >> 1.
std::uint8_t ClassOf(std::uint8_t classAndState)
{
    constexpr std::uint8_t EvolutionBit = 0x10;
    constexpr int ClassShift = 4;
    return static_cast<std::uint8_t>(
        static_cast<std::uint8_t>((classAndState & EvolutionBit) | (classAndState >> ClassShift)) >> 1);
}

constexpr std::uint8_t TeleportState = 1;

// 0x4c8a26..0x4c8c11: state 1's action and the packet to the queue again, or the action of states 2 to 4.
Core::Expected<void, std::string> StartTheState(Kernel::ActorState& actor, std::uint8_t state,
                                                std::span<const std::byte> packet,
                                                const Scenes::WorldEntryResources& resources,
                                                const IViewportHooks& hooks)
{
    if (state == TeleportState)
    {
        if (auto teleported = hooks.StartTheTeleportAction(actor); !teleported.HasValue())
            return teleported;
        // 0x4c8a43..0x4c8ad4: the whole packet to the queue again (Q-C801).
        ActorsState* const actors = Scenes::ActorsOf(resources);
        if (actors == nullptr)
            return Core::Unexpected(std::string("0x4c8a43: no Actors state for the viewport's queue"));
        StoreViewportPacket(actors->viewportPackets, packet);
        return {};
    }
    const auto action = StateAction(state, actor.characterClass);
    if (!action)
        return {};
    const auto* skeleton = resources.models.SkeletonOf(Tables::PlayerSkeletonModelId);
    if (skeleton == nullptr)
        return Core::Unexpected(std::string("0x4ea030: no Player.bmd for the 0x12's action"));
    Models::SetAction(actor.animation, *skeleton, *action);
    return {};
}

// Just arrived, at the center of its cell with effect 0x61f and the alpha at 0; otherwise, its path.
Core::Expected<void, std::string> ArriveOrWalk(Kernel::ActorState& actor, const Protocol::Actors::PlayerRecord& record,
                                               const Scenes::WorldEntryResources& resources,
                                               const IViewportHooks& hooks)
{
    if (!record.isNew)
        return WalkToTheTarget(actor, record.targetX, record.targetY, resources);
    PutInTheCellCenter(actor, resources.cloth.random);
    hooks.CreateTheArrivalEffect(actor);
    actor.alpha = 0.0f;
    return {};
}

Core::Expected<void, std::string> ApplyPlayerRecord(const Protocol::Actors::PlayerRecord& record,
                                                    std::span<const std::byte> packet,
                                                    const Scenes::WorldEntryResources& resources,
                                                    const IViewportHooks& hooks)
{
    Kernel::WorldState& world = resources.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    constexpr std::uint8_t StateMask = 0xf;
    const std::uint8_t state = record.classAndState & StateMask;
    // 0x4c88e2..0x4c8965.
    std::optional<Kept> kept;
    if (state == TeleportState)
    {
        const std::size_t previous = Kernel::ActorWithKey(world, record.key);
        if (previous != CreatedActorSearch)
            kept = KeepFrom(world.Actors()[previous]);
    }
    // 0x4c8968..0x4c8992.
    const ActorConstructionContext construction{Scenes::TerrainOf(resources), runtime.map, Terrain::CullingScene::Game,
                                                resources.camera.fixedHeight};
    const auto created = CreateActor(world, record.key, Tables::PlayerSkeletonModel, {record.x, record.y, 0.0f},
                                     construction, resources.cloth);
    if (!created.HasValue())
        return Core::Unexpected(created.Error());
    const std::size_t index = created.Value();
    Kernel::ActorState& actor = world.Actors()[index];
    if (kept)
        GiveBack(actor, *kept);
    // 0x4c89f3..0x4c8a2a.
    actor.characterClass = ClassOf(record.classAndState);
    actor.skin = 0;
    actor.byte88 = 1;
    actor.pk = PkOf(record.directionAndPk);
    if (auto started = StartTheState(actor, state, packet, resources, hooks); !started.HasValue())
        return started;
    // 0x4c8adc..0x4c8aec.
    if (auto flagged = ApplyEffectFlags(actor, record.effects, index == runtime.hero, resources); !flagged.HasValue())
        return flagged;
    WriteTheCells(actor, record.x, record.y, resources.cloth.random);
    actor.angles.z = AngleOf(record.directionAndPk);
    if (auto placed = ArriveOrWalk(actor, record, resources, hooks); !placed.HasValue())
        return placed;
    // 0x4c91aa..0x4c91e1: in Kalima (24..29 and 36), 0x530860(0x610, position, position, angles, 8, player, 20, −1, 0,
    // 0, −1), without rand(): the class of the Atlans fish with the ActorState as owner. With the joints full it
    // creates nothing and goes on.
    if (IsKalimaMap(runtime.map))
    {
        if (auto beam = hooks.CreateTheKalimaBeam(actor); !beam.HasValue())
            return Core::Unexpected(std::format("0x4c91dc: {}", beam.Error()));
    }
    // 0x4c91e4..0x4c91fa.
    if (auto set = ApplyCharacterSet(Kernel::ActorWithKey(world, record.key), record.equipment, resources);
        !set.HasValue())
        return set;
    // 0x4c920b..0x4c9246.
    actor.byte1e3 = 0;
    actor.name = record.name;
    if (kept)
    {
        actor.byte360 = kept->byte360;
        actor.flags78 = kept->flags78;
    }
    // 0x4c9249..0x4c9256.
    ApplyCastleSiegeFlags(actor, record.effects, runtime.map);
    return hooks.RefreshTheSiegePanel();
}
} // namespace

Core::Expected<void, std::string> ApplyViewportPlayers(const Protocol::Actors::Players& players,
                                                       const Scenes::WorldEntryResources& resources,
                                                       const IViewportHooks& hooks)
{
    // The session already made the dispatcher's copy (0x4e372e..0x4e37b8), before the reader (CopyToPacketRing).
    for (const Protocol::Actors::PlayerRecord& record : players.records)
    {
        // 0x4c88d2..0x4c88dc.
        if (IsHiddenName(record.name))
            continue;
        if (auto applied = ApplyPlayerRecord(record, players.packet, resources, hooks); !applied.HasValue())
            return applied;
    }
    return {};
}
} // namespace Client::Actors::In
