#include "Joints/FaunaJointCreation.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Models/Cloth.h"
#include "Kernel/ActorState.h"
#include "Kernel/DrawTailState.h"
#include "Joints/JointKernels.h"
#include "Kernel/JointRequest.h"
#include "Effects/ModelEffects.h"
#include "Particles/Particles.h"
#include "Particles/Sprites.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// The 0x60f/7, /0xb and /0x19 (WanderingJoints.cpp): creation 0x533bbd, 0x533d5f and 0x533db8 and step
// 0x53a87a..0x53b01f.
namespace
{
using Client::Crt::CrtRandom;
using Client::Effects::ModelEffect;
using Client::Kernel::ActorState;
using Client::Kernel::JointOwnerKind;
using Client::Kernel::JointState;

constexpr std::uint32_t Seed = 0x60fu;

std::uint64_t DrawsBetween(CrtRandom from, const CrtRandom& to)
{
    std::uint64_t draws = 0;
    while (from.State() != to.State() && draws < 100000)
    {
        static_cast<void>(from.Next());
        ++draws;
    }
    return draws;
}

struct Pool
{
    Pool()
    {
        cloth->random = CrtRandom{Seed};
        cloth->logicSteps = 77;
        cloth->openCounterAccesses = 1; // without the close's four rand().
        arrow.alive = true;
        arrow.start = {1100, 2100, 160};
        actor.alive = true;
        actor.position = {520, 640, 0};
    }

    Client::Core::Expected<std::optional<std::size_t>, std::string> Create(std::int32_t subtype, std::int32_t skill = 0,
                                                                           glm::vec3 to = {1000, 2000, 150})
    {
        glm::vec3 from{10, 20, 30};
        glm::vec3 angles{0, 0, 45};
        const bool arrowOwned = subtype != 7;
        const Client::Kernel::JointRequest request{.type = 0x60f,
                                                   .firstPosition = from,
                                                   .secondPosition = to,
                                                   .angles = angles,
                                                   .subtype = subtype,
                                                   .ownerContext = arrowOwned ? static_cast<void*>(&arrow)
                                                                              : static_cast<void*>(&actor),
                                                   .scale = 50,
                                                   .key = -1,
                                                   .skill = skill,
                                                   .skillSerial = 0,
                                                   .target = -1};
        return Client::Joints::CreateFaunaJoint(joints, request, cloth->random,
                                                arrowOwned ? JointOwnerKind::ModelEffect : JointOwnerKind::Actor);
    }

    Client::Core::Expected<void, std::string> Step()
    {
        return Client::Joints::StepJoints(
            joints, cloth->random, cloth.get(), particles.get(), 0, sprites.get(), nullptr,
            [this](Client::Audio::SoundId sound) { sounds.push_back(sound.Value()); }, {}, {},
            [this](ModelEffect& hit) -> Client::Core::Expected<void, std::string>
            {
                hits.push_back(hit.transform.position);
                return {};
            });
    }

    std::vector<JointState> joints = std::vector<JointState>(4);
    std::unique_ptr<Client::Models::ClothWorld> cloth = std::make_unique<Client::Models::ClothWorld>();
    std::unique_ptr<Client::Particles::Particles> particles = std::make_unique<Client::Particles::Particles>();
    std::unique_ptr<Client::Particles::FrameSprites> sprites = std::make_unique<Client::Particles::FrameSprites>();
    ModelEffect arrow{};
    ActorState actor{};
    std::vector<std::int32_t> sounds;
    std::vector<glm::vec3> hits;
};

std::size_t LiveSprites(Client::Particles::FrameSprites& sprites)
{
    return static_cast<std::size_t>(
        std::ranges::count_if(sprites.Sprites(), [](const auto& sprite) { return sprite.alive; }));
}
} // namespace

// 0x533bbd..0x533c13: the /7 is born blue (0.2; 0.2; 1) with life rand() % 10 + 30 and the rest in +0x9ec, 15 points,
// width 30 (not the requested one) and vx at rand() % 3000; the target is the second point. With the pool full,
// nothing, not even rand().
TEST(WanderingJointsTest, TheWanderersBornWithTheirDelay)
{
    Pool pool;
    CrtRandom expected{Seed};
    const std::int32_t delay = expected.Next() % 10;
    const std::int32_t drift = expected.Next() % 3000;

    ASSERT_TRUE(pool.Create(7).HasValue());

    const JointState& joint = pool.joints[0];
    EXPECT_EQ(pool.cloth->random.State(), expected.State());
    const float dim = std::bit_cast<float>(0x3e4ccccdu);
    EXPECT_EQ(joint.light, glm::vec3(dim, dim, 1));
    EXPECT_EQ(joint.life, delay + 30);
    EXPECT_EQ(joint.field9ec, delay);
    EXPECT_EQ(joint.maximumCount, 15);
    EXPECT_EQ(joint.width, 30.0f);
    EXPECT_EQ(joint.speed, 0.0f);
    EXPECT_EQ(joint.velocity, glm::vec3(static_cast<float>(drift), 0, 0));
    EXPECT_EQ(joint.targetPosition, glm::vec3(1000, 2000, 150));
    EXPECT_TRUE(joint.tailEnabled);

    for (auto& other : pool.joints)
        other.alive = true;
    const CrtRandom before = pool.cloth->random;
    const auto full = pool.Create(7);
    ASSERT_TRUE(full.HasValue());
    EXPECT_FALSE(full.Value().has_value());
    EXPECT_EQ(pool.cloth->random.State(), before.State());
}

// 0x533d5f..0x533db3 and 0x533db8..0x533e0c: the /0xb (blue) and the /0x19 (pink) without delay: life 30, +0x9ec at 0
// and a single rand(), the vx one.
TEST(WanderingJointsTest, TheArrowWanderersHaveNoDelay)
{
    for (const std::int32_t subtype : {0xb, 0x19})
    {
        SCOPED_TRACE(subtype);
        Pool pool;
        CrtRandom expected{Seed};
        const std::int32_t drift = expected.Next() % 3000;

        ASSERT_TRUE(pool.Create(subtype).HasValue());

        const JointState& joint = pool.joints[0];
        EXPECT_EQ(pool.cloth->random.State(), expected.State());
        const glm::vec3 light = subtype == 0xb
                                    ? glm::vec3(std::bit_cast<float>(0x3e4ccccdu), std::bit_cast<float>(0x3e4ccccdu), 1)
                                    : glm::vec3(std::bit_cast<float>(0x3f666666u), std::bit_cast<float>(0x3ecccccdu),
                                                std::bit_cast<float>(0x3f19999au));
        EXPECT_EQ(joint.light, light);
        EXPECT_EQ(joint.life, 30);
        EXPECT_EQ(joint.field9ec, 0);
        EXPECT_EQ(joint.maximumCount, 15);
        EXPECT_EQ(joint.width, 30.0f);
        EXPECT_EQ(joint.velocity.x, static_cast<float>(drift));
        EXPECT_EQ(joint.ownerKind, JointOwnerKind::ModelEffect);
    }
}

// 0x53a89e..0x53b01f with the /0xb in slot 0 and 77 steps (phase −77), life 30: the direction with the
// one-and-a-half frequencies, radius 150, the blend with the arrow's start shifted by cos(arg · 0.01f) · 25, the sound
// 0x42 (life 30) and three sprites (0x5fd and two 0x5ac) with two rand() each. Computed apart from MuRet
// (golden_wander.py: Fraction and Decimal, FSIN/FCOS to 64 bits, PC53 and stores to float).
TEST(WanderingJointsTest, TheArrowWandererCirclesItsStart)
{
    Pool pool;
    ASSERT_TRUE(pool.Create(0xb).HasValue());
    const CrtRandom before = pool.cloth->random;

    ASSERT_TRUE(pool.Step().HasValue());

    const JointState& joint = pool.joints[0];
    EXPECT_EQ(joint.position, glm::vec3(std::bit_cast<float>(0x4484710bu), std::bit_cast<float>(0x4502a1eeu),
                                        std::bit_cast<float>(0x4237a6c4u)));
    EXPECT_EQ(DrawsBetween(before, pool.cloth->random), 6u);
    EXPECT_EQ(LiveSprites(*pool.sprites), 3u);
    EXPECT_EQ(pool.sounds, std::vector<std::int32_t>{0x42});
    EXPECT_TRUE(pool.hits.empty());
    EXPECT_EQ(joint.life, 29);
    EXPECT_EQ(joint.count, 1);
    // The /0xb does not fade (0x53b318).
    EXPECT_EQ(joint.light.x, std::bit_cast<float>(0x3e4ccccdu));
}

// 0x53acaf..0x53ae06: the /7 in slot 1 (phase 53808) with life 12 and +0x9ec 3, around the character's +0x10 and 100
// higher; with the counter closed at 0, its four rand() before the six of the sprites.
TEST(WanderingJointsTest, TheWandererCirclesItsOwner)
{
    Pool pool;
    pool.joints[0].alive = true;
    pool.joints[0].type = 0x617; // an occupied slot that does not move
    pool.joints[0].subtype = 0;
    pool.joints[0].life = 1000;
    pool.joints[0].tailEnabled = false;
    ASSERT_TRUE(pool.Create(7, 0, {500, 600, 70}).HasValue());
    JointState& joint = pool.joints[1];
    joint.life = 12;
    joint.field9ec = 3;
    pool.cloth->openCounterAccesses = 0;
    const CrtRandom before = pool.cloth->random;

    ASSERT_TRUE(pool.Step().HasValue());

    EXPECT_EQ(joint.position, glm::vec3(std::bit_cast<float>(0x43f50746u), std::bit_cast<float>(0x44021e2cu),
                                        std::bit_cast<float>(0x432cb27fu)));
    EXPECT_EQ(DrawsBetween(before, pool.cloth->random), 4u + 6u);
    EXPECT_TRUE(pool.sounds.empty());
}

// 0x53aaa2..0x53acd7 with the radius not clipped to 150 and the blend of the "· 4" branch: with t = life · 4/3, the
// /0x19 with life 5 (t under 20: the blend t · 4 and radius 0, clipped from t − 10 < 0), the /7 with life 29 and +0x9ec
// 3 (radius (40 − t) · 15 ≈ 20) and the /0xb with life 20 (radius (t − 10) · 5 ≈ 83), with 77 steps and the clock not
// closed. The bits came from an x87 emulation independent of MuRet (the fourth round's ASM review).
TEST(WanderingJointsTest, TheWanderersOutsideTheWidestRadius)
{
    struct Probe
    {
        std::int32_t subtype;
        std::size_t slot;
        std::int32_t life;
        std::int32_t delay;
        glm::vec3 to;
        glm::vec3 expected;
    };
    const auto bits = [](std::uint32_t x, std::uint32_t y, std::uint32_t z)
    { return glm::vec3(std::bit_cast<float>(x), std::bit_cast<float>(y), std::bit_cast<float>(z)); };
    for (const Probe& probe : {Probe{0x19, 2, 5, 0, {1000, 2000, 150}, bits(0x44875a61u, 0x45029f09u, 0x432f74d5u)},
                               Probe{7, 1, 29, 3, {500, 600, 70}, bits(0x43fa232au, 0x44135cf0u, 0x43280d68u)},
                               Probe{0xb, 0, 20, 0, {1000, 2000, 150}, bits(0x4482aa50u, 0x4500b5c2u, 0x42cc8c48u)}})
    {
        SCOPED_TRACE(probe.subtype);
        Pool pool;
        for (std::size_t slot = 0; slot < probe.slot; ++slot)
        {
            pool.joints[slot].alive = true;
            pool.joints[slot].type = 0x617; // an occupied slot that does not move
            pool.joints[slot].subtype = 0;
            pool.joints[slot].life = 1000;
            pool.joints[slot].tailEnabled = false;
        }
        const auto created = pool.Create(probe.subtype, 0, probe.to);
        ASSERT_TRUE(created.HasValue());
        ASSERT_EQ(created.Value(), std::optional<std::size_t>(probe.slot));
        JointState& joint = pool.joints[probe.slot];
        joint.life = probe.life;
        joint.field9ec = probe.delay;
        const CrtRandom before = pool.cloth->random;

        ASSERT_TRUE(pool.Step().HasValue());

        EXPECT_EQ(joint.position, probe.expected);
        EXPECT_EQ(DrawsBetween(before, pool.cloth->random), 6u);
        EXPECT_TRUE(pool.sounds.empty());
    }
}

// 0x53afc0..0x53b017: the /7 in its last life also drops the 0x5ed/1 (scale 0.6f) at its position, without rand() and
// without fading (0x53b318); with the 3000 particles full, it does not create it and goes on the same.
TEST(WanderingJointsTest, TheWandererBurstsAtTheEndUnlessTheParticlesAreFull)
{
    for (const bool full : {false, true})
    {
        SCOPED_TRACE(full);
        Pool pool;
        ASSERT_TRUE(pool.Create(7).HasValue());
        JointState& joint = pool.joints[0];
        joint.life = 1;
        const glm::vec3 light = joint.light;
        if (full)
            for (auto& particle : pool.particles->All())
                particle.alive = true;
        const CrtRandom before = pool.cloth->random;

        ASSERT_TRUE(pool.Step().HasValue());

        EXPECT_EQ(DrawsBetween(before, pool.cloth->random), 6u);
        EXPECT_EQ(joint.light, light);
        EXPECT_EQ(joint.life, 0);
        EXPECT_TRUE(joint.alive);
        if (full)
            continue;
        const auto bursts = std::ranges::count_if(pool.particles->All(), [](const auto& particle)
                                                  { return particle.alive && particle.type == 0x5ed; });
        ASSERT_EQ(bursts, 1);
        const auto& burst = pool.particles->All().front();
        EXPECT_EQ(burst.subtype, 1);
        EXPECT_EQ(burst.position, joint.position);
        EXPECT_EQ(burst.scale, std::bit_cast<float>(0x3f19999au));
    }
}

// 0x53ae2f..0x53ae72: with skill 1, the arrow stays at the joint's position and hits (0x520170).
// 0x53a884..0x53a899: with the arrow off, the joint goes out with life at −1 and goes through the tail (−2), without
// rand().
TEST(WanderingJointsTest, TheSecondArrowWandererDrivesTheArrow)
{
    Pool pool;
    ASSERT_TRUE(pool.Create(0xb, 1).HasValue());
    const JointState& joint = pool.joints[0];

    ASSERT_TRUE(pool.Step().HasValue());

    ASSERT_EQ(pool.hits.size(), 1u);
    EXPECT_EQ(pool.hits.front(), joint.position);
    EXPECT_EQ(pool.arrow.transform.position, joint.position);

    pool.arrow.alive = false;
    const CrtRandom before = pool.cloth->random;
    ASSERT_TRUE(pool.Step().HasValue());
    EXPECT_FALSE(joint.alive);
    EXPECT_EQ(joint.life, -2);
    EXPECT_EQ(joint.count, 2);
    EXPECT_EQ(pool.cloth->random.State(), before.State());
    EXPECT_EQ(pool.hits.size(), 1u);
}

// 0x53afc9..0x53b017 and 0x53b339: the /0x19 in its last life drops the particle 0x5ed/1 (scale 0.6f) and, with life
// under 10, fades by 0.7692308f. The /0xb does not drop it.
TEST(WanderingJointsTest, ThePinkWandererBurstsAtTheEnd)
{
    for (const std::int32_t subtype : {0x19, 0xb})
    {
        SCOPED_TRACE(subtype);
        Pool pool;
        ASSERT_TRUE(pool.Create(subtype).HasValue());
        JointState& joint = pool.joints[0];
        joint.life = 1;
        const glm::vec3 light = joint.light;

        ASSERT_TRUE(pool.Step().HasValue());

        const auto bursts = std::ranges::count_if(pool.particles->All(), [](const auto& particle)
                                                  { return particle.alive && particle.type == 0x5ed; });
        EXPECT_EQ(bursts, subtype == 0x19 ? 1 : 0);
        const double fade = static_cast<double>(std::bit_cast<float>(0x3f44ec4fu));
        EXPECT_EQ(joint.light.x, subtype == 0x19 ? static_cast<float>(static_cast<double>(light.x) * fade) : light.x);
    }
}

// Without the live owner arrow and without the hit, the /0xb with skill 1 stops at 0x53ae6d.
TEST(WanderingJointsTest, TheArrowHitNeedsItsService)
{
    Pool pool;
    ASSERT_TRUE(pool.Create(0xb, 1).HasValue());

    const auto stepped = Client::Joints::StepJoints(pool.joints, pool.cloth->random, pool.cloth.get(),
                                                    pool.particles.get(), 0, pool.sprites.get());

    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x53ae6d"), std::string::npos) << stepped.Error();
}
