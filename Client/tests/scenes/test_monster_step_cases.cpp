#include "Monsters/MonsterStepCases.h"

#include "Core/Expected.h"
#include "Tables/PlayerModels.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Models/Skeleton.h"
#include "Kernel/ActorState.h"
#include "Actors/Drawing/GameCharacters.h"
#include "Monsters/MonsterCreation.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldTextQueue.h"
#include "Pathfinding/TerrainWalls.h"
#include "support/SceneDataFiles.h"
#include "CastleSiege/CastleSiegeState.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The per-type step cases of characters other than 0x20a: the 0xc5 name (0x4fd603), the 0xf1 fall (0x4fb910),
// 0x42a020 and the map 30 objects of 0x42da90, and 0x42da60 at the end of the draw. Literal vectors from the ASM
// c32bbd5f, without running the original; the names are made up.
namespace
{
using Client::Kernel::ActorState;
using Client::Kernel::WorldRuntime;
using Client::Monsters::CastleObjectStep;

constexpr std::size_t LevelText = 1176;

ActorState NamedGate(std::string_view name, std::uint16_t level)
{
    ActorState gate;
    gate.type = 0xc5;
    gate.level = level;
    gate.position = {1200.0f, 3400.0f, 150.0f};
    std::vector<std::byte> bytes;
    for (const char letter : name)
        bytes.push_back(static_cast<std::byte>(letter));
    bytes.push_back(std::byte{0});
    EXPECT_TRUE(Client::Monsters::PutActorText(gate, bytes, 0x4fd612).HasValue());
    return gate;
}

std::string QueuedText(const Client::Kernel::WorldText& entry)
{
    const auto end = std::ranges::find(entry.text, '\0');
    return {entry.text.begin(), end};
}

// 0x4fd603..0x4fd698: "%s ." with the name, Text[1176] with unsigned +0x1d6 and the strcat of both, into the 0x4381f0
// queue with the object's position.
TEST(MonsterStepCasesTest, TheKalimaGateQueuesItsNameAndLevel)
{
    const auto texts = Client::Tests::MakeTextTable({{LevelText, "Nivel %d"}});
    const ActorState gate = NamedGate("Portal", 3);
    Client::Kernel::WorldTextQueue queue;

    const auto queued = Client::Monsters::QueueTheGateName(gate, &texts, queue);

    ASSERT_TRUE(queued.HasValue()) << queued.Error();
    ASSERT_EQ(queue.size(), 1u);
    EXPECT_EQ(QueuedText(queue.front()), "Portal .Nivel 3");
    EXPECT_EQ(queue.front().position, gate.position);
}

// 0x4fd625: a stone's name with a long owner (24 letters, "Magic stone of " plus nine) reaches +0x1f1; one of 29 with
// no 0 would go on into +0x1f6, which MuRet does not keep, and cuts (Q-C847). The owners are made up.
TEST(MonsterStepCasesTest, TheGateNameReadsUpToItsZero)
{
    const auto texts = Client::Tests::MakeTextTable({{LevelText, "Nivel %d"}});
    Client::Kernel::WorldTextQueue queue;

    ASSERT_TRUE(Client::Monsters::QueueTheGateName(NamedGate("Magic stone of Carpincho", 3), &texts, queue).HasValue());
    ASSERT_EQ(queue.size(), 1u);
    EXPECT_EQ(QueuedText(queue.front()), "Magic stone of Carpincho .Nivel 3");

    const auto unended =
        Client::Monsters::QueueTheGateName(NamedGate("Piedra de mago de Yacare Overo", 3), &texts, queue);
    ASSERT_FALSE(unended.HasValue());
    EXPECT_NE(unended.Error().find("0x4fd625"), std::string::npos) << unended.Error();
    EXPECT_EQ(queue.size(), 1u);
}

// The strcat writes into the 100-byte buffer (0xf8 − 0x94): 99 letters and the 0 fit; with one more the original would
// overwrite the second buffer, and MuRet cuts. Without Text.bmd too.
TEST(MonsterStepCasesTest, TheGateNameCutsWithoutTextsOrPastItsBuffer)
{
    const auto texts = Client::Tests::MakeTextTable({{LevelText, std::string(90, 'x') + "%d"}});
    Client::Kernel::WorldTextQueue queue;

    ASSERT_TRUE(Client::Monsters::QueueTheGateName(NamedGate("Portal", 3), &texts, queue).HasValue());
    ASSERT_EQ(queue.size(), 1u);
    // 0x438209: 0x4381f0 copies 64 bytes (the 0x10 dwords).
    EXPECT_EQ(QueuedText(queue.front()), "Portal ." + std::string(56, 'x'));

    const auto overflow = Client::Monsters::QueueTheGateName(NamedGate("Portal", 10), &texts, queue);
    ASSERT_FALSE(overflow.HasValue());
    EXPECT_NE(overflow.Error().find("0x4fd67c"), std::string::npos) << overflow.Error();
    EXPECT_NE(overflow.Error().find("101"), std::string::npos) << overflow.Error();

    const auto missing = Client::Monsters::QueueTheGateName(NamedGate("Portal", 3), nullptr, queue);
    ASSERT_FALSE(missing.HasValue());
    EXPECT_NE(missing.Error().find("0x4fd63b"), std::string::npos) << missing.Error();
    EXPECT_EQ(queue.size(), 1u);
    // A Text.bmd with an empty 1176 leaves the name alone.
    const auto empty = Client::Tests::MakeTextTable({{1, "otro"}});
    ASSERT_TRUE(Client::Monsters::QueueTheGateName(NamedGate("Portal", 3), &empty, queue).HasValue());
    ASSERT_EQ(queue.size(), 2u);
    EXPECT_EQ(QueuedText(queue.back()), "Portal .");
}

// 0x4fb910..0x4fb9c7: z rises by +0xe4 and +0xe4 drops 6; with the floor plus 30 above, z moves to it and +0xe4
// bounces (× −0.4f); the angles add +0xcc, +0xd0 and +0xd4, which shrink (× 0.8f).
TEST(MonsterStepCasesTest, TheBallFallsBouncesAndSpins)
{
    ActorState ball;
    ball.type = 0xf1;
    ball.position = {500.0f, 700.0f, 260.0f};
    ball.floatE4 = -10.0f;
    ball.angles = {1.0f, 2.0f, 3.0f};
    ball.floatCC = 10.0f;
    ball.floatD0 = -5.0f;
    ball.floatD4 = 2.5f;
    std::vector<glm::vec2> asked;
    const auto ground = [&asked](float x, float y)
    {
        asked.emplace_back(x, y);
        return 200.0;
    };

    // In the air: 260 − 10 = 250, above the floor's 230.
    Client::Monsters::StepTheFallingBall(ball, ground);
    EXPECT_EQ(ball.position.z, 250.0f);
    EXPECT_EQ(ball.floatE4, -16.0f);
    EXPECT_EQ(ball.angles, glm::vec3(11.0f, -3.0f, 5.5f));
    EXPECT_EQ(ball.floatCC, 8.0f);
    EXPECT_EQ(ball.floatD0, -4.0f);
    EXPECT_EQ(ball.floatD4, 2.0f);
    ASSERT_EQ(asked.size(), 1u);
    EXPECT_EQ(asked[0], glm::vec2(500.0f, 700.0f));

    // 250 − 16 = 234, still above; then 234 − 22 = 212: the floor (230) lifts it and +0xe4 bounces.
    Client::Monsters::StepTheFallingBall(ball, ground);
    EXPECT_EQ(ball.position.z, 234.0f);
    Client::Monsters::StepTheFallingBall(ball, ground);
    EXPECT_EQ(ball.position.z, 230.0f);
    EXPECT_EQ(ball.floatE4, static_cast<float>(-28.0 * static_cast<double>(-0.4f)));

    // Exactly at the floor it does not bounce: the fcom needs the floor strictly above.
    ActorState resting;
    resting.position = {0.0f, 0.0f, 236.0f};
    resting.floatE4 = -6.0f;
    Client::Monsters::StepTheFallingBall(resting, ground);
    EXPECT_EQ(resting.position.z, 230.0f);
    EXPECT_EQ(resting.floatE4, -12.0f);
}

ActorState Player(glm::vec3 position)
{
    ActorState player;
    player.alive = true;
    player.byte88 = 1;
    player.type = Client::Tables::PlayerSkeletonModel;
    player.seen = true;
    player.position = position;
    return player;
}

// 0x42a020: only visible (+0x16c) players (+0x88 at 1, the 0x20a); dx and dy are the object minus the player.
TEST(MonsterStepCasesTest, PlayerNearMeasuresByItsMode)
{
    ActorState object;
    object.position = {1000.0f, 1000.0f, 0.0f};
    auto pool = std::make_unique<std::array<ActorState, 401>>();
    const auto near = [&](std::uint8_t mode, float range)
    { return Client::Monsters::PlayerNear(object, *pool, mode, range); };

    // Mode 1: |dx| below 1 and dy in (0, range).
    (*pool)[5] = Player({1000.5f, 600.0f, 0.0f});
    EXPECT_TRUE(near(1, 500.0f));
    EXPECT_FALSE(near(1, 400.0f));
    (*pool)[5].position.x = 1001.0f;
    EXPECT_FALSE(near(1, 500.0f));
    (*pool)[5].position = {1000.0f, 1000.0f, 0.0f};
    EXPECT_FALSE(near(1, 500.0f));
    (*pool)[5].position = {1000.0f, 1100.0f, 0.0f};
    EXPECT_FALSE(near(1, 500.0f));

    // Mode 0: dx below the range and dy below 100, with no absolute value.
    (*pool)[5].position = {5000.0f, 950.0f, 0.0f};
    EXPECT_TRUE(near(0, 10.0f));
    (*pool)[5].position = {5000.0f, 900.0f, 0.0f};
    EXPECT_FALSE(near(0, 10.0f));
    (*pool)[5].position = {900.0f, 950.0f, 0.0f};
    EXPECT_FALSE(near(0, 100.0f));

    // Mode 2: the distance in the plane; any other mode never.
    (*pool)[5].position = {1030.0f, 1040.0f, 0.0f};
    EXPECT_TRUE(near(2, 50.5f));
    EXPECT_FALSE(near(2, 50.0f));
    EXPECT_FALSE(near(3, 1.0e9f));

    // A NaN passes the "below" comparisons (C0) and not dy > 0.
    (*pool)[5].position = {std::numeric_limits<float>::quiet_NaN(), 600.0f, 0.0f};
    EXPECT_TRUE(near(1, 500.0f));
    EXPECT_TRUE(near(2, 1.0f));
    (*pool)[5].position = {1000.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f};
    EXPECT_FALSE(near(1, 500.0f));

    // Those that do not count: another +0x88, another model, not seen, or beyond the 400.
    (*pool)[5] = Player({1000.0f, 600.0f, 0.0f});
    (*pool)[5].byte88 = 2;
    EXPECT_FALSE(near(1, 500.0f));
    (*pool)[5] = Player({1000.0f, 600.0f, 0.0f});
    (*pool)[5].type = 0x153;
    EXPECT_FALSE(near(1, 500.0f));
    (*pool)[5] = Player({1000.0f, 600.0f, 0.0f});
    (*pool)[5].seen = false;
    EXPECT_FALSE(near(1, 500.0f));
    (*pool)[5] = ActorState{};
    (*pool)[400] = Player({1000.0f, 600.0f, 0.0f});
    EXPECT_FALSE(near(1, 500.0f));
    (*pool)[399] = Player({1000.0f, 600.0f, 0.0f});
    EXPECT_TRUE(near(1, 500.0f));
}

class CastleObjectTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_runtime = std::make_unique<WorldRuntime>();
        m_runtime->map = 30;
        m_pool = std::make_unique<std::array<ActorState, 400>>();
        m_skeleton.actions.assign(6, Client::Models::ActionInfo{.repeats = true, .speed = 0.25f, .keyCount = 10});
    }

    CastleObjectStep Step()
    {
        return {.runtime = *m_runtime,
                .pool = *m_pool,
                .height = [](float x, float y) { return static_cast<double>(x + y) * 0.5; },
                .skeleton = &m_skeleton};
    }

    static ActorState Object(std::uint16_t monster)
    {
        ActorState object;
        object.alive = true;
        object.type = 0x19c;
        object.word376 = monster;
        object.position = {300.0f, 500.0f, 10.0f};
        return object;
    }

    std::unique_ptr<WorldRuntime> m_runtime;
    std::unique_ptr<std::array<ActorState, 400>> m_pool;
    Client::Models::Skeleton m_skeleton;
};

// 0x42da9a
TEST_F(CastleObjectTest, OutsideTheCastleMapNothingMoves)
{
    for (const std::int32_t map : {0, 2, 24})
    {
        m_runtime->map = map;
        for (const std::uint16_t monster : std::array<std::uint16_t, 6>{216, 277, 278, 283, 286, 287})
        {
            SCOPED_TRACE(testing::Message() << map << ' ' << monster);
            ActorState object = Object(monster);
            object.byte42d = 0x80;
            object.flags78 = 1;
            const ActorState before = object;
            const auto stepped = Client::Monsters::StepTheCastleObject(object, Step());
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            EXPECT_EQ(object.position, before.position);
            EXPECT_EQ(object.dword100, before.dword100);
            EXPECT_EQ(object.byte42c, before.byte42c);
            EXPECT_EQ(m_runtime->castleSiege.castleGatesClosed, 0);
            EXPECT_EQ(m_runtime->castleSiege.castleMarkX, 0.0f);
            EXPECT_EQ(m_runtime->castleSiege.castleFlags7239e4, 0u);
        }
    }
}

// 216 (0x42dacb): 200 above the terrain, +0x78 to 0x7239e4 and +0x42c to 1 if +0x78 is not 0; without the height it
// cuts.
TEST_F(CastleObjectTest, TheBannerStandsOnTheGroundAndCopiesItsFlags)
{
    ActorState banner = Object(216);
    banner.flags78 = 0x40;
    ASSERT_TRUE(Client::Monsters::StepTheCastleObject(banner, Step()).HasValue());
    EXPECT_EQ(banner.position.z, 600.0f);
    EXPECT_EQ(m_runtime->castleSiege.castleFlags7239e4, 0x40u);
    EXPECT_EQ(banner.byte42c, 1);

    banner.flags78 = 0;
    ASSERT_TRUE(Client::Monsters::StepTheCastleObject(banner, Step()).HasValue());
    EXPECT_EQ(m_runtime->castleSiege.castleFlags7239e4, 0u);
    EXPECT_EQ(banner.byte42c, 0);

    CastleObjectStep flat = Step();
    flat.height = {};
    const auto cut = Client::Monsters::StepTheCastleObject(banner, flat);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x42dad6"), std::string::npos) << cut.Error();
}

// 277 locks the frame's gates (0x7239d9), 278 in mode 0x80 stores its x and y, and 283 follows the siege.
TEST_F(CastleObjectTest, TheGateLockTheMarkAndTheSiegeSwitch)
{
    ActorState lock = Object(277);
    ASSERT_TRUE(Client::Monsters::StepTheCastleObject(lock, Step()).HasValue());
    EXPECT_EQ(m_runtime->castleSiege.castleGatesClosed, 1);

    ActorState mark = Object(278);
    mark.byte42d = 0x40;
    ASSERT_TRUE(Client::Monsters::StepTheCastleObject(mark, Step()).HasValue());
    EXPECT_EQ(m_runtime->castleSiege.castleMarkX, 0.0f);
    mark.byte42d = 0x80;
    ASSERT_TRUE(Client::Monsters::StepTheCastleObject(mark, Step()).HasValue());
    EXPECT_EQ(m_runtime->castleSiege.castleMarkX, 300.0f);
    EXPECT_EQ(m_runtime->castleSiege.castleMarkY, 500.0f);

    ActorState lever = Object(283);
    ASSERT_TRUE(Client::Monsters::StepTheCastleObject(lever, Step()).HasValue());
    EXPECT_EQ(lever.dword58, -2);
    EXPECT_EQ(lever.byte42c, 0);
    m_runtime->castleSiege.siegeActive = true;
    ASSERT_TRUE(Client::Monsters::StepTheCastleObject(lever, Step()).HasValue());
    EXPECT_EQ(lever.dword58, -1);
    EXPECT_EQ(lever.byte42c, 1);
}

// 286 and 287: a player right in front (mode 1, 500) opens (action 4, 0x7239d8 at 2, +0x110 at 1 and +0x100 at 300);
// +0x100 drops 1 per step and, expired with nobody there, closes with action 0. With 0x7239d8 at 2 it opens with
// nobody.
TEST_F(CastleObjectTest, TheGatesOpenForAPlayerInFrontAndCloseWhenTheTimeRunsOut)
{
    for (const std::uint16_t monster : std::array<std::uint16_t, 2>{286, 287})
    {
        SCOPED_TRACE(monster);
        SetUp();
        ActorState gate = Object(monster);
        gate.dword100 = 77;
        ASSERT_TRUE(Client::Monsters::StepTheCastleObject(gate, Step()).HasValue());
        EXPECT_EQ(gate.byte110, 0);
        EXPECT_EQ(gate.dword100, -1);
        EXPECT_EQ(gate.animation.action, 0);

        (*m_pool)[12] = Player({300.0f, 200.0f, 0.0f});
        ASSERT_TRUE(Client::Monsters::StepTheCastleObject(gate, Step()).HasValue());
        EXPECT_EQ(gate.animation.action, 4);
        EXPECT_EQ(m_runtime->castleSiege.castleByte7239d8, 2);
        EXPECT_EQ(gate.byte110, 1);
        EXPECT_EQ(gate.dword100, 299);

        // With someone in front when it expires, back to 300.
        gate.dword100 = -1;
        ASSERT_TRUE(Client::Monsters::StepTheCastleObject(gate, Step()).HasValue());
        EXPECT_EQ(gate.dword100, 299);
        EXPECT_EQ(gate.animation.action, 4);

        (*m_pool)[12] = ActorState{};
        gate.dword100 = -1;
        ASSERT_TRUE(Client::Monsters::StepTheCastleObject(gate, Step()).HasValue());
        EXPECT_EQ(gate.animation.action, 0);
        EXPECT_EQ(gate.byte110, 0);
        EXPECT_EQ(m_runtime->castleSiege.castleByte7239d8, 0);
        EXPECT_EQ(gate.dword100, -2);

        // Another open gate (0x7239d8 at 2) opens this one too.
        m_runtime->castleSiege.castleByte7239d8 = 2;
        ASSERT_TRUE(Client::Monsters::StepTheCastleObject(gate, Step()).HasValue());
        EXPECT_EQ(gate.animation.action, 4);
        EXPECT_EQ(gate.dword100, 299);
    }
}

// Action 5 (opening) does not close; 0x7239d8 at 0 closes before it expires; the dec of +0x100 wraps at the minimum;
// without the model, the action change cuts.
TEST_F(CastleObjectTest, TheGateEdges)
{
    ActorState gate = Object(286);
    gate.byte110 = 1;
    gate.dword100 = -5;
    gate.animation.action = 5;
    ASSERT_TRUE(Client::Monsters::StepTheCastleObject(gate, Step()).HasValue());
    EXPECT_EQ(gate.byte110, 1);
    EXPECT_EQ(gate.dword100, -6);

    gate.animation.action = 4;
    gate.dword100 = 100;
    m_runtime->castleSiege.castleByte7239d8 = 0;
    ASSERT_TRUE(Client::Monsters::StepTheCastleObject(gate, Step()).HasValue());
    EXPECT_EQ(gate.byte110, 0);
    EXPECT_EQ(gate.animation.action, 0);

    gate.byte110 = 1;
    gate.animation.action = 5;
    gate.dword100 = std::numeric_limits<std::int32_t>::min();
    ASSERT_TRUE(Client::Monsters::StepTheCastleObject(gate, Step()).HasValue());
    EXPECT_EQ(gate.dword100, std::numeric_limits<std::int32_t>::max());

    CastleObjectStep bare = Step();
    bare.skeleton = nullptr;
    ActorState closed = Object(287);
    m_runtime->castleSiege.castleByte7239d8 = 2;
    const auto cut = Client::Monsters::StepTheCastleObject(closed, bare);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x42db87"), std::string::npos) << cut.Error();
}

// 0x42da60 at the end of the draw: 0x7239dc and 0x7239e0 to 0; without 277, the TerrainWall gates open; with it, they
// stay, and 0x7239d9 goes back to 0 for the next frame.
TEST(MonsterStepCasesTest, TheEndOfTheFrameOpensTheGatesUnlessTheLockStepped)
{
    const Client::Pathfinding::GateCell gate = Client::Pathfinding::GateCells.front();
    const auto cell = Client::Data::Terrain::CellIndex(gate.x, static_cast<std::uint8_t>(gate.y + 1));
    auto walls = std::make_unique<Client::Data::Terrain::TerrainLayer<std::uint16_t>>();
    WorldRuntime runtime;

    walls->fill(4);
    runtime.castleSiege.castleGatesClosed = 1;
    runtime.castleSiege.castleMarkX = 12.0f;
    runtime.castleSiege.castleMarkY = 34.0f;
    Client::Actors::Drawing::FinishTheCastleFrame(&runtime, *walls);
    EXPECT_EQ((*walls)[cell], 4);
    EXPECT_EQ(runtime.castleSiege.castleGatesClosed, 0);
    EXPECT_EQ(runtime.castleSiege.castleMarkX, 0.0f);
    EXPECT_EQ(runtime.castleSiege.castleMarkY, 0.0f);

    Client::Actors::Drawing::FinishTheCastleFrame(&runtime, *walls);
    EXPECT_EQ((*walls)[cell], 0);

    walls->fill(4);
    Client::Actors::Drawing::FinishTheCastleFrame(nullptr, *walls);
    EXPECT_EQ((*walls)[cell], 0);
}
} // namespace
