#include "Joints/FaunaJointCreation.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Math/OriginalRotation.h"
#include "Kernel/ActorState.h"
#include "Kernel/DrawTailState.h"
#include "Kernel/JointRequest.h"
#include "Effects/ModelEffects.h"
#include "Particles/Sprites.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// The 0x61c/2..6 (InfernoJoints.cpp): creation 0x534444..0x53463b and step 0x53b6a0..0x53b7ec, and the 0x62b/6 that
// the /4 releases (creation 0x533faf..0x5340d7 with life and points at 15, step 0x53b029 with life · 0.8f).
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::JointState;

constexpr std::uint32_t Seed = 0x61cu;

using Client::Math::D;
using Client::Math::F32;

struct Pool
{
    Client::Core::Expected<std::optional<std::size_t>, std::string> Create(std::int32_t subtype)
    {
        glm::vec3 from = origin;
        glm::vec3 turn = angles;
        const Client::Kernel::JointRequest request{.type = 0x61c,
                                                   .firstPosition = from,
                                                   .secondPosition = from,
                                                   .angles = turn,
                                                   .subtype = subtype,
                                                   .ownerContext = nullptr,
                                                   .scale = 40,
                                                   .key = -1,
                                                   .skill = 0,
                                                   .skillSerial = 0,
                                                   .target = -1};
        return Client::Joints::CreateFaunaJoint(joints, request, random);
    }

    Client::Core::Expected<void, std::string> Step()
    {
        return Client::Joints::StepJoints(joints, random, nullptr, nullptr, worldClock, sprites.get(), nullptr, {}, {},
                                          {}, {}, effects.get(), hero);
    }

    std::vector<JointState> joints = std::vector<JointState>(4);
    CrtRandom random{Seed};
    std::unique_ptr<Client::Particles::FrameSprites> sprites = std::make_unique<Client::Particles::FrameSprites>();
    glm::vec3 origin{1500.0f, 1600.0f, 170.0f};
    glm::vec3 angles{0.0f, 0.0f, 135.0f};
    float worldClock = 1234.5f;
    std::unique_ptr<Client::Effects::ModelEffects> effects;
    const Client::Kernel::ActorState* hero = nullptr;
};

std::vector<Client::Particles::Sprite> LiveSprites(Client::Particles::FrameSprites& sprites)
{
    std::vector<Client::Particles::Sprite> alive;
    for (const auto& sprite : sprites.Sprites())
        if (sprite.alive)
            alive.push_back(sprite);
    return alive;
}
} // namespace

// 0x534444..0x5345bd: the common part of the /2..6 (speed 8, vx 3.5, vz 1, life 20, 12 points, the count at −1, +0x9ec
// at 5, the turn (0, 0, z) at +0x3c) and each subtype's own; the path (+0x24) at the position. No rand().
TEST(InfernoJointsTest, EachSubtypeBornsWithItsTexture)
{
    struct Probe
    {
        std::int32_t subtype;
        std::uint16_t texture;
        float speed;
        glm::vec3 velocity;
        std::int32_t life;
        std::int32_t points;
        std::int32_t fade;
        float lift;
    };
    for (const Probe& probe :
         {Probe{2, 0x633, 8, {5, 0, 5}, 15, 12, 5, 10}, Probe{3, 0x60f, 10, {10.5f, 0, 2}, 20, 12, 10, 0},
          Probe{4, 0x6ae, 8, {15.5f, 0, 3}, 20, 13, 10, 0}, Probe{5, 0x634, 8, {3.5f, 0, 1}, 20, 12, 5, 0},
          Probe{6, 0x634, 20, {3.5f, 0, 1}, 16, 12, 5, 0}})
    {
        SCOPED_TRACE(probe.subtype);
        Pool pool;
        ASSERT_TRUE(pool.Create(probe.subtype).HasValue());
        const JointState& joint = pool.joints[0];
        EXPECT_EQ(pool.random.State(), CrtRandom{Seed}.State());
        EXPECT_EQ(joint.texture, probe.texture);
        EXPECT_EQ(joint.speed, probe.speed);
        EXPECT_EQ(joint.velocity, probe.velocity);
        EXPECT_EQ(joint.life, probe.life);
        EXPECT_EQ(joint.maximumCount, probe.points);
        EXPECT_EQ(joint.field9ec, probe.fade);
        EXPECT_EQ(joint.count, -1);
        EXPECT_EQ(joint.width, 40.0f);
        EXPECT_EQ(joint.bendAngles, glm::vec3(0.0f, 0.0f, 135.0f));
        EXPECT_EQ(joint.position, pool.origin + glm::vec3(0.0f, 0.0f, probe.lift));
        EXPECT_EQ(joint.pathIncrement, joint.position);
    }
}

// 0x53b6b2..0x53b7ec: the /4 advances with the earlier speed (0x53590e), the speed adds vz and vz adds vx (life 20); it
// releases a 0x62b/6 of width 30 from its position with its angles (one rand(): the 0x62b's vx), which steps in the
// same loop, the 0x5ac sprite (1.6f) and the 0x5fd (1.5f, turned by the clock · 0.1f), with light (0.1; 0.6; 1).
TEST(InfernoJointsTest, TheHoleRisesAndDropsSparks)
{
    Pool pool;
    ASSERT_TRUE(pool.Create(4).HasValue());
    JointState& joint = pool.joints[0];
    CrtRandom expected = pool.random;
    const auto turn = static_cast<float>(expected.Next() % 360);
    const auto matrix = Client::Math::OriginalAngleMatrix(pool.angles);
    const auto moved = Client::Math::OriginalVectorRotate({0, -8.0f, 0}, matrix);
    const glm::vec3 position{F32(D(pool.origin.x) + D(moved.x)), F32(D(pool.origin.y) + D(moved.y)),
                             F32(D(pool.origin.z) + D(moved.z))};

    ASSERT_TRUE(pool.Step().HasValue());

    EXPECT_EQ(pool.random.State(), expected.State());
    EXPECT_EQ(joint.position, position);
    EXPECT_EQ(joint.speed, 11.0f);
    EXPECT_EQ(joint.velocity.z, 18.5f);
    EXPECT_EQ(joint.life, 19);
    EXPECT_EQ(joint.count, 0);
    const JointState& impact = pool.joints[1];
    ASSERT_TRUE(impact.alive);
    EXPECT_EQ(impact.type, 0x62b);
    EXPECT_EQ(impact.subtype, 6);
    EXPECT_EQ(impact.width, 30.0f);
    EXPECT_EQ(impact.maximumCount, 15);
    EXPECT_EQ(impact.life, 14); // 15 from creation and the life of the step in the same loop.
    EXPECT_EQ(impact.velocity.x, turn);
    EXPECT_EQ(impact.angles, pool.angles);
    const auto sprites = LiveSprites(*pool.sprites);
    ASSERT_EQ(sprites.size(), 2u);
    const glm::vec3 glow{std::bit_cast<float>(0x3dcccccdu), std::bit_cast<float>(0x3f19999au), 1};
    EXPECT_EQ(sprites[0].texture, 0x5ac);
    EXPECT_EQ(sprites[0].scale, std::bit_cast<float>(0x3fcccccdu));
    EXPECT_EQ(sprites[0].rotation, 0.0f);
    EXPECT_EQ(sprites[0].light, glow);
    EXPECT_EQ(sprites[0].position, position);
    EXPECT_EQ(sprites[1].texture, 0x5fd);
    EXPECT_EQ(sprites[1].scale, 1.5f);
    EXPECT_EQ(sprites[1].rotation, F32(D(pool.worldClock) * D(std::bit_cast<float>(0x3dcccccdu))));
}

// 0x53b6cf: with less than 15 life vz only adds 0.5; with less than +0x9ec, the light times 0.7692308f. With the pool
// full, the 0x62b/6 is missing and draws no rand(), and the sprites are there.
TEST(InfernoJointsTest, TheHoleSlowsAndFadesAtTheEnd)
{
    Pool pool;
    ASSERT_TRUE(pool.Create(4).HasValue());
    JointState& joint = pool.joints[0];
    joint.life = 9;
    for (std::size_t slot = 1; slot < pool.joints.size(); ++slot)
    {
        pool.joints[slot].alive = true;
        pool.joints[slot].type = 0x617; // occupied, no rand() in their step
        pool.joints[slot].life = 100;
        pool.joints[slot].tailEnabled = false;
    }
    const CrtRandom before = pool.random;

    ASSERT_TRUE(pool.Step().HasValue());

    EXPECT_EQ(pool.random.State(), before.State());
    EXPECT_EQ(joint.velocity.z, 3.5f);
    const float fade = F32(D(1.0f) * D(std::bit_cast<float>(0x3f44ec4fu)));
    EXPECT_EQ(joint.light, glm::vec3(fade));
    EXPECT_EQ(LiveSprites(*pool.sprites).size(), 2u);
}

// 0x53b709..0x53b732 with +0x9ec at 5 (the /2, /5 and /6): with life 9 they do not fade (with the 10 of the /3 and /4
// they do) and with life 4, the light times 0.7692308f.
TEST(InfernoJointsTest, TheLavaFadesBelowFive)
{
    const double fade = D(std::bit_cast<float>(0x3f44ec4fu));
    for (const std::int32_t subtype : {2, 5, 6})
    {
        for (const std::int32_t life : {9, 4})
        {
            SCOPED_TRACE(testing::Message() << "subtipo " << subtype << ", vida " << life);
            Pool pool;
            ASSERT_TRUE(pool.Create(subtype).HasValue());
            JointState& joint = pool.joints[0];
            ASSERT_EQ(joint.field9ec, 5);
            joint.life = life;

            ASSERT_TRUE(pool.Step().HasValue());

            EXPECT_EQ(joint.light, glm::vec3(life < 5 ? F32(D(1.0f) * fade) : 1.0f));
            EXPECT_EQ(pool.random.State(), CrtRandom{Seed}.State());
        }
    }
}

// 0x53b6f2..0x53b702: the /6 (the fast lava) stops appending segments with the tail full and releases nothing.
TEST(InfernoJointsTest, TheLavaStopsWithAFullTail)
{
    Pool pool;
    ASSERT_TRUE(pool.Create(6).HasValue());
    JointState& joint = pool.joints[0];
    joint.count = joint.maximumCount - 1;

    ASSERT_TRUE(pool.Step().HasValue());

    EXPECT_FALSE(joint.tailEnabled);
    EXPECT_EQ(joint.count, joint.maximumCount - 1);
    EXPECT_FALSE(pool.joints[1].alive);
    EXPECT_TRUE(LiveSprites(*pool.sprites).empty());
}

// 0x53b735..0x53b768: the /2 with life a multiple of 5 releases the 0xf6/6 on its path (+0x24, the start 10 higher)
// with its own angles (+0x3c), its light and key 20, ownerless: life 5, +0xec at 20 and scale 20 · 0.01f, the model's
// speed and light at 0.1f, without rand(). With another life, nothing. Without the effect table, it stops there;
// without the hero, it is created all the same: the /6 does not look at it (0x519a5c, only below 2).
TEST(InfernoJointsTest, TheInfernoDropsItsBurstEveryFiveSteps)
{
    const Client::Kernel::ActorState hero{};
    for (const std::int32_t life : {15, 14, 10})
    {
        SCOPED_TRACE(life);
        Pool pool;
        pool.effects = std::make_unique<Client::Effects::ModelEffects>();
        pool.hero = &hero;
        ASSERT_TRUE(pool.Create(2).HasValue());
        JointState& joint = pool.joints[0];
        joint.life = life;
        const glm::vec3 light = joint.light;
        ASSERT_EQ(joint.pathIncrement, glm::vec3(1500.0f, 1600.0f, 180.0f));

        ASSERT_TRUE(pool.Step().HasValue());

        EXPECT_EQ(pool.random.State(), CrtRandom{Seed}.State());
        const Client::Effects::ModelEffect& burst = pool.effects->Effects()[0];
        if (life % 5 != 0)
        {
            EXPECT_FALSE(burst.alive);
            continue;
        }
        ASSERT_TRUE(burst.alive);
        EXPECT_EQ(burst.type, 0xf6);
        EXPECT_EQ(burst.subtype, 6);
        EXPECT_EQ(burst.life, 5);
        EXPECT_EQ(burst.transform.position, glm::vec3(1500.0f, 1600.0f, 180.0f));
        EXPECT_EQ(burst.transform.angles, joint.bendAngles);
        EXPECT_EQ(burst.light, light);
        EXPECT_EQ(burst.flagEc, 20.0f);
        EXPECT_EQ(burst.transform.scale, F32(20.0 * D(0.01f)));
        EXPECT_EQ(burst.speed, 0.1f);
        EXPECT_EQ(burst.objectState.light, 0.1f);
        EXPECT_FALSE(burst.hasOwner);
        EXPECT_FALSE(pool.effects->Effects()[1].alive);
    }

    Pool tableless;
    ASSERT_TRUE(tableless.Create(2).HasValue());
    const auto cut = tableless.Step();
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x53b760"), std::string::npos) << cut.Error();
    Pool heroless;
    heroless.effects = std::make_unique<Client::Effects::ModelEffects>();
    ASSERT_TRUE(heroless.Create(2).HasValue());
    const auto blind = heroless.Step();
    ASSERT_TRUE(blind.HasValue()) << blind.Error();
    EXPECT_TRUE(heroless.effects->Effects()[0].alive);
    EXPECT_EQ(heroless.effects->Effects()[0].subtype, 6);
}

// The 61c/1, the red ray of the wizards' 0xd0/1 laser (0x51a5dd), owned by the effect. 0x53439e..0x53442d: speed 0, the
// width, pass 1 (+0x64), light at 1, 15 points, life 30 and the owner's position and angles. No rand(). The angles are
// not the request's but the owner's (+0x1c). Without an owner, the original turns the joint off, dereferences the null
// pointer (+0x10, 0x5343fb) and crashes: MuRet stops (0x5343f8).
TEST(InfernoJointsTest, TheLaserJointIsBornOnItsOwner)
{
    Client::Effects::ModelEffect laser{};
    laser.alive = true;
    laser.transform.position = {700.0f, 800.0f, 450.0f};
    laser.transform.angles = {30.0f, 0.0f, 90.0f};
    std::vector<JointState> joints(2);
    CrtRandom random(Seed);
    glm::vec3 from{1.0f, 2.0f, 3.0f};
    glm::vec3 turn{5.0f, 6.0f, 7.0f};
    Client::Kernel::JointRequest request{.type = 0x61c,
                                         .firstPosition = from,
                                         .secondPosition = from,
                                         .angles = turn,
                                         .subtype = 1,
                                         .ownerContext = &laser,
                                         .scale = 180.0f,
                                         .key = -1,
                                         .skill = 0,
                                         .skillSerial = 0,
                                         .target = -1};
    const auto created = Client::Joints::CreateFaunaJoint(joints, request, random);
    ASSERT_TRUE(created.HasValue()) << created.Error();
    EXPECT_EQ(random.State(), CrtRandom(Seed).State());
    const JointState& joint = joints[0];
    EXPECT_EQ(joint.speed, 0.0f);
    EXPECT_EQ(joint.width, 180.0f);
    EXPECT_EQ(joint.category, 1);
    EXPECT_EQ(joint.light, glm::vec3(1.0f));
    EXPECT_EQ(joint.maximumCount, 15);
    EXPECT_EQ(joint.life, 30);
    EXPECT_EQ(joint.position, laser.transform.position);
    EXPECT_EQ(joint.angles, laser.transform.angles);

    request.ownerContext = nullptr;
    std::vector<JointState> lonely(1);
    const auto cut = Client::Joints::CreateFaunaJoint(lonely, request, random);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x5343f8"), std::string::npos) << cut.Error();
}

// 0x53b628..0x53b69b: takes the owner's alive byte, the width drops 1, the position is the owner's 100 higher and, with
// less than 15 life, the light times 0.7692308f; then the common tail. No rand().
TEST(InfernoJointsTest, TheLaserJointFollowsItsOwnerAndNarrows)
{
    Client::Effects::ModelEffect laser{};
    laser.alive = true;
    laser.transform.position = {700.0f, 800.0f, 450.0f};
    std::vector<JointState> joints(2);
    CrtRandom random(Seed);
    glm::vec3 from{1.0f, 2.0f, 3.0f};
    glm::vec3 turn{30.0f, 0.0f, 90.0f};
    ASSERT_TRUE(Client::Joints::CreateFaunaJoint(joints,
                                                 {.type = 0x61c,
                                                  .firstPosition = from,
                                                  .secondPosition = from,
                                                  .angles = turn,
                                                  .subtype = 1,
                                                  .ownerContext = &laser,
                                                  .scale = 180.0f,
                                                  .key = -1,
                                                  .skill = 0,
                                                  .skillSerial = 0,
                                                  .target = -1},
                                                 random)
                    .HasValue());
    JointState& joint = joints[0];
    laser.transform.position = {710.0f, 790.0f, 460.0f};

    ASSERT_TRUE(Client::Joints::StepJoints(joints, random).HasValue());

    EXPECT_TRUE(joint.alive);
    EXPECT_EQ(joint.width, 179.0f);
    EXPECT_EQ(joint.position, glm::vec3(710.0f, 790.0f, 560.0f));
    EXPECT_EQ(joint.light, glm::vec3(1.0f));
    EXPECT_EQ(joint.life, 29);
    EXPECT_EQ(random.State(), CrtRandom(Seed).State());

    joint.life = 14;
    laser.alive = false;
    ASSERT_TRUE(Client::Joints::StepJoints(joints, random).HasValue());

    EXPECT_FALSE(joint.alive);
    EXPECT_EQ(joint.width, 178.0f);
    EXPECT_EQ(joint.light, glm::vec3(F32(D(1.0f) * D(std::bit_cast<float>(0x3f44ec4fu)))));
    EXPECT_EQ(joint.life, 13);
}
