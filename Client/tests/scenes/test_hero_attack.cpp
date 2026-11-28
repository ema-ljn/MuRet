#include "Network/Xor32Packet.h"
#include "Combat/Attackable.h"
#include "Hero/HeroAttack.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Network/Packet.h"
#include "Protocol/Combat/Combat.h"
#include "Protocol/Movement/Movement.h"
#include "Models/Skeleton.h"
#include "Combat/ActorCombat.h"
#include "Kernel/ActorState.h"
#include "Movement/GameMovementState.h"
#include "Guild/GuildState.h"
#include "Social/SocialState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Terrain/WorldTerrain.h"

#include "support/PlayerActors.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Whether the chosen one can be attacked (0x558bb0), the 300 ms brake (0x576791c) and case 3 of 0x5650c0, the attack
// on arrival, with the bytes of the turning D3 and of the D7. The keys and names are made up.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Kernel::WorldState;

constexpr std::uint32_t Seed = 99;
constexpr std::uint32_t OwnRecordDraws = 0xf08;
constexpr std::uint32_t CellDraws = 4;

void Name(ActorState& actor, std::string_view name)
{
    actor.name.fill(std::byte{0});
    for (std::size_t i = 0; i < name.size() && i < actor.name.size(); ++i)
        actor.name[i] = static_cast<std::byte>(name[i]);
}

std::vector<std::byte> Plain(const Client::Network::Packet& packet)
{
    const auto decoded = Client::Network::Xor32Decode(packet);
    return {decoded.Bytes().begin(), decoded.Bytes().end()};
}

std::vector<std::byte> Bytes(std::initializer_list<int> values)
{
    std::vector<std::byte> bytes;
    for (const int value : values)
        bytes.push_back(static_cast<std::byte>(value));
    return bytes;
}

class HeroAttackTest : public ::testing::Test
{
protected:
    HeroAttackTest()
    {
        Reset(0);
    }

    // The hero at (182, 115), still, arriving with +0x379 at 3 and the 0x6c8ebc target one cell east.
    void Reset(std::int32_t map)
    {
        m_random = CrtRandom(Seed);
        m_sent.clear();
        Runtime().map = map;
        Runtime().hero = HeroIndex;
        ActorState& hero = Hero();
        hero = Client::Tests::UnarmedPlayer();
        hero.alive = true;
        hero.key = 0x2a;
        hero.byte88 = 1;
        hero.cellX = 182;
        hero.cellY = 115;
        hero.position = {18250.0f, 11550.0f, 0.0f};
        hero.movementType = 3;
        hero.movement = 1;
        ActorState& target = Target();
        target.alive = true;
        target.type = 0x10;
        target.key = TargetKey;
        target.byte88 = 2;
        target.position = {18350.0f, 11550.0f, 0.0f};
        Runtime().movement.attackTarget = TargetIndex;
        Runtime().mouse.updateTime = 0;
        Runtime().mouse.updateTimeMax = 16;
    }

    [[nodiscard]] Client::Kernel::WorldRuntime& Runtime()
    {
        return m_world->Runtime();
    }
    [[nodiscard]] ActorState& Hero()
    {
        return m_world->Actors()[HeroIndex];
    }
    [[nodiscard]] ActorState& Target()
    {
        return m_world->Actors()[TargetIndex];
    }
    // The arrival context with the sends recorded as the session sends them (XOR32 and in clear).
    [[nodiscard]] Client::Hero::HeroArrivalContext Arrival()
    {
        return {.combat = {.world = *m_world, .random = m_random, .playerSkeleton = &m_skeleton, .terrain = &m_terrain},
                .sendWalk = [this](std::span<const Client::Protocol::Movement::Cell> path,
                                   Client::Protocol::Movement::Cell target,
                                   float angle) -> Client::Core::Expected<void, std::string>
                {
                    const auto packet = Client::Protocol::Movement::WriteWalk(path, target, angle);
                    if (!packet.HasValue())
                        return Client::Core::Unexpected(std::string("D3 inválido"));
                    m_sent.push_back(Plain(packet.Value().packet));
                    return {};
                },
                .sendAttack = [this](std::uint16_t key, std::uint8_t action,
                                     std::uint8_t direction) -> Client::Core::Expected<void, std::string>
                {
                    m_sent.push_back(Plain(Client::Protocol::Combat::WriteAttack(key, action, direction).packet));
                    return {};
                }};
    }
    [[nodiscard]] static std::uint32_t After(std::uint32_t count)
    {
        CrtRandom expected(Seed);
        expected.Discard(count);
        return expected.State();
    }
    // The name (+0xd) of entry `index` of the guild table (0x787aa48).
    void GuildName(std::size_t index, std::string_view name)
    {
        auto& text = Runtime().guild.table[index].guildName;
        text.fill('\0');
        name.copy(text.data(), text.size() - 1);
    }
    // The 0x6c8e30 chosen one for 0x558bb0.
    ActorState& Chosen(std::size_t index)
    {
        Runtime().selectedTargets[2] = static_cast<std::int32_t>(index);
        return m_world->Actors()[index];
    }

    static constexpr std::size_t HeroIndex = 3;
    static constexpr std::size_t TargetIndex = 13;
    static constexpr std::uint16_t TargetKey = 0x1234;
    std::unique_ptr<WorldState> m_world = std::make_unique<WorldState>();
    Client::Terrain::WorldTerrain m_terrain;
    CrtRandom m_random{Seed};
    Client::Models::Skeleton m_skeleton = Client::Tests::PlayerActionsSkeleton();
    std::vector<std::vector<std::byte>> m_sent;
};

// 0x565107..0x56601e: the target one cell east (90 degrees, heading 3). The hero takes the hit action, faces the
// target, sends C1 06 D3 X Y 30 (the one-step turn) and C1 07 D7 12 34 78 03, which FoxAnti outputs as A4. The rand():
// the own record of the range and that of 0x4f3f80 (3848 each), and the two cells of the published hero and the two of
// the hitter (4 each). The attack on arrival does not look at the map: the same in Lorencia, Devias and Blood Castle.
TEST_F(HeroAttackTest, InRangeTheHeroTurnsAndSendsTheD3AndTheD7)
{
    for (const std::int32_t map : {0, 2, 11})
    {
        Reset(map);
        const auto attacked = Client::Hero::InteractOnArrival(Hero(), Arrival());
        ASSERT_TRUE(attacked.HasValue()) << attacked.Error();

        ASSERT_EQ(m_sent.size(), 2u);
        EXPECT_EQ(m_sent[0], Bytes({0xc1, 0x06, 0xd3, 182, 115, 0x30}));
        EXPECT_EQ(m_sent[1], Bytes({0xc1, 0x07, 0xd7, 0x12, 0x34, 0x78, 0x03}));
        EXPECT_EQ(Hero().animation.action, 0x22);
        EXPECT_EQ(Hero().attackCount, 1);
        EXPECT_EQ(Hero().byte381, 1);
        EXPECT_EQ(Hero().angles.z, 90.0f);
        EXPECT_EQ(Hero().attackPosition, Target().position);
        EXPECT_EQ(Hero().attackTarget, TargetIndex);
        EXPECT_EQ(Hero().movement, 0);
        EXPECT_EQ(Runtime().mouse.updateTime, 16);
        EXPECT_EQ(Runtime().movement.targetX, 183u);
        EXPECT_EQ(Runtime().movement.targetY, 115u);
        EXPECT_EQ(m_random.State(), After(2 * OwnRecordDraws + 4 * CellDraws));
    }
}

// The target's key travels whole, and the D7's heading is the hero's after turning: the target at y + 100 (180
// degrees) gives heading 5.
TEST_F(HeroAttackTest, TheD7CarriesTheWholeKeyAndTheTurnedDirection)
{
    Target().key = 0x8001;
    Target().position = {18250.0f, 11650.0f, 0.0f};
    ASSERT_TRUE(Client::Hero::InteractOnArrival(Hero(), Arrival()).HasValue());
    ASSERT_EQ(m_sent.size(), 2u);
    EXPECT_EQ(m_sent[0], Bytes({0xc1, 0x06, 0xd3, 182, 115, 0x50}));
    EXPECT_EQ(m_sent[1], Bytes({0xc1, 0x07, 0xd7, 0x80, 0x01, 0x78, 0x05}));
}

// 0x5659e6..0x565a47
TEST_F(HeroAttackTest, AHeroNamedWebzenTurnsButDoesNotHit)
{
    Name(Hero(), "xwebzenx");
    ASSERT_TRUE(Client::Hero::InteractOnArrival(Hero(), Arrival()).HasValue());
    ASSERT_EQ(m_sent.size(), 1u);
    EXPECT_EQ(m_sent[0][2], std::byte{0xd3});
    EXPECT_EQ(Hero().attackTarget, TargetIndex);

    m_sent.clear();
    Name(Hero(), "webze");
    ASSERT_TRUE(Client::Hero::InteractOnArrival(Hero(), Arrival()).HasValue());
    EXPECT_EQ(m_sent.size(), 2u);
}

TEST_F(HeroAttackTest, WithoutSendersTheHeroStillHits)
{
    auto context = Arrival();
    context.sendWalk = nullptr;
    context.sendAttack = nullptr;
    ASSERT_TRUE(Client::Hero::InteractOnArrival(Hero(), context).HasValue());
    EXPECT_TRUE(m_sent.empty());
    EXPECT_EQ(Hero().animation.action, 0x22);
    EXPECT_EQ(m_random.State(), After(2 * OwnRecordDraws + 4 * CellDraws));
}

TEST_F(HeroAttackTest, AFailedTurnSendStillCompletesTheAttackAndAttemptsTheD7)
{
    auto context = Arrival();
    bool attemptedAttack = false;
    context.sendWalk = [](std::span<const Client::Protocol::Movement::Cell>, Client::Protocol::Movement::Cell,
                          float) -> Client::Core::Expected<void, std::string>
    { return Client::Core::Unexpected(std::string("GameServer no abierto")); };
    context.sendAttack = [&](std::uint16_t, std::uint8_t, std::uint8_t) -> Client::Core::Expected<void, std::string>
    {
        attemptedAttack = true;
        return {};
    };

    const auto attacked = Client::Hero::InteractOnArrival(Hero(), context);

    ASSERT_TRUE(attacked.HasValue()) << attacked.Error();
    EXPECT_TRUE(attemptedAttack);
    EXPECT_EQ(Hero().movement, 0);
    EXPECT_EQ(Hero().attackTarget, TargetIndex);
    EXPECT_EQ(m_random.State(), After(2 * OwnRecordDraws + 4 * CellDraws));
}

TEST_F(HeroAttackTest, AFailedAttackSendDoesNotUndoTheCompletedArrival)
{
    auto context = Arrival();
    bool attemptedAttack = false;
    context.sendAttack = [&](std::uint16_t, std::uint8_t, std::uint8_t) -> Client::Core::Expected<void, std::string>
    {
        attemptedAttack = true;
        return Client::Core::Unexpected(std::string("GameServer no abierto"));
    };

    const auto attacked = Client::Hero::InteractOnArrival(Hero(), context);

    ASSERT_TRUE(attacked.HasValue()) << attacked.Error();
    EXPECT_TRUE(attemptedAttack);
    EXPECT_EQ(Hero().movement, 0);
    EXPECT_EQ(Hero().attackTarget, TargetIndex);
    EXPECT_EQ(m_random.State(), After(2 * OwnRecordDraws + 4 * CellDraws));
}

// 0x565107..0x56526a: the range is 1.8 cells bare-handed or with a sword, 2.2 with spears (0x600..0x7ff in the right
// hand), 6 with bows and crossbows and 7 with +0x376 at 9. A target two cells away is out with 1.8 and in with 2.2.
TEST_F(HeroAttackTest, TheRangeFollowsTheOwnWeapons)
{
    Target().position = {18450.0f, 11550.0f, 0.0f};
    ASSERT_TRUE(Client::Hero::InteractOnArrival(Hero(), Arrival()).HasValue());
    EXPECT_TRUE(m_sent.empty());
    EXPECT_EQ(Hero().byte381, 0);
    EXPECT_EQ(Runtime().movement.targetX, 184u);
    EXPECT_EQ(m_random.State(), After(OwnRecordDraws));

    m_world->OwnCharacter().equipment[0].id = 0x600;
    ASSERT_TRUE(Client::Hero::InteractOnArrival(Hero(), Arrival()).HasValue());
    EXPECT_EQ(m_sent.size(), 2u);

    // At six cells, the left-hand bow reaches.
    m_sent.clear();
    Hero().animation.action = 0;
    Target().position = {18850.0f, 11550.0f, 0.0f};
    m_world->OwnCharacter().equipment[0].id = 0xffff;
    m_world->OwnCharacter().equipment[1].id = 0x800;
    ASSERT_TRUE(Client::Hero::InteractOnArrival(Hero(), Arrival()).HasValue());
    EXPECT_EQ(m_sent.size(), 2u);

    m_sent.clear();
    Target().position = {18950.0f, 11550.0f, 0.0f};
    ASSERT_TRUE(Client::Hero::InteractOnArrival(Hero(), Arrival()).HasValue());
    EXPECT_TRUE(m_sent.empty());
    Hero().word376 = 9;
    ASSERT_TRUE(Client::Hero::InteractOnArrival(Hero(), Arrival()).HasValue());
    EXPECT_EQ(m_sent.size(), 2u);
}

// 0x565349..0x566299: out of range only the elf approaches. It reads the Y and the X of its cell and looks with
// PathFinding2 for a path to the cells within range of the target (1.8 without a bow: r = 1, the 3 × 3 square around
// (192, 115)); with a path, +0x378 to 1, without D3 or D7. Without TerrainWall it cuts.
TEST_F(HeroAttackTest, OutOfRangeOnlyTheElfWalksIntoRange)
{
    Target().position = {19250.0f, 11550.0f, 0.0f};
    Hero().movement = 0;
    ASSERT_TRUE(Client::Hero::InteractOnArrival(Hero(), Arrival()).HasValue());
    EXPECT_EQ(Hero().movement, 0);
    EXPECT_EQ(m_random.State(), After(OwnRecordDraws));

    Hero().characterClass = 2;
    m_random = CrtRandom(Seed);
    const auto cut = Client::Hero::InteractOnArrival(Hero(), Arrival());
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x566272"), std::string::npos);

    m_terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    m_random = CrtRandom(Seed);
    ASSERT_TRUE(Client::Hero::InteractOnArrival(Hero(), Arrival()).HasValue());
    EXPECT_EQ(Hero().movement, 1);
    EXPECT_TRUE(m_sent.empty());
    EXPECT_EQ(Hero().byte381, 0);
    ASSERT_EQ(Hero().path.count, 10u);
    EXPECT_EQ(Hero().path.cells[0].x, 182);
    EXPECT_EQ(Hero().path.cells[9].x, 191);
    EXPECT_EQ(m_random.State(), After(OwnRecordDraws + 2 * CellDraws));
}

// The elf out of range with the target in column 256, which PathFinding2 (0x566272) takes as a linear index: MuRet
// discards the approach after reading the Y and the X (07, Q-C1179). With Crywolf in stage 3, on map 34 and an altar
// (0x206) at 0x6c8e54, PathFinding2 sets the flag for the A* (0x4eac0a..0x4eac41), which does not count with a nonzero
// range (0x4eadd4): the elf approaches by the same path as in Lorencia and in Devias.
TEST_F(HeroAttackTest, TheElfDiscardsATargetOffTheGridAndWalksTheSameWithACrywolfAltar)
{
    constexpr std::size_t AltarIndex = 30;
    for (const std::int32_t map : {0, 2, 34})
    {
        for (const bool offTheGrid : {true, false})
        {
            SCOPED_TRACE(::testing::Message() << "mapa " << map << (offTheGrid ? ", la columna 256" : ", el altar"));
            Reset(map);
            m_terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
            Hero().characterClass = 2;
            Hero().movement = 0;
            Hero().path = {};
            Target().position = {offTheGrid ? 25650.0f : 19250.0f, 11550.0f, 0.0f};
            Runtime().crywolf.stage = 3;
            m_world->Actors()[AltarIndex].type = 0x206;
            Runtime().chosenNpc.index = offTheGrid ? -1 : static_cast<std::int32_t>(AltarIndex);

            const auto arrived = Client::Hero::InteractOnArrival(Hero(), Arrival());

            EXPECT_EQ(m_random.State(), After(OwnRecordDraws + 2 * CellDraws));
            EXPECT_TRUE(m_sent.empty());
            ASSERT_TRUE(arrived.HasValue()) << arrived.Error();
            EXPECT_EQ(Runtime().movement.targetX, offTheGrid ? 256u : 192u);
            EXPECT_EQ(Hero().movement, offTheGrid ? 0 : 1);
            EXPECT_EQ(Hero().path.count, offTheGrid ? 0u : 10u);
            if (!offTheGrid)
            {
                EXPECT_EQ(Hero().path.cells[0].x, 182);
                EXPECT_EQ(Hero().path.cells[9].x, 191);
            }
        }
    }
}

// The exits before hitting: no target (0x6c8ebc at −1) after the range, the hero already hitting (0x8d, or 3 and 4 if
// it is not a player) and the safe zone (after storing the target's cell).
TEST_F(HeroAttackTest, TheEarlyExitsDoNotHit)
{
    Runtime().movement.attackTarget = -1;
    ASSERT_TRUE(Client::Hero::InteractOnArrival(Hero(), Arrival()).HasValue());
    EXPECT_EQ(m_random.State(), After(OwnRecordDraws));

    Runtime().movement.attackTarget = TargetIndex;
    Hero().animation.action = 0x8d;
    ASSERT_TRUE(Client::Hero::InteractOnArrival(Hero(), Arrival()).HasValue());
    EXPECT_EQ(Runtime().movement.targetX, 0u);

    Hero().animation.action = 0;
    Hero().safeZone = 1;
    ASSERT_TRUE(Client::Hero::InteractOnArrival(Hero(), Arrival()).HasValue());
    EXPECT_EQ(Runtime().movement.targetX, 183u);
    EXPECT_TRUE(m_sent.empty());
    EXPECT_EQ(Hero().byte381, 0);
}

// 0x565289..0x565296: between 0x22 and 0x97 only 0x8d exits; with another of those actions the hero hits again.
TEST_F(HeroAttackTest, OnlyAction8DStopsAPlayerThatIsAlreadyHitting)
{
    Hero().animation.action = 0x23;
    ASSERT_TRUE(Client::Hero::InteractOnArrival(Hero(), Arrival()).HasValue());
    EXPECT_EQ(m_sent.size(), 2u);
}

// 0x5650ef..0x565100: the table by +0x379 − 1. 0 and those outside 1 to 5 do nothing and take no rand(); 1, 2 and 4
// cut with their milestone's address (5, in test_hero_arrival_skill.cpp).
TEST_F(HeroAttackTest, TheOtherArrivalCasesCutWithTheirAddress)
{
    for (const std::uint8_t type : {std::uint8_t{0}, std::uint8_t{6}, std::uint8_t{0xff}})
    {
        Hero().movementType = type;
        ASSERT_TRUE(Client::Hero::InteractOnArrival(Hero(), Arrival()).HasValue());
    }
    EXPECT_EQ(m_random.State(), After(0));
    struct Case
    {
        std::uint8_t type;
        std::string_view address;
    };
    for (const Case probe : {Case{1, "0x566755"}, Case{2, "0x5674c8"}, Case{4, "0x5695b4"}})
    {
        Hero().movementType = probe.type;
        const auto cut = Client::Hero::InteractOnArrival(Hero(), Arrival());
        ASSERT_FALSE(cut.HasValue());
        EXPECT_NE(cut.Error().find(probe.address), std::string::npos) << cut.Error();
    }
    EXPECT_TRUE(m_sent.empty());
}

// 0x57f2d1..0x57f2f9: |GetTickCount() − [0x576791c]| of 300 or less does not pass; if it passes, it stores a new
// GetTickCount.
TEST(ActionBrakeTest, ThreeHundredMillisecondsOrLessDoNotPass)
{
    std::vector<std::uint32_t> ticks;
    std::size_t calls = 0;
    const std::function<std::uint32_t()> tickCount = [&]() { return ticks[calls++]; };

    std::uint32_t last = 1000;
    ticks = {1300};
    EXPECT_FALSE(Client::Combat::PassesTheActionBrake(last, 0, tickCount));
    EXPECT_EQ(calls, 1u);
    EXPECT_EQ(last, 1000u);

    calls = 0;
    ticks = {1301, 1305};
    EXPECT_TRUE(Client::Combat::PassesTheActionBrake(last, 0, tickCount));
    EXPECT_EQ(calls, 2u);
    EXPECT_EQ(last, 1305u);
}

// The subtraction is signed 32-bit and its absolute value is taken (CDQ/XOR/SUB): a clock that goes back more than 300
// passes, and one that wrapped around by a little does not.
TEST(ActionBrakeTest, TheDifferenceIsSignedAndAbsolute)
{
    std::uint32_t now = 0;
    const std::function<std::uint32_t()> tickCount = [&]() { return now; };

    std::uint32_t last = 2000;
    now = 1000;
    EXPECT_TRUE(Client::Combat::PassesTheActionBrake(last, 0, tickCount));
    EXPECT_EQ(last, 1000u);

    last = 1200;
    now = 1000;
    EXPECT_FALSE(Client::Combat::PassesTheActionBrake(last, 0, tickCount));

    last = 0xffffff00u;
    now = 0x10;
    EXPECT_FALSE(Client::Combat::PassesTheActionBrake(last, 0, tickCount));
}

// With skill 0x28 there is no brake: it stores the clock with a single call.
TEST(ActionBrakeTest, TheSkill28SkipsTheBrake)
{
    std::size_t calls = 0;
    const std::function<std::uint32_t()> tickCount = [&]() { return static_cast<std::uint32_t>(1001 + calls++); };
    std::uint32_t last = 1000;
    EXPECT_TRUE(Client::Combat::PassesTheActionBrake(last, 0x28, tickCount));
    EXPECT_EQ(calls, 1u);
    EXPECT_EQ(last, 1001u);
}

// 0x558bb0: with an item on the cursor or with none chosen, no; in Chaos Castle, anyone who is not the hero.
TEST_F(HeroAttackTest, NothingIsAttackableWithAnItemOnTheCursorOrWithoutAChosen)
{
    EXPECT_FALSE(Client::Combat::IsAttackable(*m_world, true).Value());
    Chosen(TargetIndex);
    Runtime().cursorItemCount = 1;
    EXPECT_FALSE(Client::Combat::IsAttackable(*m_world, true).Value());
    Runtime().cursorItemCount = -1;
    EXPECT_TRUE(Client::Combat::IsAttackable(*m_world, false).Value());

    Runtime().selectedTargets[2] = 401;
    EXPECT_FALSE(Client::Combat::IsAttackable(*m_world, false).HasValue());
}

TEST_F(HeroAttackTest, InChaosCastleEveryoneButTheHeroIsAttackable)
{
    for (const std::int32_t map : {18, 23})
    {
        Runtime().map = map;
        ActorState& other = Chosen(20);
        other.byte88 = 0;
        EXPECT_TRUE(Client::Combat::IsAttackable(*m_world, false).Value());
        Chosen(HeroIndex);
        EXPECT_FALSE(Client::Combat::IsAttackable(*m_world, true).Value());
    }
}

// A monster can be attacked except in the guild war (0x576796c) without 0x576796d; a player by +0x370 at 2/8, PK 6 or
// more (the hero too), guild war or duel; otherwise, Ctrl and not being the hero.
TEST_F(HeroAttackTest, MonstersAndPlayersFollowTheRules)
{
    Chosen(TargetIndex);
    for (const std::int32_t map : {0, 2})
    {
        Runtime().map = map;
        EXPECT_TRUE(Client::Combat::IsAttackable(*m_world, false).Value());
    }
    Runtime().guild.warOn = 1;
    EXPECT_FALSE(Client::Combat::IsAttackable(*m_world, false).Value());
    Runtime().guild.warType = 1;
    EXPECT_TRUE(Client::Combat::IsAttackable(*m_world, false).Value());
    Runtime().guild.warOn = 0;
    Runtime().guild.warType = 0;

    ActorState& player = Chosen(20);
    player = Client::Tests::UnarmedPlayer();
    player.byte88 = 1;
    player.pk = 3;
    EXPECT_FALSE(Client::Combat::IsAttackable(*m_world, false).Value());
    EXPECT_TRUE(Client::Combat::IsAttackable(*m_world, true).Value());
    player.pk = 6;
    EXPECT_TRUE(Client::Combat::IsAttackable(*m_world, false).Value());
    player.pk = 3;
    player.byte370 = 8;
    EXPECT_TRUE(Client::Combat::IsAttackable(*m_world, false).Value());

    Chosen(HeroIndex);
    EXPECT_FALSE(Client::Combat::IsAttackable(*m_world, true).Value());
}

// 0x558e74..0x558e7b: with 5, not even with Ctrl.
TEST_F(HeroAttackTest, TheOutlawThresholdAllowsTheHeroAtSixButNotFive)
{
    Chosen(HeroIndex);
    Hero().pk = 5;
    EXPECT_FALSE(Client::Combat::IsAttackable(*m_world, false).Value());
    EXPECT_FALSE(Client::Combat::IsAttackable(*m_world, true).Value());

    Hero().pk = 6;
    EXPECT_TRUE(Client::Combat::IsAttackable(*m_world, false).Value());
    EXPECT_TRUE(Client::Combat::IsAttackable(*m_world, true).Value());
}

// 0x558d6e..0x558d7e: +0x370 at 2/8 comes before the guild, duel and PK filters.
TEST_F(HeroAttackTest, Byte370OverridesGuildDuelAndPkFilters)
{
    ActorState& player = Chosen(20);
    player = Client::Tests::UnarmedPlayer();
    player.byte88 = 1;
    player.pk = 6;
    player.word1f2 = 3;
    Name(player, "not-rival");
    Runtime().guild.warOn = 1;
    Runtime().social.duel.active = 1;
    Runtime().social.duel.rivalKey = 0x7777;

    for (const std::uint8_t byte370 : {std::uint8_t{2}, std::uint8_t{8}})
    {
        SCOPED_TRACE(byte370);
        player.byte370 = byte370;
        EXPECT_TRUE(Client::Combat::IsAttackable(*m_world, false).Value());
    }
}

// In the guild war +0x374 at 2 counts; with PK 6 and a guild, if their guild has the hero's guild name (strcmp of
// 0x787aa55 at 0x558da4) it is not attackable, even with the control or the mark; with another name it follows the
// mark.
TEST_F(HeroAttackTest, TheGuildWarLooksAtTheEnemyMark)
{
    Runtime().guild.warOn = 1;
    ActorState& player = Chosen(20);
    player = Client::Tests::UnarmedPlayer();
    player.byte88 = 1;
    EXPECT_FALSE(Client::Combat::IsAttackable(*m_world, true).Value());
    player.byte374 = 2;
    EXPECT_TRUE(Client::Combat::IsAttackable(*m_world, false).Value());
    player.pk = 6;
    player.word1f2 = 3;
    Hero().word1f2 = 5;
    GuildName(3, "Rojos");
    GuildName(5, "Rojos");
    for (const bool controlDown : {false, true})
    {
        const auto attackable = Client::Combat::IsAttackable(*m_world, controlDown);
        ASSERT_TRUE(attackable.HasValue()) << attackable.Error();
        EXPECT_FALSE(attackable.Value());
    }
    GuildName(5, "Azules");
    EXPECT_TRUE(Client::Combat::IsAttackable(*m_world, false).Value());
    player.byte374 = 0;
    EXPECT_FALSE(Client::Combat::IsAttackable(*m_world, true).Value());

    // A hero without a guild (−1) compares with what lies before the table (Q-C1281): byte 9 of member 74.
    Hero().word1f2 = 0xffff;
    player.byte374 = 2;
    EXPECT_TRUE(Client::Combat::IsAttackable(*m_world, false).Value());
    // An index outside the table would read other memory: it cuts.
    player.word1f2 = 2000;
    const auto outside = Client::Combat::IsAttackable(*m_world, false);
    ASSERT_FALSE(outside.HasValue());
    EXPECT_NE(outside.Error().find("0x558da4"), std::string::npos);
}

// In the duel (0x78abad8), only the rival: the key at 0x78abadc and the name at 0x78abae0, those of SocialState::duel
// that the duel packets write (08 P-C206 b).
TEST_F(HeroAttackTest, InADuelOnlyTheRivalIsAttackable)
{
    ActorState& player = Chosen(20);
    player = Client::Tests::UnarmedPlayer();
    player.byte88 = 1;
    player.key = 0x77;
    player.pk = 6;
    Name(player, "rival");
    Client::Social::DuelState& duel = Runtime().social.duel;
    duel.active = 1;
    duel.rivalKey = 0x77;
    for (std::size_t i = 0; i < 5; ++i)
        duel.rivalName[i] = static_cast<std::byte>(std::string_view("rival")[i]);
    EXPECT_TRUE(Client::Combat::IsAttackable(*m_world, false).Value());
    EXPECT_EQ(Client::Combat::ChosenKeyOf(*m_world, HeroIndex, false).Value(), 0x77);
    duel.rivalKey = 0x78;
    EXPECT_FALSE(Client::Combat::IsAttackable(*m_world, true).Value());
    EXPECT_EQ(Client::Combat::ChosenKeyOf(*m_world, HeroIndex, true).Value(), -1);
}

// 0x558e0f and 0x558f94: the character's key goes with MOVSX against the DWORD at 0x78abadc, which the duel writes
// unsigned: a rival from 0x8000 never matches, as in the original.
TEST_F(HeroAttackTest, ARivalKeyFrom0x8000NeverMatches)
{
    ActorState& player = Chosen(20);
    player = Client::Tests::UnarmedPlayer();
    player.byte88 = 1;
    player.key = 0x8001;
    Name(player, "rival");
    Client::Social::DuelState& duel = Runtime().social.duel;
    duel.active = 1;
    duel.rivalKey = 0x8001;
    for (std::size_t i = 0; i < 5; ++i)
        duel.rivalName[i] = static_cast<std::byte>(std::string_view("rival")[i]);
    EXPECT_FALSE(Client::Combat::IsAttackable(*m_world, true).Value());
    EXPECT_EQ(Client::Combat::ChosenKeyOf(*m_world, HeroIndex, true).Value(), -1);
    duel.rivalKey = -0x7fff;
    EXPECT_TRUE(Client::Combat::IsAttackable(*m_world, false).Value());
    EXPECT_EQ(Client::Combat::ChosenKeyOf(*m_world, HeroIndex, false).Value(), -0x7fff);
}

// 0x558c2a..0x558d6d: in the siege, an attacker (+0x360 at 6 to 11) against a defender (12 or 13) decides by the
// chosen one's 0x4000 shield.
TEST_F(HeroAttackTest, InTheSiegeTheSidesDecideByTheShield)
{
    Runtime().castleFlags.siegeActive = true;
    Hero().byte360 = 9;
    ActorState& player = Chosen(20);
    player = Client::Tests::UnarmedPlayer();
    player.byte88 = 1;
    player.byte360 = 13;
    EXPECT_TRUE(Client::Combat::IsAttackable(*m_world, false).Value());
    player.flags78 = 0x4000;
    EXPECT_FALSE(Client::Combat::IsAttackable(*m_world, true).Value());

    // On the same side it does not decide: it goes on as without the siege.
    player.flags78 = 0;
    player.byte360 = 9;
    EXPECT_FALSE(Client::Combat::IsAttackable(*m_world, false).Value());
    EXPECT_TRUE(Client::Combat::IsAttackable(*m_world, true).Value());
}

// 0x558c5b..0x558c7e: the defender can also attack the attacker; the target's 0x4000 shield blocks both ways.
TEST_F(HeroAttackTest, InTheSiegeDefendersCanAttackUnshieldedAttackers)
{
    Runtime().castleFlags.siegeActive = true;
    Hero().byte360 = 13;
    ActorState& player = Chosen(20);
    player = Client::Tests::UnarmedPlayer();
    player.byte88 = 1;
    player.byte360 = 6;
    EXPECT_TRUE(Client::Combat::IsAttackable(*m_world, false).Value());
    player.flags78 = 0x4000;
    EXPECT_FALSE(Client::Combat::IsAttackable(*m_world, true).Value());
}

// 0x558cc1..0x558d59: marks 0x80000/0x400000/0x800000 compare against an unmarked target; if both have a mark, it
// continues as outside the siege. The hero's 0x40000 is a separate rule when it has none of those three marks.
TEST_F(HeroAttackTest, SiegeMarksUseTheShieldAndBothMarkedSidesContinue)
{
    Runtime().castleFlags.siegeActive = true;
    ActorState& player = Chosen(20);
    player = Client::Tests::UnarmedPlayer();
    player.byte88 = 1;
    player.pk = 3;
    Hero().flags78 = 0x80000;
    player.flags78 = 0;
    EXPECT_TRUE(Client::Combat::IsAttackable(*m_world, false).Value());
    player.flags78 = 0x4000;
    EXPECT_FALSE(Client::Combat::IsAttackable(*m_world, true).Value());

    player.pk = 6;
    player.flags78 = 0x400000 | 0x4000;
    EXPECT_TRUE(Client::Combat::IsAttackable(*m_world, false).Value());

    Hero().flags78 = 0x40000;
    player.pk = 3;
    player.flags78 = 0;
    EXPECT_TRUE(Client::Combat::IsAttackable(*m_world, false).Value());
    player.flags78 = 0x4000;
    EXPECT_FALSE(Client::Combat::IsAttackable(*m_world, false).Value());
}
} // namespace
