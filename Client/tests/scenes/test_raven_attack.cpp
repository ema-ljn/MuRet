#include "Pets/DarkSpirit.h"

#include "Crt/CrtRandom.h"
#include "Kernel/ActorSpirit.h"
#include "Math/OriginalAngles.h"
#include "Models/Skeleton.h"
#include "Particles/Particles.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <memory>

// The raven's attack states in 0x416a20 (CL9 milestone 4, 06 § El ataque del cuervo): the warning with the A7, 5, 2,
// 6, the +0x38d counter and the flight toward +8's target. *Static*, from the ASM: the real emulated code
// (tests/vectors/world55/raven.txt) only has states 0, 1, 3 and 4.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::DarkSpirit;
using Client::Pets::DarkSpiritOwner;
using Client::Pets::DarkSpiritTargets;

constexpr std::uint8_t Lorencia = 0;

Client::Models::Skeleton RavenSkeleton()
{
    Client::Models::Skeleton skeleton;
    skeleton.actions.assign(4, Client::Models::ActionInfo{.repeats = true, .keyCount = 4});
    return skeleton;
}

struct Raven
{
    Raven()
    {
        spirit.position = {1000.0f, 1000.0f, 300.0f};
        owner.position = {1000.0f, 1100.0f, 200.0f};
    }
    Client::Pets::DarkSpiritStepped Step(std::uint8_t map = Lorencia, const DarkSpiritTargets& targets = {})
    {
        return Client::Pets::StepDarkSpirit(spirit, skeleton, owner, map, *particles, 0.0f, random, 1, targets);
    }

    DarkSpirit spirit;
    DarkSpiritOwner owner;
    Client::Models::Skeleton skeleton = RavenSkeleton();
    std::unique_ptr<Client::Particles::Particles> particles = std::make_unique<Client::Particles::Particles>();
    CrtRandom random;
};

// 0x416a88..0x4176e6: with the owner hidden, in 2, 5 and 6, the warning: +0x3b0 at 0xffff, the A7 and state 4 with
// speed 3. In the safe zone it keeps descending (Land accelerates 1); outside, the zone sets it back to 0.
TEST(RavenAttackTest, AHiddenOwnerStopsTheAttack)
{
    for (const std::uint8_t state : {std::uint8_t{2}, std::uint8_t{5}, std::uint8_t{6}})
    {
        SCOPED_TRACE(int{state});
        Raven tested;
        tested.owner.flags = 0x4000;
        tested.owner.safeZone = 1;
        tested.spirit.state = state;
        tested.spirit.target3b0 = 9;
        tested.spirit.far = 7;
        const auto stepped = tested.Step();
        EXPECT_TRUE(stepped.stopsTheAttack);
        EXPECT_EQ(tested.spirit.target3b0, 0xffff);
        EXPECT_EQ(tested.spirit.state, 4);
        EXPECT_EQ(tested.spirit.speed, 4.0f);

        Raven outside;
        outside.owner.flags = 0x4000;
        outside.spirit.state = state;
        EXPECT_TRUE(outside.Step().stopsTheAttack);
        EXPECT_LE(outside.spirit.state, 1);
    }
}

// A hidden +0x3b0 target stops it the same, but only if it is among the 400; states 0, 1, 3 and 4 do not warn.
TEST(RavenAttackTest, AHiddenTargetStopsTheAttackOnlyInsideThePool)
{
    Raven inPool;
    inPool.spirit.state = 5;
    EXPECT_TRUE(inPool.Step(Lorencia, {.attackInPool = true, .attackFlags = 0x4000}).stopsTheAttack);
    Raven outOfPool;
    outOfPool.spirit.state = 5;
    EXPECT_FALSE(outOfPool.Step(Lorencia, {.attackInPool = false, .attackFlags = 0x4000}).stopsTheAttack);
    for (const std::uint8_t state : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{3}, std::uint8_t{4}})
    {
        Raven resting;
        resting.owner.flags = 0x4000;
        resting.spirit.state = state;
        EXPECT_FALSE(resting.Step().stopsTheAttack) << int{state};
    }
}

// 0x4179ef..0x417ae3: 5 advances (0, −speed, 0) rotated, turns toward +8's target 50 higher, raises the distance, the
// speed adds +0xf0 and +0xf0 adds 0.2; action 3 and +0x38d rises. No rand().
TEST(RavenAttackTest, TheAttackChargesTowardTheTarget)
{
    Raven tested;
    tested.spirit.state = 5;
    tested.spirit.byte1ad = 1;
    tested.spirit.speed = 20.0f;
    tested.spirit.floatF0 = 0.5f;
    tested.spirit.far = 3;
    const DarkSpiritTargets targets{.target = {1000.0f, 400.0f, 100.0f}};
    glm::vec3 expectedAngles{};
    const double distance =
        Client::Math::MoveToward({1000.0f, 980.0f, 300.0f}, expectedAngles, {1000.0f, 400.0f, 150.0f}, 20.0f);
    ASSERT_GE(distance, 20.0);

    const auto stepped = tested.Step(Lorencia, targets);

    EXPECT_FALSE(stepped.stopsTheAttack);
    EXPECT_EQ(tested.spirit.state, 5);
    EXPECT_EQ(tested.spirit.position, glm::vec3(1000.0f, 980.0f, 300.0f));
    EXPECT_EQ(tested.spirit.angles, expectedAngles);
    EXPECT_EQ(tested.spirit.far, 4);
    EXPECT_EQ(tested.spirit.speed, 20.5f);
    EXPECT_EQ(tested.spirit.floatF0, static_cast<float>(0.5 + static_cast<double>(0.2f)));
    EXPECT_EQ(tested.spirit.animation.action, 3);
    EXPECT_EQ(tested.spirit.byte38d, 1);
    EXPECT_EQ(tested.spirit.byte1ad, 1);
    // Only the cry's rand().
    CrtRandom expected;
    static_cast<void>(expected.Next());
    EXPECT_EQ(tested.random.State(), expected.State());
}

// 0x417a74..0x417aa6: under 20 it returns (2) with the pitch at −45 and the distance at 1; +0x1ad at 0 except with
// order 3. With the distance over 20, too.
TEST(RavenAttackTest, TheAttackReturnsNearTheTargetOrAfterTwentySteps)
{
    for (const std::uint8_t order : {std::uint8_t{0}, std::uint8_t{3}})
    {
        Raven tested;
        tested.spirit.state = 5;
        tested.spirit.byte1ad = 1;
        tested.spirit.order = order;
        tested.spirit.speed = 5.0f;
        tested.Step(Lorencia, {.target = {1000.0f, 995.0f, 250.0f}});
        EXPECT_EQ(tested.spirit.state, 2);
        EXPECT_EQ(tested.spirit.angles.x, -45.0f);
        EXPECT_EQ(tested.spirit.far, 1);
        EXPECT_EQ(tested.spirit.byte1ad, order == 3 ? 1 : 0);
    }
    Raven tired;
    tired.spirit.state = 5;
    tired.spirit.far = 21;
    tired.Step(Lorencia, {.target = {1000.0f, 0.0f, 0.0f}});
    EXPECT_EQ(tired.spirit.state, 2);
    Raven fresh;
    fresh.spirit.state = 5;
    fresh.spirit.far = 20;
    fresh.Step(Lorencia, {.target = {1000.0f, 0.0f, 0.0f}});
    EXPECT_EQ(fresh.spirit.state, 5);
}

// 0x417aec..0x417b26: returning, at 250 or more from the target (before advancing) it goes to 0; it brakes 1 and takes
// action 3.
TEST(RavenAttackTest, TheReturnFlightBrakesUntilItIsFar)
{
    Raven near;
    near.spirit.state = 2;
    near.spirit.speed = 10.0f;
    near.Step();
    EXPECT_EQ(near.spirit.state, 2);
    EXPECT_EQ(near.spirit.speed, 9.0f);
    EXPECT_EQ(near.spirit.animation.action, 3);
    EXPECT_EQ(near.spirit.byte38d, 0);

    Raven far;
    far.spirit.state = 2;
    far.owner.position = {1000.0f, 1250.0f, 200.0f};
    far.Step();
    EXPECT_EQ(far.spirit.state, 0);
}

// 0x417847..0x41789d: 6 turns 40 toward the +0x3b0 character and does not move; with 0xffff, nothing. +0x38d rises up
// to 15.
TEST(RavenAttackTest, TheShoutTurnsTowardTheAttackTarget)
{
    Raven tested;
    tested.spirit.state = 6;
    tested.spirit.target3b0 = 4;
    tested.spirit.byte38d = 14;
    const glm::vec3 victim{1500.0f, 1000.0f, 0.0f};
    const auto angle = static_cast<float>(Client::Math::CreateAngle(1000.0f, 1000.0f, victim.x, victim.y));
    const auto turned = static_cast<float>(Client::Math::TurnAngle(0.0f, angle, 40.0f));
    tested.Step(Lorencia, {.attackPosition = victim});
    EXPECT_EQ(tested.spirit.angles.z, turned);
    EXPECT_EQ(tested.spirit.position, glm::vec3(1000.0f, 1000.0f, 300.0f));
    EXPECT_EQ(tested.spirit.byte38d, 15);
    tested.Step(Lorencia, {.attackPosition = victim});
    EXPECT_EQ(tested.spirit.byte38d, 15);

    Raven still;
    still.spirit.state = 6;
    still.spirit.target3b0 = 0xffff;
    still.Step(Lorencia, {.attackPosition = victim});
    EXPECT_EQ(still.spirit.angles.z, 0.0f);
}

// 0x4177b1 and 0x417b33: with +0x1ad at 1, states 0 and 1 fly toward +8's target as they would toward the owner, with
// the target's height, and +0x1ad stays at 0. In Lorencia, Devias and the login map (350 above the target).
TEST(RavenAttackTest, TheFreeFlightFollowsTheTargetOfEight)
{
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}, std::uint8_t{0x37}})
    {
        SCOPED_TRACE(int{map});
        const glm::vec3 target{1300.0f, 900.0f, 50.0f};
        Raven following;
        following.spirit.byte1ad = 1;
        following.Step(map, {.target = target});

        Raven owned;
        owned.owner.position = target;
        owned.Step(map);

        EXPECT_EQ(following.spirit.byte1ad, 0);
        EXPECT_EQ(following.spirit.position, owned.spirit.position);
        EXPECT_EQ(following.spirit.angles, owned.spirit.angles);
        EXPECT_EQ(following.spirit.state, owned.spirit.state);
        EXPECT_EQ(following.random.State(), owned.random.State());
    }
}

// States 7 onward (the A8 type that wraps) only do the cry and the distance.
TEST(RavenAttackTest, AStateBeyondSixOnlyShoutsAndCounts)
{
    Raven tested;
    tested.spirit.state = 9;
    tested.Step();
    EXPECT_EQ(tested.spirit.state, 9);
    EXPECT_EQ(tested.spirit.position, glm::vec3(1000.0f, 1000.0f, 300.0f));
    EXPECT_EQ(tested.spirit.byte38d, 0);
}
} // namespace
