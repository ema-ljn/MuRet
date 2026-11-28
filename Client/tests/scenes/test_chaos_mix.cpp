#include "ChaosMix/ChaosMix.h"

#include "Crt/CrtRandom.h"
#include "Tables/ItemTable.h"
#include "Models/LogicalAccess.h"
#include "Items/ItemPrice.h"
#include "Npc/NpcWindowState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/OwnInventory.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Kernel/ItemState.h"

#include "support/TableFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>
#include <utility>
#include <vector>

// The chaos box (CL9 milestone 6, phase 3; 06 § La caja del caos): what it is worth (0x5fd120), the mix kind (0x5fd4f0
// and 0x5fd4b0's mode), the room it asks for and what its button does (0x5fe9a2..0x5fea04), the drawing's rate and
// price (0x5fedac..0x5ff0c2) and the thousands of 0x5b1130 and 0x5b12f0. The Item.bmd items are made up, with a fixed
// price (+0x40) so that 0x54d0f0 is easy to follow.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ItemState;
using Client::Kernel::WorldState;
using Client::Npc::NpcWindowState;

constexpr int Own = Client::Models::OwnRecordCloseRandoms;
constexpr std::uint32_t Seed = 0x13572468u;

constexpr std::uint16_t Sword = 0x0001;
constexpr std::uint16_t Staff = 0x0a07;
constexpr std::uint16_t Armor = 0x0c00;
constexpr std::uint16_t Wings = 0x1800;
constexpr std::uint16_t Chaos = 0x180f;
constexpr std::uint16_t Uniria = 0x1a02;
constexpr std::uint16_t Feather = 0x1a0e;
constexpr std::uint16_t Scroll = 0x1a10;
constexpr std::uint16_t Bone = 0x1a11;
constexpr std::uint16_t Horn = 0x1a20;
constexpr std::uint16_t Claw = 0x1a21;
constexpr std::uint16_t Fragment = 0x1a22;
constexpr std::uint16_t Piece = 0x1a23;
constexpr std::uint16_t Crest = 0x1a24;
constexpr std::uint16_t Fenrir = 0x1a25;
constexpr std::uint16_t Bless = 0x1c0d;
constexpr std::uint16_t Soul = 0x1c0e;
constexpr std::uint16_t Life = 0x1c10;
constexpr std::uint16_t Eye = 0x1c11;
constexpr std::uint16_t Key = 0x1c12;
constexpr std::uint16_t Creation = 0x1c16;
constexpr std::uint16_t Guardian = 0x1c1f;
constexpr std::uint32_t SwordPrice = 300'000;
constexpr std::uint32_t StaffPrice = 60'000;
constexpr std::uint32_t ArmorPrice = 90'000;
constexpr std::uint32_t WingsPrice = 8'000'000;

void PutDword(std::vector<std::byte>& plain, std::size_t at, std::uint32_t value)
{
    for (std::size_t index = 0; index < 4; ++index)
        plain[at + index] = static_cast<std::byte>((value >> (8 * index)) & 0xffu);
}

Client::Tables::ItemTable ChaosItems()
{
    using Client::Tables::ItemRecordSize;
    struct Shape
    {
        std::uint16_t type;
        std::uint8_t width;
        std::uint8_t height;
        std::uint32_t price;
    };
    constexpr std::array<Shape, 23> Shapes{
        {{Sword, 1, 3, SwordPrice}, {Staff, 1, 4, StaffPrice}, {Armor, 2, 3, ArmorPrice}, {Wings, 3, 2, WingsPrice},
         {Chaos, 1, 1, 0},          {Uniria, 1, 1, 1000},      {Feather, 1, 2, 1000},     {Scroll, 1, 1, 1000},
         {Bone, 1, 1, 1000},        {Horn, 1, 1, 1000},        {Claw, 1, 1, 1000},        {Fragment, 1, 1, 1000},
         {Piece, 1, 1, 1000},       {Crest, 1, 1, 1000},       {Fenrir, 2, 2, 1000},      {Bless, 1, 1, 0},
         {Soul, 1, 1, 0},           {Life, 1, 1, 0},           {Eye, 1, 1, 1000},         {Key, 1, 1, 1000},
         {Creation, 1, 1, 0},       {Guardian, 1, 1, 1000},    {0x0002, 1, 1, 0}}};
    std::vector<std::byte> plain(Client::Tables::ItemTableSize);
    for (const Shape& shape : Shapes)
    {
        const std::size_t base = std::size_t{shape.type} * ItemRecordSize;
        plain[base + 0x24] = std::byte{shape.width};
        plain[base + 0x25] = std::byte{shape.height};
        PutDword(plain, base + 0x40, shape.price);
    }
    const std::vector<std::byte> encrypted = Client::Tests::EncryptRecords(std::move(plain), ItemRecordSize);
    return Client::Tables::ItemTable(std::span<const std::byte>(encrypted));
}

ItemState Item(std::uint16_t type, std::uint32_t level = 0)
{
    return ItemState{.id = type, .scalar1Low = level << 3, .occupied = 1};
}

// An option: its mark (D+0x2b) and its value (D+0x33).
struct Option
{
    std::uint8_t tag = 0;
    std::uint8_t value = 0;
};

ItemState WithOptions(ItemState item, std::initializer_list<Option> options)
{
    for (const Option& option : options)
    {
        item.optionBytes[item.optionCount] = option.tag;
        item.optionBytes[Client::Kernel::ItemOptionValueOffset + item.optionCount] = option.value;
        ++item.optionCount;
    }
    return item;
}

ItemState Excellent(ItemState item)
{
    item.flags1b = 1;
    return item;
}

class ChaosMixTest : public ::testing::Test
{
protected:
    ChaosMixTest() : m_world(std::make_unique<WorldState>())
    {
        Clear();
    }

    void Clear()
    {
        for (ItemState& cell : Npc().chaosBox)
            cell = ItemState{.id = 0xffff};
        m_next = 0;
    }

    // One after the other, in separate cells: 0x5fd4f0 only looks at each one's type and +0x3c.
    void Put(std::initializer_list<ItemState> items)
    {
        for (const ItemState& item : items)
            Npc().chaosBox.at(m_next++) = item;
    }

    void PutTimes(const ItemState& item, int count)
    {
        for (int i = 0; i < count; ++i)
            Put({item});
    }

    [[nodiscard]] std::int32_t Kind()
    {
        auto kind = Client::ChaosMix::ChaosMixKind(*m_world, m_random, &m_items);
        EXPECT_TRUE(kind.HasValue()) << kind.Error();
        return kind.HasValue() ? kind.Value() : 0x7fff;
    }

    NpcWindowState& Npc()
    {
        return m_world->Runtime().npcWindows;
    }

    std::unique_ptr<WorldState> m_world;
    CrtRandom m_random{Seed};
    Client::Tables::ItemTable m_items = ChaosItems();
    std::size_t m_next = 0;
};

CrtRandom Advanced(CrtRandom random, int count)
{
    random.Discard(static_cast<std::uint32_t>(count));
    return random;
}

// 0x5fd120: the sword is worth 0x54d0f0's value in mode 0 (its fixed price), the jewels their own, and without the
// chaos one, 0.
TEST_F(ChaosMixTest, TheBoxIsWorthItsItemsOnlyWithAChaosJewel)
{
    Put({Item(Sword), Item(Bless), Item(Soul), Item(Life), Item(Creation)});
    const Client::Items::PetPriceLevels pets{};
    auto none = Client::ChaosMix::ChaosBoxValue(Npc().chaosBox, &m_items, pets);
    ASSERT_TRUE(none.HasValue()) << none.Error();
    EXPECT_EQ(none.Value(), 0);
    Put({Item(Chaos)});
    auto value = Client::ChaosMix::ChaosBoxValue(Npc().chaosBox, &m_items, pets);
    ASSERT_TRUE(value.HasValue()) << value.Error();
    EXPECT_EQ(value.Value(), 300'000 + 100'000 + 70'000 + 450'000 + 450'000 + 40'000);
    // +0x3c at 0 (an item's footprint) does not count.
    ItemState footprint = Item(Sword);
    footprint.occupied = 0;
    Put({footprint});
    EXPECT_EQ(Client::ChaosMix::ChaosBoxValue(Npc().chaosBox, &m_items, pets).Value(), value.Value());
    EXPECT_FALSE(Client::ChaosMix::ChaosBoxValue(Npc().chaosBox, nullptr, pets).HasValue());
}

// 0x5fdac0..0x5fdb8a: the eye and the key of the same level with the chaos jewel are the 2 with level + 1; different,
// the −2. The scroll and the bone, the 8 and the −8. The larger width and height go back to 1 before counting.
TEST_F(ChaosMixTest, TheTicketsNeedTheSameLevel)
{
    Put({Item(Eye, 3), Item(Key, 3), Item(Chaos)});
    EXPECT_EQ(Kind(), 2);
    EXPECT_EQ(Npc().mixLevel, 4);
    EXPECT_EQ(Npc().mixWidth, 1);
    EXPECT_EQ(Npc().mixHeight, 1);
    Clear();
    Put({Item(Eye, 3), Item(Key, 4), Item(Chaos)});
    EXPECT_EQ(Kind(), -2);
    Clear();
    Put({Item(Scroll, 2), Item(Bone, 2), Item(Chaos)});
    EXPECT_EQ(Kind(), 8);
    EXPECT_EQ(Npc().mixLevel, 3);
    Clear();
    Put({Item(Scroll, 2), Item(Bone, 1), Item(Chaos)});
    EXPECT_EQ(Kind(), -8);
    // Without the chaos jewel it is none.
    Clear();
    Put({Item(Eye, 3), Item(Key, 3), Item(Bless)});
    EXPECT_EQ(Kind(), 0);
    EXPECT_EQ(m_random.State(), Seed);
}

// 0x5fe3c7..0x5fe4e4: an item of level 9 to 12 with 1 to 4 Bless and Soul jewels and the chaos one: the 3, the 4, the
// 0x16 and the 0x17. The leveled item's luck (0x54) stays in 0x7d2dc9c. With another count, the tail or nothing.
TEST_F(ChaosMixTest, TheUpgradesNeedTheirJewels)
{
    Put({WithOptions(Item(Sword, 9), {{0x54, 0}}), Item(Bless), Item(Soul), Item(Chaos)});
    EXPECT_EQ(Kind(), 3);
    EXPECT_EQ(Npc().mixLuck, 1);
    EXPECT_EQ(Npc().mixWidth, 1);
    EXPECT_EQ(Npc().mixHeight, 3);
    Clear();
    Put({Item(Armor, 10), Item(Chaos)});
    PutTimes(Item(Bless), 2);
    PutTimes(Item(Soul), 2);
    EXPECT_EQ(Kind(), 4);
    EXPECT_EQ(Npc().mixLuck, 0);
    EXPECT_EQ(Npc().mixWidth, 2);
    EXPECT_EQ(Npc().mixHeight, 3);
    Clear();
    Put({Item(Sword, 11), Item(Chaos)});
    PutTimes(Item(Bless), 3);
    PutTimes(Item(Soul), 3);
    EXPECT_EQ(Kind(), 0x16);
    Clear();
    Put({Item(Sword, 12), Item(Chaos)});
    PutTimes(Item(Bless), 4);
    PutTimes(Item(Soul), 4);
    EXPECT_EQ(Kind(), 0x17);
    // A +10 with just one of each: neither the 3 nor the 4.
    Clear();
    Put({Item(Sword, 10), Item(Bless), Item(Soul), Item(Chaos)});
    EXPECT_EQ(Kind(), 0);
}

// 0x5fe559..0x5fe5b5: only Bless jewels, the 0xf; only Soul, the 0x10; 5 and 5 with the chaos and the guardian ones,
// the 0x11.
TEST_F(ChaosMixTest, OnlyBlessOrOnlySoulArePotions)
{
    PutTimes(Item(Bless), 3);
    EXPECT_EQ(Kind(), 0xf);
    Clear();
    PutTimes(Item(Soul), 2);
    EXPECT_EQ(Kind(), 0x10);
    Clear();
    PutTimes(Item(Bless), 5);
    PutTimes(Item(Soul), 5);
    Put({Item(Chaos), Item(Guardian)});
    EXPECT_EQ(Kind(), 0x11);
    Clear();
    EXPECT_EQ(Kind(), 0);
}

// 0x5fe4e9..0x5fe558: items of level 4 or more with an option from 0x50 to 0x53 and the chaos jewel, and nothing else
// but jewels: the 1 (the chaos weapon), or the 11 with a staff 0x406, 0x806 or 0xa07. The excellents' value
// (0x7d2dc88) adds its 0x54d0f0 in mode 1.
TEST_F(ChaosMixTest, TheChaosWeaponNeedsAnOptionAndAChaosJewel)
{
    Put({WithOptions(Item(Sword, 4), {{0x51, 1}}), Item(Chaos), Item(Bless)});
    EXPECT_EQ(Kind(), 1);
    EXPECT_EQ(Npc().mixExcellentValue, static_cast<std::int32_t>(SwordPrice / 3));
    Clear();
    Put({WithOptions(Item(Staff, 5), {{0x50, 1}}), Item(Chaos)});
    EXPECT_EQ(Kind(), 11);
    // An item without the option counts among the others and there is no mix.
    Clear();
    Put({WithOptions(Item(Sword, 4), {{0x51, 1}}), Item(Armor, 4), Item(Chaos)});
    EXPECT_EQ(Kind(), 0);
}

// 0x5fdc31..0x5fe3c6: the wings, the level-0 feather, the chaos jewel and the rest excellent are the 7, with the rate
// from 0x54d0f0 in mode 0: 300000 / 40000 + 8000000 / 4000000 = 9. The wings' option 0x55 is worth its value · 4.
TEST_F(ChaosMixTest, TheWingsRateComesFromThePrices)
{
    Put({WithOptions(Item(Wings), {{0x55, 3}}), Item(Feather), Item(Chaos), Excellent(Item(Sword, 4))});
    EXPECT_EQ(Kind(), 7);
    EXPECT_EQ(Npc().mixWingRate, 9);
    EXPECT_EQ(Npc().mixWingOption, 12);
    EXPECT_EQ(Npc().mixWidth, 3);
    EXPECT_EQ(Npc().mixHeight, 3);
    EXPECT_EQ(m_random.State(), Seed);
    // Without the excellent, no.
    Clear();
    Put({Item(Wings), Item(Feather), Item(Chaos), Item(Sword, 4)});
    EXPECT_EQ(Kind(), 0);
}

// 0x5fdc6f..0x5fe12e: with the level-1 feather, the hero's level (+0xe of the own record, opened and closed: 3848
// rand()): below 10, the −0x18; from 10, the 0x18 with the wings' rate.
TEST_F(ChaosMixTest, TheCapeAsksForTheHeroLevel)
{
    for (const std::uint16_t level : {std::uint16_t{9}, std::uint16_t{10}})
    {
        SCOPED_TRACE(level);
        Clear();
        m_world->OwnCharacter().level = level;
        Put({Item(Wings), Item(Feather, 1), Item(Chaos), Excellent(Item(Sword, 4))});
        const CrtRandom expected = Advanced(m_random, Own);
        EXPECT_EQ(Kind(), level < 10 ? -0x18 : 0x18);
        EXPECT_EQ(m_random.State(), expected.State());
        EXPECT_EQ(Npc().mixWingRate, level < 10 ? 0 : 9);
    }
}

// 0x5fe3c7..0x5fe4e4: ten whole Uniria (durability 255) with the chaos jewel, the 5; the creation jewel with the chaos
// one, the 6. A worn Uniria does not count.
TEST_F(ChaosMixTest, TheUniriaAndTheFruit)
{
    ItemState full = Item(Uniria);
    full.durability = 0xff;
    PutTimes(full, 10);
    Put({Item(Chaos)});
    EXPECT_EQ(Kind(), 5);
    Clear();
    ItemState worn = full;
    worn.durability = 0xfe;
    PutTimes(full, 9);
    Put({worn, Item(Chaos)});
    EXPECT_EQ(Kind(), 0);
    Clear();
    Put({Item(Creation), Item(Chaos)});
    EXPECT_EQ(Kind(), 6);
}

// 0x5fdb8f..0x5fdc30: the Fenrir mixes (0x19, 0x1a, 0x1b) and the 0x1c's cape, 3 with a weapon with an option and 4
// with an armor.
TEST_F(ChaosMixTest, TheFenrirMixes)
{
    ItemState horn = Item(Horn);
    horn.durability = 0x14;
    ItemState claw = Item(Claw);
    claw.durability = 0x14;
    Put({horn, claw, Item(Chaos)});
    EXPECT_EQ(Kind(), 0x19);
    Clear();
    ItemState fragment = Item(Fragment);
    fragment.durability = 10;
    Put({fragment, Item(Chaos)});
    PutTimes(Item(Piece), 5);
    EXPECT_EQ(Kind(), 0x1a);
    Clear();
    Put({Item(Crest), Item(Chaos)});
    PutTimes(Item(Life), 3);
    EXPECT_EQ(Kind(), 0x1b);
    Clear();
    Put({Item(Fenrir), Item(Chaos), WithOptions(Item(Sword, 4), {{0x52, 1}})});
    PutTimes(Item(Life), 5);
    EXPECT_EQ(Kind(), 0x1c);
    EXPECT_EQ(Npc().mixCapeKind, 3);
    EXPECT_EQ(Npc().mixExcellentValue, static_cast<std::int32_t>(SwordPrice / 3));
    Clear();
    Put({Item(Fenrir), Item(Chaos), WithOptions(Item(Armor, 4), {{0x53, 1}})});
    PutTimes(Item(Life), 5);
    EXPECT_EQ(Kind(), 0x1c);
    EXPECT_EQ(Npc().mixCapeKind, 4);
}

// 0x5fd4b0: mode 0 does not allow the 1, mode 1 only allows the 1 and the others allow everything.
TEST(ChaosMixKindForModeTest, TheModeFiltersTheChaosWeapon)
{
    using Client::ChaosMix::ChaosMixKindForMode;
    EXPECT_EQ(ChaosMixKindForMode(1, 0), 0);
    EXPECT_EQ(ChaosMixKindForMode(3, 0), 3);
    EXPECT_EQ(ChaosMixKindForMode(1, 1), 1);
    EXPECT_EQ(ChaosMixKindForMode(3, 1), 0);
    EXPECT_EQ(ChaosMixKindForMode(-2, 1), 0);
    EXPECT_EQ(ChaosMixKindForMode(1, 2), 1);
    EXPECT_EQ(ChaosMixKindForMode(-0x18, 2), -0x18);
}

// 0x5fedac..0x5ff0c2: without its own case, the worth / 20000 (capped at 100) and · 10000; the 2 with its level's rate
// (60 for 1, 0x6c9da8's from 2 to 7) and its price table; from level 8 it reads the dwords of the adjacent .data.
TEST(MixQuoteTest, TheQuoteFollowsTheKind)
{
    auto npc = std::make_unique<NpcWindowState>();
    npc->chaosBoxValue = 250'000;
    npc->mixKind = 1;
    auto quote = Client::ChaosMix::MixQuoteOf(*npc);
    ASSERT_TRUE(quote.HasValue()) << quote.Error();
    EXPECT_EQ(quote.Value().rate, 12);
    EXPECT_EQ(quote.Value().price, 120'000u);
    EXPECT_EQ(quote.Value().priceText, "120,000");
    npc->chaosBoxValue = 3'000'000;
    EXPECT_EQ(Client::ChaosMix::MixQuoteOf(*npc).Value().rate, 100);
    EXPECT_EQ(Client::ChaosMix::MixQuoteOf(*npc).Value().priceText, "1,000,000");

    // The −0x18 leaves the price from the worth with the rate at 0.
    npc->mixKind = -0x18;
    EXPECT_EQ(Client::ChaosMix::MixQuoteOf(*npc).Value().rate, 0);
    EXPECT_EQ(Client::ChaosMix::MixQuoteOf(*npc).Value().price, 1'000'000u);
    npc->mixKind = -2;
    EXPECT_EQ(Client::ChaosMix::MixQuoteOf(*npc).Value().priceText, "0");

    npc->mixKind = 2;
    constexpr std::array<std::int32_t, 7> Rates{60, 75, 70, 65, 60, 55, 50};
    constexpr std::array<std::uint32_t, 7> Prices{100'000, 100'000, 200'000, 400'000, 700'000, 1'100'000, 1'600'000};
    for (std::int32_t level = 1; level <= 7; ++level)
    {
        SCOPED_TRACE(level);
        npc->mixLevel = level;
        auto ticket = Client::ChaosMix::MixQuoteOf(*npc);
        ASSERT_TRUE(ticket.HasValue()) << ticket.Error();
        EXPECT_EQ(ticket.Value().rate, Rates[static_cast<std::size_t>(level - 1)]);
        EXPECT_EQ(ticket.Value().price, Prices[static_cast<std::size_t>(level - 1)]);
    }
    // The rates the 30 sends with result 3 override the .data ones.
    npc->chaosRates = {10, 20, 30, 40, 50, 60};
    npc->mixLevel = 3;
    EXPECT_EQ(Client::ChaosMix::MixQuoteOf(*npc).Value().rate, 20);

    npc->mixKind = 8;
    npc->mixLevel = 9;
    EXPECT_EQ(Client::ChaosMix::MixQuoteOf(*npc).Value().rate, 80);
    EXPECT_EQ(Client::ChaosMix::MixQuoteOf(*npc).Value().price, 850'000u);

    npc->mixKind = 4;
    npc->mixLuck = 1;
    EXPECT_EQ(Client::ChaosMix::MixQuoteOf(*npc).Value().rate, 70);
    EXPECT_EQ(Client::ChaosMix::MixQuoteOf(*npc).Value().priceText, "4,000,000");
    npc->mixKind = 0x17;
    npc->mixLuck = 0;
    EXPECT_EQ(Client::ChaosMix::MixQuoteOf(*npc).Value().rate, 45);
    EXPECT_EQ(Client::ChaosMix::MixQuoteOf(*npc).Value().price, 8'000'000u);

    npc->mixKind = 0x1c;
    npc->mixExcellentValue = 300'000;
    EXPECT_EQ(Client::ChaosMix::MixQuoteOf(*npc).Value().rate, 30);
    npc->mixExcellentValue = 1'000'000;
    EXPECT_EQ(Client::ChaosMix::MixQuoteOf(*npc).Value().rate, 79);
    EXPECT_EQ(Client::ChaosMix::MixQuoteOf(*npc).Value().price, 10'000'000u);

    npc->mixKind = 0x18;
    npc->mixWingRate = 33;
    EXPECT_EQ(Client::ChaosMix::MixQuoteOf(*npc).Value().rate, 33);
    EXPECT_EQ(Client::ChaosMix::MixQuoteOf(*npc).Value().price, 5'000'000u);
}

// 0x5fd200 and 0x5febd4's table: the room each kind asks for in the 8 × 8 inventory; 0x5feb88: what the button does.
TEST(MixRoomTest, TheRoomAndTheButton)
{
    auto npc = std::make_unique<NpcWindowState>();
    npc->mixWidth = 2;
    npc->mixHeight = 3;
    using Client::ChaosMix::MixButtonAction;
    using Client::ChaosMix::MixRoomFor;
    EXPECT_EQ(MixRoomFor(*npc, 1).width, 5);
    EXPECT_EQ(MixRoomFor(*npc, 7).height, 4);
    EXPECT_EQ(MixRoomFor(*npc, 8).width, 2);
    EXPECT_EQ(MixRoomFor(*npc, 0xf).width, 1);
    EXPECT_EQ(MixRoomFor(*npc, 0x18).height, 3);
    EXPECT_EQ(MixRoomFor(*npc, 3).width, 2);
    EXPECT_EQ(MixRoomFor(*npc, 3).height, 3);

    std::array<ItemState, Client::Kernel::InventoryCellCount> cells{};
    for (ItemState& cell : cells)
        cell.id = 0x0002;
    EXPECT_FALSE(Client::ChaosMix::HasRoomFor(cells, 8, 8, 1, 1));
    EXPECT_TRUE(Client::ChaosMix::HasRoomFor(cells, 8, 8, 1, 0));
    // A 2 × 3 hole at the bottom right.
    for (std::size_t row = 5; row < 8; ++row)
    {
        for (std::size_t column = 6; column < 8; ++column)
            cells[row * 8 + column].id = 0xffff;
    }
    EXPECT_TRUE(Client::ChaosMix::HasRoomFor(cells, 8, 8, 2, 3));
    EXPECT_FALSE(Client::ChaosMix::HasRoomFor(cells, 8, 8, 3, 2));
    EXPECT_FALSE(Client::ChaosMix::HasRoomFor(cells, 8, 8, 5, 4));

    using Client::ChaosMix::MixButtonActionFor;
    EXPECT_EQ(MixButtonActionFor(-0x18), MixButtonAction::LevelTooLow);
    EXPECT_EQ(MixButtonActionFor(-8), MixButtonAction::NotTheSame);
    EXPECT_EQ(MixButtonActionFor(-2), MixButtonAction::NotTheSame);
    EXPECT_EQ(MixButtonActionFor(0), MixButtonAction::NothingToMix);
    EXPECT_EQ(MixButtonActionFor(0xb), MixButtonAction::Mix);
    EXPECT_EQ(MixButtonActionFor(0x1c), MixButtonAction::Mix);
    EXPECT_EQ(MixButtonActionFor(9), MixButtonAction::Nothing);
    EXPECT_EQ(MixButtonActionFor(-1), MixButtonAction::Nothing);
}

// 0x5b1130 and 0x5b12f0: the thousands unsigned and the tax (signed, in 64 bits) added to the price.
// 0x5fee3a reads dwords after the rates. The eye's level has four bits: mixLevel reaches 16.
TEST(MixQuoteTest, HighInvitationsReadTheNativeDataAfterTheRates)
{
    auto npc = std::make_unique<NpcWindowState>();
    npc->mixKind = 2;
    constexpr std::array<std::int32_t, 9> Rates{0x2b207325, 0x25642520, 0x25, 4, -1, -1, -1, -1, -1082130432};
    for (std::size_t i = 0; i < Rates.size(); ++i)
    {
        npc->mixLevel = 8 + static_cast<std::int32_t>(i);
        SCOPED_TRACE(npc->mixLevel);
        const auto quote = Client::ChaosMix::MixQuoteOf(*npc);
        ASSERT_TRUE(quote.HasValue()) << quote.Error();
        EXPECT_EQ(quote.Value().rate, Rates[i]);
        EXPECT_EQ(quote.Value().price, 1'600'000u);
        EXPECT_EQ(quote.Value().priceText, "1,600,000");
    }
}

TEST(MixQuoteTest, ThousandsAndTax)
{
    using Client::ChaosMix::TaxedPriceText;
    using Client::ChaosMix::ThousandsText;
    EXPECT_EQ(ThousandsText(0), "0");
    EXPECT_EQ(ThousandsText(999), "999");
    EXPECT_EQ(ThousandsText(1000), "1,000");
    EXPECT_EQ(ThousandsText(1'234'567), "1,234,567");
    EXPECT_EQ(ThousandsText(4'294'967'295u), "4,294,967,295");
    EXPECT_EQ(TaxedPriceText(2'000'000, 0), "2,000,000");
    EXPECT_EQ(TaxedPriceText(2'000'000, 3), "2,060,000");
    EXPECT_EQ(TaxedPriceText(2'000'000, -5), "1,900,000");
    EXPECT_EQ(TaxedPriceText(999, 1), "1,008");
}
} // namespace
