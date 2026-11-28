#include "Skills/AttackStep.h"
#include "Skills/AttackSkillCases.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Models/LogicalAccess.h"
#include "Models/Skeleton.h"
#include "Kernel/ActorState.h"
#include "Actors/CharacterStep.h"
#include "Session/CharacterStepHooks.h"
#include "Combat/Attackable.h"
#include "Joints/FaunaJointCreation.h"
#include "Kernel/DrawTailState.h"
#include "Guild/GuildState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/WorldMap.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Effects/GameModelEffects.h"
#include "Effects/ModelEffects.h"
#include "Particles/Particles.h"
#include "Effects/WizardModelEffects.h"
#include "Particles/PortableParticleTrigonometry.h"
#include "Terrain/TerrainLighting.h"
#include "Terrain/WorldTerrain.h"
#include "Assets/FaunaModels.h"
#include "Audio/SoundId.h"

#include "support/PlayerActors.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// StepTheAttack (0x4fdb9a..0x50539e with 0x4f97b0 and 0x4f5b70) and ChosenKeyOf (0x558eb0). 06, "El paso del ataque".
namespace
{
const Client::Session::CharacterStepHooks StepHooks{};
using Client::Actors::CharacterStepContext;
using Client::Actors::StepCharacters;
using Client::Combat::ChosenKeyOf;
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Kernel::JointState;
using Client::Kernel::WorldState;
using Client::Models::ActionInfo;
using Client::Skills::StepTheAttack;

constexpr std::uint32_t Seed = 0x13579bdfu;
constexpr std::size_t HeroSlot = 42;
constexpr std::size_t OtherSlot = 3;
constexpr std::size_t VictimSlot = 7;
// One read of +0x38e (0x517630 and its copies): one rand() when it closes at 0.
constexpr int SkillRead = 1;
// The own record closed at 0: the 3848 rand() of 0x40b4d0.
constexpr int OwnRecord = Client::Models::OwnRecordCloseRandoms;
// A +0x381 step before frame 5 with a target: the read of 0x4f97b0, the rand() of 0x4f5b97 and the two reads of
// 0x4f811b and 0x4f8a2c.
constexpr int CountingStep = SkillRead + 1 + 2 * SkillRead;
// The step that strikes before the skill: the same with 0x4f97b0's own record (frame 5).
constexpr int StrikingStep = CountingStep + OwnRecord;
// The skill with a target without damage or +0x380 at 2: the head, the two switches, the sound with the own record and
// the write of +0x38e.
constexpr int SkillWithTarget = SkillRead + SkillRead + SkillRead + 1 + OwnRecord + SkillRead;
// Without a target: the head, the read at 0x502b12 and the write.
constexpr int SkillWithoutTarget = 3 * SkillRead;

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

struct AttackWorld
{
    AttackWorld()
    {
        world.Runtime().map = 0;
        world.Runtime().hero = HeroSlot;
        world.Runtime().heroSlot = static_cast<std::int32_t>(HeroSlot);
        world.OwnCharacter().ClearEquipmentForEntry();
        // The timers of 0x4fc450 far from 0: its helpers would read Item.bmd with a weapon in the own record.
        constexpr std::uint16_t FarTimer = 1000;
        world.OwnCharacter().word3e = FarTimer;
        world.OwnCharacter().word40 = FarTimer;
        world.OwnCharacter().word42 = FarTimer;
        terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        Hero() = Player(1);
        Victim() = Player(2);
        Victim().alive = false;
    }

    static ActorState Player(std::uint8_t characterClass)
    {
        ActorState actor = Client::Tests::UnarmedPlayer(characterClass);
        actor.alive = true;
        actor.byte88 = 1;
        actor.pk = 3;
        actor.animation.action = 1;
        return actor;
    }

    ActorState& Hero()
    {
        return world.Actors()[HeroSlot];
    }
    ActorState& Victim()
    {
        return world.Actors()[VictimSlot];
    }

    // The striker with +0x381 at 1, as the attack leaves it.
    ActorState& Striker(std::size_t slot, std::uint8_t action, std::size_t target)
    {
        ActorState& striker = world.Actors()[slot];
        if (slot != HeroSlot)
            striker = Player(0);
        striker.animation.action = action;
        striker.byte381 = 1;
        striker.attackTarget = static_cast<std::uint16_t>(target);
        return striker;
    }

    ActorState& Monster(std::size_t slot, std::uint16_t type, std::uint16_t kind, std::size_t target)
    {
        ActorState& monster = world.Actors()[slot];
        monster = ActorState{};
        monster.alive = true;
        monster.type = type;
        monster.word376 = kind;
        monster.byte88 = 2;
        monster.animation.action = 3;
        monster.byte381 = 1;
        monster.attackTarget = static_cast<std::uint16_t>(target);
        return monster;
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
                                    { sounds.push_back(sound.Value()); },
                                    .soundAt =
                                        [this](Client::Audio::SoundId sound, const glm::vec3& origin)
                                    {
                                        sounds.push_back(sound.Value());
                                        soundOrigins.push_back(origin);
                                    },
                                    .controlDown = controlDown,
                                    .faunaModels = &faunaModels,
                                    .hooks = &StepHooks};
    }

    Client::Core::Expected<void, std::string> Attack(ActorState& striker, std::size_t slot)
    {
        return StepTheAttack(striker, slot, Context());
    }

    std::unique_ptr<WorldState> ownedWorld = std::make_unique<WorldState>();
    WorldState& world = *ownedWorld;
    Client::Terrain::WorldTerrain terrain;
    CrtRandom random{Seed};
    Client::Models::Skeleton skeleton = Client::Tests::PlayerActionsSkeleton();
    std::vector<std::int32_t> sounds;
    std::vector<glm::vec3> soundOrigins;
    bool controlDown = false;
    Client::Particles::Particles particles;
    bool withParticles = true;
    // A short pool: the killer's two 0x618 are the only joints in these tests.
    std::vector<JointState> joints = std::vector<JointState>(16);
    bool withJoints = true;
    Client::Assets::FaunaModels faunaModels;
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

// Before frame 5, each step reads +0x38e in 0x4f97b0, draws 0x4f5b70's rand(), reads +0x38e twice (the target is in
// the pool) and raises +0x381; the one that leaves it at 15 runs the skill: o+0x8a at −1 (no chosen one), the head, the
// two switches, the sound 0x46 + rand() & 3 with the own record open, and +0x38e, +0x382 and +0x380 at 0. Then nothing.
TEST(AttackStepTest, TheCounterRunsFourteenStepsAndThenTheSkill)
{
    AttackWorld tested;
    ActorState& hero = tested.Striker(HeroSlot, 0x23, VictimSlot);
    hero.word382 = 18;
    hero.word8a = 5;
    CrtRandom expected(Seed);
    for (int step = 1; step < 14; ++step)
    {
        ASSERT_TRUE(tested.Attack(hero, HeroSlot).HasValue());
        EXPECT_EQ(hero.byte381, step + 1);
        expected.Discard(CountingStep);
        EXPECT_EQ(tested.random.State(), expected.State());
    }

    const auto struck = tested.Attack(hero, HeroSlot);

    ASSERT_TRUE(struck.HasValue()) << struck.Error();
    EXPECT_EQ(hero.byte381, 0);
    EXPECT_EQ(hero.word8a, -1);
    EXPECT_EQ(hero.word382, 0);
    EXPECT_EQ(hero.byte380, 0);
    EXPECT_EQ(tested.world.Runtime().skillThreshold, 15);
    expected.Discard(CountingStep + SkillRead + SkillRead + SkillRead);
    const int drawn = expected.Next() & 3;
    expected.Discard(OwnRecord + SkillRead);
    EXPECT_EQ(tested.random.State(), expected.State());
    EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x46 + drawn}));

    ASSERT_TRUE(tested.Attack(hero, HeroSlot).HasValue());
    EXPECT_EQ(tested.random.State(), expected.State());
}

// Past frame 5 (0x6b87f8), 0x4f97b0 reads the own record's left hand and sets +0x381 to 15: the skill runs in the same
// step. The 0x20a's 0x3e ends from frame 1, without the record. Before 5, or with NaN, it goes on.

TEST(AttackStepTest, TheStrikeFrameEndsTheCount)
{
    struct Case
    {
        std::string_view name;
        std::uint8_t action;
        float frame;
        int draws;
        bool struck;
    };
    for (const Case& probe : {Case{"0x23 en el frame 5", 0x23, 5.0f, StrikingStep + SkillWithTarget, true},
                              Case{"0x97 en el frame 7", 0x97, 7.0f, StrikingStep + SkillWithTarget, true},
                              Case{"0x23 en el frame 4,9", 0x23, 4.9f, CountingStep, false},
                              Case{"0x98 en el frame 5", 0x98, 5.0f, CountingStep, false},
                              Case{"0x3e en el frame 1", 0x3e, 1.0f, CountingStep + SkillWithTarget, true},
                              Case{"0x3e en el frame 0,9", 0x3e, 0.9f, CountingStep, false}})
    {
        SCOPED_TRACE(probe.name);
        AttackWorld tested;
        ActorState& hero = tested.Striker(HeroSlot, probe.action, VictimSlot);
        hero.animation.frame = probe.frame;

        const auto stepped = tested.Attack(hero, HeroSlot);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(hero.byte381, probe.struck ? 0 : 2);
        EXPECT_EQ(DrawsSince(Seed, tested.random), probe.draws);
        EXPECT_EQ(tested.sounds.size(), probe.struck ? 1u : 0u);
    }
}

// A monster with no case in 0x4f5b70 (+0x376 3 in Lorencia) counts and strikes like a player: the map helpers do not
// check it, its +0x376 goes to 0x4f80e0 and 0x4f97b0 lets it strike in action 3 or 4 from frame 5. The sound is that of
// the own record's hands (Q-C1168).

TEST(AttackStepTest, AnOrdinaryMonsterCountsAndStrikesLikeAPlayer)
{
    AttackWorld tested;
    ActorState& monster = tested.Monster(OtherSlot, 0x155, 3, HeroSlot);
    for (int step = 1; step < 4; ++step)
    {
        const auto counted = tested.Attack(monster, OtherSlot);
        ASSERT_TRUE(counted.HasValue()) << counted.Error();
        EXPECT_EQ(monster.byte381, step + 1);
    }
    EXPECT_EQ(DrawsSince(Seed, tested.random), 3 * CountingStep);

    monster.animation.frame = 5.0f;
    const auto struck = tested.Attack(monster, OtherSlot);

    ASSERT_TRUE(struck.HasValue()) << struck.Error();
    EXPECT_EQ(monster.byte381, 0);
    EXPECT_EQ(DrawsSince(Seed, tested.random), 3 * CountingStep + StrikingStep + SkillWithTarget);
    EXPECT_EQ(tested.sounds.size(), 1u);

    // In action 5 it is not a monster attack for 0x4f97b0: it keeps counting.
    ActorState& other = tested.Monster(VictimSlot + 1, 0x155, 3, HeroSlot);
    other.animation.action = 5;
    other.animation.frame = 5.0f;
    ASSERT_TRUE(tested.Attack(other, VictimSlot + 1).HasValue());
    EXPECT_EQ(other.byte381, 2);
}

// 0x4f5b70's helpers, each on its maps, after 0x4f5b97's rand(): those that return 1 leave without the tail (0x4436f0,
// Kalima's, and those of 33 and 54 outside their effect); 0x42fce0 and 0x4324c0 check the type and +0x381 and go on.
// The fortress's E1/1 creates its joint; then the caller reaches threshold 15 and runs the skill. The other cuts and
// the cases one by one are in test_monster_attack_cases.cpp.

TEST(AttackStepTest, TheMapHelpersOfTheMonstersExitOrCut)
{
    struct Case
    {
        std::string_view name;
        std::int32_t map;
        std::uint16_t type;
        std::uint16_t kind;
        std::uint8_t counter;
        std::string_view cut;
        int draws;
        std::vector<std::int32_t> sounds{};
        bool completesAttack = false;
    };
    // The read of 0x4f97b0 and the rand() of 0x4f5b97.
    constexpr int Helper = SkillRead + 1;
    const std::array cases{
        Case{"Kalima 1", 0x18, 0x160, 0x91, 1, "", Helper + SkillRead},
        Case{"Kalima 7", 0x24, 0x160, 0x113, 1, "", Helper + SkillRead},
        Case{"Kalima fuera de Kalima", 0, 0x160, 0x91, 1, "", CountingStep},
        Case{"el 31", 0x1f, 0x160, 0x123, 1, "", Helper},
        Case{"el 31 con otro", 0x1f, 0x160, 0x126, 1, "", CountingStep},
        // The creation of the pulse 0x61c/0 draws its rand() % 360 and creates its joint 0x61c/3 (0x51f1c5, without
        // rand()).
        Case{"el pulso del asedio", 0x1e, 0x160, 0x120, 1, "", Helper + 1},
        Case{"la piedra del asedio en5", 0x1e, 0x160, 0x68, 5, "", Helper},
        Case{"el 54", 0x36, 0x160, 0x134, 1, "", Helper},
        Case{"el 35 en 14", 0x23, 0x1b2, 3, 14, "", Helper + OwnRecord + SkillWithTarget, {0x47}, true},
        Case{"el 35 en 2", 0x23, 0x1b2, 3, 2, "", CountingStep},
        Case{"Crywolf 0x17c", 0x22, 0x17c, 3, 1, "", Helper + 1210, std::vector<std::int32_t>(8, 0x40)},
        Case{"Crywolf 0x1ad en 2", 0x22, 0x1ad, 3, 2, "", CountingStep},
        Case{"un caso de +0x376", 0, 0x160, 0x4d, 1, "", CountingStep + SkillRead}};
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(probe.name);
        AttackWorld tested;
        tested.world.Runtime().map = probe.map;
        ActorState& monster = tested.Monster(OtherSlot, probe.type, probe.kind, HeroSlot);
        monster.byte381 = probe.counter;
        if (probe.completesAttack)
        {
            monster.position = {2100, 3100, 450};
            monster.light = {.2f, .4f, .6f};
            monster.word8a = -0x1234;
            tested.world.OwnCharacter().bytef04 = 0x5a;
        }

        const auto stepped = tested.Attack(monster, OtherSlot);

        if (probe.cut.empty())
        {
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            EXPECT_EQ(monster.byte381, probe.completesAttack ? 0 : probe.counter + 1);
        }
        else
        {
            EXPECT_TRUE(HasError(stepped, probe.cut)) << ErrorOf(stepped);
        }
        EXPECT_EQ(DrawsSince(Seed, tested.random), probe.draws);
        EXPECT_EQ(tested.sounds, probe.sounds);
        if (probe.map == 30 && probe.cut.empty())
        {
            const auto effects = tested.world.Map().effects.Effects();
            ASSERT_EQ(std::ranges::count_if(effects, [](const auto& effect) { return effect.alive; }), 1);
            EXPECT_EQ(effects[0].type, probe.kind == 0x120 ? 0x61c : 0x19f);
            EXPECT_EQ(effects[0].subtype, 0);
            EXPECT_EQ(effects[0].transform.position.x, monster.position.x);
            EXPECT_EQ(effects[0].transform.position.y, monster.position.y);
            EXPECT_EQ(effects[0].transform.position.z, monster.position.z + (probe.kind == 0x68 ? 500.0f : 0.0f));
            EXPECT_FALSE(effects[0].hasOwner);
        }
        if (probe.completesAttack)
        {
            const auto effects = tested.world.Map().effects.Effects();
            ASSERT_EQ(std::ranges::count_if(effects, [](const auto& effect) { return effect.alive; }), 1);
            const auto& arrow = effects[0];
            EXPECT_EQ(arrow.type, 0xe1);
            EXPECT_EQ(arrow.subtype, 1);
            EXPECT_EQ(arrow.life, 30);
            EXPECT_EQ(arrow.ownerContext, &monster);
            EXPECT_EQ(arrow.disturbanceState, 0xedcc);
            EXPECT_EQ(arrow.word8c, 0x5a);
            EXPECT_EQ(arrow.light, monster.light);
            EXPECT_EQ(arrow.transform.position, glm::vec3(2090, 3040, 585));
            const auto& joints = tested.world.DrawTail().joints;
            ASSERT_EQ(std::ranges::count_if(joints, [](const auto& joint) { return joint.alive; }), 1);
            EXPECT_EQ(joints[0].type, 0x610);
            EXPECT_EQ(joints[0].subtype, 13);
            EXPECT_EQ(joints[0].ownerContext, &arrow);
            EXPECT_EQ(joints[0].life, 40);
            EXPECT_EQ(joints[0].width, 20.0f);
            EXPECT_EQ(monster.byte38eAccesses, 0);
        }
    }
}

// Without a target (+0x3a4 at −1), 0x4f5b70 leaves after its rand() (0x4f80ea) and the skill does not sound.

TEST(AttackStepTest, WithoutATargetThereIsNoSound)
{
    AttackWorld tested;
    ActorState& hero = tested.Striker(HeroSlot, 0x22, 0xffff);
    hero.animation.frame = 6.0f;

    const auto stepped = tested.Attack(hero, HeroSlot);

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawsSince(Seed, tested.random), SkillRead + OwnRecord + 1 + SkillWithoutTarget);
    EXPECT_TRUE(tested.sounds.empty());
    EXPECT_EQ(hero.byte381, 0);
}

// The sound comes from the own record's hands, also when another strikes: 0x4b with both from 0x800 to 0x9ff and 0x46
// otherwise; the cursor item, taken from the inventory grid, takes the place of [0x78a6758].

TEST(AttackStepTest, TheStrikeSoundFollowsTheOwnHands)
{
    struct Case
    {
        std::string_view name;
        std::uint16_t right;
        std::uint16_t left;
        std::optional<std::uint16_t> cursor;
        std::int32_t slot;
        std::int32_t base;
    };
    for (const Case& probe : {Case{"sin armas", 0xffff, 0xffff, std::nullopt, 0, 0x46},
                              Case{"la ballesta y el arco", 0x808, 0x800, std::nullopt, 0, 0x4b},
                              Case{"la espada y el arco", 0x000, 0x800, std::nullopt, 0, 0x46},
                              Case{"el cursor en la derecha", 0x000, 0x800, std::uint16_t{0x808}, 0, 0x4b},
                              Case{"el cursor en la izquierda", 0x808, 0x000, std::uint16_t{0x9ff}, 1, 0x4b},
                              Case{"el cursor en otro lugar", 0x808, 0x000, std::uint16_t{0x9ff}, 2, 0x46}})
    {
        SCOPED_TRACE(probe.name);
        AttackWorld tested;
        auto& equipment = tested.world.OwnCharacter().equipment;
        equipment[0].id = probe.right;
        equipment[1].id = probe.left;
        if (probe.cursor)
        {
            auto& runtime = tested.world.Runtime();
            runtime.cursorItemCount = 1;
            runtime.cursorGrid = Client::Kernel::CursorGrid::Inventory;
            runtime.slot78a6758 = probe.slot;
            runtime.cursorItem.id = *probe.cursor;
        }
        ActorState& other = tested.Striker(OtherSlot, 0x23, HeroSlot);
        other.byte381 = 14;
        other.position = glm::vec3(125.0f, 250.0f, 375.0f);

        const auto stepped = tested.Attack(other, OtherSlot);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        CrtRandom expected(Seed);
        expected.Discard(CountingStep + 3 * SkillRead);
        EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{probe.base + (expected.Next() & 3)}));
        ASSERT_EQ(tested.soundOrigins.size(), 1u);
        EXPECT_EQ(tested.soundOrigins.front(), other.position);
    }
}

// 0x5042ce: with 0x39 at 14, 0x52e740 does not cut: the spark 0x5c6/0 on the target and, with two rand() before each,
// the twenty 0x5c5/0; the step goes on.
TEST(AttackStepTest, TheSparkOf0x39LeavesItsParticles)
{
    AttackWorld tested;
    ActorState& hero = tested.Striker(HeroSlot, 0x23, VictimSlot);
    hero.animation.action = 0x39;
    hero.byte381 = 14;

    const auto stepped = tested.Attack(hero, HeroSlot);

    ASSERT_TRUE(stepped.HasValue()) << ErrorOf(stepped);
    const auto count = [&](std::int32_t type)
    {
        return std::ranges::count_if(tested.particles.All(), [type](const Client::Particles::Particle& particle)
                                     { return particle.alive && particle.type == type && particle.subtype == 0; });
    };
    EXPECT_EQ(count(0x5c6), 1);
    EXPECT_EQ(count(0x5c5), 20);
    // The 140 of 0x52e740 (twenty rounds of two rand() and the five of the 0x5c5/0) and the rest of the step.
    EXPECT_GE(DrawsSince(Seed, tested.random), CountingStep + SkillRead + 20 * (2 + 5));
}

// What cuts, with the rand() before it: without the joint pool, those with +0x380 at 2 (0x505169). Skill 0x0c (the ray
// 0x5e8, 0x500c64) and +0x376's case 0x31 (0x4f67ad) do not cut (AttackSkillCases.cpp, MonsterAttackStep.cpp). The
// arrow (0x53e2b0) is in test_arrow_effects.cpp, the other cases of 0x4f97b0 in test_attack_frame_skills.cpp and those
// of 0x4fdeeb and 0x504372 in test_attack_skill_cases.cpp.
TEST(AttackStepTest, TheCutsAfterTheirDraws)
{
    struct Case
    {
        std::string_view name;
        void (*prepare)(AttackWorld&, ActorState&);
        std::string_view cut;
        int draws;
    };
    const std::array cases{
        Case{"mató, sin los joints",
             [](AttackWorld& tested, ActorState& hero)
             {
                 tested.withJoints = false;
                 hero.byte380 = 2;
                 hero.byte381 = 14;
             },
             "0x505169", CountingStep + 2 * SkillRead},
    };
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(probe.name);
        AttackWorld tested;
        ActorState& hero = tested.Striker(HeroSlot, 0x23, VictimSlot);
        probe.prepare(tested, hero);

        const auto stepped = tested.Attack(hero, HeroSlot);

        EXPECT_TRUE(HasError(stepped, probe.cut)) << ErrorOf(stepped);
        EXPECT_EQ(DrawsSince(Seed, tested.random), probe.draws);
    }
}

// 0x4f8f5b: with skill 3 and no case, the two joints 0x614/0 of hand 0 (0x4f8fef of width 50 and 0x4f9012 of width
// 10, both hanging from the target) and the particle 0x5ca/0 (0x4f9034) with its two rand().
TEST(AttackStepTest, TheSkillThreeTailCreatesItsTwoLightningJoints)
{
    AttackWorld tested;
    ActorState& hero = tested.Striker(HeroSlot, 0x23, VictimSlot);
    hero.byte38e = 3;

    const auto stepped = tested.Attack(hero, HeroSlot);

    ASSERT_TRUE(stepped.HasValue()) << ErrorOf(stepped);
    EXPECT_EQ(DrawsSince(Seed, tested.random), CountingStep + 2);
    ASSERT_TRUE(tested.joints[0].alive);
    ASSERT_TRUE(tested.joints[1].alive);
    EXPECT_FALSE(tested.joints[2].alive);
    for (const JointState& joint : {tested.joints[0], tested.joints[1]})
    {
        EXPECT_EQ(joint.type, 0x614);
        EXPECT_EQ(joint.subtype, 0);
        EXPECT_EQ(joint.ownerContext, &tested.Victim());
        EXPECT_EQ(joint.angles, glm::vec3(-60.0f, 0.0f, hero.angles.z));
        EXPECT_EQ(joint.position, tested.joints[0].position);
    }
    EXPECT_EQ(tested.joints[0].width, 50.0f);
    EXPECT_EQ(tested.joints[1].width, 10.0f);
    const auto particles = tested.particles.All();
    EXPECT_EQ(std::ranges::count_if(particles, [](const Client::Particles::Particle& particle)
                                    { return particle.alive && particle.type == Client::Particles::ThunderParticle; }),
              1);
}

// The local light of 0x4fd5f0 reaches the cases: 0x07 creates its 0xc1/0 over the target with it (0x50474e), the one
// from Skills::BodyLight or the one the body passes with the particle 0x5ac (0x4fdb3f).
TEST(AttackStepTest, TheSkillCasesTakeTheBodyLight)
{
    for (const glm::vec3& light : {Client::Skills::BodyLight, glm::vec3(0.3f, 0.5f, 1.0f)})
    {
        AttackWorld tested;
        ActorState& hero = tested.Striker(HeroSlot, 0x23, VictimSlot);
        hero.byte38e = 7;
        hero.byte381 = 14;

        const auto stepped = StepTheAttack(hero, HeroSlot, tested.Context(), light);

        ASSERT_TRUE(stepped.HasValue()) << ErrorOf(stepped);
        const auto ice = std::ranges::find_if(tested.world.Map().effects.Effects(), [](const auto& effect)
                                              { return effect.alive && effect.type == Client::Effects::IceEffect; });
        ASSERT_NE(ice, tested.world.Map().effects.Effects().end());
        EXPECT_EQ(ice->light, light);
        EXPECT_EQ(ice->transform.position, tested.Victim().position);
    }
}

// 0x558eb0: the chosen one's key only with the hero striking. −1 with another, with an item on the cursor or without a
// chosen one; PK 6 or more, Ctrl with another and +0x370 at 2 give the key; Chaos Castle, the hero's too; the duel,
// only the rival's; the guild war with PK 6 cuts.

TEST(AttackStepTest, TheChosenKeyIsTheHerosOnly)
{
    struct Case
    {
        std::string_view name;
        void (*prepare)(AttackWorld&);
        std::size_t attacker;
        std::int32_t key;
    };
    constexpr std::int32_t Key = 0x1234;
    const std::array cases{
        Case{"sin elegido", [](AttackWorld&) {}, HeroSlot, -1},
        Case{"un PK de 6", [](AttackWorld& tested) { tested.Victim().pk = 6; }, HeroSlot, Key},
        Case{"un PK de 6 con otro que pega", [](AttackWorld& tested) { tested.Victim().pk = 6; }, OtherSlot, -1},
        Case{"un PK de 3", [](AttackWorld&) {}, HeroSlot, -1},
        Case{"Ctrl", [](AttackWorld& tested) { tested.controlDown = true; }, HeroSlot, Key},
        Case{"+0x370 en 2", [](AttackWorld& tested) { tested.Victim().byte370 = 2; }, HeroSlot, Key},
        Case{"el cursor",
             [](AttackWorld& tested)
             {
                 tested.Victim().pk = 6;
                 tested.world.Runtime().cursorItemCount = 1;
             },
             HeroSlot, -1},
        Case{"Chaos Castle", [](AttackWorld& tested) { tested.world.Runtime().map = 18; }, HeroSlot, Key},
        Case{"el duelo con otro",
             [](AttackWorld& tested)
             {
                 tested.Victim().pk = 6;
                 tested.world.Runtime().social.duel.active = 1;
             },
             HeroSlot, -1},
        Case{"el duelo con el rival",
             [](AttackWorld& tested)
             {
                 tested.world.Runtime().social.duel.active = 1;
                 tested.world.Runtime().social.duel.rivalKey = 0x1234;
             },
             HeroSlot, Key},
    };
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(probe.name);
        AttackWorld tested;
        tested.Victim().key = Key;
        if (probe.name != "sin elegido")
            tested.world.Runtime().selectedTargets[2] = static_cast<std::int32_t>(VictimSlot);
        probe.prepare(tested);

        const auto key = ChosenKeyOf(tested.world, probe.attacker, tested.controlDown);

        ASSERT_TRUE(key.HasValue()) << key.Error();
        EXPECT_EQ(key.Value(), probe.key);
    }

    AttackWorld guild;
    guild.world.Runtime().selectedTargets[2] = static_cast<std::int32_t>(VictimSlot);
    guild.world.Runtime().guild.warOn = 1;
    guild.Victim().pk = 6;
    guild.Victim().word1f2 = 2;
    guild.Victim().key = Key;
    guild.Victim().byte374 = 2;
    // 0x558f29: the guild names (0x787aa55) of the chosen one and the hero equal (both empty), −1 even with the mark;
    // different, it goes on to the mark (+0x374 at 2) with the key.
    for (const bool controlDown : {false, true})
    {
        const auto discarded = ChosenKeyOf(guild.world, HeroSlot, controlDown);
        ASSERT_TRUE(discarded.HasValue()) << discarded.Error();
        EXPECT_EQ(discarded.Value(), -1);
    }
    guild.world.Runtime().guild.table[2].guildName[0] = 'R';
    const auto marked = ChosenKeyOf(guild.world, HeroSlot, false);
    ASSERT_TRUE(marked.HasValue()) << marked.Error();
    EXPECT_EQ(marked.Value(), Key);

    // In the skill step, the key goes to o+0x8a.
    AttackWorld stepped;
    stepped.world.Runtime().selectedTargets[2] = static_cast<std::int32_t>(VictimSlot);
    stepped.Victim().key = Key;
    stepped.Victim().pk = 6;
    ActorState& hero = stepped.Striker(HeroSlot, 0x23, VictimSlot);
    hero.byte381 = 14;
    ASSERT_TRUE(stepped.Attack(hero, HeroSlot).HasValue());
    EXPECT_EQ(hero.word8a, Key);
}

// 0x5040b9..0x5041a4: with the last 15's damage on the target (+0x384) and a type other than 0x159, ten times three
// rand() & 63 (the point: the target's x and y − 32, its z + 0x5a) and the blood 0x5e4 (0x53fe70, with four rand() and
// the striker's angles); then the skill goes on up to the sound. Without the pool, it cuts after the first point; with
// the pool full, only the three rand() of each point. The 0x159 does not bleed.

TEST(AttackStepTest, TheTargetWithDamageBleedsTenTimes)
{
    constexpr int Bleeding = 10 * (3 + 4);
    const glm::vec3 victimAt{12800.0f, 6400.0f, 150.0f};
    const auto prepare = [&victimAt](AttackWorld& tested)
    {
        tested.Victim().alive = true;
        tested.Victim().damage = 12;
        tested.Victim().position = victimAt;
        ActorState& hero = tested.Striker(HeroSlot, 0x23, VictimSlot);
        hero.byte381 = 14;
        hero.angles = {0.0f, 0.0f, 135.0f};
        return &hero;
    };
    {
        AttackWorld tested;
        ActorState* hero = prepare(tested);
        const auto stepped = tested.Attack(*hero, HeroSlot);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(DrawsSince(Seed, tested.random), CountingStep + SkillWithTarget + Bleeding);
        EXPECT_EQ(tested.sounds.size(), 1u);
        const auto blood = AliveOf(tested.particles, Client::Particles::BloodParticle);
        ASSERT_EQ(blood.size(), 10u);
        CrtRandom expected(Seed);
        for (int draw = 0; draw < CountingStep + SkillRead; ++draw)
            static_cast<void>(expected.Next());
        for (const Client::Particles::Particle* drop : blood)
        {
            const int x = (expected.Next() & 63) - 32;
            const int y = (expected.Next() & 63) - 32;
            const int z = (expected.Next() & 63) + 0x5a;
            EXPECT_EQ(drop->start, (glm::vec3{victimAt.x + static_cast<float>(x), victimAt.y + static_cast<float>(y),
                                              victimAt.z + static_cast<float>(z)}));
            EXPECT_EQ(drop->angles.y, 0.0f);
            EXPECT_EQ(drop->angles.z, 135.0f);
            EXPECT_EQ(drop->life, 12);
            for (int draw = 0; draw < 4; ++draw)
                static_cast<void>(expected.Next());
        }
    }
    {
        AttackWorld tested;
        tested.withParticles = false;
        ActorState* hero = prepare(tested);
        const auto stepped = tested.Attack(*hero, HeroSlot);
        EXPECT_TRUE(HasError(stepped, "0x50419c")) << ErrorOf(stepped);
        EXPECT_EQ(DrawsSince(Seed, tested.random), CountingStep + SkillRead + 3);
    }
    {
        AttackWorld tested;
        for (Client::Particles::Particle& particle : tested.particles.All())
            particle.alive = true;
        ActorState* hero = prepare(tested);
        ASSERT_TRUE(tested.Attack(*hero, HeroSlot).HasValue());
        EXPECT_EQ(DrawsSince(Seed, tested.random), CountingStep + SkillWithTarget + 10 * 3);
    }
    {
        AttackWorld tested;
        ActorState* hero = prepare(tested);
        tested.Victim().type = 0x159;
        ASSERT_TRUE(tested.Attack(*hero, HeroSlot).HasValue());
        EXPECT_EQ(DrawsSince(Seed, tested.random), CountingStep + SkillWithTarget);
        EXPECT_TRUE(AliveOf(tested.particles, Client::Particles::BloodParticle).empty());
    }
}

// 0x5041bf..0x50426f: the target 0x18e (Blood Castle's statues) with damage, after the blood, throws rand() & 1 five
// times; with 0, rand() % 30 for the z (z + 50 + the rand()) and the stone 0x10b (0x51bafb, nine rand(): subtype 0
// becomes 1), with the striker's angles and light and ownerless. Then sound 0x88 and the rest. In Lorencia, Devias and
// Blood Castle 1 (11). Each stone steps until it dies or until the cut of 0x5252e6.

TEST(AttackStepTest, TheStatueWithDamageThrowsStones)
{
    constexpr int Bleeding = 10 * (3 + 4);
    constexpr int Flips = 5;
    constexpr int StoneDraws = 1 + 9;
    const glm::vec3 victimAt{12800.0f, 6400.0f, 150.0f};
    int cuts = 0;
    int steps = 0;
    for (const std::int32_t map : {0, 2, 11})
    {
        SCOPED_TRACE(map);
        AttackWorld tested;
        tested.world.Runtime().map = map;
        tested.Victim().alive = true;
        tested.Victim().type = 0x18e;
        tested.Victim().damage = 12;
        tested.Victim().position = victimAt;
        ActorState& hero = tested.Striker(HeroSlot, 0x23, VictimSlot);
        hero.byte381 = 14;
        hero.angles = {0.0f, 0.0f, 135.0f};
        hero.light = {0.25f, 0.5f, 0.75f};

        const auto stepped = tested.Attack(hero, HeroSlot);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        CrtRandom expected(Seed);
        expected.Discard(CountingStep + SkillRead + Bleeding);
        std::vector<int> rises;
        for (int flip = 0; flip < Flips; ++flip)
        {
            if ((expected.Next() & 1) != 0)
                continue;
            rises.push_back(expected.Next() % 30);
            expected.Discard(9);
        }
        ASSERT_FALSE(rises.empty());
        EXPECT_EQ(DrawsSince(Seed, tested.random),
                  CountingStep + SkillWithTarget + Bleeding + Flips + static_cast<int>(rises.size()) * StoneDraws);
        EXPECT_NE(std::ranges::find(tested.sounds, 0x88), tested.sounds.end());
        std::vector<Client::Effects::ModelEffect*> stones;
        for (Client::Effects::ModelEffect& effect : tested.world.Map().effects.Effects())
        {
            if (effect.alive && effect.type == Client::Effects::DebrisEffect)
                stones.push_back(&effect);
        }
        ASSERT_EQ(stones.size(), rises.size());
        for (std::size_t index = 0; index < stones.size(); ++index)
        {
            const Client::Effects::ModelEffect& stone = *stones[index];
            const float lifted = static_cast<float>(static_cast<double>(victimAt.z) + 50.0);
            const float z = static_cast<float>(static_cast<double>(rises[index]) + static_cast<double>(lifted));
            EXPECT_EQ(stone.transform.position, (glm::vec3{victimAt.x, victimAt.y, z + 50.0f}));
            EXPECT_EQ(stone.subtype, 1);
            EXPECT_EQ(stone.light, hero.light);
            EXPECT_EQ(stone.ownerContext, nullptr);
            EXPECT_EQ(stone.disturbanceState, 0xffff);
            EXPECT_GE(stone.life, 32);
            EXPECT_LE(stone.life, 47);
        }
        // The steps of each stone (0x5208a0 → 0x52531d), two rand() each: the local light and 0x5252e6's. With rand() %
        // 10 at 0 it cuts there (the dust in an unwritten local, Q-C1176); otherwise the tail subtracts the life. With
        // the same seed the three stones would take the same first step: each map advances the CRT `map` places to go
        // through both branches.
        Client::Effects::ModelEffectStepServices services;
        services.terrainHeight = [](float, float) { return 0.0; };
        tested.random.Discard(static_cast<std::uint32_t>(map));
        for (Client::Effects::ModelEffect* stone : stones)
        {
            while (stone->alive)
            {
                CrtRandom probe = tested.random;
                static_cast<void>(probe.Next());
                const bool dusty = probe.Next() % 10 == 0;
                const std::int32_t life = stone->life;
                const CrtRandom before = tested.random;
                const auto moved = Client::Effects::StepGameModelEffect(*stone, nullptr, tested.particles, 0.0f,
                                                                        tested.random, nullptr, services);
                EXPECT_EQ(DrawsSince(before.State(), tested.random), 2);
                if (dusty)
                {
                    EXPECT_TRUE(HasError(moved, "0x5252e6")) << ErrorOf(moved);
                    ++cuts;
                    break;
                }
                ASSERT_TRUE(moved.HasValue()) << moved.Error();
                EXPECT_LT(stone->life, life);
                ++steps;
            }
        }
    }
    EXPECT_GT(cuts, 0);
    EXPECT_GT(steps, 0);
}

// 0x4fb08c..0x4fb0da: with the 0x815 in the own record's left hand, frame 5 of anyone striking asks for twenty sparks
// 0x5c6/12 over them, with their light (+0xf4), scale 2 and six rand() each, and the attack goes on up to the sound.
// Without the pool it cuts after the record; with the pool full it draws nothing more.

TEST(AttackStepTest, TheBowInTheOwnLeftHandSparksTwentyTimes)
{
    constexpr int Sparks = 20;
    constexpr int SparkDraws = 6;
    // After 0x4f97b0, the three of 0x4f5b70 and, with +0x381 at 16, the skill with a target.
    constexpr int AfterTheFrames = CountingStep - SkillRead + SkillWithTarget;
    const auto prepare = [](AttackWorld& tested)
    {
        tested.world.OwnCharacter().equipment[1].id = 0x815;
        ActorState& hero = tested.Striker(HeroSlot, 0x23, VictimSlot);
        hero.animation.frame = 5.0f;
        hero.position = {1000.0f, 2000.0f, 50.0f};
        hero.angles = {0.0f, 0.0f, 90.0f};
        hero.light = {0.5f, 0.25f, 1.0f};
        return &hero;
    };
    {
        AttackWorld tested;
        ActorState* hero = prepare(tested);
        ASSERT_TRUE(tested.Attack(*hero, HeroSlot).HasValue());
        EXPECT_EQ(DrawsSince(Seed, tested.random), SkillRead + OwnRecord + Sparks * SparkDraws + AfterTheFrames);
        EXPECT_EQ(hero->byte381, 0);
        int found = 0;
        for (const Client::Particles::Particle& spark : tested.particles.All())
        {
            if (!spark.alive)
                continue;
            ++found;
            EXPECT_EQ(spark.type, Client::Particles::SparkParticle);
            EXPECT_EQ(spark.subtype, Client::Particles::BowSparkSubtype);
            EXPECT_EQ(spark.texture, 0x5fc);
            EXPECT_EQ(spark.life, 40);
            EXPECT_EQ(spark.angles, hero->angles);
            EXPECT_EQ(spark.light, hero->light);
        }
        EXPECT_EQ(found, Sparks);
    }
    {
        AttackWorld tested;
        tested.withParticles = false;
        ActorState* hero = prepare(tested);
        EXPECT_TRUE(HasError(tested.Attack(*hero, HeroSlot), "0x4fb0d1"));
        EXPECT_EQ(DrawsSince(Seed, tested.random), SkillRead + OwnRecord);
    }
    {
        AttackWorld tested;
        for (Client::Particles::Particle& particle : tested.particles.All())
            particle.alive = true;
        ActorState* hero = prepare(tested);
        ASSERT_TRUE(tested.Attack(*hero, HeroSlot).HasValue());
        EXPECT_EQ(DrawsSince(Seed, tested.random), SkillRead + OwnRecord + AfterTheFrames);
    }
}

// 0x505069..0x5051a4: with +0x380 at 2 (the hero's 16: it killed), the skill launches the 0x618/0 and the 0x618/1 from
// the target, with the striker as owner, width 20 and the striker's angles, without rand(), and goes on up to the
// sound.

TEST(AttackStepTest, TheKillerLaunchesTwoBeamsFromTheTarget)
{
    AttackWorld tested;
    tested.Victim().alive = true;
    tested.Victim().position = {3000.0f, 4000.0f, 100.0f};
    ActorState& hero = tested.Striker(HeroSlot, 0x23, VictimSlot);
    hero.byte380 = 2;
    hero.byte381 = 14;
    hero.angles = {0.0f, 0.0f, 45.0f};

    ASSERT_TRUE(tested.Attack(hero, HeroSlot).HasValue());

    EXPECT_EQ(DrawsSince(Seed, tested.random), CountingStep + SkillWithTarget);
    EXPECT_EQ(hero.byte380, 0);
    EXPECT_EQ(tested.sounds.size(), 1u);
    for (const std::int32_t subtype : {0, 1})
    {
        SCOPED_TRACE(subtype);
        const JointState& beam = tested.joints[static_cast<std::size_t>(subtype)];
        EXPECT_TRUE(beam.alive);
        EXPECT_EQ(beam.type, Client::Joints::KillBeamJoint);
        EXPECT_EQ(beam.subtype, subtype);
        EXPECT_EQ(beam.ownerContext, &hero);
        EXPECT_EQ(beam.position, tested.Victim().position);
        EXPECT_EQ(beam.angles, hero.angles);
        EXPECT_EQ(beam.width, 20.0f);
        EXPECT_EQ(beam.speed, 3.0f);
        EXPECT_EQ(beam.life, 120);
        EXPECT_EQ(beam.maximumCount, 8);
        EXPECT_EQ(beam.key, -1);
    }
    EXPECT_FALSE(tested.joints[2].alive);
}

// Each class's melee attack, by the hero (at a pool target) and by another player (at the hero, as the 18 leaves it),
// through the whole character step (0x508f00) and the particle step (0x5446e0): twenty-two steps without a cut, with
// the action at 0.5 keys per step (frame 5 on the tenth). What it draws beyond the same world without +0x381: 4 rand()
// in each of the first nine steps and, on the tenth, 0x4f97b0's own record, 0x4f5b70's reads and the skill with the
// sound. Then +0x381 at 0. With the last 15's damage on the target, also the ten blood 0x5e4 with their rand(), which
// live their 12 steps. The two rand() of the head turn (0x5068fd, one in 32 per step for those that are not the hero)
// depend on the seed: those of each target that changed are discounted.

TEST(AttackStepTest, AMeleeAttackOfEachClassRunsItsSteps)
{
    struct Case
    {
        std::string_view name;
        std::uint8_t characterClass;
        std::uint8_t action;
        std::uint16_t right;
        std::uint16_t left;
    };
    constexpr int Steps = 22;
    constexpr int AttackDraws = 9 * CountingStep + StrikingStep + SkillWithTarget;
    constexpr int HeadSortDraws = 2;
    constexpr int Bleeding = 10 * (3 + 4);
    const std::array cases{Case{"el mago con el bastón", 0, 0x2b, 0xa00, 0xffff},
                           Case{"el caballero con la espada", 1, 0x23, 0x000, 0xffff},
                           Case{"el elfo sin armas", 2, 0x22, 0xffff, 0xffff},
                           Case{"el gladiador con dos espadas", 3, 0x25, 0x000, 0x001},
                           Case{"el Dark Lord con el cetro", 4, 0x24, 0x400, 0xffff},
                           Case{"el Dark Lord en Fenrir", 4, 0x59, 0x400, 0xffff}};
    for (const auto& [heroStrikes, hurt] :
         {std::pair{true, false}, std::pair{false, false}, std::pair{true, true}, std::pair{false, true}})
    {
        for (const Case& probe : cases)
        {
            SCOPED_TRACE(::testing::Message()
                         << probe.name << (heroStrikes ? ", el héroe" : ", otro") << (hurt ? ", con daño" : ""));
            const auto run = [&](bool attacking, int& draws)
            {
                AttackWorld tested;
                tested.skeleton.actions[probe.action] =
                    ActionInfo{.repeats = false, .speed = 0.5f, .keyCount = 30, .lockPositions = false};
                auto& equipment = tested.world.OwnCharacter().equipment;
                equipment[0].id = probe.right;
                equipment[1].id = probe.left;
                tested.Hero().characterClass = probe.characterClass;
                const std::size_t slot = heroStrikes ? HeroSlot : OtherSlot;
                ActorState& striker = tested.Striker(slot, probe.action, heroStrikes ? VictimSlot : HeroSlot);
                striker.characterClass = probe.characterClass;
                if (!attacking)
                    striker.byte381 = 0;
                if (hurt)
                    tested.world.Actors()[heroStrikes ? VictimSlot : HeroSlot].damage = 12;
                Client::Particles::PortableParticleTrigonometry trigonometry;
                const float clock = 0.0f;
                const std::int32_t map = 0;
                const auto heads = [&tested]
                {
                    std::vector<glm::vec2> targets;
                    for (const ActorState& actor : tested.world.Actors())
                        targets.push_back(actor.headTurn.target);
                    return targets;
                };
                int headDraws = 0;
                for (int step = 0; step < Steps; ++step)
                {
                    const std::vector<glm::vec2> before = heads();
                    const auto stepped = StepCharacters(tested.Context());
                    if (!stepped.HasValue())
                        return stepped;
                    const std::vector<glm::vec2> after = heads();
                    for (std::size_t k = 0; k < before.size(); ++k)
                    {
                        if (before[k] != after[k])
                            headDraws += HeadSortDraws;
                    }
                    if (!Client::Particles::StepParticlesChecked(tested.particles, tested.random, std::cref(clock),
                                                                 &map, trigonometry)
                             .HasValue())
                        return Client::Core::Expected<void, std::string>(
                            Client::Core::Unexpected(std::string("0x5446e0: el paso de las partículas")));
                }
                EXPECT_EQ(striker.byte381, 0);
                EXPECT_EQ(tested.sounds.size(), attacking ? 1u : 0u);
                const auto created = std::ranges::count(tested.particles.All(), Client::Particles::BloodParticle,
                                                        &Client::Particles::Particle::type);
                EXPECT_EQ(created, attacking && hurt ? 10 : 0);
                EXPECT_TRUE(AliveOf(tested.particles, Client::Particles::BloodParticle).empty());
                draws = DrawsSince(Seed, tested.random) - headDraws;
                return Client::Core::Expected<void, std::string>{};
            };
            int attackDraws = 0;
            int quietDraws = 0;
            const auto attacked = run(true, attackDraws);
            const auto quiet = run(false, quietDraws);

            ASSERT_TRUE(attacked.HasValue()) << attacked.Error();
            ASSERT_TRUE(quiet.HasValue()) << quiet.Error();
            EXPECT_EQ(attackDraws - quietDraws, AttackDraws + (hurt ? Bleeding : 0));
        }
    }
}

// One death per class, in Lorencia and Devias: the hero with +0x380 at 2 strikes a target with damage and the whole
// character step (0x508f00), the joint step (0x535840) and the particle step (0x5446e0) run without a cut until the
// two 0x618 return to the hero with their sound 0x23 (besides the strike's) and go out, with their 0x5d5 and the blood.

TEST(AttackStepTest, AKillOfEachClassSendsTheBeamsBackWithoutACut)
{
    constexpr int Steps = 120;
    constexpr std::int32_t HitSound = 0x23;
    const std::array cases{std::pair{0, 0x2b}, std::pair{1, 0x23}, std::pair{2, 0x22}, std::pair{3, 0x25},
                           std::pair{4, 0x24}};
    for (const std::int32_t map : {0, 2})
    {
        for (const auto& [characterClass, action] : cases)
        {
            SCOPED_TRACE(::testing::Message() << "mapa " << map << ", clase " << characterClass);
            AttackWorld tested;
            tested.world.Runtime().map = map;
            tested.skeleton.actions[static_cast<std::size_t>(action)] =
                ActionInfo{.repeats = false, .speed = 0.5f, .keyCount = 30, .lockPositions = false};
            tested.Hero().characterClass = static_cast<std::uint8_t>(characterClass);
            ActorState& hero = tested.Striker(HeroSlot, static_cast<std::uint8_t>(action), VictimSlot);
            hero.byte380 = 2;
            tested.Victim().alive = true;
            tested.Victim().position = {hero.position.x + 300.0f, hero.position.y, hero.position.z};
            tested.Victim().damage = 12;
            Client::Particles::PortableParticleTrigonometry trigonometry;
            auto lighting = std::make_unique<Client::Terrain::TerrainLighting>();
            const float clock = 0.0f;
            const auto sound = [&tested](Client::Audio::SoundId played) { tested.sounds.push_back(played.Value()); };
            int step = 0;
            for (; step < Steps; ++step)
            {
                SCOPED_TRACE(step);
                const auto stepped = StepCharacters(tested.Context());
                ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
                ASSERT_TRUE(Client::Joints::StepJoints(tested.joints, tested.random, nullptr, &tested.particles, clock,
                                                       nullptr, lighting.get(), sound)
                                .HasValue());
                ASSERT_TRUE(Client::Particles::StepParticlesChecked(tested.particles, tested.random, std::cref(clock),
                                                                    &map, trigonometry, {}, {}, {}, {}, lighting.get())
                                .HasValue());
                if (step > 10 && !tested.particles.Any() && std::ranges::none_of(tested.joints, &JointState::alive))
                    break;
            }
            EXPECT_LT(step, Steps);
            EXPECT_EQ(std::ranges::count(tested.sounds, HitSound), 2);
            EXPECT_EQ(hero.byte380, 0);
        }
    }
}

// The elf with the bow (0x2e, the 4/0 in the own record's left hand) or the crossbow (0x2f, the 4/8 in the right) and
// skill 0x18 in slot 3 of +0x6c shoots in the frame 5 step (0x504060): 0x59fb80 gives the slot, 0x53e2b0 creates the
// arrow with the slot in +0x89 and leaves the skill in [0x5767948]; then the 0x18 case of the target skills (0x504379)
// shoots the volley of three and goes on into 0x34's, which shoots one more: five arrows, without a cut.

TEST(AttackStepTest, TheElfBowShootsAndThenTheSkillVolley)
{
    struct Case
    {
        std::uint8_t action;
        std::uint16_t right;
        std::uint16_t left;
        std::int16_t arrow;
    };
    for (const Case& probe : {Case{0x2e, 0xffff, 0x800, 0xdb}, Case{0x2f, 0x808, 0xffff, 0xdc}})
    {
        SCOPED_TRACE(probe.action);
        AttackWorld tested;
        tested.skeleton.actions[probe.action] =
            ActionInfo{.repeats = false, .speed = 0.5f, .keyCount = 30, .lockPositions = false};
        tested.Hero().characterClass = 2;
        tested.world.OwnCharacter().words6c[0] = 0x18000000u; // byte 3 of +0x6c
        tested.world.OwnCharacter().equipment[0].id = probe.right;
        tested.world.OwnCharacter().equipment[1].id = probe.left;
        ActorState& hero = tested.Striker(HeroSlot, probe.action, VictimSlot);
        hero.byte38e = 0x18;
        for (int step = 1; step <= 10; ++step)
        {
            SCOPED_TRACE(step);
            const auto stepped = StepCharacters(tested.Context());
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        }

        EXPECT_EQ(hero.byte381, 0);
        EXPECT_EQ(tested.world.Runtime().lastShotSkill, 0x18u);
        const auto& effects = tested.world.Map().effects.Effects();
        EXPECT_EQ(std::ranges::count_if(effects, [](const auto& effect) { return effect.alive; }), 5);
        for (const auto& arrow : effects)
        {
            if (!arrow.alive)
                continue;
            EXPECT_EQ(arrow.type, probe.arrow);
            EXPECT_EQ(arrow.byte89, 3);
            EXPECT_EQ(arrow.ownerContext, &hero);
        }
    }
}

// 0x4fd236..0x4fd5bb: during action 0x23, a player's whole body with the sword 0x222 in the right hand makes the orange
// level-13 ribbon and goes on through 0x4fd5f0 without a cut. The ribbon ages at the end of each step.
TEST(AttackStepTest, AVisibleMeleeWeaponSurvivesTheFullAttackBodyPipeline)
{
    constexpr int Steps = 4;
    constexpr std::uint8_t KnightClass = 1;
    constexpr std::uint8_t SwordAttack = 0x23;
    constexpr std::uint16_t SwordModel = 0x222;
    constexpr std::uint8_t SwordLevel = 13;
    constexpr std::size_t SwordBone = 33;

    AttackWorld tested;
    tested.skeleton.actions[SwordAttack] = ActionInfo{.repeats = true, .speed = 0.0f, .keyCount = 10};
    Client::Models::SkeletonBone swordBone;
    swordBone.actions.resize(tested.skeleton.actions.size());
    for (auto& keys : swordBone.actions)
        keys.resize(10);
    tested.skeleton.bones.assign(SwordBone + 1, swordBone);

    tested.Hero().characterClass = KnightClass;
    tested.Hero().position = {100.0f, 200.0f, 300.0f};
    ActorState& hero = tested.Striker(HeroSlot, SwordAttack, VictimSlot);
    hero.animation.frame = 5.0f;
    hero.scale = 0.9f;
    hero.hands[0].model = SwordModel;
    hero.hands[0].level = SwordLevel;
    hero.hands[0].bone = SwordBone;
    tested.Victim().alive = true;
    tested.Victim().position = {hero.position.x + 300.0f, hero.position.y, hero.position.z};

    for (int step = 0; step < Steps; ++step)
    {
        SCOPED_TRACE(step);
        const auto stepped = StepCharacters(tested.Context());

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    }

    const auto& ribbons = tested.world.DrawTail().ribbons;
    const auto ribbon = std::ranges::find_if(ribbons, [](const auto& candidate) { return candidate.alive; });
    ASSERT_NE(ribbon, ribbons.end());
    EXPECT_EQ(ribbon->ownerContext, &hero);
    EXPECT_EQ(ribbon->ownerEquipment, &hero.level);
    EXPECT_EQ(ribbon->type, 0);
    // 0x52e454 saturates at 29 and 0x5091d0, at the end of each character step, removes one: 10−1, 19−1, 28−1 and 29−1.
    EXPECT_EQ(ribbon->count, 28);
    EXPECT_EQ(ribbon->life, 15 - Steps); // the short ribbon is born with life 15 and ages once per step
    EXPECT_EQ(ribbon->light, (glm::vec3{1.0f, 0.6f, 0.2f}));
}

// Composition 359/H4: action 0x38 reaches SparkTheTarget with P full and goes on until it makes the trail.
TEST(AttackStepTest, FullSparkParticlesDoNotPreventTheVisibleSpearTrail)
{
    constexpr std::uint8_t SpearAttack = 0x38;
    constexpr std::uint16_t SpearModel = 0x415;
    constexpr std::size_t HandBone = 33;
    std::uint32_t withoutTrail = 0;
    for (const bool visible : {false, true})
    {
        SCOPED_TRACE(visible);
        AttackWorld tested;
        tested.skeleton.actions[SpearAttack] = ActionInfo{.repeats = true, .speed = 0.0f, .keyCount = 10};
        Client::Models::SkeletonBone bone;
        bone.actions.resize(tested.skeleton.actions.size());
        for (auto& keys : bone.actions)
            keys.resize(10);
        tested.skeleton.bones.assign(HandBone + 1, bone);
        auto& hero = tested.Striker(HeroSlot, SpearAttack, VictimSlot);
        hero.animation.frame = 5.0f;
        hero.byte381 = 14;
        hero.hands[0].model = visible ? SpearModel : Client::Kernel::NoItemModel;
        hero.hands[0].level = 13;
        hero.hands[0].bone = HandBone;
        tested.Victim().alive = true;
        for (auto& particle : tested.particles.All())
            particle.alive = true;
        const auto stepped = StepCharacters(tested.Context());
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(hero.byte381, 0);
        const bool ribbon = std::ranges::any_of(tested.world.DrawTail().ribbons, [](const auto& r) { return r.alive; });
        EXPECT_EQ(ribbon, visible);
        if (visible)
            EXPECT_EQ(tested.random.State(), withoutTrail);
        else
            withoutTrail = tested.random.State();
    }
}
} // namespace
