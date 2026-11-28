#include "Scenes/CharacterEffectSteps.h"

#include "Crt/CrtRandom.h"
#include "Kernel/ActorState.h"
#include "Kernel/WorldMap.h"
#include "Kernel/WorldState.h"
#include "Math/HalfLifeMath.h"
#include "Math/OriginalRetainedMath.h"
#include "Math/OriginalRotation.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/GameParticles.h"
#include "Session/CharacterDrawHooks.h"
#include "World/ModelEffects.h"
#include "World/ParticleOwner.h"
#include "World/Particles.h"
#include "support/ParticleDrawRecorder.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

// The steps of the characters' 0x5ac and 0x61e effects (0x52a9ad, 0x52a866, 0x52a8a7 and 0x52b9a6), against the ASM of
// c32bbd5f. The rand() calls come from the CRT with seed 1.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;

const Client::Session::CharacterDrawHooks JointHooks;

float Float(std::uint32_t bits)
{
    return std::bit_cast<float>(bits);
}

std::uint32_t AfterRandoms(std::size_t count)
{
    CrtRandom random{1};
    for (std::size_t call = 0; call < count; ++call)
        static_cast<void>(random.Next());
    return random.State();
}

// How many rand() calls must be drawn with seed 1 for the next two to meet `first` and `second`.
template <typename First, typename Second> std::size_t RandomsBeforePair(First first, Second second)
{
    CrtRandom random{1};
    int previous = random.Next();
    for (std::size_t count = 0;; ++count)
    {
        const int next = random.Next();
        if (first(previous) && second(next))
            return count;
        previous = next;
    }
}

// The rand() calls of seed 1 from the one after the first `skipped`.
std::vector<int> RandomsAfter(std::size_t skipped, std::size_t count)
{
    CrtRandom random{1};
    for (std::size_t call = 0; call < skipped; ++call)
        static_cast<void>(random.Next());
    std::vector<int> values;
    for (std::size_t call = 0; call < count; ++call)
        values.push_back(random.Next());
    return values;
}

std::vector<Client::World::Particle> LiveParticles(Client::World::Particles& pool)
{
    std::vector<Client::World::Particle> live;
    for (const auto& particle : pool.All())
    {
        if (particle.alive)
            live.push_back(particle);
    }
    return live;
}

std::vector<Client::Scenes::JointState> LiveJoints(std::span<const Client::Scenes::JointState> joints)
{
    std::vector<Client::Scenes::JointState> live;
    for (const auto& joint : joints)
    {
        if (joint.alive)
            live.push_back(joint);
    }
    return live;
}

class CharacterEffectStepsTest : public testing::Test
{
protected:
    void SetUp() override
    {
        m_actor.type = 0x20a;
        m_actor.scale = 1.0f;
        m_actor.alpha = 1.0f;
        m_actor.position = {100.0f, 200.0f, 300.0f};
        m_actor.light = {0.1f, 0.2f, 0.3f};
    }

    static constexpr std::size_t Slot = 3;
    ActorState m_actor;
    CrtRandom m_random{1};
    Client::World::Particles m_particles;
    std::vector<Client::Scenes::JointState> m_joints = std::vector<Client::Scenes::JointState>(16);
    Client::World::ModelEffects m_effects;
};

// 0x52a9ad: the light's rand(), +0xd8 one more and, with the life a multiple of 15, the z turn 180 more and the
// position at 50 from the owner with the sine of the unrounded sum and the cosine of the stored float, 20 higher, with
// a 0x619/9 there. The common tail takes off the life: with 30, two joints and it dies at step 30.
TEST_F(CharacterEffectStepsTest, TheFullSetFlareCirclesItsOwnerEveryFifteenStepsWithAnEnergyJoint)
{
    using Client::Math::Detail::CosineProductPC53;
    using Client::Math::Detail::SineProductPC53;
    m_actor.angles = {0.0f, 0.0f, 33.3f};
    ASSERT_TRUE(m_effects.CreateFullSetFlare(m_actor.position, m_actor.angles, glm::vec3(0.5f), &m_actor).has_value());
    Client::World::ModelEffect& flare = m_effects.Effects()[0];
    const float tenth = Float(0x3dcccccdu);

    std::vector<glm::vec3> expected;
    for (int step = 1; step <= 30; ++step)
    {
        SCOPED_TRACE(step);
        const glm::vec3 before = flare.transform.position;
        const float yaw = flare.transform.angles.z;
        if (step == 16)
            m_actor.position = {130.0f, 170.0f, 290.0f};
        const auto stepped = Client::Scenes::StepFullSetFlare(flare, m_joints, &JointHooks, m_random);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(m_random.State(), AfterRandoms(static_cast<std::size_t>(step)));
        EXPECT_EQ(flare.speed, static_cast<float>(step));
        if (step == 1 || step == 16)
        {
            const double turned = static_cast<double>(yaw) + 180.0;
            const float storedYaw = static_cast<float>(turned);
            EXPECT_EQ(flare.transform.angles.z, storedYaw);
            const glm::vec3 at{
                static_cast<float>(*SineProductPC53(turned * static_cast<double>(tenth), 50.0f) +
                                   static_cast<double>(m_actor.position.x)),
                static_cast<float>(
                    *CosineProductPC53(static_cast<double>(storedYaw) * static_cast<double>(tenth), 50.0f) +
                    static_cast<double>(m_actor.position.y)),
                static_cast<float>(static_cast<double>(m_actor.position.z) + 20.0)};
            EXPECT_EQ(flare.transform.position, at);
            expected.push_back(at);
        }
        else
        {
            EXPECT_EQ(flare.transform.angles.z, yaw);
            EXPECT_EQ(flare.transform.position, before);
        }
        EXPECT_EQ(flare.life, 30 - step);
        EXPECT_EQ(flare.alive, step < 30);
    }
    const auto joints = LiveJoints(m_joints);
    ASSERT_EQ(joints.size(), 2u);
    for (std::size_t index = 0; index < joints.size(); ++index)
    {
        SCOPED_TRACE(index);
        EXPECT_EQ(joints[index].type, 0x619);
        EXPECT_EQ(joints[index].subtype, 9);
        EXPECT_EQ(joints[index].ownerContext, &m_actor);
        EXPECT_EQ(joints[index].width, 10.0f);
        EXPECT_EQ(joints[index].key, -1);
    }
    // The first 0x619/9 is born on the owner of then; the second at the new position.
    EXPECT_EQ(joints[0].field9ec, 213);
    EXPECT_EQ(joints[0].position, glm::vec3(100.0f, 200.0f, 300.0f));
    EXPECT_EQ(joints[1].field9ec, 393);
    EXPECT_EQ(joints[1].position, m_actor.position);
}

// Without an owner, the 0x5ac/3 neither moves nor releases joints; another effect is not the 0x5ac/3.
TEST_F(CharacterEffectStepsTest, TheFullSetFlareWithoutOwnerOnlyCountsAndOtherEffectsAreRefused)
{
    ASSERT_TRUE(m_effects.CreateFullSetFlare(m_actor.position, {}, glm::vec3(0.5f), nullptr).has_value());
    Client::World::ModelEffect& flare = m_effects.Effects()[0];
    EXPECT_FALSE(flare.hasOwner);

    ASSERT_TRUE(Client::Scenes::StepFullSetFlare(flare, m_joints, &JointHooks, m_random).HasValue());

    EXPECT_EQ(flare.transform.position, m_actor.position);
    EXPECT_EQ(flare.transform.angles.z, 0.0f);
    EXPECT_TRUE(LiveJoints(m_joints).empty());
    EXPECT_EQ(m_random.State(), AfterRandoms(1));

    flare.subtype = 0;
    const auto refused = Client::Scenes::StepFullSetFlare(flare, m_joints, &JointHooks, m_random);
    ASSERT_FALSE(refused.HasValue());
    EXPECT_NE(refused.Error().find("0x52a9ad"), std::string::npos) << refused.Error();
}

// 0x52b9a6 → 0x518900: the pet's 0x61e/1 decal draws the light's rand() and, for each of its three 0x619/1, the turn
// (rand() % 90, 0, rand() % 360); each joint is born where that turn takes (0, −200, 0) subtracted from the decal's
// position, 120 higher, with width 5, key −1 and the hero as owner. The common tail takes off the life: with 20, it
// dies at step 20.
TEST_F(CharacterEffectStepsTest, ThePetSummonDropsThreeEnergyJointsAroundItsOwnerEachStep)
{
    m_actor.position = {1300.0f, 1700.0f, 290.0f};
    m_actor.angles = {0.0f, 0.0f, 45.0f};
    ASSERT_TRUE(m_effects.CreatePetSummon(m_actor.position, m_actor.angles, glm::vec3(0.5f), &m_actor).has_value());
    Client::World::ModelEffect& summon = m_effects.Effects()[0];

    const auto stepped = Client::Scenes::StepPetSummon(summon, m_joints, &JointHooks, m_random);

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(m_random.State(), AfterRandoms(7));
    EXPECT_EQ(summon.life, 19);
    EXPECT_TRUE(summon.alive);
    EXPECT_EQ(summon.transform.position, m_actor.position);
    const std::vector<int> draws = RandomsAfter(1, 6);
    const auto joints = LiveJoints(m_joints);
    ASSERT_EQ(joints.size(), 3u);
    for (std::size_t index = 0; index < joints.size(); ++index)
    {
        SCOPED_TRACE(index);
        const glm::vec3 angles{static_cast<float>(draws[index * 2] % 90), 0.0f,
                               static_cast<float>(draws[(index * 2) + 1] % 360)};
        const glm::vec3 reach =
            Client::Math::OriginalVectorRotate({0.0f, -200.0f, 0.0f}, Client::Math::OriginalAngleMatrix(angles));
        const glm::vec3 start{
            static_cast<float>(static_cast<double>(m_actor.position.x) - static_cast<double>(reach.x)),
            static_cast<float>(static_cast<double>(m_actor.position.y) - static_cast<double>(reach.y)),
            static_cast<float>(static_cast<double>(m_actor.position.z) - static_cast<double>(reach.z) + 120.0)};
        EXPECT_EQ(joints[index].type, 0x619);
        EXPECT_EQ(joints[index].subtype, 1);
        EXPECT_EQ(joints[index].angles, angles);
        EXPECT_EQ(joints[index].position, start);
        EXPECT_EQ(joints[index].ownerContext, &m_actor);
        EXPECT_EQ(joints[index].width, 5.0f);
        EXPECT_EQ(joints[index].key, -1);
        EXPECT_EQ(joints[index].life, 12);
        EXPECT_EQ(joints[index].maximumCount, 2);
    }

    for (int step = 2; step <= 20; ++step)
    {
        for (auto& joint : m_joints)
            joint.alive = false;
        ASSERT_TRUE(Client::Scenes::StepPetSummon(summon, m_joints, &JointHooks, m_random).HasValue());
    }
    EXPECT_FALSE(summon.alive);
    EXPECT_EQ(m_random.State(), AfterRandoms(7 * 20));
}

// 0x52b984..0x52bb2c: /0 emits no joints and goes to the common tail, with a single rand() per step.
TEST_F(CharacterEffectStepsTest, TheLevelUpDecalExpiresWithoutAJointTable)
{
    ASSERT_TRUE(m_effects.CreatePetSummon(m_actor.position, {}, glm::vec3(0.5f), &m_actor, 0).has_value());
    Client::World::ModelEffect& decal = m_effects.Effects()[0];
    for (int step = 1; step <= 20; ++step)
    {
        const auto stepped = Client::Scenes::StepPetSummon(decal, {}, &JointHooks, m_random);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(decal.life, 20 - step);
        EXPECT_EQ(decal.alive, step < 20);
        EXPECT_EQ(decal.transform.position, m_actor.position);
        EXPECT_EQ(m_random.State(), AfterRandoms(static_cast<std::size_t>(step)));
    }
}

// Another effect is not the 0x61e/0, /1 or /3, and without the joint table /1 stops before drawing rand().
TEST_F(CharacterEffectStepsTest, ThePetSummonRefusesOtherEffectsAndAMissingJointTable)
{
    ASSERT_TRUE(m_effects.CreatePetSummon(m_actor.position, {}, glm::vec3(0.5f), &m_actor).has_value());
    Client::World::ModelEffect& summon = m_effects.Effects()[0];

    const auto tableless = Client::Scenes::StepPetSummon(summon, {}, &JointHooks, m_random);
    ASSERT_FALSE(tableless.HasValue());
    EXPECT_NE(tableless.Error().find("0x5189cc"), std::string::npos) << tableless.Error();
    EXPECT_EQ(m_random.State(), 1u);
    EXPECT_EQ(summon.life, 20);

    summon.subtype = 4;
    const auto refused = Client::Scenes::StepPetSummon(summon, m_joints, &JointHooks, m_random);
    ASSERT_FALSE(refused.HasValue());
    EXPECT_NE(refused.Error().find("0x52b9a6"), std::string::npos) << refused.Error();
}

// 0x52b9a6 → 0x518900 with the /3 of the 0x1c and of the summons: the light's rand() and, like /1, three 0x619/3 joints
// (0x5189cc), each with the two rand() calls of its turn, with the character as owner. With the joint table full, none,
// and it goes on: the same seven rand() calls and the tail.
TEST_F(CharacterEffectStepsTest, TheSummonDecalDropsThreeEnergyJoints)
{
    ASSERT_TRUE(
        m_effects.CreatePetSummon(m_actor.position, {}, glm::vec3(0.5f), &m_actor, Client::World::SummonDecalSubtype)
            .has_value());
    Client::World::ModelEffect& summon = m_effects.Effects()[0];

    const auto stepped = Client::Scenes::StepPetSummon(summon, m_joints, &JointHooks, m_random);

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(m_random.State(), AfterRandoms(7));
    EXPECT_EQ(summon.life, 19);
    const std::vector<int> draws = RandomsAfter(1, 6);
    const auto joints = LiveJoints(m_joints);
    ASSERT_EQ(joints.size(), 3u);
    for (std::size_t index = 0; index < joints.size(); ++index)
    {
        SCOPED_TRACE(index);
        EXPECT_EQ(joints[index].type, 0x619);
        EXPECT_EQ(joints[index].subtype, 3);
        EXPECT_EQ(joints[index].angles, glm::vec3(static_cast<float>(draws[index * 2] % 90), 0.0f,
                                                  static_cast<float>(draws[(index * 2) + 1] % 360)));
        EXPECT_EQ(joints[index].ownerContext, &m_actor);
        EXPECT_EQ(joints[index].width, 5.0f);
        EXPECT_EQ(joints[index].life, 12);
        EXPECT_EQ(joints[index].maximumCount, 2);
    }

    for (auto& joint : m_joints)
        joint.alive = true;
    CrtRandom full{1};
    ASSERT_TRUE(Client::Scenes::StepPetSummon(summon, m_joints, &JointHooks, full).HasValue());
    EXPECT_EQ(full.State(), AfterRandoms(7));
    EXPECT_EQ(summon.life, 18);
}

// 0x52a8a7: the 0x5ac/2 of the 0x35 draws the light's rand(), leaves the life at 10 with the owner alive with 0x400 (at
// 0 otherwise) and raises +0xd8; every 50 it turns 50 in z, places itself at 80 from the owner (the sine of the
// unrounded sum, the cosine of the stored float) and 50 higher, and releases a 0x619/9 of width 15 there. The tail
// takes off the life.
TEST_F(CharacterEffectStepsTest, TheGuardFlareCirclesItsTargetEveryFiftyStepsWithAnEnergyJoint)
{
    using Client::Math::Detail::CosineProductPC53;
    using Client::Math::Detail::SineProductPC53;
    m_actor.alive = true;
    m_actor.flags78 = 0x400;
    m_actor.angles = {0.0f, 0.0f, 33.3f};
    ASSERT_TRUE(m_effects
                    .CreateSkillFlare(m_actor.position, m_actor.angles, glm::vec3(0.5f),
                                      Client::World::GuardFlareSubtype, &m_actor)
                    .has_value());
    Client::World::ModelEffect& flare = m_effects.Effects()[0];
    const float tenth = Float(0x3dcccccdu);

    for (int step = 1; step <= 50; ++step)
    {
        SCOPED_TRACE(step);
        const glm::vec3 before = flare.transform.position;
        const float yaw = flare.transform.angles.z;
        const auto stepped = Client::Scenes::StepSkillFlare(flare, m_joints, &JointHooks, m_random);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(m_random.State(), AfterRandoms(static_cast<std::size_t>(step)));
        EXPECT_EQ(flare.speed, static_cast<float>(step));
        EXPECT_EQ(flare.life, 9);
        if (step == 50)
        {
            const double turned = static_cast<double>(yaw) + 50.0;
            const float storedYaw = static_cast<float>(turned);
            EXPECT_EQ(flare.transform.angles.z, storedYaw);
            const glm::vec3 at{
                static_cast<float>(*SineProductPC53(turned * static_cast<double>(tenth), 80.0f) +
                                   static_cast<double>(m_actor.position.x)),
                static_cast<float>(
                    *CosineProductPC53(static_cast<double>(storedYaw) * static_cast<double>(tenth), 80.0f) +
                    static_cast<double>(m_actor.position.y)),
                static_cast<float>(static_cast<double>(m_actor.position.z) + 50.0)};
            EXPECT_EQ(flare.transform.position, at);
        }
        else
        {
            EXPECT_EQ(flare.transform.angles.z, yaw);
            EXPECT_EQ(flare.transform.position, before);
        }
    }
    const auto joints = LiveJoints(m_joints);
    ASSERT_EQ(joints.size(), 1u);
    EXPECT_EQ(joints[0].type, 0x619);
    EXPECT_EQ(joints[0].subtype, 9);
    EXPECT_EQ(joints[0].ownerContext, &m_actor);
    EXPECT_EQ(joints[0].width, 15.0f);
    EXPECT_EQ(joints[0].key, -1);

    // Without 0x400 the life stays at 0 and it dies in the tail; with 0x4000 on the owner, it neither counts nor moves.
    m_actor.flags78 = 0x4000;
    const float counted = flare.speed;
    ASSERT_TRUE(Client::Scenes::StepSkillFlare(flare, m_joints, &JointHooks, m_random).HasValue());
    EXPECT_EQ(flare.speed, counted);
    EXPECT_FALSE(flare.alive);
    EXPECT_EQ(flare.life, -1);
    EXPECT_EQ(m_random.State(), AfterRandoms(51));
}

// 0x52a866/0x52a89a: the selector comes before the two additions, even with the table full or at the final step.
TEST_F(CharacterEffectStepsTest, TheSwellFlareEmitsItsMirroredBonePairBeforeTheCommonTail)
{
    for (const std::size_t freeSlots : std::array<std::size_t, 3>{0, 1, 2})
    {
        SCOPED_TRACE(freeSlots);
        m_random = CrtRandom(1);
        for (std::size_t slot = 0; slot < m_particles.All().size(); ++slot)
            m_particles.All()[slot].alive = slot >= freeSlots;
        m_effects.Clear();
        m_actor.alive = true;
        m_actor.flags78 = 0x10;
        ASSERT_TRUE(m_effects
                        .CreateSkillFlare(m_actor.position, m_actor.angles, glm::vec3(0.5f),
                                          Client::World::SwellFlareSubtype, &m_actor)
                        .has_value());
        auto& flare = m_effects.Effects()[0];
        const Client::World::ParticleOwner owner{Client::World::ParticleOwnerKind::Actor, Slot};

        const auto stepped =
            Client::Scenes::StepSkillFlare(flare, m_joints, &JointHooks, m_random, &m_particles, owner);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(m_random.State(), AfterRandoms(2));
        EXPECT_EQ(flare.life, 9);
        EXPECT_TRUE(flare.alive);
        for (std::size_t slot = 0; slot < freeSlots; ++slot)
        {
            const auto& child = m_particles.All()[slot];
            EXPECT_EQ(child.rotation, slot == 0 ? 26.0f : 35.0f); // CRT(1): R2 = 18467, k = 1.
            EXPECT_EQ(child.typedOwner, owner);
            EXPECT_EQ(child.life, 10);
            EXPECT_EQ(child.scale, 2.0f);
            EXPECT_EQ(child.position, flare.transform.position);
            EXPECT_EQ(child.angles, glm::vec3(0));
            EXPECT_EQ(child.light, glm::vec3(1, 0.5f, 0.1f));
        }
    }
    m_particles.Clear();
    auto& flare = m_effects.Effects()[0];
    m_actor.flags78 = 0x4010;
    ASSERT_TRUE(Client::Scenes::StepSkillFlare(flare, m_joints, &JointHooks, m_random, &m_particles).HasValue());
    EXPECT_EQ(flare.life, 9);
    EXPECT_TRUE(LiveParticles(m_particles).empty());
    EXPECT_EQ(m_random.State(), AfterRandoms(3));
    m_actor.flags78 = 0x10;
    m_actor.alive = false;
    ASSERT_TRUE(Client::Scenes::StepSkillFlare(flare, m_joints, &JointHooks, m_random, &m_particles).HasValue());
    EXPECT_FALSE(flare.alive);
    EXPECT_EQ(LiveParticles(m_particles).size(), 2u);
    EXPECT_EQ(m_random.State(), AfterRandoms(5));

    m_particles.Clear();
    flare.alive = true;
    m_actor.alive = true;
    m_actor.flags78 = 0;
    ASSERT_TRUE(Client::Scenes::StepSkillFlare(flare, m_joints, &JointHooks, m_random, &m_particles).HasValue());
    EXPECT_FALSE(flare.alive);
    EXPECT_EQ(LiveParticles(m_particles).size(), 2u);
    EXPECT_EQ(m_random.State(), AfterRandoms(7));

    flare.alive = true;
    flare.ownerContext = nullptr;
    const auto ownerless = Client::Scenes::StepSkillFlare(flare, m_joints, &JointHooks, m_random, &m_particles);
    ASSERT_FALSE(ownerless.HasValue());
    EXPECT_NE(ownerless.Error().find("0x52a7ec"), std::string::npos);
    EXPECT_EQ(flare.life, 0);
}

TEST_F(CharacterEffectStepsTest, TheSwellBoneTableCoversAllSevenMirroredPairs)
{
    constexpr std::array<float, 7> Bones{25, 26, 27, 20, 34, 35, 36};
    m_actor.alive = true;
    m_actor.flags78 = 0x10;
    for (std::size_t index = 0; index < Bones.size(); ++index)
    {
        m_particles.Clear();
        m_effects.Clear();
        m_random = CrtRandom(1);
        const auto skipped = RandomsBeforePair([](int) { return true; }, [index](int value)
                                               { return static_cast<std::size_t>(value % 7) == index; });
        for (std::size_t draw = 0; draw < skipped; ++draw)
            (void)m_random.Next();
        ASSERT_TRUE(m_effects.CreateSkillFlare({}, {}, {}, Client::World::SwellFlareSubtype, &m_actor).has_value());
        const auto stepped =
            Client::Scenes::StepSkillFlare(m_effects.Effects()[0], m_joints, &JointHooks, m_random, &m_particles,
                                           {Client::World::ParticleOwnerKind::Actor, Slot});
        ASSERT_TRUE(stepped.HasValue());
        ASSERT_EQ(LiveParticles(m_particles).size(), 2u);
        EXPECT_EQ(m_particles.All()[0].rotation, Bones[index]);
        EXPECT_EQ(m_particles.All()[1].rotation, Bones[Bones.size() - 1 - index]);
        EXPECT_EQ(m_random.State(), AfterRandoms(skipped + 2));
    }
}

TEST(SwellLifePipelineTest, RunsFromParentToRetainedActorBonesAndExistingFlareDraw)
{
    auto world = std::make_unique<Client::Kernel::WorldState>();
    auto& actor = world->Actors()[3];
    actor.type = 0x20a;
    actor.alive = true;
    actor.byte88 = 1;
    actor.flags78 = 0x10;
    actor.scale = 7; // Read by other effects: this particle does not apply the scale.
    actor.position = {100, 200, 300};
    actor.bodyPose.assign(40, Client::Math::BoneTransform(1));
    actor.bodyPose[36][3] = {11, 22, 33};
    actor.bodyPose[25][3] = {44, 55, 66};
    world->PlaceModel(actor);
    auto& effects = world->Map().effects;
    ASSERT_TRUE(
        effects.CreateSkillFlare({1, 2, 3}, {}, glm::vec3(1), Client::World::SwellFlareSubtype, &actor).has_value());
    CrtRandom random(7);
    const Client::World::ParticleOwner owner{Client::World::ParticleOwnerKind::Actor, 3};
    ASSERT_TRUE(Client::Scenes::StepSkillFlare(effects.Effects()[0], world->DrawTail().joints, &JointHooks, random,
                                               &world->Particles(), owner)
                    .HasValue());
    const auto resolver = Client::Scenes::ParticleOwnerBonesOf(*world);
    ASSERT_TRUE(
        Client::Scenes::StepGameParticles(world->Particles(), random, 0, {}, 0, {}, {}, {}, nullptr, {}, resolver)
            .HasValue());
    const auto children = LiveParticles(world->Particles());
    ASSERT_EQ(children.size(), 2u);
    EXPECT_EQ(children[0].rotation, 36.0f);
    EXPECT_EQ(children[1].rotation, 25.0f);
    constexpr std::array SpeedBits{0x40b970a4u, 0x410bd70au};
    constexpr std::array ScaleBits{0x3ff9652cu, 0x3ff86595u};
    for (std::size_t slot = 0; slot < children.size(); ++slot)
    {
        auto child = children[slot];
        EXPECT_EQ(std::bit_cast<std::uint32_t>(child.verticalSpeed), SpeedBits[slot]);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(child.scale), ScaleBits[slot]);
        EXPECT_EQ(child.light, (glm::vec3{Float(0x3f3da12fu), Float(0x3ebda12fu), Float(0x3d97b426u)}));
        const glm::vec3 base = slot == 0 ? glm::vec3(111, 222, 333) : glm::vec3(144, 255, 366);
        EXPECT_EQ(
            child.position,
            (glm::vec3{base.x, base.y, static_cast<float>(static_cast<double>(base.z) + Float(SpeedBits[slot]))}));
        const auto drawn = Client::Tests::DrawOneParticle(child, glm::vec2(64), std::uint8_t{3});
        ASSERT_TRUE(drawn.HasValue());
        ASSERT_TRUE(drawn.Value().has_value());
        EXPECT_EQ(drawn.Value()->texture, 0x5ac);
        EXPECT_EQ(drawn.Value()->blend, Client::Tests::RecordedParticleDraw::Blend::Additive);
        EXPECT_EQ(drawn.Value()->light, child.light);
        EXPECT_EQ(drawn.Value()->rotation, child.rotation);
    }
    EXPECT_EQ(random.State(), 0x2e12d22fu);
    // Reusing the actor's slot keeps the native address: alive and flags do not invalidate it, type and byte88 do.
    actor.alive = false;
    actor.flags78 = 0;
    actor.position = {500, 600, 700};
    EXPECT_EQ(resolver(children[0]), std::optional(glm::vec3(511, 622, 733)));
    actor.type = 0x194;
    EXPECT_FALSE(resolver(children[0]).has_value());
    actor.type = 0x20a;
    actor.byte88 = 0;
    EXPECT_FALSE(resolver(children[0]).has_value());
    actor.byte88 = 1;
    actor.bodyPose.clear();
    EXPECT_EQ(resolver(children[0]), std::optional(actor.position));
}
} // namespace
