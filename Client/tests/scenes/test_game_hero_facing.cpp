#include "Scenes/GameHeroFacing.h"

#include "Core/Expected.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameMovementState.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace
{
struct IdleHero
{
    Client::Scenes::ActorState hero;
    Client::Scenes::GameMovementState movement;
    std::vector<std::uint8_t> directions;

    Client::Scenes::HeroFacingContext Context()
    {
        return {.hero = hero,
                .movement = movement,
                .mouseX = 420,
                .mouseY = 180,
                .sendFacing = [&](std::uint8_t direction) -> Client::Core::Expected<void, std::string>
                {
                    directions.push_back(direction);
                    return {};
                }};
    }
};

TEST(GameHeroFacingTest, TheFortiethIdleStepTurnsAndPublishesTheNewSector)
{
    IdleHero tested;
    tested.movement.movementTarget = 38;
    ASSERT_TRUE(Client::Scenes::StepHeroFacing(tested.Context()).HasValue());
    EXPECT_EQ(tested.movement.movementTarget, 39);
    EXPECT_FLOAT_EQ(tested.hero.angles.z, 0.0f);
    EXPECT_TRUE(tested.directions.empty());
    auto context = tested.Context();
    context.sendFacing = [&](std::uint8_t direction) -> Client::Core::Expected<void, std::string>
    {
        EXPECT_EQ(tested.movement.movementTarget, 0);
        EXPECT_EQ(tested.movement.arrivalYaw, 135);
        EXPECT_FLOAT_EQ(tested.hero.angles.z, 135.0f);
        tested.directions.push_back(direction);
        return {};
    };
    ASSERT_TRUE(Client::Scenes::StepHeroFacing(context).HasValue());
    EXPECT_EQ(tested.directions, (std::vector<std::uint8_t>{4}));
}

TEST(GameHeroFacingTest, EqualSectorsStillResetTheCounterAndPublishTheTargetYaw)
{
    IdleHero tested;
    tested.movement.movementTarget = 39;
    tested.hero.angles.z = 140.0f;
    ASSERT_TRUE(Client::Scenes::StepHeroFacing(tested.Context()).HasValue());
    EXPECT_EQ(tested.movement.movementTarget, 0);
    EXPECT_EQ(tested.movement.arrivalYaw, 135);
    EXPECT_FLOAT_EQ(tested.hero.angles.z, 140.0f);
    EXPECT_TRUE(tested.directions.empty());
}

TEST(GameHeroFacingTest, Action39PublishesWithoutTurningTheBody)
{
    IdleHero tested;
    tested.movement.movementTarget = 39;
    tested.hero.animation.action = 0x39;
    ASSERT_TRUE(Client::Scenes::StepHeroFacing(tested.Context()).HasValue());
    EXPECT_FLOAT_EQ(tested.hero.angles.z, 0.0f);
    EXPECT_EQ(tested.directions, (std::vector<std::uint8_t>{4}));
}

TEST(GameHeroFacingTest, WalkingAndInputInhibitionDoNotAdvanceTheIdleCounter)
{
    IdleHero tested;
    tested.movement.movementTarget = 39;
    tested.hero.movement = 1;
    ASSERT_TRUE(Client::Scenes::StepHeroFacing(tested.Context()).HasValue());
    EXPECT_EQ(tested.movement.movementTarget, 39);
    tested.hero.movement = 0;
    tested.movement.inputBlocked = true;
    ASSERT_TRUE(Client::Scenes::StepHeroFacing(tested.Context()).HasValue());
    EXPECT_EQ(tested.movement.movementTarget, 39);
    EXPECT_TRUE(tested.directions.empty());
}

TEST(GameHeroFacingTest, ExcludedActionsAndHeadGuardPreserveTheIncrementedCounter)
{
    for (const int action : {0xcb, 0xcc, 0xc5, 0xc7, 0x4d, 0x4f, 0x5a})
    {
        IdleHero tested;
        tested.movement.movementTarget = 39;
        tested.hero.animation.action = static_cast<std::uint8_t>(action);
        ASSERT_TRUE(Client::Scenes::StepHeroFacing(tested.Context()).HasValue());
        EXPECT_EQ(tested.movement.movementTarget, 40);
        EXPECT_TRUE(tested.directions.empty());
    }
    IdleHero tested;
    tested.movement.movementTarget = 39;
    auto context = tested.Context();
    context.headTurningAllowed = false;
    ASSERT_TRUE(Client::Scenes::StepHeroFacing(context).HasValue());
    EXPECT_EQ(tested.movement.movementTarget, 40);
    EXPECT_TRUE(tested.directions.empty());
}
TEST(GameHeroFacingTest, InhibitorsAdvanceButDoNotConsumeTheFacingAttempt)
{
    for (int guard = 0; guard < 4; ++guard)
    {
        IdleHero tested;
        tested.movement.movementTarget = 39;
        if (guard == 0)
            tested.hero.byte381 = 1;
        if (guard == 1)
            tested.hero.dying = 1;
        if (guard == 2)
            tested.movement.facingBlocked = 1;
        if (guard == 3)
            tested.movement.facingMode = 1;
        ASSERT_TRUE(Client::Scenes::StepHeroFacing(tested.Context()).HasValue());
        EXPECT_EQ(tested.movement.movementTarget, 40);
        EXPECT_TRUE(tested.directions.empty());
    }
}

TEST(GameHeroFacingTest, Window40000AllowsFacingEvenWithModeOne)
{
    IdleHero tested;
    tested.movement.movementTarget = 39;
    tested.movement.facingMode = 1;
    tested.movement.windowsMask = 0x40000;
    ASSERT_TRUE(Client::Scenes::StepHeroFacing(tested.Context()).HasValue());
    EXPECT_EQ(tested.movement.movementTarget, 0);
    EXPECT_EQ(tested.directions, (std::vector<std::uint8_t>{4}));
}

TEST(GameHeroFacingTest, TheCounterWrapsLikeTheNativeDword)
{
    IdleHero tested;
    tested.movement.movementTarget = std::numeric_limits<std::int32_t>::max();
    ASSERT_TRUE(Client::Scenes::StepHeroFacing(tested.Context()).HasValue());
    EXPECT_EQ(tested.movement.movementTarget, std::numeric_limits<std::int32_t>::min());
    EXPECT_TRUE(tested.directions.empty());
}

TEST(GameHeroFacingTest, MissingOrFailedPublicationPreservesTheAlreadyWrittenOrientation)
{
    IdleHero tested;
    tested.movement.movementTarget = 39;
    auto context = tested.Context();
    context.sendFacing = {};
    const auto missing = Client::Scenes::StepHeroFacing(context);
    EXPECT_FALSE(missing.HasValue());
    EXPECT_EQ(tested.movement.movementTarget, 0);
    EXPECT_EQ(tested.movement.arrivalYaw, 135);
    EXPECT_FLOAT_EQ(tested.hero.angles.z, 135.0f);
    tested.hero.angles.z = 0.0f;
    tested.movement.movementTarget = 39;
    context.sendFacing = [](std::uint8_t) -> Client::Core::Expected<void, std::string>
    { return Client::Core::Unexpected(std::string("wire failed")); };
    const auto failed = Client::Scenes::StepHeroFacing(context);
    ASSERT_FALSE(failed.HasValue());
    EXPECT_EQ(failed.Error(), "wire failed");
    EXPECT_FLOAT_EQ(tested.hero.angles.z, 135.0f);
}

TEST(GameHeroFacingTest, TheMouseCenterComesFromThePublishedWorldViewport)
{
    IdleHero tested;
    tested.movement.movementTarget = 39;
    auto context = tested.Context();
    context.centerX = 420;
    context.mouseY = 80;
    ASSERT_TRUE(Client::Scenes::StepHeroFacing(context).HasValue());
    EXPECT_EQ(tested.movement.arrivalYaw, 225);
    EXPECT_FLOAT_EQ(tested.hero.angles.z, 225.0f);
    EXPECT_EQ(tested.directions, (std::vector<std::uint8_t>{6}));
}

TEST(GameHeroFacingTest, TheSectorBoundaryKeepsTheRetainedReciprocal)
{
    IdleHero tested;
    tested.movement.movementTarget = 39;
    tested.hero.angles.z = 112.5f;
    ASSERT_TRUE(Client::Scenes::StepHeroFacing(tested.Context()).HasValue());
    EXPECT_TRUE(tested.directions.empty());
    EXPECT_FLOAT_EQ(tested.hero.angles.z, 112.5f);
    tested.movement.movementTarget = 39;
    tested.hero.angles.z = std::nextafter(112.5f, 0.0f);
    ASSERT_TRUE(Client::Scenes::StepHeroFacing(tested.Context()).HasValue());
    EXPECT_EQ(tested.directions, (std::vector<std::uint8_t>{4}));
    EXPECT_FLOAT_EQ(tested.hero.angles.z, 135.0f);
}

TEST(GameHeroFacingTest, InvalidBodyYawPreservesTheAlreadyPublishedTargetBeforeTheFrontier)
{
    IdleHero tested;
    tested.movement.movementTarget = 39;
    tested.hero.angles.z = std::numeric_limits<float>::infinity();
    const auto result = Client::Scenes::StepHeroFacing(tested.Context());
    EXPECT_FALSE(result.HasValue());
    EXPECT_EQ(tested.movement.movementTarget, 0);
    EXPECT_EQ(tested.movement.arrivalYaw, 135);
    EXPECT_TRUE(tested.directions.empty());
}

} // namespace
