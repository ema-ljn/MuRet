#include "Models/IModelLibrary.h"
#include "Skills/SkillReplies.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Tables/PlayerModels.h"
#include "Tables/SkillTable.h"
#include "Protocol/Skills/Skill.h"
#include "Models/Cloth.h"
#include "Models/LogicalAccess.h"
#include "Math/OriginalAngles.h"
#include "Models/Skeleton.h"
#include "Models/ModelId.h"
#include "Render/TextureSlot.h"
#include "Actors/ActorActions.h"
#include "Actors/ActorEffectFlags.h"
#include "Actors/ActorFlags.h"
#include "Kernel/ActorState.h"
#include "Effects/ActorEffectHooks.h"
#include "Combat/BlastEffectCalls.h"
#include "Kernel/DrawTailState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "FrontEnd/SelectionState.h"
#include "Session/CharacterDrawHooks.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldMap.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Session/WorldTransitionSupport.h"
#include "Camera/GameCamera.h"
#include "Effects/ModelEffects.h"
#include "Effects/WizardModelEffects.h"
#include "Session/ModuleStates.h"
#include "Audio/SoundId.h"

#include "support/PlayerActors.h"
#include "support/Bytes.h"
#include "support/Packets.h"
#include "support/RecordingSink.h"
#include "support/TableFiles.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

// The incoming skills (06 § Las skills que llegan), with invented keys and characters.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Protocol::Skills::ActorEffect;
using Client::Protocol::Skills::DurationMagic;
using Client::Protocol::Skills::MagicAttack;
using Client::Protocol::Skills::MagicCancel;
using Client::Protocol::Skills::SkillList;
using Client::Protocol::Skills::SkillListEntry;
using Client::Protocol::Skills::SkillTarget;
using Client::Protocol::Skills::SummonLife;

constexpr std::uint16_t HeroKey = 0x21;
constexpr std::uint16_t CasterKey = 0x1234;
constexpr std::uint16_t TargetKey = 0x0456;
constexpr std::size_t HeroIndex = 7;
constexpr std::size_t CasterIndex = 3;
constexpr std::size_t TargetIndex = 5;
constexpr std::uint16_t MonsterType = 0x1a;
// +0x38e and +0x42e: one rand() when they close at 0.
constexpr int ActorByteRandoms = 1;
constexpr int OwnRecordRandoms = 3848;
constexpr int SkillEntryRandoms = 60;

CrtRandom Advanced(CrtRandom random, int count)
{
    for (int i = 0; i < count; ++i)
        static_cast<void>(random.Next());
    return random;
}

Client::Tables::SkillTable SkillsHiding(std::initializer_list<std::uint8_t> hidden)
{
    std::vector<std::byte> plain(Client::Tables::SkillTableSize);
    for (const std::uint8_t skill : hidden)
        plain[std::size_t{skill} * Client::Tables::SkillRecordSize + 0x31] = std::byte{1};
    const std::vector<std::byte> encrypted =
        Client::Tests::EncryptRecords(std::move(plain), Client::Tables::SkillRecordSize);
    return Client::Tables::SkillTable(std::span<const std::byte>(encrypted));
}

class SkillModels final : public Client::Models::IModelLibrary
{
public:
    const Client::Data::Models::Model* Find(Client::Models::ModelId) const override
    {
        return nullptr;
    }
    const Client::Models::Skeleton* SkeletonOf(Client::Models::ModelId model) const override
    {
        return model.Value() == MonsterType || model.Value() == Client::Tables::PlayerSkeletonModel ? &skeleton
                                                                                                    : nullptr;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(Client::Models::ModelId) const override
    {
        return {};
    }
    Client::Models::Skeleton skeleton = Client::Tests::PlayerActionsSkeleton();
};

struct Replies
{
    Replies()
    {
        world.Runtime().hero = HeroIndex;
        Place(HeroIndex, HeroKey, Client::Tests::UnarmedPlayer());
        Place(CasterIndex, CasterKey, Client::Tests::UnarmedPlayer());
        Place(TargetIndex, TargetKey, Client::Tests::UnarmedPlayer());
        Actor(CasterIndex).position = {1000.0f, 1000.0f, 0.0f};
        Actor(TargetIndex).position = {1300.0f, 1200.0f, 0.0f};
        Actor(TargetIndex).seen = true;
    }
    void Place(std::size_t index, std::uint16_t key, ActorState actor)
    {
        actor.alive = true;
        actor.key = key;
        world.Actors()[index] = actor;
    }
    ActorState& Actor(std::size_t index)
    {
        return world.Actors()[index];
    }
    Client::Scenes::WorldEntryResources Resources()
    {
        Client::Scenes::WorldEntryResources resources{.world = world,
                                                      .selection = selection,
                                                      .dataRoot = {},
                                                      .cloth = cloth,
                                                      .camera = camera,
                                                      .cameraInput = input,
                                                      .models = models,
                                                      .log = log};
        resources.skills = skills;
        resources.states = &states;
        resources.jointHooks = &jointHooks;
        resources.effectHooks = &effectHooks;
        return resources;
    }
    Client::Core::Expected<void, std::string> Attack(std::uint8_t skill, std::uint16_t source = CasterKey,
                                                     bool succeeded = true)
    {
        return Client::Skills::ApplyMagicAttack(MagicAttack{skill, source, TargetKey, succeeded}, Resources());
    }
    std::size_t AliveEffects(std::int16_t type)
    {
        std::size_t count = 0;
        for (const auto& effect : world.Map().effects.Effects())
            count += effect.alive && effect.type == type ? 1u : 0u;
        return count;
    }

    Client::Kernel::WorldState world;
    Client::FrontEnd::SelectionState selection{world};
    Client::Camera::GameCamera camera;
    Client::Camera::GameCameraInput input;
    Client::Models::ClothWorld cloth;
    SkillModels models;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
    const Client::Tables::SkillTable* skills = nullptr;
    Client::Session::ModuleStates states;
    Client::Session::CharacterDrawHooks jointHooks;
    Client::Effects::ActorEffectHooks effectHooks;
};

// ---------------------------------------------------------------------------------------------------------------
// 0x19 (0x4cc530)

// On another: the heading toward the target, the target in +0x3a4, +0x38d, the skill in +0x38e (one rand()) and branch
// 0x13.
TEST(SkillRepliesTest, TheMagicAttackOfAnotherTurnsTowardsTheTargetAndActs)
{
    auto tested = std::make_unique<Replies>();
    const CrtRandom expected = Advanced(tested->cloth.random, ActorByteRandoms);
    ASSERT_TRUE(tested->Attack(0x13).HasValue());
    const ActorState& caster = tested->Actor(CasterIndex);
    EXPECT_EQ(caster.angles.z, static_cast<float>(Client::Math::CreateAngle(1000.0f, 1000.0f, 1300.0f, 1200.0f)));
    EXPECT_EQ(caster.attackTarget, TargetIndex);
    EXPECT_EQ(caster.skillSucceeded, 1);
    EXPECT_EQ(caster.byte38e, 0x13);
    EXPECT_EQ(caster.byte38eAccesses, 0);
    EXPECT_EQ(caster.animation.action, 0x38);
    EXPECT_EQ(caster.byte381, 1);
    EXPECT_EQ(tested->world.Runtime().lastSoundId, Client::Audio::SoundId{0x70});
    EXPECT_EQ(tested->world.Runtime().lastAttacker, static_cast<std::int32_t>(CasterIndex));
    EXPECT_EQ(tested->cloth.random.State(), expected.State());
}

// The hero does not turn, and with 0x10 it does not strike either: only +0x381.
TEST(SkillRepliesTest, TheMagicAttackOfTheHeroKeepsItsDirection)
{
    auto tested = std::make_unique<Replies>();
    ActorState& hero = tested->Actor(HeroIndex);
    hero.angles.z = 45.0f;
    const std::uint8_t action = hero.animation.action;
    ASSERT_TRUE(tested->Attack(0x10, HeroKey, false).HasValue());
    EXPECT_EQ(hero.angles.z, 45.0f);
    EXPECT_EQ(hero.skillSucceeded, 0);
    EXPECT_EQ(hero.byte38e, 0x10);
    EXPECT_EQ(hero.animation.action, action);
    EXPECT_EQ(hero.byte381, 1);
    EXPECT_EQ(tested->world.Runtime().lastAttacker, static_cast<std::int32_t>(HeroIndex));
}

// Without the target (or with its bit 15, which the reader already removed and which stays in the success), nothing.
TEST(SkillRepliesTest, TheMagicAttackWithoutTheTargetDoesNothing)
{
    auto tested = std::make_unique<Replies>();
    tested->world.Runtime().lastAttacker = -5;
    const auto applied =
        Client::Skills::ApplyMagicAttack(MagicAttack{0x13, CasterKey, 0x0999, true}, tested->Resources());
    ASSERT_TRUE(applied.HasValue());
    EXPECT_EQ(tested->Actor(CasterIndex).byte38e, 0);
    EXPECT_EQ(tested->world.Runtime().lastAttacker, -5);
    EXPECT_EQ(tested->cloth.random.State(), 1u);
}

// Q-C1237: a missing source is the 400 (0x4cce50). What comes before the branch goes there: 0x5767964, the heading
// toward the target, +0x3a4, +0x38d and the skill in +0x38e with its rand(); the branch, which in the original comes
// from whatever is in the 400 (its type, its mount), is discarded without cutting: no action, +0x381, sound, or
// anything it touches on the target. 0x3b has nothing before the branch. In Lorencia and Devias.
TEST(SkillRepliesTest, AnUnknownSourceIsTheLastActor)
{
    for (const std::int32_t map : {0, 2})
    {
        for (const std::uint8_t skill :
             {std::uint8_t{0x13}, std::uint8_t{0x10}, std::uint8_t{0x4b}, std::uint8_t{0x3b}})
        {
            SCOPED_TRACE(std::to_string(map) + " " + std::to_string(skill));
            auto tested = std::make_unique<Replies>();
            tested->world.Runtime().map = map;
            const bool head = skill != 0x3b;
            const CrtRandom expected = Advanced(tested->cloth.random, head ? ActorByteRandoms : 0);
            const std::uint8_t action = tested->Actor(400).animation.action;
            ASSERT_TRUE(tested->Attack(skill, 0x0777).HasValue());
            const ActorState& phantom = tested->Actor(400);
            EXPECT_EQ(tested->world.Runtime().lastAttacker, 400);
            EXPECT_EQ(phantom.byte38e, head ? skill : 0);
            EXPECT_EQ(phantom.attackTarget, head ? TargetIndex : 0);
            EXPECT_EQ(phantom.skillSucceeded, head ? 1 : 0);
            EXPECT_EQ(phantom.angles.z,
                      head ? static_cast<float>(Client::Math::CreateAngle(0.0f, 0.0f, 1300.0f, 1200.0f)) : 0.0f);
            EXPECT_EQ(phantom.animation.action, action);
            EXPECT_EQ(phantom.byte381, 0);
            EXPECT_EQ(tested->Actor(TargetIndex).flags78, 0u);
            EXPECT_FALSE(tested->world.Runtime().lastSoundId.has_value());
            EXPECT_EQ(tested->cloth.random.State(), expected.State());
        }
    }
}

// Another player's 0x10: 0x4f5290 opens the own stats (0x4f3f80) and draws the rand() of action 0x8e or 0x8f.
TEST(SkillRepliesTest, TheHitOfAnotherPlayerDrawsAfterTheSpeeds)
{
    auto tested = std::make_unique<Replies>();
    CrtRandom expected = Advanced(tested->cloth.random, ActorByteRandoms + OwnRecordRandoms);
    const int bit = expected.Next() & 1;
    ASSERT_TRUE(tested->Attack(0x10).HasValue());
    EXPECT_EQ(tested->Actor(CasterIndex).animation.action, 0x8e + bit);
    EXPECT_EQ(tested->Actor(CasterIndex).animation.frame, 0.0f);
    EXPECT_EQ(tested->cloth.random.State(), expected.State());
}

// On a monster, 3 and 4 in threes with +0x38f.
TEST(SkillRepliesTest, TheHitOfAMonsterAlternatesEveryThree)
{
    auto tested = std::make_unique<Replies>();
    ActorState& caster = tested->Actor(CasterIndex);
    caster.type = MonsterType;
    ASSERT_TRUE(tested->Attack(0x10).HasValue());
    EXPECT_EQ(caster.animation.action, 3);
    EXPECT_EQ(caster.attackCount, 1);
    ASSERT_TRUE(tested->Attack(0x10).HasValue());
    EXPECT_EQ(caster.animation.action, 4);
    EXPECT_EQ(caster.attackCount, 2);
}

// 0x4cd3e0 → 0x4cd68e: branches 1, 2, 4, 7, 0xb and 0x11 on a monster do 0x4f4a10's attack action (SetActorAttack: 3
// with +0x38f at 0, which rises twice), the frame at 0 and +0x381 at 1. The rand(): the close of +0x38e and the hit
// sound's without the model's table (P-C253). In Lorencia, Devias and Kalima (a monster not among those of 0x43a2f0).
TEST(SkillRepliesTest, TheStrikeOfAMonsterIsTheAttackAction)
{
    for (const std::int32_t map : {0, 2, 0x18})
    {
        for (const int skill : {1, 2, 4, 7, 0xb, 0x11})
        {
            SCOPED_TRACE(std::to_string(map) + " " + std::to_string(skill));
            auto tested = std::make_unique<Replies>();
            tested->world.Runtime().map = map;
            ActorState& caster = tested->Actor(CasterIndex);
            caster.type = MonsterType;
            caster.animation.frame = 2.0f;
            const CrtRandom expected = Advanced(tested->cloth.random, ActorByteRandoms + 1);
            ASSERT_TRUE(tested->Attack(static_cast<std::uint8_t>(skill)).HasValue());
            EXPECT_EQ(caster.byte38e, skill);
            EXPECT_EQ(caster.animation.action, 3);
            EXPECT_EQ(caster.attackCount, 2);
            EXPECT_EQ(caster.animation.frame, 0.0f);
            EXPECT_EQ(caster.byte381, 1);
            EXPECT_EQ(tested->cloth.random.State(), expected.State());
        }
    }
}

// 0x4cd316, 0x4cd34b and 0x4cd35b: a player's 0xc9, 0xca and 0xc8 strike with 0x4f4a10 (unarmed, 0x22, with
// 0x4f3f80's 3848 rand()) and return without the frame at 0 (already in 0x22, 0x4ea030 does not touch it); the first
// two set their flag and +0x381.
TEST(SkillRepliesTest, TheMonsterSkillsOfAPlayerAreTheAttackAction)
{
    for (const auto& [skill, flag, settled] :
         {std::tuple{0xc9, 0x100u, 1}, std::tuple{0xca, 0x8u, 1}, std::tuple{0xc8, 0u, 0}})
    {
        SCOPED_TRACE(skill);
        auto tested = std::make_unique<Replies>();
        ActorState& caster = tested->Actor(CasterIndex);
        caster.animation.action = 0x22;
        caster.animation.frame = 2.0f;
        const CrtRandom expected = Advanced(tested->cloth.random, ActorByteRandoms + OwnRecordRandoms);
        ASSERT_TRUE(tested->Attack(static_cast<std::uint8_t>(skill)).HasValue());
        EXPECT_EQ(caster.animation.action, 0x22);
        EXPECT_EQ(caster.attackCount, 1);
        EXPECT_EQ(caster.flags78, flag);
        EXPECT_EQ(caster.byte381, settled);
        EXPECT_EQ(caster.animation.frame, 2.0f);
        EXPECT_EQ(tested->cloth.random.State(), expected.State());
    }
}

// 0x4cd681 and 0x4cdd21: 0x37 without success and 0x3f on a monster end in 0x4f4a10 (3, which overwrites 0x3f's 0x4d),
// the frame at 0 and +0x381; on a player, 0x37 returns with nothing and 0x3f keeps its action.
TEST(SkillRepliesTest, TheSkills37And3FOfAMonsterEndInTheAttackAction)
{
    for (const std::uint8_t skill : {std::uint8_t{0x37}, std::uint8_t{0x3f}})
    {
        SCOPED_TRACE(int{skill});
        auto monster = std::make_unique<Replies>();
        monster->Actor(CasterIndex).type = MonsterType;
        ASSERT_TRUE(monster->Attack(skill, CasterKey, false).HasValue());
        EXPECT_EQ(monster->Actor(CasterIndex).animation.action, 3);
        EXPECT_EQ(monster->Actor(CasterIndex).attackCount, 2);
        EXPECT_EQ(monster->Actor(CasterIndex).byte381, 1);

        auto player = std::make_unique<Replies>();
        ASSERT_TRUE(player->Attack(skill, CasterKey, false).HasValue());
        EXPECT_EQ(player->Actor(CasterIndex).attackCount, 0);
        EXPECT_EQ(player->Actor(CasterIndex).byte381, skill == 0x3f ? 1 : 0);
    }
}

// 0x4cdda4: 0x31 in Atlans (8) and in the sky (10) is 0x41; in Lorencia, Devias and the rest, 0x40.
TEST(SkillRepliesTest, TheSkill31DependsOnTheMap)
{
    for (const auto& [map, action] :
         {std::pair{0, 0x40}, std::pair{2, 0x40}, std::pair{8, 0x41}, std::pair{10, 0x41}, std::pair{7, 0x40}})
    {
        auto tested = std::make_unique<Replies>();
        tested->world.Runtime().map = map;
        ASSERT_TRUE(tested->Attack(0x31).HasValue()) << map;
        EXPECT_EQ(tested->Actor(CasterIndex).animation.action, action) << map;
        EXPECT_EQ(tested->Actor(CasterIndex).byte381, 1) << map;
    }
}

// The mounts of branches 0x43 to 0x48, outside the safe zone.
TEST(SkillRepliesTest, TheMountChoosesTheActionOfTheLordSkills)
{
    for (const auto& [model, safe, action] :
         {std::tuple{std::uint16_t{0x1c16}, 0, 0x52}, std::tuple{std::uint16_t{0x1c14}, 0, 0x40},
          std::tuple{std::uint16_t{0x1c15}, 0, 0x41}, std::tuple{std::uint16_t{0x1c37}, 0, 0x5d},
          std::tuple{std::uint16_t{0x1c16}, 1, 0x3f}, std::tuple{Client::Kernel::NoItemModel, 0, 0x3f}})
    {
        auto tested = std::make_unique<Replies>();
        tested->Actor(CasterIndex).pet.model = model;
        tested->Actor(CasterIndex).safeZone = static_cast<std::uint8_t>(safe);
        ASSERT_TRUE(tested->Attack(0x43).HasValue());
        EXPECT_EQ(tested->Actor(CasterIndex).animation.action, action) << model << " " << safe;
    }
}

// 0x4cd410: the target vanishes and the source arrives, each with its flash 0x5c6 and the 0x76; then the 0x83.
TEST(SkillRepliesTest, TheTeleportSkillSwapsTheVanishing)
{
    auto tested = std::make_unique<Replies>();
    tested->world.Runtime().movement.gateRequestBlocked = 1;
    tested->Actor(HeroIndex).position = {1000.0f, 1000.0f, 0.0f};
    ASSERT_TRUE(tested->Attack(0xf, HeroKey).HasValue());
    const ActorState& hero = tested->Actor(HeroIndex);
    const ActorState& target = tested->Actor(TargetIndex);
    EXPECT_EQ(hero.angles.z, 0.0f);
    EXPECT_EQ(target.alphaTarget, 0.0f);
    EXPECT_EQ(target.byte80, 1);
    EXPECT_EQ(target.animation.action, 0x93);
    EXPECT_EQ(hero.alphaTarget, 1.0f);
    EXPECT_EQ(hero.byte80, 3);
    EXPECT_EQ(hero.animation.frame, 5.0f);
    // 0x4cd442 jumps to 0x4cde69, past the +0x381 of 0x4cde62.
    EXPECT_EQ(hero.byte381, 0);
    EXPECT_EQ(tested->AliveEffects(0x5c6), 2u);
    EXPECT_EQ(tested->world.Runtime().movement.gateRequestBlocked, 0);
    EXPECT_EQ(tested->world.Runtime().lastSoundId, Client::Audio::SoundId{0x83});
}

// 0x4cdb0f: a successful 0x44 clears the target's 0x1000 (0x4e6910) and kills its 0x628/5.
TEST(SkillRepliesTest, TheSucceededSkill44LowersTheTargetFlag)
{
    auto tested = std::make_unique<Replies>();
    ActorState& target = tested->Actor(TargetIndex);
    target.flags78 = 0x1001;
    // The target's 0x628/5, set by hand: MuRet does not create the 0x628 (CL11).
    auto& sword = tested->world.Map().effects.Effects()[4];
    sword.alive = true;
    sword.type = 0x628;
    sword.subtype = 5;
    sword.ownerContext = &target;
    ASSERT_TRUE(tested->Attack(0x44).HasValue());
    EXPECT_EQ(target.flags78, 1u);
    EXPECT_FALSE(sword.alive);

    auto failed = std::make_unique<Replies>();
    failed->Actor(TargetIndex).flags78 = 0x1001;
    ASSERT_TRUE(failed->Attack(0x44, CasterKey, false).HasValue());
    EXPECT_EQ(failed->Actor(TargetIndex).flags78, 0x1001u);
}

TEST(SkillRepliesTest, TheSkill46MarksAnotherTarget)
{
    auto tested = std::make_unique<Replies>();
    ASSERT_TRUE(tested->Attack(0x46).HasValue());
    EXPECT_EQ(tested->Actor(TargetIndex).flags78, 0x4000u);
}

// 0x4cddd3..0x4cde03: 0x3b drops the 0x112/0 at the source (its position, angles and light, ownerless), with its sixty
// joints 0x5ac/0 in the world's table, and asks for sound 0x94. The replica: the same with the table full (0x518cf0
// leaves without rand()) and then the creation on a twin world with that CRT.
TEST(SkillRepliesTest, TheSkill3bDropsItsComboAtTheSource)
{
    auto tested = std::make_unique<Replies>();
    auto full = std::make_unique<Replies>();
    for (auto& effect : full->world.Map().effects.Effects())
        effect.alive = true;
    ASSERT_TRUE(full->Attack(0x3b).HasValue());
    EXPECT_EQ(full->world.Runtime().lastSoundId, Client::Audio::SoundId{0x94});

    const auto applied = tested->Attack(0x3b);
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(tested->world.Runtime().lastSoundId, Client::Audio::SoundId{0x94});
    const ActorState& source = tested->Actor(CasterIndex);
    ASSERT_EQ(tested->AliveEffects(0x112), 1u);
    const auto& combo = tested->world.Map().effects.Effects()[0];
    EXPECT_EQ(combo.type, 0x112);
    EXPECT_EQ(combo.subtype, 0);
    EXPECT_EQ(combo.life, 20);
    EXPECT_FALSE(combo.hasOwner);
    EXPECT_EQ(combo.start, source.position);
    EXPECT_EQ(combo.light, source.light);

    auto twin = std::make_unique<Replies>();
    Client::Crt::CrtRandom replay = full->cloth.random;
    const ActorState& twinSource = twin->Actor(CasterIndex);
    const auto made = Client::Combat::CreateBlastEffectOutsideTheStep({.type = 0x112,
                                                                       .position = twinSource.position,
                                                                       .angles = twinSource.angles,
                                                                       .light = twinSource.light,
                                                                       .subtype = 0,
                                                                       .key = 0xffff},
                                                                      nullptr, twin->world, replay);
    ASSERT_TRUE(made.HasValue()) << made.Error();
    EXPECT_EQ(tested->cloth.random.State(), replay.State());
    const auto& joints = tested->world.DrawTail().joints;
    const auto& expected = twin->world.DrawTail().joints;
    EXPECT_EQ(std::ranges::count_if(joints, [](const auto& joint) { return joint.alive; }), 60);
    for (std::size_t slot = 0; slot < joints.size(); ++slot)
    {
        ASSERT_EQ(joints[slot].alive, expected[slot].alive) << slot;
        if (!joints[slot].alive)
            continue;
        EXPECT_EQ(joints[slot].type, 0x5ac);
        EXPECT_EQ(joints[slot].subtype, 0);
        EXPECT_EQ(joints[slot].position, expected[slot].position);
        EXPECT_EQ(joints[slot].angles, expected[slot].angles);
        EXPECT_EQ(joints[slot].width, expected[slot].width);
    }
}

// 0x4cd6bc..0x4cd748: with the skill succeeded, 0x33 wraps the target in ice (0x4cd6ff and 0x4cd72b, the 0xc1/1 and the
// /2 with it as owner and z 180 higher) and then stops it and sets 0x20 on it. The reference does the same by hand on
// an equal target, after the head's rand(): the ice spends its rand() before 0x4f2250.
TEST(SkillRepliesTest, TheSkill33FreezesTheTarget)
{
    auto tested = std::make_unique<Replies>();
    auto reference = std::make_unique<Replies>();
    for (Replies* replies : {tested.get(), reference.get()})
        replies->Actor(TargetIndex).angles = {0.0f, 0.0f, 90.0f};
    reference->cloth.random = Advanced(reference->cloth.random, ActorByteRandoms);
    ActorState& wrapped = reference->Actor(TargetIndex);
    ASSERT_TRUE(Client::Actors::WrapInIce(wrapped, reference->Resources(), {}).HasValue());
    wrapped.movement = 0;
    const auto context = Client::Scenes::TransitionStopContext(wrapped, reference->Resources());
    ASSERT_TRUE(context.HasValue()) << context.Error();
    ASSERT_TRUE(Client::Actors::SetActorStop(wrapped, context.Value()).HasValue());

    const auto applied = tested->Attack(0x33);

    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    const ActorState& target = tested->Actor(TargetIndex);
    EXPECT_EQ(target.flags78, 0x20u);
    EXPECT_EQ(target.movement, 0);
    EXPECT_EQ(target.light, glm::vec3(0.4f, 0.3f, 0.2f));
    const auto effects = tested->world.Map().effects.Effects();
    for (const auto& [slot, subtype, heading] :
         {std::tuple{std::size_t{0}, 1, 90.0f}, std::tuple{std::size_t{1}, 2, 270.0f}})
    {
        SCOPED_TRACE(subtype);
        EXPECT_TRUE(effects[slot].alive);
        EXPECT_EQ(effects[slot].type, Client::Effects::IceEffect);
        EXPECT_EQ(effects[slot].subtype, subtype);
        EXPECT_EQ(effects[slot].ownerContext, &target);
        EXPECT_FLOAT_EQ(effects[slot].transform.angles.z, heading);
    }
    EXPECT_EQ(tested->cloth.random.State(), reference->cloth.random.State());
}

// 0x4cd617..0x4cd67e: with the skill succeeded, 0x37 kills the target's 0x628/0 and puts another with it as owner
// (life 1000, its position and angles, light 1), without rand() of its own; then sound 0x85 on it and its 0x40. The
// target's 0x628/5 stays.
TEST(SkillRepliesTest, TheSkill37PutsASkullOverItsTarget)
{
    auto tested = std::make_unique<Replies>();
    ActorState& target = tested->Actor(TargetIndex);
    target.position = {1500, 2500, 100};
    target.angles = {0, 0, 30};
    const auto effects = tested->world.Map().effects.Effects();
    for (const std::size_t slot : {std::size_t{0}, std::size_t{1}})
    {
        effects[slot].alive = true;
        effects[slot].type = 0x628;
        effects[slot].subtype = static_cast<std::int32_t>(slot) * 5;
        effects[slot].ownerContext = &target;
    }
    const auto expected = Advanced(tested->cloth.random, ActorByteRandoms);

    const auto applied = tested->Attack(0x37);

    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(tested->cloth.random.State(), expected.State());
    EXPECT_TRUE(effects[1].alive);
    const auto& skull = effects[0];
    ASSERT_TRUE(skull.alive);
    EXPECT_EQ(skull.type, 0x628);
    EXPECT_EQ(skull.subtype, 0);
    EXPECT_EQ(skull.life, 1000);
    EXPECT_EQ(skull.ownerContext, &target);
    EXPECT_EQ(skull.transform.position, target.position);
    EXPECT_EQ(skull.transform.angles, target.angles);
    EXPECT_EQ(skull.light, glm::vec3(1.0f));
    EXPECT_EQ(tested->world.Runtime().lastSoundId, Client::Audio::SoundId{0x85});
    EXPECT_EQ(target.flags78, 0x40u);
}

// 0x4e683b..0x4e68a6: 0x4e6380's 0x40 without the bit puts the skull 0x628/0 over the character and sound 0x85; with
// the bit, it only sets it again.
TEST(SkillRepliesTest, TheFlag40PutsASkullOverTheActorOnce)
{
    auto tested = std::make_unique<Replies>();
    ActorState& caster = tested->Actor(CasterIndex);
    caster.position = {700, 800, 90};
    ASSERT_TRUE(
        Client::Skills::ApplyActorState(Client::Protocol::Skills::ActorState{1, 0x40, CasterKey}, tested->Resources())
            .HasValue());
    EXPECT_EQ(caster.flags78, 0x40u);
    EXPECT_EQ(tested->world.Runtime().lastSoundId, Client::Audio::SoundId{0x85});
    const auto& effects = tested->world.Map().effects.Effects();
    const auto skulls = [&effects]
    { return std::ranges::count_if(effects, [](const auto& effect) { return effect.alive && effect.type == 0x628; }); };
    ASSERT_EQ(skulls(), 1);
    EXPECT_EQ(effects[0].ownerContext, &caster);
    EXPECT_EQ(effects[0].transform.position, caster.position);

    tested->world.Runtime().lastSoundId.reset();
    ASSERT_TRUE(Client::Actors::ApplyEffectFlags(caster, 0x40, false, tested->Resources()).HasValue());
    EXPECT_EQ(skulls(), 1);
    EXPECT_FALSE(tested->world.Runtime().lastSoundId.has_value());
}

// 0x4cdb36..0x4cdb7a: with the skill succeeded, 0x45 sets 0x2000 on the source and puts the mana rune 0x138/0 at its
// position, with its angles and light, without rand() of its own.
TEST(SkillRepliesTest, TheSkill45RaisesAManaRuneOverItsCaster)
{
    auto tested = std::make_unique<Replies>();
    ActorState& source = tested->Actor(CasterIndex);
    source.position = {1000, 2000, 300};
    source.angles = {0, 0, 90};
    source.light = {0.5f, 0.6f, 0.7f};
    const auto applied = tested->Attack(0x45);
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(source.flags78 & 0x2000u, 0x2000u);
    const auto& effects = tested->world.Map().effects.Effects();
    const auto rune = std::ranges::find_if(effects, [](const auto& effect) { return effect.alive; });
    ASSERT_NE(rune, effects.end());
    EXPECT_EQ(rune->type, 0x138);
    EXPECT_EQ(rune->subtype, 0);
    EXPECT_EQ(rune->life, 50);
    EXPECT_EQ(rune->transform.position, glm::vec3(1000, 2000, 600));
    EXPECT_EQ(rune->transform.angles, glm::vec3(0, 0, 45));
    EXPECT_EQ(rune->light, glm::vec3(0.5f, 0.6f, 0.7f));
    EXPECT_FALSE(rune->hasOwner);
}

// 0x518d0f..0x518d26 returns when it finds no free slot, without running the pending constructor or using its RNG.
// What follows the creation does not depend on it: 0x37 sounds 0x85 on the target and sets 0x40 (0x4cd669..0x4cd67e),
// and 0x33 sets 0x20 (0x4cd740..0x4cd748), after stopping it (TheSkill33WithAFullPoolStillStopsTheTarget).
TEST(SkillRepliesTest, AFullEffectPoolDoesNotStopTheMagicReplyTail)
{
    for (const auto skill : {std::uint8_t{0x37}, std::uint8_t{0x33}, std::uint8_t{0x45}, std::uint8_t{0x3b}})
    {
        SCOPED_TRACE(static_cast<int>(skill));
        auto tested = std::make_unique<Replies>();
        ActorState& source = tested->Actor(CasterIndex);
        const ActorState& target = tested->Actor(TargetIndex);
        source.byte381 = 7;
        for (auto& effect : tested->world.Map().effects.Effects())
        {
            effect.alive = true;
            effect.type = 0;
        }
        const bool prehead = skill != 0x3b;
        const auto expected = Advanced(tested->cloth.random, prehead ? ActorByteRandoms : 0);

        const auto applied = tested->Attack(skill);

        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        // 0x33 also spends the rand() of 0x4f2250: TheSkill33WithAFullPoolStillStopsTheTarget checks it.
        if (skill != 0x33)
        {
            EXPECT_EQ(tested->cloth.random.State(), expected.State());
        }
        EXPECT_EQ(source.byte381, skill == 0x33 || skill == 0x45 ? 1 : 7);
        EXPECT_EQ(source.flags78, skill == 0x45 ? 0x2000u : 0u);
        EXPECT_EQ(target.flags78, skill == 0x37 ? 0x40u : (skill == 0x33 ? 0x20u : 0u));
        EXPECT_EQ(source.byte38e, prehead ? skill : 0);
        if (skill == 0x3b)
        {
            EXPECT_EQ(tested->world.Runtime().lastSoundId, Client::Audio::SoundId{0x94});
        }
        else if (skill == 0x37)
        {
            EXPECT_EQ(tested->world.Runtime().lastSoundId, Client::Audio::SoundId{0x85});
        }
        else
        {
            EXPECT_FALSE(tested->world.Runtime().lastSoundId.has_value());
        }
        EXPECT_TRUE(std::ranges::all_of(tested->world.Map().effects.Effects(),
                                        [](const auto& effect) { return effect.alive; }));
    }
}

// 0x4cd733..0x4cd748: with the table full, 0x33 still leaves the target's +0x378 at 0, stops it (0x4f2250, with its
// rand()) and sets 0x20. The reference does the same by hand on an equal target, after the head's rand().
TEST(SkillRepliesTest, TheSkill33WithAFullPoolStillStopsTheTarget)
{
    constexpr std::uint8_t BusyAction = 0x22;
    auto tested = std::make_unique<Replies>();
    auto reference = std::make_unique<Replies>();
    for (Replies* replies : {tested.get(), reference.get()})
    {
        ActorState& target = replies->Actor(TargetIndex);
        target.animation.action = BusyAction;
        target.movement = 1;
    }
    for (auto& effect : tested->world.Map().effects.Effects())
    {
        effect.alive = true;
        effect.type = 0;
    }
    reference->cloth.random = Advanced(reference->cloth.random, ActorByteRandoms);
    ActorState& stopped = reference->Actor(TargetIndex);
    stopped.movement = 0;
    const auto context = Client::Scenes::TransitionStopContext(stopped, reference->Resources());
    ASSERT_TRUE(context.HasValue()) << context.Error();
    const auto stop = Client::Actors::SetActorStop(stopped, context.Value());
    ASSERT_TRUE(stop.HasValue()) << stop.Error();
    ASSERT_NE(stopped.animation.action, BusyAction);

    const auto applied = tested->Attack(0x33);

    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    const ActorState& target = tested->Actor(TargetIndex);
    EXPECT_EQ(target.movement, 0);
    EXPECT_EQ(target.animation.action, stopped.animation.action);
    EXPECT_EQ(target.flags78, 0x20u);
    EXPECT_EQ(tested->cloth.random.State(), reference->cloth.random.State());
}

// 0x518ac0 before 0x518cf0: with the table full, killing the target's effect frees its slot for the creation. 0x37
// creates the new skull there (life 1000); 0x33 creates its 0xc1/1 in the slot the old one left and the /2 has nowhere
// to go.
TEST(SkillRepliesTest, KillingAMatchingEffectReopensItsConstructorEvenFromAFullPool)
{
    for (const auto skill : {std::uint8_t{0x37}, std::uint8_t{0x33}})
    {
        SCOPED_TRACE(static_cast<int>(skill));
        auto tested = std::make_unique<Replies>();
        auto effects = tested->world.Map().effects.Effects();
        for (auto& effect : effects)
            effect.alive = true;
        ActorState* target = &tested->Actor(TargetIndex);
        effects.front().type = skill == 0x37 ? 0x628 : 0xc1;
        effects.front().subtype = skill == 0x37 ? 0 : 1;
        effects.front().life = 7;
        effects.front().ownerContext = target;
        const auto applied = tested->Attack(skill);
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_TRUE(effects.front().alive);
        if (skill == 0x37)
        {
            EXPECT_EQ(effects.front().life, 1000);
            continue;
        }
        EXPECT_EQ(effects.front().subtype, 1);
        EXPECT_EQ(effects.front().life, 20);
        EXPECT_EQ(std::ranges::count_if(effects, [target](const auto& effect)
                                        { return effect.type == 0xc1 && effect.ownerContext == target; }),
                  1);
        EXPECT_EQ(target->flags78, 0x20u);
    }
}

// 0x4cdc37..0x4cdc9c and 0x4cdd45..0x4cdd94: without the bit (0x400 for 0x35, 0x10 for 0x30) on the target, they kill
// its 0x5ac/2 (with its joints 0x619/9) or its 0x5ac/1 and create another with the target as owner (life 1000); with
// the bit, nothing. Then the bit, and it goes on. No extra rand(). With the table full, none.
TEST(SkillRepliesTest, TheGuardAndSwellSkillsLightAFlareOnTheTarget)
{
    for (const std::int32_t map : {0, 2})
    {
        for (const auto& [skill, subtype, bit] :
             {std::tuple{std::uint8_t{0x35}, 2, 0x400u}, std::tuple{std::uint8_t{0x30}, 1, 0x10u}})
        {
            SCOPED_TRACE(testing::Message() << "mapa " << map << ", skill " << int{skill});
            auto tested = std::make_unique<Replies>();
            tested->world.Runtime().map = map;
            ActorState& target = tested->Actor(TargetIndex);
            auto& effects = tested->world.Map().effects;
            ASSERT_TRUE(effects.CreateSkillFlare({}, {}, {}, subtype, &target).has_value());
            auto& joint = tested->world.DrawTail().joints[0];
            joint.alive = true;
            joint.type = 0x619;
            joint.subtype = 9;
            joint.ownerContext = &target;
            ASSERT_TRUE(tested->Attack(skill).HasValue());

            const auto& flare = effects.Effects()[0];
            ASSERT_TRUE(flare.alive);
            EXPECT_EQ(flare.type, Client::Effects::IcarusFlareEffect);
            EXPECT_EQ(flare.subtype, subtype);
            EXPECT_EQ(flare.ownerContext, &target);
            EXPECT_EQ(flare.life, 1000);
            EXPECT_EQ(flare.transform.position, target.position);
            EXPECT_EQ(tested->AliveEffects(Client::Effects::IcarusFlareEffect), 1u);
            EXPECT_EQ(joint.alive, skill != 0x35);
            EXPECT_EQ(target.flags78 & bit, bit);

            // With the bit already set, it neither kills nor creates, and the same rand().
            auto lit = std::make_unique<Replies>();
            lit->world.Runtime().map = map;
            lit->Actor(TargetIndex).flags78 = bit;
            ASSERT_TRUE(lit->Attack(skill).HasValue());
            EXPECT_EQ(lit->AliveEffects(Client::Effects::IcarusFlareEffect), 0u);
            EXPECT_EQ(lit->cloth.random.State(), tested->cloth.random.State());

            auto full = std::make_unique<Replies>();
            full->world.Runtime().map = map;
            for (auto& effect : full->world.Map().effects.Effects())
                effect.alive = true;
            ASSERT_TRUE(full->Attack(skill).HasValue());
            EXPECT_EQ(full->Actor(TargetIndex).flags78 & bit, bit);
            EXPECT_EQ(full->cloth.random.State(), tested->cloth.random.State());
        }
    }
}

// 0x4ccea1: 0x3b does not turn or touch +0x3a4, +0x38d or +0x38e (no rand()). With the effect table full, the 0x112 is
// not born and does not draw its joints' rand().
TEST(SkillRepliesTest, TheSkill3bGoesStraightToTheTable)
{
    auto tested = std::make_unique<Replies>();
    for (auto& effect : tested->world.Map().effects.Effects())
        effect.alive = true;
    ASSERT_TRUE(tested->Attack(0x3b).HasValue());
    EXPECT_EQ(tested->Actor(CasterIndex).byte38e, 0);
    EXPECT_EQ(tested->Actor(CasterIndex).attackTarget, 0);
    EXPECT_EQ(tested->Actor(CasterIndex).angles.z, 0.0f);
    EXPECT_EQ(tested->cloth.random.State(), 1u);
}

// 0x4ccef1: 0x4c also leaves the target in +0x434.
TEST(SkillRepliesTest, TheSkill4cKeepsItsTarget)
{
    auto tested = std::make_unique<Replies>();
    ASSERT_TRUE(tested->Attack(0x4c).HasValue());
    EXPECT_EQ(tested->Actor(CasterIndex).skillTarget4c, static_cast<std::int32_t>(TargetIndex));
}

// 0x4cde2f..0x4cde58: the hero's 0x28 lets go of the right button: the release (0x7d36840), the click (0x7d36880), the
// down (0x7d3685c) and the charge frames (0x7d368a4). Another's does not touch them.
TEST(SkillRepliesTest, TheSkill28OfTheHeroDropsTheRightButton)
{
    auto tested = std::make_unique<Replies>();
    auto& ui = tested->world.Runtime().ui;
    ui.rightReleased = true;
    ui.rightClick = true;
    ui.rightDown = true;
    ui.rightHoldFrames = 12;
    ASSERT_TRUE(tested->Attack(0x28, HeroKey).HasValue());
    EXPECT_FALSE(ui.rightReleased);
    EXPECT_FALSE(ui.rightClick);
    EXPECT_FALSE(ui.rightDown);
    EXPECT_EQ(ui.rightHoldFrames, 0);
    EXPECT_EQ(tested->Actor(HeroIndex).animation.action, 0x45);
    EXPECT_EQ(tested->Actor(HeroIndex).byte381, 1);

    auto other = std::make_unique<Replies>();
    auto& otherUi = other->world.Runtime().ui;
    otherUi.rightReleased = true;
    otherUi.rightClick = true;
    otherUi.rightHoldFrames = 12;
    ASSERT_TRUE(other->Attack(0x28).HasValue());
    EXPECT_TRUE(otherUi.rightReleased);
    EXPECT_TRUE(otherUi.rightClick);
    EXPECT_EQ(otherUi.rightHoldFrames, 12);
}

// The hands of 0x4cc110: a staff in the left, another weapon in the right, and the elf with wings.
TEST(SkillRepliesTest, TheStaffSkillsActByTheHands)
{
    auto staff = std::make_unique<Replies>();
    staff->Actor(CasterIndex).hands[1].model = 0xa12;
    ASSERT_TRUE(staff->Attack(0x18).HasValue());
    EXPECT_EQ(staff->Actor(CasterIndex).animation.action, 0x2e);

    auto winged = std::make_unique<Replies>();
    winged->Actor(CasterIndex).characterClass = 2;
    winged->Actor(CasterIndex).wings.model = 0xc00;
    winged->Actor(CasterIndex).hands[0].model = 0xa1a;
    ASSERT_TRUE(winged->Attack(0x34).HasValue());
    EXPECT_EQ(winged->Actor(CasterIndex).animation.action, 0x31);

    auto bare = std::make_unique<Replies>();
    const std::uint8_t before = bare->Actor(CasterIndex).animation.action;
    ASSERT_TRUE(bare->Attack(0x36).HasValue());
    EXPECT_EQ(bare->Actor(CasterIndex).animation.action, before);
    EXPECT_EQ(bare->Actor(CasterIndex).byte381, 1);
}

// ---------------------------------------------------------------------------------------------------------------
// 0x1E (0x4ce000)

// On another player: the skill in +0x38e, the heading, the table's action with the frame at 0 and the cell.
TEST(SkillRepliesTest, TheDurationMagicOfAnotherPlayerActsAndKeepsTheCell)
{
    auto tested = std::make_unique<Replies>();
    ActorState& caster = tested->Actor(CasterIndex);
    caster.attackTarget = 3;
    caster.byte380 = 9;
    const CrtRandom expected = Advanced(tested->cloth.random, ActorByteRandoms);
    ASSERT_TRUE(Client::Skills::ApplyDurationMagic(DurationMagic{0xc, CasterKey, 0x80, 0x81, 0x40}, tested->Resources())
                    .HasValue());
    EXPECT_EQ(caster.byte38e, 0xc);
    EXPECT_EQ(caster.angles.z, static_cast<float>(64.0 * static_cast<double>(1.0f / 255.0f) * 360.0));
    EXPECT_EQ(caster.animation.action, 0x94);
    EXPECT_EQ(caster.skillCellX, 0x80);
    EXPECT_EQ(caster.skillCellY, 0x81);
    EXPECT_EQ(caster.byte381, 1);
    EXPECT_EQ(caster.attackTarget, 0xffff);
    EXPECT_EQ(caster.byte380, 0);
    EXPECT_EQ(tested->cloth.random.State(), expected.State());
}

// 0x4cea84 (0x18 and 0x34) and 0x4ceaa9 (0x36): another player does 0x4f4a10's attack action (unarmed, 0x22 with
// 0x4f3f80's 3848 rand()) and the frame at 0, in Lorencia, Devias and Atlans (8).
TEST(SkillRepliesTest, TheDurationMagicOfAnotherPlayerCanBeTheAttackAction)
{
    for (const std::int32_t map : {0, 2, 8})
    {
        for (const std::uint8_t skill : {std::uint8_t{0x18}, std::uint8_t{0x34}, std::uint8_t{0x36}})
        {
            SCOPED_TRACE(std::to_string(map) + " " + std::to_string(skill));
            auto tested = std::make_unique<Replies>();
            tested->world.Runtime().map = map;
            ActorState& caster = tested->Actor(CasterIndex);
            caster.animation.frame = 2.0f;
            const CrtRandom expected = Advanced(tested->cloth.random, ActorByteRandoms + OwnRecordRandoms);
            ASSERT_TRUE(
                Client::Skills::ApplyDurationMagic(DurationMagic{skill, CasterKey, 4, 5, 0}, tested->Resources())
                    .HasValue());
            EXPECT_EQ(caster.animation.action, 0x22);
            EXPECT_EQ(caster.attackCount, 1);
            EXPECT_EQ(caster.animation.frame, 0.0f);
            EXPECT_EQ(caster.skillCellX, 4);
            EXPECT_EQ(caster.byte381, 1);
            EXPECT_EQ(tested->cloth.random.State(), expected.State());
        }
    }
}

TEST(SkillRepliesTest, TheDurationMagicOfTheHeroOnlyKeepsTheCell)
{
    auto tested = std::make_unique<Replies>();
    ActorState& hero = tested->Actor(HeroIndex);
    const std::uint8_t action = hero.animation.action;
    ASSERT_TRUE(
        Client::Skills::ApplyDurationMagic(DurationMagic{0xe, HeroKey, 10, 11, 0}, tested->Resources()).HasValue());
    EXPECT_EQ(hero.byte38e, 0xe);
    EXPECT_EQ(hero.animation.action, action);
    EXPECT_EQ(hero.skillCellX, 10);
    EXPECT_EQ(hero.skillCellY, 11);
    EXPECT_EQ(hero.attackTarget, 0xffff);
}

// 0x4cece8: a monster does 0x4f4a10's attack action after the skill and the heading, with the frame at 0, and goes on
// with the cell, +0x381, +0x3a4 and +0x380. The rand(): the close of +0x38e and the hit sound's.
TEST(SkillRepliesTest, TheDurationMagicOfAMonsterIsTheAttackAction)
{
    auto tested = std::make_unique<Replies>();
    ActorState& caster = tested->Actor(CasterIndex);
    caster.type = MonsterType;
    caster.animation.frame = 2.0f;
    caster.byte380 = 2;
    const CrtRandom expected = Advanced(tested->cloth.random, ActorByteRandoms + 1);
    ASSERT_TRUE(
        Client::Skills::ApplyDurationMagic(DurationMagic{0xc, CasterKey, 1, 2, 0xff}, tested->Resources()).HasValue());
    EXPECT_EQ(caster.byte38e, 0xc);
    EXPECT_EQ(caster.animation.action, 3);
    EXPECT_EQ(caster.attackCount, 2);
    EXPECT_EQ(caster.animation.frame, 0.0f);
    EXPECT_EQ(caster.skillCellX, 1);
    EXPECT_EQ(caster.skillCellY, 2);
    EXPECT_EQ(caster.byte381, 1);
    EXPECT_EQ(caster.attackTarget, 0xffff);
    EXPECT_EQ(caster.byte380, 0);
    EXPECT_EQ(tested->cloth.random.State(), expected.State());
    // 255 · (1/255) · 360 in double goes past 360 and rounds to the float above.
    EXPECT_EQ(tested->Actor(CasterIndex).angles.z,
              static_cast<float>(255.0 * static_cast<double>(1.0f / 255.0f) * 360.0));
    EXPECT_GT(tested->Actor(CasterIndex).angles.z, 360.0f);
}

// Q-C1204: without the key's character, the 400: the skill in +0x38e (its rand()), the heading, the frame at 0, the
// cell, +0x381, +0x3a4 and +0x380. The action, which in the original comes from whatever type is in the 400, is
// discarded without cutting and without rand(). The 1B also goes to the 400. In Lorencia and Devias.
TEST(SkillRepliesTest, TheDurationMagicWithoutTheKeyWritesTheLastActor)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        auto tested = std::make_unique<Replies>();
        tested->world.Runtime().map = map;
        ActorState& phantom = tested->Actor(400);
        phantom.animation.frame = 2.0f;
        phantom.byte380 = 4;
        const std::uint8_t action = phantom.animation.action;
        const CrtRandom expected = Advanced(tested->cloth.random, ActorByteRandoms);
        ASSERT_TRUE(
            Client::Skills::ApplyDurationMagic(DurationMagic{0xc, 0x0777, 1, 2, 0x40}, tested->Resources()).HasValue());
        EXPECT_EQ(phantom.byte38e, 0xc);
        EXPECT_EQ(phantom.angles.z, static_cast<float>(64.0 * static_cast<double>(1.0f / 255.0f) * 360.0));
        EXPECT_EQ(phantom.animation.action, action);
        EXPECT_EQ(phantom.attackCount, 0);
        EXPECT_EQ(phantom.animation.frame, 0.0f);
        EXPECT_EQ(phantom.skillCellX, 1);
        EXPECT_EQ(phantom.skillCellY, 2);
        EXPECT_EQ(phantom.byte381, 1);
        EXPECT_EQ(phantom.attackTarget, 0xffff);
        EXPECT_EQ(phantom.byte380, 0);
        EXPECT_EQ(tested->cloth.random.State(), expected.State());
        phantom.flags78 = 8;
        ASSERT_TRUE(Client::Skills::ApplyMagicCancel(MagicCancel{0x1b, 0x0777}, tested->Resources()).HasValue());
        EXPECT_EQ(phantom.flags78, 0u);
    }
}

TEST(SkillRepliesTest, TheDurationTableChoosesTheActionOfTheOtherPlayer)
{
    for (const auto& [skill, model, action] : {std::tuple{std::uint8_t{0xa}, Client::Kernel::NoItemModel, 0x96},
                                               std::tuple{std::uint8_t{0x41}, std::uint16_t{0x1c37}, 0x5b},
                                               std::tuple{std::uint8_t{0x41}, Client::Kernel::NoItemModel, 0x94},
                                               std::tuple{std::uint8_t{0x2a}, Client::Kernel::NoItemModel, 0x3e},
                                               std::tuple{std::uint8_t{0x37}, Client::Kernel::NoItemModel, 0x3d},
                                               std::tuple{std::uint8_t{0x46}, Client::Kernel::NoItemModel, 0x3f},
                                               std::tuple{std::uint8_t{0x4c}, Client::Kernel::NoItemModel, 0x62}})
    {
        auto tested = std::make_unique<Replies>();
        tested->Actor(CasterIndex).pet.model = model;
        ASSERT_TRUE(Client::Skills::ApplyDurationMagic(DurationMagic{skill, CasterKey, 1, 2, 3}, tested->Resources())
                        .HasValue());
        EXPECT_EQ(tested->Actor(CasterIndex).animation.action, action) << int{skill};
    }
}

// ---------------------------------------------------------------------------------------------------------------
// 0x1B (0x4cbe90), 0x07 (0x4cbe00) and 0x4e6910

TEST(SkillRepliesTest, TheMagicCancelLowersItsFlag)
{
    auto tested = std::make_unique<Replies>();
    ActorState& caster = tested->Actor(CasterIndex);
    caster.flags78 = 0x4fff;
    ASSERT_TRUE(Client::Skills::ApplyMagicCancel(MagicCancel{0x1b, CasterKey}, tested->Resources()).HasValue());
    EXPECT_EQ(caster.flags78, 0x4ff7u);
    ASSERT_TRUE(Client::Skills::ApplyMagicCancel(MagicCancel{0x46, CasterKey}, tested->Resources()).HasValue());
    EXPECT_EQ(caster.flags78, 0x0ff7u);
    ASSERT_TRUE(Client::Skills::ApplyMagicCancel(MagicCancel{0x99, CasterKey}, tested->Resources()).HasValue());
    EXPECT_EQ(caster.flags78, 0x0ff7u);
}

// 0x4cbfc8: 0xc9 first goes through 0x439d40, which on a monster clears the 0x100 and sets +0x110 to 0x21.
TEST(SkillRepliesTest, TheMonsterCancelSettlesBeforeTheFlag)
{
    auto tested = std::make_unique<Replies>();
    ActorState& caster = tested->Actor(CasterIndex);
    caster.type = MonsterType;
    caster.flags78 = 0x108;
    ASSERT_TRUE(Client::Skills::ApplyMagicCancel(MagicCancel{0xc9, CasterKey}, tested->Resources()).HasValue());
    EXPECT_EQ(caster.byte110, 0x21);
    EXPECT_EQ(caster.flags78, 0u);
}

// With type 1 and the mask set, +0x7c stays at 0x200000 if it had it; unset, 0x4e6380.
TEST(SkillRepliesTest, TheActorStateOfTypeOneRewritesTheSecondFlags)
{
    auto tested = std::make_unique<Replies>();
    ActorState& caster = tested->Actor(CasterIndex);
    caster.flags78 = 0x30;
    caster.flags7c = 0x200123;
    ASSERT_TRUE(
        Client::Skills::ApplyActorState(Client::Protocol::Skills::ActorState{1, 0x10, CasterKey}, tested->Resources())
            .HasValue());
    EXPECT_EQ(caster.flags7c, 0x200000u);
    caster.flags7c = 0x123;
    ASSERT_TRUE(
        Client::Skills::ApplyActorState(Client::Protocol::Skills::ActorState{1, 0x10, CasterKey}, tested->Resources())
            .HasValue());
    EXPECT_EQ(caster.flags7c, 0u);
    // 0x4cbe47: unset, 0x4e6380 with the mask (ApplyEffectFlags): the 4 is set and +0x7c stays.
    ASSERT_TRUE(
        Client::Skills::ApplyActorState(Client::Protocol::Skills::ActorState{1, 0x4, CasterKey}, tested->Resources())
            .HasValue());
    EXPECT_EQ(caster.flags78, 0x34u);
    EXPECT_EQ(caster.flags7c, 0u);
    // The 0x1000 sets its bit and creates three 0x10f/8 (0x4e662d) from its +0x10, turned 90, 180 and 270, each 10
    // lower.
    caster.position = {1000, 2000, 300};
    ASSERT_TRUE(
        Client::Skills::ApplyActorState(Client::Protocol::Skills::ActorState{1, 0x1000, CasterKey}, tested->Resources())
            .HasValue());
    EXPECT_EQ(caster.flags78, 0x1034u);
    const auto& joints = tested->world.DrawTail().joints;
    for (std::size_t i = 0; i < 3; ++i)
    {
        ASSERT_TRUE(joints[i].alive);
        EXPECT_EQ(joints[i].type, 0x10f);
        EXPECT_EQ(joints[i].subtype, 8);
        EXPECT_EQ(joints[i].texture, 0x5ac);
        EXPECT_EQ(joints[i].ownerContext, &caster);
        EXPECT_EQ(joints[i].width, 30.0f);
        EXPECT_EQ(joints[i].life, 40);
        EXPECT_EQ(joints[i].angles.z, 90.0f * static_cast<float>(i + 1));
        EXPECT_EQ(joints[i].pathIncrement.z, 300.0f - 10.0f * static_cast<float>(i));
        EXPECT_EQ(joints[i].targetPosition, caster.position);
    }
    EXPECT_FALSE(joints[3].alive);
    // The 0x400: the character's 0x5ac/2, and it goes on.
    ASSERT_TRUE(
        Client::Skills::ApplyActorState(Client::Protocol::Skills::ActorState{1, 0x400, CasterKey}, tested->Resources())
            .HasValue());
    EXPECT_EQ(caster.flags78, 0x1434u);
    const auto& flare = tested->world.Map().effects.Effects()[0];
    EXPECT_TRUE(flare.alive);
    EXPECT_EQ(flare.subtype, 2);
    EXPECT_EQ(flare.ownerContext, &caster);
}

// 0x4e6430..0x4e6491: the 0x100 on a player that did not have it kills its 0x10f/0 (0x534f90) and creates five of
// width 50 around it (0x4e644f); with the bit already set, nothing.
TEST(SkillRepliesTest, TheActorStateHundredRingsAPlayer)
{
    auto tested = std::make_unique<Replies>();
    ActorState& caster = tested->Actor(CasterIndex);
    auto& joints = tested->world.DrawTail().joints;
    joints[0].alive = true;
    joints[0].type = 0x10f;
    joints[0].subtype = 0;
    joints[0].ownerContext = &caster;
    joints[0].life = 5;
    const auto rings = [&]
    {
        return std::ranges::count_if(joints,
                                     [&caster](const Client::Kernel::JointState& joint)
                                     {
                                         return joint.alive && joint.type == 0x10f && joint.subtype == 0 &&
                                                joint.ownerContext == &caster && joint.width == 50.0f &&
                                                joint.life == 999999;
                                     });
    };
    ASSERT_TRUE(
        Client::Skills::ApplyActorState(Client::Protocol::Skills::ActorState{1, 0x100, CasterKey}, tested->Resources())
            .HasValue());
    EXPECT_EQ(caster.flags78 & 0x100u, 0x100u);
    EXPECT_EQ(rings(), 5);
    joints[1].alive = false;
    ASSERT_TRUE(
        Client::Skills::ApplyActorState(Client::Protocol::Skills::ActorState{1, 0x100, CasterKey}, tested->Resources())
            .HasValue());
    EXPECT_EQ(rings(), 4);
}

// 0x4cbe45: 0x4e6380 knows whether it is the published hero ([0x743a170]): the 0x2000000 cuts on it and is set on
// another.
TEST(SkillRepliesTest, TheActorStateOfTypeOneKnowsThePublishedHero)
{
    auto tested = std::make_unique<Replies>();
    const auto hero = Client::Skills::ApplyActorState(Client::Protocol::Skills::ActorState{1, 0x2000000, HeroKey},
                                                      tested->Resources());
    ASSERT_FALSE(hero.HasValue());
    EXPECT_NE(hero.Error().find("0x4e68f5"), std::string::npos) << hero.Error();
    ASSERT_TRUE(Client::Skills::ApplyActorState(Client::Protocol::Skills::ActorState{1, 0x2000000, CasterKey},
                                                tested->Resources())
                    .HasValue());
    EXPECT_EQ(tested->Actor(CasterIndex).flags78, 0x2000000u);
}

// 0x4e67a6..0x4e6835: the flag 0x20 without its bit kills the character's 0xc1/1 (0x518ac0) and creates the 0xc1/1 and
// the /2 (0x4e67fc, 0x4e6825) at its position, with its angles (the second with z 180 higher) and it as owner. Each
// creation overwrites the character's light with (0.4f; 0.3f; 0.2f). With the bit already set, nothing.
TEST(SkillRepliesTest, TheIceFlagFreezesTheActor)
{
    constexpr std::size_t OldIce = 5;
    auto tested = std::make_unique<Replies>();
    ActorState& caster = tested->Actor(CasterIndex);
    caster.angles = {0.0f, 0.0f, 30.0f};
    const auto effects = tested->world.Map().effects.Effects();
    effects[OldIce].alive = true;
    effects[OldIce].type = Client::Effects::IceEffect;
    effects[OldIce].subtype = 1;
    effects[OldIce].ownerContext = &caster;

    ASSERT_TRUE(
        Client::Skills::ApplyActorState(Client::Protocol::Skills::ActorState{1, 0x20, CasterKey}, tested->Resources())
            .HasValue());

    EXPECT_EQ(caster.flags78, 0x20u);
    EXPECT_FALSE(effects[OldIce].alive);
    for (const auto& [slot, subtype, heading] :
         {std::tuple{std::size_t{0}, 1, 30.0f}, std::tuple{std::size_t{1}, 2, 210.0f}})
    {
        SCOPED_TRACE(subtype);
        const auto& ice = effects[slot];
        EXPECT_TRUE(ice.alive);
        EXPECT_EQ(ice.type, Client::Effects::IceEffect);
        EXPECT_EQ(ice.subtype, subtype);
        EXPECT_EQ(ice.ownerContext, &caster);
        EXPECT_EQ(ice.transform.position, caster.position);
        EXPECT_FLOAT_EQ(ice.transform.angles.x, -20.0f);
        EXPECT_FLOAT_EQ(ice.transform.angles.z, heading);
    }
    EXPECT_EQ(caster.light, glm::vec3(0.4f, 0.3f, 0.2f));

    const auto alive = std::ranges::count_if(effects, [](const auto& effect) { return effect.alive; });
    const auto state = tested->cloth.random.State();
    ASSERT_TRUE(
        Client::Skills::ApplyActorState(Client::Protocol::Skills::ActorState{1, 0x20, CasterKey}, tested->Resources())
            .HasValue());
    EXPECT_EQ(std::ranges::count_if(effects, [](const auto& effect) { return effect.alive; }), alive);
    EXPECT_EQ(tested->cloth.random.State(), state);
}

TEST(SkillRepliesTest, TheActorStateOfAnotherTypeLowersTheLitFlag)
{
    auto tested = std::make_unique<Replies>();
    ActorState& caster = tested->Actor(CasterIndex);
    caster.flags78 = 0x30;
    ASSERT_TRUE(
        Client::Skills::ApplyActorState(Client::Protocol::Skills::ActorState{2, 0x20, CasterKey}, tested->Resources())
            .HasValue());
    EXPECT_EQ(caster.flags78, 0x10u);
    ASSERT_TRUE(
        Client::Skills::ApplyActorState(Client::Protocol::Skills::ActorState{2, 0x20, CasterKey}, tested->Resources())
            .HasValue());
    EXPECT_EQ(caster.flags78, 0x10u);
}

// 0x4e6cb6: the 0x100000 clears its own from +0x7c and the thirteen of +0x78, and kills the character's effects and
// joints.
TEST(SkillRepliesTest, TheBigClearLowersEverything)
{
    auto tested = std::make_unique<Replies>();
    ActorState& actor = tested->Actor(CasterIndex);
    actor.flags78 = 0xffffffffu;
    actor.flags7c = 0x100001u;
    auto& joints = tested->world.DrawTail().joints;
    joints[0].alive = true;
    joints[0].type = 0x10f;
    joints[0].subtype = 9;
    joints[0].ownerContext = &actor;
    joints[1].alive = true;
    joints[1].type = 0x10f;
    joints[1].subtype = 9;
    joints[1].ownerContext = &tested->Actor(TargetIndex);
    ASSERT_TRUE(Client::Actors::RemoveActorFlag(0x100000, actor, tested->world, &tested->effectHooks).HasValue());
    EXPECT_EQ(actor.flags7c, 1u);
    EXPECT_EQ(actor.flags78, 0xffffffffu & ~0x5f7fu);
    EXPECT_FALSE(joints[0].alive);
    EXPECT_TRUE(joints[1].alive);
}

// 0x4e6c23: types 0x204 to 0x208 subtract the 0x10000 from +0x7c, whether they have it or not.
TEST(SkillRepliesTest, SomeTypesLowerTheFlag10000FromTheSecondFlags)
{
    auto tested = std::make_unique<Replies>();
    ActorState& actor = tested->Actor(CasterIndex);
    actor.type = 0x205;
    actor.flags78 = 0x10000;
    ASSERT_TRUE(Client::Actors::RemoveActorFlag(0x10000, actor, tested->world, nullptr).HasValue());
    EXPECT_EQ(actor.flags78, 0x10000u);
    EXPECT_EQ(actor.flags7c, 0u - 0x10000u);
}

// 0x4e6910 turns the effects off through the hooks: without them, a flag with effects lowers its bit and stops, and
// one without effects does not need them.
TEST(SkillRepliesTest, AFlagWithEffectsStopsWithoutTheEffectHooks)
{
    auto tested = std::make_unique<Replies>();
    ActorState& actor = tested->Actor(CasterIndex);
    actor.flags78 = 0x11;
    const auto stopped = Client::Actors::RemoveActorFlag(0x10, actor, tested->world, nullptr);
    ASSERT_FALSE(stopped.HasValue());
    EXPECT_NE(stopped.Error().find("0x4e6910"), std::string::npos) << stopped.Error();
    EXPECT_EQ(actor.flags78, 0x1u);
    ASSERT_TRUE(Client::Actors::RemoveActorFlag(0x1, actor, tested->world, nullptr).HasValue());
    EXPECT_EQ(actor.flags78, 0u);
}

// ---------------------------------------------------------------------------------------------------------------
// 0x69 (0x4cc290) and 0x48 (0x4dbf50)

TEST(SkillRepliesTest, TheSkillTargetKeepsTheSkillAndTheTarget)
{
    auto tested = std::make_unique<Replies>();
    const CrtRandom expected = Advanced(tested->cloth.random, ActorByteRandoms);
    ASSERT_TRUE(
        Client::Skills::ApplySkillTarget(SkillTarget{0x20, CasterKey, TargetKey}, tested->Resources()).HasValue());
    const ActorState& caster = tested->Actor(CasterIndex);
    EXPECT_EQ(caster.byte42e, 0x20);
    EXPECT_EQ(caster.attackTarget, TargetIndex);
    EXPECT_EQ(caster.skillSucceeded, 0);
    EXPECT_EQ(tested->world.Runtime().lastAttacker, static_cast<std::int32_t>(CasterIndex));
    EXPECT_EQ(tested->cloth.random.State(), expected.State());

    // With bit 15, 0x5110d0 does not find it: nothing.
    ASSERT_TRUE(Client::Skills::ApplySkillTarget(
                    SkillTarget{0x21, CasterKey, static_cast<std::uint16_t>(TargetKey | 0x8000)}, tested->Resources())
                    .HasValue());
    EXPECT_EQ(caster.byte42e, 0x20);
}

// 0x4cc503..0x4cc51c: with skills 1 to 0xd, 0x4f4a10 (an unarmed player: 0x4f3f80's 3848 rand() and 0x22), the frame
// at 0 and +0x381 at 1; with 0 or 0xe, none of that.
TEST(SkillRepliesTest, TheSkillTargetOfTheFirstSkillsIsTheAttackAction)
{
    for (const std::uint8_t skill : {std::uint8_t{1}, std::uint8_t{0xd}})
    {
        auto tested = std::make_unique<Replies>();
        ActorState& caster = tested->Actor(CasterIndex);
        caster.animation.frame = 2.0f;
        const CrtRandom expected = Advanced(tested->cloth.random, ActorByteRandoms + OwnRecordRandoms);
        ASSERT_TRUE(
            Client::Skills::ApplySkillTarget(SkillTarget{skill, CasterKey, TargetKey}, tested->Resources()).HasValue());
        EXPECT_EQ(caster.byte42e, skill);
        EXPECT_EQ(caster.animation.action, 0x22);
        EXPECT_EQ(caster.attackCount, 1);
        EXPECT_EQ(caster.animation.frame, 0.0f);
        EXPECT_EQ(caster.byte381, 1);
        EXPECT_EQ(tested->cloth.random.State(), expected.State());
    }
    for (const std::uint8_t skill : {std::uint8_t{0}, std::uint8_t{0xe}})
    {
        auto tested = std::make_unique<Replies>();
        ASSERT_TRUE(
            Client::Skills::ApplySkillTarget(SkillTarget{skill, CasterKey, TargetKey}, tested->Resources()).HasValue());
        EXPECT_EQ(tested->Actor(CasterIndex).byte381, 0);
        EXPECT_EQ(tested->Actor(CasterIndex).attackCount, 0);
    }
}

// Q-C1237: a missing source is the 400 (0x4cc2b0): 0x5767964, the skill in +0x42e (its rand()), +0x3a4 and +0x38d,
// and with skills 1 to 0xd the frame at 0 and +0x381 at 1; the 400's 0x4f4a10, which comes from whatever type is
// there, is discarded without cutting and without rand(). In Lorencia and Devias.
TEST(SkillRepliesTest, TheSkillTargetOfAnUnknownSourceWritesTheLastActor)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        auto tested = std::make_unique<Replies>();
        tested->world.Runtime().map = map;
        ActorState& phantom = tested->Actor(400);
        phantom.animation.frame = 2.0f;
        const std::uint8_t action = phantom.animation.action;
        const CrtRandom expected = Advanced(tested->cloth.random, ActorByteRandoms);
        ASSERT_TRUE(
            Client::Skills::ApplySkillTarget(SkillTarget{1, 0x0777, TargetKey}, tested->Resources()).HasValue());
        EXPECT_EQ(tested->world.Runtime().lastAttacker, 400);
        EXPECT_EQ(phantom.byte42e, 1);
        EXPECT_EQ(phantom.attackTarget, TargetIndex);
        EXPECT_EQ(phantom.skillSucceeded, 0);
        EXPECT_EQ(phantom.animation.action, action);
        EXPECT_EQ(phantom.attackCount, 0);
        EXPECT_EQ(phantom.animation.frame, 0.0f);
        EXPECT_EQ(phantom.byte381, 1);
        EXPECT_EQ(tested->cloth.random.State(), expected.State());
    }
}

// 0x4dbfad..0x4dc004: a full pool does not interrupt the fifteen attempts; the sound always comes last.
TEST(SkillRepliesTest, TheLevelUpEffectRespectsBothPoolsAndContinuesToItsSound)
{
    for (const int freeJoints : {0, 1, 7, 15, 16})
    {
        for (const bool effectsFull : {false, true})
        {
            SCOPED_TRACE(testing::Message() << freeJoints << " joints libres; efectos llenos " << effectsFull);
            auto tested = std::make_unique<Replies>();
            ActorState& actor = tested->Actor(CasterIndex);
            actor.byte88 = 1;
            actor.light = {0.3f, 0.5f, 0.7f};
            auto& joints = tested->world.DrawTail().joints;
            for (std::size_t i = 0; i < joints.size(); ++i)
                joints[i].alive = i >= static_cast<std::size_t>(freeJoints);
            auto effects = tested->world.Map().effects.Effects();
            for (auto& effect : effects)
                effect.alive = effectsFull;
            const int created = std::min(freeJoints, 15);
            const auto expected = Advanced(tested->cloth.random, 3 * created);

            const auto applied = Client::Skills::ApplyActorEffect(ActorEffect{CasterKey, 0x10}, tested->Resources());

            ASSERT_TRUE(applied.HasValue()) << applied.Error();
            EXPECT_EQ(tested->cloth.random.State(), expected.State());
            EXPECT_EQ(tested->world.Runtime().lastSoundId, Client::Audio::SoundId{0x98});
            for (int i = 0; i < created; ++i)
            {
                const auto& joint = joints[static_cast<std::size_t>(i)];
                EXPECT_TRUE(joint.alive);
                EXPECT_EQ(joint.type, 0x60f);
                EXPECT_EQ(joint.subtype, 0);
                EXPECT_EQ(joint.key, 2);
                EXPECT_EQ(joint.width, 40.0f);
                EXPECT_EQ(joint.ownerContext, &actor);
                EXPECT_EQ(joint.light, actor.light);
            }
            if (freeJoints > created)
            {
                EXPECT_FALSE(joints[static_cast<std::size_t>(created)].alive);
            }
            if (!effectsFull)
            {
                const auto& decal = effects.front();
                EXPECT_TRUE(decal.alive);
                EXPECT_EQ(decal.type, 0x61e);
                EXPECT_EQ(decal.subtype, 0);
                EXPECT_EQ(decal.life, 20);
                EXPECT_EQ(decal.ownerContext, &actor);
                EXPECT_EQ(decal.transform.position, actor.position);
                EXPECT_EQ(decal.transform.angles, actor.angles);
                EXPECT_EQ(decal.light, actor.light);
                EXPECT_FALSE(effects[1].alive);
            }
        }
    }
}

TEST(SkillRepliesTest, LevelUpUpdatesTheOwnRecordBeforeEffectsWithoutActorGuards)
{
    for (const std::uint16_t level : {std::uint16_t{255}, std::uint16_t{256}, std::uint16_t{65535}})
    {
        for (const bool full : {false, true})
        {
            auto tested = std::make_unique<Replies>();
            auto& own = tested->world.OwnCharacter();
            own.experience = 123;
            own.bp = 13;
            own.word48 = 99;
            own.word4a = 99;
            own.level = level;
            auto& actor = tested->Actor(HeroIndex);
            actor.byte88 = 2;
            actor.alive = false;
            for (auto& joint : tested->world.DrawTail().joints)
                joint.alive = full;
            for (auto& effect : tested->world.Map().effects.Effects())
                effect.alive = full;
            auto expected = Advanced(tested->cloth.random, OwnRecordRandoms);
            if (!full)
                expected = Advanced(expected, 45);
            const auto applied = Client::Skills::ApplyLevelUp(
                Client::Protocol::Skills::LevelUp{level, 2, 3, 4, 5, 6, 7}, tested->Resources());
            ASSERT_TRUE(applied.HasValue()) << applied.Error();
            EXPECT_EQ(own.level, level);
            EXPECT_EQ(own.levelUpPoints, 2);
            EXPECT_EQ(own.life, 3);
            EXPECT_EQ(own.maxLife, 3);
            EXPECT_EQ(own.mana, 4);
            EXPECT_EQ(own.maxMana, 4);
            EXPECT_EQ(own.maxBp, 5);
            EXPECT_EQ(own.addedPoints, 6);
            EXPECT_EQ(own.maxAddedPoints, 7);
            EXPECT_EQ(own.word48, 0);
            EXPECT_EQ(own.word4a, 0);
            EXPECT_EQ(own.bp, 13);
            EXPECT_EQ(own.experience, 123u);
            const std::uint64_t value = level;
            const std::uint64_t extra = level > 255 ? value - 255 : 0;
            EXPECT_EQ(own.nextExperience, static_cast<std::uint32_t>(10 * (value + 9) * value * value +
                                                                     1000 * (extra + 9) * extra * extra));
            EXPECT_EQ(tested->world.Runtime().ownRecordDepth, 0);
            EXPECT_EQ(tested->cloth.random.State(), expected.State());
            EXPECT_EQ(tested->world.Runtime().lastSoundId, Client::Audio::SoundId{0x98});
            if (!full)
            {
                EXPECT_EQ(tested->world.DrawTail().joints.front().ownerContext, &actor);
                EXPECT_EQ(tested->world.Map().effects.Effects().front().ownerContext, &actor);
            }
        }
    }
}

TEST(SkillRepliesTest, TheLevelUpEffectChecksItsActorAndKindBeforeEitherPool)
{
    auto tested = std::make_unique<Replies>();
    tested->Actor(CasterIndex).byte88 = 1;
    const auto before = tested->cloth.random.State();
    EXPECT_TRUE(Client::Skills::ApplyActorEffect(ActorEffect{CasterKey, 0x11}, tested->Resources()).HasValue());
    EXPECT_TRUE(Client::Skills::ApplyActorEffect(ActorEffect{0x0777, 0x10}, tested->Resources()).HasValue());
    tested->Actor(CasterIndex).byte88 = 2;
    EXPECT_TRUE(Client::Skills::ApplyActorEffect(ActorEffect{CasterKey, 0x10}, tested->Resources()).HasValue());
    EXPECT_EQ(tested->cloth.random.State(), before);
    EXPECT_FALSE(tested->world.Map().effects.Effects().front().alive);
    EXPECT_FALSE(tested->world.DrawTail().joints.front().alive);
}

// ---------------------------------------------------------------------------------------------------------------
// F3 11 (0x4c5070) and F3 20

// The whole list: with the list at 0, the 256 at 0, the entries and the count of the first 64, with their Skill.bmd
// entry opened and closed.
TEST(SkillRepliesTest, TheWholeSkillListCountsTheShownAndTheHidden)
{
    auto tested = std::make_unique<Replies>();
    const auto skills = SkillsHiding({0x29});
    tested->skills = &skills;
    auto& own = tested->world.OwnCharacter();
    own.SetSkillAt(9, 0x33);
    own.SetSkillAt(0x90, 0x34);
    const CrtRandom expected =
        Advanced(tested->cloth.random, OwnRecordRandoms + 2 * SkillEntryRandoms + OwnRecordRandoms);
    const SkillList list{
        .count = 2, .list = 0, .slot = 0, .skill = 0, .entries = {SkillListEntry{0, 0x11}, SkillListEntry{1, 0x29}}};
    ASSERT_TRUE(Client::Skills::ApplySkillList(list, tested->Resources()).HasValue());
    EXPECT_EQ(own.SkillAt(0), 0x11);
    EXPECT_EQ(own.SkillAt(1), 0x29);
    EXPECT_EQ(own.SkillAt(9), 0);
    EXPECT_EQ(own.SkillAt(0x90), 0);
    EXPECT_EQ(own.byte6a, 2);
    EXPECT_EQ(own.byte6b, 1);
    EXPECT_EQ(tested->world.Runtime().ownRecordDepth, 0);
    EXPECT_EQ(tested->cloth.random.State(), expected.State());
}

// 0x4c539b: the Dark Lord gets the raven's four orders in 0x78 to 0x7b, which do not count.
TEST(SkillRepliesTest, TheDarkLordGetsTheRavenOrders)
{
    auto tested = std::make_unique<Replies>();
    const auto skills = SkillsHiding({});
    tested->skills = &skills;
    tested->Actor(HeroIndex).characterClass = 4;
    const SkillList list{.count = 1, .list = 1, .slot = 0, .skill = 0, .entries = {SkillListEntry{0, 0x3c}}};
    ASSERT_TRUE(Client::Skills::ApplySkillList(list, tested->Resources()).HasValue());
    const auto& own = tested->world.OwnCharacter();
    for (std::uint8_t order = 0x78; order < 0x7c; ++order)
        EXPECT_EQ(own.SkillAt(order), order);
    EXPECT_EQ(own.byte6a, 1);
}

// 0xfe adds and 0xff removes, and the hero's +0x425 goes back to 0 without a skill and skips 0x43 to 0x48.
TEST(SkillRepliesTest, OneSkillAddedOrRemovedFixesTheHeroSkill)
{
    auto tested = std::make_unique<Replies>();
    const auto skills = SkillsHiding({});
    tested->skills = &skills;
    auto& own = tested->world.OwnCharacter();
    own.SetSkillAt(0, 0x43);
    own.SetSkillAt(1, 0x48);
    own.SetSkillAt(2, 0x11);
    ActorState& hero = tested->Actor(HeroIndex);
    hero.skillIndex = 0;
    const SkillList add{.count = 0xfe, .list = 0, .slot = 3, .skill = 0x12, .entries = {}};
    ASSERT_TRUE(Client::Skills::ApplySkillList(add, tested->Resources()).HasValue());
    EXPECT_EQ(own.SkillAt(3), 0x12);
    EXPECT_EQ(own.byte6a, 4);
    EXPECT_EQ(hero.skillIndex, 2);

    hero.skillIndex = 3;
    const SkillList remove{.count = 0xff, .list = 0, .slot = 3, .skill = 0, .entries = {}};
    ASSERT_TRUE(Client::Skills::ApplySkillList(remove, tested->Resources()).HasValue());
    EXPECT_EQ(own.SkillAt(3), 0);
    EXPECT_EQ(own.byte6a, 3);
    // 3 >= +0x6a: to 0, and from there it skips the Dark Lord's two.
    EXPECT_EQ(hero.skillIndex, 2);
}

TEST(SkillRepliesTest, TheListTwoClearsTheSlot)
{
    auto tested = std::make_unique<Replies>();
    const auto skills = SkillsHiding({});
    tested->skills = &skills;
    auto& own = tested->world.OwnCharacter();
    own.SetSkillAt(5, 0x11);
    own.SetSkillAt(6, 0x12);
    const SkillList clear{.count = 1, .list = 2, .slot = 5, .skill = 0, .entries = {}};
    ASSERT_TRUE(Client::Skills::ApplySkillList(clear, tested->Resources()).HasValue());
    EXPECT_EQ(own.SkillAt(5), 0);
    EXPECT_EQ(own.SkillAt(6), 0x12);
    EXPECT_EQ(own.byte6a, 1);
}

// The empty F3 11 with list=2 keeps the slots and does not add the DL orders, but recounts through two accesses.
TEST(SkillRepliesTest, TheEmptyClearListStillRecountsAndClosesBothAccesses)
{
    for (const std::uint8_t depth : {std::uint8_t{0}, std::uint8_t{1}})
    {
        auto tested = std::make_unique<Replies>();
        const auto skills = SkillsHiding({0x12});
        tested->skills = &skills;
        tested->Actor(HeroIndex).characterClass = 4;
        tested->Actor(HeroIndex).skillIndex = 9;
        auto& own = tested->world.OwnCharacter();
        own.SetSkillAt(0, 0x12);
        own.SetSkillAt(64, 0x11);
        tested->world.Runtime().ownRecordDepth = depth;
        const auto expected =
            Advanced(tested->cloth.random, (depth == 0 ? 2 * OwnRecordRandoms : 0) + SkillEntryRandoms);
        const auto read = Client::Protocol::Skills::ReadSkillList(
            Client::Tests::PacketOf(Client::Tests::Bytes({0xc1, 6, 0xf3, 0x11, 0, 2})));

        ASSERT_TRUE(read.HasValue());
        ASSERT_TRUE(Client::Skills::ApplySkillList(read.Value(), tested->Resources()).HasValue());

        EXPECT_EQ(own.SkillAt(0), 0x12);
        EXPECT_EQ(own.SkillAt(64), 0x11);
        EXPECT_EQ(own.SkillAt(0x78), 0);
        EXPECT_EQ(own.byte6a, 1);
        EXPECT_EQ(own.byte6b, 1);
        EXPECT_EQ(tested->Actor(HeroIndex).skillIndex, 0);
        EXPECT_EQ(tested->world.Runtime().ownRecordDepth, depth);
        EXPECT_EQ(tested->cloth.random.State(), expected.State());
    }
}

TEST(SkillRepliesTest, TheSkillListWithoutSkillBmdIsACut)
{
    auto tested = std::make_unique<Replies>();
    const SkillList add{.count = 0xfe, .list = 0, .slot = 0, .skill = 0x11, .entries = {}};
    const auto applied = Client::Skills::ApplySkillList(add, tested->Resources());
    ASSERT_FALSE(applied.HasValue());
    EXPECT_NE(applied.Error().find("0x4c54ed"), std::string::npos) << applied.Error();
    EXPECT_EQ(tested->world.OwnCharacter().SkillAt(0), 0x11);
}

TEST(SkillRepliesTest, TheSummonLifeGoesToTheHud)
{
    auto tested = std::make_unique<Replies>();
    Client::Skills::ApplySummonLife(SummonLife{0x42}, tested->Resources());
    EXPECT_EQ(tested->world.Runtime().hud.summonLife, 0x42);
}
} // namespace
