#include "World/GameCamera.h"

#include "Render/Pass3D.h"
#include "World/TerrainCulling.h"

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>

TEST(LeafCameraBindingTest, PublishesOnlyTheActualDrawAndKeepsItWhenTheHeroMovesWithoutDrawing)
{
    Client::World::GameCamera state;
    Client::World::GameCameraInput input{
        {14350.0f, 12050.0f, 165.0f}, 0x01, 0, Client::World::CullingScene::Game, 0.0f, 0.0f, false, {}};
    EXPECT_EQ(std::bit_cast<std::uint32_t>(state.publishedY), 0u);
    static_cast<void>(Client::World::GameCameraOf(state, input));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(state.publishedY), 0u);
    const auto first = Client::World::StepGameCamera(state, input);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(first.position.y), 0x46313b93u); // Literal Lorencia del research06.
    EXPECT_EQ(std::bit_cast<std::uint32_t>(state.publishedY), 0x46313b93u);
    input.hero.y += 256;
    for (int repeat = 0; repeat < 2; ++repeat)
    {
        const auto reconstructed = Client::World::GameCameraOf(state, input);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(reconstructed.position.y), 0x46353b93u);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(state.publishedY), 0x46313b93u);
    }
    static_cast<void>(Client::World::StepGameCamera(state, input));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(state.publishedY), 0x46353b93u);
}

TEST(LeafCameraBindingTest, PublishesTheReturnedPositionAfterZoomWithoutAnotherCalculation)
{
    Client::World::GameCamera state;
    Client::World::GameCameraInput input{.hero = {14350, 12050, 165}, .cellAttribute = 0x80};
    for (int repeat = 0; repeat < 22; ++repeat)
    {
        const auto result = Client::World::StepGameCamera(state, input);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(state.publishedY), std::bit_cast<std::uint32_t>(result.position.y));
    }
}
