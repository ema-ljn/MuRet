#include "Network/Xor32Packet.h"
#include "Hero/HeroAttack.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Tables/SkillTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Pathfinding/PathFinder.h"
#include "Network/Delivery.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Models/Skeleton.h"
#include "Kernel/ActorState.h"
#include "Movement/GameMovementState.h"
#include "Skills/HeroSkillCasts.h"
#include "Skills/HeroSkills.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Skills/Skill55cbc0.h"
#include "Skills/Skill55faa0.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Terrain/WorldTerrain.h"

#include "support/PlayerActors.h"
#include "support/TableFiles.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

// Case 5 of 0x5650c0 (0x56601f..0x566754): the hero arrives with +0x379 at 5 and uses the pending weapon skill
// (0x7758580..0x7758588). Each case against the direct call (0x55cbc0, 0x55b6f0 or 0x55faa0) after the same rand(), in
// Lorencia (0), Devias (2) and Blood Castle (11, the map of 0x55b6f0).
namespace
{
using Client::Crt::CrtRandom;
using Client::Skills::HeroSkillContext;

constexpr int OwnDraws = 3848;
constexpr int EntryDraws = 60;
constexpr int CellDraws = 4;
constexpr int SendDraws = 2;
// The skill (the own record) and its range (0x5526f0, the Skill.bmd entry).
constexpr int SkillDraws = OwnDraws + EntryDraws;
// The hero's logical cell: the Y and the X.
constexpr int HeroCellDraws = 2 * CellDraws;
constexpr std::size_t TargetSlot = 5;
constexpr std::uint16_t TargetKey = 0x0a1b;
constexpr std::uint32_t Ticks = 100000;
constexpr std::uint8_t ElfClass = 2;
constexpr std::uint16_t BowModel = 0xa12;
constexpr std::uint16_t UniriaModel = 0x1c14;

CrtRandom Advanced(CrtRandom random, int count)
{
    for (int i = 0; i < count; ++i)
        static_cast<void>(random.Next());
    return random;
}

// Skill.bmd with the range (+0x26) of every skill.
Client::Tables::SkillTable SkillsWithRange(std::uint8_t range)
{
    std::vector<std::byte> plain(Client::Tables::SkillTableSize);
    for (std::size_t skill = 0; skill < 0x100; ++skill)
        plain[skill * Client::Tables::SkillRecordSize + 0x26] = static_cast<std::byte>(range);
    const std::vector<std::byte> encrypted =
        Client::Tests::EncryptRecords(std::move(plain), Client::Tables::SkillRecordSize);
    return Client::Tables::SkillTable(std::span<const std::byte>(encrypted));
}

std::vector<std::uint8_t> ClearBytesOf(const Client::Protocol::Outgoing& outgoing)
{
    const Client::Network::Packet clear = Client::Network::Xor32Decode(outgoing.packet);
    std::vector<std::uint8_t> bytes;
    for (const std::byte value : clear.Bytes())
        bytes.push_back(std::to_integer<std::uint8_t>(value));
    return bytes;
}

struct ArrivingHero
{
    ArrivingHero(std::uint8_t used, std::int32_t map, std::uint8_t range = 4) : skills(SkillsWithRange(range))
    {
        auto& runtime = world.Runtime();
        runtime.map = map;
        terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        auto& hero = Hero();
        hero.alive = true;
        hero.type = Client::Tables::PlayerSkeletonModel;
        hero.characterClass = ElfClass;
        hero.cellX = 182;
        hero.cellY = 115;
        hero.position = {18250.0f, 11550.0f, 0.0f};
        hero.hands[0].model = hero.wings.model = hero.pet.model = Client::Kernel::NoItemModel;
        hero.hands[1].model = BowModel;
        hero.alpha = 1.0f;
        hero.animation.action = 1;
        hero.movementType = 5;
        world.OwnCharacter().SetSkillAt(3, used);
        auto& target = Target();
        target.alive = true;
        target.type = 0x20;
        target.byte88 = 2;
        target.byte370 = 2;
        target.key = TargetKey;
        target.alpha = 1.0f;
        target.position = {18450.0f, 11750.0f, 0.0f};
        runtime.movement.weaponSkill = {.active = 1, .slot = 3, .target = static_cast<std::int32_t>(TargetSlot)};
    }
    Client::Kernel::ActorState& Hero()
    {
        return world.Actors()[0];
    }
    Client::Kernel::ActorState& Target()
    {
        return world.Actors()[TargetSlot];
    }
    Client::Movement::GameMovementState& Movement()
    {
        return world.Runtime().movement;
    }
    void Send(Client::Protocol::Outgoing outgoing)
    {
        sent.push_back(std::move(outgoing));
    }
    Client::Hero::HeroArrivalContext Arrival()
    {
        return {.combat = {.world = world, .random = random, .playerSkeleton = &skeleton, .terrain = &terrain},
                .skills = &skills,
                .tickCount = [this] { return ticks; },
                .sendSkill = [this](Client::Protocol::Outgoing outgoing) { Send(std::move(outgoing)); }};
    }
    // The same context, to call 0x55cbc0, 0x55b6f0 or 0x55faa0 directly.
    HeroSkillContext Direct()
    {
        return {.world = world,
                .random = random,
                .skills = &skills,
                .playerSkeleton = &skeleton,
                .tickCount = [this] { return ticks; },
                .send = [this](Client::Protocol::Outgoing outgoing) { Send(std::move(outgoing)); },
                .terrain = &terrain};
    }
    Client::Core::Expected<void, std::string> Arrive()
    {
        return Client::Hero::InteractOnArrival(Hero(), Arrival());
    }
    std::vector<std::uint8_t> Sent(std::size_t index) const
    {
        return ClearBytesOf(sent.at(index));
    }

    Client::Kernel::WorldState world;
    Client::Terrain::WorldTerrain terrain;
    CrtRandom random;
    std::uint32_t ticks = Ticks;
    Client::Models::Skeleton skeleton = Client::Tests::PlayerActionsSkeleton();
    std::vector<Client::Protocol::Outgoing> sent;
    Client::Tables::SkillTable skills;
};

std::unique_ptr<ArrivingHero> Arriving(std::uint8_t skill, std::int32_t map, std::uint8_t range = 4)
{
    return std::make_unique<ArrivingHero>(skill, map, range);
}

using DirectCall = std::function<Client::Core::Expected<void, std::string>(ArrivingHero&)>;

// The arrival against the direct call after `before` rand() and with the target's cell already stored: the same
// rand(), the same bytes and the same hero.
void ExpectTheDirectCall(std::uint8_t skill, std::int32_t map, int before, const DirectCall& call,
                         const std::function<void(ArrivingHero&)>& prepare = {})
{
    SCOPED_TRACE(skill);
    SCOPED_TRACE(map);
    const auto arrived = Arriving(skill, map);
    const auto direct = Arriving(skill, map);
    if (prepare)
    {
        prepare(*arrived);
        prepare(*direct);
    }
    ASSERT_TRUE(arrived->Arrive().HasValue());
    direct->random = Advanced(direct->random, before);
    direct->Movement().targetX = 184;
    direct->Movement().targetY = 117;
    ASSERT_TRUE(call(*direct).HasValue());
    EXPECT_EQ(arrived->random.State(), direct->random.State());
    ASSERT_EQ(arrived->sent.size(), direct->sent.size());
    for (std::size_t i = 0; i < arrived->sent.size(); ++i)
    {
        EXPECT_EQ(arrived->sent[i].delivery, direct->sent[i].delivery);
        EXPECT_EQ(arrived->Sent(i), direct->Sent(i));
    }
    EXPECT_EQ(arrived->Movement().targetX, 184u);
    EXPECT_EQ(arrived->Movement().targetY, 117u);
    EXPECT_EQ(arrived->Hero().animation.action, direct->Hero().animation.action);
    EXPECT_EQ(arrived->Hero().attackPosition, direct->Hero().attackPosition);
    EXPECT_FLOAT_EQ(arrived->Hero().angles.z, direct->Hero().angles.z);
    EXPECT_EQ(arrived->Hero().movement, direct->Hero().movement);
    EXPECT_EQ(arrived->Hero().movementType, 5);
}

Client::Core::Expected<void, std::string> OnItsTarget(ArrivingHero& hero)
{
    return Client::Skills::UseTheSkillOnItsTarget(hero.Hero(), hero.Hero(), hero.Direct());
}

Client::Core::Expected<void, std::string> AtTheTarget(ArrivingHero& hero)
{
    return Client::Skills::CastTheSkillAtTheTarget(hero.Hero(), hero.Hero(), hero.Direct());
}

Client::Core::Expected<void, std::string> ElfOnItsTarget(ArrivingHero& hero)
{
    return Client::Skills::UseTheElfSkillOnItsTarget(hero.Hero(), hero.Hero(), hero.Direct());
}

// 0x56629a..0x5663b7: 1 to 4, 7, 0xb, 0x11, 0x2d and 0x49 in range (· 100) and in sight: 0x55cbc0(hero, hero) after the
// skill, the range and the hero's cell. Skill 1: the 19 with the target's key and the D3 with the new heading.
TEST(HeroArrivalSkillTest, TheFirstGroupGoesTo55cbc0)
{
    for (const std::int32_t map : {0, 2})
    {
        for (const std::uint8_t skill : {std::uint8_t{0x1}, std::uint8_t{0x4}, std::uint8_t{0x11}, std::uint8_t{0x2d}})
            ExpectTheDirectCall(skill, map, SkillDraws + HeroCellDraws, OnItsTarget);
    }
    const auto tested = Arriving(0x1, 0);
    ASSERT_TRUE(tested->Arrive().HasValue());
    ASSERT_EQ(tested->sent.size(), 2u);
    EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, 0x01, 0x0a, 0x1b}));
    EXPECT_EQ(tested->sent[1].delivery, Client::Network::Delivery::Clear);
    EXPECT_EQ(tested->Sent(1)[2], 0xd3);
    EXPECT_EQ(tested->Sent(1)[3], 182);
    EXPECT_EQ(tested->Sent(1)[4], 115);
    EXPECT_FLOAT_EQ(tested->Hero().angles.z, 135.0f);
}

// 0x566143..0x5661fc: 0x13 to 0x17, 0x2b, 0x2c, 0x31, 0x39, 0x3c, 0x3d and 0x4a in range (· 120.0000076):
// 0x55b6f0(hero, hero) without the hero's cell or line of sight. 0x2f only with the published hero on Uniria
// (0x56612e).
TEST(HeroArrivalSkillTest, TheSecondGroupGoesTo55b6f0WithoutLookingAtTheWalls)
{
    for (const std::int32_t map : {0, 2, 11})
    {
        for (const std::uint8_t skill : {std::uint8_t{0x13}, std::uint8_t{0x2c}, std::uint8_t{0x3c}})
            ExpectTheDirectCall(skill, map, SkillDraws, AtTheTarget);
    }
    // A wall between them changes nothing.
    ExpectTheDirectCall(0x13, 0, SkillDraws, AtTheTarget,
                        [](ArrivingHero& hero) { hero.terrain.attributes->cells[(116u << 8) + 183u] = 4; });
    ExpectTheDirectCall(0x2f, 2, SkillDraws, AtTheTarget,
                        [](ArrivingHero& hero) { hero.Hero().pet.model = UniriaModel; });
    // 0x13: the D3, the action, the glow and the 19 with the target's key.
    {
        const auto tested = Arriving(0x13, 0);
        ASSERT_TRUE(tested->Arrive().HasValue());
        ASSERT_GE(tested->sent.size(), 2u);
        EXPECT_EQ(tested->Sent(0)[2], 0xd3);
        EXPECT_EQ(tested->Sent(1), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, 0x13, 0x0a, 0x1b}));
    }
    // Without Uniria, 0x2f does nothing.
    const auto tested = Arriving(0x2f, 0);
    const CrtRandom expected = Advanced(tested->random, SkillDraws);
    ASSERT_TRUE(tested->Arrive().HasValue());
    EXPECT_EQ(tested->random.State(), expected.State());
    EXPECT_TRUE(tested->sent.empty());
    EXPECT_EQ(tested->Movement().targetX, 0u);
}

// 0x56642c..0x56654f and 0x5665c3..0x5666e0: 0x2e and 0x33, and 0x1a to 0x1c, in range and in sight: 0x55faa0. The D3
// with the previous heading (0: 0x10) and the 19 with the target's key.
TEST(HeroArrivalSkillTest, TheElfSkillsGoTo55faa0)
{
    for (const std::int32_t map : {0, 2})
    {
        for (const std::uint8_t skill :
             {std::uint8_t{0x1a}, std::uint8_t{0x1c}, std::uint8_t{0x2e}, std::uint8_t{0x33}})
            ExpectTheDirectCall(skill, map, SkillDraws + HeroCellDraws, ElfOnItsTarget);
    }
    // 0x1b: the skill, the range, the hero's cell, the published one's and the own record of 0x55faa0, the 19 and
    // 0x4f5290 (0x4f3f80, the own record).
    const auto tested = Arriving(0x1b, 2);
    const CrtRandom expected =
        Advanced(tested->random, SkillDraws + HeroCellDraws + HeroCellDraws + OwnDraws + SendDraws + OwnDraws);
    ASSERT_TRUE(tested->Arrive().HasValue());
    EXPECT_EQ(tested->random.State(), expected.State());
    ASSERT_EQ(tested->sent.size(), 2u);
    EXPECT_EQ(tested->Sent(0), (std::vector<std::uint8_t>{0xc1, 0x06, 0xd3, 182, 115, 0x10}));
    EXPECT_EQ(tested->Sent(1), (std::vector<std::uint8_t>{0xc1, 0x06, 0x19, 0x1b, 0x0a, 0x1b}));
    EXPECT_EQ(tested->Hero().animation.action, 0x92);
}

// 0x56609c..0x5660b1: without the byte at 0x7758580, 0x7758584 is the skill; the own record is opened anyway.
TEST(HeroArrivalSkillTest, WithoutTheActiveByteTheSlotIsTheSkill)
{
    const auto tested = Arriving(0x13, 0);
    tested->Movement().weaponSkill.active = 0x100;
    tested->Movement().weaponSkill.slot = 0x5;
    const CrtRandom expected = Advanced(tested->random, SkillDraws);
    ASSERT_TRUE(tested->Arrive().HasValue());
    EXPECT_EQ(tested->random.State(), expected.State());
    EXPECT_TRUE(tested->sent.empty());
    // A skill wider than one byte is not in Skill.bmd.
    tested->Movement().weaponSkill.slot = 0x113;
    const auto cut = tested->Arrive();
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x566108"), std::string::npos) << cut.Error();
}

// The exits: the target at −1 or dying (+0x389) after the range; 0x1a without line of sight after the hero's cell; the
// target outside the pool cuts.
TEST(HeroArrivalSkillTest, TheEarlyExitsDoNothing)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = Arriving(0x1a, map);
        tested->Movement().weaponSkill.target = -1;
        CrtRandom expected = Advanced(tested->random, SkillDraws);
        ASSERT_TRUE(tested->Arrive().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());

        tested->Movement().weaponSkill.target = static_cast<std::int32_t>(TargetSlot);
        tested->Target().dying = 1;
        expected = Advanced(expected, SkillDraws);
        ASSERT_TRUE(tested->Arrive().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->Movement().targetX, 0u);

        tested->Target().dying = 0;
        tested->terrain.attributes->cells[(116u << 8) + 183u] = 4;
        expected = Advanced(expected, SkillDraws + HeroCellDraws);
        ASSERT_TRUE(tested->Arrive().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_EQ(tested->Movement().targetX, 184u);
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Hero().movement, 0);
    }
    const auto tested = Arriving(0x1a, 0);
    tested->Movement().weaponSkill.target = static_cast<std::int32_t>(tested->world.Actors().size());
    const auto cut = tested->Arrive();
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x56615b"), std::string::npos) << cut.Error();
}

// 0x56620d..0x566299: out of range, the PathFinding2 path to the range from the hero's cell (the Y and the X), · 1.2 in
// a float with 0x55b6f0; with it, +0x378 to 1 and +0x379 stays at 5. Range 5 and the target ten cells east, at
// (192, 115): the 5 and the 6 end in different cells.
TEST(HeroArrivalSkillTest, OutOfReachTheHeroWalksIntoReach)
{
    struct Probe
    {
        std::uint8_t skill;
        float distance;
    };
    for (const std::int32_t map : {0, 2})
    {
        for (const Probe probe : {Probe{0x1, 5.0f}, Probe{0x13, 6.0f}, Probe{0x1a, 5.0f}, Probe{0x33, 5.0f}})
        {
            SCOPED_TRACE(map);
            SCOPED_TRACE(probe.skill);
            const auto tested = Arriving(probe.skill, map, 5);
            tested->Target().position = {19250.0f, 11550.0f, 0.0f};
            const CrtRandom expected = Advanced(tested->random, SkillDraws + HeroCellDraws);
            ASSERT_TRUE(tested->Arrive().HasValue());
            EXPECT_EQ(tested->random.State(), expected.State());
            EXPECT_TRUE(tested->sent.empty());
            const auto path = Client::Pathfinding::PathFinder(tested->terrain.attributes->cells)
                                  .Find({182, 115}, {192, 115}, false, probe.distance);
            ASSERT_TRUE(path.has_value());
            EXPECT_EQ(tested->Hero().movement, 1);
            EXPECT_EQ(tested->Hero().movementType, 5);
            ASSERT_EQ(tested->Hero().path.count, path->count);
            EXPECT_EQ(tested->Hero().path.cells[path->count - 1].x, path->cells[path->count - 1].x);
            EXPECT_EQ(tested->Hero().path.cells[path->count - 1].y, path->cells[path->count - 1].y);
            EXPECT_EQ(tested->Movement().targetX, 192u);
            EXPECT_EQ(tested->Movement().targetY, 115u);
        }
    }
    const auto walls = Arriving(0x1, 0);
    auto& cells = walls->terrain.attributes->cells;
    const auto near = Client::Pathfinding::PathFinder(cells).Find({182, 115}, {192, 115}, false, 5.0f);
    const auto far = Client::Pathfinding::PathFinder(cells).Find({182, 115}, {192, 115}, false, 6.0f);
    ASSERT_TRUE(near && far);
    EXPECT_NE(near->count, far->count);
}

// P-C256: out of range, a target with its cell outside 0..255 (which PathFinding2, 0x566272, takes as a linear index)
// gets no path: after reading the hero's Y and X, the path is discarded without cutting. In range, the line of sight
// that leaves TerrainWall (0x56639d → 0x55886d, cell 256 of row 255) is taken as no line.
TEST(HeroArrivalSkillTest, ACellOffTheGridDiscardsTheWalkAndTheLine)
{
    for (const std::int32_t map : {0, 2})
    {
        for (const glm::vec3 position : {glm::vec3{-1000.0f, 11550.0f, 0.0f}, glm::vec3{18450.0f, 30000.0f, 0.0f}})
        {
            SCOPED_TRACE(std::to_string(map) + " " + std::to_string(position.x));
            const auto tested = Arriving(0x1, map);
            tested->Target().position = position;
            const CrtRandom expected = Advanced(tested->random, SkillDraws + HeroCellDraws);
            const auto result = tested->Arrive();
            ASSERT_TRUE(result.HasValue()) << result.Error();
            EXPECT_EQ(tested->random.State(), expected.State());
            EXPECT_TRUE(tested->sent.empty());
            EXPECT_EQ(tested->Hero().movement, 0);
            EXPECT_EQ(tested->Hero().path.count, 0);
        }
        SCOPED_TRACE(map);
        const auto edge = Arriving(0x1, map);
        edge->Hero().cellX = 255;
        edge->Hero().cellY = 255;
        edge->Hero().position = {25550.0f, 25550.0f, 0.0f};
        edge->Target().position = {25650.0f, 25550.0f, 0.0f};
        const CrtRandom expected = Advanced(edge->random, SkillDraws + HeroCellDraws);
        const auto result = edge->Arrive();
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(edge->random.State(), expected.State());
        EXPECT_TRUE(edge->sent.empty());
        EXPECT_EQ(edge->Movement().targetX, 256u);
    }
}

// 0x5661e8 and its pairs: in range but in the safe zone, the path instead of the skill, without sends; with the hero
// already in range, PathFinding2 gives no two-cell path and does not walk (Q-C1217).
TEST(HeroArrivalSkillTest, InTheSafeZoneTheHeroWalksInstead)
{
    for (const std::uint8_t skill : {std::uint8_t{0x1}, std::uint8_t{0x13}, std::uint8_t{0x33}})
    {
        SCOPED_TRACE(skill);
        const auto tested = Arriving(skill, 0);
        tested->Hero().safeZone = 1;
        const CrtRandom expected = Advanced(tested->random, SkillDraws + HeroCellDraws);
        ASSERT_TRUE(tested->Arrive().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        const float distance = skill == 0x13 ? 4.8f : 4.0f;
        const auto path = Client::Pathfinding::PathFinder(tested->terrain.attributes->cells)
                              .Find({182, 115}, {184, 117}, false, distance);
        EXPECT_FALSE(path.has_value());
        EXPECT_EQ(tested->Hero().movement, 0);
    }
}

// The skills outside the tables (0x5, 0x34, 0x43) and 0: the skill and the range, and nothing else.
TEST(HeroArrivalSkillTest, TheOtherSkillsDoNothing)
{
    for (const std::uint8_t skill : {std::uint8_t{0}, std::uint8_t{0x5}, std::uint8_t{0x34}, std::uint8_t{0x43}})
    {
        SCOPED_TRACE(skill);
        const auto tested = Arriving(skill, 2);
        const CrtRandom expected = Advanced(tested->random, SkillDraws);
        ASSERT_TRUE(tested->Arrive().HasValue());
        EXPECT_EQ(tested->random.State(), expected.State());
        EXPECT_TRUE(tested->sent.empty());
        EXPECT_EQ(tested->Movement().targetX, 0u);
    }
}
} // namespace
