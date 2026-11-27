#include "Crt/CrtRandom.h"
#include "Scenes/CastleSiegeBlessing.h"
#include "Kernel/ActorState.h"
#include "World/Particles.h"
#include <gtest/gtest.h>
#include <limits>
namespace
{
TEST(CastleSiegeBlessingTest, DefenderChecksAllFourPositionsAndIncludesTheExactRadius)
{
    Client::Kernel::ActorState actor;
    actor.flags78 = 0x40000;
    actor.position = {9780, 22700, 0};
    Client::World::Particles particles;
    Client::Crt::CrtRandom random(1);
    ASSERT_TRUE(Client::Scenes::DrawTheCastleBlessing(actor, 0, 0, 0, 10, particles, random).HasValue());
    EXPECT_TRUE(particles.All()[0].alive);
    Client::Crt::CrtRandom expected(1);
    expected.Discard(2);
    EXPECT_EQ(random.State(), expected.State());
    actor.position.x = 9781;
    ASSERT_TRUE(Client::Scenes::DrawTheCastleBlessing(actor, 0, 0, 0, 10, particles, random).HasValue());
    EXPECT_FALSE(particles.All()[1].alive);
    EXPECT_EQ(random.State(), expected.State());
}
TEST(CastleSiegeBlessingTest, AttackerNeedsBothMarksTheSameGuildAndTheTenthFrame)
{
    Client::Kernel::ActorState actor;
    actor.word1f2 = 7;
    actor.position = {1000, 2000, 0};
    Client::World::Particles particles;
    Client::Crt::CrtRandom random(1);
    for (auto test : {0, 1, 2, 3, 4})
    {
        ASSERT_TRUE(Client::Scenes::DrawTheCastleBlessing(
                        actor, test == 0 ? 6 : 7,
                        test == 1 ? 0.0f : (test == 4 ? std::numeric_limits<float>::quiet_NaN() : 1000.0f),
                        test == 2 ? 0.0f : 2000.0f, test == 3 ? 9 : 10, particles, random)
                        .HasValue());
        EXPECT_FALSE(particles.All()[0].alive);
        EXPECT_EQ(random.State(), 1u);
    }
    ASSERT_TRUE(Client::Scenes::DrawTheCastleBlessing(actor, 7, 1000, 2000, 10, particles, random).HasValue());
    EXPECT_TRUE(particles.All()[0].alive);
}
TEST(CastleSiegeBlessingTest, UnorderedDistanceFollowsTheNativeComparison)
{
    Client::Kernel::ActorState actor;
    actor.flags78 = 0x40000;
    actor.position.x = std::numeric_limits<float>::quiet_NaN();
    Client::World::Particles particles;
    Client::Crt::CrtRandom random(1);
    ASSERT_TRUE(Client::Scenes::DrawTheCastleBlessing(actor, 0, 0, 0, 0, particles, random).HasValue());
    EXPECT_TRUE(particles.All()[0].alive);
}
} // namespace
