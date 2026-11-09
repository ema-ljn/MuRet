#include "World/Leaves.h"

#include "Render/Models/Cloth.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <limits>

namespace
{
using Client::Render::Models::CrtRandom;
using Client::World::CreateLorenciaLeaf;
using Client::World::Leaf;
using Client::World::LeafCreationWorld;
using Client::World::StepDriftingLeaf;

std::uint32_t Bits(float value)
{
    return std::bit_cast<std::uint32_t>(value);
}

TEST(LorenciaLeavesTest, CreationConsumesNineDrawsAndPreservesDispatcherFields)
{
    const std::int32_t map = 0;
    const glm::vec3 hero{1000, 2000, 3000};
    const float cameraY = 0;
    Leaf leaf;
    leaf.type = -123;
    leaf.scale = 2.5f;
    leaf.angles = {11, -22, 33};
    leaf.light = {0.25f, 0.5f, 0.75f};
    CrtRandom random(74);
    ASSERT_TRUE(CreateLorenciaLeaf(leaf, LeafCreationWorld{map, hero, cameraY}, random));
    // Oráculo racional independiente de52fa60 y CRT, sin ejecutar el port; evidencia derive-lorencia-leaf.py.
    EXPECT_EQ(random.State(), 0x4db430cdu);
    EXPECT_EQ(leaf.type, 100);
    EXPECT_EQ(leaf.position, glm::vec3(480, 2769, 3117));
    EXPECT_EQ(leaf.start, glm::vec3(480, 2769, 3117));
    EXPECT_EQ(Bits(leaf.velocity.x), 0xc1033333u);
    EXPECT_EQ(Bits(leaf.velocity.y), 0xbf8ccccdu);
    EXPECT_EQ(Bits(leaf.velocity.z), 0xbf666667u);
    EXPECT_EQ(Bits(leaf.spin.x), 0x3f19999au);
    EXPECT_EQ(Bits(leaf.spin.y), 0x3ff33333u);
    EXPECT_EQ(Bits(leaf.spin.z), 0xbecccccdu);
    EXPECT_FALSE(leaf.alive);
    EXPECT_EQ(leaf.scale, 2.5f);
    EXPECT_EQ(leaf.angles, glm::vec3(11, -22, 33));
    EXPECT_EQ(leaf.light, glm::vec3(0.25f, 0.5f, 0.75f));
}

TEST(LorenciaLeavesTest, MirroringUsesTheUnroundedSpeed)
{
    const std::int32_t map = 0;
    const glm::vec3 hero{1000, 2000, 3000};
    const float cameraY = 2370;
    CrtRandom random(74);
    Leaf leaf;
    ASSERT_TRUE(CreateLorenciaLeaf(leaf, LeafCreationWorld{map, hero, cameraY}, random));
    // F32(3.2f - 82*-0.1f) difiere1ULP de usar el primer store F32(-8.2...).
    EXPECT_EQ(Bits(leaf.velocity.x), 0x41366667u);
    EXPECT_EQ(random.State(), 0x4db430cdu) << "el branch no sortea otra vez";
}

TEST(LorenciaLeavesTest, TheCameraThresholdIsStrictAndUnorderedDoesNotMirror)
{
    const std::int32_t map = 0;
    const glm::vec3 hero{1000, 2000, 3000};
    for (const float cameraY : {2368.0f, 2369.0f, std::numeric_limits<float>::quiet_NaN()})
    {
        CrtRandom random(74);
        Leaf leaf;
        ASSERT_TRUE(CreateLorenciaLeaf(leaf, LeafCreationWorld{map, hero, cameraY}, random));
        EXPECT_EQ(Bits(leaf.velocity.x), 0xc1033333u);
        EXPECT_EQ(random.State(), 0x4db430cdu);
    }
}

TEST(LorenciaLeavesTest, AllOtherMapsRejectWithoutDrawsOrWrites)
{
    const glm::vec3 hero{1000, 2000, 3000};
    const float cameraY = 2370;
    for (const std::int32_t map : {-1, 1, 9, 10, 18, 23, 30, 34, 36, 54, 55, 256})
    {
        CrtRandom random(74);
        Leaf leaf;
        leaf.alive = true;
        leaf.type = -123;
        leaf.position = {10, 20, 30};
        leaf.start = {-10, -20, -30};
        leaf.velocity = {1, 2, 3};
        leaf.spin = {-1, -2, -3};
        EXPECT_FALSE(CreateLorenciaLeaf(leaf, LeafCreationWorld{map, hero, cameraY}, random)) << map;
        EXPECT_EQ(random.State(), 74u);
        EXPECT_TRUE(leaf.alive);
        EXPECT_EQ(leaf.type, -123);
        EXPECT_EQ(leaf.position, glm::vec3(10, 20, 30));
        EXPECT_EQ(leaf.start, glm::vec3(-10, -20, -30));
        EXPECT_EQ(leaf.velocity, glm::vec3(1, 2, 3));
        EXPECT_EQ(leaf.spin, glm::vec3(-1, -2, -3));
    }
}

TEST(LorenciaLeavesTest, TheCreationContextBorrowsLiveInputs)
{
    std::int32_t map = 1;
    glm::vec3 hero{0, 0, 0};
    float cameraY = 0;
    const LeafCreationWorld world{map, hero, cameraY};
    map = 0;
    hero = {1000, 2000, 3000};
    cameraY = 2370;
    CrtRandom random(74);
    Leaf leaf;
    ASSERT_TRUE(CreateLorenciaLeaf(leaf, world, random));
    EXPECT_EQ(leaf.position, glm::vec3(480, 2769, 3117));
    EXPECT_EQ(Bits(leaf.velocity.x), 0x41366667u);
}
TEST(DriftingLeavesTest, SixDrawsKeepBothUnroundedZAccumulators)
{
    Leaf leaf;
    leaf.alive = true;
    leaf.type = -123;
    leaf.scale = 3.5f;
    leaf.light = {0.25f, 0.5f, 0.75f};
    leaf.position = {100, 200, -2.5f};
    leaf.start = {100, 200, -2.5f};
    leaf.velocity = {1, 2, 3};
    leaf.spin = {0, 1, -3};
    leaf.angles = {10, 20, 2.94f};
    CrtRandom random(74);
    StepDriftingLeaf(leaf, random);
    // Oráculo racional de5301a0, evidencia derive-drifting-leaf.py: las Z finales no son cero.
    EXPECT_EQ(random.State(), 0x9d275818u);
    EXPECT_EQ(Bits(leaf.velocity.x), 0x3f800000u);
    EXPECT_EQ(Bits(leaf.velocity.y), 0x40200000u);
    EXPECT_EQ(Bits(leaf.velocity.z), 0x40200000u);
    EXPECT_EQ(Bits(leaf.position.x), 0x42ca0000u);
    EXPECT_EQ(Bits(leaf.position.y), 0x434a8000u);
    EXPECT_EQ(Bits(leaf.position.z), 0xb2000000u);
    EXPECT_EQ(Bits(leaf.spin.x), 0xbd23d70au);
    EXPECT_EQ(Bits(leaf.spin.y), 0x3f70a3d7u);
    EXPECT_EQ(Bits(leaf.spin.z), 0xc03c28f6u);
    EXPECT_EQ(Bits(leaf.angles.x), 0x411f5c29u);
    EXPECT_EQ(Bits(leaf.angles.y), 0x41a7851fu);
    EXPECT_EQ(Bits(leaf.angles.z), 0x33700000u);
    EXPECT_TRUE(leaf.alive);
    EXPECT_EQ(leaf.type, -123);
    EXPECT_EQ(leaf.scale, 3.5f);
    EXPECT_EQ(leaf.light, glm::vec3(0.25f, 0.5f, 0.75f));
    EXPECT_EQ(leaf.start, glm::vec3(100, 200, -2.5f));
}

TEST(DriftingLeavesTest, DistanceThresholdIncludesEqualityAndLeavesUnorderedAlive)
{
    struct Case
    {
        float x;
        bool alive;
    };
    const Case cases[]{{199, true}, {200, false}, {201, false}, {std::numeric_limits<float>::quiet_NaN(), true}};
    for (const Case& sample : cases)
    {
        Leaf leaf;
        leaf.alive = true;
        leaf.position = {sample.x, 400, 2000};
        leaf.start = {0, 0, 2000};
        leaf.velocity = {0, -0.5f, 0.5f};
        CrtRandom random(74);
        StepDriftingLeaf(leaf, random);
        EXPECT_EQ(leaf.alive, sample.alive) << sample.x;
        EXPECT_EQ(random.State(), 0x9d275818u) << "la baja no saltea draws";
    }
}

TEST(DriftingLeavesTest, ADeadSlotStillConsumesAndAdvancesWithoutReactivation)
{
    Leaf leaf;
    leaf.alive = false;
    leaf.position = {100, 200, -2.5f};
    leaf.start = leaf.position;
    leaf.velocity = {1, 2, 3};
    CrtRandom random(74);
    StepDriftingLeaf(leaf, random);
    EXPECT_FALSE(leaf.alive);
    EXPECT_EQ(random.State(), 0x9d275818u);
    EXPECT_EQ(Bits(leaf.position.z), 0xb2000000u);
}
} // namespace
