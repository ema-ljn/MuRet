#include "Scenes/CharacterStep.h"

#include "Core/Expected.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorState.h"
#include "Scenes/PlayerActionSpeeds.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/TerrainCulling.h"
#include "World/WorldTerrain.h"
#include "support/PlayerActors.h"
#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace
{
class CharacterMotionStepTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_world.Runtime().map = 0;
        m_world.Runtime().hero = 0;
        m_world.Runtime().terrainPolygon.everything = true;
        auto& terrain = m_world.Map().terrain;
        terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        terrain.heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
        m_skeleton = Client::Tests::PlayerActionsSkeleton();
        for (auto& info : m_skeleton.actions)
        {
            info.speed = 0.6f;
            info.keyCount = 20;
        }
        Actor() = Client::Tests::UnarmedPlayer();
        Actor().alive = true;
        Actor().byte88 = 1;
        Actor().cellX = Actor().targetX = 10;
        Actor().cellY = Actor().targetY = 20;
        Actor().position = {1050.0f, 2050.0f, 0.0f};
    }
    Client::Scenes::ActorState& Actor()
    {
        return m_world.Actors()[1];
    }
    auto Step()
    {
        return Client::Scenes::StepCharacters({.world = m_world, .random = m_random, .playerSkeleton = &m_skeleton});
    }
    Client::Scenes::WorldState m_world;
    Client::Render::Models::CrtRandom m_random{1};
    Client::Render::Models::Skeleton m_skeleton;
};

TEST_F(CharacterMotionStepTest, MovingOtherUsesTheSharedWalkingSpeedInItsSameBodyStep)
{
    auto& actor = Actor();
    actor.movement = 1;
    actor.flags78 = 2;
    actor.path.count = 3;
    actor.path.cells[0] = {10, 20};
    actor.path.cells[1] = {11, 20};
    actor.path.cells[2] = {12, 20};
    actor.targetX = 12;
    const auto stepped = Step();
    ASSERT_TRUE(stepped.HasValue()) << (stepped.HasValue() ? "" : stepped.Error());
    EXPECT_EQ(actor.cellX, 11);
    EXPECT_EQ(actor.targetX, 12);
    EXPECT_FLOAT_EQ(actor.animation.frame, 0.165f);
    EXPECT_FLOAT_EQ(m_world.ActionSpeeds().Speed(13, 9.0f), 0.165f);
    EXPECT_FLOAT_EQ(m_skeleton.actions[13].speed, 0.6f);
    EXPECT_GT(actor.position.x, 1050.0f);
}

TEST_F(CharacterMotionStepTest, CooldownIsRetainedOffscreenAndDecrementedBeforeItsFirstUnsupportedCreator)
{
    auto& actor = Actor();
    actor.movementCooldown = 2;
    m_world.Runtime().terrainPolygon = {.x = {200.0f, 201.0f, 201.0f, 200.0f}, .y = {200.0f, 200.0f, 201.0f, 201.0f}};
    ASSERT_FALSE(Client::Scenes::CharacterSeen(m_world.Runtime().terrainPolygon, actor.position));
    const auto unseen = Step();
    ASSERT_TRUE(unseen.HasValue()) << (unseen.HasValue() ? "" : unseen.Error());
    EXPECT_EQ(actor.movementCooldown, 2);
    m_world.Runtime().terrainPolygon.everything = true;
    const auto seen = Step();
    EXPECT_FALSE(seen.HasValue());
    EXPECT_EQ(actor.movementCooldown, 1);
    EXPECT_FLOAT_EQ(actor.light.x, 1.0f);
    EXPECT_FLOAT_EQ(actor.light.y, 1.0f);
    EXPECT_FLOAT_EQ(actor.light.z, 1.0f);
}
} // namespace

class GoldenCharacterMotionStep : public Client::Tests::GoldenDataTest
{
};

TEST_F(GoldenCharacterMotionStep, RealWalkUpdatesSafeZoneFromTheCellAheadInLorenciaAndDevias)
{
    struct Border
    {
        std::uint8_t map;
        std::uint8_t x;
        std::uint8_t y;
        bool north;
    };
    for (const Border border : {Border{0, 172, 128, false}, Border{2, 220, 76, true}})
    {
        SCOPED_TRACE(border.map);
        Client::Scenes::WorldState world;
        world.Runtime().map = border.map;
        world.Runtime().hero = 0;
        world.Runtime().terrainPolygon.everything = true;
        auto loaded = Client::World::LoadWorldTerrain(m_dataRoot, border.map);
        ASSERT_TRUE(loaded.HasValue()) << (loaded.HasValue() ? "" : loaded.Error());
        world.Map().terrain = std::move(loaded).Value();
        auto& actor = world.Actors()[1];
        actor = Client::Tests::UnarmedPlayer();
        actor.alive = true;
        actor.byte88 = 1;
        actor.safeZone = 1;
        actor.movement = 1;
        actor.cellX = border.x;
        actor.cellY = border.y;
        actor.targetX = static_cast<std::uint8_t>(border.x + (border.north ? 0 : 1));
        actor.targetY = static_cast<std::uint8_t>(border.y + (border.north ? 1 : 0));
        actor.position = {static_cast<float>(border.x) * 100.0f + 50.0f, static_cast<float>(border.y) * 100.0f + 50.0f,
                          0.0f};
        actor.path.count = 2;
        actor.path.cells[0] = {actor.cellX, actor.cellY};
        actor.path.cells[1] = {actor.targetX, actor.targetY};
        const auto original = actor.position;
        const auto skeleton = Client::Tests::PlayerActionsSkeleton();
        Client::Render::Models::CrtRandom random(1);
        const auto stepped =
            Client::Scenes::StepCharacters({.world = world, .random = random, .playerSkeleton = &skeleton});
        ASSERT_TRUE(stepped.HasValue()) << (stepped.HasValue() ? "" : stepped.Error());
        EXPECT_EQ(actor.cellX, actor.targetX);
        EXPECT_EQ(actor.cellY, actor.targetY);
        EXPECT_EQ(actor.safeZone, 0);
        EXPECT_LT(actor.position.x - original.x, 100.0f);
        EXPECT_LT(actor.position.y - original.y, 100.0f);
    }
}

TEST_F(CharacterMotionStepTest, WalkingHeroRequestsItsSoundWithoutStoppingTheWorldStep)
{
    m_world.Runtime().hero = 1;
    m_world.OwnCharacter().ClearEquipmentForEntry();
    auto& actor = Actor();
    actor.animation.action = 13;
    actor.animation.frame = 1.6f;
    std::vector<std::int32_t> sounds;
    const auto stepped = Client::Scenes::StepCharacters(
        {.world = m_world, .random = m_random, .playerSkeleton = &m_skeleton, .soundRequest = [&](std::int32_t sound) {
             sounds.push_back(sound);
         }});
    ASSERT_TRUE(stepped.HasValue()) << (stepped.HasValue() ? "" : stepped.Error());
    EXPECT_EQ(sounds, (std::vector<std::int32_t>{9}));
    EXPECT_EQ(actor.firstFootstepSound, 1);
    EXPECT_EQ(actor.secondFootstepSound, 0);
}

TEST_F(CharacterMotionStepTest, FinishedBodyResetsBothFootstepLatches)
{
    auto& actor = Actor();
    actor.animation.action = 13;
    actor.animation.frame = 19.8f;
    actor.firstFootstepSound = 7;
    actor.secondFootstepSound = 9;
    const auto stepped = Step();
    ASSERT_TRUE(stepped.HasValue()) << (stepped.HasValue() ? "" : stepped.Error());
    EXPECT_EQ(actor.firstFootstepSound, 0);
    EXPECT_EQ(actor.secondFootstepSound, 0);
}
