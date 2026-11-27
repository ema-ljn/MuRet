#include "Actors/ActorWalk.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/ItemTable.h"
#include "Models/Skeleton.h"
#include "Kernel/ActorState.h"
#include "Actors/PlayerActionSpeeds.h"
#include "Scenes/SceneId.h"
#include "Kernel/WorldRuntime.h"
#include "support/PlayerActors.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace
{
class ActorWalkTest : public ::testing::Test
{
protected:
    void Walk(Client::Kernel::ActorState& actor, bool hero = true)
    {
        auto walked = Client::Actors::SetActorWalk(
            actor, {{m_runtime, Client::Scenes::SceneId::Game, m_skeleton, &m_items, m_random}, m_speeds, hero});
        ASSERT_TRUE(walked.HasValue()) << (walked.HasValue() ? "" : walked.Error());
    }
    Client::Kernel::WorldRuntime m_runtime = []
    {
        Client::Kernel::WorldRuntime value{};
        value.map = 0;
        return value;
    }();
    Client::Models::Skeleton m_skeleton = Client::Tests::PlayerActionsSkeleton();
    Client::Tables::ItemTable m_items = Client::Tests::ItemsMarking({0x213, 0x227, 0xc12});
    Client::Crt::CrtRandom m_random{1};
    Client::Actors::PlayerActionSpeeds m_speeds;
};

TEST_F(ActorWalkTest, RunRequiresClassOrTheMapSpecificLevelFivePart)
{
    for (std::int32_t map : {0, 3, 7, 24, 29, 36})
    {
        m_runtime.map = map;
        for (int characterClass : {0, 1, 2, 3, 4, 11, 12})
        {
            auto actor = Client::Tests::UnarmedPlayer(static_cast<std::uint8_t>(characterClass));
            Walk(actor);
            EXPECT_EQ(actor.run, (characterClass & 7) == 3 || (characterClass & 7) == 4 ? 1 : 0);
        }
        auto boots = Client::Tests::UnarmedPlayer();
        boots.body[4].model = 0x1412;
        boots.body[4].level = 5;
        Walk(boots);
        EXPECT_EQ(boots.run, map == 0 || map == 3 ? 1 : 0);
        auto gloves = Client::Tests::UnarmedPlayer();
        gloves.body[3].model = 0x1212;
        gloves.body[3].level = 5;
        Walk(gloves);
        EXPECT_EQ(gloves.run, map == 0 || map == 3 ? 0 : 1);
    }
}

TEST_F(ActorWalkTest, SafeZoneResetsRunAndTheFortiethStepChangesAction)
{
    auto actor = Client::Tests::UnarmedPlayer(3);
    actor.run = 39;
    Walk(actor);
    EXPECT_EQ(actor.run, 40);
    EXPECT_EQ(actor.animation.action, 0x16);
    actor.safeZone = 0x80;
    Walk(actor);
    EXPECT_EQ(actor.run, 0);
    EXPECT_EQ(actor.animation.action, 0xd);
}

TEST_F(ActorWalkTest, SharedWalkingSpeedsOverwriteThePriorActorAndPreserveAction21)
{
    auto actor = Client::Tests::UnarmedPlayer();
    actor.flags78 = 2;
    Walk(actor);
    for (unsigned action : {13U, 20U, 73U, 75U})
        EXPECT_FLOAT_EQ(m_speeds.Speed(action, 9.0f), 0.165f);
    for (unsigned action : {22U, 33U})
        EXPECT_FLOAT_EQ(m_speeds.Speed(action, 9.0f), 0.17f);
    EXPECT_FLOAT_EQ(m_speeds.Speed(21, 0.35f), 0.35f);
    actor.flags78 = 0;
    Walk(actor);
    EXPECT_FLOAT_EQ(m_speeds.Speed(13, 9.0f), 0.33f);
    EXPECT_FLOAT_EQ(m_speeds.Speed(22, 9.0f), 0.34f);
    EXPECT_FLOAT_EQ(m_skeleton.actions[13].speed, 0.0f);
}

TEST_F(ActorWalkTest, OrdinaryHeroAndOtherEachConsumeOneRandom)
{
    auto actor = Client::Tests::UnarmedPlayer();
    Walk(actor, true);
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(1, 1).State());
    Walk(actor, false);
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(1, 2).State());
}

TEST_F(ActorWalkTest, FenrirCounterGrowsInsideRunGuardAndUsesItsSoundRandom)
{
    auto actor = Client::Tests::UnarmedPlayer();
    actor.pet.model = 0x1c37;
    m_runtime.movementSpeed.itemCounterBits = 19;
    Walk(actor);
    EXPECT_EQ(m_runtime.movementSpeed.itemCounterBits, 20U);
    EXPECT_EQ(actor.run, 0);
    EXPECT_EQ(actor.animation.action, 0x6a);
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(1, 1).State());
    actor.run = 255;
    Walk(actor);
    EXPECT_EQ(m_runtime.movementSpeed.itemCounterBits, 20U);
}

TEST_F(ActorWalkTest, WeaponBranchesPreserveSignedRangesAndTheRunningDualHandShortcut)
{
    struct Example
    {
        std::uint16_t right;
        std::uint16_t left;
        std::uint8_t run;
        int action;
    };
    for (const Example sample : {Example{0x212, 0xffff, 0, 0xf},
                                 {0x213, 0xffff, 0, 0x10},
                                 {0x227, 0xffff, 0, 0x8b},
                                 {0x813, 0xffff, 0, 0x11},
                                 {0xa14, 0xffff, 0, 0x11},
                                 {0x814, 0xffff, 0, 0x12},
                                 {0xc12, 0xffff, 0, 0x12},
                                 {0xc13, 0xffff, 0, 0xf},
                                 {0xffff, 0xa12, 0, 0x13},
                                 {0xa1a, 0xffff, 0, 0x14},
                                 {0xfffe, 0xffff, 0, 0xd},
                                 {0x212, 0x213, 40, 0x18},
                                 {0x213, 0xffff, 40, 0x19},
                                 {0x227, 0xffff, 40, 0x8c},
                                 {0x813, 0xffff, 40, 0x1a},
                                 {0xffff, 0xa12, 40, 0x1b},
                                 {0xa1a, 0xffff, 40, 0x1c}})
    {
        SCOPED_TRACE(sample.right);
        auto actor = Client::Tests::UnarmedPlayer();
        actor.hands[0].model = sample.right;
        actor.hands[1].model = sample.left;
        actor.run = sample.run;
        Walk(actor);
        EXPECT_EQ(actor.animation.action, sample.action);
    }
}

TEST_F(ActorWalkTest, MountedFlyingSwimmingAndSafeRavenBranchesSelectBeforeWeapons)
{
    struct Example
    {
        int map;
        std::uint16_t pet;
        std::uint16_t wings;
        int safe;
        int action;
    };
    for (const Example sample : {Example{0, 0x1c16, 0xffff, 0, 0x4b},
                                 {0, 0x1c14, 0xffff, 0, 0x20},
                                 {0, 0x1c15, 0xffff, 0, 0x20},
                                 {8, 0x1c15, 0xffff, 0, 0x55},
                                 {10, 0x1c15, 0xffff, 0, 0x55},
                                 {0, 0xffff, 0x1a12, 0, 0x1e},
                                 {7, 0xffff, 0xffff, 0, 0x15},
                                 {24, 0xffff, 0xffff, 0, 0x15},
                                 {36, 0xffff, 0xffff, 0, 0x15},
                                 {0, 0x1c16, 0x1a12, 128, 0xd}})
    {
        auto actor = Client::Tests::UnarmedPlayer();
        actor.animation.action = 0x55;
        actor.pet.model = sample.pet;
        actor.wings.model = sample.wings;
        actor.safeZone = static_cast<std::uint8_t>(sample.safe);
        m_runtime.map = sample.map;
        Walk(actor);
        EXPECT_EQ(actor.animation.action, sample.action);
    }
    auto raven = Client::Tests::UnarmedPlayer();
    raven.hands[1].model = 0x1c17;
    raven.safeZone = 1;
    Walk(raven);
    EXPECT_EQ(raven.animation.action, 0x49);
}

TEST_F(ActorWalkTest, MissingWeaponTableFailsAfterRunAndSharedSpeedWritesWithoutSoundRandom)
{
    auto actor = Client::Tests::UnarmedPlayer(3);
    actor.hands[0].model = 0x213;
    actor.run = 39;
    const auto walked = Client::Actors::SetActorWalk(
        actor, {{m_runtime, Client::Scenes::SceneId::Game, m_skeleton, nullptr, m_random}, m_speeds, true});
    EXPECT_FALSE(walked.HasValue());
    EXPECT_EQ(actor.run, 40);
    EXPECT_FLOAT_EQ(m_speeds.Speed(22, 9.0f), 0.34f);
    EXPECT_EQ(m_random.State(), 1U);
}

TEST_F(ActorWalkTest, NonplayerUsesItsOwnSkeletonWithoutWritingPlayerSpeeds)
{
    auto actor = Client::Tests::UnarmedPlayer(3);
    actor.type = 0xa9;
    m_speeds.SetWalking(true);
    const auto walked = Client::Actors::SetActorWalk(
        actor,
        {{m_runtime, Client::Scenes::SceneId::Game, m_skeleton, nullptr, m_random}, m_speeds, true, &m_skeleton});
    ASSERT_TRUE(walked.HasValue()) << (walked.HasValue() ? "" : walked.Error());
    EXPECT_EQ(actor.animation.action, 2);
    EXPECT_EQ(actor.run, 1);
    EXPECT_FLOAT_EQ(m_speeds.Speed(13, 9.0f), 0.165f);
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(1, 1).State());
}

TEST_F(ActorWalkTest, MissingNonplayerSkeletonLeavesTheModelActionAloneAndKeepsWalking)
{
    auto actor = Client::Tests::UnarmedPlayer(3);
    actor.type = 0x154;
    actor.animation.action = 1;
    const std::array<std::int16_t, 10> fallbackSounds{};
    m_speeds.SetWalking(true);

    const auto walked =
        Client::Actors::SetActorWalk(actor, {{m_runtime, Client::Scenes::SceneId::Game, m_skeleton, nullptr, m_random},
                                             m_speeds,
                                             true,
                                             nullptr,
                                             fallbackSounds});

    ASSERT_TRUE(walked.HasValue()) << (walked.HasValue() ? "" : walked.Error());
    EXPECT_EQ(actor.animation.action, 1);
    EXPECT_EQ(actor.run, 1);
    EXPECT_FLOAT_EQ(m_speeds.Speed(13, 9.0f), 0.165f);
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(1, 1).State());
}

TEST_F(ActorWalkTest, Type16dRequestsItsUnconditionalSoundWithoutDrawingRandom)
{
    auto actor = Client::Tests::UnarmedPlayer();
    actor.type = 0x16d;
    const auto walked = Client::Actors::SetActorWalk(
        actor,
        {{m_runtime, Client::Scenes::SceneId::Game, m_skeleton, nullptr, m_random}, m_speeds, true, &m_skeleton});
    ASSERT_TRUE(walked.HasValue()) << (walked.HasValue() ? "" : walked.Error());
    ASSERT_TRUE(walked.Value().sound.has_value());
    EXPECT_EQ(*walked.Value().sound, Client::Audio::SoundId{0x7c});
    EXPECT_EQ(m_random.State(), 1U);
}

// 0x4f3edc..0x4f3f6c: a non-player, with rand() & 15 at 0, asks for WORD 0 or 1 of the table its creation wrote (rand()
// % 2, plus 5 with subtype 9 and plus 0xd2); with WORD 0 at −1, neither, and without the second rand(); without the
// table, the zeroed WORDs of 0x60dc80: the rand() and 0xd2.
TEST_F(ActorWalkTest, AMonsterChosenToSoundDrawsItsWalkWord)
{
    std::uint32_t seed = 1;
    while ((Client::Crt::CrtRandom(seed).Next() & 15) != 0)
        ++seed;
    constexpr std::uint16_t Model = 0x15b;
    const auto walk = [this, seed](std::int32_t subtype)
    {
        m_random = Client::Crt::CrtRandom(seed);
        auto actor = Client::Tests::UnarmedPlayer(3);
        actor.type = Model;
        actor.subtype = subtype;
        return Client::Actors::SetActorWalk(
            actor,
            {{m_runtime, Client::Scenes::SceneId::Game, m_skeleton, nullptr, m_random}, m_speeds, false, &m_skeleton});
    };

    const auto unwritten = walk(0);
    ASSERT_TRUE(unwritten.HasValue()) << (unwritten.HasValue() ? "" : unwritten.Error());
    EXPECT_EQ(unwritten.Value().sound, Client::Audio::SoundId{0xd2});
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(seed, 2).State());

    m_runtime.modelSounds[Model] = {0x30, 0x31, -1, -1, -1, -1, -1, -1, -1, -1};
    const auto ninth = walk(9);
    ASSERT_TRUE(ninth.HasValue()) << (ninth.HasValue() ? "" : ninth.Error());
    auto expected = Client::Tests::AfterDraws(seed, 1);
    const std::int32_t word = expected.Next() % 2;
    EXPECT_EQ(ninth.Value().sound, Client::Audio::SoundId{0x30 + word + 5 + 0xd2});
    EXPECT_EQ(m_random.State(), expected.State());

    m_runtime.modelSounds[Model][0] = -1;
    const auto silent = walk(0);
    ASSERT_TRUE(silent.HasValue()) << (silent.HasValue() ? "" : silent.Error());
    EXPECT_FALSE(silent.Value().sound.has_value());
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(seed, 1).State());
}

} // namespace

TEST(ActorFootstepTest, ThresholdsUsePersistentLatchesAndDoNotDrawRandom)
{
    auto actor = Client::Tests::UnarmedPlayer();
    actor.animation.action = 13;
    actor.animation.frame = 1.49f;
    auto sounds = Client::Actors::TakeActorFootstepSounds(actor, 0, 0);
    EXPECT_FALSE(sounds[0]);
    EXPECT_EQ(actor.firstFootstepSound, 0);
    actor.animation.frame = 1.5f;
    sounds = Client::Actors::TakeActorFootstepSounds(actor, 0, 0);
    EXPECT_EQ(sounds[0], 9);
    EXPECT_EQ(actor.firstFootstepSound, 1);
    EXPECT_EQ(actor.secondFootstepSound, 0);
    sounds = Client::Actors::TakeActorFootstepSounds(actor, 0, 0);
    EXPECT_FALSE(sounds[0]);
    actor.animation.frame = 4.5f;
    sounds = Client::Actors::TakeActorFootstepSounds(actor, 2, 0);
    EXPECT_FALSE(sounds[0]);
    EXPECT_EQ(sounds[1], 10);
    EXPECT_EQ(actor.secondFootstepSound, 1);
}

TEST(ActorFootstepTest, FlyingActionsSetTheLatchesButSuppressSounds)
{
    auto actor = Client::Tests::UnarmedPlayer();
    actor.animation.action = 0x1e;
    actor.animation.frame = 5.0f;
    const auto sounds = Client::Actors::TakeActorFootstepSounds(actor, 0, 0);
    EXPECT_FALSE(sounds[0]);
    EXPECT_FALSE(sounds[1]);
    EXPECT_EQ(actor.firstFootstepSound, 1);
    EXPECT_EQ(actor.secondFootstepSound, 1);
}

TEST(ActorFootstepTest, MapLayerAndSafeZoneChooseTheNativeSound)
{
    struct Sample
    {
        int map;
        int layer;
        int safe;
        int sound;
    };
    for (const Sample sample : {Sample{0, 0, 0, 9},
                                {0, 1, 0, 8},
                                {3, 0, 0, 9},
                                {2, 3, 0, 8},
                                {2, 9, 0, 10},
                                {2, 10, 0, 8},
                                {7, 0, 0, 11},
                                {7, 0, 1, 8},
                                {24, 0, 0, 11},
                                {29, 0, 0, 11},
                                {36, 0, 0, 11},
                                {1, 0, 0, 8}})
    {
        auto actor = Client::Tests::UnarmedPlayer();
        actor.animation.action = 13;
        actor.animation.frame = 5.0f;
        actor.safeZone = static_cast<std::uint8_t>(sample.safe);
        const auto sounds = Client::Actors::TakeActorFootstepSounds(actor, sample.map, sample.layer);
        EXPECT_EQ(sounds[0], sample.sound);
        EXPECT_EQ(sounds[1], sample.sound);
    }
}

// 0x4f3efa..0x4f3f67: a model other than 0x20a, chosen with rand() & 15, sounds with its model's table (+0xaa, that of
// 0x612c80): one of the first two by rand() & 1, plus 0xd2 (and 5 with subtype 9). With the first at −1, nothing;
// without the table or WorldRuntime::modelSounds, the zeroed WORDs of 0x60dc80: the rand() and 0xd2 (0xd7 with 9).
TEST_F(ActorWalkTest, AnotherModelSoundsFromItsModelTable)
{
    const std::array<std::int16_t, 10> table{0x10, 0x11, 0x12, 0x13, 0x14, -1, -1, -1, -1, -1};
    const std::array<std::int16_t, 10> silent{-1, 0x11, 0x12, 0x13, 0x14, -1, -1, -1, -1, -1};
    int sounded = 0;
    int drawnTwice = 0;
    for (std::uint32_t seed = 1; seed < 200; ++seed)
    {
        SCOPED_TRACE(seed);
        for (const std::int32_t subtype : {0, 9})
        {
            auto actor = Client::Tests::UnarmedPlayer(3);
            actor.type = 0x153;
            actor.subtype = subtype;
            Client::Crt::CrtRandom random(seed);
            const auto walked = Client::Actors::SetActorWalk(
                actor, {{m_runtime, Client::Scenes::SceneId::Game, m_skeleton, nullptr, random},
                        m_speeds,
                        false,
                        &m_skeleton,
                        table});
            ASSERT_TRUE(walked.HasValue()) << walked.Error();
            if (walked.Value().sound.has_value())
            {
                ++sounded;
                const std::int32_t base = subtype == 9 ? 0xd2 + 5 : 0xd2;
                EXPECT_TRUE(walked.Value().sound->Value() == base + 0x10 ||
                            walked.Value().sound->Value() == base + 0x11);
            }

            Client::Crt::CrtRandom quiet(seed);
            const auto muted = Client::Actors::SetActorWalk(
                actor, {{m_runtime, Client::Scenes::SceneId::Game, m_skeleton, nullptr, quiet},
                        m_speeds,
                        false,
                        &m_skeleton,
                        silent});
            ASSERT_TRUE(muted.HasValue()) << muted.Error();
            EXPECT_FALSE(muted.Value().sound.has_value());

            Client::Crt::CrtRandom missing(seed);
            const auto unknown = Client::Actors::SetActorWalk(
                actor, {{m_runtime, Client::Scenes::SceneId::Game, m_skeleton, nullptr, missing},
                        m_speeds,
                        false,
                        &m_skeleton});
            ASSERT_TRUE(unknown.HasValue()) << unknown.Error();
            EXPECT_EQ(unknown.Value().sound.has_value(), walked.Value().sound.has_value());
            if (unknown.Value().sound.has_value())
            {
                EXPECT_EQ(*unknown.Value().sound, Client::Audio::SoundId{subtype == 9 ? 0xd2 + 5 : 0xd2});
            }
            if (walked.Value().sound.has_value())
            {
                ++drawnTwice;
                EXPECT_EQ(missing.State(), random.State());
            }
        }
    }
    EXPECT_GT(sounded, 0);
    EXPECT_EQ(drawnTwice, sounded);
}

// 0x4f3edc..0x4f3ef2: with subtypes 209 to 211, before the table, 0x41ed10(0x7b) without rand(): if the table does not
// sound, 0x7b stays; if it does, its request is the last. The rand() are those of subtype 0.
TEST_F(ActorWalkTest, TheSubtypes209To211AskForTheirLeadSoundBeforeTheTable)
{
    const std::array<std::int16_t, 10> table{0x10, 0x11, 0x12, 0x13, 0x14, -1, -1, -1, -1, -1};
    const std::array<std::int16_t, 10> silent{-1, 0x11, 0x12, 0x13, 0x14, -1, -1, -1, -1, -1};
    int sounded = 0;
    for (std::uint32_t seed = 1; seed < 200; ++seed)
    {
        SCOPED_TRACE(seed);
        auto plain = Client::Tests::UnarmedPlayer(3);
        plain.type = 0x153;
        Client::Crt::CrtRandom reference(seed);
        const auto expected = Client::Actors::SetActorWalk(
            plain, {{m_runtime, Client::Scenes::SceneId::Game, m_skeleton, nullptr, reference},
                    m_speeds,
                    false,
                    &m_skeleton,
                    table});
        ASSERT_TRUE(expected.HasValue()) << expected.Error();
        for (const std::int32_t subtype : {209, 210, 211})
        {
            SCOPED_TRACE(subtype);
            auto actor = Client::Tests::UnarmedPlayer(3);
            actor.type = 0x153;
            actor.subtype = subtype;
            Client::Crt::CrtRandom random(seed);
            const auto walked = Client::Actors::SetActorWalk(
                actor, {{m_runtime, Client::Scenes::SceneId::Game, m_skeleton, nullptr, random},
                        m_speeds,
                        false,
                        &m_skeleton,
                        table});
            ASSERT_TRUE(walked.HasValue()) << walked.Error();
            EXPECT_EQ(walked.Value().sound, expected.Value().sound);
            EXPECT_EQ(random.State(), reference.State());

            Client::Crt::CrtRandom quiet(seed);
            const auto muted = Client::Actors::SetActorWalk(
                actor, {{m_runtime, Client::Scenes::SceneId::Game, m_skeleton, nullptr, quiet},
                        m_speeds,
                        false,
                        &m_skeleton,
                        silent});
            ASSERT_TRUE(muted.HasValue()) << muted.Error();
            if (expected.Value().sound.has_value())
            {
                ++sounded;
                EXPECT_EQ(muted.Value().sound, Client::Audio::SoundId{0x7b});
            }
            else
            {
                EXPECT_FALSE(muted.Value().sound.has_value());
            }
        }
    }
    EXPECT_GT(sounded, 0);
}
