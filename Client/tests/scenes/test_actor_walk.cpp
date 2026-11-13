#include "Scenes/ActorWalk.h"

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorState.h"
#include "Scenes/PlayerActionSpeeds.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldRuntime.h"
#include "support/PlayerActors.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>

namespace
{
class ActorWalkTest : public ::testing::Test
{
protected:
    void Walk(Client::Scenes::ActorState& actor, bool hero = true)
    {
        auto walked = Client::Scenes::SetActorWalk(
            actor, {{m_runtime, Client::Scenes::SceneId::Game, m_skeleton, &m_items, m_random}, m_speeds, hero});
        ASSERT_TRUE(walked.HasValue()) << (walked.HasValue() ? "" : walked.Error());
    }
    Client::Scenes::WorldRuntime m_runtime = []
    {
        Client::Scenes::WorldRuntime value{};
        value.map = 0;
        return value;
    }();
    Client::Render::Models::Skeleton m_skeleton = Client::Tests::PlayerActionsSkeleton();
    Client::Data::Tables::ItemTable m_items = Client::Tests::ItemsMarking({0x213, 0x227, 0xc12});
    Client::Render::Models::CrtRandom m_random{1};
    Client::Scenes::PlayerActionSpeeds m_speeds;
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
    const auto walked = Client::Scenes::SetActorWalk(
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
    const auto walked = Client::Scenes::SetActorWalk(
        actor,
        {{m_runtime, Client::Scenes::SceneId::Game, m_skeleton, nullptr, m_random}, m_speeds, true, &m_skeleton});
    ASSERT_TRUE(walked.HasValue()) << (walked.HasValue() ? "" : walked.Error());
    EXPECT_EQ(actor.animation.action, 2);
    EXPECT_EQ(actor.run, 1);
    EXPECT_FLOAT_EQ(m_speeds.Speed(13, 9.0f), 0.165f);
    EXPECT_EQ(m_random.State(), Client::Tests::AfterDraws(1, 1).State());
}

TEST_F(ActorWalkTest, Type16dRequestsItsUnconditionalSoundWithoutDrawingRandom)
{
    auto actor = Client::Tests::UnarmedPlayer();
    actor.type = 0x16d;
    const auto walked = Client::Scenes::SetActorWalk(
        actor,
        {{m_runtime, Client::Scenes::SceneId::Game, m_skeleton, nullptr, m_random}, m_speeds, true, &m_skeleton});
    ASSERT_TRUE(walked.HasValue()) << (walked.HasValue() ? "" : walked.Error());
    ASSERT_TRUE(walked.Value().sound.has_value());
    EXPECT_EQ(*walked.Value().sound, 0x7c);
    EXPECT_EQ(m_random.State(), 1U);
}

} // namespace

TEST(ActorFootstepTest, ThresholdsUsePersistentLatchesAndDoNotDrawRandom)
{
    auto actor = Client::Tests::UnarmedPlayer();
    actor.animation.action = 13;
    actor.animation.frame = 1.49f;
    auto sounds = Client::Scenes::TakeActorFootstepSounds(actor, 0, 0);
    EXPECT_FALSE(sounds[0]);
    EXPECT_EQ(actor.firstFootstepSound, 0);
    actor.animation.frame = 1.5f;
    sounds = Client::Scenes::TakeActorFootstepSounds(actor, 0, 0);
    EXPECT_EQ(sounds[0], 9);
    EXPECT_EQ(actor.firstFootstepSound, 1);
    EXPECT_EQ(actor.secondFootstepSound, 0);
    sounds = Client::Scenes::TakeActorFootstepSounds(actor, 0, 0);
    EXPECT_FALSE(sounds[0]);
    actor.animation.frame = 4.5f;
    sounds = Client::Scenes::TakeActorFootstepSounds(actor, 2, 0);
    EXPECT_FALSE(sounds[0]);
    EXPECT_EQ(sounds[1], 10);
    EXPECT_EQ(actor.secondFootstepSound, 1);
}

TEST(ActorFootstepTest, FlyingActionsSetTheLatchesButSuppressSounds)
{
    auto actor = Client::Tests::UnarmedPlayer();
    actor.animation.action = 0x1e;
    actor.animation.frame = 5.0f;
    const auto sounds = Client::Scenes::TakeActorFootstepSounds(actor, 0, 0);
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
        const auto sounds = Client::Scenes::TakeActorFootstepSounds(actor, sample.map, sample.layer);
        EXPECT_EQ(sounds[0], sample.sound);
        EXPECT_EQ(sounds[1], sample.sound);
    }
}

// 0x4f3efa..0x4f3f67: otro modelo que el 0x20a, sorteado con rand() & 15, suena con la tabla de su modelo (+0xaa, la de
// 0x612c80): una de las dos primeras por rand() & 1, más 0xd2 (y 5 con el subtipo 9). Con la primera en −1, nada; sin
// la tabla, corta.
TEST_F(ActorWalkTest, AnotherModelSoundsFromItsModelTable)
{
    const std::array<std::int16_t, 10> table{0x10, 0x11, 0x12, 0x13, 0x14, -1, -1, -1, -1, -1};
    const std::array<std::int16_t, 10> silent{-1, 0x11, 0x12, 0x13, 0x14, -1, -1, -1, -1, -1};
    int sounded = 0;
    int cut = 0;
    for (std::uint32_t seed = 1; seed < 200; ++seed)
    {
        SCOPED_TRACE(seed);
        for (const std::int32_t subtype : {0, 9})
        {
            auto actor = Client::Tests::UnarmedPlayer(3);
            actor.type = 0x153;
            actor.subtype = subtype;
            Client::Render::Models::CrtRandom random(seed);
            const auto walked = Client::Scenes::SetActorWalk(
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
                EXPECT_TRUE(*walked.Value().sound == base + 0x10 || *walked.Value().sound == base + 0x11);
            }

            Client::Render::Models::CrtRandom quiet(seed);
            const auto muted = Client::Scenes::SetActorWalk(
                actor, {{m_runtime, Client::Scenes::SceneId::Game, m_skeleton, nullptr, quiet},
                        m_speeds,
                        false,
                        &m_skeleton,
                        silent});
            ASSERT_TRUE(muted.HasValue()) << muted.Error();
            EXPECT_FALSE(muted.Value().sound.has_value());

            Client::Render::Models::CrtRandom missing(seed);
            const auto unknown = Client::Scenes::SetActorWalk(
                actor, {{m_runtime, Client::Scenes::SceneId::Game, m_skeleton, nullptr, missing},
                        m_speeds,
                        false,
                        &m_skeleton});
            if (!unknown.HasValue())
            {
                ++cut;
                EXPECT_NE(unknown.Error().find("0x4f3f0f"), std::string::npos) << unknown.Error();
            }
        }
    }
    EXPECT_GT(sounded, 0);
    EXPECT_EQ(cut, sounded);
}
