#include "Actors/ActorEquipment.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/ItemSetTables.h"
#include "Tables/ItemTable.h"
#include "Models/LogicalAccess.h"
#include "Models/Skeleton.h"
#include "Actors/ActorActions.h"
#include "Kernel/ActorState.h"
#include "Actors/FullSetScan.h"
#include "Player/ItemSetState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/SceneId.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/ItemState.h"
#include "support/ItemSetFiles.h"
#include "support/PlayerActors.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Client::Actors::ApplyActorEquipment: 0x512a30, with 0x505820 and 0x550d10 (06, "El equipo propio en el personaje",
// "El conjunto completo de 0x505820" and "Los stats del registro propio"; 04, "El F3 10"). The values come from the ASM
// checked against PE c32bbd5f; none from a run.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ActorItemPart;
using Client::Kernel::ActorState;
using Client::Kernel::NoItemModel;
using Client::Tests::AfterDraws;
using Client::Tests::FirstSeedWhere;

// The first seed whose rand() % 16 is not 0: the suffix of 0x4f2250 draws one and ends.
std::uint32_t QuietSeed()
{
    return FirstSeedWhere([](CrtRandom& random) { return random.Next() % 16 != 0; });
}

// The rand() of the tail of 0x550d10 (0x5512ee and the two % 100).
constexpr int TailRandoms = 3;

// What 0x512a30 does not write keeps this.
constexpr std::uint8_t Untouched = 0x5a;
constexpr std::uint16_t UntouchedModel = 0x5a5a;

ActorItemPart UntouchedPart()
{
    return {.model = UntouchedModel,
            .level = Untouched,
            .flags1b = Untouched,
            .flags1c = Untouched,
            .bone = Untouched,
            .animation = {.action = Untouched, .frame = 7.0f, .previousAction = Untouched, .previousKey = Untouched},
            .speed = 7.0f};
}

// A player with all its parts, the head and the scale at Untouched, standing in action 0x22: so 0x512a30 does not call
// 0x4f2250 unless the test asks for it.
ActorState DressedInUntouched(std::uint8_t characterClass)
{
    ActorState actor = Client::Tests::UnarmedPlayer(characterClass);
    actor.body.fill(UntouchedPart());
    actor.hands.fill(UntouchedPart());
    actor.wings = UntouchedPart();
    actor.pet = UntouchedPart();
    actor.head = UntouchedModel;
    actor.scale = 7.0f;
    actor.animation.action = 0x22;
    actor.run = 40;
    return actor;
}

class ActorEquipmentTest : public ::testing::Test
{
protected:
    ActorEquipmentTest()
    {
        // A type, a B1 and two different bytes per slot. Slots 9 to 11 too: 0x512a30 does not read them.
        for (std::size_t slot = 0; slot < m_own.equipment.size(); ++slot)
        {
            Client::Kernel::ItemState& item = m_own.equipment[slot];
            item.id = static_cast<std::uint16_t>(0x10 * (slot + 1));
            item.scalar1Low = static_cast<std::uint32_t>((slot + 1) << 3) | 0x87u;
            item.flags1b = static_cast<std::uint8_t>(0xb0 + slot);
            item.flags1c = static_cast<std::uint8_t>(0xc0 + slot);
        }
    }

    Client::Core::Expected<Client::Actors::ActorStopResult, std::string> Apply(ActorState& actor)
    {
        const Client::Actors::ActorEquipmentContext context{.own = m_own,
                                                            .ownRecordAccesses = m_accesses,
                                                            .publishedHero = m_hero,
                                                            .fullSetScan = m_runtime.fullSetScan,
                                                            .stop = {.runtime = m_runtime,
                                                                     .scene = Client::Kernel::SceneId::Game,
                                                                     .playerSkeleton = m_skeleton,
                                                                     .items = &m_items,
                                                                     .random = m_random},
                                                            .itemSetTables = &m_sets,
                                                            .itemSets = &m_itemSets};
        return Client::Actors::ApplyActorEquipment(actor, context);
    }

    // What [0x743a178] and [0x743a17c] hold.
    void ExpectScan(std::int32_t lowestLevel, std::uint8_t keepsTheBonus) const
    {
        ASSERT_TRUE(m_runtime.fullSetScan.has_value());
        EXPECT_EQ(m_runtime.fullSetScan->dword743a178, lowestLevel);
        EXPECT_EQ(m_runtime.fullSetScan->byte743a17c, keepsTheBonus);
    }

    Client::Kernel::OwnCharacterRecord m_own;
    std::uint8_t m_accesses = 0;
    ActorState m_hero;
    Client::Kernel::WorldRuntime m_runtime = [] {
        Client::Kernel::WorldRuntime runtime;
        runtime.map = 0;
        return runtime;
    }();
    Client::Models::Skeleton m_skeleton = Client::Tests::PlayerActionsSkeleton();
    Client::Tables::ItemTable m_items = Client::Tests::ItemsMarking({});
    // The set tables without any set: 0x40e1b0 walks the equipment and applies nothing.
    Client::Tables::ItemSetTables m_sets =
        Client::Tests::ItemSetTablesFrom(std::vector<std::byte>(Client::Tables::ItemSetTypeTableSize),
                                         std::vector<std::byte>(Client::Tables::ItemSetOptionTableSize));
    Client::Player::ItemSetState m_itemSets;
    CrtRandom m_random{QuietSeed()};
};

// 0x512abf..0x512b9b and 0x512cc7..0x512ec9: the model is the type + 0x212, the level (B1 >> 3) & 0xf, and the item's
// bytes +0x1b and +0x1c go as they are. The hands carry +0x1b, the wings and the pet no byte, and the body both. Slots
// 9 to 11 are not read.
TEST_F(ActorEquipmentTest, ProjectsTheNineSlotsItReads)
{
    ActorState actor = DressedInUntouched(1);
    ASSERT_TRUE(Apply(actor).HasValue());

    const auto expectedModel = [this](std::size_t slot)
    { return static_cast<std::uint16_t>(m_own.equipment[slot].id + 0x212); };
    const auto expectedLevel = [](std::size_t slot) { return static_cast<std::uint8_t>((slot + 1) & 0xf); };
    for (std::size_t side = 0; side < 2; ++side)
    {
        SCOPED_TRACE(side);
        EXPECT_EQ(actor.hands[side].model, expectedModel(side));
        EXPECT_EQ(actor.hands[side].level, expectedLevel(side));
        EXPECT_EQ(actor.hands[side].flags1b, static_cast<std::uint8_t>(0xb0 + side));
        EXPECT_EQ(actor.hands[side].flags1c, Untouched);
        // +5..+0x10, the bone and what hangs, do not belong to 0x512a30.
        EXPECT_EQ(actor.hands[side].bone, Untouched);
        EXPECT_EQ(actor.hands[side].animation.frame, 7.0f);
        EXPECT_EQ(actor.hands[side].speed, 7.0f);
    }
    for (std::size_t part = 0; part < actor.body.size(); ++part)
    {
        SCOPED_TRACE(part);
        const std::size_t slot = 2 + part;
        EXPECT_EQ(actor.body[part].model, expectedModel(slot));
        EXPECT_EQ(actor.body[part].level, expectedLevel(slot));
        EXPECT_EQ(actor.body[part].flags1b, static_cast<std::uint8_t>(0xb0 + slot));
        EXPECT_EQ(actor.body[part].flags1c, static_cast<std::uint8_t>(0xc0 + slot));
    }
    EXPECT_EQ(actor.wings.model, expectedModel(7));
    EXPECT_EQ(actor.wings.level, expectedLevel(7));
    EXPECT_EQ(actor.wings.flags1b, Untouched);
    EXPECT_EQ(actor.wings.flags1c, Untouched);
    EXPECT_EQ(actor.pet.model, expectedModel(8));
    EXPECT_EQ(actor.pet.level, expectedLevel(8));
    EXPECT_EQ(actor.pet.flags1b, Untouched);
    EXPECT_EQ(actor.pet.flags1c, Untouched);
}

// A type of −1 leaves 0xffff in the hands, the wings and the pet, and the class's part on the body: 0x2212, 0x221a,
// 0x2222, 0x222a and 0x2232 plus (class >> 3) · 5 + (class & 7). The level is copied anyway.
TEST_F(ActorEquipmentTest, EmptySlotsLeaveNoItemOrTheClassPart)
{
    for (Client::Kernel::ItemState& item : m_own.equipment)
    {
        item.id = 0xffff;
        item.scalar1Low = 0x78;
    }
    ActorState actor = DressedInUntouched(0x0a);
    EXPECT_TRUE(Apply(actor).HasValue());

    EXPECT_EQ(actor.hands[0].model, NoItemModel);
    EXPECT_EQ(actor.hands[1].model, NoItemModel);
    EXPECT_EQ(actor.wings.model, NoItemModel);
    EXPECT_EQ(actor.pet.model, NoItemModel);
    EXPECT_EQ(actor.hands[0].level, 15);
    const std::uint16_t classOffset = 1 * 5 + 2;
    const std::array<std::uint16_t, 5> classParts = {0x2212, 0x221a, 0x2222, 0x222a, 0x2232};
    for (std::size_t part = 0; part < actor.body.size(); ++part)
    {
        SCOPED_TRACE(part);
        EXPECT_EQ(actor.body[part].model, classParts[part] + classOffset);
        EXPECT_EQ(actor.body[part].level, 15);
    }
    // 0x512930: the class's part is not an open helmet.
    EXPECT_EQ(actor.head, NoItemModel);
}

// 0x512a3c..0x512a42: with another type it returns without opening the record or touching the character.
TEST_F(ActorEquipmentTest, ANonPlayerIsLeftAsIs)
{
    ActorState monster = DressedInUntouched(0);
    monster.type = 0x172;
    const auto applied = Apply(monster);
    ASSERT_TRUE(applied.HasValue());
    EXPECT_EQ(monster.hands[0].model, UntouchedModel);
    EXPECT_EQ(monster.body[0].model, UntouchedModel);
    EXPECT_EQ(monster.run, 40);
    EXPECT_EQ(m_accesses, 0);
    EXPECT_EQ(m_random.State(), CrtRandom(QuietSeed()).State());
}

// +0x3e3 other than 0: after the hands and 0x4f2250 it closes the record (0x512bea..0x512cc6) and does not touch the
// clothes, the head or the scale. The close that leaves the count at 0 draws 3848 rand(), after that of 0x4f2250; if
// the caller has it open, none.
TEST_F(ActorEquipmentTest, WithByte3e3TheClothesStayAndTheRecordCloses)
{
    const std::uint32_t seed = QuietSeed();
    ActorState actor = DressedInUntouched(0);
    actor.byte3e3 = 1;
    actor.animation.action = 0;
    const auto applied = Apply(actor);
    ASSERT_TRUE(applied.HasValue());
    EXPECT_EQ(applied.Value().sound, std::nullopt);
    EXPECT_EQ(actor.hands[0].model, m_own.equipment[0].id + 0x212);
    EXPECT_EQ(actor.body[0].model, UntouchedModel);
    EXPECT_EQ(actor.head, UntouchedModel);
    EXPECT_EQ(actor.scale, 7.0f);
    EXPECT_EQ(actor.run, 0);
    EXPECT_EQ(m_accesses, 0);
    EXPECT_EQ(m_random.State(), AfterDraws(seed, 1 + Client::Models::OwnRecordCloseRandoms).State());

    m_random = CrtRandom(seed);
    m_accesses = 1;
    ASSERT_TRUE(Apply(actor).HasValue());
    EXPECT_EQ(m_accesses, 1);
    EXPECT_EQ(m_random.State(), AfterDraws(seed, 1).State());
}

// 0x512ba1..0x512bd4: calls 0x4f2250 with the hands already set, except on maps 18 to 23, with actions 0xc5 to 0xcc or
// with those from 0x22 to 0x97.
TEST_F(ActorEquipmentTest, TheStopRunsInTheMiddleUnlessBusyOrInMaps18To23)
{
    m_own.equipment[0].id = 0;
    m_own.equipment[1].id = 0xffff;
    const auto stopsWith = [this](std::uint8_t action, std::int32_t map)
    {
        m_runtime.map = map;
        ActorState actor = DressedInUntouched(0);
        actor.byte3e3 = 1;
        actor.animation.action = action;
        EXPECT_TRUE(Apply(actor).HasValue());
        return actor.run == 0;
    };
    for (const std::uint8_t action : std::array<std::uint8_t, 6>{0x00, 0x21, 0x98, 0xc4, 0xcd, 0xff})
    {
        SCOPED_TRACE(action);
        EXPECT_TRUE(stopsWith(action, 0));
        EXPECT_TRUE(stopsWith(action, 17));
    }
    for (const std::uint8_t action : std::array<std::uint8_t, 6>{0x22, 0x50, 0x97, 0xc5, 0xca, 0xcc})
    {
        SCOPED_TRACE(action);
        EXPECT_FALSE(stopsWith(action, 0));
    }
    for (const std::int32_t map : {18, 23})
    {
        SCOPED_TRACE(map);
        EXPECT_FALSE(stopsWith(0, map));
    }

    // 0x4f2250 sees what it has just set: with the new wings it flies (9); without them, the new right hand, 0x212
    // without the mark, chooses 3.
    m_runtime.map = 0;
    ActorState actor = DressedInUntouched(0);
    actor.byte3e3 = 1;
    actor.animation.action = 0;
    ASSERT_TRUE(Apply(actor).HasValue());
    EXPECT_EQ(actor.animation.action, 9);
    m_own.equipment[7].id = 0xffff;
    ASSERT_TRUE(Apply(actor).HasValue());
    EXPECT_EQ(actor.animation.action, 3);
}

// An error from 0x4f2250 stops before the clothes, and the access to the record closes anyway.
TEST_F(ActorEquipmentTest, AStopErrorStopsBeforeTheClothes)
{
    ActorState actor = DressedInUntouched(0);
    actor.animation.action = 0;
    actor.flags7c = 0x200000;
    const auto applied = Apply(actor);
    ASSERT_FALSE(applied.HasValue());
    EXPECT_NE(applied.Error().find("0x4f2416"), std::string::npos);
    EXPECT_EQ(actor.body[0].model, UntouchedModel);
    EXPECT_EQ(m_accesses, 0);
}

// The clothes go on with 0x4053f0, 0x512930, 0x505820 with the published hero and 0x550d10, whole. With another
// character 0x505820 does not run and 0x550990 reads what was there: here, the .bss's 0. With the hero it does: without
// a set, [0x743a178] is left at 0 and [0x743a17c] at 1.
TEST_F(ActorEquipmentTest, TheClothesRunTheScanOnlyWithTheHeroAndThenTheStats)
{
    // 0x1012 is an open helmet: 0x512930 sets the class's head.
    m_own.equipment[2].id = 0x1012 - 0x212;

    ActorState someone = DressedInUntouched(2);
    someone.skin = 1;
    const auto stats = Apply(someone);
    ASSERT_TRUE(stats.HasValue()) << stats.Error();
    EXPECT_EQ(someone.head, 0x2212 + 2);
    EXPECT_EQ(someone.scale, 0.86f);
    EXPECT_EQ(m_accesses, 0);
    ExpectScan(0, 0);

    ActorState& hero = m_hero;
    hero = DressedInUntouched(5);
    const auto scanned = Apply(hero);
    ASSERT_TRUE(scanned.HasValue()) << scanned.Error();
    EXPECT_EQ(hero.head, 0x2212 + 5);
    // Class 5 has no case in 0x512930's tables: the scale stays.
    EXPECT_EQ(hero.scale, 7.0f);
    ExpectScan(0, 1);
}

// 0x550d10 (0x512ef5) runs after the head and before the close, with the record opened by 0x512a30: 0x54ef00 sets the
// WORDs from +0x2a to +0x36 to 0, and the eight helpers and the tail follow. The 33 accesses of 0x40e1b0 do not leave
// it at 0: the rand() are the tail's three and those of 0x512a30's close.
TEST_F(ActorEquipmentTest, TheStatsRunWholeBeforeTheClose)
{
    const std::uint32_t seed = QuietSeed();
    m_own.word2a = 0x2a2a;
    m_own.word36 = 0x3636;
    m_own.wordef8 = 0xef8;
    ActorState actor = DressedInUntouched(0);

    ASSERT_TRUE(Apply(actor).HasValue());

    EXPECT_EQ(m_own.word2a, 0);
    EXPECT_EQ(m_own.word36, 0);
    // The tail: +0xef0 − +0xee6, both at 0.
    EXPECT_EQ(m_own.wordef8, 0);
    EXPECT_EQ(actor.head, NoItemModel);
    EXPECT_EQ(m_accesses, 0);
    EXPECT_EQ(m_random.State(), AfterDraws(seed, TailRandoms + Client::Models::OwnRecordCloseRandoms).State());
}

// 0x512eda..0x512eec: 0x505820 with the hero already dressed and the record opened by 0x512a30, so class 3's access
// does not leave it at 0 or draw rand(). 0x550990 reads what it left: with [0x743a17c] at 1 and [0x743a178] at 13,
// +0x62 adds _ftol(+0x62 · 0.2f) (0x550b1e..0x550b83).
TEST_F(ActorEquipmentTest, TheHeroIsScannedWithTheRecordOpenAnd0x550990ReadsIt)
{
    const std::uint32_t seed = QuietSeed();
    // From the helmet to the boots, the same index in groups 7 to 11; the helmet at level 9.
    const std::array<std::uint8_t, 5> levels{9, 13, 14, 15, 13};
    const auto wear = [this, &levels](std::uint16_t index)
    {
        for (std::size_t part = 0; part < levels.size(); ++part)
        {
            Client::Kernel::ItemState& item = m_own.equipment[2 + part];
            item.id = static_cast<std::uint16_t>(((7 + part) << 9) + index);
            item.scalar1Low = static_cast<std::uint32_t>(levels[part]) << 3;
            item.flags1c = 0;
        }
    };
    // With class 3 in the record, the base of +0x62 is agility / 5 (0x550990): 80.
    m_own.characterClass = 3;
    m_own.agility = 400;
    ActorState& hero = m_hero;

    // Class 3 does not look at the helmet: the lowest level is 13. Armor 8 15 is one of the four (0x505aa7).
    wear(15);
    hero = DressedInUntouched(3);
    ASSERT_TRUE(Apply(hero).HasValue());
    ExpectScan(13, 1);
    EXPECT_EQ(m_own.word62, 80 + 16);
    EXPECT_EQ(m_accesses, 0);
    EXPECT_EQ(m_random.State(), AfterDraws(seed, TailRandoms + Client::Models::OwnRecordCloseRandoms).State());

    // With 8 5, [0x743a17c] is left at 0 and +0x62 adds nothing.
    m_random = CrtRandom(seed);
    wear(5);
    hero = DressedInUntouched(3);
    ASSERT_TRUE(Apply(hero).HasValue());
    ExpectScan(13, 0);
    EXPECT_EQ(m_own.word62, 80);
    EXPECT_EQ(m_random.State(), AfterDraws(seed, TailRandoms + Client::Models::OwnRecordCloseRandoms).State());

    // Another class looks at the helmet: the lowest is 9, which has no factor, and [0x743a17c] is left at 1.
    hero = DressedInUntouched(1);
    ASSERT_TRUE(Apply(hero).HasValue());
    ExpectScan(9, 1);
    EXPECT_EQ(m_own.word62, 80);
}

// With another character 0x550990 reads what the last 0x505820 left, without touching it. If MuRet does not know it (a
// game frame in between), it stops there with an error; with the hero, 0x505820 writes it again first.
TEST_F(ActorEquipmentTest, AnotherCharacterReadsWhatTheLastScanLeft)
{
    m_own.agility = 400;
    m_own.characterClass = 3;
    for (Client::Kernel::ItemState& item : m_own.equipment)
    {
        item.flags1c = 0;
    }
    ActorState someone = DressedInUntouched(3);

    m_runtime.fullSetScan = Client::Actors::Scan505820{.dword743a178 = 12, .byte743a17c = 1};
    ASSERT_TRUE(Apply(someone).HasValue());
    ExpectScan(12, 1);
    EXPECT_EQ(m_own.word62, 80 + 12); // _ftol(80 · 0.15f)

    m_runtime.fullSetScan = std::nullopt;
    const auto unknown = Apply(someone);
    ASSERT_FALSE(unknown.HasValue());
    EXPECT_NE(unknown.Error().find("0x550990"), std::string::npos);
    EXPECT_FALSE(m_runtime.fullSetScan.has_value());
    EXPECT_EQ(m_accesses, 0);

    m_hero = DressedInUntouched(3);
    ASSERT_TRUE(Apply(m_hero).HasValue());
    ExpectScan(0, 1);
}

// 0x4053f0 on maps 18 to 23, after the clothes: 0x4053a0 leaves the hands, the wings and the pet without an item, with
// the level at 0 and the hands' +0x1b at 0 (not that of the wings or the pet), and the class (+0x1d4 & 7) chooses the
// weapon: 0xc17 in the right hand of class 0, 0x222 in both hands of classes 1, 3 and 4, 0xa25 in the right hand of
// class 2 and nothing for 5 to 7. It goes on with 0x512930 and 0x550d10.
TEST_F(ActorEquipmentTest, Maps18To23LeaveOnlyTheWeaponOfTheClass)
{
    struct Armed
    {
        std::uint16_t right;
        std::uint16_t left;
    };
    const std::array<Armed, 8> byClass = {Armed{0xc17, NoItemModel},
                                          Armed{0x222, 0x222},
                                          Armed{0xa25, NoItemModel},
                                          Armed{0x222, 0x222},
                                          Armed{0x222, 0x222},
                                          Armed{NoItemModel, NoItemModel},
                                          Armed{NoItemModel, NoItemModel},
                                          Armed{NoItemModel, NoItemModel}};
    m_own.equipment[8].id = 0x1c15 - 0x212;
    for (const std::int32_t map : {18, 23})
    {
        m_runtime.map = map;
        for (std::uint8_t characterClass = 0; characterClass < 16; ++characterClass)
        {
            SCOPED_TRACE(testing::Message() << "mapa " << map << ", clase " << int{characterClass});
            ActorState actor = DressedInUntouched(characterClass);
            actor.previousHad1c15 = true;
            ASSERT_TRUE(Apply(actor).HasValue());

            const Armed& armed = byClass[static_cast<std::size_t>(characterClass & 7)];
            EXPECT_EQ(actor.hands[0].model, armed.right);
            EXPECT_EQ(actor.hands[1].model, armed.left);
            for (const ActorItemPart& hand : actor.hands)
            {
                EXPECT_EQ(hand.level, 0);
                EXPECT_EQ(hand.flags1b, 0);
                EXPECT_EQ(hand.flags1c, Untouched);
            }
            EXPECT_EQ(actor.wings.model, NoItemModel);
            EXPECT_EQ(actor.wings.level, 0);
            EXPECT_EQ(actor.wings.flags1b, Untouched);
            EXPECT_EQ(actor.pet.model, NoItemModel);
            EXPECT_EQ(actor.pet.level, 0);
            EXPECT_EQ(actor.pet.flags1b, Untouched);
            EXPECT_FALSE(actor.previousHad1c15);
            // The earlier clothes stay, and 0x512930 runs afterwards.
            EXPECT_EQ(actor.body[0].model, m_own.equipment[2].id + 0x212);
            EXPECT_EQ(actor.head, NoItemModel);
        }
    }
}

// 0x512a30 writes the whole word318: what the selection keeps of it is brought up to date.
TEST_F(ActorEquipmentTest, ThePetKeepsTheWord318HistoryInStep)
{
    ActorState actor = DressedInUntouched(0);
    actor.byte3e3 = 1;
    m_own.equipment[8].id = 0x1c15 - 0x212;
    ASSERT_TRUE(Apply(actor).HasValue());
    EXPECT_TRUE(actor.previousHad1c15);

    m_own.equipment[8].id = 0x1c16 - 0x212;
    ASSERT_TRUE(Apply(actor).HasValue());
    EXPECT_FALSE(actor.previousHad1c15);
}
} // namespace
