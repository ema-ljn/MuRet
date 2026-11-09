#include "World/Leaves.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <system_error>

namespace
{
using Client::Core::Expected;
using Client::Render::Models::CrtRandom;
using Client::World::CreateEmberLeaf;
using Client::World::CreateMapZeroLeafC3;
using Client::World::Leaf;
using Client::World::LeafC3World;
using Client::World::LeafCreationWorld;
using Client::World::LeafRandomDraw;

TEST(EmberLeavesTest, LoginBypassesTheCastleFlagAndRectangle)
{
    const std::int32_t map = 55;
    const std::uint8_t enabled = 0;
    const glm::vec3 hero{1000, 2000, 3000};
    const float cameraY = 0;
    Leaf leaf;
    leaf.alive = 0x80;
    leaf.angles = {11, 22, 33};
    leaf.light = {7, 8, 9};
    CrtRandom random(74);
    ASSERT_TRUE(CreateEmberLeaf(leaf, {map, hero, cameraY}, enabled, random));
    EXPECT_EQ(random.State(), 0x8667805cu) << "diez draws, incluida escala";
    EXPECT_EQ(leaf.alive, 0x80);
    EXPECT_EQ(leaf.type, 0x635);
    EXPECT_EQ(leaf.scale, 0.8f);
    EXPECT_EQ(leaf.position, glm::vec3(869, 1767, 3700));
    EXPECT_EQ(leaf.start, glm::vec3(869, 1767, 3700));
    EXPECT_EQ(leaf.angles, glm::vec3(11, 22, 33));
    EXPECT_EQ(leaf.light, glm::vec3(1, 0, 0));
    EXPECT_EQ(leaf.velocity.x, -10.1f);
    EXPECT_EQ(leaf.spin.z, -0.1f);
}

TEST(EmberLeavesTest, CastleEmbersUseTheShortHeightSpanAndAnyNonzeroByte)
{
    const std::int32_t map = 30;
    const glm::vec3 hero{5000, 6000, 3000};
    const float cameraY = 0;
    for (const std::uint8_t enabled : std::array<std::uint8_t, 4>{1, 2, 0x80, 0xff})
    {
        Leaf leaf;
        leaf.alive = 0x80;
        leaf.angles = {11, 22, 33};
        CrtRandom random(74);
        ASSERT_TRUE(CreateEmberLeaf(leaf, {map, hero, cameraY}, enabled, random));
        EXPECT_EQ(random.State(), 0x8667805cu);
        EXPECT_EQ(leaf.position, glm::vec3(4869, 5767, 3200));
        EXPECT_EQ(leaf.start, glm::vec3(4869, 5767, 3200));
        EXPECT_EQ(leaf.light, glm::vec3(1, 0, 0));
        EXPECT_EQ(leaf.alive, 0x80);
        EXPECT_EQ(leaf.angles, glm::vec3(11, 22, 33));
    }
}

TEST(EmberLeavesTest, TheCastleRectangleIncludesAllFourEdges)
{
    const std::int32_t map = 30;
    const std::uint8_t enabled = 1;
    const float cameraY = 0;
    for (const float x : {4700.0f, 14800.0f})
    {
        for (const float y : {5300.0f, 23900.0f})
        {
            const glm::vec3 hero{x, y, 3000};
            Leaf leaf;
            CrtRandom random(74);
            EXPECT_TRUE(CreateEmberLeaf(leaf, {map, hero, cameraY}, enabled, random));
            EXPECT_EQ(random.State(), 0x8667805cu);
        }
    }
}

TEST(EmberLeavesTest, DisabledOrOutsideCastleDoesNotWriteOrConsumeRandom)
{
    const std::int32_t map = 30;
    std::uint8_t enabled = 1;
    const float cameraY = 0;
    for (const glm::vec3 hero : {glm::vec3(4699, 6000, 0), glm::vec3(14801, 6000, 0), glm::vec3(5000, 5299, 0),
                                 glm::vec3(5000, 23901, 0), glm::vec3(std::numeric_limits<float>::quiet_NaN(), 6000, 0),
                                 glm::vec3(5000, std::numeric_limits<float>::quiet_NaN(), 0),
                                 glm::vec3(std::numeric_limits<float>::infinity(), 6000, 0)})
    {
        Leaf leaf;
        leaf.type = -3;
        leaf.position = {10, 20, 30};
        CrtRandom random(74);
        EXPECT_FALSE(CreateEmberLeaf(leaf, {map, hero, cameraY}, enabled, random));
        EXPECT_EQ(random.State(), 74u);
        EXPECT_EQ(leaf.type, -3);
        EXPECT_EQ(leaf.position, glm::vec3(10, 20, 30));
    }
    const glm::vec3 hero{5000, 6000, 0};
    const LeafCreationWorld world{map, hero, cameraY};
    enabled = 0;
    Leaf leaf;
    CrtRandom random(74);
    EXPECT_FALSE(CreateEmberLeaf(leaf, world, enabled, random));
    EXPECT_EQ(random.State(), 74u);
}

TEST(EmberLeavesTest, EveryOtherRawMapRejects)
{
    const std::uint8_t enabled = 0xff;
    const glm::vec3 hero{5000, 6000, 0};
    const float cameraY = 0;
    for (std::int32_t map = -1; map <= 54; ++map)
    {
        if (map == 30)
            continue;
        Leaf leaf;
        leaf.type = -3;
        CrtRandom random(74);
        EXPECT_FALSE(CreateEmberLeaf(leaf, {map, hero, cameraY}, enabled, random)) << map;
        EXPECT_EQ(random.State(), 74u);
        EXPECT_EQ(leaf.type, -3);
    }
    for (const std::int32_t map : {286, 311})
    {
        Leaf leaf;
        CrtRandom random(74);
        EXPECT_FALSE(CreateEmberLeaf(leaf, {map, hero, cameraY}, enabled, random));
        EXPECT_EQ(random.State(), 74u);
    }
}

TEST(EmberLeavesTest, MapZeroCreatorUsesNineSignedDrawsAndWritesOnlyItsFields)
{
    const std::int32_t map = 0;
    const glm::vec3 hero{5000.0f, 6000.0f, 7.0f};
    const glm::vec3* heroPointer = &hero;
    const float cameraY = 0.0f;
    const float scale = -0.1f;
    const float threshold = 400.0f;
    const float mirror = 3.2f;
    const float step = 0.1f;
    const LeafC3World world{map, heroPointer, cameraY, scale, threshold, mirror, step};
    const std::array<std::int32_t, 9> draws{-1601, 1401, -301, 23, -33, 17, -17, 65, -17};
    std::size_t nextDraw = 0;
    const LeafRandomDraw random = [&]() -> Expected<std::int32_t, std::error_code> { return draws[nextDraw++]; };
    Leaf leaf;
    leaf.alive = 0x80;
    leaf.scale = 0.75f;
    leaf.angles = {1.0f, 2.0f, 3.0f};
    leaf.light = {4.0f, 5.0f, 6.0f};

    const auto result = CreateMapZeroLeafC3(leaf, world, random);

    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(result.Value(), 1);
    EXPECT_EQ(nextDraw, draws.size());
    EXPECT_EQ(leaf.type, 100);
    EXPECT_EQ(leaf.position, glm::vec3(4199.0f, 5501.0f, 56.0f));
    EXPECT_EQ(leaf.start, leaf.position);
    EXPECT_EQ(leaf.velocity.x, static_cast<float>(87.0 * static_cast<double>(scale)));
    EXPECT_FLOAT_EQ(leaf.velocity.y, static_cast<float>(-17.0 * static_cast<double>(step)));
    EXPECT_FLOAT_EQ(leaf.velocity.z, static_cast<float>(1.0 * static_cast<double>(step)));
    EXPECT_FLOAT_EQ(leaf.spin.x, static_cast<float>(-9.0 * static_cast<double>(step)));
    EXPECT_FLOAT_EQ(leaf.spin.z, static_cast<float>(-9.0 * static_cast<double>(step)));
    EXPECT_FLOAT_EQ(leaf.spin.y, static_cast<float>(-31.0 * static_cast<double>(step)));
    EXPECT_EQ(leaf.alive, 0x80);
    EXPECT_EQ(leaf.scale, 0.75f);
    EXPECT_EQ(leaf.angles, glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(leaf.light, glm::vec3(4.0f, 5.0f, 6.0f));
}

TEST(EmberLeavesTest, MapZeroCreatorRereadsHeroAfterThreeDrawsAndUsesRetainedVelocityForMirror)
{
    const std::int32_t map = 0;
    const glm::vec3 heroFirst{1.0f, 2.0f, 3.0f};
    const glm::vec3 heroSecond{101.0f, 102.0f, 103.0f};
    const glm::vec3 heroThird{201.0f, 202.0f, 203.0f};
    const glm::vec3* heroPointer = &heroFirst;
    const float cameraY = -100.0f;
    const float scale = -0.1f;
    const float threshold = 400.0f;
    const float mirror = 3.2f;
    const float step = 0.1f;
    const LeafC3World world{map, heroPointer, cameraY, scale, threshold, mirror, step};
    const std::array<std::int32_t, 9> draws{800, 500, 250, 0, 0, 31, 7, 63, 15};
    std::size_t nextDraw = 0;
    const LeafRandomDraw random = [&]() -> Expected<std::int32_t, std::error_code>
    {
        if (nextDraw == 0)
            heroPointer = &heroSecond;
        else if (nextDraw == 1)
            heroPointer = &heroThird;
        else if (nextDraw == 2)
            heroPointer = &heroFirst;
        return draws[nextDraw++];
    };
    Leaf leaf;

    const auto result = CreateMapZeroLeafC3(leaf, world, random);

    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(nextDraw, draws.size());
    EXPECT_EQ(leaf.position, glm::vec3(101.0f, 202.0f, 303.0f));
    EXPECT_EQ(leaf.start, leaf.position);
    const double retainedVelocity = 64.0 * static_cast<double>(scale);
    EXPECT_EQ(leaf.velocity.x, static_cast<float>(static_cast<double>(mirror) - retainedVelocity));
}

TEST(EmberLeavesTest, MapZeroCreatorReportsMissingRandomAtFirstReachedDrawAfterTypeStore)
{
    const std::int32_t map = 0;
    const glm::vec3 hero{1.0f, 2.0f, 3.0f};
    const glm::vec3* heroPointer = &hero;
    const float cameraY = 0.0f;
    const float scale = -0.1f;
    const float threshold = 400.0f;
    const float mirror = 3.2f;
    const float step = 0.1f;
    const LeafC3World world{map, heroPointer, cameraY, scale, threshold, mirror, step};
    Leaf leaf;
    leaf.type = -4;

    const auto result = CreateMapZeroLeafC3(leaf, world, {});

    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error(), std::make_error_code(std::errc::invalid_argument));
    EXPECT_EQ(leaf.type, 100);
    EXPECT_EQ(leaf.position, glm::vec3(0.0f));
}

TEST(EmberLeavesTest, MapZeroCreatorRejectsOtherMapsBeforePointerOrRandomAccess)
{
    const glm::vec3* heroPointer = nullptr;
    const float cameraY = 0.0f;
    const float scale = -0.1f;
    const float threshold = 400.0f;
    const float mirror = 3.2f;
    const float step = 0.1f;
    std::size_t nextDraw = 0;
    const LeafRandomDraw random = [&]() -> Expected<std::int32_t, std::error_code>
    {
        ++nextDraw;
        return 0;
    };

    for (std::int32_t map = -1; map <= 55; ++map)
    {
        if (map == 0)
            continue;
        const LeafC3World world{map, heroPointer, cameraY, scale, threshold, mirror, step};
        Leaf leaf;
        leaf.type = -4;
        leaf.position = {10.0f, 20.0f, 30.0f};

        const auto result = CreateMapZeroLeafC3(leaf, world, random);

        ASSERT_TRUE(result.HasValue()) << map;
        EXPECT_EQ(result.Value(), 0) << map;
        EXPECT_EQ(nextDraw, 0) << map;
        EXPECT_EQ(leaf.type, -4) << map;
        EXPECT_EQ(leaf.position, glm::vec3(10.0f, 20.0f, 30.0f)) << map;
    }
}

TEST(EmberLeavesTest, MapZeroCreatorKeepsCompletedCoordinateStoresWhenFourthDrawErrors)
{
    const std::int32_t map = 0;
    const glm::vec3 hero{5000.0f, 6000.0f, 7.0f};
    const glm::vec3* heroPointer = &hero;
    const float cameraY = 0.0f;
    const float scale = -0.1f;
    const float threshold = 400.0f;
    const float mirror = 3.2f;
    const float step = 0.1f;
    const LeafC3World world{map, heroPointer, cameraY, scale, threshold, mirror, step};
    std::size_t nextDraw = 0;
    const LeafRandomDraw random = [&]() -> Expected<std::int32_t, std::error_code>
    {
        ++nextDraw;
        if (nextDraw == 4)
            return Client::Core::Unexpected(std::make_error_code(std::errc::io_error));
        return 0;
    };
    Leaf leaf;
    leaf.velocity = {11.0f, 12.0f, 13.0f};

    const auto result = CreateMapZeroLeafC3(leaf, world, random);

    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error(), std::make_error_code(std::errc::io_error));
    EXPECT_EQ(nextDraw, 4);
    EXPECT_EQ(leaf.type, 100);
    EXPECT_EQ(leaf.position, glm::vec3(4200.0f, 5500.0f, 57.0f));
    EXPECT_EQ(leaf.start, leaf.position);
    EXPECT_EQ(leaf.velocity, glm::vec3(11.0f, 12.0f, 13.0f));
}

TEST(EmberLeavesTest, MapZeroCreatorDoesNotMirrorAtTheStrictCameraThreshold)
{
    const std::int32_t map = 0;
    const glm::vec3 hero{100.0f, 200.0f, 300.0f};
    const glm::vec3* heroPointer = &hero;
    const float cameraY = -200.0f;
    const float scale = -0.1f;
    const float threshold = 400.0f;
    const float mirror = 3.2f;
    const float step = 0.1f;
    const LeafC3World world{map, heroPointer, cameraY, scale, threshold, mirror, step};
    const std::array<std::int32_t, 9> draws{800, 500, 250, 0, 0, 0, 0, 0, 0};
    std::size_t nextDraw = 0;
    const LeafRandomDraw random = [&]() -> Expected<std::int32_t, std::error_code> { return draws[nextDraw++]; };
    Leaf leaf;

    const auto result = CreateMapZeroLeafC3(leaf, world, random);

    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(leaf.position.y, 200.0f);
    EXPECT_EQ(leaf.velocity.x, static_cast<float>(64.0 * static_cast<double>(scale)));
}

TEST(EmberLeavesTest, MapZeroCreatorReadsAliasedCameraAndLiveTuningAtTheirNativeStages)
{
    const std::int32_t map = 0;
    const glm::vec3 hero{100.0f, 100.0f, 100.0f};
    const glm::vec3* heroPointer = &hero;
    Leaf leaf;
    leaf.velocity.x = -1000.0f;
    const float& cameraY = leaf.velocity.x;
    float scale = -0.1f;
    const float threshold = 400.0f;
    float mirror = 3.2f;
    float step = 0.1f;
    const LeafC3World world{map, heroPointer, cameraY, scale, threshold, mirror, step};
    const std::array<std::int32_t, 9> draws{800, 500, 250, 0, 0, 31, 7, 63, 15};
    std::size_t nextDraw = 0;
    const LeafRandomDraw random = [&]() -> Expected<std::int32_t, std::error_code>
    {
        if (nextDraw == 3)
        {
            scale = -0.2f;
            mirror = 1.2f;
            leaf.position.y = 10000.0f;
        }
        else if (nextDraw == 4)
            step = 0.2f;
        else if (nextDraw == 5)
            step = 0.3f;
        else if (nextDraw == 6)
            step = 0.4f;
        else if (nextDraw == 7)
            step = 0.5f;
        else if (nextDraw == 8)
            step = 0.6f;
        return draws[nextDraw++];
    };

    const auto result = CreateMapZeroLeafC3(leaf, world, random);

    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(nextDraw, draws.size());
    EXPECT_EQ(leaf.position.y, 10000.0f);
    EXPECT_EQ(leaf.velocity.x, static_cast<float>(static_cast<double>(1.2f) - (64.0 * static_cast<double>(-0.2f))));
    EXPECT_EQ(leaf.velocity.y, static_cast<float>(-16.0 * static_cast<double>(0.2f)));
    EXPECT_EQ(leaf.velocity.z, static_cast<float>(15.0 * static_cast<double>(0.3f)));
    EXPECT_EQ(leaf.spin.x, static_cast<float>(-1.0 * static_cast<double>(0.4f)));
    EXPECT_EQ(leaf.spin.y, static_cast<float>(31.0 * static_cast<double>(0.5f)));
    EXPECT_EQ(leaf.spin.z, static_cast<float>(7.0 * static_cast<double>(0.6f)));
}
} // namespace
