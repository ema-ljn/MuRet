#include "Scenes/MapLoad.h"

#include "Core/Expected.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainObjects.h"
#include "Data/WorldLoadIdentity.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorState.h"
#include "Scenes/HelperTable.h"
#include "Scenes/MapModelEffectServices.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/Particles.h"
#include "World/WorldObjectRegistrations.h"
#include "World/WorldObjects.h"
#include "World/WorldTerrain.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
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
constexpr std::int32_t FirstKalimaMap = 24;
constexpr std::int32_t LastKalimaMap = 29;
constexpr std::int32_t SeventhKalimaMap = 36;
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
Core::Expected<void, std::string> CreateTheObjects(std::span<const Data::Terrain::TerrainObject> records,
                                                   const char* source, WorldMap& loaded, std::int32_t map,
                                                   Render::Models::CrtRandom& random)
{
    MapModelEffectServices services(loaded.effects, random);
    World::WorldObjectCreationContext creation{.map = map,
                                               .scene = World::ObjectScene::Other,
                                               .registrations = loaded.registrations,
                                               .services = &services,
                                               .random = &random};
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

bool IsKalima(std::int32_t map)
{
    return (map >= FirstKalimaMap && map <= LastKalimaMap) || map == SeventhKalimaMap;
}

// Lo que no depende de un mapa de frontera: el terreno, el .obj y sus objetos.
Core::Expected<void, std::string> LoadTheFiles(const MapLoadContext& context, WorldMap& loaded, std::int32_t map)
{
    const auto folderMap = static_cast<std::uint8_t>(map);
    auto terrain = World::LoadWorldTerrain(context.dataRoot, folderMap);
    if (!terrain.HasValue())
    {
        return Core::Unexpected(terrain.Error());
    }
    loaded.terrain = std::move(terrain).Value();
    auto records = World::LoadWorldObjects(context.dataRoot, folderMap);
    if (!records.HasValue())
    {
        return Core::Unexpected(records.Error());
    }
    loaded.objectRecords = std::move(records).Value();
    return CreateTheObjects(loaded.objectRecords, "EncTerrain.obj", loaded, map, context.random);
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
            TurnOffHelpersOf(world.Helpers(), slot);
        }
        // 0x510fc2..0x510fc3: 0x421d60 también con los muertos y los que conservan su clave.
        actor.darkSpirit.reset();
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

Core::Expected<void, std::string> LoadMap(std::uint8_t argumentMap, const MapLoadContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    runtime.map = Data::MapAtWorldLoadStart(argumentMap, runtime.map);
    const std::int32_t map = runtime.map;
    WorldMap& loaded = context.world.Map();
    ReleaseTheMapObjects(loaded);
    context.world.Particles().Clear();
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
    if (auto created = CreateTheObjects(specials, "los especiales de 0x616ec0", loaded, map, context.random);
        !created.HasValue())
    {
        return created;
    }
    if (map == CastleSiegeMap)
    {
        return Core::Unexpected(std::string("0x61a560: el mapa 30 pasa por 0x42a760 y elige el .att con 0x4294b0"));
    }
    FillTheGrassOffsets(loaded.grassOffsets, context.random);
    if (map == CrywolfMap)
    {
        return Core::Unexpected(std::string("0x61a560: el .att del mapa 34 depende del estado de Crywolf (0x42fe50)"));
    }
    if (auto files = LoadTheFiles(context, loaded, map); !files.HasValue())
    {
        return files;
    }
    if (IsKalima(map))
    {
        return Core::Unexpected(std::string("0x4377a0: el mundo especial de Kalima (0x723c48) no está portado"));
    }
    return {};
}

Core::Expected<void, std::string> CheckEventMap(std::int32_t map)
{
    const bool bloodCastle = map >= FirstBloodCastleMap && map <= LastBloodCastleMap;
    const bool chaosCastle = map >= FirstChaosCastleMap && map <= LastChaosCastleMap;
    if (map == DevilSquareMap || bloodCastle || chaosCastle)
    {
        return Core::Unexpected("0x447d70: el objeto de evento del mapa " + std::to_string(map) +
                                " (0x447eb0 y operator new(0x134)) no está portado");
    }
    return {};
}
} // namespace Client::Scenes
