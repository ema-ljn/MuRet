#include "World/Leaves.h"

#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Render/Models/Cloth.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <limits>
#include <memory>

namespace
{
using Client::Render::Models::CrtRandom;
using Client::World::Leaf;
using Client::World::LeafTerrainWorld;
using Client::World::StepFallingLeaf;

struct Ground
{
    std::unique_ptr<Client::Data::Terrain::TerrainHeights> heights =
        std::make_unique<Client::Data::Terrain::TerrainHeights>();
    std::unique_ptr<Client::Data::Terrain::TerrainAttributes> attributes =
        std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    std::int32_t scene = 5;
    float fixedHeight = 1200;

    explicit Ground(float height = 100)
    {
        heights->cells.fill(height);
    }

    LeafTerrainWorld World() const
    {
        return {scene, *heights, *attributes, fixedHeight};
    }
};

TEST(FallingLeavesTest, AirUsesThreeDrawsAndTheUnroundedZIncrement)
{
    Ground ground(-100);
    Leaf leaf;
    leaf.alive = true;
    leaf.type = -3;
    leaf.position = {100, 200, -2.5f};
    leaf.velocity = {1, 2, 3};
    leaf.light = {0.25f, 0.5f, 0.75f};
    leaf.angles = {11, 22, 33};
    leaf.spin = {-1, -2, -3};
    leaf.start = {7, 8, 9};
    CrtRandom random(74);
    StepFallingLeaf(leaf, ground.World(), random);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(leaf.position.x), 0x42ca0000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(leaf.position.y), 0x434a8000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(leaf.position.z), 0xb2000000u);
    EXPECT_EQ(leaf.velocity, glm::vec3(1, 2.5f, 2.5f));
    EXPECT_EQ(random.State(), 0xfed30b87u);
    EXPECT_TRUE(leaf.alive);
    EXPECT_EQ(leaf.type, -3);
    EXPECT_EQ(leaf.light, glm::vec3(0.25f, 0.5f, 0.75f));
    EXPECT_EQ(leaf.angles, glm::vec3(11, 22, 33));
    EXPECT_EQ(leaf.spin, glm::vec3(-1, -2, -3));
    EXPECT_EQ(leaf.start, glm::vec3(7, 8, 9));
}

TEST(FallingLeavesTest, EqualityLandsWithoutRandomDrawsAndFadesAllChannels)
{
    Ground ground;
    Leaf leaf;
    leaf.alive = true;
    leaf.position = {100, 200, 100};
    leaf.velocity = {9, 8, 7};
    leaf.light = {0.05f, -0.2f, 0.75f};
    CrtRandom random(74);
    StepFallingLeaf(leaf, ground.World(), random);
    EXPECT_EQ(random.State(), 74u);
    EXPECT_FALSE(leaf.alive);
    EXPECT_EQ(leaf.position, glm::vec3(100, 200, 100));
    EXPECT_EQ(leaf.light.x, 0.0f);
    EXPECT_EQ(leaf.light.y, -0.25f);
    EXPECT_EQ(leaf.light.z, 0.7f);
    EXPECT_EQ(leaf.velocity, glm::vec3(9, 8, 7));
}

TEST(FallingLeavesTest, PositiveRedRetainsTheExactActiveByte)
{
    Ground ground;
    Leaf leaf;
    leaf.alive = 0x80;
    leaf.position = {100, 200, 99};
    leaf.light = {1, -2, -3};
    CrtRandom random(74);
    StepFallingLeaf(leaf, ground.World(), random);
    EXPECT_EQ(leaf.alive, 0x80);
    EXPECT_EQ(leaf.position.z, 100);
    EXPECT_EQ(leaf.light.x, 0.95f);
    EXPECT_EQ(random.State(), 74u);
}

TEST(FallingLeavesTest, UnorderedHeightOrPositionSelectsMovement)
{
    Ground ground(std::numeric_limits<float>::quiet_NaN());
    Leaf leaf;
    leaf.alive = true;
    leaf.position = {100, 200, 100};
    CrtRandom random(74);
    StepFallingLeaf(leaf, ground.World(), random);
    EXPECT_EQ(random.State(), 0xfed30b87u);
    EXPECT_TRUE(leaf.alive);
    ground.heights->cells.fill(100);
    leaf.position.z = std::numeric_limits<float>::quiet_NaN();
    random = CrtRandom(74);
    StepFallingLeaf(leaf, ground.World(), random);
    EXPECT_EQ(random.State(), 0xfed30b87u);
    EXPECT_TRUE(leaf.alive);
}

TEST(FallingLeavesTest, UnorderedRedClearsAfterAllThreeStores)
{
    Ground ground;
    Leaf leaf;
    leaf.alive = true;
    leaf.position = {100, 200, 0};
    leaf.light = {std::numeric_limits<float>::quiet_NaN(), 0.05f, 0.1f};
    CrtRandom random(74);
    StepFallingLeaf(leaf, ground.World(), random);
    EXPECT_FALSE(leaf.alive);
    EXPECT_EQ(leaf.light.y, 0.0f);
    EXPECT_EQ(leaf.light.z, 0.05f);
    EXPECT_EQ(random.State(), 74u);
}

TEST(FallingLeavesTest, HeightReadsTheSceneIdAndNotTheMap)
{
    Ground ground(1000);
    for (const std::int32_t scene : {0, 1, 3})
    {
        ground.scene = scene;
        Leaf leaf;
        leaf.position = {100, 200, 10};
        CrtRandom random(74);
        StepFallingLeaf(leaf, ground.World(), random);
        EXPECT_EQ(random.State(), 0xfed30b87u) << scene;
    }
    for (const std::int32_t scene : {-1, 2, 4, 5, 256})
    {
        ground.scene = scene;
        Leaf leaf;
        leaf.position = {100, 200, 10};
        leaf.light = glm::vec3(1);
        CrtRandom random(74);
        StepFallingLeaf(leaf, ground.World(), random);
        EXPECT_EQ(random.State(), 74u) << scene;
        EXPECT_EQ(leaf.position.z, 1000) << scene;
    }
}

TEST(FallingLeavesTest, TerrainAndFixedHeightAreBorrowedAndStayLive)
{
    Ground ground;
    ground.attributes->cells.fill(0x40);
    const LeafTerrainWorld world = ground.World();
    ground.fixedHeight = 1500;
    Leaf leaf;
    leaf.position = {100, 200, 1300};
    leaf.light = glm::vec3(1);
    CrtRandom random(74);
    StepFallingLeaf(leaf, world, random);
    EXPECT_EQ(leaf.position.z, 1500);
    EXPECT_EQ(random.State(), 74u);
    ground.attributes->cells.fill(0);
    ground.heights->cells.fill(1700);
    StepFallingLeaf(leaf, world, random);
    EXPECT_EQ(leaf.position.z, 1700);
}
} // namespace
