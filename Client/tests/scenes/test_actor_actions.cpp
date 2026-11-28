#include "Actors/ActorActions.h"

#include "Audio/Playback.h"
#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/ItemTable.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Models/LogicalAccess.h"
#include "Models/Skeleton.h"
#include "Actors/ActionSend.h"
#include "Kernel/ActorState.h"
#include "Kernel/SceneId.h"
#include "Kernel/WorldRuntime.h"
#include "Sound/SoundRequests.h"
#include "support/PlayerActors.h"
#include "support/SentActions.h"
#include "Actors/MovementSpeed.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/ext/vector_float3.hpp>

// Client::Actors::SetActorStop: 0x4f2250, the standing action (06, "Caminar"; P-C14 c). The values come from the ASM
// checked against PE c32bbd5f; none from a run of the original.
namespace
{
using Client::Actors::ActorStopContext;
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Kernel::NoItemModel;
using Client::Kernel::SceneId;
using Client::Tests::AfterDraws;
using Client::Tests::FirstSeedWhere;
using Client::Tests::UnarmedPlayer;

class RecordingPlayback final : public Client::Audio::IPlayback
{
public:
    struct SoundRequest
    {
        std::int32_t id;
        std::optional<glm::vec3> origin;
        bool loop;
    };

    void PlaySound(std::int32_t id, std::optional<glm::vec3> origin, bool loop) override
    {
        requests.push_back({id, origin, loop});
    }
    void StopSound(std::int32_t, bool) override {}
    void PlayMusic(std::string_view, bool) override {}
    void StopMusic() override {}
    void SetEffectsVolume(std::uint8_t) override {}
    void SetListener(glm::vec3) override {}

    std::vector<SoundRequest> requests;
};

// The first seed whose rand() % 16 is not 0: the suffix draws one and ends.
std::uint32_t QuietSeed()
{
    return FirstSeedWhere([](CrtRandom& random) { return random.Next() % 16 != 0; });
}

std::uint32_t SeedWithSuffixGate(std::uint32_t prefixDraws, bool gate)
{
    return FirstSeedWhere(
        [prefixDraws, gate](CrtRandom& random)
        {
            random.Discard(prefixDraws);
            return (random.Next() % 16 == 0) == gate;
        });
}

class ActorStopTest : public ::testing::Test
{
protected:
    // 0x4f2250 on `actor`, in the test's map and scene, with m_random's seed.
    Client::Core::Expected<Client::Actors::ActorStopResult, std::string> Stop(ActorState& actor)
    {
        Client::Actors::ActionSender send{};
        if (m_withSender)
            send = Client::Tests::RecordingActions(m_sent);
        const ActorStopContext context{.runtime = m_runtime,
                                       .scene = m_scene,
                                       .playerSkeleton = m_skeleton,
                                       .items = m_withItems ? &m_items : nullptr,
                                       .random = m_random,
                                       .terrainWalls = m_terrainWalls,
                                       .sendAction = send,
                                       .actorSkeleton = m_actorSkeleton,
                                       .modelSounds = m_modelSounds};
        return Client::Actors::SetActorStop(actor, context);
    }

    // The action left after a 0x4f2250 that does not fail.
    std::uint8_t StandOf(ActorState actor)
    {
        const auto stopped = Stop(actor);
        EXPECT_TRUE(stopped.HasValue()) << (stopped.HasValue() ? "" : stopped.Error());
        return actor.animation.action;
    }

    Client::Kernel::WorldRuntime m_runtime = []
    {
        Client::Kernel::WorldRuntime runtime;
        runtime.map = 0;
        return runtime;
    }();
    SceneId m_scene = SceneId::Game;
    Client::Models::Skeleton m_skeleton = Client::Tests::PlayerActionsSkeleton();
    Client::Models::Skeleton m_actorModelSkeleton = Client::Tests::PlayerActionsSkeleton();
    const Client::Models::Skeleton* m_actorSkeleton = nullptr;
    std::span<const std::int16_t> m_modelSounds{};
    const Client::Data::Terrain::TerrainLayer<std::uint16_t>* m_terrainWalls = nullptr;
    // The matrix's weapons that carry the +0x1e mark in Item.bmd.
    Client::Tables::ItemTable m_items =
        Client::Tests::ItemsMarking({0x213, 0x227, 0x229, 0x231, 0x811, 0x815, 0xc12, 0xe11});
    bool m_withItems = true;
    CrtRandom m_random{QuietSeed()};
    // The C1 18 that sitting down sends, with the direction and the action. Without m_withSender, nobody sends them.
    bool m_withSender = true;
    std::vector<Client::Tests::SentAction> m_sent;
};

ActorState Armed(std::uint16_t right, std::uint16_t left = NoItemModel, std::uint8_t characterClass = 0)
{
    ActorState actor = UnarmedPlayer(characterClass);
    actor.hands[0].model = right;
    actor.hands[1].model = left;
    return actor;
}

// 0x4f2bf6..0x4f2c2c and 0x4f2d5b..0x4f2d72: with nothing in the hands, or in the safe zone of maps below 11 or above
// 23, action 1, and 2 with class 2 (without looking at the second class's bit).
TEST_F(ActorStopTest, SafeZoneOrNoWeaponStands1Or2ForTheElf)
{
    EXPECT_EQ(StandOf(UnarmedPlayer(0)), 1);
    EXPECT_EQ(StandOf(UnarmedPlayer(1)), 1);
    EXPECT_EQ(StandOf(UnarmedPlayer(2)), 2);
    EXPECT_EQ(StandOf(UnarmedPlayer(2 | 8)), 2);

    ActorState armedSafe = Armed(0x212);
    armedSafe.safeZone = 0x80;
    for (const std::int32_t map : {0, 10, 30})
    {
        SCOPED_TRACE(map);
        m_runtime.map = map;
        EXPECT_EQ(StandOf(armedSafe), 1);
        armedSafe.characterClass = 2;
        EXPECT_EQ(StandOf(armedSafe), 2);
        armedSafe.characterClass = 0;
    }
    // From 11 to 23, the safe zone changes nothing: the weapon chooses (0x4f2c16..0x4f2c1e).
    for (const std::int32_t map : {11, 23})
    {
        SCOPED_TRACE(map);
        m_runtime.map = map;
        EXPECT_EQ(StandOf(armedSafe), 3);
    }
}

// Run at 0 on entry (0x4f227f), and the action through 0x4ea030: with the same one it does not touch the frame.
TEST_F(ActorStopTest, RunGoesToZeroAndTheActionGoesThroughSetAction)
{
    ActorState walking = UnarmedPlayer();
    walking.run = 40;
    walking.animation.action = 0x16;
    walking.animation.frame = 3.5f;
    ASSERT_TRUE(Stop(walking).HasValue());
    EXPECT_EQ(walking.run, 0);
    EXPECT_EQ(walking.animation.action, 1);
    EXPECT_EQ(walking.animation.previousAction, 0x16);
    EXPECT_EQ(walking.animation.previousKey, 3);
    EXPECT_EQ(walking.animation.frame, 0.0f);

    ActorState standing = UnarmedPlayer();
    standing.animation.action = 1;
    standing.animation.frame = 2.25f;
    ASSERT_TRUE(Stop(standing).HasValue());
    EXPECT_EQ(standing.animation.frame, 2.25f);
}

// 0x4f2c31..0x4f2d82, with the models as signed WORDs: groups 0 to 2 by the record's +0x1e mark, 0x813 and 0x814,
// groups 3 and 5, the left one of group 4 and the right one of group 4.
TEST_F(ActorStopTest, WeaponMatrix)
{
    struct Case
    {
        std::uint16_t right;
        std::uint16_t left;
        std::uint8_t stand;
    };
    const std::vector<Case> cases = {
        {0x212, NoItemModel, 3},    {0x213, NoItemModel, 4},    {0x811, NoItemModel, 4}, {0x227, NoItemModel, 0x8a},
        {0x229, NoItemModel, 0x8a}, {0x231, NoItemModel, 0x8a}, {0x228, NoItemModel, 3}, {0x812, NoItemModel, 3},
        {0x815, NoItemModel, 6},    {0x813, NoItemModel, 5},    {0x814, NoItemModel, 5}, {0xc12, NoItemModel, 6},
        {0xe11, NoItemModel, 6},    {0xc13, NoItemModel, 3},    {NoItemModel, 0xa12, 7}, {NoItemModel, 0xa18, 7},
        {NoItemModel, 0xa23, 7},    {NoItemModel, 0xa26, 7},    {NoItemModel, 0xa27, 7}, {NoItemModel, 0xa19, 1},
        {0xa1a, NoItemModel, 8},    {0xa20, NoItemModel, 8},    {0xa22, NoItemModel, 8}, {0xa24, NoItemModel, 8},
        {0xc11, NoItemModel, 8},    {0xa21, NoItemModel, 1},    {0xa23, NoItemModel, 1}, {0xa1a, 0xa12, 7},
        {0x213, 0xa12, 4},          {0xe12, NoItemModel, 1},    {NoItemModel, 0xe12, 1}, {0x1c17, NoItemModel, 1},
    };
    for (const Case& weapon : cases)
    {
        SCOPED_TRACE(testing::Message() << "derecha " << weapon.right << ", izquierda " << weapon.left);
        EXPECT_EQ(StandOf(Armed(weapon.right, weapon.left)), weapon.stand);
    }
    EXPECT_EQ(StandOf(Armed(0xa1a, NoItemModel, 2)), 8);
    EXPECT_EQ(StandOf(Armed(NoItemModel, 0xe12, 2)), 2);
}

// The mark comes from *[0x7758550]: without Item.bmd it is not made up. 0x813, 0x814 and group 4 do not read it.
TEST_F(ActorStopTest, WithoutItemBmdTheMarkIsAnError)
{
    m_withItems = false;
    ActorState sword = Armed(0x212);
    const auto stopped = Stop(sword);
    ASSERT_FALSE(stopped.HasValue());
    EXPECT_NE(stopped.Error().find("+0x1e"), std::string::npos);

    EXPECT_EQ(StandOf(Armed(0x813)), 5);
    EXPECT_EQ(StandOf(Armed(0xa1a)), 8);
    EXPECT_EQ(StandOf(Armed(NoItemModel, 0xa12)), 7);
}

// 0x4f22fb..0x4f230f: the dark horse outside the safe zone, with weapons or wings, is 0x4a. In the safe zone it goes
// past.
TEST_F(ActorStopTest, DarkHorseOutsideIs4a)
{
    ActorState rider = Armed(0x212, 0xa12);
    rider.pet.model = 0x1c16;
    rider.wings.model = 0x1a12;
    EXPECT_EQ(StandOf(rider), 0x4a);

    ActorState inTown = UnarmedPlayer();
    inTown.pet.model = 0x1c16;
    inTown.safeZone = 1;
    EXPECT_EQ(StandOf(inTown), 1);
}

// 0x4f2314..0x4f2360: the raven in the left hand in the safe zone is 0x48; 0x1c14 and 0x1c15 outside it, 0xb without
// weapons and 0xc with any.
TEST_F(ActorStopTest, TheRavenInTheSafeZoneIs48AndRidingIsBOrC)
{
    ActorState raven = Armed(0x212, 0x1c17);
    raven.safeZone = 1;
    EXPECT_EQ(StandOf(raven), 0x48);
    raven.safeZone = 0;
    EXPECT_EQ(StandOf(raven), 3);

    for (const std::uint16_t pet : {std::uint16_t{0x1c14}, std::uint16_t{0x1c15}})
    {
        SCOPED_TRACE(pet);
        ActorState rider = UnarmedPlayer();
        rider.pet.model = pet;
        EXPECT_EQ(StandOf(rider), 0xb);
        rider.hands[1].model = 0xa12;
        EXPECT_EQ(StandOf(rider), 0xc);
        rider.safeZone = 1;
        EXPECT_EQ(StandOf(rider), 1);
    }
}

// 0x4f228c..0x4f22f6: Fenrir outside the safe zone sets the latch 0x7d3a0d0 to 0 and chooses by the hands, before the
// horse, the wings or sitting.
TEST_F(ActorStopTest, FenrirIs76To79)
{
    struct Case
    {
        std::uint16_t right;
        std::uint16_t left;
        std::uint8_t stand;
    };
    const std::vector<Case> cases = {
        {NoItemModel, NoItemModel, 0x76}, {0x212, 0xa12, 0x77}, {0x212, NoItemModel, 0x78}, {NoItemModel, 0xa12, 0x79}};
    for (const Case& hands : cases)
    {
        SCOPED_TRACE(hands.stand);
        m_runtime.movementSpeed = {.itemLatch = 7, .itemCounterBits = 9};
        ActorState rider = Armed(hands.right, hands.left);
        rider.pet.model = 0x1c37;
        rider.wings.model = 0x1a12;
        rider.flags7c = 0x200000;
        EXPECT_EQ(StandOf(rider), hands.stand);
        EXPECT_EQ(m_runtime.movementSpeed.itemLatch, 0u);
        EXPECT_EQ(m_runtime.movementSpeed.itemCounterBits, 9u);
    }

    m_runtime.movementSpeed.itemLatch = 7;
    ActorState inTown = UnarmedPlayer();
    inTown.pet.model = 0x1c37;
    inTown.safeZone = 1;
    EXPECT_EQ(StandOf(inTown), 1);
    EXPECT_EQ(m_runtime.movementSpeed.itemLatch, 7u);
}

// 0x4f2365..0x4f27fc: it flies with wings outside the safe zone or with +0x376 at 0x101; 10 with the right one of group
// 4, otherwise 9.
TEST_F(ActorStopTest, WingsOutsideOrWord376FlyAs9Or10)
{
    ActorState winged = Armed(0x212);
    winged.wings.model = 0x1a12;
    EXPECT_EQ(StandOf(winged), 9);
    winged.hands[0].model = 0xa1a;
    EXPECT_EQ(StandOf(winged), 10);
    winged.safeZone = 1;
    EXPECT_EQ(StandOf(winged), 1);

    ActorState word376 = UnarmedPlayer();
    word376.safeZone = 1;
    word376.word376 = 0x101;
    EXPECT_EQ(StandOf(word376), 9);
}

// 0x4f2376..0x4f23ce: on maps 7, 24 to 29 and 36 of scene 5 the cell of the live TerrainWall decides, which MuRet does
// not have in the game. Outside scene 5 it does not look at it.
TEST_F(ActorStopTest, FlyingMapsOfTheGameSceneAreAFrontier)
{
    for (const std::int32_t map : {7, 24, 29, 36})
    {
        SCOPED_TRACE(map);
        m_runtime.map = map;
        m_scene = SceneId::Game;
        ActorState hero = UnarmedPlayer();
        const auto stopped = Stop(hero);
        ASSERT_FALSE(stopped.HasValue());
        EXPECT_NE(stopped.Error().find("0x7d0d348"), std::string::npos);
        EXPECT_EQ(hero.animation.action, 0);

        m_scene = SceneId::CharacterSelect;
        EXPECT_EQ(StandOf(UnarmedPlayer()), 1);
    }
    m_scene = SceneId::Game;
    for (const std::int32_t map : {6, 8, 23, 30, 35, 37})
    {
        SCOPED_TRACE(map);
        m_runtime.map = map;
        EXPECT_EQ(StandOf(UnarmedPlayer()), 1);
    }
}

// 0x4f23e3..0x4f2bea and 0x4f2801..0x4f2bea: with +0x7c & 0x200000, already sitting, nothing changes; otherwise it sets
// 0xca and sends the C1 05 18 with the direction of its own angle (+0x24) and 0x82, through both copies (flying and on
// the ground), and goes on to the suffix, which draws its rand().
TEST_F(ActorStopTest, SittingSendsTheActionWithItsOwnAngle)
{
    using Sent = std::vector<Client::Tests::SentAction>;
    const std::uint32_t quiet = QuietSeed();
    ActorState sitting = Armed(0x212);
    sitting.flags7c = 0x200000;
    sitting.angles.z = 100.0f;
    const auto stopped = Stop(sitting);
    ASSERT_TRUE(stopped.HasValue()) << stopped.Error();
    EXPECT_EQ(sitting.animation.action, 0xca);
    EXPECT_EQ(m_sent, (Sent{{3, 0x82}}));
    EXPECT_EQ(m_random.State(), AfterDraws(quiet, 1).State());

    m_sent.clear();
    ActorState winged = UnarmedPlayer();
    winged.wings.model = 0x1a12;
    winged.flags7c = 0x200000;
    winged.angles.z = -30.0f;
    EXPECT_EQ(StandOf(winged), 0xca);
    EXPECT_EQ(m_sent, (Sent{{0, 0x82}}));

    m_sent.clear();
    ActorState seated = UnarmedPlayer();
    seated.flags7c = 0x200000;
    seated.animation.action = 0xca;
    seated.animation.frame = 4.5f;
    EXPECT_EQ(StandOf(seated), 0xca);
    EXPECT_EQ(seated.animation.frame, 4.5f);
    EXPECT_TRUE(m_sent.empty());
}

// Without a sender for the C1 18 (a call that does not wire it yet), sitting down stops with its address after 0xca.
TEST_F(ActorStopTest, SittingWithoutASenderIsAnExplicitError)
{
    m_withSender = false;
    ActorState sitting = Armed(0x212);
    sitting.flags7c = 0x200000;
    const auto stopped = Stop(sitting);
    ASSERT_FALSE(stopped.HasValue());
    EXPECT_NE(stopped.Error().find("0x4f2416"), std::string::npos);
    EXPECT_EQ(sitting.animation.action, 0xca);
}

// 0x4f36be..0x4f37a5: one rand() always; with rand() % 16 at 0 and Fenrir, another % 3 and, if it gives 0, sound 0x279
// + rand() & 1. The pet decides, not the action.
TEST_F(ActorStopTest, TheSuffixDrawsOneRandAndFenrirMaySound)
{
    const std::uint32_t quiet = QuietSeed();
    ActorState plain = UnarmedPlayer();
    const auto calm = Stop(plain);
    ASSERT_TRUE(calm.HasValue());
    EXPECT_EQ(calm.Value().sound, std::nullopt);
    EXPECT_EQ(m_random.State(), AfterDraws(quiet, 1).State());

    const std::uint32_t lucky = FirstSeedWhere([](CrtRandom& random) { return random.Next() % 16 == 0; });
    m_random = CrtRandom(lucky);
    const auto noFenrir = Stop(plain);
    ASSERT_TRUE(noFenrir.HasValue());
    EXPECT_EQ(noFenrir.Value().sound, std::nullopt);
    EXPECT_EQ(m_random.State(), AfterDraws(lucky, 1).State());

    ActorState fenrir = UnarmedPlayer();
    fenrir.pet.model = 0x1c37;
    fenrir.safeZone = 1;
    const std::uint32_t silent =
        FirstSeedWhere([](CrtRandom& random) { return random.Next() % 16 == 0 && random.Next() % 3 != 0; });
    m_random = CrtRandom(silent);
    const auto withoutSound = Stop(fenrir);
    ASSERT_TRUE(withoutSound.HasValue());
    EXPECT_EQ(withoutSound.Value().sound, std::nullopt);
    EXPECT_EQ(m_random.State(), AfterDraws(silent, 2).State());

    const std::uint32_t loud =
        FirstSeedWhere([](CrtRandom& random) { return random.Next() % 16 == 0 && random.Next() % 3 == 0; });
    CrtRandom expected = AfterDraws(loud, 2);
    const int variant = expected.Next() % 2;
    m_random = CrtRandom(loud);
    const auto withSound = Stop(fenrir);
    ASSERT_TRUE(withSound.HasValue());
    EXPECT_EQ(withSound.Value().sound, Client::Audio::SoundId{0x279 + variant});
    EXPECT_EQ(m_random.State(), expected.State());
}

TEST_F(ActorStopTest, FenrirSuffixRequestsItsSoundOnceAtTheActorPosition)
{
    const std::uint32_t loud =
        FirstSeedWhere([](CrtRandom& random) { return random.Next() % 16 == 0 && random.Next() % 3 == 0; });
    CrtRandom expected = AfterDraws(loud, 2);
    const std::int32_t soundId = 0x279 + expected.Next() % 2;
    m_random = CrtRandom(loud);
    ActorState fenrir = UnarmedPlayer();
    fenrir.pet.model = 0x1c37;
    fenrir.safeZone = 1;
    fenrir.position = {123.0f, 456.0f, 7.0f};
    RecordingPlayback playback;
    m_runtime.audio = &playback;
    Client::Sound::SoundRequests playbackRequests(&playback);
    m_runtime.sounds = &playbackRequests;

    const auto stopped = Stop(fenrir);

    ASSERT_TRUE(stopped.HasValue()) << stopped.Error();
    ASSERT_EQ(stopped.Value().sound, Client::Audio::SoundId{soundId});
    ASSERT_EQ(playback.requests.size(), 1u);
    EXPECT_EQ(playback.requests[0].id, soundId);
    ASSERT_TRUE(playback.requests[0].origin.has_value());
    EXPECT_EQ(*playback.requests[0].origin, fenrir.position);
    EXPECT_FALSE(playback.requests[0].loop);
}

// 0x4f2286..0x4f36be: type 0x154 asks for action 0 of its own skeleton after reading Y and then X.
TEST_F(ActorStopTest, TransformedActorUsesItsModelIdleAndPreservesTheSameActionFrame)
{
    m_actorModelSkeleton.actions.resize(7);
    m_actorSkeleton = &m_actorModelSkeleton;
    const std::array<std::int16_t, 10> sounds{10, 11, 11, 11, 12, -1, -1, -1, -1, -1};
    m_modelSounds = sounds;
    ActorState transformed = UnarmedPlayer();
    transformed.type = 0x154;
    transformed.cellX = 23;
    transformed.cellY = 17;
    transformed.run = 40;
    transformed.animation.action = 3;
    transformed.animation.frame = 4.5f;
    const std::uint32_t quiet = SeedWithSuffixGate(8, false);
    m_random = CrtRandom(quiet);

    const auto changed = Stop(transformed);

    ASSERT_TRUE(changed.HasValue()) << changed.Error();
    EXPECT_EQ(transformed.run, 0);
    EXPECT_EQ(transformed.animation.action, 0);
    EXPECT_EQ(transformed.animation.frame, 0.0f);
    EXPECT_EQ(transformed.cellX, 23);
    EXPECT_EQ(transformed.cellY, 17);
    EXPECT_EQ(transformed.cellXAccesses, 0);
    EXPECT_EQ(transformed.cellYAccesses, 0);
    EXPECT_EQ(m_random.State(), AfterDraws(quiet, 9).State());

    transformed.animation.frame = 6.25f;
    m_random = CrtRandom(quiet);
    const auto unchanged = Stop(transformed);

    ASSERT_TRUE(unchanged.HasValue()) << unchanged.Error();
    EXPECT_EQ(transformed.animation.action, 0);
    EXPECT_EQ(transformed.animation.frame, 6.25f);
    EXPECT_EQ(m_random.State(), AfterDraws(quiet, 9).State());
}

// 0x172 looks only at bit 0 of the TerrainWall in the logical cell and chooses 7 or 0.
TEST_F(ActorStopTest, Type172UsesTheLogicalCellTerrainWallBit)
{
    m_actorModelSkeleton.actions.resize(8);
    m_actorSkeleton = &m_actorModelSkeleton;
    const std::array<std::int16_t, 10> noModelSound{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    m_modelSounds = noModelSound;
    Client::Data::Terrain::TerrainLayer<std::uint16_t> walls{};
    m_terrainWalls = &walls;
    ActorState actor = UnarmedPlayer();
    actor.type = 0x172;
    actor.cellX = 31;
    actor.cellY = 47;
    actor.position = {9000.0f, 8000.0f, 12.0f};
    walls[Client::Data::Terrain::CellIndex(actor.cellX, actor.cellY)] = 1;
    m_random = CrtRandom(SeedWithSuffixGate(8, false));

    ASSERT_TRUE(Stop(actor).HasValue());
    EXPECT_EQ(actor.animation.action, 7);

    actor.animation.action = 6;
    actor.animation.frame = 5.0f;
    walls[Client::Data::Terrain::CellIndex(actor.cellX, actor.cellY)] = 0;
    m_random = CrtRandom(SeedWithSuffixGate(8, false));
    ASSERT_TRUE(Stop(actor).HasValue());
    EXPECT_EQ(actor.animation.action, 0);
    EXPECT_EQ(actor.animation.frame, 0.0f);
    EXPECT_EQ(actor.position, glm::vec3(9000.0f, 8000.0f, 12.0f));
}

// The transformed actor needs the Y and X cells with separate closes: each close to zero draws four rand().
TEST_F(ActorStopTest, LogicalCellCloseDrawsRespectExistingDepthAndByteWrap)
{
    m_actorSkeleton = &m_actorModelSkeleton;
    const std::array<std::int16_t, 10> noModelSound{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    m_modelSounds = noModelSound;
    struct DepthCase
    {
        std::uint8_t y;
        std::uint8_t x;
        int draws;
    };
    const std::array cases{DepthCase{0, 0, 9},     DepthCase{1, 0, 5},   DepthCase{0, 1, 5},  DepthCase{1, 1, 1},
                           DepthCase{255, 255, 1}, DepthCase{255, 0, 5}, DepthCase{0, 255, 5}};
    constexpr std::uint32_t Seed = 0x734u;
    for (const DepthCase& depth : cases)
    {
        SCOPED_TRACE(testing::Message() << "Y " << int{depth.y} << ", X " << int{depth.x});
        ActorState actor = UnarmedPlayer();
        actor.type = 0x154;
        actor.cellX = 29;
        actor.cellY = 51;
        actor.cellXAccesses = depth.x;
        actor.cellYAccesses = depth.y;
        m_random = CrtRandom(Seed);

        const auto stopped = Stop(actor);

        ASSERT_TRUE(stopped.HasValue()) << stopped.Error();
        EXPECT_EQ(actor.cellXAccesses, depth.x);
        EXPECT_EQ(actor.cellYAccesses, depth.y);
        EXPECT_EQ(actor.cellX, 29);
        EXPECT_EQ(actor.cellY, 51);
        EXPECT_EQ(m_random.State(), AfterDraws(Seed, depth.draws).State());
    }
}

// A model without a skeleton is the same as the empty record: SetAction does nothing, but the suffix still runs.
TEST_F(ActorStopTest, MissingOrEmptyActorSkeletonLeavesTheAnimationAndRunsTheSuffix)
{
    const std::array<std::int16_t, 10> noModelSound{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    m_modelSounds = noModelSound;
    Client::Models::Skeleton emptySkeleton;
    const std::array<const Client::Models::Skeleton*, 2> skeletons{nullptr, &emptySkeleton};
    for (const Client::Models::Skeleton* skeleton : skeletons)
    {
        ActorState actor = UnarmedPlayer();
        actor.type = 0x154;
        actor.animation.action = 3;
        actor.animation.frame = 8.5f;
        m_actorSkeleton = skeleton;
        const std::uint32_t seed = SeedWithSuffixGate(8, false);
        m_random = CrtRandom(seed);

        const auto stopped = Stop(actor);

        ASSERT_TRUE(stopped.HasValue()) << stopped.Error();
        EXPECT_EQ(actor.animation.action, 3);
        EXPECT_EQ(actor.animation.frame, 8.5f);
        EXPECT_EQ(m_random.State(), AfterDraws(seed, 9).State());
    }
}

// On 0x172 a missing TerrainWall stops with an error after run and the two accesses, before changing the action.
TEST_F(ActorStopTest, Type172WithoutTerrainWallsErrorsAfterTheLogicalCellAccesses)
{
    m_actorSkeleton = &m_actorModelSkeleton;
    const std::array<std::int16_t, 10> noModelSound{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    m_modelSounds = noModelSound;
    m_terrainWalls = nullptr;
    ActorState monster = UnarmedPlayer();
    monster.type = 0x172;
    monster.run = 40;
    monster.animation.action = 2;
    const std::uint32_t seed = 0x735u;
    m_random = CrtRandom(seed);

    const auto stopped = Stop(monster);

    ASSERT_FALSE(stopped.HasValue());
    EXPECT_NE(stopped.Error().find("TerrainWall"), std::string::npos);
    EXPECT_EQ(monster.run, 0);
    EXPECT_EQ(monster.animation.action, 2);
    EXPECT_EQ(monster.cellXAccesses, 0);
    EXPECT_EQ(monster.cellYAccesses, 0);
    EXPECT_EQ(m_random.State(), AfterDraws(seed, 8).State());
}

// 0x4f36e2..0x4f36f3: subtypes 209 to 211 use the sound table with players and non-players.
TEST_F(ActorStopTest, ModelSoundSubtypesUseTheirTableWhenTheSuffixGateOpens)
{
    const std::array<std::int16_t, 10> sounds{10, 11, 11, 11, 12, -1, -1, -1, -1, -1};
    m_modelSounds = sounds;
    const std::uint32_t seed = FirstSeedWhere([](CrtRandom& random) { return random.Next() % 16 == 0; });
    for (const std::int32_t subtype : {209, 210, 211})
    {
        SCOPED_TRACE(subtype);
        ActorState player = UnarmedPlayer();
        player.subtype = subtype;
        CrtRandom expected = AfterDraws(seed, 1);
        const int variant = expected.Next() & 1;
        m_random = CrtRandom(seed);
        const auto stopped = Stop(player);
        ASSERT_TRUE(stopped.HasValue()) << stopped.Error();
        EXPECT_EQ(stopped.Value().sound, Client::Audio::SoundId{0xd2 + sounds[static_cast<std::size_t>(variant)]});
        EXPECT_EQ(m_random.State(), expected.State());
    }
    for (const std::int32_t subtype : {208, 212})
    {
        SCOPED_TRACE(subtype);
        ActorState player = UnarmedPlayer();
        player.subtype = subtype;
        m_random = CrtRandom(seed);
        const auto stopped = Stop(player);
        ASSERT_TRUE(stopped.HasValue()) << stopped.Error();
        EXPECT_EQ(stopped.Value().sound, std::nullopt);
        EXPECT_EQ(m_random.State(), AfterDraws(seed, 1).State());
    }
}

// A first WORD of −1 stops before the second rand(); the empty record's zeros are valid data and do take the second.
TEST_F(ActorStopTest, ModelSoundSentinelAndZeroFallbackHaveDifferentRandomPaths)
{
    m_actorSkeleton = &m_actorModelSkeleton;
    ActorState monster = UnarmedPlayer();
    monster.type = 0x154;
    const std::uint32_t seed = SeedWithSuffixGate(8, true);

    const std::array<std::int16_t, 10> emptyModel{};
    m_modelSounds = emptyModel;
    m_random = CrtRandom(seed);
    const auto zeroFallback = Stop(monster);
    ASSERT_TRUE(zeroFallback.HasValue()) << zeroFallback.Error();
    EXPECT_EQ(zeroFallback.Value().sound, Client::Audio::SoundId{0xd2});
    EXPECT_EQ(m_random.State(), AfterDraws(seed, 10).State());

    const std::array<std::int16_t, 10> noSound{-1, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    m_modelSounds = noSound;
    m_random = CrtRandom(seed);
    const auto sentinel = Stop(monster);
    ASSERT_TRUE(sentinel.HasValue()) << sentinel.Error();
    EXPECT_EQ(sentinel.Value().sound, std::nullopt);
    EXPECT_EQ(m_random.State(), AfterDraws(seed, 9).State());
}

// The noPlayer suffix uses the model's two WORDs, adds 5 with subtype 9 and does not take Fenrir's sound branch.
TEST_F(ActorStopTest, NonPlayerSuffixUsesItsModelTableInsteadOfFenrirSounds)
{
    m_actorSkeleton = &m_actorModelSkeleton;
    const std::array<std::int16_t, 10> sounds{10, 11, 11, 11, 12, -1, -1, -1, -1, -1};
    m_modelSounds = sounds;
    ActorState monster = UnarmedPlayer();
    monster.type = 0x154;
    monster.subtype = 9;
    monster.pet.model = 0x1c37;
    const std::uint32_t seed = SeedWithSuffixGate(8, true);
    CrtRandom expected = AfterDraws(seed, 9);
    const int variant = expected.Next() & 1;
    m_random = CrtRandom(seed);

    const auto stopped = Stop(monster);

    ASSERT_TRUE(stopped.HasValue()) << stopped.Error();
    EXPECT_EQ(stopped.Value().sound, Client::Audio::SoundId{0xd2 + sounds[static_cast<std::size_t>(variant)] + 5});
    EXPECT_EQ(m_random.State(), expected.State());
}

// Both WORDs are read signed: if the second is −1 it is added as such, and it does not end the suffix.
TEST_F(ActorStopTest, TheSecondModelSoundWordIsSignedAndStillAddsTheSubtypeOffset)
{
    m_actorSkeleton = &m_actorModelSkeleton;
    const std::array<std::int16_t, 10> sounds{10, -1, 11, 11, 12, -1, -1, -1, -1, -1};
    m_modelSounds = sounds;
    ActorState monster = UnarmedPlayer();
    monster.type = 0x154;
    monster.subtype = 9;
    const std::uint32_t seed = FirstSeedWhere(
        [](CrtRandom& random)
        {
            random.Discard(8);
            return random.Next() % 16 == 0 && (random.Next() & 1) == 1;
        });
    m_random = CrtRandom(seed);

    const auto stopped = Stop(monster);

    ASSERT_TRUE(stopped.HasValue()) << stopped.Error();
    EXPECT_EQ(stopped.Value().sound, Client::Audio::SoundId{0xd2 - 1 + 5});
    EXPECT_EQ(m_random.State(), AfterDraws(seed, 10).State());
}

// If the suffix does not open the table it need not be lent; if it opens it, the error keeps the earlier changes.
TEST_F(ActorStopTest, MissingModelSoundTableErrorsOnlyWhenTheSuffixReadsIt)
{
    m_actorSkeleton = &m_actorModelSkeleton;
    ActorState monster = UnarmedPlayer();
    monster.type = 0x154;
    const std::uint32_t quiet = SeedWithSuffixGate(8, false);
    m_random = CrtRandom(quiet);
    const auto skipped = Stop(monster);
    ASSERT_TRUE(skipped.HasValue()) << skipped.Error();
    EXPECT_EQ(m_random.State(), AfterDraws(quiet, 9).State());

    monster.run = 40;
    monster.animation.action = 3;
    const std::uint32_t loud = SeedWithSuffixGate(8, true);
    m_random = CrtRandom(loud);
    const auto read = Stop(monster);
    ASSERT_FALSE(read.HasValue());
    EXPECT_NE(read.Error().find("tabla de sonidos"), std::string::npos);
    EXPECT_EQ(monster.run, 0);
    EXPECT_EQ(monster.animation.action, 0);
    EXPECT_EQ(m_random.State(), AfterDraws(loud, 9).State());
}

// On the transformed player too, 209..211 look at the table their caller lends.
TEST_F(ActorStopTest, PlayerSoundSubtypesUseTheBorrowedModelTable)
{
    const std::array<std::int16_t, 10> sounds{10, 11, 11, 11, 12, -1, -1, -1, -1, -1};
    m_modelSounds = sounds;
    const std::uint32_t seed = FirstSeedWhere([](CrtRandom& random) { return random.Next() % 16 == 0; });
    ActorState player = UnarmedPlayer();
    player.subtype = 209;
    CrtRandom expected = AfterDraws(seed, 1);
    const int variant = expected.Next() & 1;
    m_random = CrtRandom(seed);

    const auto stopped = Stop(player);

    ASSERT_TRUE(stopped.HasValue()) << stopped.Error();
    EXPECT_EQ(stopped.Value().sound, Client::Audio::SoundId{0xd2 + sounds[static_cast<std::size_t>(variant)]});
    EXPECT_EQ(m_random.State(), expected.State());
}
} // namespace
