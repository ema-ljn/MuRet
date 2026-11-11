#include "Scenes/MapLoad.h"

#include "Core/Expected.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Render/Models/Cloth.h"
#include "Scenes/DarkHorse.h"
#include "Scenes/HelperTable.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldState.h"
#include "World/GroundMarks.h"
#include "World/WorldObjectRegistrations.h"
#include "World/WorldTerrain.h"

#include "support/GoldenDir.h"
#include "support/PlayerActors.h"
#include "support/TemporaryFolder.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::CheckEventMap;
using Client::Scenes::LoadMap;
using Client::Scenes::MapLoadContext;
using Client::Scenes::SceneId;
using Client::Scenes::WorldState;

constexpr std::uint32_t Seed = 0x5eed;
constexpr std::uint16_t PlayerKey = 5;
constexpr int GrassRandoms = 65536;

// 06 § La carga del mapa, paso 2: 0x60719c..0x6071ac solo apaga el primer byte de los 200
// registros de 0x7d34818. La próxima creación debe poder reutilizarlos sin borrar sus demás campos.
TEST(MapObjectsReleaseTest, ReopensEveryRegistrationWithoutErasingItsPayload)
{
    Client::Scenes::WorldMap loaded;
    auto& registrations = loaded.registrations;
    for (std::size_t slot = 0; slot < registrations.entries.size(); ++slot)
    {
        registrations.entries[slot] =
            Client::World::WorldObjectRegistration{.active = 0x80,
                                                   .metadata = {1, 2, 3, 4, 5, 6, 7},
                                                   .object = static_cast<Client::World::WorldObjectIdentity>(slot + 1)};
    }
    constexpr Client::World::WorldObjectIdentity NewObject = 999;
    registrations.nextIdentity = NewObject;
    const auto before = registrations.entries;

    Client::Scenes::ReleaseTheMapObjects(loaded);

    for (std::size_t slot = 0; slot < registrations.entries.size(); ++slot)
    {
        SCOPED_TRACE(slot);
        EXPECT_EQ(registrations.entries[slot].active, 0);
        EXPECT_EQ(registrations.entries[slot].metadata, before[slot].metadata);
        EXPECT_EQ(registrations.entries[slot].object, before[slot].object);
    }
    EXPECT_EQ(registrations.nextIdentity, NewObject);

    Client::World::RegisterWorldObject(registrations, NewObject);

    EXPECT_EQ(registrations.entries.front().active, 1);
    EXPECT_EQ(registrations.entries.front().object, NewObject);
    EXPECT_EQ(registrations.entries.front().metadata, before.front().metadata);
    EXPECT_EQ(registrations.entries.back().active, 0);
    EXPECT_EQ(registrations.entries.back().object, before.back().object);
}

class MapLoadTest : public Client::Tests::GoldenDataTest
{
protected:
    // Como 0x4c41cf: el mundo en curso primero, después 0x61a560.
    [[nodiscard]] Client::Core::Expected<void, std::string> Load(std::uint8_t map, SceneId scene = SceneId::Game)
    {
        world.Runtime().map = map;
        return LoadMap(
            map, MapLoadContext{
                     .dataRoot = m_dataRoot, .world = world, .scene = scene, .playerKey = PlayerKey, .random = random});
    }

    WorldState world;
    CrtRandom random{Seed};
};

// Q-C146: 65.536 (rand() % 4) · 0,25, y el 0 no pide rand() al crear sus objetos.
TEST_F(MapLoadTest, TheGrassOffsetsAreTheFirstSixtyFiveThousandRandoms)
{
    ASSERT_TRUE(Load(0).HasValue());

    const auto& offsets = world.Map().grassOffsets;
    ASSERT_EQ(offsets.size(), Client::Data::Terrain::TerrainCellCount);
    CrtRandom replay(Seed);
    for (const float offset : offsets)
    {
        ASSERT_EQ(offset, static_cast<float>(replay.Next() % 4) * 0.25f);
    }
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(Seed, GrassRandoms).State());
    EXPECT_FALSE(world.Map().objects.empty());
    EXPECT_EQ(world.Map().objects.size(), world.Map().objectRecords.size());
    EXPECT_TRUE(world.Map().terrain.attributes && world.Map().terrain.heights);
}

// Q-C58: con el argumento y el mundo en curso en 32, la carga sigue como el 9, en World10.
TEST_F(MapLoadTest, MapThirtyTwoLoadsWorldTenAsNine)
{
    ASSERT_TRUE(Load(32).HasValue());

    EXPECT_EQ(world.Runtime().map, 9);
    const auto worldTen = Client::World::LoadWorldTerrain(m_dataRoot, 9);
    ASSERT_TRUE(worldTen.HasValue());
    EXPECT_EQ(world.Map().terrain.attributes->cells, worldTen.Value().attributes->cells);
    EXPECT_EQ(world.Map().terrain.heights->cells, worldTen.Value().heights->cells);
}

// 0x447d70: Devil Square (9), Blood Castle (11 a 17) y Chaos Castle (18 a 23) arman su objeto de
// evento; los demás, nada.
TEST(MapLoadEventTest, AnEventMapStopsAt447d70)
{
    for (std::int32_t map = 0; map <= 55; ++map)
    {
        SCOPED_TRACE(map);
        const bool event = map == 9 || (map >= 11 && map <= 23);
        const auto checked = CheckEventMap(map);
        ASSERT_EQ(checked.HasValue(), !event);
        if (event)
        {
            EXPECT_NE(checked.Error().find("0x447d70"), std::string::npos);
        }
    }
}

// Q-C228: la carga del mapa no vacía las marcas del suelo (0x6070a0 apaga las tablas de al lado y no esta): la
// marca que había en Lorencia sigue igual en Devias, y de vuelta en Lorencia.
TEST_F(MapLoadTest, TheGroundMarksSurviveTheMapLoad)
{
    ASSERT_TRUE(Load(0).HasValue());
    const Client::World::GroundMark mark{.live = true,
                                         .type = 0x5e3,
                                         .size = 0.65f,
                                         .position = glm::vec3(12850.0f, 12850.0f, 170.0f),
                                         .angle = 45.0f,
                                         .color = glm::vec3(0.1f, 0.0f, 0.0f),
                                         .alpha = 0.5f,
                                         .life = 25};
    world.Runtime().groundMarks[63] = mark;

    for (const std::uint8_t map : {std::uint8_t{2}, std::uint8_t{0}})
    {
        SCOPED_TRACE(static_cast<int>(map));
        ASSERT_TRUE(Load(map).HasValue());
        EXPECT_EQ(world.Runtime().groundMarks[63], mark);
        EXPECT_FALSE(world.Runtime().groundMarks[62].live);
    }
}

// Q-C180: sin TerrainHeight.OZB el original avisa y cierra; MuRet corta nombrando el archivo, con el
// pasto ya sacado.
TEST_F(MapLoadTest, AMissingTerrainHeightNamesTheFile)
{
    const Client::Tests::TemporaryFolder folder("map-load");
    const std::filesystem::path worldOne = folder.Path() / "World1";
    std::filesystem::create_directories(worldOne);
    for (const std::string_view name : {"EncTerrain1.map", "EncTerrain1.att", "EncTerrain1.obj", "TerrainLight.OZJ"})
    {
        const std::optional<std::filesystem::path> source = DataFile("World1\\" + std::string(name));
        ASSERT_TRUE(source.has_value());
        std::filesystem::copy_file(*source, worldOne / name);
    }
    world.Runtime().map = 0;

    const auto loaded = LoadMap(0, MapLoadContext{.dataRoot = folder.Path(),
                                                  .world = world,
                                                  .scene = SceneId::Game,
                                                  .playerKey = PlayerKey,
                                                  .random = random});

    ASSERT_FALSE(loaded.HasValue());
    EXPECT_NE(loaded.Error().find("TerrainHeight.OZB"), std::string::npos) << loaded.Error();
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(Seed, GrassRandoms).State());
}

// 0x510f70: fuera de la selección mata a los vivos de otra clave, comparada con signo (movsx), entre
// los 400 primeros, y apaga los ayudantes de cada uno que mata (0x510fa1..0x510fc0). Los del que sobrevive,
// los de uno que ya estaba muerto y los del 401 quedan.
TEST_F(MapLoadTest, KillsTheActorsOfAnotherKeyOutsideTheSelection)
{
    auto actors = world.Actors();
    for (std::size_t slot = 0; slot < 5; ++slot)
    {
        actors[slot].alive = true;
        actors[slot].key = static_cast<std::uint16_t>(slot);
    }
    actors[7].alive = true;
    actors[7].key = PlayerKey;
    actors[400].alive = true;
    actors[400].key = 9;
    auto& helpers = world.Helpers();
    const std::array<std::size_t, 4> riders{2, 7, 9, 400};
    for (std::size_t index = 0; index < riders.size(); ++index)
    {
        helpers[index] =
            Client::Scenes::HelperState{.alive = true, .type = Client::Scenes::DarkHorseModel, .rider = riders[index]};
    }

    ASSERT_TRUE(Load(0).HasValue());

    for (std::size_t slot = 0; slot < 5; ++slot)
    {
        EXPECT_FALSE(actors[slot].alive) << slot;
    }
    EXPECT_TRUE(actors[7].alive);
    EXPECT_TRUE(actors[400].alive);
    EXPECT_FALSE(helpers[0].alive);
    EXPECT_TRUE(helpers[1].alive);
    EXPECT_TRUE(helpers[2].alive);
    EXPECT_TRUE(helpers[3].alive);
}

TEST_F(MapLoadTest, AKeyAbove7fffKillsEvenItsOwnerAndTheSelectionKillsNobody)
{
    auto actors = world.Actors();
    actors[3].alive = true;
    actors[3].key = 0x8005;
    actors[4].alive = true;
    actors[4].key = 4;
    world.Runtime().map = 0;
    const auto selection = LoadMap(0, MapLoadContext{.dataRoot = m_dataRoot,
                                                     .world = world,
                                                     .scene = SceneId::CharacterSelect,
                                                     .playerKey = 0x8005,
                                                     .random = random});
    ASSERT_TRUE(selection.HasValue());
    EXPECT_TRUE(actors[3].alive);
    EXPECT_TRUE(actors[4].alive);

    const auto game = LoadMap(
        0, MapLoadContext{
               .dataRoot = m_dataRoot, .world = world, .scene = SceneId::Game, .playerKey = 0x8005, .random = random});

    ASSERT_TRUE(game.HasValue());
    EXPECT_FALSE(actors[3].alive);
    EXPECT_FALSE(actors[4].alive);
}

// 0x61744d..0x61751b: Devias crea el 0x201 y el 0x202 con 0x606660 antes del .obj, sin ángulo y con
// escala 1. En la captura de la VM son el primer nodo de las listas 176 y 177 (sector x 11; la y de
// 1.600 por el float de 1/1.600 todavía es el sector 0).
TEST_F(MapLoadTest, DeviasCreatesItsTwoSpecialsBeforeTheObjFile)
{
    ASSERT_TRUE(Load(2).HasValue());

    const auto& objects = world.Map().objects;
    ASSERT_GE(objects.size(), 2u);
    EXPECT_EQ(objects[0].type, 0x201);
    EXPECT_EQ(objects[0].transform.position, glm::vec3(19100.0f, 1600.0f, 270.0f));
    EXPECT_EQ(objects[1].type, 0x202);
    EXPECT_EQ(objects[1].transform.position, glm::vec3(19100.0f, 1700.0f, 270.0f));
    for (const auto& special : {objects[0], objects[1]})
    {
        EXPECT_EQ(special.transform.angles, glm::vec3(0.0f));
        EXPECT_EQ(special.transform.scale, 1.0f);
        EXPECT_EQ(special.sectorX, 11);
    }
    EXPECT_EQ(objects[0].sectorY, 0);
    EXPECT_EQ(objects[1].sectorY, 1);
}

// 0x6175ea..0x617626: el especial de Noria nace en z=0 y con yaw=10, antes de pedir sus cinco
// efectos. Los cuatro 322/323 gastan tres rand() cada uno antes del pasto; 324 no gasta.
TEST_F(MapLoadTest, NoriaCreatesItsSpecialWithTheHeightAndYawInTheNativeArguments)
{
    const auto loaded = Load(3);

    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    ASSERT_EQ(world.Map().objects.size(), world.Map().objectRecords.size() + 1);
    const auto& special = world.Map().objects.front();
    EXPECT_EQ(special.type, 0x142);
    EXPECT_EQ(special.transform.position, glm::vec3(22300.0f, 3000.0f, 0.0f));
    EXPECT_EQ(special.transform.angles, glm::vec3(0.0f, 0.0f, 10.0f));
    EXPECT_EQ(special.transform.scale, 1.0f);
    constexpr int TowerRandoms = 12;
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(Seed, TowerRandoms + GrassRandoms).State());
    EXPECT_EQ(world.Map().grassOffsets.size(), static_cast<std::size_t>(GrassRandoms));
    constexpr std::array<std::int16_t, 5> Types{322, 323, 322, 323, 324};
    constexpr std::array<float, 5> HeightsY{3000.0f, 3004.0f, 3008.0f, 3012.0f, 3020.0f};
    for (std::size_t slot = 0; slot < Types.size(); ++slot)
    {
        const auto& effect = world.Map().effects.Effects()[slot];
        EXPECT_TRUE(effect.alive);
        EXPECT_EQ(effect.type, Types[slot]);
        EXPECT_EQ(effect.transform.position, glm::vec3(22300.0f, HeightsY[slot], 350.0f));
    }
}

// Los mapas con datos en la Data del repo: los que carga y, en los demás, dónde corta.
TEST_F(MapLoadTest, ConstructsRealMapsAndNamesTheirFrontiers)
{
    struct Case
    {
        std::uint8_t map;
        std::string_view frontier; // vacío: carga
    };
    constexpr Case Cases[] = {
        {0, ""},          {1, ""},  {2, ""},  {3, ""},          {4, ""},  {5, "World6"},    {6, ""},
        {7, ""},          {8, ""},  {9, ""},  {10, ""},         {11, ""}, {18, ""},         {24, "0x4377a0"},
        {30, "0x42a760"}, {31, ""}, {33, ""}, {34, "0x42fe50"}, {54, ""}, {55, "0x6188ae"},
    };
    for (const Case& tested : Cases)
    {
        SCOPED_TRACE(static_cast<int>(tested.map));
        const auto loaded = Load(tested.map);
        if (tested.frontier.empty())
        {
            ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
            EXPECT_TRUE(world.Map().terrain.attributes && world.Map().terrain.heights);
            continue;
        }
        ASSERT_FALSE(loaded.HasValue());
        EXPECT_NE(loaded.Error().find(tested.frontier), std::string::npos) << loaded.Error();
    }
}
} // namespace
