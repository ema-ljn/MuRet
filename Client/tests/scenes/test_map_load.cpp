#include "Session/MapLoad.h"
#include "Kernel/HelperTable.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainLight.h"
#include "Models/Cloth.h"
#include "Render/ModelBatch.h"
#include "Render/Pass3D.h"
#include "Actors/Drawing/CharacterCloth.h"
#include "Actors/ActorConstruction.h"
#include "Actors/ActorCreation.h"
#include "Pets/DarkHorse.h"
#include "Events/EventMapObject.h"
#include "Pets/HelperTable.h"
#include "Kernel/SceneId.h"
#include "Kernel/WorldMap.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Terrain/GroundMarks.h"
#include "Terrain/TerrainLighting.h"
#include "Terrain/TerrainState.h"
#include "WorldObjects/WorldObjectRegistrations.h"
#include "Terrain/WorldTerrain.h"
#include "Terrain/TerrainCulling.h"
#include "Tables/PlayerModels.h"

#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/PlayerActors.h"
#include "support/TemporaryFolder.h"
#include "support/ClothVectors.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace
{
using Client::Actors::Drawing::CharacterCloth;
using Client::Actors::Drawing::ClothWearer;
using Client::Crt::CrtRandom;
using Client::Events::EventMapKind;
using Client::Events::EventMapObject;
using Client::Kernel::SceneId;
using Client::Kernel::WorldRuntime;
using Client::Kernel::WorldState;
using Client::Scenes::ArmEventMapObject;
using Client::Scenes::LoadMap;
using Client::Scenes::MapLoadContext;

constexpr std::uint32_t Seed = 0x5eed;
constexpr std::uint16_t PlayerKey = 5;
constexpr int GrassRandoms = 65536;

TEST(ActorClothLifecycleTest, MapCleanupReleasesClothsInEveryPoolSlotExceptTheSentinel)
{
    Client::Tests::ClothVectors vectors;
    const auto error =
        Client::Tests::ReadClothVectors(Client::Tests::ClothVectorsDir() / "a_login_dark_lord.txt", vectors);
    ASSERT_FALSE(error.has_value()) << *error;
    ASSERT_FALSE(vectors.frames.empty());
    const auto& frame = vectors.frames.front();
    Client::Models::ClothWorld clothWorld{.random = Client::Crt::CrtRandom(vectors.randomState)};

    WorldState world;
    for (const std::size_t slot : {0u, 1u, 400u})
    {
        CharacterCloth& cloth = world.ActorCloths()[slot];
        const Client::Models::ClothBody body{
            .bones = frame.bones, .angles = frame.angles, .scale = frame.scale, .position = frame.origin};
        ClothWearer wearer;
        wearer.characterClass = frame.characterClass;
        wearer.armor = frame.armor;
        if (frame.wings != 0xffff)
            wearer.wings = frame.wings;
        Client::Render::ModelBatch batch;
        batch.Begin(Client::Render::Pass3D{});
        cloth.Frame(batch, wearer, body, clothWorld, frame.map, frame.worldTime, frame.wears);
        ASSERT_FALSE(cloth.Cloths().empty()) << "slot " << slot;
    }

    world.Actors()[0].alive = true;
    world.Actors()[0].key = 7;
    world.Actors()[1].alive = false;
    world.Actors()[1].key = 7;
    world.Actors()[400].alive = true;
    world.Actors()[400].key = 8;

    Client::Scenes::KillOtherActors(world, 7);

    EXPECT_TRUE(world.Actors()[0].alive);
    EXPECT_TRUE(world.ActorCloths()[0].Cloths().empty());
    EXPECT_TRUE(world.ActorCloths()[1].Cloths().empty());
    EXPECT_FALSE(world.ActorCloths()[400].Cloths().empty());
}

// 06 § La carga del mapa, step 2: 0x60719c..0x6071ac only clears the first byte of 0x7d34818's 200 records. The next
// creation must be able to reuse them without erasing their other fields.
TEST(MapObjectsReleaseTest, ReopensEveryRegistrationWithoutErasingItsPayload)
{
    Client::Kernel::WorldMap loaded;
    auto& registrations = loaded.registrations;
    for (std::size_t slot = 0; slot < registrations.entries.size(); ++slot)
    {
        registrations.entries[slot] = Client::WorldObjects::WorldObjectRegistration{
            .active = 0x80,
            .metadata = {1, 2, 3, 4, 5, 6, 7},
            .object = static_cast<Client::WorldObjects::WorldObjectIdentity>(slot + 1)};
    }
    constexpr Client::WorldObjects::WorldObjectIdentity NewObject = 999;
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

    Client::WorldObjects::RegisterWorldObject(registrations, NewObject);

    EXPECT_EQ(registrations.entries.front().active, 1);
    EXPECT_EQ(registrations.entries.front().object, NewObject);
    EXPECT_EQ(registrations.entries.front().metadata, before.front().metadata);
    EXPECT_EQ(registrations.entries.back().active, 0);
    EXPECT_EQ(registrations.entries.back().object, before.back().object);
}

class MapLoadTest : public Client::Tests::GoldenDataTest
{
protected:
    // Like 0x4c41cf: the current world first, then 0x61a560.
    [[nodiscard]] Client::Core::Expected<void, std::string> Load(std::uint8_t map, SceneId scene = SceneId::Game)
    {
        world.Runtime().map = map;
        return LoadMap(map, MapLoadContext{.dataRoot = m_dataRoot,
                                           .world = world,
                                           .terrain = terrainState,
                                           .scene = scene,
                                           .playerKey = PlayerKey,
                                           .random = random});
    }

    WorldState world;
    Client::Terrain::TerrainState terrainState;
    CrtRandom random{Seed};
};

// Q-C146: 65536 (rand() % 4) · 0.25, and 0 draws no rand() when creating its objects.
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
    EXPECT_TRUE(terrainState.current.attributes && terrainState.current.heights);
}

// Q-C58: with the argument and the current world at 32, the load goes on as 9, in World10.
TEST_F(MapLoadTest, MapThirtyTwoLoadsWorldTenAsNine)
{
    ASSERT_TRUE(Load(32).HasValue());

    EXPECT_EQ(world.Runtime().map, 9);
    const auto worldTen = Client::Terrain::LoadWorldTerrain(m_dataRoot, 9);
    ASSERT_TRUE(worldTen.HasValue());
    EXPECT_EQ(terrainState.current.attributes->cells, worldTen.Value().attributes->cells);
    EXPECT_EQ(terrainState.current.heights->cells, worldTen.Value().heights->cells);
}

TEST_F(MapLoadTest, CreatingAnActorInAFreeSlotDropsItsClothAndLiveKeyRecreationKeepsIt)
{
    ASSERT_TRUE(Load(0).HasValue());

    Client::Tests::ClothVectors vectors;
    const auto error =
        Client::Tests::ReadClothVectors(Client::Tests::ClothVectorsDir() / "a_login_dark_lord.txt", vectors);
    ASSERT_FALSE(error.has_value()) << *error;
    ASSERT_FALSE(vectors.frames.empty());
    const auto& frame = vectors.frames.front();
    const auto buildCloth = [&](CharacterCloth& cloth)
    {
        Client::Models::ClothWorld clothWorld{.random = CrtRandom(vectors.randomState)};
        const Client::Models::ClothBody body{
            .bones = frame.bones, .angles = frame.angles, .scale = frame.scale, .position = frame.origin};
        ClothWearer wearer;
        wearer.characterClass = frame.characterClass;
        wearer.armor = frame.armor;
        if (frame.wings != 0xffff)
            wearer.wings = frame.wings;
        Client::Render::ModelBatch batch;
        batch.Begin(Client::Render::Pass3D{});
        cloth.Frame(batch, wearer, body, clothWorld, frame.map, frame.worldTime, frame.wears);
    };

    buildCloth(world.ActorCloths()[0]);
    ASSERT_FALSE(world.ActorCloths()[0].Cloths().empty());
    const Client::Actors::ActorConstructionContext construction{terrainState.current, 0,
                                                                Client::Terrain::CullingScene::Game, 450.0f};
    Client::Models::ClothWorld actorClothWorld{.random = CrtRandom(Seed)};

    const auto first = Client::Actors::CreateActor(world, PlayerKey, Client::Tables::PlayerSkeletonModel,
                                                   {100, 100, 0.0f}, construction, actorClothWorld);
    ASSERT_TRUE(first.HasValue()) << first.Error();
    ASSERT_EQ(first.Value(), 0u);
    EXPECT_TRUE(world.ActorCloths()[0].Cloths().empty());

    buildCloth(world.ActorCloths()[0]);
    ASSERT_FALSE(world.ActorCloths()[0].Cloths().empty());
    const auto liveRecreation = Client::Actors::CreateActor(world, PlayerKey, Client::Tables::PlayerSkeletonModel,
                                                            {100, 100, 0.0f}, construction, actorClothWorld);
    ASSERT_TRUE(liveRecreation.HasValue()) << liveRecreation.Error();
    EXPECT_EQ(liveRecreation.Value(), first.Value());
    EXPECT_FALSE(world.ActorCloths()[0].Cloths().empty());

    Client::Actors::RemoveActor(world, PlayerKey);
    EXPECT_TRUE(world.ActorCloths()[0].Cloths().empty());
    const auto afterLeave = Client::Actors::CreateActor(world, PlayerKey, Client::Tables::PlayerSkeletonModel,
                                                        {100, 100, 0.0f}, construction, actorClothWorld);
    ASSERT_TRUE(afterLeave.HasValue()) << afterLeave.Error();
    EXPECT_EQ(afterLeave.Value(), first.Value());
    EXPECT_TRUE(world.ActorCloths()[0].Cloths().empty());
}

// 0x447d70: Devil Square (9), Blood Castle (11 to 17) and Chaos Castle (18 to 23) arm their event object, with the
// fields of 0x447db5..0x447ddc (and of 0x404d60); the others do not touch the previous one (Q-C320).
TEST(MapLoadEventTest, AnEventMapArmsItsObjectAndTheOthersKeepThePreviousOne)
{
    for (std::int32_t map = 0; map <= 55; ++map)
    {
        SCOPED_TRACE(map);
        WorldRuntime runtime;
        runtime.map = map;
        runtime.chaosCastleArea = 3;
        ArmEventMapObject(map, runtime);
        const bool devilSquare = map == 9;
        const bool bloodCastle = map >= 11 && map <= 17;
        const bool chaosCastle = map >= 18 && map <= 23;
        ASSERT_EQ(runtime.eventObject.has_value(), devilSquare || bloodCastle || chaosCastle);
        EXPECT_EQ(runtime.chaosCastleArea, chaosCastle ? 0xff : 3);
        if (!runtime.eventObject)
            continue;
        const EventMapObject& armed = *runtime.eventObject;
        EXPECT_EQ(armed.kind, devilSquare   ? EventMapKind::DevilSquare
                              : bloodCastle ? EventMapKind::BloodCastle
                                            : EventMapKind::ChaosCastle);
        EXPECT_EQ(armed.field0c, 0);
        EXPECT_EQ(armed.field10, 0);
        EXPECT_EQ(armed.field14, (std::array<std::int32_t, 4>{-1, -1, -1, -1}));
        EXPECT_EQ(armed.field24, 0);
        EXPECT_EQ(armed.field28, 0);
        EXPECT_EQ(armed.field2c, (std::array<std::int32_t, 0x42>{}));
    }

    WorldRuntime leaving;
    leaving.map = 11;
    ArmEventMapObject(11, leaving);
    leaving.eventObject->field0c = 7;
    leaving.map = 0;
    ArmEventMapObject(0, leaving);
    ASSERT_TRUE(leaving.eventObject.has_value());
    EXPECT_EQ(leaving.eventObject->kind, EventMapKind::BloodCastle);
    EXPECT_EQ(leaving.eventObject->field0c, 7);
}

// 0x447de7: Chaos Castle looks at the current world (0x6c9dd0), not the argument; Blood Castle and Devil Square, at
// the argument. Blood Castle goes first.
TEST(MapLoadEventTest, ChaosCastleReadsTheCurrentWorldInsteadOfTheArgument)
{
    WorldRuntime runtime;
    runtime.map = 20;
    ArmEventMapObject(9, runtime);
    ASSERT_TRUE(runtime.eventObject.has_value());
    EXPECT_EQ(runtime.eventObject->kind, EventMapKind::ChaosCastle);
    ArmEventMapObject(12, runtime);
    EXPECT_EQ(runtime.eventObject->kind, EventMapKind::BloodCastle);

    WorldRuntime elsewhere;
    elsewhere.map = 0;
    ArmEventMapObject(20, elsewhere);
    EXPECT_FALSE(elsewhere.eventObject.has_value());
    EXPECT_EQ(elsewhere.chaosCastleArea, 0xff);
}

// Q-C228: the map load does not empty the ground marks (0x6070a0 clears the neighboring tables, not this one): the
// mark in Lorencia stays the same in Devias, and back in Lorencia.
TEST_F(MapLoadTest, TheGroundMarksSurviveTheMapLoad)
{
    ASSERT_TRUE(Load(0).HasValue());
    const Client::Terrain::GroundMark mark{.live = true,
                                           .type = 0x5e3,
                                           .size = 0.65f,
                                           .position = glm::vec3(12850.0f, 12850.0f, 170.0f),
                                           .angle = 45.0f,
                                           .color = glm::vec3(0.1f, 0.0f, 0.0f),
                                           .alpha = 0.5f,
                                           .life = 25};
    terrainState.groundMarks[63] = mark;

    for (const std::uint8_t map : {std::uint8_t{2}, std::uint8_t{0}})
    {
        SCOPED_TRACE(static_cast<int>(map));
        ASSERT_TRUE(Load(map).HasValue());
        EXPECT_EQ(terrainState.groundMarks[63], mark);
        EXPECT_FALSE(terrainState.groundMarks[62].live);
    }
}

// Q-C180: without TerrainHeight.OZB the original warns and closes; MuRet stops naming the file, with the grass already
// drawn.
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
                                                  .terrain = terrainState,
                                                  .scene = SceneId::Game,
                                                  .playerKey = PlayerKey,
                                                  .random = random});

    ASSERT_FALSE(loaded.HasValue());
    EXPECT_NE(loaded.Error().find("TerrainHeight.OZB"), std::string::npos) << loaded.Error();
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(Seed, GrassRandoms).State());
}

// 0x510f70: outside the selection it kills the live ones of another key, compared signed (movsx), among the first 400,
// and turns off the helpers of each one it kills (0x510fa1..0x510fc0). Those of the survivor, of one already dead and
// of the 401st stay.
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
            Client::Kernel::HelperState{.alive = true, .type = Client::Pets::DarkHorseModel, .rider = riders[index]};
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
                                                     .terrain = terrainState,
                                                     .scene = SceneId::CharacterSelect,
                                                     .playerKey = 0x8005,
                                                     .random = random});
    ASSERT_TRUE(selection.HasValue());
    EXPECT_TRUE(actors[3].alive);
    EXPECT_TRUE(actors[4].alive);

    const auto game = LoadMap(0, MapLoadContext{.dataRoot = m_dataRoot,
                                                .world = world,
                                                .terrain = terrainState,
                                                .scene = SceneId::Game,
                                                .playerKey = 0x8005,
                                                .random = random});

    ASSERT_TRUE(game.HasValue());
    EXPECT_FALSE(actors[3].alive);
    EXPECT_FALSE(actors[4].alive);
}

// 0x61744d..0x61751b: Devias creates 0x201 and 0x202 with 0x606660 before the .obj, without angle and with scale 1. In
// the VM capture they are the first node of lists 176 and 177 (sector x 11; the y of 1600 times the float of 1/1600 is
// still sector 0).
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

// 0x6175ea..0x617626: Noria's special is born at z=0 and with yaw=10, before asking for its five effects. The four
// 322/323 spend three rand() each before the grass; 324 spends none.
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

// 0x42a760 with the siege byte at 0, as the original starts: the music hour (0x7239f0) at 0, a B9 02 and the five
// 0x19f spearmen at (x, 11300, 270), before the .obj ones (06 § Los objetos de Castle Siege).
TEST_F(MapLoadTest, CastleSiegeAsksForItsStateAndPlacesTheFiveSpearmen)
{
    world.Runtime().map = 30;
    world.Runtime().castleSiege.siegeMusicTime = 1234.0f;
    int requests = 0;
    const auto loaded = LoadMap(30, MapLoadContext{.dataRoot = m_dataRoot,
                                                   .world = world,
                                                   .terrain = terrainState,
                                                   .scene = SceneId::Game,
                                                   .playerKey = PlayerKey,
                                                   .random = random,
                                                   .requestCastleSiegeState = [&requests] { ++requests; }});

    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    EXPECT_EQ(requests, 1);
    EXPECT_EQ(world.Runtime().castleSiege.siegeMusicTime, 0.0f);
    constexpr std::size_t Spearmen = 5;
    ASSERT_EQ(world.Map().objects.size(), world.Map().objectRecords.size() + Spearmen);
    constexpr std::array<float, Spearmen> X{6500.0f, 7100.0f, 9100.0f, 11800.0f, 12300.0f};
    for (std::size_t index = 0; index < Spearmen; ++index)
    {
        SCOPED_TRACE(index);
        const auto& spearman = world.Map().objects[index];
        EXPECT_EQ(spearman.type, 0x19f);
        EXPECT_EQ(spearman.transform.position, glm::vec3(X[index], 11300.0f, 270.0f));
        EXPECT_EQ(spearman.transform.angles, glm::vec3(0.0f));
        EXPECT_EQ(spearman.transform.scale, 1.0f);
    }
    EXPECT_TRUE(terrainState.current.attributes && terrainState.current.heights);
}

// 0x61a6ea/0x61a8af: at war, ATT312 and TerrainLight2, after the spearmen.
TEST_F(MapLoadTest, CastleSiegeWithTheSiegeOnLoadsTheWarAttributesAndLight)
{
    world.Runtime().castleSiege.siegeActive = true;
    const auto loaded = Load(30);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    const auto file = Client::Tests::ReadFileBytes(m_dataRoot / "world31" / "EncTerrain312.att");
    ASSERT_TRUE(file.has_value());
    const auto attributes = Client::Data::Terrain::LoadTerrainAttributes(std::as_bytes(std::span(*file)), 31);
    ASSERT_TRUE(attributes.HasValue());
    ASSERT_NE(terrainState.current.attributes, nullptr);
    EXPECT_EQ(terrainState.current.attributes->cells, attributes.Value()->cells);
    const auto light = Client::Tests::ReadFileBytes(m_dataRoot / "world31" / "TerrainLight2.OZJ");
    ASSERT_TRUE(light.has_value());
    const auto decoded = Client::Data::Terrain::LoadTerrainLight(std::as_bytes(std::span(*light)));
    ASSERT_TRUE(decoded.HasValue());
    EXPECT_EQ(terrainState.current.lighting->cells,
              Client::Terrain::ShadeTerrainLight(*decoded.Value(), *terrainState.current.heights, 30)->cells);
}

// 0x61a6d0 → 0x42fe50: with the .bss Crywolf state (0) map 34 takes EncTerrain35.att, the same one the repo loader
// reads, and not EncTerrain351.att. With 1 and 2, set by the BD 00, EncTerrain351/352.att and TerrainLight1/2.
TEST_F(MapLoadTest, CrywolfTakesTheAttributesAndTheLightOfItsState)
{
    ASSERT_EQ(world.Runtime().crywolf.state, 0);
    ASSERT_TRUE(Load(34).HasValue());
    const auto plain = Client::Tests::ReadFileBytes(m_dataRoot / "world35" / "EncTerrain35.att");
    const auto first = Client::Tests::ReadFileBytes(m_dataRoot / "world35" / "EncTerrain351.att");
    ASSERT_TRUE(plain.has_value() && first.has_value());
    const auto expected = Client::Data::Terrain::LoadTerrainAttributes(std::as_bytes(std::span(*plain)), 35);
    const auto other = Client::Data::Terrain::LoadTerrainAttributes(std::as_bytes(std::span(*first)), 35);
    ASSERT_TRUE(expected.HasValue() && other.HasValue());
    ASSERT_NE(expected.Value()->cells, other.Value()->cells);
    ASSERT_TRUE(terrainState.current.attributes);
    EXPECT_EQ(terrainState.current.attributes->cells, expected.Value()->cells);
    const auto plainLight = terrainState.current.lighting->cells;
    for (const std::uint8_t state : {std::uint8_t{1}, std::uint8_t{2}})
    {
        SCOPED_TRACE(static_cast<int>(state));
        world.Runtime().crywolf.state = state;
        const auto loaded = Load(34);
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        const auto file =
            Client::Tests::ReadFileBytes(m_dataRoot / "world35" / ("EncTerrain35" + std::to_string(state) + ".att"));
        ASSERT_TRUE(file.has_value());
        const auto attributes = Client::Data::Terrain::LoadTerrainAttributes(std::as_bytes(std::span(*file)), 35);
        ASSERT_TRUE(attributes.HasValue());
        EXPECT_EQ(terrainState.current.attributes->cells, attributes.Value()->cells);
        const auto light =
            Client::Tests::ReadFileBytes(m_dataRoot / "world35" / ("TerrainLight" + std::to_string(state) + ".OZJ"));
        ASSERT_TRUE(light.has_value());
        const auto decoded = Client::Data::Terrain::LoadTerrainLight(std::as_bytes(std::span(*light)));
        ASSERT_TRUE(decoded.HasValue());
        EXPECT_EQ(terrainState.current.lighting->cells,
                  Client::Terrain::ShadeTerrainLight(*decoded.Value(), *terrainState.current.heights, 34)->cells);
        EXPECT_NE(terrainState.current.lighting->cells, plainLight);
    }
}

TEST_F(MapLoadTest, CrywolfReusesTerrainTablesAfterTheAttributeReaderRejectsTheMappingFile)
{
    const Client::Tests::TemporaryFolder folder("crywolf-map-load");
    const auto worldFolder = folder.Path() / "World35";
    std::filesystem::create_directories(worldFolder);
    for (const std::string_view name : {"EncTerrain35.map", "EncTerrain35.obj", "TerrainHeight.OZB"})
    {
        const auto source = DataFile("World35\\" + std::string(name));
        ASSERT_TRUE(source.has_value());
        std::filesystem::copy_file(*source, worldFolder / name);
    }
    for (const bool aliasExists : {true, false})
    {
        SCOPED_TRACE(aliasExists);
        const auto alias = worldFolder / "TerrainHeight.OZJ";
        if (aliasExists)
        {
            std::filesystem::copy_file(worldFolder / "TerrainHeight.OZB", alias);
        }
        else
        {
            ASSERT_TRUE(std::filesystem::remove(alias));
        }
        ASSERT_TRUE(Load(0).HasValue());
        const auto* attributes = terrainState.current.attributes.get();
        const auto* retainedLight = terrainState.current.rawLight.get();
        const auto* oldHeights = terrainState.current.heights.get();
        ASSERT_NE(attributes, nullptr);
        ASSERT_NE(retainedLight, nullptr);
        ASSERT_NE(oldHeights, nullptr);
        world.Runtime().crywolf.state = aliasExists ? 3 : 255;
        world.Runtime().map = 34;

        const auto loaded = LoadMap(34, MapLoadContext{.dataRoot = folder.Path(),
                                                       .world = world,
                                                       .terrain = terrainState,
                                                       .scene = SceneId::Game,
                                                       .playerKey = PlayerKey,
                                                       .random = random});

        if (aliasExists)
        {
            EXPECT_TRUE(loaded.HasValue()) << loaded.Error();
            EXPECT_FALSE(world.Runtime().fatalDataMessage.has_value());
        }
        else
        {
            ASSERT_FALSE(loaded.HasValue());
            EXPECT_NE(loaded.Error().find("TerrainHeight.OZJ"), std::string::npos) << loaded.Error();
            EXPECT_EQ(world.Runtime().fatalDataMessage, "Data\\World35\\TerrainHeight.OZJ - File not exist.");
        }
        const auto& terrain = terrainState.current;
        ASSERT_NE(terrain.mapping, nullptr);
        EXPECT_FALSE(world.Map().objectRecords.empty());
        EXPECT_EQ(terrain.attributes.get(), attributes);
        ASSERT_EQ(terrain.rawLight.get(), retainedLight);
        ASSERT_NE(terrain.heights, nullptr);
        ASSERT_NE(terrain.lighting, nullptr);
        EXPECT_EQ(terrain.lighting->cells,
                  Client::Terrain::ShadeTerrainLight(*terrain.rawLight, *terrain.heights, 34)->cells);
    }
}

// The maps with data in the repo's Data: those it loads and, for the others, where it stops.
TEST_F(MapLoadTest, ConstructsRealMapsAndNamesTheirFrontiers)
{
    struct Case
    {
        std::uint8_t map;
        std::string_view frontier; // empty: loads
    };
    constexpr Case Cases[] = {
        {0, ""},  {1, ""},  {2, ""},  {3, ""},  {4, ""},  {5, "World6"}, {6, ""},  {7, ""},  {8, ""},  {9, ""},
        {10, ""}, {11, ""}, {18, ""}, {24, ""}, {30, ""}, {31, ""},      {33, ""}, {34, ""}, {54, ""}, {55, "0x6188ae"},
    };
    for (const Case& tested : Cases)
    {
        SCOPED_TRACE(static_cast<int>(tested.map));
        const auto loaded = Load(tested.map);
        if (tested.frontier.empty())
        {
            ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
            EXPECT_TRUE(terrainState.current.attributes && terrainState.current.heights);
            continue;
        }
        ASSERT_FALSE(loaded.HasValue());
        EXPECT_NE(loaded.Error().find(tested.frontier), std::string::npos) << loaded.Error();
    }
}
} // namespace
