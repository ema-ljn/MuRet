#include "Scenes/CastleSiegeBlessing.h"
#include "Scenes/ActorState.h"
#include "World/Particles.h"
#include "Render/Models/Cloth.h"
#include <gtest/gtest.h>
#include <limits>
namespace
{
TEST(CastleSiegeBlessingTest, DefenderChecksAllFourPositionsAndIncludesTheExactRadius)
{
    Client::Scenes::ActorState actor;
    actor.flags78 = 0x40000;
    actor.position = {9780, 22700, 0};
    Client::World::Particles particles;
    Client::Render::Models::CrtRandom random(1);
    ASSERT_TRUE(Client::Scenes::DrawTheCastleBlessing(actor, 0, 0, 0, 10, particles, random).HasValue());
    EXPECT_TRUE(particles.All()[0].alive);
    Client::Render::Models::CrtRandom expected(1);
    expected.Discard(2);
    EXPECT_EQ(random.State(), expected.State());
    actor.position.x = 9781;
    ASSERT_TRUE(Client::Scenes::DrawTheCastleBlessing(actor, 0, 0, 0, 10, particles, random).HasValue());
    EXPECT_FALSE(particles.All()[1].alive);
    EXPECT_EQ(random.State(), expected.State());
}
TEST(CastleSiegeBlessingTest, AttackerNeedsBothMarksTheSameGuildAndTheTenthFrame)
{
    Client::Scenes::ActorState actor;
    actor.word1f2 = 7;
    actor.position = {1000, 2000, 0};
    Client::World::Particles particles;
    Client::Render::Models::CrtRandom random(1);
    for (auto test : {0, 1, 2, 3})
    {
        ASSERT_TRUE(Client::Scenes::DrawTheCastleBlessing(actor, test == 0 ? 6 : 7, test == 1 ? 0 : 1000,
                                                          test == 2 ? 0 : 2000, test == 3 ? 9 : 10, particles, random)
                        .HasValue());
        EXPECT_FALSE(particles.All()[0].alive);
        EXPECT_EQ(random.State(), 1u);
    }
    ASSERT_TRUE(Client::Scenes::DrawTheCastleBlessing(actor, 7, 1000, 2000, 10, particles, random).HasValue());
    EXPECT_TRUE(particles.All()[0].alive);
}
TEST(CastleSiegeBlessingTest, UnorderedDistanceFollowsTheNativeComparison)
{
    Client::Scenes::ActorState actor;
    actor.flags78 = 0x40000;
    actor.position.x = std::numeric_limits<float>::quiet_NaN();
    Client::World::Particles particles;
    Client::Render::Models::CrtRandom random(1);
    ASSERT_TRUE(Client::Scenes::DrawTheCastleBlessing(actor, 0, 0, 0, 0, particles, random).HasValue());
    EXPECT_TRUE(particles.All()[0].alive);
}
} // namespace
