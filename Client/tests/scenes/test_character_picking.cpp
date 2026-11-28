#include "Actors/CharacterPicking.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainMapping.h"
#include "Models/LogicalAccess.h"
#include "Kernel/ActorState.h"
#include "Actors/ActorPickingBox.h"
#include "Scenes/GameDrawTail.h"
#include "Hud/GameUi.h"
#include "Party/PartyActors.h"
#include "Social/SocialState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/Widget.h"
#include "Terrain/TerrainPickingSegment.h"
#include "Terrain/WorldTerrain.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <limits>
#include <memory>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// The picking of the game's characters (0x59e700) and what 0x59ec80 does with it. The worlds go on the heap.
namespace
{
using Client::Actors::PickTheCharacter;
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Kernel::WorldState;
using Client::Terrain::TerrainPickingSegment;

constexpr std::uint8_t Players = 1;
constexpr std::uint8_t Monsters = 0x22;
constexpr std::uint8_t Npcs = 4;
constexpr std::int32_t NoPick = -1;

// A horizontal segment along X, at Y 1,000 and at height 60: it touches the box of whoever is at Y 1,000 (from −60 to
// 40 around the position, Q-C100).
const TerrainPickingSegment Ray{.origin = {0.0f, 1000.0f, 60.0f}, .end = {20000.0f, 1000.0f, 60.0f}};

void Place(WorldState& world, std::size_t slot, float x, std::uint8_t kind)
{
    ActorState& actor = world.Actors()[slot];
    actor.alive = true;
    actor.seen = true;
    actor.alpha = 1.0f;
    actor.byte42c = 1;
    actor.byte88 = kind;
    actor.position = {x, 1000.0f, 0.0f};
    // The box 0x511290 gives 0x20a (0x512270 and 0x5122d4).
    actor.boundsMin = {-60.0f, -60.0f, 0.0f};
    actor.boundsMaxX = 40.0f;
    actor.boundsMaxY = 40.0f;
    actor.boundsTop = 120.0f;
    Client::Actors::PublishPickingBox(actor, actor.position, actor.boundsMin,
                                      {actor.boundsMaxX, actor.boundsMaxY, actor.boundsTop});
}

struct Picking
{
    Picking()
    {
        world->Runtime().hero = 0;
        Place(*world, 0, 500.0f, Players);
    }
    std::unique_ptr<WorldState> world = std::make_unique<WorldState>();
    Client::Terrain::WorldTerrain terrain;
};

// 0x59e737..0x59e7f6: those of the mask, alive, in view and with alpha are tinted by +0x1d6 and by the PK.
TEST(CharacterPickingTest, TheMaskedOnesAreTintedByLevelAndPk)
{
    Picking tested;
    WorldState& world = *tested.world;
    for (std::size_t slot = 1; slot <= 6; ++slot)
    {
        Place(world, slot, 0.0f, Players);
        world.Actors()[slot].level = static_cast<std::uint16_t>(slot - 1);
        world.Actors()[slot].light = glm::vec3(9.0f);
    }
    world.Actors()[7] = world.Actors()[1];
    world.Actors()[7].pk = 6;
    world.Actors()[8] = world.Actors()[1];
    world.Actors()[8].byte88 = 2;
    world.Actors()[9] = world.Actors()[1];
    world.Actors()[9].alpha = 0.0f;

    EXPECT_EQ(PickTheCharacter(world, tested.terrain, Players, Ray.origin, std::nullopt), NoPick);
    const auto light = [&world](std::size_t slot) { return world.Actors()[slot].light; };
    EXPECT_EQ(light(1), glm::vec3(0.2f));
    EXPECT_EQ(light(2), glm::vec3(-0.4f));
    EXPECT_EQ(light(3), glm::vec3(0.2f, -0.6f, -0.6f));
    EXPECT_EQ(light(4), glm::vec3(1.5f));
    EXPECT_EQ(light(5), glm::vec3(0.3f, 0.2f, -0.5f));
    EXPECT_EQ(light(6), glm::vec3(9.0f));
    EXPECT_EQ(light(7), glm::vec3(-0.4f));
    EXPECT_EQ(light(8), glm::vec3(9.0f));
    EXPECT_EQ(light(9), glm::vec3(9.0f));
}

TEST(CharacterPickingTest, EligibleTintingSetsThePersistentEffectFlag)
{
    Picking tested;
    WorldState& world = *tested.world;
    Place(world, 1, 0.0f, Players);
    Place(world, 2, 0.0f, Players);
    Place(world, 3, 0.0f, Players);
    Place(world, 4, 0.0f, Monsters);
    world.Actors()[1].byteE8 = 0;
    world.Actors()[2].alpha = std::nextafter(0.0f, 1.0f);
    world.Actors()[3].alpha = std::numeric_limits<float>::quiet_NaN();
    world.Actors()[4].byteE8 = 0;
    world.Actors()[0].byteE8 = 0;
    world.Actors()[0].light = glm::vec3(9.0f);

    ASSERT_EQ(PickTheCharacter(world, tested.terrain, Players, Ray.origin, std::nullopt), NoPick);

    EXPECT_EQ(world.Actors()[1].byteE8, 1);
    EXPECT_EQ(world.Actors()[2].byteE8, 1);
    EXPECT_EQ(world.Actors()[3].byteE8, 0);
    EXPECT_EQ(world.Actors()[4].byteE8, 0);
    EXPECT_EQ(world.Actors()[0].byteE8, 1);
    EXPECT_EQ(world.Actors()[0].light, glm::vec3(0.2f));
}

// 0x59e7fc..0x59ea95: the one nearest the camera whose box touches the segment; never the published hero, the dying
// one, the hidden one (+0x7c & 0x10000000), the one with +0x42c at 0 or one of another mask.
TEST(CharacterPickingTest, TheNearestBoxOnTheSegmentIsPicked)
{
    Picking tested;
    WorldState& world = *tested.world;
    Place(world, 1, 3000.0f, Players);
    Place(world, 2, 2000.0f, Players);
    Place(world, 3, 900.0f, Players);
    world.Actors()[3].dying = 1;
    Place(world, 4, 1000.0f, Players);
    world.Actors()[4].flags7c = 0x10000000;
    Place(world, 5, 1100.0f, Players);
    world.Actors()[5].byte42c = 0;
    Place(world, 6, 1200.0f, Monsters & 2);
    // Off the segment: at Y 2,000.
    Place(world, 7, 600.0f, Players);
    world.Actors()[7].position.y = 2000.0f;
    Client::Actors::PublishPickingBox(
        world.Actors()[7], world.Actors()[7].position, world.Actors()[7].boundsMin,
        {world.Actors()[7].boundsMaxX, world.Actors()[7].boundsMaxY, world.Actors()[7].boundsTop});
    EXPECT_EQ(PickTheCharacter(world, tested.terrain, Players, Ray.origin, Ray), 2);
    EXPECT_EQ(PickTheCharacter(world, tested.terrain, Monsters, Ray.origin, Ray), 6);
    EXPECT_EQ(PickTheCharacter(world, tested.terrain, Npcs, Ray.origin, Ray), NoPick);
    // On a tie, the first one wins.
    Place(world, 8, 2000.0f, Players);
    EXPECT_EQ(PickTheCharacter(world, tested.terrain, Players, Ray.origin, Ray), 2);
}

TEST(CharacterPickingTest, DistanceUsesTheCameraPositionRatherThanTheRayOrigin)
{
    Picking tested;
    WorldState& world = *tested.world;
    Place(world, 1, 100.0f, Players);
    Place(world, 2, 9000.0f, Players);
    const glm::vec3 camera{10000.0f, 1000.0f, 60.0f};

    EXPECT_EQ(PickTheCharacter(world, tested.terrain, Players, camera, Ray), 2);
    EXPECT_EQ(PickTheCharacter(world, tested.terrain, Players, Ray.origin, Ray), 1);
}

TEST(CharacterPickingTest, FloatRoundedBestCanLetTheLaterSlightlyFartherCandidateWin)
{
    Picking tested;
    WorldState& world = *tested.world;
    Place(world, 1, 10000.3f, Players);
    Place(world, 2, 10000.3f, Players);
    world.Actors()[2].position.y = 1001.0f;
    Client::Actors::PublishPickingBox(
        world.Actors()[2], world.Actors()[2].position, world.Actors()[2].boundsMin,
        {world.Actors()[2].boundsMaxX, world.Actors()[2].boundsMaxY, world.Actors()[2].boundsTop});

    EXPECT_EQ(PickTheCharacter(world, tested.terrain, Players, Ray.origin, Ray), 2);
}

TEST(CharacterPickingTest, UnorderedDistanceComparisonAcceptsTheLaterCandidate)
{
    Picking tested;
    WorldState& world = *tested.world;
    Place(world, 1, 1000.0f, Players);
    Place(world, 2, 2000.0f, Players);
    world.Actors()[2].position.x = std::numeric_limits<float>::quiet_NaN();

    EXPECT_EQ(PickTheCharacter(world, tested.terrain, Players, Ray.origin, Ray), 2);
}

// Each type's box: at height 150, the segment passes above 0x20a (top 120) and touches that of 0x18e (0x51236f, from
// (−90, −50, 0) to (90, 50, 200)), even though it is farther.
TEST(CharacterPickingTest, TheBoxComesFromTheConstructedType)
{
    Picking tested;
    WorldState& world = *tested.world;
    Place(world, 1, 1000.0f, 2);
    Place(world, 2, 3000.0f, 2);
    ActorState& tall = world.Actors()[2];
    tall.boundsMin = {-90.0f, -50.0f, 0.0f};
    tall.boundsMaxX = 90.0f;
    tall.boundsMaxY = 50.0f;
    tall.boundsTop = 200.0f;
    Client::Actors::PublishPickingBox(tall, tall.position, tall.boundsMin,
                                      {tall.boundsMaxX, tall.boundsMaxY, tall.boundsTop});
    const TerrainPickingSegment high{.origin = {0.0f, 1000.0f, 150.0f}, .end = {20000.0f, 1000.0f, 150.0f}};
    EXPECT_EQ(PickTheCharacter(world, tested.terrain, Monsters, high.origin, high), 2);
    EXPECT_EQ(PickTheCharacter(world, tested.terrain, Monsters, Ray.origin, Ray), 1);
}

TEST(CharacterPickingTest, PickingUsesTheLastPublishedBoxAfterTheActorMoves)
{
    Picking tested;
    WorldState& world = *tested.world;
    Place(world, 1, 2000.0f, Players);
    ActorState& actor = world.Actors()[1];
    actor.field13c = 1940.0f;
    actor.field140 = 940.0f;
    actor.field144 = 0.0f;
    actor.field148 = 100.0f;
    actor.field14c = 0.0f;
    actor.field150 = 0.0f;
    actor.field154 = 0.0f;
    actor.field158 = 100.0f;
    actor.field15c = 0.0f;
    actor.field160 = 0.0f;
    actor.field164 = 0.0f;
    actor.field168 = 120.0f;
    actor.position.x = 9000.0f;
    const TerrainPickingSegment shortRay{.origin = {0.0f, 1000.0f, 60.0f}, .end = {2500.0f, 1000.0f, 60.0f}};

    EXPECT_EQ(PickTheCharacter(world, tested.terrain, Players, shortRay.origin, shortRay), 1);
}

// 0x59e9ec..0x59ea6d: in Lorencia layer 4 and in Devias layer 3 count only with the layer of the hero's cell; on the
// other maps, any.
TEST(CharacterPickingTest, TheUpperFloorsCountOnlyOnTheHerosLayer)
{
    struct Case
    {
        std::int32_t map;
        std::uint8_t upper;
    };
    for (const Case tried : {Case{0, 4}, Case{2, 3}})
    {
        SCOPED_TRACE(tried.map);
        Picking tested;
        WorldState& world = *tested.world;
        world.Runtime().map = tried.map;
        auto& mapping = tested.terrain.mapping;
        mapping = std::make_unique<Client::Data::Terrain::TerrainMapping>();
        Place(world, 1, 1250.0f, Players);
        Place(world, 2, 3000.0f, Players);
        // The first one's cell (12, 10).
        mapping->layer1[Client::Data::Terrain::CellIndex(12, 10)] = tried.upper;
        EXPECT_EQ(PickTheCharacter(world, tested.terrain, Players, Ray.origin, Ray), 2);
        world.Runtime().heroCellLayer = tried.upper;
        EXPECT_EQ(PickTheCharacter(world, tested.terrain, Players, Ray.origin, Ray), 1);
        // On another map the layer does not count.
        world.Runtime().heroCellLayer = 0;
        world.Runtime().map = 1;
        EXPECT_EQ(PickTheCharacter(world, tested.terrain, Players, Ray.origin, Ray), 1);
    }
}

// 0x59ec80 with the draw's segment.
struct Frame
{
    Frame()
    {
        world->Runtime().hero = 0;
        Place(*world, 0, 500.0f, Players);
    }
    Client::Core::Expected<void, std::string> Pick(bool alt = false)
    {
        return Client::Hud::PickInTheFrame({.world = *world,
                                            .random = random,
                                            .ownRecordDepth = world->Runtime().ownRecordDepth,
                                            .targets = world->Runtime().selectedTargets,
                                            .altDown = alt,
                                            .ray = Ray,
                                            .mouse = mouse});
    }
    std::unique_ptr<WorldState> world = std::make_unique<WorldState>();
    CrtRandom random;
    Client::Ui::Point mouse{};
};

CrtRandom Advanced(CrtRandom random, int count)
{
    for (int draw = 0; draw < count; ++draw)
        static_cast<void>(random.Next());
    return random;
}

// A made-up name in the bytes of a character or of a party slot.
void Name(std::span<std::byte> name, std::string_view text)
{
    std::ranges::fill(name, std::byte{0});
    for (std::size_t at = 0; at < text.size(); ++at)
        name[at] = static_cast<std::byte>(text[at]);
}

// The party at 0x77f3cd0 with these names, each with +0x1c at `index`.
void Party(WorldState& world, std::initializer_list<std::string_view> names, std::int32_t index)
{
    Client::Social::PartyState& party = world.Runtime().social.party;
    std::size_t at = 0;
    for (const std::string_view name : names)
    {
        Name(party.slots[at].name, name);
        party.slots[at].actorIndex = index;
        party.slots[at].actorFlags = 0x55;
        ++at;
    }
    world.Runtime().hud.partyCount = static_cast<std::int32_t>(names.size());
}

std::array<std::int32_t, 4> Indices(const WorldState& world)
{
    const auto& slots = world.Runtime().social.party.slots;
    return {slots[0].actorIndex, slots[1].actorIndex, slots[2].actorIndex, slots[3].actorIndex};
}

// 0x59e86a..0x59e948 and 0x59ea9b..0x59eb66: each player (+0x88 at 1) that passes the first conditions looks for its
// party slot at −2, even without a segment, with its +0x78; the hero too. At the end, an unseen hero is left at −3 and
// the others without a character at −1, with +0x20 at 0.
TEST(CharacterPickingTest, ThePlayersFindTheirPartySlotsWhilePicking)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Picking tested;
        WorldState& world = *tested.world;
        world.Runtime().map = map;
        Name(world.Actors()[0].name, "Tejon");
        Place(world, 3, 2000.0f, Players);
        Name(world.Actors()[3].name, "Brezal");
        world.Actors()[3].flags78 = 0x48;
        Place(world, 4, 2500.0f, 2);
        Name(world.Actors()[4].name, "Zorzal");
        Party(world, {"Brezal", "Tejon", "Zorzal", "Mirlo"}, -2);
        EXPECT_EQ(PickTheCharacter(world, tested.terrain, Npcs, Ray.origin, std::nullopt), NoPick);
        EXPECT_EQ(Indices(world), (std::array<std::int32_t, 4>{3, 0, -1, -1}));
        const auto& slots = world.Runtime().social.party.slots;
        EXPECT_EQ(slots[0].actorFlags, 0x48U);
        EXPECT_EQ(slots[2].actorFlags, 0U);
        // What was found stays: only −2 is searched.
        Name(world.Actors()[3].name, "Otro");
        EXPECT_EQ(PickTheCharacter(world, tested.terrain, Players, Ray.origin, Ray), 3);
        EXPECT_EQ(Indices(world), (std::array<std::int32_t, 4>{3, 0, -1, -1}));

        // The unseen hero.
        Party(world, {"Brezal", "Tejon", "Zorzal", "Mirlo"}, -2);
        world.Actors()[0].seen = false;
        static_cast<void>(PickTheCharacter(world, tested.terrain, Players, Ray.origin, Ray));
        EXPECT_EQ(Indices(world), (std::array<std::int32_t, 4>{-1, -3, -1, -1}));
        EXPECT_EQ(slots[1].actorFlags, 0U);
    }
}

// 0x59e680 (and the inline loop of 0x5926ca): strncmp(slot, name, max(length, 1)) on the slots up to 0x78aba48. A slot
// that starts with the name counts; with an empty name, only the empty slot; without a party or with a negative count,
// nobody. It does not touch +0x1c or +0x20.
TEST(CharacterPickingTest, IsInThePartyComparesTheNameAsAPrefix)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Picking tested;
        WorldState& world = *tested.world;
        world.Runtime().map = map;
        Client::Kernel::ActorState& actor = world.Actors()[3];
        Name(actor.name, "Brezal");
        EXPECT_FALSE(Client::Party::IsInTheParty(world, actor));
        Party(world, {"Tejon", "Brez"}, -2);
        EXPECT_FALSE(Client::Party::IsInTheParty(world, actor));
        Party(world, {"Tejon", "Brezales"}, -2);
        EXPECT_TRUE(Client::Party::IsInTheParty(world, actor));
        EXPECT_EQ(Indices(world), (std::array<std::int32_t, 4>{-2, -2, 0, 0}));
        Party(world, {"Tejon", "Brezal"}, -2);
        EXPECT_TRUE(Client::Party::IsInTheParty(world, actor));
        // The third, empty slot does not count with the count at 2.
        Name(actor.name, "");
        EXPECT_FALSE(Client::Party::IsInTheParty(world, actor));
        world.Runtime().hud.partyCount = 3;
        EXPECT_TRUE(Client::Party::IsInTheParty(world, actor));
        world.Runtime().hud.partyCount = -1;
        EXPECT_FALSE(Client::Party::IsInTheParty(world, actor));
    }
}

// 0x59ef2f → 0x59e3b0: with the mouse over the UI it only looks for the party, among those of type 0x20a.
TEST(CharacterPickingTest, OverTheUiOnlyThePartyIsLookedFor)
{
    Frame tested;
    WorldState& world = *tested.world;
    world.Runtime().map = 2;
    world.Runtime().ui.mouseOnUi = 1;
    Name(world.Actors()[0].name, "Tejon");
    Place(world, 3, 2000.0f, Players);
    Name(world.Actors()[3].name, "Brezal");
    world.Actors()[3].type = 0x20a;
    Place(world, 4, 2500.0f, Players);
    Name(world.Actors()[4].name, "Zorzal");
    Party(world, {"Brezal", "Tejon", "Zorzal"}, -2);
    ASSERT_TRUE(tested.Pick().HasValue());
    EXPECT_EQ(Indices(world), (std::array<std::int32_t, 4>{3, -3, -1, 0}));
    EXPECT_EQ(world.Runtime().selectedTargets, (std::array<std::int32_t, 5>{-1, -1, -1, -1, -1}));
    EXPECT_EQ(tested.random.State(), CrtRandom().State());
}

// 0x59ef46..0x59ef5f: with Alt, a floor item the ray touches jumps to 0x59f52a, which looks for the party (0x59e3b0)
// before 0x59f52f, without looking for characters; without an item, the characters (0x59e700, which also look at the
// party).
TEST(CharacterPickingTest, AltOverAFloorItemAlsoLooksForTheParty)
{
    for (const bool withItem : {true, false})
    {
        SCOPED_TRACE(withItem);
        Frame tested;
        WorldState& world = *tested.world;
        Name(world.Actors()[0].name, "Tejon");
        Place(world, 3, 2000.0f, Players);
        Name(world.Actors()[3].name, "Brezal");
        world.Actors()[3].type = 0x20a;
        Party(world, {"Brezal", "Tejon"}, -2);
        auto items = std::make_unique<std::array<Client::Scenes::FloorItemState, 1000>>();
        if (withItem)
        {
            Client::Scenes::FloorItemState& item = (*items)[6];
            item.alive = true;
            item.seen = true;
            item.pickBox = Client::Models::Box{.min = {1500.0f, 970.0f, 30.0f}, .max = {1560.0f, 1030.0f, 90.0f}};
        }
        ASSERT_TRUE(Client::Hud::PickInTheFrame({.world = world,
                                                 .random = tested.random,
                                                 .ownRecordDepth = world.Runtime().ownRecordDepth,
                                                 .targets = world.Runtime().selectedTargets,
                                                 .altDown = true,
                                                 .ray = Ray,
                                                 .mouse = tested.mouse,
                                                 .floorItems = *items})
                        .HasValue());
        if (withItem)
        {
            EXPECT_EQ(world.Runtime().selectedTargets[0], 6);
            EXPECT_EQ(Indices(world)[0], 3);
            EXPECT_EQ(Indices(world)[1], -3);
        }
        else
            EXPECT_EQ(world.Runtime().selectedTargets[0], -1);
        EXPECT_EQ(tested.random.State(), CrtRandom().State());
    }
}

// 0x5e4140: with the party window closed, the list on the right (640 − 50, every 19 from 5). With skill 0x1a, the Elf
// opens the own record for each member with a character and picks the one on the mouse's row; then 0x59e3b0 looks for
// them again. A member at −1 takes its row and one at −3 does not. The Dark Knight opens nothing.
TEST(CharacterPickingTest, TheListOnTheRightPicksAMember)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        Frame tested;
        WorldState& world = *tested.world;
        world.Runtime().map = map;
        world.Actors()[0].characterClass = 2;
        world.Actors()[0].type = 0x20a;
        world.OwnCharacter().words6c[0] = 0x1a;
        Name(world.Actors()[0].name, "Tejon");
        Place(world, 3, 2000.0f, Players);
        world.Actors()[3].type = 0x20a;
        world.Actors()[3].position.y = 5000.0f;
        // The box the picking reads is the one the last draw published (+0x13c..+0x168): the new slot's.
        Client::Actors::PublishPickingBox(
            world.Actors()[3], world.Actors()[3].position, world.Actors()[3].boundsMin,
            {world.Actors()[3].boundsMaxX, world.Actors()[3].boundsMaxY, world.Actors()[3].boundsTop});
        Name(world.Actors()[3].name, "Brezal");
        Party(world, {"Mirlo", "Tejon", "Zorzal", "Brezal"}, -1);
        auto& slots = world.Runtime().social.party.slots;
        slots[0].actorIndex = -3;
        slots[1].actorIndex = 0;
        slots[3].actorIndex = 3;
        // Tejon at [5, 24], Zorzal at [24, 43] and Brezal at [43, 62].
        tested.mouse = {.x = 587, .y = 62};
        const CrtRandom expected = Advanced(tested.random, 3 * Client::Models::OwnRecordCloseRandoms);
        ASSERT_TRUE(tested.Pick().HasValue());
        EXPECT_EQ(world.Runtime().selectedTargets[2], 3);
        EXPECT_EQ(tested.random.State(), expected.State());
        EXPECT_EQ(Indices(world), (std::array<std::int32_t, 4>{-1, 0, -1, 3}));

        // Outside the list (x + 50 counts, x + 51 does not), it looks as always: nobody on the segment.
        tested.mouse = {.x = 641, .y = 62};
        world.Runtime().selectedTargets = {-1, -1, -1, -1, -1};
        ASSERT_TRUE(tested.Pick().HasValue());
        EXPECT_EQ(world.Runtime().selectedTargets[2], -1);

        // The Dark Knight does not open the record or pick from the list.
        world.Actors()[0].characterClass = 1;
        Party(world, {"Mirlo", "Tejon", "Zorzal", "Brezal"}, 3);
        tested.mouse = {.x = 600, .y = 10};
        const CrtRandom before = tested.random;
        ASSERT_TRUE(tested.Pick().HasValue());
        EXPECT_EQ(world.Runtime().selectedTargets[2], -1);
        EXPECT_EQ(tested.random.State(), before.State());
    }
}

// 0x59efb9..0x59f509: the Dark Wizard opens the own record for its skill (the 3,848 rand() of the close) and picks the
// player of mask 1 at 0x6c8e30. With nobody picked before, 0x59ef07 already left 0x6c8e38 at −1.
TEST(CharacterPickingTest, TheWizardPicksAPlayerAndDrawsTheOwnRecord)
{
    Frame tested;
    Place(*tested.world, 3, 2000.0f, Players);
    auto& targets = tested.world->Runtime().selectedTargets;
    targets[4] = 9;
    const CrtRandom expected = Advanced(tested.random, Client::Models::OwnRecordCloseRandoms);
    ASSERT_TRUE(tested.Pick().HasValue());
    EXPECT_EQ(targets, (std::array<std::int32_t, 5>{-1, -1, 3, -1, -1}));
    EXPECT_EQ(tested.random.State(), expected.State());
}

// 0x59efda..0x59f489: with skills 0x1a to 0x1c the Elf looks for the players first; without them, the monsters.
TEST(CharacterPickingTest, TheSkillOfTheElfPutsThePlayersFirst)
{
    for (const std::uint8_t skill : {std::uint8_t{0x1a}, std::uint8_t{0x1c}, std::uint8_t{0x1d}})
    {
        SCOPED_TRACE(int{skill});
        Frame tested;
        WorldState& world = *tested.world;
        world.Actors()[0].characterClass = 2;
        world.OwnCharacter().words6c[0] = skill;
        Place(world, 3, 2000.0f, Players);
        Place(world, 4, 3000.0f, 2);
        ASSERT_TRUE(tested.Pick().HasValue());
        EXPECT_EQ(world.Runtime().selectedTargets[2], skill <= 0x1c ? 3 : 4);
    }
}

// 0x59ef46..0x59efb4: with Alt, first those of mask 4 (0x6c8e2c) and, with none, those of 0x22 and 1.
TEST(CharacterPickingTest, AltLooksForTheNpcsFirst)
{
    Frame tested;
    WorldState& world = *tested.world;
    world.Actors()[0].characterClass = 1;
    Place(world, 3, 2000.0f, Players);
    Place(world, 4, 3000.0f, Npcs);
    ASSERT_TRUE(tested.Pick(true).HasValue());
    EXPECT_EQ(world.Runtime().selectedTargets, (std::array<std::int32_t, 5>{-1, 4, -1, -1, -1}));
    world.Actors()[4].alive = false;
    ASSERT_TRUE(tested.Pick(true).HasValue());
    EXPECT_EQ(world.Runtime().selectedTargets, (std::array<std::int32_t, 5>{-1, -1, 3, -1, -1}));
    EXPECT_EQ(tested.random.State(), CrtRandom().State());
}

// 0x59ee4b..0x59ef10: the chosen monster stays with the automatic attack and no buttons; with a button, it is released.
TEST(CharacterPickingTest, TheChosenMonsterStaysWithoutButtons)
{
    Frame tested;
    WorldState& world = *tested.world;
    world.Actors()[0].characterClass = 1;
    Place(world, 5, 8000.0f, 2);
    world.Actors()[5].position.y = 5000.0f;
    Client::Actors::PublishPickingBox(
        world.Actors()[5], world.Actors()[5].position, world.Actors()[5].boundsMin,
        {world.Actors()[5].boundsMaxX, world.Actors()[5].boundsMaxY, world.Actors()[5].boundsTop});
    Place(world, 3, 2000.0f, Players);
    auto& targets = world.Runtime().selectedTargets;
    targets = {-1, -1, 5, -1, 5};
    ASSERT_TRUE(tested.Pick().HasValue());
    EXPECT_EQ(targets[2], 5);
    EXPECT_EQ(targets[4], 5);
    world.Runtime().movement.leftHeld = true;
    ASSERT_TRUE(tested.Pick().HasValue());
    EXPECT_EQ(targets[2], 3);
    EXPECT_EQ(targets[4], 5);
}
} // namespace
