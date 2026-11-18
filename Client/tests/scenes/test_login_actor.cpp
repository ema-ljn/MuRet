#include "Scenes/LoginActor.h"

#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorConstruction.h"
#include "Scenes/ActorCreation.h"
#include "Scenes/ActorState.h"
#include "Scenes/MapLoad.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/TerrainCulling.h"
#include "World/WorldTerrain.h"
#include "support/PlayerActors.h"

#include <glm/ext/vector_float2.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>

namespace
{
constexpr std::uint32_t Seed = 0xc0ffee;

TEST(LoginActorTest, MapCleanupDropsTheRavenEvenOfSurvivingActors)
{
    Client::Scenes::WorldState world;
    auto& survivor = world.Actors()[0];
    survivor.alive = true;
    survivor.key = 7;
    survivor.darkSpirit.emplace();
    world.Actors()[1].darkSpirit.emplace();
    world.Actors()[400].darkSpirit.emplace();
    Client::Scenes::KillOtherActors(world, 7);
    EXPECT_TRUE(survivor.alive);
    EXPECT_FALSE(survivor.darkSpirit);
    EXPECT_FALSE(world.Actors()[1].darkSpirit);
    EXPECT_TRUE(world.Actors()[400].darkSpirit);
}

// 0x510fd2: 0x414340 en los 400, vivos o no: la capa de los GM se suelta y +0x360 vuelve a 0; sin capa,
// +0x360 queda como estaba.
TEST(LoginActorTest, MapCleanupReleasesTheGameMasterLayers)
{
    Client::Scenes::WorldState world;
    auto& survivor = world.Actors()[0];
    survivor.alive = true;
    survivor.key = 7;
    survivor.byte360 = 5;
    survivor.layer.emplace();
    world.Actors()[1].byte360 = 5;
    world.Actors()[400].byte360 = 5;
    world.Actors()[400].layer.emplace();
    Client::Scenes::KillOtherActors(world, 7);
    EXPECT_FALSE(survivor.layer);
    EXPECT_EQ(survivor.byte360, 0);
    EXPECT_EQ(world.Actors()[1].byte360, 5);
    EXPECT_TRUE(world.Actors()[400].layer);
    EXPECT_EQ(world.Actors()[400].byte360, 5);
}

// 0x510f99 (CL9 hito 2): 0x401040 saca los huesos con nombre de los que mueren; los del que sobrevive quedan.
TEST(LoginActorTest, MapCleanupForgetsTheNamedBonesOfTheKilled)
{
    const auto world = std::make_unique<Client::Scenes::WorldState>();
    world->Actors()[0].alive = true;
    world->Actors()[0].key = 7;
    world->Actors()[1].alive = true;
    world->Actors()[1].key = 8;
    Client::Scenes::NameABone(world->Runtime(), 0, "Hueso_Uno", 3);
    Client::Scenes::NameABone(world->Runtime(), 1, "Hueso_Dos", 4);
    Client::Scenes::KillOtherActors(*world, 7);
    ASSERT_EQ(world->Runtime().namedBones.size(), 1u);
    EXPECT_EQ(world->Runtime().namedBones[0].actor, 0u);
    EXPECT_EQ(world->Runtime().namedBones[0].name, "Hueso_Uno");
}

TEST(LoginActorTest, MissingWorldDataCutsAfterThePreviousCellRandoms)
{
    for (const bool missingAttributes : {true, false})
    {
        Client::World::WorldTerrain terrain;
        if (!missingAttributes)
        {
            terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        }
        Client::Scenes::ActorState actor;
        Client::Scenes::WorldRuntime runtime;
        Client::Render::Models::ClothWorld cloth{.random = Client::Render::Models::CrtRandom(Seed)};
        const auto skeleton = Client::Tests::PlayerActionsSkeleton();
        const auto built = Client::Scenes::ConstructLoginActor(
            actor, {.terrain = terrain, .map = 55, .scene = Client::World::CullingScene::Login, .fixedHeight = 450.0f},
            runtime, cloth, skeleton);
        ASSERT_FALSE(built.HasValue());
        EXPECT_NE(built.Error().find(missingAttributes ? "0x511b9f" : "0x51225f"), std::string::npos);
        EXPECT_EQ(cloth.random.State(), Client::Tests::AfterDraws(Seed, missingAttributes ? 16 : 28).State());
    }
}

TEST(LoginActorTest, TheHorseChoosesItsStandingActionWithoutItemBmd)
{
    Client::World::WorldTerrain terrain;
    terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    terrain.heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    Client::Scenes::ActorState actor;
    actor.headTurn.angles = {20.0f, 30.0f};
    Client::Scenes::WorldRuntime runtime;
    Client::Render::Models::ClothWorld cloth{.random = Client::Render::Models::CrtRandom(Seed)};
    const auto skeleton = Client::Tests::PlayerActionsSkeleton();
    const auto built = Client::Scenes::ConstructLoginActor(
        actor, {.terrain = terrain, .map = 55, .scene = Client::World::CullingScene::Login, .fixedHeight = 450.0f},
        runtime, cloth, skeleton);
    ASSERT_TRUE(built.HasValue()) << built.Error();
    EXPECT_EQ(actor.animation.action, 0x4a);
    EXPECT_EQ(actor.headTurn.angles, glm::vec2(20.0f, 30.0f));
    EXPECT_EQ(cloth.random.State(), Client::Tests::AfterDraws(Seed, 41).State());
    ASSERT_TRUE(actor.darkSpirit);
    EXPECT_EQ(actor.darkSpirit->position.x, 50.0f);
    EXPECT_EQ(actor.darkSpirit->position.y, 50.0f);
}

TEST(LoginActorTest, OpenCellAndCounterAccessesKeepTheirBirthRandoms)
{
    Client::World::WorldTerrain terrain;
    terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    terrain.heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    Client::Scenes::ActorState actor;
    actor.cellXAccesses = 1;
    actor.cellYAccesses = 1;
    Client::Scenes::WorldRuntime runtime;
    Client::Render::Models::ClothWorld cloth{.random = Client::Render::Models::CrtRandom(Seed),
                                             .openCounterAccesses = 1};
    const auto skeleton = Client::Tests::PlayerActionsSkeleton();
    const auto built = Client::Scenes::ConstructLoginActor(
        actor, {.terrain = terrain, .map = 55, .scene = Client::World::CullingScene::Login, .fixedHeight = 450.0f},
        runtime, cloth, skeleton);
    ASSERT_TRUE(built.HasValue()) << built.Error();
    // Ni el personaje ni el cuervo cierran estas cuentas en cero: solo el sufijo de parado.
    EXPECT_EQ(cloth.random.State(), Client::Tests::AfterDraws(Seed, 1).State());
    EXPECT_EQ(actor.cellXAccesses, 1);
    EXPECT_EQ(actor.cellYAccesses, 1);
    EXPECT_EQ(cloth.openCounterAccesses, 1);
}
} // namespace
