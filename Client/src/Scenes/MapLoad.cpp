#include "Scenes/MapLoad.h"

#include "Core/Expected.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainObjects.h"
#include "Data/WorldFolder.h"
#include "Data/WorldLoadIdentity.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorCreation.h"
#include "Scenes/ActorState.h"
#include "Scenes/EventMapObject.h"
#include "Scenes/FaunaState.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/HelperTable.h"
#include "Scenes/MapModelEffectServices.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/Leaves.h"
#include "World/KalimaWorld.h"
#include "World/Particles.h"
#include "World/WorldObjectRegistrations.h"
#include "World/WorldObjects.h"
#include "World/WorldTerrain.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
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
constexpr std::int32_t DeviasMap = 2;
constexpr std::int32_t NoriaMap = 3;
constexpr std::int32_t CastleSiegeMap = 30;
constexpr std::int32_t CrywolfMap = 34;
constexpr std::int32_t LoginMap = 55;
constexpr std::int32_t DevilSquareMap = 9;
constexpr std::int32_t FirstBloodCastleMap = 11;
constexpr std::int32_t LastBloodCastleMap = 17;
constexpr std::int32_t FirstChaosCastleMap = 18;
constexpr std::int32_t LastChaosCastleMap = 23;
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

// Lo que la carga vuelve a llenar: el original lo pisa al cargar, y MuRet lo vacía antes para que una
// carga que corta no deje el del mapa anterior.
void ForgetThePreviousTerrain(WorldMap& loaded)
{
    loaded.grassOffsets.clear();
    loaded.terrain = World::WorldTerrain{};
}

std::string DescribeCreationError(World::WorldObjectCreationError error)
{
    switch (error)
    {
    case World::WorldObjectCreationError::UnsupportedEffect:
        return "un efecto que MuRet no crea";
    case World::WorldObjectCreationError::UnsupportedMapHandler:
        return "un caso del mapa sin su estado (el asedio o el rand())";
    case World::WorldObjectCreationError::InvalidAngle:
        return "un ángulo fuera de lo que porta ftol";
    }
    return "un caso desconocido";
}

// 0x606660 sobre cada registro, en orden; se detiene en el primero que MuRet no crea.
// DAT_006d5dc8 al crear (0x60685e): el farol y el dueño del dragón solo corren en el login y la selección.
World::ObjectScene ObjectSceneOf(SceneId scene)
{
    switch (scene)
    {
    case SceneId::Login:
        return World::ObjectScene::Login;
    case SceneId::CharacterSelect:
        return World::ObjectScene::CharacterSelect;
    default:
        return World::ObjectScene::Other;
    }
}

Core::Expected<void, std::string> CreateTheObjects(std::span<const Data::Terrain::TerrainObject> records,
                                                   const char* source, WorldMap& loaded, std::int32_t map,
                                                   const MapLoadContext& context)
{
    Render::Models::CrtRandom& random = context.random;
    MapModelEffectServices services(loaded.effects, random);
    // 0x4294b0 lee el byte 0x7239ec en cada objeto del 30 (0x42b480); ningún paquete corre durante la carga.
    const std::uint8_t siegeState = context.world.Runtime().siegeActive ? 1 : 0;
    World::WorldObjectCreationContext creation{.map = map,
                                               .scene = ObjectSceneOf(context.scene),
                                               .registrations = loaded.registrations,
                                               .services = &services,
                                               .random = &random,
                                               .state7239Ec = &siegeState};
    World::WorldObjectCreationResult created = World::CreateWorldObjects(records, creation);
    for (World::WorldObject& object : created.objects)
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

void FillTheGrassOffsets(std::vector<float>& offsets, Render::Models::CrtRandom& random)
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
// (WorldRuntime::castleByte7239d8), que también pone en 0 (0x42a78e), lo escribe 0x42d890 y lo lee 0x42da90 con los
// monstruos 0x11e y 0x11f.
Core::Expected<void, std::string> PrepareTheCastleSiege(WorldMap& loaded, std::int32_t map,
                                                        const MapLoadContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    runtime.siegeMusicTime = 0.0f;
    runtime.castleByte7239d8 = 0;
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
};

// Lo que no depende de un mapa de frontera: el terreno, el .obj y sus objetos.
Core::Expected<void, std::string> LoadTheFiles(const MapLoadContext& context, WorldMap& loaded, std::int32_t map,
                                               const TerrainFileNames& names)
{
    const auto folderMap = static_cast<std::uint8_t>(map);
    auto& fatalMessage = context.world.Runtime().fatalDataMessage;
    auto terrain = World::LoadWorldTerrainLayout(context.dataRoot, folderMap, loaded.terrain, fatalMessage,
                                                 names.attributesNumber);
    if (!terrain.HasValue())
    {
        return Core::Unexpected(terrain.Error());
    }
    auto records = World::LoadWorldObjects(context.dataRoot, folderMap, &fatalMessage);
    if (!records.HasValue())
    {
        return Core::Unexpected(records.Error());
    }
    loaded.objectRecords = std::move(records).Value();
    if (auto created = CreateTheObjects(loaded.objectRecords, "EncTerrain.obj", loaded, map, context);
        !created.HasValue())
        return created;
    return World::LoadWorldTerrainSurface(context.dataRoot, folderMap, loaded.terrain, fatalMessage, names.lightName);
}
} // namespace

void KillOtherActors(WorldState& world, std::int32_t key)
{
    const std::span<ActorState, ActorPoolSize> actors = world.Actors();
    for (std::size_t slot = 0; slot < KilledActorCount; ++slot)
    {
        ActorState& actor = actors[slot];
        const auto actorKey = static_cast<std::int32_t>(static_cast<std::int16_t>(actor.key));
        if (actor.alive && actorKey != key)
        {
            actor.alive = false;
            // 0x510f99: 0x401040, los huesos con nombre del registro.
            ForgetNamedBones(world.Runtime(), slot);
            TurnOffHelpersOf(world.Helpers(), slot);
        }
        // 0x510fc2..0x510fc3: 0x421d60 también con los muertos y los que conservan su clave, y después
        // 0x414340 (0x510fd2), la capa de los GM.
        actor.darkSpirit.reset();
        ReleaseTheLayer(actor);
    }
}

void ReleaseTheMapObjects(WorldMap& loaded)
{
    loaded.effects.Clear();
    loaded.objects.clear();
    loaded.objectRecords.clear();
    // 0x60719c..0x6071ac: solo el byte vivo de los 200 registros; los demás campos sobreviven.
    for (World::WorldObjectRegistration& entry : loaded.registrations.entries)
    {
        entry.active = 0;
    }
}

void ClearWorldTransientPools(WorldState& world)
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
    WorldRuntime& runtime = context.world.Runtime();
    runtime.map = Data::MapAtWorldLoadStart(argumentMap, runtime.map);
    const std::int32_t map = runtime.map;
    WorldMap& loaded = context.world.Map();
    // Invalidación interna por intento, también si falla antes de alcanzar4377a0. No consume CRT.
    ++loaded.loadRevision;
    ReleaseTheMapObjects(loaded);
    ClearWorldTransientPools(context.world);
    ForgetThePreviousTerrain(loaded);
    if (context.scene != SceneId::CharacterSelect)
    {
        // El DWORD 0x57678bc lleva la clave sin signo.
        KillOtherActors(context.world, static_cast<std::int32_t>(context.playerKey));
    }
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
    // la ruta del .map y la luz con la de TerrainHeight.bmp, que sombrea la luz anterior con las alturas nuevas
    // (Q-C181): MuRet no guarda la luz sin sombrear.
    TerrainFileNames names;
    if (map == CrywolfMap)
    {
        const std::uint8_t state = context.world.Runtime().crywolf.state;
        if (state > 2)
            return Core::Unexpected(std::format("0x61a74d: el .att y la luz del mapa 34 con el estado de Crywolf {} "
                                                "son los archivos anteriores (Q-C181)",
                                                static_cast<int>(state)));
        if (state != 0)
        {
            names.attributesNumber = static_cast<std::int32_t>(Data::WorldFolderNumber(CrywolfMap)) * 10 + state;
            names.lightName = state == 1 ? "TerrainLight1" : "TerrainLight2";
        }
    }
    // 0x61a6e1 y 0x61a8a8: en el 30, 0x4294b0 elige el .att y la luz. Con 0x7239ec en 0, el de siempre
    // (EncTerrain31.att y TerrainLight.jpg); prendido, que solo lo dejan los paquetes de 0x4294c0 (CL9.5), no.
    if (map == CastleSiegeMap && context.world.Runtime().siegeActive)
    {
        return Core::Unexpected(std::string("0x61a6ea: el .att 312 y TerrainLight2 del asedio (0x7239ec) no están "
                                            "portados"));
    }
    if (auto files = LoadTheFiles(context, loaded, map, names); !files.HasValue())
    {
        return files;
    }
    // 61a920→4377a0: los errores anteriores conservan723c48; recién aquí4378c0 lo suelta.
    loaded.kalima.reset();
    if (World::IsKalimaMap(map))
    {
        try
        {
            loaded.kalima = std::make_unique<World::KalimaWorld>(map);
        }
        catch (const std::bad_alloc&)
        {
            return Core::Unexpected(std::string("0x4377a0: no se pudo reservar el mundo de Kalima"));
        }
    }
    return {};
}

void ArmEventMapObject(std::int32_t map, WorldRuntime& runtime)
{
    // 0x447d84..0x447de2: el argumento entre 11 y 17, la vtable 0x6b8c30.
    if (map >= FirstBloodCastleMap && map <= LastBloodCastleMap)
    {
        runtime.eventObject = EventMapObject{.kind = EventMapKind::BloodCastle};
        return;
    }
    // 0x447de7..0x447e1b: el mundo en curso (no el argumento) entre 18 y 23; 0x404d60 pone 0x6c634c en 0xff.
    if (runtime.map >= FirstChaosCastleMap && runtime.map <= LastChaosCastleMap)
    {
        runtime.eventObject = EventMapObject{.kind = EventMapKind::ChaosCastle};
        runtime.chaosCastleArea = ChaosCastleAreaReset;
        return;
    }
    // 0x447e47..0x447e8d: el argumento en 9, la vtable 0x6b8c1c.
    if (map == DevilSquareMap)
        runtime.eventObject = EventMapObject{.kind = EventMapKind::DevilSquare};
}
} // namespace Client::Scenes
