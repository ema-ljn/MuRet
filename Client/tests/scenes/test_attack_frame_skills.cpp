#include "Skills/AttackFrameSkills.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Kernel/JointRequest.h"
#include "Tables/PlayerModels.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Math/HalfLifeMath.h"
#include "Models/LogicalAccess.h"
#include "Math/OriginalRetainedMath.h"
#include "Models/Skeleton.h"
#include "Kernel/ActorState.h"
#include "Skills/AttackStep.h"
#include "Actors/CharacterStep.h"
#include "Kernel/DrawTailState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Actors/PlayerActionSpeeds.h"
#include "Kernel/WorldMap.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Particles/Particles.h"
#include "Effects/ModelEffects.h"
#include "Effects/WizardModelEffects.h"
#include "Terrain/WorldTerrain.h"
#include "Audio/SoundId.h"

#include "support/PlayerActors.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Client::Skills::StepTheFrameSkill: the cases of 0x4f97b0 by the skill of +0x38e (0x4f9899..0x4fab5c, the table of
// 0x4fb10c), and 0x4f8f5b of the tail of 0x4f5b70. 06, "Las habilidades de los frames del ataque".
namespace
{
using Client::Actors::CharacterStepContext;
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Kernel::JointState;
using Client::Kernel::WorldState;
using Client::Skills::StepTheAttack;
using Client::Skills::StepTheFrameSkill;

constexpr std::uint32_t Seed = 0x2468ace1u;
constexpr std::size_t HeroSlot = 42;
constexpr std::size_t VictimSlot = 7;
constexpr std::uint16_t Player = Client::Tables::PlayerSkeletonModel;
constexpr std::uint16_t NoTarget = 0xffff;
// The own record closed at 0: the 3848 of 0x40b4d0.
constexpr int OwnRecord = Client::Models::OwnRecordCloseRandoms;
// [0x6c88ac] as 0x4f987d leaves it before the case.
constexpr std::int32_t FullThreshold = 15;
// A value no case writes in [0x6c88ac], to see whether it was touched.
constexpr std::int32_t UntouchedThreshold = 7;
constexpr std::uint8_t Bones = 4;
constexpr std::uint8_t DummyBone = 3;
constexpr std::uint8_t HandBone = 2;
const std::vector<std::int32_t> Maps{0, 2};

int DrawsSince(std::uint32_t seed, const CrtRandom& random)
{
    constexpr int Limit = 1'000'000;
    CrtRandom probe(seed);
    int draws = 0;
    while (probe.State() != random.State() && draws < Limit)
    {
        static_cast<void>(probe.Next());
        ++draws;
    }
    return draws;
}

template <typename T> bool HasError(const Client::Core::Expected<T, std::string>& result, std::string_view address)
{
    return !result.HasValue() && result.Error().starts_with(address);
}

template <typename T> std::string ErrorOf(const Client::Core::Expected<T, std::string>& result)
{
    return result.HasValue() ? std::string() : result.Error();
}

Client::Models::Skeleton SkeletonWithBones()
{
    Client::Models::Skeleton skeleton = Client::Tests::PlayerActionsSkeleton();
    skeleton.bones.resize(Bones);
    skeleton.bones[DummyBone].dummy = true;
    return skeleton;
}

// A map's world with the hero, the striker, in slot 42; the WorldState on the heap.
struct FrameWorld
{
    explicit FrameWorld(std::int32_t map)
    {
        world.Runtime().map = map;
        world.Runtime().hero = HeroSlot;
        world.Runtime().heroSlot = static_cast<std::int32_t>(HeroSlot);
        world.Runtime().skillThreshold = FullThreshold;
        world.OwnCharacter().ClearEquipmentForEntry();
        // 0x4fc450's timers far from 0, as in test_attack_step.cpp.
        constexpr std::uint16_t FarTimer = 1000;
        world.OwnCharacter().word3e = FarTimer;
        world.OwnCharacter().word40 = FarTimer;
        world.OwnCharacter().word42 = FarTimer;
        terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    }

    ActorState& Striker(std::uint8_t action, float frame, std::uint8_t step)
    {
        ActorState& striker = world.Actors()[HeroSlot];
        striker = Client::Tests::UnarmedPlayer(2);
        striker.alive = true;
        striker.byte88 = 1;
        striker.pk = 3;
        striker.animation.action = action;
        striker.animation.frame = frame;
        striker.byte381 = step;
        striker.attackTarget = NoTarget;
        return striker;
    }

    CharacterStepContext Context()
    {
        return CharacterStepContext{.world = world,
                                    .random = random,
                                    .playerSkeleton = &skeleton,
                                    .terrain = &terrain,
                                    .particles = withParticles ? &particles : nullptr,
                                    .joints = withJoints ? std::span<JointState>(joints) : std::span<JointState>{},
                                    .soundRequest = [this](Client::Audio::SoundId sound)
                                    { sounds.push_back(sound.Value()); }};
    }

    Client::Core::Expected<bool, std::string> Step(ActorState& actor, std::uint8_t skill)
    {
        return StepTheFrameSkill(actor, skill, HeroSlot, Context());
    }

    std::int32_t Threshold()
    {
        return world.Runtime().skillThreshold;
    }

    std::unique_ptr<WorldState> ownedWorld = std::make_unique<WorldState>();
    WorldState& world = *ownedWorld;
    Client::Terrain::WorldTerrain terrain;
    CrtRandom random{Seed};
    Client::Models::Skeleton skeleton = SkeletonWithBones();
    std::vector<std::int32_t> sounds;
    Client::Particles::Particles particles;
    bool withParticles = true;
    std::vector<JointState> joints = std::vector<JointState>(16);
    bool withJoints = true;
};

std::vector<const Client::Particles::Particle*> AliveOf(const Client::Particles::Particles& particles,
                                                        std::int32_t type)
{
    std::vector<const Client::Particles::Particle*> found;
    for (const Client::Particles::Particle& particle : particles.All())
    {
        if (particle.alive && particle.type == type)
            found.push_back(&particle);
    }
    return found;
}

std::vector<const JointState*> AliveJoints(const std::vector<JointState>& joints)
{
    std::vector<const JointState*> found;
    for (const JointState& joint : joints)
    {
        if (joint.alive)
            found.push_back(&joint);
    }
    return found;
}

// 0x4f9893: the skills without a case in the table (0x29, 0x2a, 0x2d, 0x2e, 0x32, 0x33, 0x36, 0x3a, 0x3b, 0x40 and 0x42
// go to 0x4fab5d) and those outside 0x28..0x4a continue in 0x4f97b0, with no rand() or writes.
TEST(AttackFrameSkillsTest, TheSkillsWithoutACaseGoOn)
{
    for (const std::int32_t map : Maps)
    {
        for (const std::uint8_t skill : std::vector<std::uint8_t>{0x00, 0x03, 0x18, 0x27, 0x29, 0x2a, 0x2d, 0x2e, 0x32,
                                                                  0x33, 0x36, 0x3a, 0x3b, 0x40, 0x42, 0x4b, 0xff})
        {
            SCOPED_TRACE(::testing::Message() << "mapa " << map << ", habilidad " << static_cast<int>(skill));
            FrameWorld tested(map);
            ActorState& hero = tested.Striker(0x45, 20.0f, 9);

            const auto stepped = tested.Step(hero, skill);

            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            EXPECT_FALSE(stepped.Value());
            EXPECT_EQ(hero.byte381, 9);
            EXPECT_EQ(tested.Threshold(), FullThreshold);
            EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
        }
    }
}

// The cases without effects: +0x381 at 15 when the striker reaches its action and frame (≥, NaN does not pass), or
// from a +0x381; 0x41, at 10 if it does not reach them; 0x44, 0x46 and 0x47 always; 0x43 and 0x45 nothing. No rand()
// or [0x6c88ac].
TEST(AttackFrameSkillsTest, TheQuietSkillsEndTheAttackAtTheirFrame)
{
    struct Case
    {
        std::uint8_t skill;
        std::uint16_t type;
        std::uint8_t action;
        float frame;
        std::uint8_t step;
        std::uint8_t expected;
    };
    constexpr float NaN = std::numeric_limits<float>::quiet_NaN();
    const std::vector<Case> cases{
        {0x28, Player, 0x45, 14.0f, 4, 15}, {0x28, Player, 0x45, 13.9f, 4, 4},  {0x28, Player, 0x44, 14.0f, 4, 4},
        {0x28, 0x152, 0x45, 14.0f, 4, 4},   {0x28, Player, 0x45, NaN, 4, 4},    {0x30, Player, 0x3f, 0.0f, 10, 15},
        {0x30, Player, 0x3f, 0.0f, 9, 9},   {0x30, Player, 0x40, 0.0f, 12, 12}, {0x31, Player, 0x40, 5.0f, 2, 15},
        {0x31, Player, 0x41, 5.0f, 2, 15},  {0x31, Player, 0x41, 4.9f, 2, 2},   {0x31, Player, 0x42, 5.0f, 2, 2},
        {0x35, Player, 0x22, 5.0f, 2, 15},  {0x35, Player, 0x97, 5.0f, 2, 15},  {0x35, Player, 0x98, 5.0f, 2, 2},
        {0x35, Player, 0x21, 5.0f, 2, 2},   {0x35, 0x152, 3, 5.0f, 2, 15},      {0x35, 0x1c9, 4, 5.0f, 2, 15},
        {0x35, 0x1ca, 4, 5.0f, 2, 2},       {0x35, 0x151, 4, 5.0f, 2, 2},       {0x35, 0x152, 5, 5.0f, 2, 2},
        {0x35, 0x152, 3, 4.9f, 2, 2},       {0x3c, Player, 0x4c, 3.0f, 2, 15},  {0x3c, Player, 0x4e, 3.0f, 2, 15},
        {0x3c, Player, 0x58, 2.9f, 2, 2},   {0x3d, Player, 0x58, 3.0f, 2, 15},  {0x3d, Player, 0x4d, 3.0f, 2, 2},
        {0x4a, Player, 0x4e, 3.0f, 2, 15},  {0x4a, 0x152, 0x4e, 3.0f, 2, 2},    {0x3e, Player, 0x53, 5.0f, 2, 15},
        {0x3e, Player, 0x53, 4.9f, 2, 2},   {0x3f, Player, 0x4d, 0.0f, 6, 15},  {0x3f, Player, 0x4f, 0.0f, 6, 15},
        {0x3f, Player, 0x5a, 0.0f, 5, 5},   {0x41, Player, 0x94, 5.5f, 2, 15},  {0x41, Player, 0x51, 5.5f, 2, 15},
        {0x41, Player, 0x5b, 5.5f, 2, 15},  {0x41, Player, 0x5b, 5.4f, 2, 10},  {0x41, Player, 0x50, 6.0f, 12, 10},
        {0x41, 0x152, 0x94, 6.0f, 2, 10},   {0x41, Player, 0x94, NaN, 2, 10},   {0x44, 0x152, 0, 0.0f, 2, 15},
        {0x46, Player, 0, 0.0f, 2, 15},     {0x47, 0x152, 0, 0.0f, 2, 15},      {0x48, 0x152, 0, 3.5f, 2, 15},
        {0x48, Player, 0, 3.4f, 2, 2},      {0x49, Player, 0x86, 3.0f, 2, 15},  {0x49, Player, 0x86, 2.9f, 2, 2},
        {0x49, 0x152, 0x86, 3.0f, 2, 2},    {0x43, Player, 0x45, 20.0f, 2, 2},  {0x45, Player, 0x45, 20.0f, 2, 2},
    };
    for (const std::int32_t map : Maps)
    {
        for (const Case& probe : cases)
        {
            SCOPED_TRACE(::testing::Message()
                         << "mapa " << map << ", habilidad " << static_cast<int>(probe.skill) << ", tipo " << probe.type
                         << ", acción " << static_cast<int>(probe.action) << ", frame " << probe.frame << ", +0x381 "
                         << static_cast<int>(probe.step));
            FrameWorld tested(map);
            ActorState& hero = tested.Striker(probe.action, probe.frame, probe.step);
            hero.type = probe.type;
            tested.world.Runtime().skillThreshold = UntouchedThreshold;

            const auto stepped = tested.Step(hero, probe.skill);

            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            EXPECT_TRUE(stepped.Value());
            EXPECT_EQ(hero.byte381, probe.expected);
            EXPECT_EQ(tested.Threshold(), UntouchedThreshold);
            EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
            EXPECT_TRUE(tested.sounds.empty());
        }
    }
}

// 0x2c (0x4fa895..0x4fa993): past frame 5 (>, not ≥), +0x381 at 15; each step, the point with two rand() % 30 − 15
// and z + 20, and four times a rand() % 60 + 150 in the angles' x, the 0x617/0 joint (two rand()) and the 0x5d9/2
// fire (two rand()): 22 rand() per step.
TEST(AttackFrameSkillsTest, TheBoltsOf2cGoOutEveryStep)
{
    for (const std::int32_t map : Maps)
    {
        for (const float frame : {5.0f, 5.5f})
        {
            SCOPED_TRACE(::testing::Message() << "mapa " << map << ", frame " << frame);
            FrameWorld tested(map);
            ActorState& hero = tested.Striker(0x85, frame, 4);
            hero.position = {1000.0f, 2000.0f, 50.0f};
            hero.angles = {0.0f, 0.0f, 90.0f};

            const auto stepped = tested.Step(hero, 0x2c);

            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            EXPECT_TRUE(stepped.Value());
            EXPECT_EQ(hero.byte381, frame > 5.0f ? 15 : 4);
            EXPECT_EQ(DrawsSince(Seed, tested.random), 22);
            CrtRandom replay(Seed);
            const int x = replay.Next() % 30;
            const int y = replay.Next() % 30;
            const glm::vec3 point{static_cast<float>(1000.0 + (x - 15)), static_cast<float>(2000.0 + (y - 15)), 70.0f};
            const auto joints = AliveJoints(tested.joints);
            ASSERT_EQ(joints.size(), 4u);
            for (const JointState* joint : joints)
            {
                const int pitch = (replay.Next() % 60) + 150;
                replay.Discard(4);
                EXPECT_EQ(joint->type, 0x617);
                EXPECT_EQ(joint->subtype, 0);
                EXPECT_EQ(joint->position, point);
                EXPECT_EQ(joint->angles, glm::vec3(static_cast<float>(pitch), 0.0f, 90.0f));
                EXPECT_EQ(joint->ownerContext, nullptr);
            }
            const auto flames = AliveOf(tested.particles, Client::Particles::FireParticle);
            ASSERT_EQ(flames.size(), 4u);
            for (const Client::Particles::Particle* flame : flames)
            {
                EXPECT_EQ(flame->subtype, 2);
                EXPECT_EQ(flame->position, point);
            }
            EXPECT_EQ(tested.Threshold(), FullThreshold);
        }
    }
}

// 0x2c without the joint pool cuts at the first one (0x4fa964, with the rand() of the point and the first angle), and
// without the particles, at the first fire (0x4fa980), with the first joint created.
TEST(AttackFrameSkillsTest, TheBoltsOf2cCutWithoutTheirPools)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        FrameWorld withoutJoints(map);
        withoutJoints.withJoints = false;
        ActorState& first = withoutJoints.Striker(0x85, 0.0f, 4);
        const auto cut = withoutJoints.Step(first, 0x2c);
        EXPECT_TRUE(HasError(cut, "0x4fa964")) << ErrorOf(cut);
        EXPECT_EQ(DrawsSince(Seed, withoutJoints.random), 3);

        FrameWorld withoutParticles(map);
        withoutParticles.withParticles = false;
        ActorState& second = withoutParticles.Striker(0x85, 0.0f, 4);
        const auto flamed = withoutParticles.Step(second, 0x2c);
        EXPECT_TRUE(HasError(flamed, "0x4fa980")) << ErrorOf(flamed);
        EXPECT_EQ(DrawsSince(Seed, withoutParticles.random), 5);
        EXPECT_EQ(AliveJoints(withoutParticles.joints).size(), 1u);
    }
}

// 0x2b (0x4f98a0..0x4f9bcd): the hand bone of 0x4f9780 (the left with 0x25 or 0x26) in Player.bmd. A dummy or past
// the count (JGE: equal neither), nothing. Up to 8, the hand at +0x194, moved 300 along the heading; at 8, sound 0x71;
// from 2 to 8, three 0x10f/2 joints with three rand() each; from 6 to 12, the two 0x812/1 effects (no rand()); from
// 12, +0x381 at [0x6c88ac].
TEST(AttackFrameSkillsTest, TheHandOf2bChargesUntilEight)
{
    struct Case
    {
        std::string_view name;
        std::uint8_t action;
        std::uint8_t step;
        std::uint8_t rightBone;
        std::uint8_t leftBone;
        std::string_view cut;
        int draws;
        bool handPoint;
        std::uint8_t expectedStep;
        std::vector<std::int32_t> sounds;
    };
    const std::vector<Case> cases{
        {"en 1", 0x2b, 1, HandBone, 0, "", 0, true, 1, {}},
        {"en 1, con la izquierda de la 0x25", 0x25, 1, DummyBone, HandBone, "", 0, true, 1, {}},
        {"en 1, con la izquierda de la 0x26", 0x26, 1, Bones, HandBone, "", 0, true, 1, {}},
        {"en 2", 0x2b, 2, HandBone, 0, "", 9, true, 2, {}},
        {"en 8", 0x2b, 8, HandBone, 0, "", 9, true, 8, {0x71}},
        {"en 9", 0x2b, 9, HandBone, 0, "", 0, false, 9, {}},
        {"en 12", 0x2b, 12, HandBone, 0, "", 0, false, FullThreshold, {}},
        {"en 13", 0x2b, 13, HandBone, 0, "", 0, false, FullThreshold, {}},
        {"en 14", 0x2b, 14, HandBone, 0, "", 0, false, FullThreshold, {}},
        {"un dummy", 0x2b, 13, DummyBone, 0, "", 0, false, 13, {}},
        {"el hueso de la cuenta", 0x2b, 13, Bones, 0, "", 0, false, 13, {}},
        {"el dummy de la izquierda con la 0x25", 0x25, 13, HandBone, DummyBone, "", 0, false, 13, {}},
    };
    for (const std::int32_t map : Maps)
    {
        for (const Case& probe : cases)
        {
            SCOPED_TRACE(::testing::Message() << "mapa " << map << ", " << probe.name);
            FrameWorld tested(map);
            ActorState& hero = tested.Striker(probe.action, 0.0f, probe.step);
            hero.hands[0].bone = probe.rightBone;
            hero.hands[1].bone = probe.leftBone;
            hero.position = {1000.0f, 2000.0f, 0.0f};
            hero.angles = {0.0f, 0.0f, 0.0f};
            hero.scale = 1.0f;
            hero.bodyPose.assign(Bones, Client::Math::BoneTransform(1.0f));
            hero.bodyPose[HandBone][3] = {10.0f, 20.0f, 30.0f};
            const glm::vec3 untouched{-1.0f, -2.0f, -3.0f};
            hero.skillHandPoint = untouched;
            tested.world.PlaceModel(hero); // The caller 0x4fd69b publishes before entering the frame helper.

            const auto stepped = tested.Step(hero, 0x2b);

            if (probe.cut.empty())
            {
                ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
                EXPECT_TRUE(stepped.Value());
            }
            else
            {
                EXPECT_TRUE(HasError(stepped, probe.cut)) << ErrorOf(stepped);
            }
            EXPECT_EQ(DrawsSince(Seed, tested.random), probe.draws);
            EXPECT_EQ(hero.byte381, probe.expectedStep);
            EXPECT_EQ(tested.sounds, probe.sounds);
            EXPECT_EQ(hero.skillHandPoint, probe.handPoint ? glm::vec3(1010.0f, 1720.0f, 30.0f) : untouched);
            EXPECT_EQ(tested.Threshold(), FullThreshold);
        }
    }
}

// 0x4f9918..0x4f9a07: from 2 to 8, three times o's position with z + 120 and three rand() % 601 − 300 added, minus the
// heading's sine · 1400 in x and plus the cosine in y (FSIN and FCOS retained, one rounding with the FMUL: PC53), and
// the 0x10f/2 joint of width 40 with o as owner and its angles, which leaves from its hand (AuraJoints.cpp). With the
// pool full, the nine rand() anyway and no joint; without the pool, cuts.
TEST(AttackFrameSkillsTest, TheBoltsOf2bLeaveFromTheHand)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        FrameWorld tested(map);
        ActorState& hero = tested.Striker(0x2b, 0.0f, 4);
        hero.hands[0].bone = HandBone;
        hero.position = {1000.0f, 2000.0f, 33.5f};
        hero.angles = {0.0f, 0.0f, 30.0f};
        hero.scale = 1.0f;
        tested.world.PlaceModel(hero);

        const auto stepped = tested.Step(hero, 0x2b);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        // The (x, y, z) bits of each joint in creation order, computed separately (x87 with 64-bit FSIN and FCOS and
        // rounding to 53 at the FMUL and to 24 at each FSTP) with seed 0x2468ace1.
        constexpr std::array<std::array<std::uint32_t, 3>, 3> Points{{{0x423c0001u, 0x4550f6f8u, 0x439e4000u},
                                                                      {0x4408c000u, 0x455986f8u, 0x439c4000u},
                                                                      {0x41f80003u, 0x454206f8u, 0x429d0000u}}};
        const auto bolts = AliveJoints(tested.joints);
        ASSERT_EQ(bolts.size(), Points.size());
        for (std::size_t i = 0; i < bolts.size(); ++i)
        {
            const JointState* bolt = bolts[i];
            const glm::vec3 point{std::bit_cast<float>(Points[i][0]), std::bit_cast<float>(Points[i][1]),
                                  std::bit_cast<float>(Points[i][2])};
            EXPECT_EQ(bolt->type, 0x10f);
            EXPECT_EQ(bolt->subtype, 2);
            EXPECT_EQ(bolt->position, point);
            EXPECT_EQ(bolt->targetPosition, point);
            EXPECT_EQ(bolt->angles, hero.angles);
            EXPECT_EQ(bolt->width, 40.0f);
            EXPECT_EQ(bolt->ownerContext, &hero);
            EXPECT_EQ(bolt->ownerKind, Client::Kernel::JointOwnerKind::Actor);
            EXPECT_EQ(bolt->texture, 0x62c);
        }
        EXPECT_EQ(DrawsSince(Seed, tested.random), 9);

        FrameWorld full(map);
        for (JointState& joint : full.joints)
            joint.alive = true;
        ActorState& crowded = full.Striker(0x2b, 0.0f, 4);
        crowded.hands[0].bone = HandBone;
        full.world.PlaceModel(crowded);
        ASSERT_TRUE(full.Step(crowded, 0x2b).HasValue());
        EXPECT_EQ(DrawsSince(Seed, full.random), 9);

        FrameWorld without(map);
        without.withJoints = false;
        ActorState& alone = without.Striker(0x2b, 0.0f, 4);
        alone.hands[0].bone = HandBone;
        without.world.PlaceModel(alone);
        const auto cut = without.Step(alone, 0x2b);
        EXPECT_TRUE(HasError(cut, "0x4f99fe")) << ErrorOf(cut);
        EXPECT_EQ(DrawsSince(Seed, without.random), 3);
    }
}

// 0x2b's hand with the heading: + sine · 300 in x and − cosine · 300 in y, on the x87 and stored as a float. FSIN and
// FCOS stay retained and the FMUL by 300 rounds once (0x4f9a61..0x4f9a80): the PC53 profile. Another type takes its
// skeleton from the model library; without it, cuts (0x4f98e4).
TEST(AttackFrameSkillsTest, TheHandOf2bTurnsWithTheHeading)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        FrameWorld tested(map);
        ActorState& hero = tested.Striker(0x2b, 0.0f, 1);
        hero.hands[0].bone = HandBone;
        hero.position = {1000.0f, 2000.0f, 0.0f};
        hero.angles = {0.0f, 0.0f, 30.0f};
        hero.scale = 1.0f;
        tested.world.PlaceModel(hero);

        const auto stepped = tested.Step(hero, 0x2b);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        const double angle = 30.0 * static_cast<double>(std::bit_cast<float>(0x3c8efa35u));
        const auto sine = Client::Math::Detail::SineProductPC53(angle, 300.0f);
        const auto cosine = Client::Math::Detail::CosineProductPC53(angle, 300.0f);
        ASSERT_TRUE(sine.has_value() && cosine.has_value());
        EXPECT_EQ(hero.skillHandPoint.x, static_cast<float>(*sine + 1000.0));
        EXPECT_EQ(hero.skillHandPoint.y, static_cast<float>(2000.0 - *cosine));
        EXPECT_EQ(hero.skillHandPoint.z, 0.0f);

        ActorState& other = tested.Striker(0x2b, 0.0f, 1);
        other.type = 0x152;
        const auto cut = tested.Step(other, 0x2b);
        EXPECT_TRUE(HasError(cut, "0x4f98e4")) << ErrorOf(cut);
    }
}

// 0x2f (0x4f9bce..0x4f9e60): at 10, sound 0x7d; at 4, the hand's 0x813 effect; at 8, the 0x812/0 (no rand()); at 13
// and 14, three times three rand() and the 0x10f effect. Otherwise, nothing.
TEST(AttackFrameSkillsTest, TheStepsOf2f)
{
    struct Case
    {
        std::uint8_t step;
        std::string_view cut;
        int draws;
        std::vector<std::int32_t> sounds;
    };
    const std::vector<Case> cases{
        {1, "", 0, {}},  {4, "", 0, {}},  {8, "", 0, {}},  {10, "", 0, {0x7d}},
        {12, "", 0, {}}, {13, "", 9, {}}, {14, "", 9, {}}, {15, "", 0, {}},
    };
    for (const std::int32_t map : Maps)
    {
        for (const Case& probe : cases)
        {
            SCOPED_TRACE(::testing::Message() << "mapa " << map << ", +0x381 " << static_cast<int>(probe.step));
            FrameWorld tested(map);
            ActorState& hero = tested.Striker(0x42, 0.0f, probe.step);

            const auto stepped = tested.Step(hero, 0x2f);

            if (probe.cut.empty())
            {
                ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
                EXPECT_TRUE(stepped.Value());
            }
            else
            {
                EXPECT_TRUE(HasError(stepped, probe.cut)) << ErrorOf(stepped);
            }
            EXPECT_EQ(DrawsSince(Seed, tested.random), probe.draws);
            EXPECT_EQ(tested.sounds, probe.sounds);
            EXPECT_EQ(hero.byte381, probe.step);
        }
    }
}

// 0x4f9d44..0x4f9e52 (CL11): each 0x10f of 0x2f leaves 145 in front of o along its heading (PC53) and 110 higher, with
// three rand() % 60 − 30 added; its arguments: o's angles, the light (0.3, 0.3, 0.3), the hand's model as subtype and
// o as owner. With the table full, the loop still draws its nine rand(). The base, computed separately for heading 30
// from (1000, 2000, 300): FSIN and FCOS times 145 in double, stored as floats.
TEST(AttackFrameSkillsTest, TheRocksOf2fFallInFrontOfTheStriker)
{
    constexpr std::uint16_t HandModel = 0x205;
    const glm::vec3 base{std::bit_cast<float>(0x44861000u), std::bit_cast<float>(0x44ea4da4u), 410.0f};
    for (const bool full : {false, true})
    {
        SCOPED_TRACE(full);
        FrameWorld tested(0);
        ActorState& hero = tested.Striker(0x42, 0.0f, 13);
        hero.position = glm::vec3(1000.0f, 2000.0f, 300.0f);
        hero.angles = glm::vec3(0.0f, 0.0f, 30.0f);
        hero.hands[0].model = HandModel;
        auto effects = tested.world.Map().effects.Effects();
        if (full)
        {
            for (Client::Effects::ModelEffect& effect : effects)
                effect.alive = true;
        }

        const auto stepped = tested.Step(hero, 0x2f);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(DrawsSince(Seed, tested.random), 9);
        if (full)
            continue;
        CrtRandom expected(Seed);
        for (std::size_t rock = 0; rock < 3; ++rock)
        {
            SCOPED_TRACE(rock);
            glm::vec3 point = base;
            for (int axis = 0; axis < 3; ++axis)
            {
                const int offset = expected.Next() % 60 - 30;
                point[axis] = static_cast<float>(static_cast<double>(offset) + static_cast<double>(point[axis]));
            }
            const Client::Effects::ModelEffect& effect = effects[rock];
            ASSERT_TRUE(effect.alive);
            EXPECT_EQ(effect.type, 0x10f);
            EXPECT_EQ(effect.subtype, HandModel);
            EXPECT_EQ(effect.transform.position, point);
            EXPECT_EQ(effect.light, glm::vec3(0.3f));
            EXPECT_EQ(effect.ownerContext, &hero);
            EXPECT_EQ(effect.life, 20);
        }
        EXPECT_FALSE(effects[3].alive);
    }
}

// 0x4f9a86..0x4f9b69 and 0x4f9c0e..0x4f9d3c: 0x2b's 0x812/1, (+0x381 − 8) · 10 + 100 in front of the hand along the
// heading; 0x2f's 0x813 at the hand, with its model as subtype and the light (1, 1, 0.5); and 0x2f's 0x812/0, 50 in
// front of o and 110 higher. All with o as owner.
TEST(AttackFrameSkillsTest, TheEmittersOf2bAnd2fStartAtTheHand)
{
    const auto emitters = [](const FrameWorld& tested)
    {
        std::vector<const Client::Effects::ModelEffect*> found;
        for (const auto& effect : tested.world.Map().effects.Effects())
        {
            if (effect.alive)
                found.push_back(&effect);
        }
        return found;
    };
    {
        FrameWorld tested(0);
        ActorState& hero = tested.Striker(0x2b, 0.0f, 9);
        hero.hands[0].bone = HandBone;
        hero.position = {1000.0f, 2000.0f, 0.0f};
        hero.angles = {0.0f, 0.0f, 0.0f};
        hero.scale = 1.0f;
        hero.bodyPose.assign(Bones, Client::Math::BoneTransform(1.0f));
        hero.bodyPose[HandBone][3] = {10.0f, 20.0f, 30.0f};
        tested.world.PlaceModel(hero);
        ASSERT_TRUE(tested.Step(hero, 0x2b).HasValue());
        const auto made = emitters(tested);
        ASSERT_EQ(made.size(), 2u);
        for (const auto* effect : made)
        {
            EXPECT_EQ(effect->type, 0x812);
            EXPECT_EQ(effect->subtype, 1);
            EXPECT_EQ(effect->life, 10);
            EXPECT_EQ(effect->ownerContext, &hero);
            EXPECT_EQ(effect->transform.position, glm::vec3(1010.0f, 1910.0f, 30.0f));
        }
    }
    {
        FrameWorld tested(0);
        ActorState& hero = tested.Striker(0x42, 0.0f, 8);
        hero.position = {100.0f, 200.0f, 300.0f};
        hero.angles = {0.0f, 0.0f, 0.0f};
        ASSERT_TRUE(tested.Step(hero, 0x2f).HasValue());
        const auto made = emitters(tested);
        ASSERT_EQ(made.size(), 2u);
        for (const auto* effect : made)
        {
            EXPECT_EQ(effect->type, 0x812);
            EXPECT_EQ(effect->subtype, 0);
            EXPECT_EQ(effect->transform.position, glm::vec3(100.0f, 150.0f, 410.0f));
        }
    }
    {
        FrameWorld tested(0);
        ActorState& hero = tested.Striker(0x42, 0.0f, 4);
        hero.hands[0].model = 0x0812;
        ASSERT_TRUE(tested.Step(hero, 0x2f).HasValue());
        const auto made = emitters(tested);
        ASSERT_EQ(made.size(), 1u);
        EXPECT_EQ(made[0]->type, 0x813);
        EXPECT_EQ(made[0]->subtype, 0x812);
        EXPECT_EQ(made[0]->life, 5);
        EXPECT_EQ(made[0]->light, glm::vec3(1.0f, 1.0f, 0.5f));
        EXPECT_EQ(made[0]->transform.angles, glm::vec3(0.0f));
    }
}

// 0x34 (0x4f9e61..0x4f9ef2): the 0x20a in actions 0x22 to 0x97, from frame 5, goes back to 5 with the previous key at
// 4; at 3 it creates the 0x621/0 effect and plays 0x81 before the 5 of [0x6c88ac].
TEST(AttackFrameSkillsTest, ThePiercingShotHoldsItsFrame)
{
    struct Case
    {
        std::string_view name;
        std::uint16_t type;
        std::uint8_t action;
        float frame;
        std::uint8_t step;
        float expectedFrame;
        std::int16_t expectedKey;
    };
    const std::vector<Case> cases{
        {"en el frame 6", Player, 0x2e, 6.0f, 4, 5.0f, 4},
        {"en el frame 5", Player, 0x97, 5.0f, 4, 5.0f, 4},
        {"antes del 5", Player, 0x2e, 4.9f, 4, 4.9f, 0},
        {"en la 0x21", Player, 0x21, 6.0f, 4, 6.0f, 0},
        {"en la 0x98", Player, 0x98, 6.0f, 4, 6.0f, 0},
        {"otro tipo", 0x152, 0x2e, 6.0f, 4, 6.0f, 0},
        {"en 3", Player, 0x2e, 6.0f, 3, 5.0f, 4},
        {"en 3 fuera del rango", Player, 0x21, 2.0f, 3, 2.0f, 0},
        {"en 3 con otro modelo", 0x152, 0x2e, 2.0f, 3, 2.0f, 0},
    };
    for (const std::int32_t map : Maps)
    {
        for (const Case& probe : cases)
        {
            SCOPED_TRACE(::testing::Message() << "mapa " << map << ", " << probe.name);
            FrameWorld tested(map);
            ActorState& hero = tested.Striker(probe.action, probe.frame, probe.step);
            hero.type = probe.type;
            hero.light = {0.2f, 0.4f, 0.6f};

            const auto stepped = tested.Step(hero, 0x34);

            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            EXPECT_TRUE(stepped.Value());
            EXPECT_EQ(tested.Threshold(), 5);
            if (probe.step == 3)
            {
                const auto& effect = tested.world.Map().effects.Effects()[0];
                EXPECT_TRUE(effect.alive);
                EXPECT_EQ(effect.type, 0x621);
                EXPECT_EQ(effect.life, 10);
                EXPECT_EQ(effect.ownerContext, &hero);
                EXPECT_EQ(effect.light, hero.light);
            }
            EXPECT_EQ(hero.animation.frame, probe.expectedFrame);
            EXPECT_EQ(hero.animation.previousKey, probe.expectedKey);
            EXPECT_EQ(hero.byte381, probe.step);
            EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
            EXPECT_EQ(tested.sounds,
                      probe.step == 3 ? (std::vector<std::int32_t>{0x81}) : (std::vector<std::int32_t>{}));
        }
    }
}

// 0x34 through the attack step, without a target and with the bow on foot (0x2e): from +0x381 at 1, [0x6c88ac] at 5
// and +0x381 goes up to 2 and 3, emits 0x621/0 and continues to 4. From 4 it reaches 5 and runs the skill: 0x53e2b0
// with 0x34 (sound 0x55 and [0x5767948] at 0x34; without a bow in the own record, no arrow) and the volley's count.
TEST(AttackFrameSkillsTest, ThePiercingShotReachesTheArrowThroughTheAttack)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        FrameWorld counting(map);
        ActorState& striker = counting.Striker(0x2e, 0.0f, 1);
        striker.byte38e = 0x34;
        for (const std::uint8_t next : std::vector<std::uint8_t>{2, 3})
        {
            const auto stepped = StepTheAttack(striker, HeroSlot, counting.Context());
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            EXPECT_EQ(striker.byte381, next);
            EXPECT_EQ(counting.Threshold(), 5);
        }
        const auto glowing = StepTheAttack(striker, HeroSlot, counting.Context());
        ASSERT_TRUE(glowing.HasValue()) << ErrorOf(glowing);
        EXPECT_EQ(striker.byte381, 4);
        EXPECT_EQ(counting.sounds, (std::vector<std::int32_t>{0x81}));
        EXPECT_TRUE(counting.world.Map().effects.Effects()[0].alive);
        // Each step: the read of 0x4f97b0 and the rand() of 0x4f5b97 (without a target, the tail does not read +0x38e).
        EXPECT_EQ(DrawsSince(Seed, counting.random), 2 + 2 + 2);

        FrameWorld shooting(map);
        ActorState& archer = shooting.Striker(0x2e, 0.0f, 4);
        archer.byte38e = 0x34;
        const auto shot = StepTheAttack(archer, HeroSlot, shooting.Context());
        ASSERT_TRUE(shot.HasValue()) << shot.Error();
        EXPECT_EQ(archer.byte381, 0);
        EXPECT_EQ(archer.byte38e, 0);
        EXPECT_EQ(shooting.world.Runtime().lastShotSkill, 0x34u);
        EXPECT_EQ(shooting.sounds, std::vector<std::int32_t>{0x55});
        EXPECT_EQ(shooting.world.OwnCharacter().bytef04, 1);
        // 0x4f97b0 and 0x4f5b97; the head, the flag of 0x502b12 and the shot's two reads; the slot of 0x59fb80, the
        // weapons of 0x53d9a0 and the volley, each with the own record; the write of +0x38e.
        EXPECT_EQ(DrawsSince(Seed, shooting.random), 2 + 4 + (3 * OwnRecord) + 1);
    }
}

// 0x37 (0x4f9ef3..0x4fa490), only the 0x20a in action 0x3d, on all maps: at 1 and 2, the 0x621/1 effect over o;
// before frame 3 it leaves with [0x6c88ac] at 15; from 3, the chosen target's key in +0x8a (0x558eb0), sound 0x72, the
// slot of 0x37 in the own record (0x59fb80, 3848 rand() on closing it; 0x37 in slot 5), the 0x5be/0 (0x4f9fae,
// Effects/SkillBurstEffects) with o as owner, the key and the slot, no rand(), and the speed of action 0x3d with
// another own record; +0x381 at 15.
TEST(AttackFrameSkillsTest, TheThrowOf37)
{
    struct Case
    {
        std::string_view name;
        std::uint16_t type;
        std::uint8_t action;
        float frame;
        std::uint8_t step;
        bool thrown;
    };
    const std::vector<Case> cases{
        {"otro tipo", 0x152, 0x3d, 3.0f, 3, false},
        {"otra acción", Player, 0x3c, 3.0f, 3, false},
        {"en 1", Player, 0x3d, 3.0f, 1, true},
        {"en 2", Player, 0x3d, 0.0f, 2, false},
        {"antes del frame 3", Player, 0x3d, 2.9f, 3, false},
        {"en el frame 3", Player, 0x3d, 3.0f, 3, true},
    };
    constexpr std::size_t ThrowIndex = 5;
    for (const std::int32_t map : Maps)
    {
        for (const Case& probe : cases)
        {
            SCOPED_TRACE(::testing::Message() << "mapa " << map << ", " << probe.name);
            FrameWorld tested(map);
            ActorState& hero = tested.Striker(probe.action, probe.frame, probe.step);
            hero.type = probe.type;
            hero.word8a = 0x1234;
            hero.position = {1000.0f, 2000.0f, 300.0f};
            hero.angles = {0.0f, 0.0f, 30.0f};
            tested.world.OwnCharacter().SetSkillAt(ThrowIndex, 0x37);
            tested.world.Runtime().skillThreshold = UntouchedThreshold;

            const auto stepped = tested.Step(hero, 0x37);

            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            EXPECT_TRUE(stepped.Value());
            EXPECT_EQ(DrawsSince(Seed, tested.random), probe.thrown ? 2 * OwnRecord : 0);
            EXPECT_EQ(tested.sounds, probe.thrown ? std::vector<std::int32_t>{0x72} : std::vector<std::int32_t>{});
            EXPECT_EQ(tested.Threshold(), FullThreshold);
            // With no chosen target, 0x558eb0 gives −1.
            EXPECT_EQ(static_cast<std::uint16_t>(hero.word8a), probe.thrown ? 0xffff : 0x1234);
            EXPECT_EQ(hero.byte381, probe.thrown ? 15 : probe.step);
            const auto effects = tested.world.Map().effects.Effects();
            const bool held = probe.type == Player && probe.action == 0x3d && probe.step <= 2;
            if (held)
            {
                EXPECT_EQ(effects[0].type, 0x621);
                EXPECT_EQ(effects[0].subtype, 1);
                EXPECT_EQ(effects[0].ownerContext, &hero);
                EXPECT_EQ(effects[0].life, 20);
            }
            const auto& sweep = effects[held ? 1 : 0];
            EXPECT_EQ(sweep.alive, probe.thrown);
            EXPECT_EQ(effects[held ? 0 : 1].alive, held);
            if (!probe.thrown)
                continue;
            EXPECT_EQ(sweep.type, 0x5be);
            EXPECT_EQ(sweep.subtype, 0);
            EXPECT_EQ(sweep.ownerContext, &hero);
            EXPECT_EQ(sweep.disturbanceState, 0xffff);
            EXPECT_EQ(sweep.byte89, ThrowIndex);
            EXPECT_EQ(sweep.transform.position, hero.position);
            EXPECT_EQ(sweep.start, hero.position);
            EXPECT_EQ(sweep.copiedAngles, glm::vec3(0.0f, 0.0f, 75.0f));
            EXPECT_EQ(sweep.life, 30);
        }
    }
}

TEST(AttackFrameSkillsTest, Skill39AttemptsEveryKeyAcrossPartialAndFullPools)
{
    for (const float frame : {2.4f, 5.5f})
    {
        for (const int capacity : {0, 1, 2, 3})
        {
            SCOPED_TRACE(testing::Message() << frame << "/" << capacity);
            FrameWorld tested(0);
            ActorState& actor = tested.Striker(0x84, frame, 4);
            actor.position = {100, 200, 300};
            actor.angles = {10, 20, 30};
            tested.world.Runtime().skillThreshold = UntouchedThreshold;
            for (auto& joint : tested.joints)
                joint.alive = true;
            for (int index = 0; index < capacity; ++index)
                tested.joints[static_cast<std::size_t>(index * 2)].alive = false;
            const auto stepped = tested.Step(actor, 0x39);
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            const std::array earlyKeys{2, 3};
            const std::array lateKeys{0, 1, 4};
            const auto keys = frame > 5 ? std::span<const int>(lateKeys) : std::span<const int>(earlyKeys);
            const auto admitted = std::min(static_cast<std::size_t>(capacity), keys.size());
            for (std::size_t index = 0; index < admitted; ++index)
            {
                const auto& joint = tested.joints[index * 2];
                EXPECT_EQ(joint.type, 0x60f);
                EXPECT_EQ(joint.subtype, 0x17);
                EXPECT_EQ(joint.key, keys[index]);
                EXPECT_EQ(joint.ownerContext, nullptr);
                EXPECT_EQ(joint.angles, actor.angles);
            }
            for (std::size_t index = 0; index < tested.joints.size(); ++index)
            {
                if (index % 2 == 0 && index / 2 < admitted)
                    continue;
                EXPECT_EQ(tested.joints[index].type, 0);
                EXPECT_EQ(tested.joints[index].alive,
                          !(index % 2 == 0 && index / 2 < static_cast<std::size_t>(capacity)));
            }
            EXPECT_EQ(actor.byte381, frame > 5 ? 15 : 4);
            EXPECT_EQ(tested.Threshold(), FullThreshold);
            EXPECT_EQ(tested.sounds, std::vector<std::int32_t>{0x270});
            EXPECT_EQ(tested.random.State(), Seed);
        }
    }
}

// 0x38 (0x4fa4a8..0x4fa5d3): other than the 0x20a in 0x8d (TheSpinOf38ThrowsFiveWaves), [0x6c88ac] at 15. 0x39
// (0x4fa994..0x4faaca), of any type: past frame 5, the 0x60f/0x17 joints (0x4fa9ca); between 2.3f and 2.6f (without
// the edges), those of keys 2 and 3 (0x4faa81); always [0x6c88ac] at 15 for 0x39.
TEST(AttackFrameSkillsTest, TheSpinOf38AndTheBoltsOf39)
{
    struct Case
    {
        std::string_view name;
        std::uint8_t skill;
        std::uint16_t type;
        std::uint8_t action;
        float frame;
        std::string_view cut;
    };
    constexpr float NaN = std::numeric_limits<float>::quiet_NaN();
    const std::vector<Case> cases{
        {"0x38 en la 0x8c", 0x38, Player, 0x8c, 0.0f, ""}, {"0x38 de otro tipo", 0x38, 0x152, 0x8d, 0.0f, ""},
        {"0x39 en 5,5", 0x39, Player, 0x8d, 5.5f, ""},     {"0x39 de otro tipo en 5,5", 0x39, 0x152, 3, 5.5f, ""},
        {"0x39 en 5", 0x39, Player, 0x8d, 5.0f, ""},       {"0x39 en 2,4", 0x39, Player, 0x8d, 2.4f, ""},
        {"0x39 en 2,3", 0x39, Player, 0x8d, 2.3f, ""},     {"0x39 en 2,6", 0x39, Player, 0x8d, 2.6f, ""},
        {"0x39 con NaN", 0x39, Player, 0x8d, NaN, ""},
    };
    for (const std::int32_t map : Maps)
    {
        for (const Case& probe : cases)
        {
            SCOPED_TRACE(::testing::Message() << "mapa " << map << ", " << probe.name);
            FrameWorld tested(map);
            ActorState& hero = tested.Striker(probe.action, probe.frame, 4);
            hero.type = probe.type;
            tested.world.Runtime().skillThreshold = UntouchedThreshold;

            const auto stepped = tested.Step(hero, probe.skill);

            if (probe.cut.empty())
            {
                ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
                EXPECT_TRUE(stepped.Value());
                EXPECT_EQ(tested.Threshold(), FullThreshold);
            }
            else
            {
                EXPECT_TRUE(HasError(stepped, probe.cut)) << ErrorOf(stepped);
                EXPECT_EQ(tested.Threshold(), UntouchedThreshold);
            }
            const bool emitted =
                probe.skill == 0x39 && (probe.frame > 5.0f || (probe.frame > 2.3f && probe.frame < 2.6f));
            EXPECT_EQ(hero.byte381, probe.skill == 0x39 && probe.frame > 5.0f ? 15 : 4);
            EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
            EXPECT_EQ(tested.sounds, emitted ? std::vector<std::int32_t>{0x270} : std::vector<std::int32_t>{});
        }
    }
}

// 0x4fa4c1..0x4fa5c3, 0x38 of the 0x20a in 0x8d: five 0xce/2 from o with o as owner, the heading 40 less and then 20
// more each time; each creation counts in the own record (+0x8c with the count, which goes up). Then sound 0x72,
// +0x381 at 15 and [0x6c88ac] at 15.
TEST(AttackFrameSkillsTest, TheSpinOf38ThrowsFiveWaves)
{
    FrameWorld tested(0);
    ActorState& hero = tested.Striker(0x8d, 0.0f, 4);
    hero.angles = glm::vec3(0.0f, 0.0f, 100.0f);
    hero.light = glm::vec3(0.5f, 0.25f, 1.0f);
    tested.world.OwnCharacter().bytef04 = 7;
    tested.world.Runtime().skillThreshold = UntouchedThreshold;

    const auto stepped = tested.Step(hero, 0x38);

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_TRUE(stepped.Value());
    std::vector<float> headings;
    std::vector<std::uint16_t> counts;
    for (const Client::Effects::ModelEffect& effect : tested.world.Map().effects.Effects())
    {
        if (!effect.alive || effect.type != Client::Effects::MagicEffect)
            continue;
        EXPECT_EQ(effect.subtype, 2);
        EXPECT_EQ(effect.ownerContext, &hero);
        EXPECT_EQ(effect.transform.position, hero.position);
        EXPECT_EQ(effect.light, hero.light);
        headings.push_back(effect.transform.angles.z);
        counts.push_back(effect.word8c);
    }
    EXPECT_EQ(headings, (std::vector<float>{60.0f, 80.0f, 100.0f, 120.0f, 140.0f}));
    EXPECT_EQ(counts, (std::vector<std::uint16_t>{7, 8, 9, 10, 11}));
    EXPECT_EQ(tested.world.OwnCharacter().bytef04, 12);
    EXPECT_EQ(DrawsSince(Seed, tested.random), 5 * OwnRecord);
    EXPECT_EQ(hero.byte381, 15);
    EXPECT_EQ(tested.Threshold(), FullThreshold);
    EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x72}));
}

// 0x4f8f5b, the 3 without a case of +0x376 in the tail of 0x4f5b70 (through the attack step, with the target in the
// pool): the right hand's bone against the bone count (JL: equal passes, Q-C1093); outside, nothing; otherwise the two
// 0x614/0 joints (0x4f8fef and 0x4f9012, no rand() of their own) and the 0x5ca/0 particle (0x4f9034, two rand()).
TEST(AttackFrameSkillsTest, TheHandBoltsOfTheTail)
{
    struct Case
    {
        std::string_view name;
        std::uint16_t type;
        std::uint8_t bone;
        bool cast;
    };
    const std::vector<Case> cases{
        {"el hueso 0", Player, 0, true},
        {"el hueso de la cuenta", Player, Bones, true},
        {"pasado la cuenta", Player, Bones + 1, false},
        {"otro tipo", 0x152, 0, true},
    };
    for (const std::int32_t map : Maps)
    {
        for (const Case& probe : cases)
        {
            SCOPED_TRACE(::testing::Message() << "mapa " << map << ", " << probe.name);
            FrameWorld tested(map);
            ActorState& hero = tested.Striker(0x23, 0.0f, 1);
            hero.type = probe.type;
            hero.byte38e = 3;
            hero.hands[0].bone = probe.bone;
            hero.attackTarget = static_cast<std::uint16_t>(VictimSlot);

            const auto stepped = StepTheAttack(hero, HeroSlot, tested.Context());

            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            EXPECT_EQ(hero.byte381, 2);
            const auto joints = AliveJoints(tested.joints);
            EXPECT_EQ(joints.size(), probe.cast ? 2u : 0u);
            for (const JointState* joint : joints)
            {
                EXPECT_EQ(joint->type, 0x614);
                EXPECT_EQ(joint->subtype, 0);
            }
            // The read of 0x4f97b0, the rand() of 0x4f5b97 and the reads of 0x4f811b and 0x4f8a2c; with the joints, the
            // two of the 0x5ca/0.
            EXPECT_EQ(DrawsSince(Seed, tested.random), probe.cast ? 6 : 4);
        }
    }
}
TEST(AttackFrameSkillsTest, FullPoolsContinueTheNativeFrameTails)
{
    struct Case
    {
        std::uint8_t skill, action, step;
        float frame;
        int draws;
        std::uint8_t finalStep;
        int threshold;
        std::vector<std::int32_t> sounds;
    };
    const std::vector<Case> cases{
        {0x2b, 0x2b, 6, 0.0f, 9, 6, 15, {}},
        {0x2b, 0x2b, 8, 0.0f, 9, 8, 15, {0x71}},
        {0x2b, 0x2b, 12, 0.0f, 0, 15, 15, {}},
        {0x2f, 0x42, 4, 0.0f, 0, 4, 15, {}},
        {0x2f, 0x42, 8, 0.0f, 0, 8, 15, {}},
        {0x2f, 0x42, 13, 0.0f, 9, 13, 15, {}},
        {0x34, 0x2e, 3, 6.0f, 0, 3, 5, {0x81}},
        {0x37, 0x3d, 1, 2.0f, 0, 1, 15, {}},
        {0x37, 0x3d, 2, 3.0f, 2 * OwnRecord, 15, 15, {0x72}},
        {0x38, 0x8d, 2, 0.0f, 0, 15, 15, {0x72}},
        {0x39, 0x84, 2, 5.1f, 0, 15, 15, {0x270}},
        {0x39, 0x84, 2, 2.4f, 0, 2, 15, {0x270}},
    };
    for (const auto& probe : cases)
    {
        SCOPED_TRACE(testing::Message() << int{probe.skill} << "/" << int{probe.step});
        FrameWorld tested(0);
        ActorState& actor = tested.Striker(probe.action, probe.frame, probe.step);
        actor.hands[0].bone = HandBone;
        actor.bodyPose.assign(Bones, Client::Math::BoneTransform(1.0f));
        actor.attackTarget = 4;
        tested.world.Actors()[4].alive = true;
        tested.world.OwnCharacter().attackSpeed = 123;
        for (auto& effect : tested.world.Map().effects.Effects())
            effect.alive = true;
        for (auto& joint : tested.joints)
            joint.alive = true;
        const auto stepped = tested.Step(actor, probe.skill);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_TRUE(stepped.Value());
        EXPECT_EQ(DrawsSince(Seed, tested.random), probe.draws);
        EXPECT_EQ(actor.byte381, probe.finalStep);
        EXPECT_EQ(tested.Threshold(), probe.threshold);
        EXPECT_EQ(tested.sounds, probe.sounds);
        if (probe.skill == 0x2b && probe.step >= 10)
        {
            EXPECT_EQ(tested.world.Actors()[4].byte1a0, 0x23);
        }
        if (probe.skill == 0x37 && probe.frame >= 3.0f)
        {
            const float product = static_cast<float>(123.0 * static_cast<double>(0.004f));
            const float speed = static_cast<float>(static_cast<double>(product) + static_cast<double>(0.54f));
            EXPECT_EQ(tested.world.ActionSpeeds().Speed(0x3d, -1.0f), speed);
        }
    }
}

TEST(AttackFrameSkillsTest, Skill2bReadsThePlacementPublishedBeforeTheActorMoves)
{
    FrameWorld tested(0);
    auto& actor = tested.Striker(0x2b, 0.0f, 6);
    actor.hands[0].bone = HandBone;
    actor.position = {10.0f, 20.0f, 30.0f};
    actor.scale = 0.5f;
    actor.bodyPose.assign(Bones, Client::Math::BoneTransform(1.0f));
    actor.bodyPose[HandBone][3] = {1.0f, 2.0f, 3.0f};
    tested.world.PlaceModel(actor.type, {.scale = 2.0f, .origin = {100.0f, 200.0f, 300.0f}});
    for (auto& joint : tested.joints)
        joint.alive = true;
    for (auto& effect : tested.world.Map().effects.Effects())
        effect.alive = true;
    const auto stepped = tested.Step(actor, 0x2b);
    ASSERT_TRUE(stepped.HasValue()) << ErrorOf(stepped);
    EXPECT_EQ(actor.skillHandPoint, (glm::vec3{102.0f, -96.0f, 306.0f}));
    EXPECT_EQ(actor.position, (glm::vec3{10.0f, 20.0f, 30.0f}));
    EXPECT_EQ(DrawsSince(Seed, tested.random), 9);
}

} // namespace
