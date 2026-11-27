#include "Crt/CrtRandom.h"
#include "Actors/Drawing/GameItemRenderState.h"
#include <gtest/gtest.h>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
namespace
{
TEST(GameItemRenderStateTest, StartsWithTheNativeBssAndTheSeparateDataPulse)
{
    const Client::Actors::Drawing::GameItemRenderState state;
    EXPECT_FALSE(state.initialized);
    EXPECT_EQ(state.glowPulse, 0);
    EXPECT_TRUE(state.selections.empty());
    EXPECT_EQ(state.specialPulse.x, 0.5f);
    EXPECT_EQ(state.specialPulse.y, 0.01f);
    for (const auto angle : state.floorAngles)
        EXPECT_EQ(angle, 0);
}
TEST(GameItemRenderStateTest, WinMainPublishesOneHundredAnglesAndDoesNotReseedOnReentry)
{
    Client::Actors::Drawing::GameItemRenderState state;
    Client::Crt::CrtRandom random(1);
    Client::Actors::Drawing::InitializeGameItemRenderState(state, random);
    ASSERT_TRUE(state.initialized);
    constexpr std::array<std::int32_t, 6> prefix{41, 107, 214, 220, 89, 244};
    for (std::size_t i = 0; i < prefix.size(); ++i)
        EXPECT_EQ(state.floorAngles[i], prefix[i]);
    EXPECT_EQ(state.floorAngles[50], 347);
    EXPECT_EQ(state.floorAngles[99], 341);
    EXPECT_EQ(random.State(), 0x409dcdf5u);
    EXPECT_EQ(state.glowPulse, 0);
    const auto angles = state.floorAngles;
    Client::Actors::Drawing::InitializeGameItemRenderState(state, random);
    EXPECT_EQ(state.floorAngles, angles);
    EXPECT_EQ(random.State(), 0x409dcdf5u);
}
TEST(GameItemRenderStateTest, TheFramePublisherUsesTheLiteralRdataCoefficients)
{
    Client::Actors::Drawing::GameItemRenderState state;
    Client::Actors::Drawing::UpdateGameItemGlowPulse(state, 0);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(state.glowPulse), 0x3f19999au);
    Client::Actors::Drawing::UpdateGameItemGlowPulse(state, 1000);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(state.glowPulse), 0x3ef913dcu);
    EXPECT_EQ(state.specialPulse.x, 0.5f);
    EXPECT_EQ(state.specialPulse.y, 0.01f);
}
} // namespace
