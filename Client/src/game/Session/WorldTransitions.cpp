#include "Session/WorldTransitions.h"
#include "Kernel/HelperTable.h"

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Tables/PlayerModels.h"
#include "Maps/MapId.h"
#include "Protocol/Movement/World.h"
#include "Models/Cloth.h"
#include "Models/LogicalAccess.h"
#include "Actors/ActorConstruction.h"
#include "Kernel/ActorState.h"
#include "Combat/CombatActionSpeeds.h"
#include "Scenes/GameUi.h"
#include "Pets/HelperTable.h"
#include "Session/IWorldTransitionPublisher.h"
#include "Scenes/MapBanner.h"
#include "Session/MapLoad.h"
#include "Scenes/InventoryMoves.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Session/WorldTransitionSupport.h"
#include "UI/OldWindows.h"
#include "UI/PanelRegistry.h"
#include "Camera/GameCamera.h"
#include "Terrain/TerrainCulling.h"
#include "Terrain/TerrainDefines.h"
#include "Sound/SoundIds.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace Client::Scenes
{
namespace
{
using Terrain::CellSize;
constexpr float CellCenter = 50.0f;
constexpr float DirectionDegrees = 45.0f;
using Maps::IsChaosCastle;
void WriteHeroCells(Kernel::ActorState& hero, const Protocol::Movement::Teleport& reply,
                    const WorldEntryResources& resources)
{
    {
        const Models::LogicalAccess x(hero.cellXAccesses, resources.cloth.random, Models::SmallLogicalCloseRandoms);
        hero.cellX = reply.x;
    }
    {
        const Models::LogicalAccess y(hero.cellYAccesses, resources.cloth.random, Models::SmallLogicalCloseRandoms);
        hero.cellY = reply.y;
    }
    {
        const Models::LogicalAccess x(hero.cellXAccesses, resources.cloth.random, Models::SmallLogicalCloseRandoms);
        hero.position.x = static_cast<float>(hero.cellX) * CellSize + CellCenter;
    }
    {
        const Models::LogicalAccess y(hero.cellYAccesses, resources.cloth.random, Models::SmallLogicalCloseRandoms);
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
        Kernel::WorldRuntime& runtime = resources.world.Runtime();
        runtime.RequestSound(Sound::ChaosSound, std::nullopt, true);
        if (auto hidden = UI::HideTheOldWindows(runtime.oldWindows, 1, 0, OldWindowGlobalsOf(runtime));
            !hidden.HasValue())
            return hidden;
        if (auto dropped = UI::DropTheFriendsWindow(runtime.oldWindows, runtime.ui.pressedButton); !dropped.HasValue())
            return dropped;
        // 4c7b34..4c7b53: mismas escrituras de ResetTheCommands, sin volver a crear el objeto.
        UI::ResetTheCommands(runtime.panels.delegated);
        if (auto dressed = DressTransitionHero(PublishedHero(resources), resources); !dressed.HasValue())
            return dressed;
        // 4c7b68: apagar los ayudantes del héroe después de publicar su equipo.
        Kernel::TurnOffHelpersOf(resources.world.Helpers(), runtime.hero);
    }
    if (!IsChaosCastle(loadedMap))
    {
        Kernel::WorldRuntime& runtime = resources.world.Runtime();
        runtime.StopSound(Sound::ChaosCastleSound, true);
        runtime.StopSound(Sound::ChaosSound, true);
        runtime.lastSoundId = Sound::ChaosSound;
    }
    ArmEventMapObject(loadedMap, resources.world.Runtime());
    if (auto height = RefreshTransitionHeight(PublishedHero(resources), resources); !height.HasValue())
        return height;
    // 0x4c7c17..0x4c7c4e: el renglón de bienvenida al mapa nuevo.
    return AddTheWelcomeLine(resources);
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
        const Models::LogicalAccess wait(runtime.warpDepth, resources.cloth.random, Models::SmallLogicalCloseRandoms);
        runtime.warpCounter = Kernel::WaitDrawThreshold;
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
void ApplyRegenStats(const Protocol::Movement::Regen& reply, const WorldEntryResources& resources)
{
    const Models::LogicalAccess own(resources.world.Runtime().ownRecordDepth, resources.cloth.random,
                                    Models::OwnRecordCloseRandoms);
    auto& record = resources.world.OwnCharacter();
    record.life = reply.life;
    record.mana = reply.mana;
    record.bp = reply.bp;
    record.experience = reply.experience;
    record.zen = reply.zen;
}
Core::Expected<void, std::string> RebuildRegenHero(const Protocol::Movement::Regen& reply, std::uint16_t playerKey,
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
    const Actors::ActorConstructionContext construction{TerrainOf(resources), world.Runtime().map,
                                                        Terrain::CullingScene::Game, resources.camera.fixedHeight};
    const auto built =
        Actors::ConstructActor(hero, Tables::PlayerSkeletonModel,
                               {reply.x, reply.y, (static_cast<float>(reply.direction) - 1.0f) * DirectionDegrees},
                               construction, resources.cloth);
    if (!built.HasValue())
        return built;
    hero.guildStatus = guildStatus;
    hero.byte369 = byte369;
    hero.byte370 = byte370;
    hero.key = playerKey;
    {
        const Models::LogicalAccess own(world.Runtime().ownRecordDepth, resources.cloth.random,
                                        Models::OwnRecordCloseRandoms);
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
Core::Expected<void, std::string> ChangeRegenMap(const Protocol::Movement::Regen& reply, std::uint16_t playerKey,
                                                 Kernel::ActorState& hero, const WorldEntryResources& resources)
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
    {
        Kernel::WorldRuntime& runtime = resources.world.Runtime();
        runtime.StopSound(Sound::ChaosCastleSound, true);
        runtime.StopSound(Sound::ChaosSound, true);
        runtime.lastSoundId = Sound::ChaosSound;
    }
    return RefreshTransitionHeight(hero, resources);
}
} // namespace
Core::Expected<void, std::string> ApplyTeleport(const Protocol::Movement::Teleport& reply, std::uint16_t playerKey,
                                                const WorldEntryResources& resources,
                                                IWorldTransitionPublisher& publisher)
{
    // 0x4c7523: 0x5be150 va antes de leer/mover al héroe, también con el gate0 de Teleport.
    const InventoryServices inventory = InventoryServicesOf(resources);
    if (auto returned = ReturnTheCursorItem({resources.world, resources.cloth.random, inventory}); !returned.HasValue())
        return returned;
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
    if (auto finished = FinishTeleport(resources, publisher); !finished.HasValue())
        return finished;
    // 0x4c830c..0x4c831c: con el gate (pkt[3]) en algo, 0x65dc30, el cartel con el nombre del mapa.
    auto& runtime = resources.world.Runtime();
    ShowTheMapBanner(runtime.hud.mapBanner, runtime.map);
    return {};
}
Core::Expected<void, std::string> ApplyRegen(const Protocol::Movement::Regen& reply, std::uint16_t playerKey,
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
        hero.byte374 = (PublishedHero(resources).word1f2 != Kernel::NoItemModel &&
                        hero.word1f2 == PublishedHero(resources).word1f2)
                           ? 1
                           : 0;
    // 0x4c5048..0x4c504e: 0x65dc30, el cartel con el nombre del mapa, antes de cerrar las ventanas (0x657a90).
    ShowTheMapBanner(world.Runtime().hud.mapBanner, world.Runtime().map);
    if (auto closed = CloseTransitionWindows(resources); !closed.HasValue())
        return closed;
    return {};
}
} // namespace Client::Scenes
