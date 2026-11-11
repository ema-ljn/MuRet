#include "Scenes/WorldTransitionSupport.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorActions.h"
#include "Scenes/ActorConstruction.h"
#include "Scenes/ActorEquipment.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/DarkSpirit.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/MapLoad.h"
#include "Scenes/SceneId.h"
#include "Scenes/SelectionState.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"
#include "World/ModelEffects.h"
#include "World/TerrainCulling.h"
#include "World/TerrainHeight.h"

#include <cstdint>
#include <string>

namespace Client::Scenes
{
ActorState& PublishedHero(const WorldEntryResources& resources)
{
    return resources.world.Actors()[resources.world.Runtime().hero];
}
Core::Expected<ActorStopContext, std::string> TransitionStopContext(const WorldEntryResources& resources)
{
    const auto* player = resources.models.SkeletonOf(Data::Models::PlayerSkeletonModel);
    if (player == nullptr)
        return Core::Unexpected(std::string("0x4f2250: falta Player.bmd en la transición"));
    return ActorStopContext{resources.world.Runtime(),
                            SceneId::Game,
                            *player,
                            resources.items,
                            resources.cloth.random,
                            resources.world.Map().terrain.attributes ? &resources.world.Map().terrain.attributes->cells
                                                                     : nullptr};
}
Core::Expected<void, std::string> StopTransitionHero(ActorState& actor, const WorldEntryResources& resources)
{
    actor.movement = 0;
    const auto stop = TransitionStopContext(resources);
    if (!stop.HasValue())
        return Core::Unexpected(stop.Error());
    const auto stopped = SetActorStop(actor, stop.Value());
    if (!stopped.HasValue())
        return Core::Unexpected(stopped.Error());
    if (stopped.Value().sound)
        resources.world.Runtime().lastSoundId = stopped.Value().sound;
    return {};
}
Core::Expected<void, std::string> DressTransitionHero(ActorState& actor, const WorldEntryResources& resources)
{
    auto& world = resources.world;
    const auto stop = TransitionStopContext(resources);
    if (!stop.HasValue())
        return Core::Unexpected(stop.Error());
    const ActorEquipmentContext context{
        world.OwnCharacter(), world.Runtime().ownRecordDepth, PublishedHero(resources), world.Runtime().fullSetScan,
        stop.Value(),         resources.itemSetTables,        &world.ItemSets()};
    const auto dressed = ApplyActorEquipment(actor, context);
    if (!dressed.HasValue())
        return Core::Unexpected(dressed.Error());
    if (dressed.Value().sound)
        world.Runtime().lastSoundId = dressed.Value().sound;
    return {};
}
Core::Expected<void, std::string> LoadTransitionMap(std::uint8_t map, std::uint16_t playerKey,
                                                    const WorldEntryResources& resources)
{
    resources.world.Runtime().map = map;
    const auto loaded = LoadMap(map, {resources.dataRoot, resources.world, SceneId::Game, playerKey,
                                      resources.cloth.random, resources.requestCastleSiegeState});
    if (!loaded.HasValue())
        return loaded;
    resources.selection.BorrowTerrain(resources.world.Runtime().map, &resources.world.Map().terrain);
    resources.cameraInput.map = static_cast<std::uint8_t>(resources.world.Runtime().map);
    return {};
}
Core::Expected<void, std::string> RefreshTransitionHeight(ActorState& actor, const WorldEntryResources& resources)
{
    const auto& terrain = resources.world.Map().terrain;
    if (!terrain.heights || !terrain.attributes)
        return Core::Unexpected(std::string("0x5f93c0: faltan alturas o atributos al trasladar al héroe"));
    const ActorConstructionContext context{terrain, resources.world.Runtime().map, World::CullingScene::Game,
                                           resources.camera.fixedHeight};
    constexpr std::uint16_t Dinorant = 0x1c15;
    actor.position.z = static_cast<float>(ConstructionHeight(context, actor.position.x, actor.position.y,
                                                             actor.pet.model == Dinorant, actor.safeZone != 0));
    return {};
}
Core::Expected<void, std::string> RecreateTransitionRaven(ActorState& actor, const WorldEntryResources& resources)
{
    constexpr std::uint16_t Raven = 0x1c17;
    if (actor.hands[1].model != Raven)
        return {};
    actor.darkSpirit.reset();
    const auto& terrain = resources.world.Map().terrain;
    if (!terrain.heights || !terrain.attributes)
        return Core::Unexpected(std::string("0x420d60: faltan alturas o atributos para el cuervo"));
    constexpr float CellSize = 100.0f;
    constexpr float Center = 50.0f;
    const double height = World::TerrainHeightAt(
        *terrain.heights, *terrain.attributes, static_cast<float>(actor.cellX) * CellSize + Center,
        static_cast<float>(actor.cellY) * CellSize + Center, resources.camera.fixedHeight);
    actor.darkSpirit =
        CreateDarkSpirit(actor.cellX, actor.cellY, height, resources.cloth.random,
                         {actor.cellXAccesses, actor.cellYAccesses, resources.cloth.openCounterAccesses});
    return {};
}
void ClearTransitionActorsAndItems(std::uint16_t playerKey, const WorldEntryResources& resources)
{
    for (auto& item : resources.world.DrawTail().floorItems)
        item.alive = false;
    KillOtherActors(resources.world, static_cast<std::int32_t>(playerKey));
}
void CreateTransitionArrival(ActorState& actor, const WorldEntryResources& resources)
{
    static_cast<void>(
        resources.world.Map().effects.Create(World::ArrivalEffect, actor.position, actor.angles, actor.light, 0.0f));
}
Core::Expected<void, std::string> CloseTransitionWindows(const WorldEntryResources& resources)
{
    if (resources.world.Runtime().movement.windowsMask != 0)
        return Core::Unexpected(std::string("0x657a90: cerrar ventanas abiertas requiere los servicios de la CL8"));
    constexpr int ClosedWindowAccesses = 2;
    for (int query = 0; query < ClosedWindowAccesses; ++query)
    {
        std::uint8_t depth = 0;
        const Render::Models::LogicalAccess access(depth, resources.cloth.random, 1);
    }
    return {};
}
} // namespace Client::Scenes
