#include "Scenes/WorldTransitions.h"

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Data/Models/PlayerModels.h"
#include "Protocol/World.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorConstruction.h"
#include "Scenes/ActorState.h"
#include "Scenes/CombatActionSpeeds.h"
#include "Scenes/GameUi.h"
#include "Scenes/IWorldTransitionPublisher.h"
#include "Scenes/MapLoad.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "Scenes/WorldTransitionSupport.h"
#include "UI/OldWindows.h"
#include "World/GameCamera.h"
#include "World/TerrainCulling.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Client::Scenes
{
namespace
{
constexpr float CellSize = 100.0f;
constexpr float CellCenter = 50.0f;
constexpr float DirectionDegrees = 45.0f;
constexpr std::int32_t FirstChaosCastle = 18;
constexpr std::int32_t LastChaosCastle = 23;
bool IsChaosCastle(std::int32_t map)
{
    return map >= FirstChaosCastle && map <= LastChaosCastle;
}
void WriteHeroCells(ActorState& hero, const Protocol::World::Teleport& reply, const WorldEntryResources& resources)
{
    {
        const Render::Models::LogicalAccess x(hero.cellXAccesses, resources.cloth.random,
                                              Render::Models::SmallLogicalCloseRandoms);
        hero.cellX = reply.x;
    }
    {
        const Render::Models::LogicalAccess y(hero.cellYAccesses, resources.cloth.random,
                                              Render::Models::SmallLogicalCloseRandoms);
        hero.cellY = reply.y;
    }
    {
        const Render::Models::LogicalAccess x(hero.cellXAccesses, resources.cloth.random,
                                              Render::Models::SmallLogicalCloseRandoms);
        hero.position.x = static_cast<float>(hero.cellX) * CellSize + CellCenter;
    }
    {
        const Render::Models::LogicalAccess y(hero.cellYAccesses, resources.cloth.random,
                                              Render::Models::SmallLogicalCloseRandoms);
        hero.position.y = static_cast<float>(hero.cellY) * CellSize + CellCenter;
    }
}
Core::Expected<void, std::string> ChangeTeleportMap(std::uint8_t map, std::uint16_t playerKey,
                                                    const WorldEntryResources& resources,
                                                    IWorldTransitionPublisher& publisher)
{
    const auto oldMap = resources.world.Runtime().map;
    if (oldMap == map)
        return {};
    if (auto loaded = LoadTransitionMap(map, playerKey, resources); !loaded.HasValue())
        return loaded;
    const auto loadedMap = resources.world.Runtime().map;
    constexpr std::int32_t Crywolf = 34;
    if (loadedMap == Crywolf)
        publisher.RequestCrywolfState();
    if (IsChaosCastle(oldMap) || IsChaosCastle(loadedMap))
    {
        // 4c7af1..4c7b2f: el mismo prefijo corre al entrar y al salir, antes de 512a30: el sonido, las ventanas viejas
        // escondidas sin anotarlas (0x4906a0 con 1 y 0, Q-C572) y la de amigos suelta (0x4914e0).
        WorldRuntime& runtime = resources.world.Runtime();
        runtime.lastSoundId = 0x9e;
        if (auto hidden = UI::HideTheOldWindows(runtime.oldWindows, 1, 0, OldWindowGlobalsOf(runtime));
            !hidden.HasValue())
            return hidden;
        if (auto dropped = UI::DropTheFriendsWindow(runtime.oldWindows); !dropped.HasValue())
            return dropped;
        return Core::Unexpected(std::string("0x4c7b34: rearmar el objeto de los comandos (0x6e592c) en la transición "
                                            "de Chaos Castle es de la CL9.4"));
    }
    resources.world.Runtime().lastSoundId = 0x9e;
    ArmEventMapObject(loadedMap, resources.world.Runtime());
    return RefreshTransitionHeight(PublishedHero(resources), resources);
}
Core::Expected<void, std::string> FinishTeleport(const WorldEntryResources& resources,
                                                 IWorldTransitionPublisher& publisher)
{
    auto& runtime = resources.world.Runtime();
    publisher.ConfirmTeleport();
    if (resources.clock != nullptr)
        runtime.movement.lastTeleportMs = static_cast<std::uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(resources.clock->Now()).count());
    runtime.movement.warpPending = false;
    {
        const Render::Models::LogicalAccess wait(runtime.warpDepth, resources.cloth.random,
                                                 Render::Models::SmallLogicalCloseRandoms);
        runtime.warpCounter = WaitDrawThreshold;
    }
    runtime.movement.mouseUpdateTime = 0;
    runtime.movement.mouseUpdateTimeMax = 6;
    if (auto closed = CloseTransitionWindows(resources); !closed.HasValue())
        return closed;
    auto& hero = PublishedHero(resources);
    if (auto raven = RecreateTransitionRaven(hero, resources); !raven.HasValue())
        return raven;
    CreateTransitionArrival(hero, resources);
    hero.alpha = 0.0f;
    runtime.storm = 0;
    runtime.panels.repairMode = 0;
    runtime.field78abb10 = 0;
    runtime.selectedTargets.fill(-1);
    runtime.byte6c8e4d = 0xff;
    return StopTransitionHero(hero, resources);
}
void ApplyRegenStats(const Protocol::World::Regen& reply, const WorldEntryResources& resources)
{
    const Render::Models::LogicalAccess own(resources.world.Runtime().ownRecordDepth, resources.cloth.random,
                                            Render::Models::OwnRecordCloseRandoms);
    auto& record = resources.world.OwnCharacter();
    record.life = reply.life;
    record.mana = reply.mana;
    record.bp = reply.bp;
    record.experience = reply.experience;
    record.zen = reply.zen;
}
Core::Expected<void, std::string> RebuildRegenHero(const Protocol::World::Regen& reply, std::uint16_t playerKey,
                                                   const WorldEntryResources& resources)
{
    auto& world = resources.world;
    const auto pk = PublishedHero(resources).pk;
    const auto word1f2 = PublishedHero(resources).word1f2;
    constexpr std::size_t ClearedActors = 400;
    for (std::size_t index = 0; index < ClearedActors; ++index)
        world.Actors()[index].alive = false;
    auto& hero = world.Actors()[static_cast<std::size_t>(world.Runtime().heroSlot)];
    const auto guildStatus = hero.guildStatus;
    const auto byte369 = hero.byte369;
    const auto byte370 = hero.byte370;
    const ActorConstructionContext construction{world.Map().terrain, world.Runtime().map, World::CullingScene::Game,
                                                resources.camera.fixedHeight};
    const auto built =
        ConstructActor(hero, Data::Models::PlayerSkeletonModel,
                       {reply.x, reply.y, (static_cast<float>(reply.direction) - 1.0f) * DirectionDegrees},
                       construction, resources.cloth);
    if (!built.HasValue())
        return built;
    hero.guildStatus = guildStatus;
    hero.byte369 = byte369;
    hero.byte370 = byte370;
    hero.key = playerKey;
    {
        const Render::Models::LogicalAccess own(world.Runtime().ownRecordDepth, resources.cloth.random,
                                                Render::Models::OwnRecordCloseRandoms);
        hero.skin = 0;
        hero.characterClass = world.OwnCharacter().characterClass;
    }
    hero.pk = pk;
    hero.word1f2 = word1f2;
    hero.byte88 = 1;
    hero.safeZone = 1;
    if (auto dressed = DressTransitionHero(hero, resources); !dressed.HasValue())
        return dressed;
    if (auto stopped = StopTransitionHero(hero, resources); !stopped.HasValue())
        return stopped;
    CreateTransitionArrival(hero, resources);
    return {};
}
Core::Expected<void, std::string> ChangeRegenMap(const Protocol::World::Regen& reply, std::uint16_t playerKey,
                                                 ActorState& hero, const WorldEntryResources& resources)
{
    const auto oldMap = resources.world.Runtime().map;
    if (oldMap == reply.map)
        return {};
    if (auto loaded = LoadTransitionMap(reply.map, playerKey, resources); !loaded.HasValue())
        return loaded;
    if (IsChaosCastle(oldMap) || IsChaosCastle(resources.world.Runtime().map))
    {
        if (auto dressed = DressTransitionHero(PublishedHero(resources), resources); !dressed.HasValue())
            return dressed;
    }
    if (!IsChaosCastle(resources.world.Runtime().map))
        resources.world.Runtime().lastSoundId = 0x9e;
    return RefreshTransitionHeight(hero, resources);
}
} // namespace
Core::Expected<void, std::string> ApplyTeleport(const Protocol::World::Teleport& reply, std::uint16_t playerKey,
                                                const WorldEntryResources& resources,
                                                IWorldTransitionPublisher& publisher)
{
    if (resources.world.Runtime().cursorItemCount > 0)
        return Core::Unexpected(std::string("0x5be150: el ítem en el cursor no está portado"));
    auto& hero = PublishedHero(resources);
    WriteHeroCells(hero, reply, resources);
    if (auto height = RefreshTransitionHeight(hero, resources); !height.HasValue())
        return height;
    hero.angles.z = (static_cast<float>(reply.direction) - 1.0f) * DirectionDegrees;
    if (reply.gate == 0)
    {
        if (auto teleported = StartTheTeleportAction(hero, resources); !teleported.HasValue())
            return teleported;
        resources.world.Runtime().movement.gateRequestBlocked = 0;
        return StopTransitionHero(hero, resources);
    }
    ClearTransitionActorsAndItems(playerKey, resources);
    if (auto changed = ChangeTeleportMap(reply.map, playerKey, resources, publisher); !changed.HasValue())
        return changed;
    return FinishTeleport(resources, publisher);
}
Core::Expected<void, std::string> ApplyRegen(const Protocol::World::Regen& reply, std::uint16_t playerKey,
                                             const WorldEntryResources& resources)
{
    auto& world = resources.world;
    world.Runtime().movement.pendingClick = false;
    world.Runtime().movement.gateRequestBlocked = 0;
    PublishedHero(resources).alive = false;
    ApplyRegenStats(reply, resources);
    if (auto rebuilt = RebuildRegenHero(reply, playerKey, resources); !rebuilt.HasValue())
        return rebuilt;
    ClearTransitionActorsAndItems(playerKey, resources);
    auto& hero = world.Actors()[static_cast<std::size_t>(world.Runtime().heroSlot)];
    if (auto changed = ChangeRegenMap(reply, playerKey, hero, resources); !changed.HasValue())
        return changed;
    if (auto raven = RecreateTransitionRaven(hero, resources); !raven.HasValue())
        return raven;
    if (hero.alive)
        hero.byte374 =
            (PublishedHero(resources).word1f2 != NoItemModel && hero.word1f2 == PublishedHero(resources).word1f2) ? 1
                                                                                                                  : 0;
    if (auto closed = CloseTransitionWindows(resources); !closed.HasValue())
        return closed;
    return {};
}
} // namespace Client::Scenes
