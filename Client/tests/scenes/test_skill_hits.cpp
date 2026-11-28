#include "Actors/SkillHits.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Terrain/WorldTerrain.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Skills/Skill.h"
#include "Models/LogicalAccess.h"
#include "Kernel/ActorState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"

#include "support/SameBytes.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// HitTheTargetsAround (0x517a20): a skill's targets around a point and the C1 10 that sends them (06, "La flecha").
namespace
{
using Client::Actors::HitTheTargetsAround;
using Client::Actors::SkillHitsRequest;
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Kernel::WorldState;

constexpr std::uint32_t Seed = 0x13579bdfu;
constexpr std::size_t HeroSlot = 42;
constexpr std::size_t SkillPlace = 3;
constexpr int OwnRecord = Client::Models::OwnRecordCloseRandoms;
// 0x518515..0x518606: the C3's padding and serial.
constexpr int Delivery = 2;

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

struct HitsWorld
{
    HitsWorld(std::int32_t map, std::uint8_t skill)
    {
        world.Runtime().map = map;
        world.Runtime().hero = HeroSlot;
        world.Runtime().heroSlot = static_cast<std::int32_t>(HeroSlot);
        world.OwnCharacter().SetSkillAt(SkillPlace, skill);
        ActorState& hero = world.Actors()[HeroSlot];
        hero.alive = true;
        hero.seen = true;
        hero.byte88 = 1;
        hero.key = 0x0101;
        hero.position = {12345.0f, 6789.0f, 0.0f};
    }

    // A monster (+0x88 at 2) or a player (at 1), at `distance` from the point on the x axis.
    ActorState& Put(std::size_t slot, std::uint8_t kind, std::uint16_t key, float distance)
    {
        ActorState& actor = world.Actors()[slot];
        actor = ActorState{};
        actor.alive = true;
        actor.seen = true;
        actor.byte88 = kind;
        actor.key = key;
        actor.position = {Point.x + distance, Point.y, 0.0f};
        return actor;
    }

    Client::Core::Expected<bool, std::string> Hit(const SkillHitsRequest& request)
    {
        return HitTheTargetsAround(request, world, terrain, random,
                                   [this](Client::Protocol::Outgoing packet) { sent.push_back(std::move(packet)); });
    }

    static constexpr glm::vec3 Point{12345.0f, 6789.0f, 150.0f};
    std::unique_ptr<WorldState> ownedWorld = std::make_unique<WorldState>();
    WorldState& world = *ownedWorld;
    Client::Terrain::WorldTerrain terrain;
    CrtRandom random{Seed};
    std::vector<Client::Protocol::Outgoing> sent;
};

SkillHitsRequest Request(float radius)
{
    return SkillHitsRequest{.skillIndex = SkillPlace,
                            .point = HitsWorld::Point,
                            .radius = radius,
                            .volley = 0x0c,
                            .key = 0x0222,
                            .extra = 0};
}

void Name(ActorState& actor, std::string_view name)
{
    for (std::size_t index = 0; index < name.size() && index < actor.name.size(); ++index)
        actor.name[index] = static_cast<std::byte>(name[index]);
}

// In Lorencia and in Devias, of the 400 in order: the monsters at 100 or less and the player of the key enter; the
// player without the key, the monster beyond 100, the unseen one, the dying one and the dead one stay out. The C1 10
// carries the skill, the point's cell (ftol(x · 0.01) & 0xff = 123 and 67), the volley, the count and the keys, with
// the own record's 3848 rand() and the C3's two.
TEST(SkillHitsTest, TheMonstersAroundAndThePlayerOfTheKeyAreHit)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        HitsWorld tested(map, 0x18);
        tested.Put(5, 2, 0x1005, 100.0f);
        tested.Put(6, 1, 0x1006, 10.0f);
        tested.Put(7, 1, 0x0222, 20.0f);
        tested.Put(8, 2, 0x1008, 100.5f);
        tested.Put(9, 2, 0x1009, 10.0f).seen = false;
        tested.Put(10, 2, 0x100a, 10.0f).dying = 1;
        tested.Put(11, 2, 0x100b, 10.0f).alive = false;
        tested.Put(12, 3, 0x100c, 10.0f);
        tested.Put(13, 2, 0x100d, -60.0f);

        const auto hit = tested.Hit(Request(100.0f));

        ASSERT_TRUE(hit.HasValue()) << hit.Error();
        EXPECT_TRUE(hit.Value());
        EXPECT_EQ(DrawsSince(Seed, tested.random), OwnRecord + Delivery);
        ASSERT_EQ(tested.sent.size(), 1u);
        Client::Protocol::Skills::SkillHitsSend expected{.skill = 0x18, .x = 123, .y = 67, .volley = 0x0c, .hits = 3};
        expected.keys = {0x1005, 0x0222, 0x100d, 0, 0};
        EXPECT_TRUE(Client::Tests::SameBytes(tested.sent[0].packet.Bytes(),
                                             Client::Protocol::Skills::WriteSkillHits(expected).packet.Bytes()));
    }
}

// Up to five: the sixth monster does not enter. With skill 8 or 9, each one with +0x394 at 0 gets +0x398 at 10; with
// another, none.
TEST(SkillHitsTest, FiveAtMostAndTheEighthAndNinthSkillsMark)
{
    for (const std::uint8_t skill : {std::uint8_t{8}, std::uint8_t{9}, std::uint8_t{0x33}})
    {
        SCOPED_TRACE(static_cast<int>(skill));
        HitsWorld tested(2, skill);
        for (std::size_t slot = 20; slot < 26; ++slot)
            tested.Put(slot, 2, static_cast<std::uint16_t>(0x2000 + slot), 5.0f);
        tested.world.Actors()[21].dword394 = 1;

        const auto hit = tested.Hit(Request(100.0f));

        ASSERT_TRUE(hit.HasValue()) << hit.Error();
        ASSERT_EQ(tested.sent.size(), 1u);
        Client::Protocol::Skills::SkillHitsSend expected{.skill = skill, .x = 123, .y = 67, .volley = 0x0c, .hits = 5};
        expected.keys = {0x2014, 0x2015, 0x2016, 0x2017, 0x2018};
        EXPECT_TRUE(Client::Tests::SameBytes(tested.sent[0].packet.Bytes(),
                                             Client::Protocol::Skills::WriteSkillHits(expected).packet.Bytes()));
        const bool marks = skill != 0x33;
        for (std::size_t slot = 20; slot < 26; ++slot)
        {
            const bool marked = marks && slot != 21 && slot != 25;
            EXPECT_EQ(tested.world.Actors()[slot].byte398, marked ? 10 : 0) << slot;
        }
    }
}

// With nobody, false with only the own record's rand(). With "webzen" in the hero's name, true without sending anything
// or the C3's rand(). The hero never enters, even at the point.
TEST(SkillHitsTest, NobodyOrTheGameMasterSendNothing)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        HitsWorld nobody(map, 0x18);
        nobody.world.Actors()[HeroSlot].position = HitsWorld::Point;
        const auto missed = nobody.Hit(Request(100.0f));
        ASSERT_TRUE(missed.HasValue()) << missed.Error();
        EXPECT_FALSE(missed.Value());
        EXPECT_TRUE(nobody.sent.empty());
        EXPECT_EQ(DrawsSince(Seed, nobody.random), OwnRecord);

        HitsWorld master(map, 0x18);
        Name(master.world.Actors()[HeroSlot], "xwebzenq");
        master.Put(5, 2, 0x1005, 1.0f);
        const auto hit = master.Hit(Request(100.0f));
        ASSERT_TRUE(hit.HasValue()) << hit.Error();
        EXPECT_TRUE(hit.Value());
        EXPECT_TRUE(master.sent.empty());
        EXPECT_EQ(DrawsSince(Seed, master.random), OwnRecord);
    }
}

// Without the session, the C3's two rand() are drawn anyway. With a NaN radius, the x87 comparison lets everyone in.
TEST(SkillHitsTest, WithoutTheSessionTheDeliveryStillDraws)
{
    HitsWorld tested(0, 0x18);
    tested.Put(5, 2, 0x1005, 5000.0f);
    const auto hit = HitTheTargetsAround(Request(std::numeric_limits<float>::quiet_NaN()), tested.world, tested.terrain,
                                         tested.random, nullptr);
    ASSERT_TRUE(hit.HasValue()) << hit.Error();
    EXPECT_TRUE(hit.Value());
    EXPECT_EQ(DrawsSince(Seed, tested.random), OwnRecord + Delivery);
}

// On map 30 with the siege (0x4294b0), the attribute of the point's cell cuts (0x517b1e, CL9.5), after reading the
// skill; without the siege, 30 is like the rest. A point the ftol cannot represent cuts at 0x517deb.
TEST(SkillHitsTest, TheSiegeRequiresAttributesAndTheOutOfRangePointCuts)
{
    HitsWorld siege(30, 0x18);
    siege.world.Runtime().castleFlags.siegeActive = true;
    siege.Put(5, 2, 0x1005, 1.0f);
    const auto cut = siege.Hit(Request(100.0f));
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x517b1e"), std::string::npos) << cut.Error();
    EXPECT_EQ(DrawsSince(Seed, siege.random), OwnRecord);

    HitsWorld calm(30, 0x18);
    calm.Put(5, 2, 0x1005, 1.0f);
    const auto hit = calm.Hit(Request(100.0f));
    ASSERT_TRUE(hit.HasValue()) << hit.Error();
    EXPECT_TRUE(hit.Value());
    EXPECT_EQ(calm.sent.size(), 1u);

    HitsWorld far(0, 0x18);
    SkillHitsRequest request = Request(std::numeric_limits<float>::infinity());
    request.point.x = 1.0e30f;
    far.Put(5, 2, 0x1005, 1.0f);
    const auto outside = far.Hit(request);
    ASSERT_FALSE(outside.HasValue());
    EXPECT_NE(outside.Error().find("0x517deb"), std::string::npos) << outside.Error();
    EXPECT_TRUE(far.sent.empty());
}

// 0x517bf3..0x517c00: EBX < 0x190. MuRet's slot 400 is a spare, not one of the 400 native actors.
TEST(SkillHitsTest, OnlyTheFourHundredNativeSlotsCanBeHitOrMarked)
{
    for (const std::size_t slot : {399u, 400u})
    {
        SCOPED_TRACE(slot);
        HitsWorld tested(0, 8);
        auto& target = tested.Put(slot, 2, 0x1234, 10.0f);
        const auto hit = tested.Hit(Request(100.0f));
        ASSERT_TRUE(hit.HasValue()) << hit.Error();
        EXPECT_EQ(hit.Value(), slot == 399u);
        EXPECT_EQ(tested.sent.size(), slot == 399u ? 1u : 0u);
        EXPECT_EQ(target.byte398, slot == 399u ? 10 : 0);
        EXPECT_EQ(DrawsSince(Seed, tested.random), OwnRecord + (slot == 399u ? Delivery : 0));
    }
}
} // namespace

TEST(SkillHitsTest, WarIncludesOtherPlayersAndAttribute100StopsBeforeCollectingTargets)
{
    HitsWorld tested(30, 0x18);
    tested.world.Runtime().castleFlags.siegeActive = true;
    tested.terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    tested.Put(5, 1, 0x1005, 1.0f);
    const auto hit = tested.Hit(Request(100.0f));
    ASSERT_TRUE(hit.HasValue());
    EXPECT_TRUE(hit.Value());
    EXPECT_EQ(tested.sent.size(), 1u);
    // 12345/6789 · .01f without a store: native index 123 + 67 * 256.
    tested.terrain.attributes->cells[123 + 67 * 256] = 0x100;
    const auto blocked = tested.Hit(Request(100.0f));
    ASSERT_TRUE(blocked.HasValue());
    EXPECT_FALSE(blocked.Value());
    EXPECT_EQ(tested.sent.size(), 1u);
    EXPECT_EQ(DrawsSince(Seed, tested.random), 2 * OwnRecord + Delivery);
}
