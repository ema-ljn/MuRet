#include "Skills/AttackSkillCases.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Kernel/JointRequest.h"
#include "Tables/SkillTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Models/LogicalAccess.h"
#include "Math/OriginalAngles.h"
#include "Math/OriginalRotation.h"
#include "Kernel/ActorState.h"
#include "Effects/ArrowEffects.h"
#include "Actors/CharacterStep.h"
#include "Joints/FaunaJointCreation.h"
#include "Kernel/DrawTailState.h"
#include "Effects/HighModelEffects.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Effects/SkillBurstEffects.h"
#include "Kernel/WorldMap.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Effects/ModelEffects.h"
#include "Kernel/KalimaWater.h"
#include "MapScripts/Kalima/KalimaWorld.h"
#include "MapScripts/Kalima/KalimaWaves.h"
#include "Particles/Particles.h"
#include "Effects/WizardModelEffects.h"
#include "Terrain/WorldTerrain.h"
#include "Audio/SoundId.h"

#include "support/KalimaWaves.h"
#include "support/LentWorld.h"
#include "support/PlayerActors.h"
#include "support/TableFiles.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// StepTheHeadSkill (0x4fdeeb), StepTheTargetSkill (0x504372) and SparkTheTarget (0x5042ce). 06, "El paso del ataque".
namespace
{
using Client::Actors::CharacterStepContext;
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Kernel::JointState;
using Client::Kernel::WorldState;
using Client::Skills::SparkTheTarget;
using Client::Skills::StepTheHeadSkill;
using Client::Skills::StepTheTargetSkill;

constexpr std::uint32_t Seed = 0x2468ace1u;
constexpr std::size_t HeroSlot = 42;
constexpr std::size_t StrikerSlot = 5;
constexpr std::size_t VictimSlot = 9;
// The own record closed at 0: the 3848 rand() of 0x40b4d0.
constexpr int OwnRecord = Client::Models::OwnRecordCloseRandoms;
// One read of +0x38e: one rand() when it closes at 0.
constexpr int SkillRead = 1;
// The Skill.bmd entry of 0x5526f0, closed at 0.
constexpr int SkillEntry = static_cast<int>(Client::Tables::SkillRecordSize);
constexpr std::array<std::int32_t, 3> Maps{0, 2, 7};

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

bool HasError(const Client::Core::Expected<void, std::string>& stepped, std::string_view address)
{
    return !stepped.HasValue() && stepped.Error().find(address) != std::string::npos;
}

std::string ErrorOf(const Client::Core::Expected<void, std::string>& stepped)
{
    return stepped.HasValue() ? std::string() : stepped.Error();
}

Client::Tables::SkillTable SkillsWithRange(std::uint8_t range)
{
    constexpr std::size_t AroundSkill = 0x4c;
    constexpr std::size_t RangeOffset = 0x26;
    std::vector<std::byte> plain(Client::Tables::SkillTableSize);
    plain[AroundSkill * Client::Tables::SkillRecordSize + RangeOffset] = static_cast<std::byte>(range);
    const std::vector<std::byte> encrypted =
        Client::Tests::EncryptRecords(std::move(plain), Client::Tables::SkillRecordSize);
    return Client::Tables::SkillTable(std::span<const std::byte>(encrypted));
}

struct SkillWorld
{
    explicit SkillWorld(std::int32_t map = 0)
    {
        world.Runtime().map = map;
        world.Runtime().hero = HeroSlot;
        world.Runtime().heroSlot = static_cast<std::int32_t>(HeroSlot);
        world.OwnCharacter().ClearEquipmentForEntry();
        Hero() = Player(1);
        Striker() = Player(0);
        Victim() = Player(2);
    }

    static ActorState Player(std::uint8_t characterClass)
    {
        ActorState actor = Client::Tests::UnarmedPlayer(characterClass);
        actor.alive = true;
        actor.byte88 = 1;
        actor.pk = 3;
        actor.animation.action = 1;
        actor.attackTarget = 0xffff;
        actor.skillTarget4c = -1;
        return actor;
    }

    // A live monster (+0x88 at 2) at `x` from o on the x axis.
    ActorState& Monster(std::size_t slot, float x)
    {
        ActorState& monster = world.Actors()[slot];
        monster = ActorState{};
        monster.alive = true;
        monster.byte88 = 2;
        monster.type = 0x0e;
        monster.animation.action = 1;
        monster.position = Striker().position + glm::vec3(x, 0.0f, 0.0f);
        return monster;
    }

    ActorState& Hero()
    {
        return world.Actors()[HeroSlot];
    }
    ActorState& Striker()
    {
        return world.Actors()[StrikerSlot];
    }
    ActorState& Victim()
    {
        return world.Actors()[VictimSlot];
    }

    CharacterStepContext Context()
    {
        return CharacterStepContext{.world = world,
                                    .random = random,
                                    .skills = skills,
                                    .terrain = &terrain,
                                    .particles = withParticles ? &world.Particles() : nullptr,
                                    .joints = withJoints ? std::span<JointState>(joints) : std::span<JointState>{},
                                    .soundRequest = [this](Client::Audio::SoundId sound)
                                    { sounds.push_back(sound.Value()); },
                                    .soundAt =
                                        [this](Client::Audio::SoundId sound, const glm::vec3& origin)
                                    {
                                        sounds.push_back(sound.Value());
                                        soundOrigins.push_back(origin);
                                    },
                                    .controlDown = controlDown};
    }

    Client::Core::Expected<void, std::string> Head(std::uint8_t skill)
    {
        return StepTheHeadSkill(Striker(), StrikerSlot, skill, Context());
    }

    Client::Core::Expected<void, std::string> WithTarget(std::uint8_t skill)
    {
        return StepTheTargetSkill(Striker(), Victim(), StrikerSlot, skill, Context());
    }

    std::unique_ptr<WorldState> ownedWorld = Client::Tests::LentWorld();
    WorldState& world = *ownedWorld;
    Client::Terrain::WorldTerrain terrain;
    CrtRandom random{Seed};
    std::vector<std::int32_t> sounds;
    std::vector<glm::vec3> soundOrigins;
    bool controlDown = false;
    const Client::Tables::SkillTable* skills = nullptr;
    std::vector<JointState> joints = std::vector<JointState>(16);
    bool withJoints = true;
    bool withParticles = false;
};

std::vector<const Client::Effects::ModelEffect*> AliveEffects(const WorldState& world)
{
    std::vector<const Client::Effects::ModelEffect*> found;
    for (const Client::Effects::ModelEffect& effect : world.Map().effects.Effects())
    {
        if (effect.alive)
            found.push_back(&effect);
    }
    return found;
}

// The head cases that create what MuRet does not have (CL11) cut at their creation, after the rand() before it
// (0x59fb80 with the own record closed, and 0x0e's two from 0x51fe10, which cuts at its first spark). 0x29, 0x27 and
// 0x0d (switch batch), 0x35 and 0x41 (joints batch) and 0x31 (high batch) have their own tests.
TEST(AttackSkillCasesTest, TheHeadCasesCutAfterTheirDraws)
{
    struct Case
    {
        std::uint8_t skill;
        std::string_view cut;
        int draws;
    };
    const std::array cases{
        Case{0x0e, "0x51fe9e", 2},
    };
    for (const std::int32_t map : Maps)
    {
        for (const Case& probe : cases)
        {
            SCOPED_TRACE(testing::Message() << "mapa " << map << ", skill " << int{probe.skill});
            SkillWorld tested(map);
            tested.Striker().flags78 = 0x4000;

            const auto stepped = tested.Head(probe.skill);

            EXPECT_TRUE(HasError(stepped, probe.cut)) << ErrorOf(stepped);
            EXPECT_EQ(DrawsSince(Seed, tested.random), probe.draws);
            EXPECT_EQ(tested.Striker().flags78, 0x4000u);
            EXPECT_TRUE(tested.sounds.empty());
        }
    }
}

// 0x4fe795..0x4fe981: 0x35's four 0x619/0xa start at o; their first segment sits at (sin 4.5 · 80; cos 4.5 · 80; 300)
// or (cos 22.5 · 80; sin 22.5 · 80; 300) plus o, by its bits: FSIN/FCOS at 64 bits, the product by 80 and the sum with
// o in PC53, then the fstp to float. With the new slot (width 0 at 0x530afa), the four corners fall exactly on the
// point.
TEST(AttackSkillCasesTest, TheEnergyCaseHangsFourJointsAroundItsCaster)
{
    SkillWorld tested;
    tested.Striker().position = {1000.0f, 2000.0f, 50.0f};

    const auto stepped = tested.Head(0x35);

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
    const std::array<float, 4> turns{45.0f, 405.0f, 225.0f, 450.0f};
    for (std::size_t index = 0; index < turns.size(); ++index)
    {
        SCOPED_TRACE(index);
        const JointState& joint = tested.joints[index];
        ASSERT_TRUE(joint.alive);
        EXPECT_EQ(joint.type, 0x619);
        EXPECT_EQ(joint.subtype, 0xa);
        EXPECT_EQ(joint.width, 15.0f);
        EXPECT_EQ(joint.angles, glm::vec3(-45.0f, 0.0f, turns[index]));
        EXPECT_EQ(joint.position, tested.Striker().position);
        EXPECT_EQ(joint.ownerContext, &tested.Striker());
        // 921.7976 and 1983.1364 (4.5 rad); 930.1356 and 1961.0260 (22.5 rad).
        const glm::vec3 from =
            index < 2 ? glm::vec3(std::bit_cast<float>(0x4466730cu), std::bit_cast<float>(0x44f7e45du), 350.0f)
                      : glm::vec3(std::bit_cast<float>(0x446888aeu), std::bit_cast<float>(0x44f520d5u), 350.0f);
        for (const glm::vec3& corner : joint.points[0])
            EXPECT_EQ(corner, from);
    }
    EXPECT_FALSE(tested.joints[4].alive);
    EXPECT_EQ(tested.sounds, std::vector<std::int32_t>{0x7f});
}

// 0x4feb3b..0x4febd3: 0x41 creates the effect 0x62c at o's point (0, −90, −50) (80 higher in action 0x51), with life 0,
// and its case (0x51f25e) the five 0x62c: /1, /0 of width 250, /2, /3 and /4. Four rand(), from the /1..4 volley
// (0x53479d). Sounds 0x1cd.
TEST(AttackSkillCasesTest, TheSpiralCaseRaisesFiveJointsFromItsEffect)
{
    for (const std::uint8_t action : {std::uint8_t{0}, std::uint8_t{0x51}, std::uint8_t{0x5b}})
    {
        SCOPED_TRACE(int{action});
        SkillWorld tested;
        tested.Striker().position = {1000.0f, 2000.0f, 50.0f};
        tested.Striker().animation.action = action;
        const float z = action == 0x51 ? 80.0f : action == 0x5b ? 40.0f : 0.0f;

        const auto stepped = tested.Head(0x41);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(DrawsSince(Seed, tested.random), 4);
        const auto effects = AliveEffects(tested.world);
        ASSERT_EQ(effects.size(), 1u);
        EXPECT_EQ(effects.front()->type, 0x62c);
        EXPECT_EQ(effects.front()->life, 0);
        EXPECT_EQ(effects.front()->transform.position, glm::vec3(1000.0f, 1910.0f, z));
        constexpr std::array<std::pair<std::int32_t, float>, 5> Spirals{
            {{1, 100.0f}, {0, 250.0f}, {2, 100.0f}, {3, 100.0f}, {4, 100.0f}}};
        for (std::size_t index = 0; index < Spirals.size(); ++index)
        {
            SCOPED_TRACE(index);
            const JointState& joint = tested.joints[index];
            ASSERT_TRUE(joint.alive);
            EXPECT_EQ(joint.type, 0x62c);
            EXPECT_EQ(joint.subtype, Spirals[index].first);
            EXPECT_EQ(joint.width, Spirals[index].second);
            EXPECT_EQ(joint.pathIncrement, glm::vec3(1000.0f, 1910.0f, z + 150.0f));
            EXPECT_EQ(joint.ownerContext, &tested.Striker());
        }
        EXPECT_FALSE(tested.joints[5].alive);
        EXPECT_EQ(tested.sounds, std::vector<std::int32_t>{0x1cd});
    }
}

// 0x4fe015..0x4fe13f: 0x30's 36 joints 0x613/2 hang from o 100 higher with the angles (−10, 0, 10 · i), no rand();
// 0x53265e and 0x533173: blend 1, the second face, speed 50, light 0.5, life 20, three points and the start at +0x24.
// In rounds 0 and 20, after the joint, the decal 0x61e/4 over o (0x4fe127), scale rand() % 50 + 50 times 0.04
// (0x51ef79); at the end, sound 0x7f.
TEST(AttackSkillCasesTest, TheSpiritRingThrowsItsJointsAndTwoDecals)
{
    SkillWorld tested;
    tested.joints.resize(40);
    tested.Striker().position = {1000.0f, 2000.0f, 50.0f};

    const auto stepped = tested.Head(0x30);

    ASSERT_TRUE(stepped.HasValue()) << ErrorOf(stepped);
    EXPECT_EQ(DrawsSince(Seed, tested.random), 2);
    for (std::size_t index = 0; index < 36; ++index)
    {
        SCOPED_TRACE(index);
        const JointState& joint = tested.joints[index];
        ASSERT_TRUE(joint.alive);
        EXPECT_EQ(joint.type, 0x613);
        EXPECT_EQ(joint.subtype, 2);
        EXPECT_EQ(joint.position, glm::vec3(1000.0f, 2000.0f, 150.0f));
        EXPECT_EQ(joint.angles, glm::vec3(-10.0f, 0.0f, 10.0f * static_cast<float>(index)));
        EXPECT_EQ(joint.width, 60.0f);
        EXPECT_EQ(joint.blend, 1);
        EXPECT_EQ(joint.drawEnabled, 2);
        EXPECT_EQ(joint.speed, 50.0f);
        EXPECT_EQ(joint.light, glm::vec3(0.5f));
        EXPECT_EQ(joint.life, 20);
        EXPECT_EQ(joint.maximumCount, 3);
        EXPECT_EQ(joint.pathIncrement, joint.position);
        EXPECT_EQ(joint.ownerContext, &tested.Striker());
    }
    EXPECT_FALSE(tested.joints[36].alive);
    const auto decals = AliveEffects(tested.world);
    ASSERT_EQ(decals.size(), 2u);
    for (std::size_t index = 0; index < decals.size(); ++index)
    {
        SCOPED_TRACE(index);
        EXPECT_EQ(decals[index]->type, 0x61e);
        EXPECT_EQ(decals[index]->subtype, 4);
        EXPECT_EQ(decals[index]->ownerContext, &tested.Striker());
        EXPECT_EQ(decals[index]->transform.angles, glm::vec3(-10.0f, 0.0f, index == 0 ? 0.0f : 200.0f));
    }
    EXPECT_EQ(tested.sounds, std::vector<std::int32_t>{0x7f});
}

// 0x501bab..0x501ca4: 0x09 makes four rounds; in each, 0x59fb80(9) (the own record closed at 0) and two joints 0x613/0,
// each accessing the own record for +0x9eb (0x532675): 12 closes of 3848. The width-80 one carries key +0x8a, the
// skill's index (0 without it) and +0x1bd; the width-20 one, key −1. Sounds 0x75.
TEST(AttackSkillCasesTest, TheEvilSpiritsThrowEightJointsWithTheVolley)
{
    SkillWorld tested;
    tested.Striker().position = {1000.0f, 2000.0f, 50.0f};
    tested.Striker().word8a = 0x1234;
    tested.Striker().magicKey = 7;
    tested.world.OwnCharacter().bytef04 = 9;

    const auto stepped = tested.Head(0x09);

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawsSince(Seed, tested.random), 12 * OwnRecord);
    for (std::size_t index = 0; index < 8; ++index)
    {
        SCOPED_TRACE(index);
        const JointState& joint = tested.joints[index];
        const bool wide = index % 2 == 0;
        ASSERT_TRUE(joint.alive);
        EXPECT_EQ(joint.type, 0x613);
        EXPECT_EQ(joint.subtype, 0);
        EXPECT_EQ(joint.position, glm::vec3(1000.0f, 2000.0f, 150.0f));
        EXPECT_EQ(joint.angles, glm::vec3(0.0f, 0.0f, 90.0f * static_cast<float>(index / 2)));
        EXPECT_EQ(joint.width, wide ? 80.0f : 20.0f);
        EXPECT_EQ(joint.key, wide ? 0x1234 : -1);
        EXPECT_EQ(joint.skill, 0);
        EXPECT_EQ(joint.skillSerial, wide ? 7 : 0);
        EXPECT_EQ(joint.volley, 9);
        EXPECT_EQ(joint.blend, 3);
        EXPECT_EQ(joint.speed, 70.0f);
        EXPECT_EQ(joint.life, 49);
        EXPECT_EQ(joint.maximumCount, 6);
    }
    EXPECT_FALSE(tested.joints[8].alive);
    EXPECT_EQ(tested.sounds, std::vector<std::int32_t>{0x75});
}

// 0x31 (0x4febdb..0x4fec2b): the 0x6ac over o, with o as owner, its key (+0x8a) and the low byte of [0x5767948], with
// no 0x59fb80 before; its forty joints 0x617/1 draw three rand() each (their creation's and 0x530860's two). Then
// sound 0x72. With the effect table full, neither effect nor rand(), and the same sound.
TEST(AttackSkillCasesTest, TheBreathCaseRaisesTheFireBreathOverTheStriker)
{
    SkillWorld tested;
    ActorState& striker = tested.Striker();
    striker.word8a = 0x1234;
    tested.world.Runtime().lastShotSkill = 0x1f31;

    ASSERT_TRUE(tested.Head(0x31).HasValue());

    const auto alive = AliveEffects(tested.world);
    ASSERT_EQ(alive.size(), 1u);
    const Client::Effects::ModelEffect& breath = *alive.front();
    EXPECT_EQ(breath.type, 0x6ac);
    EXPECT_EQ(breath.subtype, 0);
    EXPECT_EQ(breath.ownerContext, &striker);
    EXPECT_EQ(breath.disturbanceState, 0x1234);
    EXPECT_EQ(breath.byte89, 0x31);
    EXPECT_EQ(breath.light, striker.light);
    EXPECT_EQ(breath.life, 10);
    EXPECT_EQ(DrawsSince(Seed, tested.random), 40 * 3);
    EXPECT_EQ(tested.sounds, std::vector<std::int32_t>{0x72});

    SkillWorld full;
    for (Client::Effects::ModelEffect& effect : full.world.Map().effects.Effects())
        effect.alive = true;
    ASSERT_TRUE(full.Head(0x31).HasValue());
    EXPECT_EQ(DrawsSince(Seed, full.random), 0);
    EXPECT_EQ(full.sounds, std::vector<std::int32_t>{0x72});
}

// 0x0c (0x500c64..0x500ccf): the ray 0x5e8 at the point (−20, −90, 100) of o rotated, with o as owner, its key (+0x8a)
// and the skill's slot (0x59fb80, with the own record closed); sound 0x7a. When o is not the hero, the creation looks
// for no targets.
TEST(AttackSkillCasesTest, TheLightningRayLeavesFromTheHand)
{
    SkillWorld tested;
    ActorState& striker = tested.Striker();
    striker.angles = {0.0f, 0.0f, 90.0f};
    striker.word8a = 0x1234;
    tested.world.Runtime().hero = VictimSlot;

    ASSERT_TRUE(tested.Head(0x0c).HasValue());

    const auto alive = AliveEffects(tested.world);
    ASSERT_EQ(alive.size(), 1u);
    const Client::Effects::ModelEffect& ray = *alive.front();
    EXPECT_EQ(ray.type, 0x5e8);
    EXPECT_EQ(ray.subtype, 0);
    EXPECT_EQ(ray.transform.position, Client::Effects::PointAround(striker, {-20.0f, -90.0f, 100.0f}));
    EXPECT_EQ(ray.ownerContext, &striker);
    EXPECT_EQ(ray.disturbanceState, 0x1234);
    EXPECT_EQ(ray.life, 20);
    EXPECT_EQ(ray.transform.scale, 16.0f);
    EXPECT_EQ(DrawsSince(Seed, tested.random), OwnRecord);
    EXPECT_EQ(tested.sounds, std::vector<std::int32_t>{0x7a});
}

// With the own record open (+0x38e of 0x40ad90 in use), 0x59fb80 draws no rand().
TEST(AttackSkillCasesTest, AnOpenOwnRecordFindsTheSkillWithoutDraws)
{
    SkillWorld tested;
    tested.world.Runtime().ownRecordDepth = 1;
    tested.world.Runtime().hero = VictimSlot;

    const auto stepped = tested.Head(0x0c);

    ASSERT_TRUE(stepped.HasValue()) << ErrorOf(stepped);
    EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
}

// 0x4fdef2..0x4fdf26: 0x1e to 0x24 leave the decal 0x61e/3 over o, with o as owner, no rand() or sound, with or
// without 0x4000. With the table full, nothing.
TEST(AttackSkillCasesTest, TheSummonsLeaveTheirDecalOverTheStriker)
{
    for (const std::int32_t map : Maps)
    {
        for (std::uint8_t skill = 0x1e; skill <= 0x24; ++skill)
        {
            for (const std::uint32_t flags : {0u, 0x4000u})
            {
                SCOPED_TRACE(testing::Message() << "mapa " << map << ", skill " << int{skill} << ", " << flags);
                SkillWorld tested(map);
                ActorState& striker = tested.Striker();
                striker.flags78 = flags;
                striker.position = glm::vec3(1250.0f, 3350.0f, 120.0f);
                striker.angles = glm::vec3(0.0f, 0.0f, 45.0f);

                ASSERT_TRUE(tested.Head(skill).HasValue());

                const auto alive = AliveEffects(tested.world);
                ASSERT_EQ(alive.size(), 1u);
                const Client::Effects::ModelEffect& decal = *alive.front();
                EXPECT_EQ(decal.type, Client::Effects::ColoredDecalEffect);
                EXPECT_EQ(decal.subtype, Client::Effects::SummonDecalSubtype);
                EXPECT_EQ(decal.ownerContext, &striker);
                EXPECT_EQ(decal.transform.position, striker.position);
                EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
                EXPECT_TRUE(tested.sounds.empty());
                EXPECT_EQ(striker.flags78, flags);
            }
        }
    }
    SkillWorld full;
    for (Client::Effects::ModelEffect& effect : full.world.Map().effects.Effects())
    {
        effect.alive = true;
        effect.type = 0x01;
    }
    ASSERT_TRUE(full.Head(0x1e).HasValue());
    EXPECT_EQ(DrawsSince(Seed, full.random), 0);
}

void Flatten(Client::Terrain::WorldTerrain& terrain)
{
    terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    terrain.heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    terrain.heights->cells.fill(150.0f);
}

// 0x500496..0x500c0b, the Flame: the jet 0x5de/0 at the center of the skill's cell (+0x39c/+0x39d) at ground height,
// with o as owner, o's +0x8a, the skill's slot in +0x89 and the count +0xf04 in +0x8c (0x518cf0 reads it with the own
// record open: 3848 rand() when it closes). Only with o the published hero, +0xf04 one more (another record). Before,
// 0x59fb80(5); after, sound 0x79.
TEST(AttackSkillCasesTest, TheFlameLightsItsJetOnTheSkillCell)
{
    for (const std::int32_t map : Maps)
    {
        for (const bool hero : {false, true})
        {
            SCOPED_TRACE(testing::Message() << "mapa " << map << (hero ? ", el héroe" : ", otro"));
            SkillWorld tested(map);
            Flatten(tested.terrain);
            if (hero)
                tested.world.Runtime().hero = StrikerSlot;
            ActorState& striker = tested.Striker();
            striker.skillCellX = 120;
            striker.skillCellY = 33;
            striker.word8a = 0x0123;
            striker.angles = glm::vec3(0.0f, 0.0f, 90.0f);
            striker.light = glm::vec3(0.5f, 0.25f, 1.0f);
            tested.world.OwnCharacter().SetSkillAt(3, 0x05);
            tested.world.OwnCharacter().bytef04 = 0x41;

            ASSERT_TRUE(tested.Head(0x05).HasValue());

            EXPECT_EQ(DrawsSince(Seed, tested.random), (hero ? 3 : 2) * OwnRecord);
            const auto alive = AliveEffects(tested.world);
            ASSERT_EQ(alive.size(), 1u);
            const Client::Effects::ModelEffect& jet = *alive.front();
            EXPECT_EQ(jet.type, Client::Effects::FlameJetEffect);
            EXPECT_EQ(jet.subtype, 0);
            EXPECT_EQ(jet.life, 40);
            EXPECT_EQ(jet.transform.position, glm::vec3(12'050.0f, 3'350.0f, 150.0f));
            EXPECT_EQ(jet.transform.angles, striker.angles);
            EXPECT_EQ(jet.ownerContext, &striker);
            EXPECT_EQ(jet.disturbanceState, 0x0123);
            EXPECT_EQ(jet.byte89, 3);
            EXPECT_EQ(jet.word8c, 0x41);
            EXPECT_EQ(tested.world.OwnCharacter().bytef04, hero ? 0x42 : 0x41);
            EXPECT_EQ(tested.world.Runtime().ownRecordDepth, 0);
            EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x79}));
        }
    }
}

// With the table full, no jet and no record for its creation; the hero's record and the sound remain. Without the
// ground heights, MuRet cuts before 0x59fb80.
TEST(AttackSkillCasesTest, TheFlameWithoutRoomOrGroundSkipsItsJet)
{
    SkillWorld full;
    Flatten(full.terrain);
    full.world.Runtime().hero = StrikerSlot;
    for (Client::Effects::ModelEffect& effect : full.world.Map().effects.Effects())
    {
        effect.alive = true;
        effect.type = 0x01;
    }
    ASSERT_TRUE(full.Head(0x05).HasValue());
    EXPECT_EQ(DrawsSince(Seed, full.random), 2 * OwnRecord);
    EXPECT_EQ(full.world.OwnCharacter().bytef04, 1);
    EXPECT_EQ(full.sounds, (std::vector<std::int32_t>{0x79}));

    SkillWorld bare;
    const auto stepped = bare.Head(0x05);
    EXPECT_TRUE(HasError(stepped, "0x5004ea")) << ErrorOf(stepped);
    EXPECT_EQ(DrawsSince(Seed, bare.random), 0);
    EXPECT_TRUE(bare.sounds.empty());
}

// 0 and those not in the tables of 0x505530 go on at 0x502aff with nothing; 0x37 and 0x38 only leave the right-hand
// weapon − 0x212 in +0x8c and its level in +0x8e.
TEST(AttackSkillCasesTest, TheQuietHeadCasesLeaveAtMostTheWeapon)
{
    for (const std::uint8_t skill : std::array<std::uint8_t, 7>{0x00, 0x03, 0x0f, 0x25, 0x2b, 0x4d, 0xff})
    {
        SCOPED_TRACE(int{skill});
        SkillWorld tested;
        tested.Striker().hands[0].model = 0x212 + 7;

        EXPECT_TRUE(tested.Head(skill).HasValue());
        EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
        EXPECT_EQ(tested.Striker().word8c, 0);
        EXPECT_TRUE(AliveEffects(tested.world).empty());
    }
    for (const std::uint8_t skill : std::array<std::uint8_t, 2>{0x37, 0x38})
    {
        SCOPED_TRACE(int{skill});
        SkillWorld tested(2);
        tested.Striker().hands[0].model = 0x212 + 0x20;
        tested.Striker().hands[0].level = 9;

        EXPECT_TRUE(tested.Head(skill).HasValue());
        EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
        EXPECT_EQ(tested.Striker().word8c, 0x20);
        EXPECT_EQ(tested.Striker().byte8e, 9);
        EXPECT_TRUE(tested.sounds.empty());
    }
    // With nothing in the right hand (0xffff), +0x8c is −1 − 0x212.
    SkillWorld unarmed;
    EXPECT_TRUE(unarmed.Head(0x37).HasValue());
    EXPECT_EQ(unarmed.Striker().word8c, -1 - 0x212);
}

// 0x2a: the weapon in +0x8c/+0x8e, 0x59fb80(0x2a) and the quake 0xfa with state 0, the size from o's +0x8a and the
// skill's slot in +0x89, whose drawing reads the weapon through its owner; then sound 0x95. The rand(): 0x59fb80, the
// subtype rand() % 100 and the close of the own record of the count +0xf04.
TEST(AttackSkillCasesTest, TheQuakeKeepsTheWeaponForItsOwner)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        SkillWorld tested(map);
        ActorState& striker = tested.Striker();
        striker.hands[0].model = 0x212 + 0x0c;
        striker.hands[0].level = 4;
        striker.word8a = 0x0123;
        striker.position = glm::vec3(1250.0f, 3350.0f, 120.0f);
        tested.world.OwnCharacter().SetSkillAt(6, 0x2a);
        tested.world.OwnCharacter().bytef04 = 0x41;

        ASSERT_TRUE(tested.Head(0x2a).HasValue());

        CrtRandom expected(Seed);
        expected.Discard(static_cast<std::uint32_t>(OwnRecord));
        const int subtype = expected.Next() % 100;
        expected.Discard(static_cast<std::uint32_t>(OwnRecord));
        EXPECT_EQ(tested.random.State(), expected.State());
        const auto alive = AliveEffects(tested.world);
        ASSERT_EQ(alive.size(), 1u);
        const Client::Effects::ModelEffect& quake = *alive.front();
        EXPECT_EQ(quake.type, 0xfa);
        EXPECT_EQ(quake.subtype, subtype);
        EXPECT_EQ(quake.state, 0);
        EXPECT_EQ(quake.disturbanceState, 0x0123);
        EXPECT_EQ(quake.byte89, 6);
        EXPECT_EQ(quake.byte1bd, 0);
        EXPECT_EQ(quake.ownerContext, &striker);
        ASSERT_NE(quake.itemType, nullptr);
        ASSERT_NE(quake.itemLevel, nullptr);
        EXPECT_EQ(*quake.itemType, 0x0c);
        EXPECT_EQ(*quake.itemLevel, 4);
        EXPECT_EQ(quake.transform.position, striker.position);
        EXPECT_EQ(tested.world.OwnCharacter().bytef04, 0x42);
        EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x95}));
    }
}

TEST(AttackSkillCasesTest, AFullTableSkipsTheQuakeButNotItsSound)
{
    SkillWorld tested;
    for (Client::Effects::ModelEffect& effect : tested.world.Map().effects.Effects())
    {
        effect.alive = true;
        effect.type = 0x01;
    }

    ASSERT_TRUE(tested.Head(0x2a).HasValue());

    EXPECT_EQ(DrawsSince(Seed, tested.random), OwnRecord);
    EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x95}));
}

// 0x44 and 0x47: the target of +0x3a4, without range; without a target or with o itself, nothing; outside the pool,
// MuRet cuts at the read. With another, the joint 0x632/0 (0x44, 0x4fe235) or /1 (0x47, 0x4fe4e7) 1200 above the
// target, with its angles, it as owner, width 120 and key −1 (creation 0x534bcb: life 40, 10 points, speed 70, +0x9f1
// at rand() & 1, +0x9ec at 150 and the angles at (90, 0, 0)), and sound 0x267; with the pool full, no joint and no
// rand(), but the sound.
TEST(AttackSkillCasesTest, TheBindingSkillsDropAStormOnTheirTarget)
{
    struct Case
    {
        std::uint8_t skill;
        std::string_view read;
        std::int32_t subtype;
    };
    for (const Case& probe : {Case{0x44, "0x4fe1a6", 0}, Case{0x47, "0x4fe458", 1}})
    {
        SCOPED_TRACE(int{probe.skill});
        SkillWorld tested;
        EXPECT_TRUE(tested.Head(probe.skill).HasValue());
        tested.Striker().attackTarget = static_cast<std::uint16_t>(StrikerSlot);
        EXPECT_TRUE(tested.Head(probe.skill).HasValue());
        tested.Striker().attackTarget = std::uint16_t{0x8000};
        EXPECT_TRUE(HasError(tested.Head(probe.skill), probe.read));
        tested.Striker().attackTarget = std::uint16_t{500};
        EXPECT_TRUE(HasError(tested.Head(probe.skill), probe.read));
        EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
        EXPECT_TRUE(tested.sounds.empty());

        tested.Striker().attackTarget = static_cast<std::uint16_t>(VictimSlot);
        tested.Victim().position = glm::vec3(1500.0f, 1600.0f, 20.0f);
        tested.Victim().angles = glm::vec3(0.0f, 0.0f, 135.0f);
        const auto struck = tested.Head(probe.skill);
        ASSERT_TRUE(struck.HasValue()) << ErrorOf(struck);
        const JointState& storm = tested.joints[0];
        ASSERT_TRUE(storm.alive);
        EXPECT_EQ(storm.type, 0x632);
        EXPECT_EQ(storm.subtype, probe.subtype);
        EXPECT_EQ(storm.ownerKind, Client::Kernel::JointOwnerKind::Actor);
        EXPECT_EQ(storm.ownerContext, &tested.Victim());
        EXPECT_EQ(storm.position, glm::vec3(1500.0f, 1600.0f, 1220.0f));
        EXPECT_EQ(storm.angles, glm::vec3(90.0f, 0.0f, 0.0f));
        EXPECT_EQ(storm.bendAngles, glm::vec3(90.0f, 0.0f, 0.0f));
        EXPECT_EQ(storm.width, 120.0f);
        EXPECT_EQ(storm.life, 40);
        EXPECT_EQ(storm.maximumCount, 10);
        EXPECT_EQ(storm.speed, 70.0f);
        EXPECT_EQ(storm.field9ec, 150);
        EXPECT_EQ(storm.key, 0);
        CrtRandom expected{Seed};
        EXPECT_EQ(storm.orientation, expected.Next() & 1);
        EXPECT_EQ(DrawsSince(Seed, tested.random), 1);
        EXPECT_EQ(tested.sounds, std::vector<std::int32_t>{0x267});

        for (JointState& joint : tested.joints)
            joint.alive = true;
        const CrtRandom before = tested.random;
        ASSERT_TRUE(tested.Head(probe.skill).HasValue());
        EXPECT_EQ(tested.random.State(), before.State());
        EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x267, 0x267}));
    }
}

// 0x46: with a target, 0x534f90(0x10f, target, 4) and (…, 9) kill its joints of those subtypes, the decal 0x61e/6 is
// born over it (life 60, scale (rand() % 50 + 50) · 0.04f, the start at its position and it as owner) and 0x269
// sounds; without a target, nothing; without the joint pool, it cuts before.
TEST(AttackSkillCasesTest, TheReleaseKillsTheFlamesOfTheTarget)
{
    SkillWorld tested;
    EXPECT_TRUE(tested.Head(0x46).HasValue());

    tested.Striker().attackTarget = static_cast<std::uint16_t>(VictimSlot);
    const std::array<std::int32_t, 4> subtypes{4, 9, 0, 4};
    for (std::size_t index = 0; index < subtypes.size(); ++index)
    {
        tested.joints[index].alive = true;
        tested.joints[index].type = 0x10f;
        tested.joints[index].subtype = subtypes[index];
        tested.joints[index].ownerContext = index == 3 ? &tested.Striker() : &tested.Victim();
    }

    tested.Victim().position = glm::vec3(1500.0f, 1600.0f, 20.0f);
    tested.Victim().light = glm::vec3(0.5f);
    ASSERT_TRUE(tested.Head(0x46).HasValue());

    const auto alive = AliveEffects(tested.world);
    ASSERT_EQ(alive.size(), 1u);
    const Client::Effects::ModelEffect& decal = *alive.front();
    CrtRandom expected{Seed};
    const int grown = expected.Next() % 50 + 50;
    EXPECT_EQ(decal.type, 0x61e);
    EXPECT_EQ(decal.subtype, 6);
    EXPECT_EQ(decal.life, 60);
    EXPECT_EQ(decal.transform.scale,
              static_cast<float>(static_cast<double>(grown) * static_cast<double>(std::bit_cast<float>(0x3d23d70au))));
    EXPECT_EQ(decal.start, tested.Victim().position);
    EXPECT_EQ(decal.light, tested.Victim().light);
    EXPECT_EQ(decal.ownerContext, &tested.Victim());
    EXPECT_EQ(DrawsSince(Seed, tested.random), 1);
    EXPECT_EQ(tested.sounds, std::vector<std::int32_t>{0x269});

    EXPECT_FALSE(tested.joints[0].alive);
    EXPECT_FALSE(tested.joints[1].alive);
    EXPECT_TRUE(tested.joints[2].alive);
    EXPECT_TRUE(tested.joints[3].alive);

    tested.withJoints = false;
    EXPECT_TRUE(HasError(tested.Head(0x46), "0x4fe3ce"));
}

// 0x50294e..0x50298e, 0x2c: the 0x13b/0 over o with o as owner and the step's light (life 15, z 100 higher) and sound
// 0x26c, no rand(); with the table full, only the sound.
TEST(AttackSkillCasesTest, TheSwordForceRisesOverTheStriker)
{
    for (const bool full : {false, true})
    {
        SCOPED_TRACE(full);
        SkillWorld tested;
        tested.Striker().position = glm::vec3(100.0f, 200.0f, 300.0f);
        if (full)
        {
            for (auto& effect : tested.world.Map().effects.Effects())
                effect.alive = true;
        }
        ASSERT_TRUE(tested.Head(0x2c).HasValue());
        EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
        EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x26c}));
        if (full)
            continue;
        const auto alive = AliveEffects(tested.world);
        ASSERT_EQ(alive.size(), 1u);
        EXPECT_EQ(alive[0]->type, 0x13b);
        EXPECT_EQ(alive[0]->life, 15);
        EXPECT_EQ(alive[0]->ownerContext, &tested.Striker());
        EXPECT_EQ(alive[0]->transform.position, glm::vec3(100.0f, 200.0f, 400.0f));
        EXPECT_EQ(alive[0]->light, glm::vec3(1.0f));
    }
}

// 0x40 and 0x4b: with anything but 0xa21 in the right hand, the 0x117/0 at the right hand's bone; with anything but
// 0xa19 or a shield (0xe12..0x1011) in the left, the 0x117/1 at its bone (one rand() each, at its creation), with o's
// angles and the light (1; 0.6; 0.3) for 0x40 or (1; 1; 1) for 0x4b. Then 0x40 sounds (0x1cf, from o) and 0x4b creates
// the 0x138/0 over o and sounds (0x272). With the table full, only the sounds and no rand().
TEST(AttackSkillCasesTest, TheHandsDecideTheRaise)
{
    struct Case
    {
        std::uint16_t right;
        std::uint16_t left;
        std::vector<std::int32_t> hands;
    };
    const std::array cases{
        Case{0xffff, 0xa19, {0}}, Case{0xa21, 0xffff, {1}}, Case{0xa21, 0xa20, {1}}, Case{0xa21, 0x1012, {1}},
        Case{0xa21, 0xa19, {}},   Case{0xa21, 0xe12, {}},   Case{0xa21, 0x1011, {}}, Case{0xffff, 0xffff, {0, 1}},
    };
    const glm::vec3 warm{1.0f, std::bit_cast<float>(0x3f19999au), std::bit_cast<float>(0x3e99999au)};
    for (const Case& probe : cases)
    {
        for (const std::uint8_t skill : {std::uint8_t{0x40}, std::uint8_t{0x4b}})
        {
            SCOPED_TRACE(testing::Message() << probe.right << " y " << probe.left << ", skill " << int{skill});
            SkillWorld tested(2);
            ActorState& striker = tested.Striker();
            striker.position = glm::vec3(125.0f, 250.0f, 375.0f);
            striker.angles = glm::vec3(0.0f, 0.0f, 60.0f);
            striker.light = glm::vec3(0.25f);
            striker.hands[0].model = probe.right;
            striker.hands[1].model = probe.left;

            const auto stepped = tested.Head(skill);

            ASSERT_TRUE(stepped.HasValue()) << ErrorOf(stepped);
            const auto alive = AliveEffects(tested.world);
            const std::size_t runes = skill == 0x4b ? 1 : 0;
            ASSERT_EQ(alive.size(), probe.hands.size() + runes);
            for (std::size_t made = 0; made < probe.hands.size(); ++made)
            {
                EXPECT_EQ(alive[made]->type, 0x117);
                EXPECT_EQ(alive[made]->subtype, probe.hands[made]);
                EXPECT_EQ(alive[made]->light, skill == 0x40 ? warm : glm::vec3(1.0f));
                EXPECT_EQ(alive[made]->life, 10);
                EXPECT_EQ(alive[made]->transform.angles.x, 45.0f);
            }
            if (runes != 0)
            {
                const auto* rune = alive.back();
                EXPECT_EQ(rune->type, 0x138);
                EXPECT_EQ(rune->subtype, 0);
                EXPECT_EQ(rune->life, 50);
                EXPECT_EQ(rune->light, glm::vec3(0.25f));
                EXPECT_EQ(rune->transform.position, glm::vec3(125.0f, 250.0f, 675.0f));
            }
            EXPECT_EQ(DrawsSince(Seed, tested.random), static_cast<int>(probe.hands.size()));
            EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{skill == 0x40 ? 0x1cf : 0x272}));
            EXPECT_EQ(tested.soundOrigins.size(), skill == 0x40 ? 1u : 0u);
        }
    }
    SkillWorld full(2);
    for (auto& effect : full.world.Map().effects.Effects())
        effect.alive = true;
    full.Striker().hands[0].model = 0xffff;
    ASSERT_TRUE(full.Head(0x4b).HasValue());
    EXPECT_EQ(DrawsSince(Seed, full.random), 0);
    EXPECT_EQ(full.sounds, (std::vector<std::int32_t>{0x272}));
}

// 0x4c: sound 0x27d and the loop over the 400: the far monster reads the range and stays out, the one in action 6 does
// not read it, and of the twelve near ones ten enter (the last exactly at the range) and the loop cuts there. Then
// rand() % 360; without a target in +0x434, nothing more; with it, two rounds of two rand() for it and for each of the
// ten (the 0x14f fill the 16 slots and the rest are not created) and six 0x62c of three rand() each, without room.
TEST(AttackSkillCasesTest, TheStrikeAroundGathersTenTargetsInRange)
{
    const auto skills = SkillsWithRange(3);
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        for (const std::int32_t target : {-1, 400, 2})
        {
            SCOPED_TRACE(target);
            SkillWorld tested(map);
            tested.skills = &skills;
            tested.Striker().position = glm::vec3(5000.0f, 5000.0f, 0.0f);
            tested.Striker().skillTarget4c = target;
            tested.Monster(0, 301.0f);
            tested.Monster(1, 10.0f).animation.action = 6;
            for (std::size_t slot = 10; slot < 22; ++slot)
                tested.Monster(slot, slot == 19 ? 300.0f : -50.0f);

            const auto stepped = tested.Head(0x4c);

            const int reads = 1 + 10;
            if (target == 2)
            {
                ASSERT_TRUE(stepped.HasValue()) << ErrorOf(stepped);
                EXPECT_EQ(DrawsSince(Seed, tested.random), reads * SkillEntry + 1 + 11 * 4 + 6 * 3);
                EXPECT_TRUE(std::ranges::all_of(tested.joints, [](const JointState& joint)
                                                { return joint.alive && joint.type == 0x14f; }));
            }
            else
            {
                EXPECT_TRUE(stepped.HasValue()) << ErrorOf(stepped);
                EXPECT_EQ(DrawsSince(Seed, tested.random), reads * SkillEntry + 1);
            }
            EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x27d}));
        }
    }
}

// 0x5025d7..0x502944: with the target of +0x434 (not gathered), two rounds from o's point (0, −140, 130), with the
// angles (rand() % 360, 0, rand() % 360), of two 0x14f toward its position with it as owner: width 100 with
// 0x4f2230's subtype and 80 with that plus 3. Then six ownerless 0x62c, width 60 and the subtype plus 0xb, from (0,
// rand() % 40 − 20 + 10, 130) toward the same point, with their angles and their volley's rand() (0x53477e; the path
// 150 higher). 0x4f2230 reads +0x31b (Fenrir's excellent): 1 gives 0, 2 gives 2 and any other 1.
TEST(AttackSkillCasesTest, TheStrikeAroundSendsPlasmaAndRaisesSpirals)
{
    for (const auto& [excellent, shade] :
         {std::pair{std::uint8_t{1}, 0}, std::pair{std::uint8_t{2}, 2}, std::pair{std::uint8_t{7}, 1}})
    {
        SCOPED_TRACE(shade);
        SkillWorld tested;
        tested.Striker().pet.flags1b = excellent;
        tested.Striker().angles = {0.0f, 0.0f, 90.0f};
        ActorState& aimed = tested.world.Actors()[30];
        aimed = ActorState{};
        aimed.position = {700.0f, 800.0f, 900.0f};
        tested.Striker().skillTarget4c = 30;
        ASSERT_TRUE(tested.Head(0x4c).HasValue());

        const auto point = [&](const glm::vec3& offset)
        {
            const auto turned =
                Client::Math::OriginalVectorRotate(offset, Client::Math::OriginalAngleMatrix(tested.Striker().angles));
            const glm::vec3& at = tested.Striker().position;
            const auto sum = [](float first, float second)
            { return static_cast<float>(static_cast<double>(first) + static_cast<double>(second)); };
            return glm::vec3{sum(at.x, turned.x), sum(at.y, turned.y), sum(at.z, turned.z)};
        };
        CrtRandom expected{Seed};
        static_cast<void>(expected.Next());
        for (std::size_t round = 0; round < 2; ++round)
        {
            const auto pitch = static_cast<float>(expected.Next() % 360);
            const auto yaw = static_cast<float>(expected.Next() % 360);
            for (std::size_t pair = 0; pair < 2; ++pair)
            {
                const JointState& plasma = tested.joints[round * 2 + pair];
                EXPECT_TRUE(plasma.alive);
                EXPECT_EQ(plasma.type, 0x14f);
                EXPECT_EQ(plasma.subtype, shade + (pair == 0 ? 0 : 3));
                EXPECT_EQ(plasma.ownerContext, &aimed);
                EXPECT_EQ(plasma.ownerKind, Client::Kernel::JointOwnerKind::Actor);
                EXPECT_EQ(plasma.width, pair == 0 ? 100.0f : 80.0f);
                EXPECT_EQ(plasma.position, point({0.0f, -140.0f, 130.0f}));
                EXPECT_EQ(plasma.angles, (glm::vec3{pitch, 0.0f, yaw}));
                // With an owner, 0x530942 skips copying the second point: its step heads to the owner (0x535b37).
                EXPECT_EQ(plasma.targetPosition, glm::vec3(0.0f));
            }
        }
        for (std::size_t spiral = 0; spiral < 6; ++spiral)
        {
            const auto height = static_cast<float>(static_cast<double>(expected.Next() % 40 - 20) + 10.0);
            static_cast<void>(expected.Next());
            static_cast<void>(expected.Next());
            static_cast<void>(expected.Next());
            const JointState& risen = tested.joints[4 + spiral];
            EXPECT_TRUE(risen.alive);
            EXPECT_EQ(risen.type, 0x62c);
            EXPECT_EQ(risen.subtype, shade + 0xb);
            EXPECT_EQ(risen.ownerContext, nullptr);
            EXPECT_EQ(risen.width, 60.0f);
            // Its creation (0x5346d8) raises the path by 150.
            const glm::vec3 from = point({0.0f, height, 130.0f});
            EXPECT_EQ(risen.pathIncrement,
                      (glm::vec3{from.x, from.y, static_cast<float>(static_cast<double>(from.z) + 150.0)}));
        }
        EXPECT_FALSE(tested.joints[10].alive);
        EXPECT_EQ(tested.random.State(), expected.State());
    }
}

// The players of 0x4c's loop go through 0x5588d0: PK 6 enters, PK 3 only with Ctrl, the one in action 0xc3 is not
// checked; in Chaos Castle (18) anyone but the hero enters. With +0x88 at anything else, none.
TEST(AttackSkillCasesTest, TheStrikeAroundAsksWhoIsAttackable)
{
    const auto skills = SkillsWithRange(5);
    struct Case
    {
        std::string_view name;
        std::int32_t map;
        std::uint8_t pk;
        bool control;
        std::uint8_t action;
        std::uint8_t object;
        int reads;
    };
    const std::array cases{
        Case{"PK 6", 0, 6, false, 1, 1, 1},          Case{"PK 3", 2, 3, false, 1, 1, 0},
        Case{"PK 3 con Ctrl", 2, 3, true, 1, 1, 1},  Case{"caído", 0, 6, false, 0xc3, 1, 0},
        Case{"Chaos Castle", 18, 3, false, 1, 1, 1}, Case{"un NPC", 0, 6, false, 1, 4, 0},
    };
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(probe.name);
        SkillWorld tested(probe.map);
        tested.skills = &skills;
        tested.controlDown = probe.control;
        tested.Victim().pk = probe.pk;
        tested.Victim().animation.action = probe.action;
        tested.Victim().byte88 = probe.object;
        // The striker, with PK 3, enters only with Ctrl or in Chaos Castle; the hero never.
        const int others = probe.control || probe.map == 18 ? 1 : 0;

        ASSERT_TRUE(tested.Head(0x4c).HasValue());

        EXPECT_EQ(DrawsSince(Seed, tested.random), (probe.reads + others) * SkillEntry + 1);
    }
}

// 0x4c without Skill.bmd: cuts at the first target it checks; with none, it does not read it. With +0x434 negative and
// not −1, the pool is read out of bounds: MuRet cuts. With the entry open, the range draws no rand().
TEST(AttackSkillCasesTest, TheStrikeAroundCutsWhereItReads)
{
    {
        SkillWorld tested;
        tested.Monster(0, 10.0f);
        EXPECT_TRUE(HasError(tested.Head(0x4c), "0x5526f0"));
        EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
    }
    {
        SkillWorld tested;
        EXPECT_TRUE(tested.Head(0x4c).HasValue());
        EXPECT_EQ(DrawsSince(Seed, tested.random), 1);
    }
    {
        const auto skills = SkillsWithRange(3);
        SkillWorld tested;
        tested.skills = &skills;
        tested.world.Runtime().skillDepths[0x4c] = 1;
        tested.Monster(0, 10.0f);
        tested.Striker().skillTarget4c = -5;
        EXPECT_TRUE(HasError(tested.Head(0x4c), "0x5025b8"));
        EXPECT_EQ(DrawsSince(Seed, tested.random), 1);
    }
}

// The target cases that create what MuRet does not have (CL11), with no rand() before; the 0x20a's 0x01 first creates
// its 0xc3 (no rand()).
TEST(AttackSkillCasesTest, TheTargetCasesCutWithoutDraws)
{
    struct Case
    {
        std::uint8_t skill;
        std::uint16_t type;
        std::string_view cut;
    };
    const std::array cases{
        Case{0x01, 0x20a, "0x50497e"},
        Case{0x01, 0x0e, "0x50497e"},
    };
    for (const std::int32_t map : Maps)
    {
        for (const Case& probe : cases)
        {
            SCOPED_TRACE(testing::Message() << "mapa " << map << ", skill " << int{probe.skill});
            SkillWorld tested(map);
            tested.Striker().type = probe.type;

            const auto stepped = tested.WithTarget(probe.skill);

            EXPECT_TRUE(HasError(stepped, probe.cut)) << ErrorOf(stepped);
            EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
        }
    }
}

// 0x504cdb..0x504d7a, 0x3c: two 0x114/1 and one 0x115/0 over o, and sound 0x1c8. With room in the joints, each 0x114/1
// releases its two swirls 0x6e8/0 (0x51c25f, ownerless), each with the rand() of its scale and of its light
// (0x534619): seven rand() per wave.
TEST(AttackSkillCasesTest, TheWaveCaseRaisesTwoSwirlJointsPerWaveWithRoom)
{
    SkillWorld tested;
    tested.Striker().position = glm::vec3(1000.0f, 2000.0f, 300.0f);
    const auto stepped = tested.WithTarget(0x3c);
    ASSERT_TRUE(stepped.HasValue()) << ErrorOf(stepped);
    EXPECT_EQ(DrawsSince(Seed, tested.random), 14);
    EXPECT_EQ(tested.sounds, std::vector<std::int32_t>{0x1c8});
    const auto alive = AliveEffects(tested.world);
    ASSERT_EQ(alive.size(), 3u);
    EXPECT_EQ(alive.front()->type, 0x114);
    CrtRandom replay(Seed);
    static_cast<void>(replay.Next());
    const auto& joints = tested.world.DrawTail().joints;
    for (std::size_t index = 0; index < 4; ++index)
    {
        SCOPED_TRACE(index);
        const float scale = static_cast<float>(replay.Next() % 40 + 70);
        static_cast<void>(replay.Next());
        if (index % 2 == 1)
        {
            static_cast<void>(replay.Next());
            static_cast<void>(replay.Next());
            static_cast<void>(replay.Next());
        }
        EXPECT_TRUE(joints[index].alive);
        EXPECT_EQ(joints[index].type, 0x6e8);
        EXPECT_EQ(joints[index].subtype, 0);
        EXPECT_EQ(joints[index].width, scale);
        EXPECT_EQ(joints[index].ownerContext, nullptr);
    }
    EXPECT_FALSE(joints[4].alive);
}

// With the joints full, each 0x114/1 draws five rand() (the scale, its two joints' and the shift in x and in y) and the
// 0x115/0 none: life 10, z 130 higher.
TEST(AttackSkillCasesTest, TheWaveCaseRaisesTwoSwirlsAndAPiercingWithFullJoints)
{
    SkillWorld tested;
    for (auto& joint : tested.world.DrawTail().joints)
        joint.alive = true;
    tested.Striker().position = glm::vec3(1000.0f, 2000.0f, 300.0f);
    tested.Striker().angles = glm::vec3(0.0f, 0.0f, 45.0f);
    ASSERT_TRUE(tested.WithTarget(0x3c).HasValue());
    EXPECT_EQ(DrawsSince(Seed, tested.random), 10);
    EXPECT_EQ(tested.sounds, std::vector<std::int32_t>{0x1c8});
    const auto alive = AliveEffects(tested.world);
    ASSERT_EQ(alive.size(), 3u);
    CrtRandom replay(Seed);
    for (std::size_t index = 0; index < 2; ++index)
    {
        SCOPED_TRACE(index);
        const auto& wave = *alive[index];
        const int size = replay.Next() % 50;
        static_cast<void>(replay.Next());
        static_cast<void>(replay.Next());
        const int dx = replay.Next() % 100;
        const int dy = replay.Next() % 100;
        EXPECT_EQ(wave.type, 0x114);
        EXPECT_EQ(wave.subtype, 1);
        EXPECT_EQ(wave.life, 15);
        EXPECT_EQ(wave.renderFlags, 0x2000u);
        EXPECT_EQ(wave.transform.scale, static_cast<float>(static_cast<double>(size) * static_cast<double>(0.01f) +
                                                           static_cast<double>(0.1f)));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(wave.turnDirection), 0x3c23d70au);
        EXPECT_EQ(wave.transform.angles, (glm::vec3{90.0f, 0.0f, 45.0f}));
        EXPECT_EQ(wave.transform.position,
                  (glm::vec3{static_cast<float>(static_cast<double>(dx) - 50.0 + 1000.0),
                             static_cast<float>(static_cast<double>(dy) - 50.0 + 2000.0), 430.0f}));
        EXPECT_EQ(wave.start, (glm::vec3{1000.0f, 2000.0f, 300.0f}));
        EXPECT_EQ(wave.objectState.blendMesh, -2);
    }
    const auto& piercing = *alive[2];
    EXPECT_EQ(piercing.type, 0x115);
    EXPECT_EQ(piercing.subtype, 0);
    EXPECT_EQ(piercing.life, 10);
    EXPECT_EQ(piercing.transform.scale, 2.0f);
    EXPECT_EQ(piercing.velocity, (glm::vec3{0.0f, -60.0f, 0.0f}));
    EXPECT_EQ(piercing.transform.position, (glm::vec3{1000.0f, 2000.0f, 430.0f}));
}

// 0x504a0a..0x504abf, 0x2d: three 0x139 over o, subtypes 0, 1 and 2, the target as owner (the start at its position),
// key −1 and one rand() each (the z of +0x28); then sound 0x26d.
TEST(AttackSkillCasesTest, TheJavelinCaseThrowsThreeJavelinsAtTheTarget)
{
    SkillWorld tested;
    tested.Striker().position = glm::vec3(100.0f, 200.0f, 300.0f);
    tested.Victim().position = glm::vec3(400.0f, 500.0f, 600.0f);
    ASSERT_TRUE(tested.WithTarget(0x2d).HasValue());
    const auto alive = AliveEffects(tested.world);
    ASSERT_EQ(alive.size(), 3u);
    for (std::size_t index = 0; index < alive.size(); ++index)
    {
        SCOPED_TRACE(index);
        EXPECT_EQ(alive[index]->type, 0x139);
        EXPECT_EQ(alive[index]->subtype, static_cast<std::int32_t>(index));
        EXPECT_EQ(alive[index]->ownerContext, &tested.Victim());
        EXPECT_EQ(alive[index]->start, tested.Victim().position);
        EXPECT_EQ(alive[index]->transform.position, glm::vec3(100.0f, 200.0f, 450.0f));
        EXPECT_EQ(alive[index]->disturbanceState, 0xffff);
        EXPECT_EQ(alive[index]->life, 35);
    }
    EXPECT_EQ(DrawsSince(Seed, tested.random), 3);
    EXPECT_EQ(tested.sounds, std::vector<std::int32_t>{0x26d});
}

// 0x504b51..0x504b99, 0x4a: the 0x116/2 over o with the target as owner (life 20, z 20 lower) and sound 0x271, no
// rand(); with the table full, only the sound.
TEST(AttackSkillCasesTest, TheDivingPartFollowsTheTarget)
{
    SkillWorld tested;
    tested.Striker().position = glm::vec3(100.0f, 200.0f, 300.0f);
    tested.Striker().angles = glm::vec3(0.0f, 0.0f, 30.0f);
    ASSERT_TRUE(tested.WithTarget(0x4a).HasValue());
    const auto alive = AliveEffects(tested.world);
    ASSERT_EQ(alive.size(), 1u);
    EXPECT_EQ(alive[0]->type, 0x116);
    EXPECT_EQ(alive[0]->subtype, 2);
    EXPECT_EQ(alive[0]->ownerContext, &tested.Victim());
    EXPECT_EQ(alive[0]->transform.position, glm::vec3(100.0f, 200.0f, 280.0f));
    EXPECT_EQ(alive[0]->transform.angles, glm::vec3(0.0f, 0.0f, 30.0f));
    EXPECT_EQ(alive[0]->life, 20);
    EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
    EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x271}));

    SkillWorld full;
    for (auto& effect : full.world.Map().effects.Effects())
        effect.alive = true;
    ASSERT_TRUE(full.WithTarget(0x4a).HasValue());
    EXPECT_EQ(full.sounds, (std::vector<std::int32_t>{0x271}));
}

// 0x504d87..0x504f51, 0x3d: three 0x116/0 from o's first bone with the target as owner, headings z + 90, z and z − 90
// and the start (0, 0, top) and (0, 0, top / 2); then the 0x117/0 and the /1 (one rand() each). No sound.
TEST(AttackSkillCasesTest, TheLaunchedPartsAimAtTheTargetsTop)
{
    SkillWorld tested;
    tested.Striker().angles = glm::vec3(0.0f, 0.0f, 30.0f);
    tested.Victim().boundsTop = 150.0f;
    ASSERT_TRUE(tested.WithTarget(0x3d).HasValue());
    const auto alive = AliveEffects(tested.world);
    ASSERT_EQ(alive.size(), 5u);
    const std::array<float, 3> yaws{120.0f, 30.0f, -60.0f};
    const std::array<float, 3> aims{150.0f, 75.0f, 75.0f};
    for (std::size_t part = 0; part < 3; ++part)
    {
        SCOPED_TRACE(part);
        EXPECT_EQ(alive[part]->type, 0x116);
        EXPECT_EQ(alive[part]->subtype, 0);
        EXPECT_EQ(alive[part]->ownerContext, &tested.Victim());
        EXPECT_EQ(alive[part]->transform.angles, glm::vec3(0.0f, 0.0f, yaws[part]));
        EXPECT_EQ(alive[part]->start, glm::vec3(0.0f, 0.0f, aims[part]));
        EXPECT_EQ(alive[part]->light, glm::vec3(1.0f));
        EXPECT_EQ(alive[part]->transform.position, alive[0]->transform.position);
    }
    EXPECT_EQ(alive[3]->type, 0x117);
    EXPECT_EQ(alive[3]->subtype, 0);
    EXPECT_EQ(alive[4]->type, 0x117);
    EXPECT_EQ(alive[4]->subtype, 1);
    EXPECT_EQ(alive[4]->transform.position, alive[0]->transform.position);
    EXPECT_EQ(DrawsSince(Seed, tested.random), 2);
    EXPECT_TRUE(tested.sounds.empty());
}

// 0x504ac7..0x504b49, 0x49: an ownerless 0x61c/4 from o 130 higher, angles (0, 0, the z of o) and width 40
// (InfernoJoints.cpp: Effect\Hole.jpg, speed 8, vx 15.5, vz 3, 13 points, life 20), without rand(), and sound 0x26f.
TEST(AttackSkillCasesTest, TheInfernoRisesFromTheStriker)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(testing::Message() << "mapa " << map);
        SkillWorld tested(map);
        tested.Striker().position = {1500.0f, 1600.0f, 40.0f};
        tested.Striker().angles = {10.0f, 20.0f, 135.0f};

        ASSERT_TRUE(tested.WithTarget(0x49).HasValue());

        EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
        EXPECT_EQ(tested.sounds, std::vector<std::int32_t>{0x26f});
        const JointState& inferno = tested.joints[0];
        ASSERT_TRUE(inferno.alive);
        EXPECT_EQ(inferno.type, 0x61c);
        EXPECT_EQ(inferno.subtype, 4);
        EXPECT_EQ(inferno.ownerContext, nullptr);
        EXPECT_EQ(inferno.position, glm::vec3(1500.0f, 1600.0f, 170.0f));
        EXPECT_EQ(inferno.pathIncrement, inferno.position);
        EXPECT_EQ(inferno.angles, glm::vec3(0.0f, 0.0f, 135.0f));
        EXPECT_EQ(inferno.bendAngles, glm::vec3(0.0f, 0.0f, 135.0f));
        EXPECT_EQ(inferno.width, 40.0f);
        EXPECT_EQ(inferno.texture, 0x6ae);
        EXPECT_EQ(inferno.speed, 8.0f);
        EXPECT_EQ(inferno.velocity, glm::vec3(15.5f, 0.0f, 3.0f));
        EXPECT_EQ(inferno.life, 20);
        EXPECT_EQ(inferno.maximumCount, 13);
        EXPECT_EQ(inferno.count, -1);
        EXPECT_EQ(inferno.field9ec, 10);
        EXPECT_FALSE(tested.joints[1].alive);

        SkillWorld full(map);
        for (auto& joint : full.joints)
            joint.alive = true;
        ASSERT_TRUE(full.WithTarget(0x49).HasValue());
        EXPECT_EQ(DrawsSince(Seed, full.random), 0);
        EXPECT_EQ(full.sounds, std::vector<std::int32_t>{0x26f});
        EXPECT_TRUE(std::ranges::none_of(full.joints, [](const JointState& joint) { return joint.type == 0x61c; }));
    }
}

// 0x5046bf..0x50473b, 0x1c: without 0x4000 on o, the decal 0x61e/3 over the target, with it as owner, and with o's
// +0x38d the 4 in the target's +0x78; no rand() or sound. With the table full, no decal and the 4 still.
TEST(AttackSkillCasesTest, TheSummonTargetLeavesItsDecalAndItsMark)
{
    for (const std::int32_t map : Maps)
    {
        for (const std::uint8_t succeeded : {std::uint8_t{0}, std::uint8_t{1}})
        {
            SCOPED_TRACE(testing::Message() << "mapa " << map << ", +0x38d " << int{succeeded});
            SkillWorld tested(map);
            tested.Striker().skillSucceeded = succeeded;
            ActorState& victim = tested.Victim();
            victim.position = glm::vec3(2250.0f, 1350.0f, 80.0f);
            victim.flags78 = 0x10;

            ASSERT_TRUE(tested.WithTarget(0x1c).HasValue());

            const auto alive = AliveEffects(tested.world);
            ASSERT_EQ(alive.size(), 1u);
            const Client::Effects::ModelEffect& decal = *alive.front();
            EXPECT_EQ(decal.type, Client::Effects::ColoredDecalEffect);
            EXPECT_EQ(decal.subtype, Client::Effects::SummonDecalSubtype);
            EXPECT_EQ(decal.ownerContext, &victim);
            EXPECT_EQ(decal.transform.position, victim.position);
            EXPECT_EQ(victim.flags78, succeeded != 0 ? 0x14u : 0x10u);
            EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
            EXPECT_TRUE(tested.sounds.empty());
        }
    }
    SkillWorld full;
    full.Striker().skillSucceeded = 1;
    for (Client::Effects::ModelEffect& effect : full.world.Map().effects.Effects())
    {
        effect.alive = true;
        effect.type = 0x01;
    }
    ASSERT_TRUE(full.WithTarget(0x1c).HasValue());
    EXPECT_EQ(full.Victim().flags78, 4u);
}

// 0x1b: without +0x38d or with 0x4000 in +0x78, nothing; otherwise the decal 0x61e/2. 0x1c: with 0x4000, nothing.
// 0x11: the +0x376 in the tables of 0x50561a and 0x505672, nothing. 3 and 0, nothing.
TEST(AttackSkillCasesTest, TheQuietTargetCasesDoNothing)
{
    SkillWorld tested;
    ActorState& striker = tested.Striker();
    EXPECT_TRUE(tested.WithTarget(0x1b).HasValue());
    striker.skillSucceeded = 1;
    // The decal 0x61e/2 is born over the target, no rand(); then the five joints 0x10f/4 of 0x5045f9 (in
    // BindingFlagsPrecedeTheJointAndExistingBindingsSkipIt).
    EXPECT_TRUE(tested.WithTarget(0x1b).HasValue());
    const auto bound = AliveEffects(tested.world);
    ASSERT_EQ(bound.size(), 1u);
    EXPECT_EQ(bound.front()->type, 0x61e);
    EXPECT_EQ(bound.front()->subtype, 2);
    EXPECT_EQ(bound.front()->life, 20);
    EXPECT_EQ(bound.front()->ownerContext, &tested.Victim());
    EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
    striker.flags78 = 0x4000;
    EXPECT_TRUE(tested.WithTarget(0x1b).HasValue());
    EXPECT_TRUE(tested.WithTarget(0x1c).HasValue());
    striker.flags78 = 0;
    const std::array<std::uint16_t, 31> quietKinds{0x25, 0x2e, 0x3d, 0x42, 0x45, 0x46, 0x49, 0x4b,  0x4d, 0x57, 0x59,
                                                   0x5d, 0x5f, 0x63, 0x70, 0x74, 0x76, 0x7a, 0x7c,  0x80, 0x82, 0x8d,
                                                   0x8f, 0xa3, 0xa5, 0xa7, 0xa9, 0xab, 0xad, 0x125, 0x12f};
    for (const std::uint16_t kind : quietKinds)
    {
        SCOPED_TRACE(kind);
        striker.word376 = kind;
        EXPECT_TRUE(tested.WithTarget(0x11).HasValue());
    }
    EXPECT_TRUE(tested.WithTarget(0x03).HasValue());
    EXPECT_TRUE(tested.WithTarget(0x00).HasValue());
    EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
    EXPECT_TRUE(tested.sounds.empty());
    EXPECT_EQ(AliveEffects(tested.world).size(), 1u); // 0x1b's 0x61e/2
    // Another +0x376 (0x26) is not in the tables: the ball 0x5ca and sound 0x76 (TheEnergyBallFliesAtTheTarget).
    striker.word376 = 0x26;
    EXPECT_TRUE(tested.WithTarget(0x11).HasValue());
    EXPECT_EQ(tested.sounds, std::vector<std::int32_t>{0x76});
}

// 0x10: the 0x189 sets 0x100 in its +0x78; with 0x4000, nothing; otherwise 0x100 in the target's +0x78, sound 0x84,
// 0x534f90(0x10f, target, 0) and the first of the five joints cuts. Without the joint pool, it cuts after the sound.
TEST(AttackSkillCasesTest, TheGuardMarksTheTarget)
{
    {
        SkillWorld tested;
        tested.Striker().type = 0x189;
        EXPECT_TRUE(tested.WithTarget(0x10).HasValue());
        EXPECT_EQ(tested.Striker().flags78, 0x100u);
        EXPECT_EQ(tested.Victim().flags78, 0u);
    }
    {
        SkillWorld tested;
        tested.Striker().flags78 = 0x4000;
        EXPECT_TRUE(tested.WithTarget(0x10).HasValue());
        EXPECT_EQ(tested.Victim().flags78, 0u);
        EXPECT_TRUE(tested.sounds.empty());
    }
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        SkillWorld tested(map);
        for (std::int32_t subtype = 0; subtype < 2; ++subtype)
        {
            JointState& joint = tested.joints[static_cast<std::size_t>(subtype)];
            joint.alive = true;
            joint.type = 0x10f;
            joint.subtype = subtype;
            joint.ownerContext = &tested.Victim();
        }

        // 0x5048db: kills the target's 0x10f/0 and creates five of width 20, the first in its slot.
        ASSERT_TRUE(tested.WithTarget(0x10).HasValue());

        EXPECT_EQ(tested.Victim().flags78, 0x100u);
        EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x84}));
        EXPECT_EQ(tested.joints[1].subtype, 1);
        EXPECT_EQ(std::ranges::count_if(tested.joints,
                                        [&tested](const JointState& joint)
                                        {
                                            return joint.alive && joint.type == 0x10f && joint.subtype == 0 &&
                                                   joint.ownerContext == &tested.Victim() && joint.width == 20.0f;
                                        }),
                  5);
        EXPECT_EQ(tested.joints[0].subtype, 0);
        EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
    }
    SkillWorld unjointed;
    unjointed.withJoints = false;
    EXPECT_TRUE(HasError(unjointed.WithTarget(0x10), "0x504876"));
    EXPECT_EQ(unjointed.sounds, (std::vector<std::int32_t>{0x84}));
}

// 0x1a: the decal 0x61e/1 over the target, with it as owner, no rand(); with the table full, nothing.
TEST(AttackSkillCasesTest, TheSummonGoesOverTheTarget)
{
    for (const std::int32_t map : Maps)
    {
        SCOPED_TRACE(map);
        SkillWorld tested(map);
        tested.Victim().position = glm::vec3(700.0f, 900.0f, 40.0f);

        ASSERT_TRUE(tested.WithTarget(0x1a).HasValue());

        const auto alive = AliveEffects(tested.world);
        ASSERT_EQ(alive.size(), 1u);
        EXPECT_EQ(alive.front()->type, 0x61e);
        EXPECT_EQ(alive.front()->subtype, 1);
        EXPECT_EQ(alive.front()->life, 20);
        EXPECT_EQ(alive.front()->ownerContext, &tested.Victim());
        EXPECT_EQ(alive.front()->transform.position, tested.Victim().position);
        EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
    }
}

// 0x33 and 0x34: two reads of +0x38e (0x53e2b0's skill and 0x59fb80's) and one shot; 0x18: first, the targetless volley
// with its own read and 0x59fb80. The rand(), arrows and sounds are the reads' and those of 0x53e2b0, which runs the
// same in a twin world. 0x34's arrow leaves the trail 0x105 with its four joints 0x610 (0x51a08a), without a cut.
TEST(AttackSkillCasesTest, TheArrowCasesReadTheSkillBeforeShooting)
{
    struct Case
    {
        std::uint8_t skill;
        bool volley;
        std::size_t arrows;
        std::string_view cut;
    };
    for (const Case& probe : {Case{0x33, false, 1, ""}, Case{0x34, false, 1, ""}, Case{0x18, true, 4, ""}})
    {
        SCOPED_TRACE(int{probe.skill});
        SkillWorld tested;
        SkillWorld twin;
        for (SkillWorld* each : {&tested, &twin})
        {
            each->Striker().hands[1].model = 0xa12;
            each->Striker().byte38e = probe.skill;
            each->world.OwnCharacter().SetSkillAt(2, probe.skill);
        }

        const auto stepped = tested.WithTarget(probe.skill);

        if (probe.volley)
        {
            twin.random.Discard(static_cast<std::uint32_t>(SkillRead + OwnRecord));
            ASSERT_TRUE(Client::Effects::FireTheArrow(twin.Striker(), nullptr, StrikerSlot, 2, 1, 0, twin.Context())
                            .HasValue());
        }
        twin.random.Discard(static_cast<std::uint32_t>(2 * SkillRead + OwnRecord));
        const auto fired =
            Client::Effects::FireTheArrow(twin.Striker(), nullptr, StrikerSlot, 2, 0, probe.skill, twin.Context());
        if (probe.cut.empty())
        {
            EXPECT_TRUE(stepped.HasValue()) << ErrorOf(stepped);
        }
        else
        {
            EXPECT_TRUE(HasError(stepped, probe.cut)) << ErrorOf(stepped);
        }
        EXPECT_EQ(ErrorOf(stepped), ErrorOf(fired));
        EXPECT_EQ(tested.random.State(), twin.random.State());
        EXPECT_EQ(tested.sounds, twin.sounds);
        const auto arrows = AliveEffects(tested.world);
        EXPECT_EQ(arrows.size(), AliveEffects(twin.world).size());
        EXPECT_GE(arrows.size(), probe.arrows);
        for (const Client::Effects::ModelEffect* arrow : arrows)
        {
            if (Client::Effects::IsArrowEffect(arrow->type))
            {
                EXPECT_EQ(arrow->byte89, 2);
            }
        }
    }
}

// 0x2e: the same with the target; 0x53e2b0 creates the 0x13a with its joint 0x632/4 (0x51e01a, the effect as owner)
// and its count, adds the volley and then 0x26e sounds (0x5044ad). The rand() and the rest are 0x53e2b0's with the
// target in a twin world.
TEST(AttackSkillCasesTest, TheBlastLeavesItsJointBeforeItsSound)
{
    SkillWorld tested;
    SkillWorld twin;
    for (SkillWorld* each : {&tested, &twin})
    {
        each->Striker().byte38e = 0x2e;
        each->world.OwnCharacter().SetSkillAt(2, 0x2e);
    }

    const auto stepped = tested.WithTarget(0x2e);

    ASSERT_TRUE(stepped.HasValue()) << ErrorOf(stepped);
    twin.random.Discard(static_cast<std::uint32_t>(2 * SkillRead + OwnRecord));
    ASSERT_TRUE(Client::Effects::FireTheArrow(twin.Striker(), &twin.Victim(), StrikerSlot, 2, 0, 0x2e, twin.Context())
                    .HasValue());
    EXPECT_EQ(tested.random.State(), twin.random.State());
    EXPECT_EQ(tested.sounds, std::vector<std::int32_t>{0x26e});
    EXPECT_EQ(tested.world.OwnCharacter().bytef04, twin.world.OwnCharacter().bytef04);
    const auto alive = AliveEffects(tested.world);
    ASSERT_EQ(alive.size(), 1u);
    const Client::Effects::ModelEffect& blast = *alive.front();
    EXPECT_EQ(blast.type, 0x13a);
    EXPECT_EQ(blast.ownerContext, &tested.Victim());
    const auto& joints = tested.world.DrawTail().joints;
    ASSERT_TRUE(joints[0].alive);
    EXPECT_EQ(joints[0].type, 0x632);
    EXPECT_EQ(joints[0].subtype, 4);
    EXPECT_EQ(joints[0].ownerContext, &blast);
    EXPECT_EQ(joints[0].width, 40.0f);
    EXPECT_EQ(joints[0].key, 0x32);
    EXPECT_EQ(joints[0].position, blast.transform.position);
    EXPECT_FALSE(joints[1].alive);
}

// 0x5042ce: 0x52e740 with actions 0x38 to 0x3c: the spark 0x5c6/0 on the target, no rand(), and twenty times two rand()
// and the 0x5c5/0. With the pool full, only the forty rand().
TEST(AttackSkillCasesTest, TheSparkLeavesItsHitAndTwentyFairySparks)
{
    SkillWorld tested;
    tested.withParticles = true;
    tested.Striker().animation.action = 0x3a;

    const auto sparked = SparkTheTarget(tested.Striker(), tested.Victim(), StrikerSlot, tested.Context());

    ASSERT_TRUE(sparked.HasValue()) << ErrorOf(sparked);
    const auto count = [&](std::int32_t type)
    {
        return std::ranges::count_if(tested.world.Particles().All(), [type](const auto& particle)
                                     { return particle.alive && particle.type == type && particle.subtype == 0; });
    };
    EXPECT_EQ(count(0x5c6), 1);
    EXPECT_EQ(count(0x5c5), 20);
    EXPECT_EQ(tested.world.Particles().All()[0].position, tested.Victim().position);
    // The 0x5c6/0 draws none; each round, two and the five of the 0x5c5/0's creation (0x540333, 0x54041f, 0x540436,
    // 0x54045c and 0x540480).
    EXPECT_EQ(DrawsSince(Seed, tested.random), 20 * (2 + 5));

    SkillWorld full;
    full.withParticles = true;
    for (auto& particle : full.world.Particles().All())
        particle.alive = true;
    ASSERT_TRUE(SparkTheTarget(full.Striker(), full.Victim(), StrikerSlot, full.Context()).HasValue());
    EXPECT_EQ(DrawsSince(Seed, full.random), 40);
}
// 0x501494→0x51fff0: the burst and the stones come before the effect 0xf6/0 over o, with o as owner (0x5014da, CL11).
TEST(AttackSkillCasesTest, Skill0eRunsTheBurstBeforeTheNextCapability)
{
    SkillWorld tested;
    tested.withParticles = true;
    const auto used = tested.Head(0x0e);
    ASSERT_TRUE(used.HasValue()) << used.Error();
    const int plainDraws = DrawsSince(Seed, tested.random);
    EXPECT_TRUE(std::ranges::any_of(tested.world.Particles().All(), [](const auto& p) { return p.alive; }));
    EXPECT_EQ(
        std::ranges::count_if(tested.world.Map().effects.Effects(), [](const auto& effect) { return effect.alive; }),
        17);
    const auto inferno = std::ranges::find_if(tested.world.Map().effects.Effects(),
                                              [](const auto& effect) { return effect.alive && effect.type == 0xf6; });
    ASSERT_NE(inferno, tested.world.Map().effects.Effects().end());
    EXPECT_EQ(inferno->subtype, 0);
    EXPECT_EQ(inferno->ownerContext, &tested.Striker());
    EXPECT_EQ(inferno->transform.position, tested.Striker().position);
    for (auto& effect : tested.world.Map().effects.Effects())
        effect.alive = true;
    for (auto& particle : tested.world.Particles().All())
        particle.alive = true;
    const auto before = tested.random.State();
    const auto full = tested.Head(0x0e);
    ASSERT_TRUE(full.HasValue()) << full.Error();
    EXPECT_EQ(DrawsSince(before, tested.random), 8 * (20 * 2 + 2) + OwnRecord);

    // With o as hero and the table free, the 0xf6/0 copies the count into its own record (0x519a66) and asks 0x517a20
    // for the targets, which reads the skill in its own; then the hero's volley (0x5014e5) closes the third: three more
    // closes than without the hero, with the same burst.
    SkillWorld hero;
    hero.withParticles = true;
    hero.world.Runtime().hero = StrikerSlot;
    hero.world.Runtime().heroSlot = static_cast<std::int32_t>(StrikerSlot);
    hero.world.OwnCharacter().bytef04 = 0x41;
    const auto heroUsed = hero.Head(0x0e);
    ASSERT_TRUE(heroUsed.HasValue()) << heroUsed.Error();
    EXPECT_EQ(DrawsSince(Seed, hero.random), plainDraws + 3 * OwnRecord);
    const auto heroInferno = std::ranges::find_if(hero.world.Map().effects.Effects(), [](const auto& effect)
                                                  { return effect.alive && effect.type == 0xf6; });
    ASSERT_NE(heroInferno, hero.world.Map().effects.Effects().end());
    EXPECT_EQ(heroInferno->word8c, 0x41);
    EXPECT_EQ(hero.world.OwnCharacter().bytef04, 0x42);
}

// 0x4fec30..0x4fecb0 (0x29), 0x4ffcbe..0x4ffdb7 (0x27) and 0x500cd4..0x500dba (0x0d), CL11's switch batch: the 0xf3
// over o (with its own record: the count +0xf04 one more) and sound 0x73; ten ownerless 0x108/0 on the skill's cell,
// each with its rand() % 50 before its creation, the round's key and sound 0x90; and two 0xf5 on the cell with o as
// owner, which cut at the joint 0x618/5 (0x5193d6, joints batch) with room in the table and go on without it. Without
// the ground heights, 0x27 and 0x0d cut before their rand().
TEST(AttackSkillCasesTest, TheSwitchHeadCasesCreateTheirEffects)
{
    const auto alive = [](SkillWorld& tested, std::int16_t type)
    {
        std::vector<const Client::Effects::ModelEffect*> found;
        for (const Client::Effects::ModelEffect& effect : tested.world.Map().effects.Effects())
        {
            if (effect.alive && effect.type == type)
                found.push_back(&effect);
        }
        return found;
    };
    {
        SkillWorld tested;
        ActorState& striker = tested.Striker();
        striker.word8a = 0x0123;
        striker.hands[0].model = 0x212 + 5;
        tested.world.OwnCharacter().SetSkillAt(3, 0x29);
        tested.world.OwnCharacter().bytef04 = 0x41;
        ASSERT_TRUE(tested.Head(0x29).HasValue());
        EXPECT_EQ(DrawsSince(Seed, tested.random), 2 * OwnRecord);
        EXPECT_EQ(tested.sounds, std::vector<std::int32_t>{0x73});
        EXPECT_EQ(tested.world.OwnCharacter().bytef04, 0x42);
        const auto trails = alive(tested, 0xf3);
        ASSERT_EQ(trails.size(), 1u);
        EXPECT_EQ(trails[0]->ownerContext, &striker);
        EXPECT_EQ(trails[0]->transform.position, striker.position);
        EXPECT_EQ(trails[0]->disturbanceState, 0x0123);
        EXPECT_EQ(trails[0]->byte89, 3);
        EXPECT_EQ(trails[0]->itemType, &striker.word8c);
        EXPECT_EQ(striker.word8c, 5);
    }
    {
        SkillWorld tested;
        Flatten(tested.terrain);
        ActorState& striker = tested.Striker();
        striker.skillCellX = 120;
        striker.skillCellY = 33;
        ASSERT_TRUE(tested.Head(0x27).HasValue());
        constexpr int StormAltaDraws = 5;
        EXPECT_EQ(DrawsSince(Seed, tested.random), 10 * (1 + StormAltaDraws));
        EXPECT_EQ(tested.sounds, std::vector<std::int32_t>{0x90});
        const auto storm = alive(tested, 0x108);
        ASSERT_EQ(storm.size(), 10u);
        for (std::size_t round = 0; round < storm.size(); ++round)
        {
            EXPECT_EQ(storm[round]->subtype, 0);
            EXPECT_EQ(storm[round]->ownerContext, nullptr);
            EXPECT_EQ(storm[round]->disturbanceState, static_cast<std::uint16_t>(round + 1));
        }
    }
    for (const bool jointsFull : {false, true})
    {
        SCOPED_TRACE(jointsFull);
        SkillWorld tested;
        Flatten(tested.terrain);
        ActorState& striker = tested.Striker();
        striker.skillCellX = 120;
        striker.skillCellY = 33;
        tested.world.OwnCharacter().SetSkillAt(2, 0x0d);
        for (JointState& joint : tested.joints)
            joint.alive = jointsFull;
        const auto stepped = tested.Head(0x0d);
        constexpr int BlastDraws = 5;
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(DrawsSince(Seed, tested.random), OwnRecord + 2 * (BlastDraws + OwnRecord));
        // 0x5193d6: with room, each 0xf5 hangs its joint 0x618/5 of width 100, no rand() (joints batch); with the pool
        // full, none.
        const auto beams = std::ranges::count_if(tested.joints, [](const JointState& joint)
                                                 { return joint.alive && joint.type == 0x618 && joint.subtype == 5; });
        EXPECT_EQ(beams, jointsFull ? 0 : 2);
        const auto blasts = alive(tested, 0xf5);
        ASSERT_EQ(blasts.size(), 2u);
        for (const Client::Effects::ModelEffect* blast : blasts)
        {
            EXPECT_EQ(blast->ownerContext, &striker);
            EXPECT_EQ(blast->byte89, 2);
            // 0x51931b..0x51936a: 300 to 799 above the cell (12050; 3350) at height 150.
            EXPECT_GE(blast->transform.position.x, 12050.0f + 200.0f);
            EXPECT_GE(blast->transform.position.z, 150.0f + 300.0f);
        }
    }
    for (const std::uint8_t skill : {std::uint8_t{0x27}, std::uint8_t{0x0d}})
    {
        SkillWorld tested;
        const auto cut = tested.Head(skill);
        EXPECT_TRUE(HasError(cut, skill == 0x27 ? "0x4ffd1e" : "0x500d28")) << ErrorOf(cut);
        EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
    }
}

TEST(AttackSkillCasesTest, TheFullPoolsPreserveEveryHeadTail)
{
    struct Case
    {
        std::uint8_t skill;
        int draws, sound;
        std::uint8_t volley;
    };
    const std::vector<Case> cases{{0x30, 0, 127, 0},    {0x43, 0, 614, 0},    {0x44, 0, 615, 0},    {0x45, 0, 616, 0},
                                  {0x46, 0, 617, 0},    {0x47, 0, 615, 0},    {0x48, 0, 619, 0},    {0x35, 0, 127, 0},
                                  {0x40, 0, 463, 0},    {0x4b, 0, 626, 0},    {0x3f, 0, 459, 0},    {0x41, 0, 461, 0},
                                  {0x31, 0, 114, 0},    {0x29, 3848, 115, 0}, {0xa, 3848, 119, 0},  {0x28, 0, 147, 0},
                                  {0x26, 3848, 142, 1}, {0x27, 3858, 144, 1}, {0x8, 3848, 116, 0},  {0xc, 3848, 122, 0},
                                  {0xd, 7696, -1, 1},   {0xe, 8032, -1, 1},   {0x9, 19240, 117, 1}, {0x2c, 0, 620, 0}};
    for (const auto& probe : cases)
    {
        SCOPED_TRACE(int{probe.skill});
        SkillWorld tested;
        tested.withParticles = true;
        // 0x27 and 0x0d read the height of the skill's cell (0x5f93c0) even without room in the table.
        Flatten(tested.terrain);
        auto& actor = tested.Hero();
        actor.position = glm::vec3(125.0f, 250.0f, 375.0f);
        actor.skillTarget4c = static_cast<std::int32_t>(VictimSlot);
        actor.attackTarget = static_cast<std::uint16_t>(VictimSlot);
        actor.flags78 = probe.skill == 0x48 ? 0x8000u : 0u;
        if (probe.skill == 0x26)
            Flatten(tested.terrain);
        for (auto& effect : tested.world.Map().effects.Effects())
            effect.alive = true;
        for (auto& joint : tested.joints)
            joint.alive = true;
        for (auto& particle : tested.world.Particles().All())
            particle.alive = true;
        const auto used = StepTheHeadSkill(actor, HeroSlot, probe.skill, tested.Context());
        ASSERT_TRUE(used.HasValue()) << used.Error();
        EXPECT_EQ(DrawsSince(Seed, tested.random), probe.draws);
        EXPECT_EQ(tested.world.OwnCharacter().bytef04, probe.volley);
        EXPECT_EQ(tested.sounds,
                  probe.sound < 0 ? std::vector<std::int32_t>{} : std::vector<std::int32_t>{probe.sound});
        if (probe.skill == 0x40 || probe.skill == 0x3f || probe.skill == 0x41)
        {
            ASSERT_EQ(tested.soundOrigins.size(), 1u);
            EXPECT_EQ(tested.soundOrigins.front(), actor.position);
        }
        if (probe.skill == 0x48)
        {
            EXPECT_EQ(actor.flags78, 0u);
        }
    }
}

TEST(AttackSkillCasesTest, TheFullPoolsPreserveEveryTargetTail)
{
    struct Case
    {
        std::uint8_t skill;
        std::uint32_t flags;
        int sound;
    };
    const std::vector<Case> cases{{1, 1, 0x22},     {7, 2, 0x78},     {0x1b, 8, -1},    {0x10, 0x100, 0x84},
                                  {2, 0, 0x42},     {4, 0, 0x42},     {0xb, 0, 0x76},   {0x2d, 0, 0x26d},
                                  {0x49, 0, 0x26f}, {0x4a, 0, 0x271}, {0x3c, 0, 0x1c8}, {0x3d, 0, -1},
                                  {0x11, 0, 0x76},  {0x1c, 4, -1}};
    for (const auto& probe : cases)
    {
        SCOPED_TRACE(int{probe.skill});
        SkillWorld tested;
        tested.withParticles = true;
        tested.Striker().skillSucceeded = 1;
        for (auto& effect : tested.world.Map().effects.Effects())
            effect.alive = true;
        for (auto& joint : tested.joints)
            joint.alive = true;
        for (auto& particle : tested.world.Particles().All())
            particle.alive = true;
        const auto used = tested.WithTarget(probe.skill);
        ASSERT_TRUE(used.HasValue()) << used.Error();
        EXPECT_EQ(tested.Victim().flags78, probe.flags);
        EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
        EXPECT_EQ(tested.sounds,
                  probe.sound < 0 ? std::vector<std::int32_t>{} : std::vector<std::int32_t>{probe.sound});
    }
}

TEST(AttackSkillCasesTest, MixedPoolsStopAtTheFirstConstructorWithRoom)
{
    {
        SkillWorld tested;
        for (auto& joint : tested.joints)
            joint.alive = true;
        EXPECT_TRUE(tested.Head(0x30).HasValue());
        EXPECT_EQ(tested.sounds, std::vector<std::int32_t>{0x7f});
        EXPECT_EQ(AliveEffects(tested.world).size(), 2u);
        tested.sounds.clear();
        tested.joints.front().alive = false;
        for (auto& effect : tested.world.Map().effects.Effects())
            effect.alive = true;
        // With one slot, the first of the 36 joints and nothing more; the decals have no room and 0x7f sounds.
        const auto stepped = tested.Head(0x30);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(tested.joints.front().type, 0x613);
        EXPECT_EQ(tested.joints.front().subtype, 2);
        EXPECT_EQ(tested.sounds, std::vector<std::int32_t>{0x7f});
    }
    {
        SkillWorld tested;
        tested.withParticles = true;
        for (auto& effect : tested.world.Map().effects.Effects())
            effect.alive = true;
        // 0x50492e..0x504986: with room, the ten 0x5f2/1 over the target, without the 1 in +0x78 (no +0x38d), and the
        // sound.
        ASSERT_TRUE(tested.WithTarget(1).HasValue());
        const auto smokes =
            std::ranges::count_if(tested.world.Particles().All(), [](const auto& particle)
                                  { return particle.alive && particle.type == 0x5f2 && particle.subtype == 1; });
        EXPECT_EQ(smokes, 10);
        EXPECT_EQ(tested.Victim().flags78, 0u);
        EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x22}));
        tested.sounds.clear();
        tested.Striker().skillSucceeded = 1;
        tested.Striker().type = 0x164;
        for (auto& effect : tested.world.Map().effects.Effects())
            effect.alive = false;
        for (auto& particle : tested.world.Particles().All())
            particle.alive = true;
        ASSERT_TRUE(tested.WithTarget(1).HasValue());
        EXPECT_EQ(tested.Victim().flags78, 1u);
        EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x22}));
    }
}

TEST(AttackSkillCasesTest, KillingABuffJointRestoresTheConstructorFrontier)
{
    SkillWorld tested;
    for (auto& joint : tested.joints)
        joint.alive = true;
    auto& killed = tested.joints.front();
    killed.type = 0x10f;
    killed.subtype = 0;
    killed.ownerContext = &tested.Victim();
    // The only slot left is the one it killed: the first of the five takes it and the rest do not fit.
    ASSERT_TRUE(tested.WithTarget(0x10).HasValue());
    EXPECT_TRUE(killed.alive);
    EXPECT_EQ(killed.life, 999999);
    EXPECT_EQ(killed.width, 20.0f);
    EXPECT_EQ(tested.Victim().flags78, 0x100u);
    EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x84}));
}

TEST(AttackSkillCasesTest, BindingFlagsPrecedeTheJointAndExistingBindingsSkipIt)
{
    SkillWorld tested;
    tested.Striker().skillSucceeded = 1;
    for (auto& effect : tested.world.Map().effects.Effects())
        effect.alive = true;
    tested.Striker().attackTarget = 0xfffe;
    ASSERT_TRUE(tested.WithTarget(0x1b).HasValue());
    EXPECT_EQ(tested.Victim().flags78, 8u);
    // 0x5045f9: five 0x10f/4 of width 20 with c's +0x3a4, signed, in +0xa0c.
    const auto bindings = [&tested]
    {
        return std::ranges::count_if(tested.joints,
                                     [&tested](const JointState& joint)
                                     {
                                         return joint.alive && joint.type == 0x10f && joint.subtype == 4 &&
                                                joint.ownerContext == &tested.Victim() && joint.target == -2;
                                     });
    };
    EXPECT_EQ(bindings(), 5);
    for (auto& joint : tested.joints)
        joint.alive = false;
    auto& binding = tested.joints.front();
    binding.alive = true;
    binding.type = 0x10f;
    binding.subtype = 9;
    binding.ownerContext = &tested.Victim();
    EXPECT_TRUE(tested.WithTarget(0x1b).HasValue());
    EXPECT_EQ(bindings(), 0);
    EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
}

// 0x4fe505..0x4fe790: 0x48 clears o's +0x78 and creates six 0x10f/5..7 of width 170 from o + 100 in z (the copy drops
// 10 except after the /7), turned 45, 135, 225, 90, 180 and 270; then sound 0x26b. No rand().
TEST(AttackSkillCasesTest, TheRisingAurasLeaveFromAboveTheCaster)
{
    SkillWorld tested;
    tested.Striker().flags78 = 0x30;
    tested.Striker().position.z = 40;
    ASSERT_TRUE(tested.Head(0x48).HasValue());
    EXPECT_EQ(tested.Striker().flags78, 0u);
    constexpr std::array<float, 6> Turns{45, 135, 225, 90, 180, 270};
    constexpr std::array<float, 6> Heights{140, 130, 120, 120, 110, 100};
    for (std::size_t i = 0; i < Turns.size(); ++i)
    {
        SCOPED_TRACE(i);
        const JointState& rise = tested.joints[i];
        ASSERT_TRUE(rise.alive);
        EXPECT_EQ(rise.type, 0x10f);
        EXPECT_EQ(rise.subtype, static_cast<std::int32_t>(5 + i % 3));
        EXPECT_EQ(rise.width, 170.0f);
        EXPECT_EQ(rise.angles.z, Turns[i]);
        EXPECT_EQ(rise.pathIncrement.z, Heights[i]);
        EXPECT_EQ(rise.ownerContext, &tested.Striker());
    }
    EXPECT_FALSE(tested.joints[6].alive);
    EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x26b}));
    EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
}

// 0x4fe147..0x4fe18b: 0x43 creates the 0x632/7 over o, which drops three 0x632/6 with two rand() each, and 0x266
// sounds. Without the particle table, the /6 step cuts where it asks for its flare 0x5ee (0x53bf6e), after its rand().
TEST(AttackSkillCasesTest, TheStormDropsThreeBolts)
{
    SkillWorld tested;
    tested.Striker().angles = {0, 0, 90};
    ASSERT_TRUE(tested.Head(0x43).HasValue());
    EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x266}));
    const JointState& storm = tested.joints[0];
    ASSERT_TRUE(storm.alive);
    EXPECT_EQ(storm.subtype, 7);
    EXPECT_EQ(storm.life, 0);
    EXPECT_FALSE(storm.tailEnabled);
    for (std::size_t i = 1; i <= 3; ++i)
    {
        SCOPED_TRACE(i);
        const JointState& bolt = tested.joints[i];
        ASSERT_TRUE(bolt.alive);
        EXPECT_EQ(bolt.type, 0x632);
        EXPECT_EQ(bolt.subtype, 6);
        EXPECT_EQ(bolt.life, 25);
        EXPECT_EQ(bolt.volley, 25);
        EXPECT_EQ(bolt.width, 40.0f);
        EXPECT_EQ(bolt.orientation, 3);
        EXPECT_EQ(bolt.count, 0);
        EXPECT_EQ(bolt.ownerContext, nullptr);
        EXPECT_EQ(bolt.pathIncrement, bolt.position);
    }
    EXPECT_EQ(DrawsSince(Seed, tested.random), 6);
    const auto stepped = Client::Joints::StepJoints(tested.joints, tested.random);
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x53bf6e"), std::string::npos) << stepped.Error();
    EXPECT_EQ(DrawsSince(Seed, tested.random), 7);
}

TEST(AttackSkillCasesTest, FullJointsKeepAllStrikeAroundSelectors)
{
    const auto skills = SkillsWithRange(10);
    for (const int count : {0, 1, 10})
    {
        SCOPED_TRACE(count);
        SkillWorld tested;
        tested.skills = &skills;
        tested.Hero().alive = false;
        tested.Victim().alive = false;
        tested.Striker().byte88 = 0;
        tested.Striker().skillTarget4c = static_cast<std::int32_t>(VictimSlot);
        for (int target = 0; target < count; ++target)
            tested.Monster(static_cast<std::size_t>(100 + target), 50.0f);
        for (auto& joint : tested.joints)
            joint.alive = true;
        const auto used = tested.Head(0x4c);
        ASSERT_TRUE(used.HasValue()) << ErrorOf(used);
        EXPECT_EQ(DrawsSince(Seed, tested.random), 23 + count * (4 + SkillEntry));
    }
}

TEST(AttackSkillCasesTest, FullParticlesKeepTheFortySparkSelectors)
{
    SkillWorld tested;
    tested.withParticles = true;
    for (auto& particle : tested.world.Particles().All())
        particle.alive = true;
    const auto sparked = SparkTheTarget(tested.Striker(), tested.Victim(), StrikerSlot, tested.Context());
    ASSERT_TRUE(sparked.HasValue()) << ErrorOf(sparked);
    EXPECT_EQ(DrawsSince(Seed, tested.random), 40);
}

TEST(AttackSkillCasesTest, AFullEffectPoolStillDisturbsKalimaWhenItsOwnerExists)
{
    for (const bool hasOwner : {false, true})
    {
        SCOPED_TRACE(hasOwner);
        SkillWorld tested(24);
        tested.Striker().cellX = 64;
        tested.Striker().cellY = 63;
        if (hasOwner)
            tested.world.Map().kalima = std::make_unique<Client::MapScripts::Kalima::KalimaWorld>(24);
        for (auto& effect : tested.world.Map().effects.Effects())
            effect.alive = true;
        const auto used = tested.Head(0x0a);
        ASSERT_TRUE(used.HasValue()) << ErrorOf(used);
        EXPECT_EQ(DrawsSince(Seed, tested.random), OwnRecord + 8);
        EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x77}));
        if (hasOwner)
        {
            const auto& waves = Client::Tests::WavesOf(*tested.world.Map().kalima);
            EXPECT_NE(waves.planes[0][126 * 256 + 128], 0u);
            EXPECT_EQ(waves.planes[0][0], 0u);
        }
    }
}

std::vector<const Client::Effects::ModelEffect*> AliveOfType(const WorldState& world, std::int16_t type)
{
    std::vector<const Client::Effects::ModelEffect*> found;
    for (const Client::Effects::ModelEffect* effect : AliveEffects(world))
    {
        if (effect->type == type)
            found.push_back(effect);
    }
    return found;
}

// 0x5042d3..0x504324
float HeadingTo(const ActorState& from, const ActorState& to)
{
    return static_cast<float>(
        Client::Math::CreateAngle(from.position.x, from.position.y, to.position.x, to.position.y));
}

// 0x505024..0x505061, 0x11 of a player (+0x376 at 0xffff) other than the 0x15e, on every map: the 0x5ca/0
// (Effects/SkillBurstEffects: life 20, speed (0, −60, 0) and z 100 higher) over o with the angles toward the target,
// its light and the target as owner, no rand(), and sound 0x76 with no object; with the table full, only the sound.
TEST(AttackSkillCasesTest, TheEnergyBallFliesAtTheTarget)
{
    for (const std::int32_t map : Maps)
    {
        for (const bool full : {false, true})
        {
            SCOPED_TRACE(testing::Message() << "mapa " << map << (full ? ", lleno" : ""));
            SkillWorld tested(map);
            ActorState& striker = tested.Striker();
            striker.type = 0x20a;
            striker.position = glm::vec3(1000.0f, 2000.0f, 50.0f);
            striker.angles = glm::vec3(5.0f, 6.0f, 7.0f);
            striker.light = glm::vec3(0.5f, 0.25f, 1.0f);
            tested.Victim().position = glm::vec3(1300.0f, 2400.0f, 0.0f);
            if (full)
            {
                for (Client::Effects::ModelEffect& effect : tested.world.Map().effects.Effects())
                {
                    effect.alive = true;
                    effect.type = 0x01;
                }
            }

            const auto stepped = tested.WithTarget(0x11);

            ASSERT_TRUE(stepped.HasValue()) << ErrorOf(stepped);
            EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
            EXPECT_EQ(tested.sounds, std::vector<std::int32_t>{0x76});
            const auto balls = AliveOfType(tested.world, Client::Effects::EnergyBallEffect);
            if (full)
            {
                EXPECT_EQ(AliveEffects(tested.world).size(), tested.world.Map().effects.Effects().size());
                continue;
            }
            ASSERT_EQ(balls.size(), 1u);
            const Client::Effects::ModelEffect& ball = *balls.front();
            EXPECT_EQ(ball.subtype, 0);
            EXPECT_EQ(ball.ownerContext, &tested.Victim());
            EXPECT_EQ(ball.transform.position, glm::vec3(1000.0f, 2000.0f, 150.0f));
            EXPECT_EQ(ball.transform.angles, glm::vec3(5.0f, 6.0f, HeadingTo(striker, tested.Victim())));
            EXPECT_EQ(ball.light, striker.light);
            EXPECT_EQ(ball.life, 20);
            EXPECT_EQ(ball.velocity, glm::vec3(0.0f, -60.0f, 0.0f));
            EXPECT_EQ(ball.disturbanceState, 0xffff);
        }
    }
}

// 0x4feabb..0x4feb36, 0x3f: the 0xcb/2 with o as owner and the ownerless 0xcc/3, both over o with its light (life 250,
// no rand()), and sound 0x1cb from o.
TEST(AttackSkillCasesTest, TheRaisedRingsGoOverTheStriker)
{
    SkillWorld tested;
    ActorState& striker = tested.Striker();
    striker.position = glm::vec3(1250.0f, 3350.0f, 120.0f);
    striker.angles = glm::vec3(0.0f, 0.0f, 45.0f);
    striker.light = glm::vec3(0.5f, 0.25f, 1.0f);

    ASSERT_TRUE(tested.Head(0x3f).HasValue());

    const auto inner = AliveOfType(tested.world, Client::Effects::InnerCircleEffect);
    const auto outer = AliveOfType(tested.world, Client::Effects::OuterCircleEffect);
    ASSERT_EQ(inner.size(), 1u);
    ASSERT_EQ(outer.size(), 1u);
    EXPECT_EQ(inner.front()->subtype, 2);
    EXPECT_EQ(inner.front()->life, 250);
    EXPECT_EQ(inner.front()->ownerContext, &striker);
    EXPECT_EQ(inner.front()->transform.position, striker.position);
    EXPECT_EQ(inner.front()->light, striker.light);
    EXPECT_EQ(outer.front()->subtype, 3);
    EXPECT_EQ(outer.front()->life, 250);
    EXPECT_EQ(outer.front()->ownerContext, nullptr);
    EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
    EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x1cb}));
    ASSERT_EQ(tested.soundOrigins.size(), 1u);
    EXPECT_EQ(tested.soundOrigins.front(), striker.position);
}

// 0x4fecb5..0x4fed2a, 0x0a with someone other than the hero: 0x59fb80(0xa), the 0xcb/0 with o as owner, o's +0x8a and
// the skill's slot in +0x89 (without the own record or the targets, which are the hero's), the ownerless 0xcc/0 and
// sound 0x77.
TEST(AttackSkillCasesTest, TheHellfireLeavesBothCirclesOverTheStriker)
{
    SkillWorld tested;
    ActorState& striker = tested.Striker();
    striker.position = glm::vec3(1250.0f, 3350.0f, 120.0f);
    striker.word8a = 0x0123;
    tested.world.OwnCharacter().SetSkillAt(3, 0x0a);
    tested.world.OwnCharacter().bytef04 = 0x41;

    ASSERT_TRUE(tested.Head(0x0a).HasValue());

    const auto inner = AliveOfType(tested.world, Client::Effects::InnerCircleEffect);
    const auto outer = AliveOfType(tested.world, Client::Effects::OuterCircleEffect);
    ASSERT_EQ(inner.size(), 1u);
    ASSERT_EQ(outer.size(), 1u);
    EXPECT_EQ(inner.front()->subtype, 0);
    EXPECT_EQ(inner.front()->life, 45);
    EXPECT_EQ(inner.front()->ownerContext, &striker);
    EXPECT_EQ(inner.front()->disturbanceState, 0x0123);
    EXPECT_EQ(inner.front()->byte89, 3);
    EXPECT_EQ(outer.front()->subtype, 0);
    EXPECT_EQ(outer.front()->life, 40);
    EXPECT_EQ(outer.front()->ownerContext, nullptr);
    EXPECT_EQ(outer.front()->disturbanceState, 0xffff);
    EXPECT_EQ(tested.world.OwnCharacter().bytef04, 0x41);
    EXPECT_EQ(DrawsSince(Seed, tested.random), OwnRecord);
    EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x77}));
}

// 0x4ff4c3..0x4ff514, 0x28: the hidden 0xcb/1 (+0x58 at −2) with o as owner, without rand(), and sound 0x93.
TEST(AttackSkillCasesTest, TheHiddenCircleGoesWithTheStriker)
{
    SkillWorld tested;
    ActorState& striker = tested.Striker();

    ASSERT_TRUE(tested.Head(0x28).HasValue());

    const auto inner = AliveOfType(tested.world, Client::Effects::InnerCircleEffect);
    ASSERT_EQ(inner.size(), 1u);
    EXPECT_EQ(inner.front()->subtype, 1);
    EXPECT_EQ(inner.front()->objectState.hiddenMesh, -2);
    EXPECT_EQ(inner.front()->ownerContext, &striker);
    EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
    EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x93}));
}

// 0x4ff51c..0x4ff5f2, 0x26: two ownerless 0xc2/6 at the center of the skill's cell at ground height, light (0.8f; 0.5;
// 0.1f), key 0 (+0xd8 at 0), each 500 to 799 higher and with its trail 0x5f2/0, owned by it (0x51b098, width 100, no
// rand()); then sound 0x8e and, with the hero, the volley in +0xf04. Without the heights, it cuts before the first
// and with no rand().
TEST(AttackSkillCasesTest, TheMeteorsFallWithTheirTrails)
{
    SkillWorld tested;
    Flatten(tested.terrain);
    ActorState& striker = tested.Striker();
    striker.skillCellX = 120;
    striker.skillCellY = 33;

    const auto stepped = tested.Head(0x26);
    ASSERT_TRUE(stepped.HasValue()) << ErrorOf(stepped);

    const auto meteors = AliveOfType(tested.world, Client::Effects::FireEffect);
    ASSERT_EQ(meteors.size(), 2u);
    const auto& joints = tested.joints;
    for (std::size_t index = 0; index < meteors.size(); ++index)
    {
        SCOPED_TRACE(index);
        const Client::Effects::ModelEffect& meteor = *meteors[index];
        EXPECT_EQ(meteor.subtype, 6);
        EXPECT_EQ(meteor.ownerContext, nullptr);
        EXPECT_EQ(meteor.speed, 0.0f);
        EXPECT_EQ(meteor.light, glm::vec3(0.8f, 0.5f, 0.1f));
        EXPECT_GE(meteor.transform.position.x, 12050.0f + 200.0f);
        EXPECT_LT(meteor.transform.position.x, 12050.0f + 300.0f);
        EXPECT_GE(meteor.transform.position.z, 150.0f + 500.0f);
        EXPECT_LT(meteor.transform.position.z, 150.0f + 800.0f);
        const auto& trail = joints[index];
        ASSERT_TRUE(trail.alive);
        EXPECT_EQ(trail.type, 0x5f2);
        EXPECT_EQ(trail.subtype, 0);
        EXPECT_EQ(trail.ownerContext, &meteor);
        EXPECT_EQ(trail.position, meteor.transform.position);
        EXPECT_EQ(trail.width, 100.0f);
        EXPECT_EQ(trail.life, 20);
        EXPECT_EQ(trail.texture, 0x60f);
    }
    EXPECT_FALSE(joints[2].alive);
    EXPECT_EQ(DrawsSince(Seed, tested.random), 10);
    EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x8e}));

    SkillWorld bare;
    EXPECT_TRUE(HasError(bare.Head(0x26), "0x4ff570"));
    EXPECT_EQ(DrawsSince(Seed, bare.random), 0);
    EXPECT_TRUE(AliveEffects(bare.world).empty());
}

// 0x500c10..0x500c5c, 0x08: 0x59fb80(8) and the 0xcf/0 over o with 0x4fd5f0's local light, o as owner, o's +0x8a and
// the slot in +0x89, at ground height; its creation counts in the own record (+0x8c with the count, which rises). Then
// sound 0x74.
TEST(AttackSkillCasesTest, TheTwisterTakesTheBodyLight)
{
    for (const glm::vec3& light : {Client::Skills::BodyLight, glm::vec3(0.3f, 0.5f, 1.0f)})
    {
        SkillWorld tested;
        Flatten(tested.terrain);
        ActorState& striker = tested.Striker();
        striker.position = glm::vec3(1250.0f, 3350.0f, 20.0f);
        striker.light = glm::vec3(0.5f, 0.25f, 1.0f);
        striker.word8a = 0x0123;
        tested.world.OwnCharacter().SetSkillAt(3, 0x08);
        tested.world.OwnCharacter().bytef04 = 0x41;

        const auto stepped = StepTheHeadSkill(striker, StrikerSlot, 0x08, tested.Context(), light);
        ASSERT_TRUE(stepped.HasValue()) << ErrorOf(stepped);

        const auto storms = AliveOfType(tested.world, Client::Effects::StormEffect);
        ASSERT_EQ(storms.size(), 1u);
        const Client::Effects::ModelEffect& storm = *storms.front();
        EXPECT_EQ(storm.subtype, 0);
        EXPECT_EQ(storm.light, light);
        EXPECT_EQ(storm.ownerContext, &striker);
        EXPECT_EQ(storm.disturbanceState, 0x0123);
        EXPECT_EQ(storm.byte89, 3);
        EXPECT_EQ(storm.word8c, 0x41);
        EXPECT_EQ(storm.transform.position.z, 150.0f);
        EXPECT_EQ(tested.world.OwnCharacter().bytef04, 0x42);
        EXPECT_EQ(DrawsSince(Seed, tested.random), 2 * OwnRecord);
        EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x74}));
    }
}

// 0x504740..0x504800, 0x07: the 0xc1/0 over the target with o's angles and the local light, five ice pieces 0xca (five
// rand() each), with +0x38d the 2 in the target's +0x78, and sound 0x78.
TEST(AttackSkillCasesTest, TheIceFreezesTheTarget)
{
    for (const std::uint8_t succeeded : {std::uint8_t{0}, std::uint8_t{1}})
    {
        SCOPED_TRACE(int{succeeded});
        SkillWorld tested;
        ActorState& striker = tested.Striker();
        striker.skillSucceeded = succeeded;
        striker.angles = glm::vec3(0.0f, 0.0f, 90.0f);
        ActorState& victim = tested.Victim();
        victim.position = glm::vec3(2250.0f, 1350.0f, 80.0f);
        const glm::vec3 light(0.3f, 0.5f, 1.0f);

        const auto stepped = StepTheTargetSkill(striker, victim, StrikerSlot, 0x07, tested.Context(), light);
        ASSERT_TRUE(stepped.HasValue()) << ErrorOf(stepped);

        const auto ice = AliveOfType(tested.world, Client::Effects::IceEffect);
        ASSERT_EQ(ice.size(), 1u);
        EXPECT_EQ(ice.front()->subtype, 0);
        EXPECT_EQ(ice.front()->life, 50);
        EXPECT_EQ(ice.front()->light, light);
        EXPECT_EQ(ice.front()->transform.position, victim.position);
        EXPECT_EQ(AliveOfType(tested.world, Client::Effects::IceShardEffect).size(), 5u);
        EXPECT_EQ(DrawsSince(Seed, tested.random), 25);
        EXPECT_EQ(victim.flags78, succeeded != 0 ? 2u : 0u);
        EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x78}));
    }
}

// 0x5049bf..0x5049fd, 0x02: the 0xc2/0 over the target with its angles and o's light, and sound 0x42. 0x504b9e, 0x04:
// the 0xc2/1 from o toward the target, with it as owner, and the same sound.
TEST(AttackSkillCasesTest, TheFiresGoToTheTarget)
{
    {
        SkillWorld tested;
        tested.Striker().light = glm::vec3(0.5f, 0.25f, 1.0f);
        ActorState& victim = tested.Victim();
        victim.position = glm::vec3(2250.0f, 1350.0f, 80.0f);

        ASSERT_TRUE(tested.WithTarget(0x02).HasValue());

        const auto fires = AliveOfType(tested.world, Client::Effects::FireEffect);
        ASSERT_EQ(fires.size(), 1u);
        EXPECT_EQ(fires.front()->subtype, 0);
        EXPECT_EQ(fires.front()->ownerContext, nullptr);
        EXPECT_EQ(fires.front()->light, tested.Striker().light);
        EXPECT_EQ(fires.front()->transform.position.z, 480.0f);
        EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x42}));
    }
    {
        SkillWorld tested;
        ActorState& striker = tested.Striker();
        striker.position = glm::vec3(1250.0f, 3350.0f, 20.0f);
        striker.angles = glm::vec3(5.0f, 7.0f, 90.0f);
        ActorState& victim = tested.Victim();
        victim.position = glm::vec3(2250.0f, 1350.0f, 80.0f);

        ASSERT_TRUE(tested.WithTarget(0x04).HasValue());

        const auto fires = AliveOfType(tested.world, Client::Effects::FireEffect);
        ASSERT_EQ(fires.size(), 1u);
        EXPECT_EQ(fires.front()->subtype, 1);
        EXPECT_EQ(fires.front()->ownerContext, &victim);
        EXPECT_EQ(fires.front()->transform.angles, glm::vec3(5.0f, 7.0f, HeadingTo(striker, victim)));
        EXPECT_EQ(fires.front()->transform.position.z, 140.0f);
        EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x42}));
    }
}

// 0x504be9..0x504cce, 0x0b: the 0xce/0 from o toward the target; the 0x164 first creates two more, with the heading 10
// more and 10 less. Sound 0x76.
TEST(AttackSkillCasesTest, ThePowerWaveFansOutForTheTriple)
{
    for (const std::uint16_t type : {std::uint16_t{0x20a}, std::uint16_t{0x164}})
    {
        SCOPED_TRACE(type);
        SkillWorld tested;
        ActorState& striker = tested.Striker();
        striker.type = type;
        striker.position = glm::vec3(1250.0f, 3350.0f, 20.0f);
        ActorState& victim = tested.Victim();
        victim.position = glm::vec3(2250.0f, 1350.0f, 80.0f);
        const float heading = HeadingTo(striker, victim);

        ASSERT_TRUE(tested.WithTarget(0x0b).HasValue());

        const auto waves = AliveOfType(tested.world, Client::Effects::MagicEffect);
        std::vector<float> headings;
        for (const Client::Effects::ModelEffect* wave : waves)
        {
            EXPECT_EQ(wave->subtype, 0);
            EXPECT_EQ(wave->ownerContext, nullptr);
            headings.push_back(wave->transform.angles.z);
        }
        const auto turned = [heading](double by) { return static_cast<float>(static_cast<double>(heading) + by); };
        const float back = turned(10.0);
        const float other = static_cast<float>(static_cast<double>(back) - 20.0);
        const float last = static_cast<float>(static_cast<double>(other) + 10.0);
        EXPECT_EQ(headings, type == 0x164 ? (std::vector<float>{back, other, last}) : (std::vector<float>{heading}));
        EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
        EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x76}));
    }
}

// 0x504fdc..0x505022, 0x11 of the 0x15e: the 0xd7/0 from o toward the target, with it as owner, no sound. 0x5048f1,
// 0x01 of the 0x20a: the 0xc3/0 over the target, before the particles.
TEST(AttackSkillCasesTest, TheSnowballAndThePoisonFindTheTarget)
{
    {
        SkillWorld tested;
        ActorState& striker = tested.Striker();
        striker.type = 0x15e;
        striker.position = glm::vec3(1250.0f, 3350.0f, 20.0f);
        ActorState& victim = tested.Victim();
        victim.position = glm::vec3(2250.0f, 1350.0f, 80.0f);

        ASSERT_TRUE(tested.WithTarget(0x11).HasValue());

        const auto snow = AliveOfType(tested.world, Client::Effects::SnowEffect);
        ASSERT_EQ(snow.size(), 1u);
        EXPECT_EQ(snow.front()->ownerContext, &victim);
        EXPECT_EQ(snow.front()->transform.angles.z, HeadingTo(striker, victim));
        EXPECT_TRUE(tested.sounds.empty());
    }
    {
        SkillWorld tested;
        tested.Striker().type = 0x20a;
        ActorState& victim = tested.Victim();
        victim.position = glm::vec3(2250.0f, 1350.0f, 80.0f);

        EXPECT_TRUE(HasError(tested.WithTarget(0x01), "0x50497e"));

        const auto poison = AliveOfType(tested.world, Client::Effects::PoisonEffect);
        ASSERT_EQ(poison.size(), 1u);
        EXPECT_EQ(poison.front()->transform.position, victim.position);
        EXPECT_EQ(poison.front()->life, 40);
        EXPECT_EQ(DrawsSince(Seed, tested.random), 0);
    }
}

} // namespace
