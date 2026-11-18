#include "Scenes/ChaosMix.h"

#include "Data/Tables/ItemTable.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ItemPrice.h"
#include "Scenes/NpcWindowState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/ItemState.h"

#include "support/TableFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

// La caja del caos (CL9 hito 6, fase 3; 06 § La caja del caos): lo que vale (0x5fd120), el tipo de la mezcla
// (0x5fd4f0 y el modo de 0x5fd4b0), el lugar que pide y lo que hace su botón (0x5fe9a2..0x5fea04), la chance y el
// precio del dibujo (0x5fedac..0x5ff0c2) y los miles de 0x5b1130 y 0x5b12f0. Los ítems de Item.bmd son inventados,
// con precio fijo (+0x40) para que 0x54d0f0 sea fácil de seguir.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::NpcWindowState;
using Client::Scenes::WorldState;
using Client::World::ItemState;

constexpr int Own = Client::Render::Models::OwnRecordCloseRandoms;
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

Client::Data::Tables::ItemTable ChaosItems()
{
    using Client::Data::Tables::ItemRecordSize;
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
    std::vector<std::byte> plain(Client::Data::Tables::ItemTableSize);
    for (const Shape& shape : Shapes)
    {
        const std::size_t base = std::size_t{shape.type} * ItemRecordSize;
        plain[base + 0x24] = std::byte{shape.width};
        plain[base + 0x25] = std::byte{shape.height};
        PutDword(plain, base + 0x40, shape.price);
    }
    const std::vector<std::byte> encrypted = Client::Tests::EncryptRecords(std::move(plain), ItemRecordSize);
    return Client::Data::Tables::ItemTable(std::span<const std::byte>(encrypted));
}

ItemState Item(std::uint16_t type, std::uint32_t level = 0)
{
    return ItemState{.id = type, .scalar1Low = level << 3, .occupied = 1};
}

// Una opción: su marca (D+0x2b) y su valor (D+0x33).
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
        item.optionBytes[Client::World::ItemOptionValueOffset + item.optionCount] = option.value;
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

    // Uno detrás del otro, en casillas sueltas: 0x5fd4f0 solo mira el tipo y +0x3c de cada una.
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
        auto kind = Client::Scenes::ChaosMixKind(*m_world, m_random, &m_items);
        EXPECT_TRUE(kind.HasValue()) << kind.Error();
        return kind.HasValue() ? kind.Value() : 0x7fff;
    }

    NpcWindowState& Npc()
    {
        return m_world->Runtime().npcWindows;
    }

    std::unique_ptr<WorldState> m_world;
    CrtRandom m_random{Seed};
    Client::Data::Tables::ItemTable m_items = ChaosItems();
    std::size_t m_next = 0;
};

CrtRandom Advanced(CrtRandom random, int count)
{
    random.Discard(static_cast<std::uint32_t>(count));
    return random;
}

// 0x5fd120: la espada vale lo de 0x54d0f0 en el modo 0 (su precio fijo), las joyas lo suyo, y sin la del caos, 0.
TEST_F(ChaosMixTest, TheBoxIsWorthItsItemsOnlyWithAChaosJewel)
{
    Put({Item(Sword), Item(Bless), Item(Soul), Item(Life), Item(Creation)});
    const Client::Scenes::PetPriceLevels pets{};
    auto none = Client::Scenes::ChaosBoxValue(Npc().chaosBox, &m_items, pets);
    ASSERT_TRUE(none.HasValue()) << none.Error();
    EXPECT_EQ(none.Value(), 0);
    Put({Item(Chaos)});
    auto value = Client::Scenes::ChaosBoxValue(Npc().chaosBox, &m_items, pets);
    ASSERT_TRUE(value.HasValue()) << value.Error();
    EXPECT_EQ(value.Value(), 300'000 + 100'000 + 70'000 + 450'000 + 450'000 + 40'000);
    // +0x3c en 0 (la huella de un ítem) no cuenta.
    ItemState footprint = Item(Sword);
    footprint.occupied = 0;
    Put({footprint});
    EXPECT_EQ(Client::Scenes::ChaosBoxValue(Npc().chaosBox, &m_items, pets).Value(), value.Value());
    EXPECT_FALSE(Client::Scenes::ChaosBoxValue(Npc().chaosBox, nullptr, pets).HasValue());
}

// 0x5fdac0..0x5fdb8a: el ojo y la llave del mismo nivel con la joya del caos son el 2 con el nivel + 1; distintos, el
// −2. El pergamino y el hueso, el 8 y el −8. El ancho y el alto más grandes vuelven a 1 antes de contar.
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
    // Sin la joya del caos no es ninguno.
    Clear();
    Put({Item(Eye, 3), Item(Key, 3), Item(Bless)});
    EXPECT_EQ(Kind(), 0);
    EXPECT_EQ(m_random.State(), Seed);
}

// 0x5fe3c7..0x5fe4e4: un ítem de nivel 9 a 12 con 1 a 4 joyas de Bless y de Soul y la del caos: el 3, el 4, el 0x16 y
// el 0x17. La suerte (0x54) del ítem de nivel queda en 0x7d2dc9c. Con otra cuenta, la cola o nada.
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
    // Un +10 con una sola de cada una: ni el 3 ni el 4.
    Clear();
    Put({Item(Sword, 10), Item(Bless), Item(Soul), Item(Chaos)});
    EXPECT_EQ(Kind(), 0);
}

// 0x5fe559..0x5fe5b5: solo joyas de Bless, el 0xf; solo de Soul, el 0x10; 5 y 5 con la del caos y la del guardián, el
// 0x11.
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

// 0x5fe4e9..0x5fe558: ítems de nivel 4 o más con una opción de 0x50 a 0x53 y la joya del caos, y nada más que no sea
// joya: el 1 (el arma del caos), o el 11 con un bastón 0x406, 0x806 o 0xa07. El valor de los excelentes (0x7d2dc88)
// suma su 0x54d0f0 en el modo 1.
TEST_F(ChaosMixTest, TheChaosWeaponNeedsAnOptionAndAChaosJewel)
{
    Put({WithOptions(Item(Sword, 4), {{0x51, 1}}), Item(Chaos), Item(Bless)});
    EXPECT_EQ(Kind(), 1);
    EXPECT_EQ(Npc().mixExcellentValue, static_cast<std::int32_t>(SwordPrice / 3));
    Clear();
    Put({WithOptions(Item(Staff, 5), {{0x50, 1}}), Item(Chaos)});
    EXPECT_EQ(Kind(), 11);
    // Un ítem sin la opción cuenta en los otros y no hay mezcla.
    Clear();
    Put({WithOptions(Item(Sword, 4), {{0x51, 1}}), Item(Armor, 4), Item(Chaos)});
    EXPECT_EQ(Kind(), 0);
}

// 0x5fdc31..0x5fe3c6: las alas, la pluma de nivel 0, la joya del caos y lo demás excelente son el 7, con la chance de
// 0x54d0f0 en el modo 0: 300.000 / 40.000 + 8.000.000 / 4.000.000 = 9. La opción 0x55 de las alas vale su valor · 4.
TEST_F(ChaosMixTest, TheWingsRateComesFromThePrices)
{
    Put({WithOptions(Item(Wings), {{0x55, 3}}), Item(Feather), Item(Chaos), Excellent(Item(Sword, 4))});
    EXPECT_EQ(Kind(), 7);
    EXPECT_EQ(Npc().mixWingRate, 9);
    EXPECT_EQ(Npc().mixWingOption, 12);
    EXPECT_EQ(Npc().mixWidth, 3);
    EXPECT_EQ(Npc().mixHeight, 3);
    EXPECT_EQ(m_random.State(), Seed);
    // Sin el excelente, no.
    Clear();
    Put({Item(Wings), Item(Feather), Item(Chaos), Item(Sword, 4)});
    EXPECT_EQ(Kind(), 0);
}

// 0x5fdc6f..0x5fe12e: con la pluma de nivel 1, el nivel del héroe (+0xe del registro propio, abierto y cerrado:
// 3.848 rand()): con menos de 10, el −0x18; desde 10, el 0x18 con la chance de las alas.
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

// 0x5fe3c7..0x5fe4e4: diez Uniria enteras (durabilidad 255) con la joya del caos, el 5; la de la creación con la del
// caos, el 6. Una Uniria gastada no cuenta.
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

// 0x5fdb8f..0x5fdc30: las mezclas de Fenrir (el 0x19, el 0x1a, el 0x1b) y la capa del 0x1c, 3 con un arma con opción y
// 4 con una armadura.
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

// 0x5fd4b0: el modo 0 no deja el 1, el 1 solo deja el 1 y los demás dejan todo.
TEST(ChaosMixKindForModeTest, TheModeFiltersTheChaosWeapon)
{
    using Client::Scenes::ChaosMixKindForMode;
    EXPECT_EQ(ChaosMixKindForMode(1, 0), 0);
    EXPECT_EQ(ChaosMixKindForMode(3, 0), 3);
    EXPECT_EQ(ChaosMixKindForMode(1, 1), 1);
    EXPECT_EQ(ChaosMixKindForMode(3, 1), 0);
    EXPECT_EQ(ChaosMixKindForMode(-2, 1), 0);
    EXPECT_EQ(ChaosMixKindForMode(1, 2), 1);
    EXPECT_EQ(ChaosMixKindForMode(-0x18, 2), -0x18);
}

// 0x5fedac..0x5ff0c2: sin caso propio, lo que vale / 20.000 (tope 100) y · 10.000; el 2 con la tasa de su nivel (60
// el 1, las de 0x6c9da8 del 2 al 7) y su tabla de precios; el nivel 8 lee el formato de 0x6c9dc0 y corta (P-C197).
TEST(MixQuoteTest, TheQuoteFollowsTheKind)
{
    auto npc = std::make_unique<NpcWindowState>();
    npc->chaosBoxValue = 250'000;
    npc->mixKind = 1;
    auto quote = Client::Scenes::MixQuoteOf(*npc);
    ASSERT_TRUE(quote.HasValue()) << quote.Error();
    EXPECT_EQ(quote.Value().rate, 12);
    EXPECT_EQ(quote.Value().price, 120'000u);
    EXPECT_EQ(quote.Value().priceText, "120,000");
    npc->chaosBoxValue = 3'000'000;
    EXPECT_EQ(Client::Scenes::MixQuoteOf(*npc).Value().rate, 100);
    EXPECT_EQ(Client::Scenes::MixQuoteOf(*npc).Value().priceText, "1,000,000");

    // El −0x18 deja el precio de lo que vale con la chance en 0.
    npc->mixKind = -0x18;
    EXPECT_EQ(Client::Scenes::MixQuoteOf(*npc).Value().rate, 0);
    EXPECT_EQ(Client::Scenes::MixQuoteOf(*npc).Value().price, 1'000'000u);
    npc->mixKind = -2;
    EXPECT_EQ(Client::Scenes::MixQuoteOf(*npc).Value().priceText, "0");

    npc->mixKind = 2;
    constexpr std::array<std::int32_t, 7> Rates{60, 75, 70, 65, 60, 55, 50};
    constexpr std::array<std::uint32_t, 7> Prices{100'000, 100'000, 200'000, 400'000, 700'000, 1'100'000, 1'600'000};
    for (std::int32_t level = 1; level <= 7; ++level)
    {
        SCOPED_TRACE(level);
        npc->mixLevel = level;
        auto ticket = Client::Scenes::MixQuoteOf(*npc);
        ASSERT_TRUE(ticket.HasValue()) << ticket.Error();
        EXPECT_EQ(ticket.Value().rate, Rates[static_cast<std::size_t>(level - 1)]);
        EXPECT_EQ(ticket.Value().price, Prices[static_cast<std::size_t>(level - 1)]);
    }
    npc->mixLevel = 8;
    const auto past = Client::Scenes::MixQuoteOf(*npc);
    ASSERT_FALSE(past.HasValue());
    EXPECT_NE(past.Error().find("P-C197"), std::string::npos) << past.Error();
    // Las tasas que manda el 30 con el resultado 3 pisan las de la .data.
    npc->chaosRates = {10, 20, 30, 40, 50, 60};
    npc->mixLevel = 3;
    EXPECT_EQ(Client::Scenes::MixQuoteOf(*npc).Value().rate, 20);

    npc->mixKind = 8;
    npc->mixLevel = 9;
    EXPECT_EQ(Client::Scenes::MixQuoteOf(*npc).Value().rate, 80);
    EXPECT_EQ(Client::Scenes::MixQuoteOf(*npc).Value().price, 850'000u);

    npc->mixKind = 4;
    npc->mixLuck = 1;
    EXPECT_EQ(Client::Scenes::MixQuoteOf(*npc).Value().rate, 70);
    EXPECT_EQ(Client::Scenes::MixQuoteOf(*npc).Value().priceText, "4,000,000");
    npc->mixKind = 0x17;
    npc->mixLuck = 0;
    EXPECT_EQ(Client::Scenes::MixQuoteOf(*npc).Value().rate, 45);
    EXPECT_EQ(Client::Scenes::MixQuoteOf(*npc).Value().price, 8'000'000u);

    npc->mixKind = 0x1c;
    npc->mixExcellentValue = 300'000;
    EXPECT_EQ(Client::Scenes::MixQuoteOf(*npc).Value().rate, 30);
    npc->mixExcellentValue = 1'000'000;
    EXPECT_EQ(Client::Scenes::MixQuoteOf(*npc).Value().rate, 79);
    EXPECT_EQ(Client::Scenes::MixQuoteOf(*npc).Value().price, 10'000'000u);

    npc->mixKind = 0x18;
    npc->mixWingRate = 33;
    EXPECT_EQ(Client::Scenes::MixQuoteOf(*npc).Value().rate, 33);
    EXPECT_EQ(Client::Scenes::MixQuoteOf(*npc).Value().price, 5'000'000u);
}

// 0x5fd200 y la tabla de 0x5febd4: el lugar que pide cada tipo en el inventario de 8 × 8; 0x5feb88: lo que hace el
// botón.
TEST(MixRoomTest, TheRoomAndTheButton)
{
    auto npc = std::make_unique<NpcWindowState>();
    npc->mixWidth = 2;
    npc->mixHeight = 3;
    using Client::Scenes::MixButtonAction;
    using Client::Scenes::MixRoomFor;
    EXPECT_EQ(MixRoomFor(*npc, 1).width, 5);
    EXPECT_EQ(MixRoomFor(*npc, 7).height, 4);
    EXPECT_EQ(MixRoomFor(*npc, 8).width, 2);
    EXPECT_EQ(MixRoomFor(*npc, 0xf).width, 1);
    EXPECT_EQ(MixRoomFor(*npc, 0x18).height, 3);
    EXPECT_EQ(MixRoomFor(*npc, 3).width, 2);
    EXPECT_EQ(MixRoomFor(*npc, 3).height, 3);

    std::array<ItemState, Client::Scenes::InventoryCellCount> cells{};
    for (ItemState& cell : cells)
        cell.id = 0x0002;
    EXPECT_FALSE(Client::Scenes::HasRoomFor(cells, 8, 8, 1, 1));
    EXPECT_TRUE(Client::Scenes::HasRoomFor(cells, 8, 8, 1, 0));
    // Un hueco de 2 × 3 abajo a la derecha.
    for (std::size_t row = 5; row < 8; ++row)
    {
        for (std::size_t column = 6; column < 8; ++column)
            cells[row * 8 + column].id = 0xffff;
    }
    EXPECT_TRUE(Client::Scenes::HasRoomFor(cells, 8, 8, 2, 3));
    EXPECT_FALSE(Client::Scenes::HasRoomFor(cells, 8, 8, 3, 2));
    EXPECT_FALSE(Client::Scenes::HasRoomFor(cells, 8, 8, 5, 4));

    using Client::Scenes::MixButtonActionFor;
    EXPECT_EQ(MixButtonActionFor(-0x18), MixButtonAction::LevelTooLow);
    EXPECT_EQ(MixButtonActionFor(-8), MixButtonAction::NotTheSame);
    EXPECT_EQ(MixButtonActionFor(-2), MixButtonAction::NotTheSame);
    EXPECT_EQ(MixButtonActionFor(0), MixButtonAction::NothingToMix);
    EXPECT_EQ(MixButtonActionFor(0xb), MixButtonAction::Mix);
    EXPECT_EQ(MixButtonActionFor(0x1c), MixButtonAction::Mix);
    EXPECT_EQ(MixButtonActionFor(9), MixButtonAction::Nothing);
    EXPECT_EQ(MixButtonActionFor(-1), MixButtonAction::Nothing);
}

// 0x5b1130 y 0x5b12f0: los miles sin signo y el impuesto (con signo, en 64 bits) sumado al precio.
TEST(MixQuoteTest, ThousandsAndTax)
{
    using Client::Scenes::TaxedPriceText;
    using Client::Scenes::ThousandsText;
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
