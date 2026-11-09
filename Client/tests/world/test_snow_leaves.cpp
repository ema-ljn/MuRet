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
using Client::World::CreateNoriaAtlansLeaf;
using Client::World::CreateSnowLeaf;
using Client::World::Leaf;

// 52fd20/52fe30: draws CRT acreditados de semilla74, no salidas calculadas por el port.
TEST(SnowLeavesTest, SnowSelectsTheRareTypeBeforeFourMoreDraws)
{
    const std::int32_t map = 2;
    const glm::vec3 hero{1000, 2000, 3000};
    const float cameraY = 0;
    Leaf leaf;
    leaf.alive = 0x80;
    leaf.light = {3, 4, 5};
    leaf.spin = {6, 7, 8};
    leaf.start = {9, 10, 11};
    CrtRandom random(74);
    ASSERT_TRUE(CreateSnowLeaf(leaf, {map, hero, cameraY}, random));
    EXPECT_EQ(random.State(), 0x4825ce39u);
    EXPECT_EQ(leaf.type, 101);
    EXPECT_EQ(leaf.scale, 10);
    EXPECT_EQ(leaf.position, glm::vec3(869, 1767, 3250));
    EXPECT_EQ(leaf.angles, glm::vec3(-30, 0, 0));
    EXPECT_EQ(leaf.velocity.x, 0);
    EXPECT_LT(leaf.velocity.y, 0);
    EXPECT_LT(leaf.velocity.z, 0);
    EXPECT_EQ(leaf.alive, 0x80);
    EXPECT_EQ(leaf.light, glm::vec3(3, 4, 5));
    EXPECT_EQ(leaf.spin, glm::vec3(6, 7, 8));
    EXPECT_EQ(leaf.start, glm::vec3(9, 10, 11));
}

TEST(SnowLeavesTest, CommonSnowKeepsTheInitialTypeAndScale)
{
    const std::int32_t map = 2;
    const glm::vec3 hero{};
    const float cameraY = 0;
    Leaf leaf;
    CrtRandom random(0); // R1=38, resto10 distinto de cero.
    ASSERT_TRUE(CreateSnowLeaf(leaf, {map, hero, cameraY}, random));
    EXPECT_EQ(leaf.type, 100);
    EXPECT_EQ(leaf.scale, 5);
    EXPECT_EQ(random.State(), 0xa2974c77u);
}

TEST(SnowLeavesTest, BothOtherMapsUseTheNineDrawInitializationWithoutResettingFields)
{
    const glm::vec3 hero{1000, 2000, 3000};
    const float cameraY = 3000;
    for (const std::int32_t map : {3, 7})
    {
        Leaf leaf;
        leaf.alive = 0xff;
        leaf.scale = 2.5f;
        leaf.angles = {11, 22, 33};
        leaf.light = {3, 4, 5};
        CrtRandom random(74);
        ASSERT_TRUE(CreateNoriaAtlansLeaf(leaf, {map, hero, cameraY}, random));
        EXPECT_EQ(random.State(), 0x4db430cdu);
        EXPECT_EQ(leaf.type, 100);
        EXPECT_EQ(leaf.position, glm::vec3(480, 2769, 3117));
        EXPECT_EQ(leaf.start, leaf.position);
        // Reflexión contra q4 retenido: el spill prematuro da0x41366666.
        EXPECT_EQ(std::bit_cast<std::uint32_t>(leaf.velocity.x), 0x41366667u);
        EXPECT_EQ(leaf.velocity.y, -1.1f);
        EXPECT_EQ(leaf.velocity.z, -0.9000000357627869f);
        EXPECT_EQ(leaf.spin, glm::vec3(0.6000000238418579f, 1.899999976158142f, -0.4000000059604645f));
        EXPECT_EQ(leaf.alive, 0xff);
        EXPECT_EQ(leaf.scale, 2.5f);
        EXPECT_EQ(leaf.angles, glm::vec3(11, 22, 33));
        EXPECT_EQ(leaf.light, glm::vec3(3, 4, 5));
    }
}

TEST(SnowLeavesTest, EqualAndUnorderedCameraThresholdDoNotReflect)
{
    const std::int32_t map = 7;
    const glm::vec3 hero{1000, 2000, 3000};
    for (const float cameraY : {2369.0f, std::numeric_limits<float>::quiet_NaN()})
    {
        Leaf leaf;
        CrtRandom random(74);
        ASSERT_TRUE(CreateNoriaAtlansLeaf(leaf, {map, hero, cameraY}, random));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(leaf.velocity.x), 0xc1033333u);
    }
}

TEST(SnowLeavesTest, EveryOtherRawMapRejectsWithoutWritesOrDraws)
{
    const glm::vec3 hero{};
    const float cameraY = 0;
    for (std::int32_t map = -1; map <= 55; ++map)
    {
        Leaf leaf;
        leaf.type = -3;
        CrtRandom random(74);
        if (map != 2)
        {
            EXPECT_FALSE(CreateSnowLeaf(leaf, {map, hero, cameraY}, random)) << map;
            EXPECT_EQ(leaf.type, -3);
            EXPECT_EQ(random.State(), 74u);
        }
        if (map != 3 && map != 7)
        {
            EXPECT_FALSE(CreateNoriaAtlansLeaf(leaf, {map, hero, cameraY}, random)) << map;
            EXPECT_EQ(leaf.type, -3);
            EXPECT_EQ(random.State(), 74u);
        }
    }
    const std::int32_t map = 258;
    Leaf leaf;
    CrtRandom random(74);
    EXPECT_FALSE(CreateSnowLeaf(leaf, {map, hero, cameraY}, random));
    EXPECT_FALSE(CreateNoriaAtlansLeaf(leaf, {map, hero, cameraY}, random));
    EXPECT_EQ(random.State(), 74u);
}
} // namespace
