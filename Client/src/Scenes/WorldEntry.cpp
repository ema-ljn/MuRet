#include "Scenes/WorldEntry.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Protocol/Character/CharacterSelectResult.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorActions.h"
#include "Scenes/ActorConstruction.h"
#include "Scenes/ActorEquipment.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/GamePanels.h"
#include "Scenes/IWorldEntryPublisher.h"
#include "Scenes/MapLoad.h"
#include "Scenes/OwnStatHelpers.h"
#include "Scenes/SceneId.h"
#include "Scenes/SelectionState.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "Scenes/WorldTransitionSupport.h"
#include "World/GameCamera.h"
#include "World/ModelEffects.h"
#include "World/TerrainCulling.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace Client::Scenes
{
namespace
{
// 0x4c44b3: el héroe sale de los 400 primeros registros del pool.
constexpr int HeroSlots = 400;
// 0x4c44c2..0x4c44d1: (dirección − 1) · 45, con 0x6b87d4 (1) y 0x6b8848 (45).
constexpr double DirectionOffset = 1.0;
constexpr double DegreesPerDirection = 45.0;
// 0x4c48eb: el contador de la espera en 30.
constexpr std::int32_t EntryWait = WaitDrawThreshold;
// 0x4c49d6..0x4c4a11: los sonidos fuera de Blood Castle (11 a 17) y de Chaos Castle (18 a 23).
constexpr std::int32_t FirstBloodCastleMap = 11;
constexpr std::int32_t LastBloodCastleMap = 17;
constexpr std::int32_t FirstChaosCastleMap = 18;
constexpr std::int32_t LastChaosCastleMap = 23;
constexpr std::int32_t OutsideBloodCastleSound = 0x8b;
constexpr std::int32_t FirstOutsideChaosCastleSound = 0x99;
constexpr std::int32_t SecondOutsideChaosCastleSound = 0x9e;

ActorState& HeroOf(const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    return world.Actors()[static_cast<std::size_t>(world.Runtime().heroSlot)];
}

ActorStopContext StopContextOf(const WorldEntryResources& resources, const Render::Models::Skeleton& player)
{
    return ActorStopContext{.runtime = resources.world.Runtime(),
                            .scene = SceneId::Game,
                            .playerSkeleton = player,
                            .items = resources.items,
                            .random = resources.cloth.random,
                            .terrainWalls = resources.world.Map().terrain.attributes
                                                ? &resources.world.Map().terrain.attributes->cells
                                                : nullptr};
}

// 0x4c40cb..0x4c41c4: abre el registro propio, copia los stats y lo cierra (el primer cierre en línea).
void ApplyTheStats(const Protocol::Character::CharacterSelectResult& reply, const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    const Render::Models::LogicalAccess access(world.Runtime().ownRecordDepth, resources.cloth.random,
                                               Render::Models::OwnRecordCloseRandoms);
    world.OwnCharacter().ApplyStats(reply, resources.log);
}

// 0x4c41cf..0x4c44a8: el mundo en curso, 0x61a560 y 0x447d70. La respuesta llega en la escena 5. El
// C1 04 BD 00 del mapa 34 (0x4c421a) no se alcanza: LoadMap corta antes en ese mapa.
Core::Expected<void, std::string> LoadTheWorld(std::uint8_t map, std::uint16_t playerKey,
                                               const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    world.Runtime().map = map;
    const auto loaded = LoadMap(map, MapLoadContext{.dataRoot = resources.dataRoot,
                                                    .world = world,
                                                    .scene = SceneId::Game,
                                                    .playerKey = playerKey,
                                                    .random = resources.cloth.random,
                                                    .requestCastleSiegeState = resources.requestCastleSiegeState});
    if (!loaded.HasValue())
    {
        return loaded;
    }
    resources.selection.BorrowTerrain(world.Runtime().map, &world.Map().terrain);
    resources.cameraInput.map = static_cast<std::uint8_t>(world.Runtime().map);
    ArmEventMapObject(world.Runtime().map, world.Runtime());
    return {};
}

// 0x4c44ad..0x4c4516: el lugar del héroe, su construcción y la clave del jugador. Una segunda respuesta
// sortea otro lugar sin mirar el del héroe anterior, que tiene la misma clave y sobrevive a 0x510f70 en
// el suyo (Q-C196); si cae en el mismo, 0x511290 lo reconstruye ahí.
Core::Expected<void, std::string> BuildTheHero(const Protocol::Character::CharacterSelectResult& reply,
                                               std::uint16_t playerKey, const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    world.Runtime().heroSlot = resources.cloth.random.Next() % HeroSlots;
    const auto angle =
        static_cast<float>((static_cast<double>(reply.direction) - DirectionOffset) * DegreesPerDirection);
    ActorState& hero = HeroOf(resources);
    const ActorConstructionContext construction{.terrain = world.Map().terrain,
                                                .map = world.Runtime().map,
                                                .scene = World::CullingScene::Game,
                                                .fixedHeight = resources.camera.fixedHeight};
    const auto built = ConstructActor(hero, Data::Models::PlayerSkeletonModel, {reply.x, reply.y, angle}, construction,
                                      resources.cloth);
    if (!built.HasValue())
    {
        return built;
    }
    hero.key = playerKey;
    return {};
}

// 0x4c4522..0x4c470b: la clase del registro propio y la piel en 0 (el segundo cierre en línea), y
// después el PK, el CtlCode y +0x88.
void CopyTheIdentity(const Protocol::Character::CharacterSelectResult& reply, const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    ActorState& hero = HeroOf(resources);
    {
        const Render::Models::LogicalAccess access(world.Runtime().ownRecordDepth, resources.cloth.random,
                                                   Render::Models::OwnRecordCloseRandoms);
        hero.characterClass = world.OwnCharacter().characterClass;
        hero.skin = 0;
    }
    hero.pk = reply.pk;
    hero.controlCode = reply.controlCode;
    hero.byte88 = 1;
}

// 0x4c4712: 0x512a30 con el héroe publicado de antes (0x743a170 cambia recién en 0x4c4725): el lugar 0 de
// WinMain en la primera respuesta y el héroe anterior en una segunda. 0x550d10 le pone a ese el +0x358
// (0x40d010, Q-C197). Si el lugar del héroe es el del publicado, 0x505820 corre ahí; si no, 0x550990 lee
// lo que dejó el último dibujo de los personajes: el de la selección o, después de un frame del juego, uno
// que MuRet no tiene (corta en 0x550990, P-C40).
Core::Expected<void, std::string> DressTheHero(const WorldEntryResources& resources,
                                               const Render::Models::Skeleton& player, WorldEntryResult& result)
{
    WorldState& world = resources.world;
    const ActorEquipmentContext equipment{.own = world.OwnCharacter(),
                                          .ownRecordAccesses = world.Runtime().ownRecordDepth,
                                          .publishedHero = world.Actors()[world.Runtime().hero],
                                          .fullSetScan = world.Runtime().fullSetScan,
                                          .stop = StopContextOf(resources, player),
                                          .itemSetTables = resources.itemSetTables,
                                          .itemSets = &world.ItemSets(),
                                          .held = HeldItemOf(world.Runtime())};
    const auto dressed = ApplyActorEquipment(HeroOf(resources), equipment);
    if (!dressed.HasValue())
    {
        return Core::Unexpected(dressed.Error());
    }
    if (dressed.Value().sound)
    {
        result.sounds.push_back(*dressed.Value().sound);
    }
    return {};
}

// 0x4c4725..0x4c481e: el héroe publicado, también para la cámara; el nombre y el equipo vaciado, con
// el tercer cierre en línea.
void PublishTheHero(const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    world.Runtime().hero = static_cast<std::size_t>(world.Runtime().heroSlot);
    ActorState& hero = HeroOf(resources);
    resources.cameraInput.hero = hero.position;
    if (const auto& attributes = world.Map().terrain.attributes)
    {
        const std::size_t cell = Data::Terrain::CellIndex(hero.cellX, hero.cellY);
        resources.cameraInput.cellAttribute = static_cast<std::uint8_t>(attributes->cells[cell]);
    }
    const Render::Models::LogicalAccess access(world.Runtime().ownRecordDepth, resources.cloth.random,
                                               Render::Models::OwnRecordCloseRandoms);
    OwnCharacterRecord& own = world.OwnCharacter();
    std::copy_n(own.name.begin(), hero.name.size(), hero.name.begin());
    own.ClearEquipmentForEntry();
}

// 0x4c4823..0x4c484b y 0x4c4959..0x4c4971: 0x518cf0(0x61f, posición, ángulos, luz, 0, héroe, −1, …).
// Con la tabla llena no crea nada, como el original.
void CreateTheArrival(const WorldEntryResources& resources)
{
    const ActorState& hero = HeroOf(resources);
    static_cast<void>(
        resources.world.Map().effects.Create(World::ArrivalEffect, hero.position, hero.angles, hero.light, 0.0f));
}

// 0x4c486d..0x4c4935: abre el contador, lo pone en 30 y lo cierra (0x412890).
void StartTheWait(const WorldEntryResources& resources)
{
    WorldRuntime& runtime = resources.world.Runtime();
    const Render::Models::LogicalAccess access(runtime.warpDepth, resources.cloth.random,
                                               Render::Models::SmallLogicalCloseRandoms);
    runtime.warpCounter = EntryWait;
}

// 0x4c4987: 0x657a90 cierra los paneles abiertos y el diálogo de los paneles; con todo cerrado, los 2 rand() de las
// consultas del comercio y de la tienda de NPC.
Core::Expected<void, std::string> CloseTheWindows(const WorldEntryResources& resources)
{
    GamePanels panels(resources.world, resources.cloth.random);
    panels.LendTheWindowClose(resources.sendWindowClose);
    return panels.Registry().CloseAll();
}

// 0x4c49ce..0x4c4a16: 0x41ee00(id, 1) fuera de los mapas de evento.
void AskForTheSounds(std::int32_t map, WorldEntryResult& result)
{
    if (map < FirstBloodCastleMap || map > LastBloodCastleMap)
    {
        result.sounds.push_back(OutsideBloodCastleSound);
    }
    if (map < FirstChaosCastleMap || map > LastChaosCastleMap)
    {
        result.sounds.push_back(FirstOutsideChaosCastleSound);
        result.sounds.push_back(SecondOutsideChaosCastleSound);
    }
}

// 0x4c4981..0x4c49c9: el alpha en 0, las ventanas, los elegidos, el Movement y la acción de parado.
Core::Expected<void, std::string> StopTheHero(const WorldEntryResources& resources,
                                              const Render::Models::Skeleton& player, WorldEntryResult& result)
{
    ActorState& hero = HeroOf(resources);
    hero.alpha = 0.0f;
    if (auto closed = CloseTheWindows(resources); !closed.HasValue())
        return closed;
    WorldRuntime& runtime = resources.world.Runtime();
    runtime.byte6c8e4d = 0xff;
    runtime.selectedTargets.fill(-1);
    runtime.panels.repairMode = 0;
    runtime.field78abb10 = 0;
    hero.movement = 0;
    const auto stopped = SetActorStop(hero, StopContextOf(resources, player));
    if (!stopped.HasValue())
    {
        return Core::Unexpected(stopped.Error());
    }
    if (stopped.Value().sound)
    {
        result.sounds.push_back(*stopped.Value().sound);
    }
    return {};
}

Core::Expected<const Render::Models::Skeleton*, std::string> PlayerSkeletonOf(const WorldEntryResources& resources)
{
    const Render::Models::Skeleton* player = resources.models.SkeletonOf(Data::Models::PlayerSkeletonModel);
    if (player == nullptr)
    {
        return Core::Unexpected(std::string("la entrada al mundo necesita Player.bmd (0x20a) para 0x4f2250"));
    }
    return player;
}
} // namespace

Core::Expected<WorldEntryResult, std::string> ApplyWorldEntry(const Protocol::Character::CharacterSelectResult& reply,
                                                              std::uint16_t playerKey,
                                                              const WorldEntryResources& resources,
                                                              IWorldEntryPublisher& publisher)
{
    const auto player = PlayerSkeletonOf(resources);
    if (!player.HasValue())
    {
        return Core::Unexpected(player.Error());
    }
    // 0x4c405a: el latch izquierdo en cero antes de los stats.
    resources.world.Runtime().movement.pendingClick = false;
    ApplyTheStats(reply, resources);
    if (auto loaded = LoadTheWorld(reply.map, playerKey, resources); !loaded.HasValue())
    {
        return Core::Unexpected(loaded.Error());
    }
    if (auto built = BuildTheHero(reply, playerKey, resources); !built.HasValue())
    {
        return Core::Unexpected(built.Error());
    }
    CopyTheIdentity(reply, resources);
    WorldEntryResult result;
    if (auto dressed = DressTheHero(resources, *player.Value(), result); !dressed.HasValue())
    {
        return Core::Unexpected(dressed.Error());
    }
    return SettleWorldEntry(std::move(result), resources, publisher);
}

Core::Expected<WorldEntryResult, std::string>
SettleWorldEntry(WorldEntryResult result, const WorldEntryResources& resources, IWorldEntryPublisher& publisher)
{
    const auto player = PlayerSkeletonOf(resources);
    if (!player.HasValue())
    {
        return Core::Unexpected(player.Error());
    }
    PublishTheHero(resources);
    HeroOf(resources).byte1e3 = 0;
    CreateTheArrival(resources);
    // 0x4c4854: el 0x3d. Después, 0x77f3b67 = 0 y 0x553780(1, 0), el IME: MuRet no tiene ni uno ni otro.
    publisher.PublishWorldEntry();
    StartTheWait(resources);
    // 0x77f3b10 = 0 y 0x6c8dcc = 6 (su valor de la .data), sin quién los lea en MuRet.
    // 0x4c4954: 0x420d60 con el héroe publicado, el cuervo si lleva el 0x1c17 en la mano izquierda.
    if (auto raven = RecreateTransitionRaven(HeroOf(resources), resources); !raven.HasValue())
    {
        return Core::Unexpected(raven.Error());
    }
    CreateTheArrival(resources);
    if (auto stopped = StopTheHero(resources, *player.Value(), result); !stopped.HasValue())
    {
        return Core::Unexpected(stopped.Error());
    }
    AskForTheSounds(resources.world.Runtime().map, result);
    // 0x4c4a19..0x4c4b0e, el chequeo de ResourceGuard, no hace nada (Q-C147). 0x65dc30 y el renglón de
    // bienvenida con el nombre del mapa (0x555190) son del área de mensajes (CL9).
    return result;
}
} // namespace Client::Scenes
