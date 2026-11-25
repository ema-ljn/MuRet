#include "Scenes/WorldTransitionSupport.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Text/TextTable.h"
#include "Maps/MapId.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/IModelLibrary.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorActions.h"
#include "Scenes/ActorConstruction.h"
#include "Scenes/ActorEquipment.h"
#include "Scenes/ActorState.h"
#include "Scenes/CombatActionSpeeds.h"
#include "Scenes/DarkSpirit.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/GameMessages.h"
#include "Scenes/GamePanels.h"
#include "Scenes/CastleWindows.h"
#include "Scenes/MapLoad.h"
#include "Scenes/MapNames.h"
#include "Scenes/MonsterCreation.h"
#include "Scenes/OwnStatHelpers.h"
#include "Scenes/PersonalShopTitles.h"
#include "Scenes/SceneId.h"
#include "Scenes/SelectionState.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/MessageArea.h"
#include "World/GameCamera.h"
#include "World/ModelEffects.h"
#include "World/TerrainCulling.h"
#include "World/TerrainHeight.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace Client::Scenes
{
ActorState& PublishedHero(const WorldEntryResources& resources)
{
    return resources.world.Actors()[resources.world.Runtime().hero];
}
Core::Expected<ActorStopContext, std::string> TransitionStopContext(const ActorState& actor,
                                                                    const WorldEntryResources& resources)
{
    const auto* player = resources.models.SkeletonOf(Data::Models::PlayerSkeletonModel);
    const bool isPlayer = actor.type == Data::Models::PlayerSkeletonModel;
    if (player == nullptr && isPlayer)
        return Core::Unexpected(std::string("0x4f2250: falta Player.bmd en la transición"));
    static const Render::Models::Skeleton EmptySkeleton;
    const Render::Models::Skeleton* playerSkeleton = player != nullptr ? player : &EmptySkeleton;
    const Render::Models::Skeleton* actorSkeleton = isPlayer ? nullptr : MonsterSkeletonOf(resources, actor.type);
    return ActorStopContext{.runtime = resources.world.Runtime(),
                            .scene = SceneId::Game,
                            .playerSkeleton = *playerSkeleton,
                            .items = resources.items,
                            .random = resources.cloth.random,
                            .terrainWalls = resources.world.Map().terrain.attributes
                                                ? &resources.world.Map().terrain.attributes->cells
                                                : nullptr,
                            .actorSkeleton = actorSkeleton,
                            .modelSounds = resources.world.FaunaResources().SoundActions(actor.type)};
}
Core::Expected<void, std::string> StopTransitionHero(ActorState& actor, const WorldEntryResources& resources)
{
    actor.movement = 0;
    const auto stop = TransitionStopContext(actor, resources);
    if (!stop.HasValue())
        return Core::Unexpected(stop.Error());
    const auto stopped = SetActorStop(actor, stop.Value());
    if (!stopped.HasValue())
        return Core::Unexpected(stopped.Error());
    return {};
}
Core::Expected<void, std::string> DressTransitionHero(ActorState& actor, const WorldEntryResources& resources)
{
    auto& world = resources.world;
    const auto stop = TransitionStopContext(actor, resources);
    if (!stop.HasValue())
        return Core::Unexpected(stop.Error());
    const ActorEquipmentContext context{
        world.OwnCharacter(), world.Runtime().ownRecordDepth, PublishedHero(resources), world.Runtime().fullSetScan,
        stop.Value(),         resources.itemSetTables,        &world.ItemSets(),        HeldItemOf(world.Runtime())};
    const auto dressed = ApplyActorEquipment(actor, context);
    if (!dressed.HasValue())
        return Core::Unexpected(dressed.Error());
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
namespace
{
// 0x420d1a..0x420d22: 0x420cf0 no crea el cuervo en los mapas 18 a 23 (Chaos Castle); 0x420d60 no mira el mapa.
using Maps::FirstChaosCastleMap;
using Maps::LastChaosCastleMap;

// 0x4167b0 desde 0x420d60 o 0x420cf0: el cuervo nuevo en +0x430, en el centro de la celda del dueño y a la
// altura del terreno ahí (0x5f93c0).
Core::Expected<void, std::string> CreateTheRaven(ActorState& actor, const WorldEntryResources& resources,
                                                 std::string_view where)
{
    const auto& terrain = resources.world.Map().terrain;
    if (!terrain.heights || !terrain.attributes)
        return Core::Unexpected(std::format("{}: faltan alturas o atributos para el cuervo", where));
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
} // namespace

Core::Expected<void, std::string> RecreateTransitionRaven(ActorState& actor, const WorldEntryResources& resources)
{
    constexpr std::uint16_t Raven = 0x1c17;
    if (actor.hands[1].model != Raven)
        return {};
    actor.darkSpirit.reset();
    return CreateTheRaven(actor, resources, "0x420d60");
}
Core::Expected<void, std::string> ReplaceTheRaven(ActorState& actor, const WorldEntryResources& resources)
{
    actor.darkSpirit.reset();
    const std::int32_t map = resources.world.Runtime().map;
    if (map >= FirstChaosCastleMap && map <= LastChaosCastleMap)
        return {};
    return CreateTheRaven(actor, resources, "0x420cf0");
}
void ClearTransitionActorsAndItems(std::uint16_t playerKey, const WorldEntryResources& resources)
{
    for (auto& item : resources.world.DrawTail().floorItems)
        item.alive = false;
    KillOtherActors(resources.world, static_cast<std::int32_t>(playerKey));
    // 0x4c4e86..0x4c4e8d y 0x4c77eb..0x4c77f2: 0x451590 deja solo el cartel de la tienda del héroe.
    KeepTheHeroShopTitle(resources.world);
}
void CreateTransitionArrival(ActorState& actor, const WorldEntryResources& resources)
{
    static_cast<void>(resources.world.Map().effects.Create(World::ModelEffectArguments{.type = World::ArrivalEffect,
                                                                                       .position = actor.position,
                                                                                       .angles = actor.angles,
                                                                                       .light = actor.light,
                                                                                       .owner = &actor,
                                                                                       .key = 0xffff,
                                                                                       .scale = 0.0f}));
}
Core::Expected<void, std::string> StartTheTeleportAction(ActorState& actor, const WorldEntryResources& resources)
{
    RefreshCombatActionSpeeds(resources);
    const auto stop = TransitionStopContext(actor, resources);
    if (!stop.HasValue())
        return Core::Unexpected(stop.Error());
    constexpr std::uint8_t TeleportAction = 0x93;
    if (actor.type == Data::Models::PlayerSkeletonModel)
        Render::Models::SetAction(actor.animation, stop.Value().playerSkeleton, TeleportAction);
    else if (stop.Value().actorSkeleton != nullptr)
        Render::Models::SetAction(actor.animation, *stop.Value().actorSkeleton, TeleportAction);
    actor.animation.frame = 5.0f;
    actor.byte80 = 3;
    actor.alphaTarget = 1.0f;
    static_cast<void>(
        resources.world.Map().effects.Create(World::TeleportEffect, actor.position, actor.angles, actor.light, 0.0f));
    constexpr std::int32_t TeleportSound = 0x76;
    resources.world.Runtime().RequestSound(TeleportSound);
    return {};
}
Core::Expected<void, std::string> AddTheWelcomeLine(const WorldEntryResources& resources)
{
    constexpr std::size_t WelcomeText = 484;
    if (resources.texts == nullptr)
        return Core::Unexpected(std::string("0x4c4b38 y 0x4c7c26: falta Text.bmd para el renglón de bienvenida"));
    if (resources.textMeasure == nullptr)
        return Core::Unexpected(std::string("0x4c4b60 y 0x4c7c4e: falta el medidor para el renglón de bienvenida"));
    WorldRuntime& runtime = resources.world.Runtime();
    const auto name = resources.texts->Text(MapNameText(runtime.map));
    const auto welcome = resources.texts->Text(WelcomeText);
    if (!name || !welcome)
        return Core::Unexpected(
            std::string("0x4c4b38 y 0x4c7c26: Text.bmd no ofrece el nombre del mapa o el 484 de la bienvenida"));
    std::string line(*name);
    line += *welcome;
    const Core::PixelSize screen = resources.screen ? resources.screen() : Core::PixelSize{640, 480};
    return AddGameMessage(runtime, *resources.textMeasure, screen, {}, line, UI::SystemMessage);
}

Core::Expected<void, std::string> CloseTransitionWindows(const WorldEntryResources& resources)
{
    GamePanels panels(resources.world, resources.cloth.random);
    LendTheCastlePanelServices(panels, resources);
    return panels.Registry().CloseAll();
}
} // namespace Client::Scenes
