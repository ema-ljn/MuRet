#include "Session/MapLoad.h"
#include "Kernel/HelperTable.h"

#include "Sound/SoundCatalog.h"
#include "Audio/SoundDefinition.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainObjects.h"
#include "Maps/WorldFolder.h"
#include "Maps/WorldLoadIdentity.h"
#include "Maps/MapId.h"
#include "Protocol/Events/Events.h"
#include "Actors/ActorCreation.h"
#include "Kernel/ActorState.h"
#include "Events/EventMapObject.h"
#include "Fauna/FaunaState.h"
#include "Kernel/DrawTailState.h"
#include "Pets/HelperTable.h"
#include "WorldObjects/MapModelEffectServices.h"
#include "PersonalShop/PersonalShopTitles.h"
#include "Kernel/SceneId.h"
#include "Kernel/WorldMap.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/GameContext.h"
#include "Kernel/WorldState.h"
#include "Weather/Leaves.h"
#include "MapScripts/Kalima/KalimaWorld.h"
#include "Particles/Particles.h"
#include "Kernel/WorldObject.h"
#include "WorldObjects/WorldObjects.h"
#include "Terrain/WorldTerrain.h"
#include "Terrain/TerrainState.h"
#include "Assets/FaunaModels.h"
#include "Effects/ModelEffects.h"
#include "Actors/Drawing/CharacterCloth.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Client::Scenes
{
namespace
{
// 0x510f70 recorre hasta 0x69780: 400 registros de 0x438, uno menos que el pool.
constexpr std::size_t KilledActorCount = 400;
using Maps::CastleSiegeMap;
using Maps::CrywolfMap;
using Maps::DeviasMap;
using Maps::DevilSquareMap;
using Maps::FirstBloodCastleMap;
using Maps::FirstChaosCastleMap;
using Maps::LastBloodCastleMap;
using Maps::LastChaosCastleMap;
using Maps::LoginMap;
using Maps::NoriaMap;
constexpr std::uint8_t ChaosCastleAreaReset = 0xff;
// 0x5f831e..0x5f8342: (rand() % 4) · 0,25 en cada una de las 65.536 celdas.
constexpr int GrassSteps = 4;
constexpr float GrassStep = 0.25f;

// Los especiales de 0x616ec0 (el segundo switch, 0x61733e) que crea con 0x606660. El 0 no tiene.
std::vector<Data::Terrain::TerrainObject> SpecialObjectsOf(std::int32_t map)
{
    constexpr std::array<float, 3> NoAngle{};
    if (map == DeviasMap)
    {
        // 0x61744d: 0x201 y 0x202.
        return {Data::Terrain::TerrainObject{0x201, {19100.0f, 1600.0f, 270.0f}, NoAngle, 1.0f},
                Data::Terrain::TerrainObject{0x202, {19100.0f, 1700.0f, 270.0f}, NoAngle, 1.0f}};
    }
    if (map == NoriaMap)
    {
        // 0x6175ea..0x617626: z en 0 y el tercer ángulo en 10; después pide sus cinco efectos.
        return {Data::Terrain::TerrainObject{0x142, {22300.0f, 3000.0f, 0.0f}, {0.0f, 0.0f, 10.0f}, 1.0f}};
    }
    return {};
}

// Las tablas de atributos y luz cruda sólo sobreviven si Crywolf reutiliza las rutas de carga (Q-C181).
void ForgetThePreviousTerrain(Kernel::WorldMap& loaded, Terrain::WorldTerrain& current, bool retainTerrainTables)
{
    loaded.grassOffsets.clear();
    Terrain::WorldTerrain terrain;
    if (retainTerrainTables)
    {
        terrain.attributes = std::move(current.attributes);
        terrain.rawLight = std::move(current.rawLight);
    }
    current = std::move(terrain);
}

std::string DescribeCreationError(WorldObjects::WorldObjectCreationError error)
{
    switch (error)
    {
    case WorldObjects::WorldObjectCreationError::UnsupportedEffect:
        return "un efecto que MuRet no crea";
    case WorldObjects::WorldObjectCreationError::UnsupportedMapHandler:
        return "un caso del mapa sin su estado (el asedio o el rand())";
    case WorldObjects::WorldObjectCreationError::InvalidAngle:
        return "un ángulo fuera de lo que porta ftol";
    }
    return "un caso desconocido";
}

// 0x606660 sobre cada registro, en orden; se detiene en el primero que MuRet no crea.
// DAT_006d5dc8 al crear (0x60685e): el farol y el dueño del dragón solo corren en el login y la selección.
WorldObjects::ObjectScene ObjectSceneOf(Kernel::SceneId scene)
{
    switch (scene)
    {
    case Kernel::SceneId::Login:
        return WorldObjects::ObjectScene::Login;
    case Kernel::SceneId::CharacterSelect:
        return WorldObjects::ObjectScene::CharacterSelect;
    default:
        return WorldObjects::ObjectScene::Other;
    }
}

Core::Expected<void, std::string> CreateTheObjects(std::span<const Data::Terrain::TerrainObject> records,
                                                   const char* source, Kernel::WorldMap& loaded, std::int32_t map,
                                                   const MapLoadContext& context)
{
    Crt::CrtRandom& random = context.random;
    WorldObjects::MapModelEffectServices services(loaded.effects, random);
    // 0x4294b0 lee el byte 0x7239ec en cada objeto del 30 (0x42b480); ningún paquete corre durante la carga.
    const std::uint8_t siegeState = context.world.Runtime().castleFlags.siegeActive ? 1 : 0;
    WorldObjects::WorldObjectCreationContext creation{.map = map,
                                                      .scene = ObjectSceneOf(context.scene),
                                                      .registrations = loaded.registrations,
                                                      .services = &services,
                                                      .random = &random,
                                                      .state7239Ec = &siegeState};
    WorldObjects::WorldObjectCreationResult created = WorldObjects::CreateWorldObjects(records, creation);
    for (Kernel::WorldObject& object : created.objects)
    {
        loaded.objects.push_back(std::move(object));
    }
    if (!created.failure)
    {
        return {};
    }
    const Data::Terrain::TerrainObject& record = records[created.failure->recordIndex];
    return Core::Unexpected("0x606660: el objeto " + std::to_string(created.failure->recordIndex) + " de " + source +
                            " (tipo " + std::to_string(record.type) + ") pide " +
                            DescribeCreationError(created.failure->error));
}

void FillTheGrassOffsets(std::vector<float>& offsets, Crt::CrtRandom& random)
{
    offsets.resize(Data::Terrain::TerrainCellCount);
    for (float& offset : offsets)
    {
        offset = static_cast<float>(random.Next() % GrassSteps) * GrassStep;
    }
}

// 0x42a760, con el mapa 30, después de los especiales de 0x616ec0 y antes de leer los archivos: 0x7239f0 en 0
// (0x42a795), el pedido B9 02 (0x42a7d3..0x42aa52), el lancero 0x612d90(0x4d) en el modelo 0x19f y sus cinco
// objetos con 0x606660 (0x42aa70..0x42ab27), en (x; 11.300; 270), ángulos en 0 y escala 1. El byte 0x7239d8
// (CastleSiegeFlags::castleByte7239d8), que también pone en 0 (0x42a78e), lo escribe 0x42d890 y lo lee 0x42da90 con los
// monstruos 0x11e y 0x11f.
Core::Expected<void, std::string> PrepareTheCastleSiege(Kernel::WorldMap& loaded, std::int32_t map,
                                                        const MapLoadContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    runtime.castleFlags.siegeMusicTime = 0.0f;
    runtime.castleFlags.castleByte7239d8 = 0;
    // 0x42a981: el B9 02 sale solo con el socket del GS abierto (0x6c82a0 + 0xc distinto de −1).
    if (context.requestCastleSiegeState)
        context.requestCastleSiegeState();
    // 0x42aa6b: Monster78 lo carga WorldModels del 30 (Scenes/WorldModels.cpp), con las texturas de los objetos.
    constexpr std::int16_t SpearmanModel = 0x19f;
    constexpr float SpearmanY = 11300.0f;
    constexpr float SpearmanZ = 270.0f;
    constexpr std::array<float, 5> SpearmanX{6500.0f, 7100.0f, 9100.0f, 11800.0f, 12300.0f};
    std::vector<Data::Terrain::TerrainObject> spearmen;
    for (const float x : SpearmanX)
        spearmen.push_back(Data::Terrain::TerrainObject{SpearmanModel, {x, SpearmanY, SpearmanZ}, {}, 1.0f});
    return CreateTheObjects(spearmen, "los lanceros de 0x42a760", loaded, map, context);
}

// El .att y la luz que pide la carga (0x61a6d0..0x61a74d y 0x61a8c0..0x61a910): los del mundo, salvo en el 34 con el
// estado de Crywolf en 1 o 2 (EncTerrain351/352.att y TerrainLight1/2).
struct TerrainFileNames
{
    std::optional<std::int32_t> attributesNumber;
    std::string_view lightName = "TerrainLight";
    bool reusePreviousPaths = false;
};

// Lo que no depende de un mapa de frontera: el terreno, el .obj y sus objetos.
Core::Expected<void, std::string> LoadTheFiles(const MapLoadContext& context, Kernel::WorldMap& loaded,
                                               std::int32_t map, const TerrainFileNames& names)
{
    const auto folderMap = static_cast<std::uint8_t>(map);
    auto& fatalMessage = context.world.Runtime().fatalDataMessage;
    auto terrain = Terrain::LoadWorldTerrainLayout(context.dataRoot, folderMap, context.terrain.current, fatalMessage,
                                                   names.attributesNumber, names.reusePreviousPaths);
    if (!terrain.HasValue())
    {
        return Core::Unexpected(terrain.Error());
    }
    auto records = Terrain::LoadWorldObjects(context.dataRoot, folderMap, &fatalMessage);
    if (!records.HasValue())
    {
        return Core::Unexpected(records.Error());
    }
    loaded.objectRecords = std::move(records).Value();
    if (auto created = CreateTheObjects(loaded.objectRecords, "EncTerrain.obj", loaded, map, context);
        !created.HasValue())
        return created;
    return Terrain::LoadWorldTerrainSurface(context.dataRoot, folderMap, context.terrain.current, fatalMessage,
                                            names.lightName, names.reusePreviousPaths);
}
} // namespace

void KillOtherActors(Kernel::WorldState& world, std::int32_t key)
{
    const std::span<Kernel::ActorState, Kernel::ActorPoolSize> actors = world.Actors();
    for (std::size_t slot = 0; slot < KilledActorCount; ++slot)
    {
        Kernel::ActorState& actor = actors[slot];
        const auto actorKey = static_cast<std::int32_t>(static_cast<std::int16_t>(actor.key));
        if (actor.alive && actorKey != key)
        {
            actor.alive = false;
            // 0x510f99: 0x401040, los huesos con nombre del registro.
            Actors::ForgetNamedBones(world.Runtime(), slot);
            Kernel::TurnOffHelpersOf(world.Helpers(), slot);
        }
        // 0x510fc2..0x510fc3: 0x421d60 también con los muertos y los que conservan su clave, y después
        // 0x414340 (0x510fd2), la capa de los GM.
        world.ActorCloths()[slot] = {};
        actor.darkSpirit.reset();
        Kernel::ReleaseTheLayer(actor);
    }
}

void ReleaseTheMapObjects(Kernel::WorldMap& loaded)
{
    loaded.effects.Clear();
    loaded.objects.clear();
    loaded.objectRecords.clear();
    // 0x60719c..0x6071ac: solo el byte vivo de los 200 registros; los demás campos sobreviven.
    for (Kernel::WorldObjectRegistration& entry : loaded.registrations.entries)
    {
        entry.active = 0;
    }
}

void ClearWorldTransientPools(Kernel::WorldState& world)
{
    world.Fauna().Clear();
    world.Leaves().Clear();
    world.Particles().Clear();
    for (auto& number : world.DrawTail().damage)
        number.alive = false;
    for (auto& joint : world.DrawTail().joints)
        joint.alive = false;
    for (auto& ribbon : world.DrawTail().ribbons)
        ribbon.alive = false;
}

Core::Expected<void, std::string> LoadMap(std::uint8_t argumentMap, const MapLoadContext& context)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    if (runtime.audio != nullptr)
    {
        runtime.audio->ReleaseWorldSounds();
        if (context.faunaModels == nullptr)
            return Core::Unexpected(std::string("0x61a560: no fauna models (0x5769ad8)"));
        context.faunaModels->ResetSoundRegistrationStamps();
    }
    runtime.map = Maps::MapAtWorldLoadStart(argumentMap, runtime.map);
    const std::int32_t map = runtime.map;
    if (runtime.audio != nullptr)
        for (const Audio::SoundDefinition& sound : Sound::WorldSoundsForMap(map))
            runtime.audio->RegisterSound(sound.id, sound.path);
    const std::uint8_t crywolfState = runtime.crywolf.state;
    const bool reusePreviousPaths = map == CrywolfMap && crywolfState > Protocol::Events::CrywolfWarState;
    Kernel::WorldMap& loaded = context.world.Map();
    // Invalidación interna por intento, también si falla antes de alcanzar4377a0. No consume CRT.
    ++loaded.loadRevision;
    ReleaseTheMapObjects(loaded);
    ClearWorldTransientPools(context.world);
    ForgetThePreviousTerrain(loaded, context.terrain.current, reusePreviousPaths);
    if (context.scene != Kernel::SceneId::CharacterSelect)
    {
        // El DWORD 0x57678bc lleva la clave sin signo.
        KillOtherActors(context.world, static_cast<std::int32_t>(context.playerKey));
    }
    // 0x61a5b5..0x61a5bc, también en la selección: 0x451590 deja solo el cartel de la tienda del héroe.
    Social::KeepTheHeroShopTitle(context.world);
    if (map == LoginMap)
    {
        return Core::Unexpected(std::string("0x616ec0: los especiales del mapa 55 (0x6188ae) no están leídos"));
    }
    const std::vector<Data::Terrain::TerrainObject> specials = SpecialObjectsOf(map);
    if (auto created = CreateTheObjects(specials, "los especiales de 0x616ec0", loaded, map, context);
        !created.HasValue())
    {
        return created;
    }
    if (map == CastleSiegeMap)
    {
        if (auto prepared = PrepareTheCastleSiege(loaded, map, context); !prepared.HasValue())
            return prepared;
    }
    FillTheGrassOffsets(loaded.grassOffsets, context.random);
    // 0x61a6d0: 0x42fe50 lee el estado de Crywolf después del .map. Con 0 (el de la .bss, Q-C340) el 34 carga
    // EncTerrain35.att y TerrainLight como los demás; con 1 y 2, que pone el BD 00 (0x42fe80), EncTerrain351/352.att
    // (el mundo · 10 + estado, 0x61a706 y 0x61a710) y TerrainLight1/2 (0x61a8d2 y 0x61a8dd). Con 3 o más, el .att con
    // la ruta del .map y la luz con la de TerrainHeight.bmp (0x63b490 cambia la extensión a OZJ en modo datos).
    TerrainFileNames names;
    if (map == CrywolfMap)
    {
        if (reusePreviousPaths)
        {
            names.reusePreviousPaths = true;
            names.lightName = "TerrainHeight";
        }
        else if (crywolfState != Protocol::Events::CrywolfPeaceState)
        {
            names.attributesNumber = static_cast<std::int32_t>(Maps::WorldFolderNumber(CrywolfMap)) * 10 + crywolfState;
            names.lightName =
                crywolfState == Protocol::Events::CrywolfOccupiedState ? "TerrainLight1" : "TerrainLight2";
        }
    }
    // 61a6ea/61a8af: el estado activo elige312 yTerrainLight2; elMAP yOBJ conservan el mundo31.
    if (map == CastleSiegeMap && context.world.Runtime().castleFlags.siegeActive)
    {
        constexpr std::int32_t WarAttributes = 312;
        names.attributesNumber = WarAttributes;
        names.lightName = "TerrainLight2";
    }
    if (auto files = LoadTheFiles(context, loaded, map, names); !files.HasValue())
    {
        return files;
    }
    // 61a920→4377a0: los errores anteriores conservan723c48; recién aquí4378c0 lo suelta.
    loaded.kalima.reset();
    if (Maps::IsKalimaMap(map))
    {
        try
        {
            loaded.kalima = std::make_unique<MapScripts::Kalima::KalimaWorld>(map);
        }
        catch (const std::bad_alloc&)
        {
            return Core::Unexpected(std::string("0x4377a0: no se pudo reservar el mundo de Kalima"));
        }
    }
    return {};
}

void ArmEventMapObject(std::int32_t map, Kernel::WorldRuntime& runtime)
{
    // 0x447d84..0x447de2: el argumento entre 11 y 17, la vtable 0x6b8c30.
    if (map >= FirstBloodCastleMap && map <= LastBloodCastleMap)
    {
        runtime.eventObject = Events::EventMapObject{.kind = Events::EventMapKind::BloodCastle};
        return;
    }
    // 0x447de7..0x447e1b: el mundo en curso (no el argumento) entre 18 y 23; 0x404d60 pone 0x6c634c en 0xff.
    if (runtime.map >= FirstChaosCastleMap && runtime.map <= LastChaosCastleMap)
    {
        runtime.eventObject = Events::EventMapObject{.kind = Events::EventMapKind::ChaosCastle};
        runtime.chaosCastleArea = ChaosCastleAreaReset;
        return;
    }
    // 0x447e47..0x447e8d: el argumento en 9, la vtable 0x6b8c1c.
    if (map == DevilSquareMap)
        runtime.eventObject = Events::EventMapObject{.kind = Events::EventMapKind::DevilSquare};
}
} // namespace Client::Scenes
