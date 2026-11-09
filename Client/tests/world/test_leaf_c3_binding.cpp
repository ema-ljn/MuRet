#include "World/Leaves.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <system_error>
#include <vector>

TEST(LeafC3BindingTest, ResolvesTheCanonicalIndexAfterEachOfTheFirstThreeDrawsAndReadsCameraAfterFour)
{
    Client::Scenes::WorldState world;
    world.Runtime().map = 0;
    world.Actors()[1].position = {1000, -100, -200};
    world.Actors()[2].position = {-300, 2000, -400};
    world.Actors()[3].position = {-500, -600, 3000};
    Client::Render::Models::CrtRandom random(74);
    float cameraY = 0;
    std::size_t draws = 0;
    std::vector<int> events;
    const Client::World::LeafRandomDraw draw = [&]() -> Client::Core::Expected<std::int32_t, std::error_code>
    {
        ++draws;
        events.push_back(static_cast<int>(draws));
        if (draws <= 3)
            world.Runtime().hero = draws;
        if (draws == 4)
            cameraY = 2370;
        return random.Next();
    };
    const Client::World::LeafHeroRead read = [&]() -> Client::Core::Expected<const glm::vec3*, std::error_code>
    {
        events.push_back(10 + static_cast<int>(world.Runtime().hero));
        return &world.Actors()[world.Runtime().hero].position;
    };
    Client::World::Leaf leaf;
    leaf.alive = 0x80;
    leaf.scale = 2.5f;
    leaf.light = {0.25f, 0.5f, 0.75f};
    const auto result = Client::World::CreateMapZeroLeafC3(leaf, world.Runtime().map, &cameraY, read, draw);
    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(result.Value(), 1);
    EXPECT_EQ(events, (std::vector<int>{1, 11, 2, 12, 3, 13, 4, 5, 6, 7, 8, 9}));
    EXPECT_EQ(random.State(), 0x4db430cdu);
    EXPECT_EQ(leaf.position, glm::vec3(480, 2769, 3117));
    EXPECT_EQ(leaf.start, glm::vec3(480, 2769, 3117));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(leaf.velocity.x), 0x41366667u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(leaf.velocity.y), 0xbf8ccccdu);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(leaf.spin.z), 0xbecccccdu);
    EXPECT_EQ(leaf.alive, 0x80);
    EXPECT_EQ(leaf.scale, 2.5f);
    EXPECT_EQ(leaf.light, glm::vec3(0.25f, 0.5f, 0.75f));
}

TEST(LeafC3BindingTest, AReachedHeroFailurePreservesTheTypeStoreButCutsTheSecondDrawSuffix)
{
    const std::int32_t map = 0;
    const float cameraY = 0;
    const glm::vec3 hero{1000, 2000, 3000};
    std::size_t draws = 0;
    const Client::World::LeafRandomDraw draw = [&]() -> Client::Core::Expected<std::int32_t, std::error_code>
    {
        ++draws;
        return 0;
    };
    const auto error = std::make_error_code(std::errc::result_out_of_range);
    const Client::World::LeafHeroRead read = [&]() -> Client::Core::Expected<const glm::vec3*, std::error_code>
    {
        if (draws == 2)
            return Client::Core::Unexpected(error);
        return &hero;
    };
    Client::World::Leaf leaf;
    leaf.type = -1;
    leaf.position = {11, 22, 33};
    const auto result = Client::World::CreateMapZeroLeafC3(leaf, map, &cameraY, read, draw);
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error(), error);
    EXPECT_EQ(draws, 2u);
    EXPECT_EQ(leaf.type, 100);
    EXPECT_EQ(leaf.position, glm::vec3(11, 22, 33));
}

TEST(LeafC3BindingTest, MissingCameraFailsAfterTheFourthDrawAndFirstVelocityStore)
{
    const std::int32_t map = 0;
    const glm::vec3 hero{1000, 2000, 3000};
    std::size_t draws = 0;
    const Client::World::LeafRandomDraw draw = [&]() -> Client::Core::Expected<std::int32_t, std::error_code>
    {
        ++draws;
        return 0;
    };
    const Client::World::LeafHeroRead read = [&]() -> Client::Core::Expected<const glm::vec3*, std::error_code>
    { return &hero; };
    Client::World::Leaf leaf;
    leaf.velocity.y = 77;
    const auto result = Client::World::CreateMapZeroLeafC3(leaf, map, nullptr, read, draw);
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error(), std::make_error_code(std::errc::invalid_argument));
    EXPECT_EQ(draws, 4u);
    EXPECT_EQ(leaf.type, 100);
    EXPECT_EQ(leaf.position, glm::vec3(200, 1500, 3050));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(leaf.velocity.x), 0xc0cccccdu);
    EXPECT_EQ(leaf.velocity.y, 77);
}
