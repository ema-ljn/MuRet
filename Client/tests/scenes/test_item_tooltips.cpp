#include "Tooltips/ItemTooltips.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Tables/ItemSetTables.h"
#include "Tables/ItemTable.h"
#include "Tables/SkillTable.h"
#include "Tables/TextTable.h"
#include "Models/LogicalAccess.h"
#include "Render/SpriteBatch.h"
#include "Kernel/ActorState.h"
#include "Hud/GameHud.h"
#include "Tooltips/GameTooltips.h"
#include "Items/ItemPrice.h"
#include "Player/ItemSetState.h"
#include "Monsters/NpcNames.h"
#include "Npc/NpcWindowState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/OwnInventory.h"
#include "Social/SocialState.h"
#include "Tooltips/TooltipLines.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/ZenText.h"
#include "Ui/GameUiState.h"
#include "Ui/Widget.h"
#include "Kernel/ItemState.h"
#include "Audio/SoundId.h"
#include "support/ItemSetFiles.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"
#include "support/TableFiles.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The body of an item's tooltip (0x5b2aa0), CL8 milestone 5, with invented Text.bmd, Item.bmd, Skill.bmd and sets,
// the fake text of RecordingTextDrawer (13 high) and 640 × 480, where ry is 1. Each test opens the own record around
// it like 0x5f7bc0: its close spends 3848 rand().
namespace
{
using Client::Crt::CrtRandom;
using Client::Hud::GameHudContext;
using Client::Kernel::ItemState;
using Client::Kernel::WorldState;
using Client::Models::OwnRecordCloseRandoms;
using Client::Render::SpriteBatch;
using Client::Tooltips::TooltipLines;

constexpr Client::Core::PixelSize Screen640{640, 480};
// A Skill.bmd access that closes at 0 (0x3c bytes) and the price's IsOpen(0x10000) (a protected byte).
constexpr int SkillAccess = 60;
constexpr int NpcShopQuery = 1;
// What was in the bolds before the body: no body clears them.
constexpr std::int32_t StaleBold = 7;

// Invented types.
constexpr std::uint16_t Weapon = 0x0005;
constexpr std::uint16_t SetArmor = 0x1005;
constexpr std::uint16_t Wing = 0x1801;
constexpr std::uint16_t Jewel = 0x1c0d;
constexpr std::uint16_t BlueBox = 0x1a25;
constexpr std::uint16_t Ticket = 0x1c14;
constexpr std::uint16_t Pet = 0x1a04;
// The Chaos Castle one (0x5b63b7): the type is the original's, like the gem's 0x1c0d.
constexpr std::uint16_t ChaosCastleArmor = 0x1a1d;
constexpr std::uint16_t LostMap = 0x1c1c;
constexpr std::uint8_t ArmorSet = 3;
constexpr std::uint8_t BlueBoxSkill = 0x4c;
constexpr std::uint16_t BlueBoxMana = 85;

// The hero's classes: & 7, and the evolution at >> 3.
constexpr std::uint8_t Wizard = 0;
constexpr std::uint8_t Knight = 1;
constexpr std::uint8_t SecondElf = 2 | (1 << 3);
constexpr std::uint8_t Gladiator = 3;

Client::Tables::TextTable Texts()
{
    return Client::Tests::MakeTextTable({{20, "Clase A"},
                                         {21, "Clase B"},
                                         {22, "Clase C"},
                                         {23, "Clase D"},
                                         {24, "Clase E"},
                                         {27, "Clase C2"},
                                         {40, "Golpe"},
                                         {60, "No: %s"},
                                         {63, "Precio %s"},
                                         {61, "Si: %s"},
                                         {64, "Ritmo %d"},
                                         {65, "Aguante %d"},
                                         {71, "Vida util %d/%d"},
                                         {73, "Fuerza pedida %d"},
                                         {74, "Faltan %d"},
                                         {75, "Agilidad pedida %d"},
                                         {76, "Nivel pedido %d"},
                                         {87, "Vida extra"},
                                         {88, "Opcion +%d"},
                                         {94, "Vida %d"},
                                         {103, "Anillo"},
                                         {159, "fin"},
                                         {58, "Kalima"},
                                         {368, "Nivel"},
                                         {238, "Reparar: %s"},
                                         {572, "Gema para mejorar"},
                                         {577, "Golpe extra %d"},
                                         {578, "Absorbe %d"},
                                         {579, "Cola"},
                                         {620, "Brillante"},
                                         {629, "Opcion leve"},
                                         {631, "Opcion fija"},
                                         {632, "Stat D +%d"},
                                         {935, "Minimo"},
                                         {936, "Valor"},
                                         {950, "Stat A +%d"},
                                         {951, "Stat B +%d"},
                                         {970, "Stat C %d"},
                                         {423, "Te falta zen"},
                                         {1089, "Grupo"},
                                         {1101, "Sin precio"},
                                         {1147, "Chaos Castle"},
                                         {1123, "Desde el nivel %d"},
                                         {1157, "Clic para entrar"},
                                         {1184, "Tira la piedra"},
                                         {1620, "Compra: %s(%s)"},
                                         {1860, "Suerte %d"},
                                         {1863, "Sello uno"},
                                         {1920, "Abrir"},
                                         {1928, "Coste %d"}});
}

// An Item.bmd record in clear.
class ItemRecords
{
public:
    ItemRecords() : m_plain(Client::Tables::ItemTableSize) {}

    ItemRecords& Named(std::uint16_t type, std::string_view name)
    {
        for (std::size_t i = 0; i < name.size(); ++i)
            At(type, i) = static_cast<std::byte>(name[i]);
        return *this;
    }

    ItemRecords& Byte(std::uint16_t type, std::size_t offset, std::uint8_t value)
    {
        At(type, offset) = std::byte{value};
        return *this;
    }

    [[nodiscard]] Client::Tables::ItemTable Table() const
    {
        const std::vector<std::byte> encrypted = Client::Tests::EncryptRecords(m_plain, Client::Tables::ItemRecordSize);
        return Client::Tables::ItemTable(std::span<const std::byte>(encrypted));
    }

private:
    std::byte& At(std::uint16_t type, std::size_t offset)
    {
        return m_plain[std::size_t{type} * Client::Tables::ItemRecordSize + offset];
    }

    std::vector<std::byte> m_plain;
};

Client::Tables::ItemTable Items()
{
    return ItemRecords()
        // A one-handed weapon for classes B and D, with its speed and its durability.
        .Named(Weapon, "Hoja Gris")
        .Byte(Weapon, 0x2b, 20)
        .Byte(Weapon, 0x2d, 22)
        .Byte(Weapon, 0x25, 3)
        .Byte(Weapon, 0x46, 1)
        .Byte(Weapon, 0x48, 1)
        // A set armor of class B, with set stat 2.
        .Named(SetArmor, "Coraza")
        .Byte(SetArmor, 0x2d, 40)
        .Byte(SetArmor, 0x44, 2)
        .Byte(SetArmor, 0x46, 1)
        // A wing of classes A and C (C from its evolution 1).
        .Named(Wing, "Ala Leve")
        .Byte(Wing, 0x2d, 50)
        .Byte(Wing, 0x45, 1)
        .Byte(Wing, 0x47, 2)
        .Named(Jewel, "Gema Uno")
        .Byte(Jewel, 0x25, 2)
        .Named(BlueBox, "Caja Azul")
        .Named(Ticket, "Boleto")
        .Named(Pet, "Mascota")
        // With class A marked: the body does not write the classes of 0x1a1d (0x5b79c7).
        .Named(LostMap, "Mapa")
        .Named(ChaosCastleArmor, "Armadura")
        .Byte(ChaosCastleArmor, 0x45, 1)
        .Table();
}

// Skill.bmd in clear, with the mana (+0x22) of the blue box's skill.
Client::Tables::SkillTable Skills()
{
    std::vector<std::byte> plain(Client::Tables::SkillTableSize);
    const std::size_t mana = std::size_t{BlueBoxSkill} * Client::Tables::SkillRecordSize + 0x22;
    plain[mana] = std::byte{BlueBoxMana & 0xff};
    plain[mana + 1] = std::byte{BlueBoxMana >> 8};
    const std::vector<std::byte> encrypted =
        Client::Tests::EncryptRecords(std::move(plain), Client::Tables::SkillRecordSize);
    return Client::Tables::SkillTable(std::span<const std::byte>(encrypted));
}

// ItemSetType.bmd with set 3 in the armor's piece 1, and ItemSetOption.bmd with set 3: two pairs (the second with a
// single option) and one full-set option; everything else unused (0xff).
Client::Tables::ItemSetTables Sets()
{
    std::vector<std::byte> types(Client::Tables::ItemSetTypeTableSize);
    types[std::size_t{SetArmor} * Client::Tables::ItemSetTypeRecordSize] = std::byte{ArmorSet};
    std::vector<std::byte> options(Client::Tables::ItemSetOptionTableSize, std::byte{0xff});
    const std::size_t base = std::size_t{ArmorSet} * Client::Tables::ItemSetOptionRecordSize;
    const std::string_view name = "Conjunto Gris";
    for (std::size_t i = 0; i < name.size(); ++i)
        options[base + i] = static_cast<std::byte>(name[i]);
    options[base + name.size()] = std::byte{0};
    const auto set = [&](std::size_t offset, std::uint8_t value) { options[base + offset] = std::byte{value}; };
    set(0x3c, 5);
    set(0x20, 0);
    set(0x21, 1);
    set(0x2c, 10);
    set(0x2d, 15);
    set(0x22, 0x15);
    set(0x2e, 5);
    set(0x3d, 7);
    set(0x42, 20);
    return Client::Tests::ItemSetTablesFrom(std::move(types), std::move(options));
}

struct Line
{
    std::string text;
    std::int32_t color = 0;
    std::int32_t bold = 0;
};

class ItemTooltipsTest : public ::testing::Test
{
protected:
    ItemTooltipsTest()
    {
        sprites.Begin(Screen640);
        Client::Kernel::OwnCharacterRecord& own = world.OwnCharacter();
        own.level = 200;
        own.strength = 80;
        own.agility = 30;
        own.word2c = 5;
        for (std::int32_t& bold : Lines().bold)
            bold = StaleBold;
        HeroIs(Knight);
    }

    GameHudContext Context()
    {
        Client::Kernel::WorldRuntime& runtime = world.Runtime();
        Client::Kernel::ActorState& hero = world.Hero();
        return GameHudContext{.world = world,
                              .random = random,
                              .ownRecordDepth = runtime.ownRecordDepth,
                              .cellXDepth = hero.cellXAccesses,
                              .cellYDepth = hero.cellYAccesses,
                              .hud = runtime.hud,
                              .crywolfSandOverlay = runtime.crywolf.sandOverlay,
                              .sound = sound,
                              .projecting = projecting,
                              .sprites = sprites,
                              .text = text,
                              .texts = &texts,
                              .items = &items,
                              .skills = &skills,
                              .setOptions = withSets ? &sets.options : nullptr,
                              .setTypes = withSets ? &sets.types : nullptr,
                              .mouse = mouse};
    }

    // 0x5f7bc0: the body with the own record open around it.
    Client::Core::Expected<void, std::string> Draw(ItemState item, std::int32_t x = 320, std::int32_t y = 300,
                                                   std::uint8_t grid = 0)
    {
        GameHudContext context = Context();
        const Client::Models::LogicalAccess own(world.Runtime().ownRecordDepth, random, OwnRecordCloseRandoms);
        return Client::Tooltips::DrawTheItemTooltip(
            Client::Tooltips::ItemTooltipContext{.hud = context, .x = x, .y = y, .item = item, .grid = grid});
    }

    void HeroIs(std::uint8_t characterClass)
    {
        world.Hero().characterClass = characterClass;
    }

    TooltipLines& Lines()
    {
        return world.Runtime().hud.tooltip;
    }

    void ExpectLines(const std::vector<Line>& expected, std::int32_t halves)
    {
        TooltipLines& lines = Lines();
        ASSERT_EQ(lines.next, static_cast<std::int32_t>(expected.size()));
        EXPECT_EQ(lines.halves, halves);
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            EXPECT_EQ(lines.text[i], expected[i].text) << "renglón " << i;
            EXPECT_EQ(lines.colors[i], expected[i].color) << "renglón " << i;
            EXPECT_EQ(lines.bold[i], expected[i].bold) << "renglón " << i;
        }
        for (std::size_t i = expected.size(); i < Client::Tooltips::TooltipLineCount; ++i)
            EXPECT_TRUE(lines.text[i].empty()) << "renglón " << i;
    }

    Client::Core::Expected<void, std::string> Repair(ItemState item, std::int32_t x = 320, std::int32_t y = 300)
    {
        GameHudContext context = Context();
        const Client::Models::LogicalAccess own(world.Runtime().ownRecordDepth, random, OwnRecordCloseRandoms);
        return Client::Tooltips::DrawTheRepairTooltip(
            Client::Tooltips::ItemTooltipContext{.hud = context, .x = x, .y = y, .item = item});
    }

    static CrtRandom Advanced(CrtRandom from, int count)
    {
        for (int i = 0; i < count; ++i)
            static_cast<void>(from.Next());
        return from;
    }

    // What the box drew, without the "\n".
    std::vector<std::string> DrawnTexts() const
    {
        std::vector<std::string> drawn;
        for (const auto& recorded : text.texts)
            drawn.push_back(recorded.text);
        return drawn;
    }

    WorldState world;
    CrtRandom random;
    std::optional<Client::Audio::SoundId> sound;
    SpriteBatch sprites;
    Client::Tests::RecordingTextDrawer text{sprites};
    Client::Tables::TextTable texts = Texts();
    Client::Tables::ItemTable items = Items();
    Client::Tables::SkillTable skills = Skills();
    Client::Tables::ItemSetTables sets = Sets();
    bool withSets = true;
    bool projecting = false;
    Client::Ui::Point mouse{};
};

// The excellent +9 weapon with two options: 0x50 (with its value) and 0x5f (without arguments).
ItemState ExcellentWeapon()
{
    ItemState item;
    item.id = Weapon;
    item.scalar1Low = 9u << 3u;
    item.word0c = 20;
    item.word0e = 28;
    item.durability = 25;
    item.word1e = 60;
    item.word20 = 50;
    item.flags1b = 0x02;
    item.optionCount = 2;
    item.optionBytes[0] = 0x50;
    item.optionBytes[1] = 0x5f;
    item.optionBytes[Client::Kernel::ItemOptionValueOffset] = 4;
    return item;
}

TEST_F(ItemTooltipsTest, AnExcellentWeaponListsItsLinesForAKnightOfItsClass)
{
    const CrtRandom expected = Advanced(random, OwnRecordCloseRandoms + NpcShopQuery + SkillAccess);

    const auto drawn = Draw(ExcellentWeapon());

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    // 0x5b1ba0: 22 + 4 · 1 + 5 · 2 from the levels + 15 from the excellent.
    ExpectLines({{"\n", 0, StaleBold},
                 {"Brillante Hoja Gris +9", 4, 1},
                 {"\n", 0, StaleBold},
                 {"Golpe: 20 ~ 28", 1, 0},
                 {"Ritmo 20", 0, 0},
                 {"Vida util 25/51", 0, 0},
                 {"Fuerza pedida 60", 0, 0},
                 {"Agilidad pedida 50", 2, 0},
                 {"Faltan 15", 2, 0},
                 {"\n", 0, StaleBold},
                 {"Si: Clase B", 0, 0},
                 {"Si: Clase D", 0, 0},
                 {"\n", 0, StaleBold},
                 {"Opcion +4", 1, 0},
                 {"Opcion fija", 1, 0},
                 {"\n", 0, StaleBold}},
                5);
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
    EXPECT_EQ(world.Runtime().movement.skillDepths[0], 0);
    // The box draws the lines that are not "\n", up to the first empty one.
    EXPECT_EQ(DrawnTexts(),
              (std::vector<std::string>{"Brillante Hoja Gris +9", "Golpe: 20 ~ 28", "Ritmo 20", "Vida util 25/51",
                                        "Fuerza pedida 60", "Agilidad pedida 50", "Faltan 15", "Si: Clase B",
                                        "Si: Clase D", "Opcion +4", "Opcion fija"}));
}

TEST_F(ItemTooltipsTest, TheSameWeaponMarksTheClassesAndTheStrengthOfAWizard)
{
    HeroIs(Wizard);
    world.OwnCharacter().strength = 20;

    const auto drawn = Draw(ExcellentWeapon());

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    // Its class does not use it (Item.bmd+0x45 at 0): what it does not meet goes in color 5.
    ExpectLines({{"\n", 0, StaleBold},
                 {"Brillante Hoja Gris +9", 4, 1},
                 {"\n", 0, StaleBold},
                 {"Golpe: 20 ~ 28", 1, 0},
                 {"Ritmo 20", 0, 0},
                 {"Vida util 25/51", 0, 0},
                 {"Fuerza pedida 60", 2, 0},
                 {"Faltan 40", 2, 0},
                 {"Agilidad pedida 50", 2, 0},
                 {"Faltan 15", 2, 0},
                 {"\n", 0, StaleBold},
                 {"Si: Clase B", 5, 0},
                 {"Si: Clase D", 5, 0},
                 {"\n", 0, StaleBold},
                 {"Opcion +4", 1, 0},
                 {"Opcion fija", 1, 0},
                 {"\n", 0, StaleBold}},
                5);
}

TEST_F(ItemTooltipsTest, AGladiatorOnlyLooksAtItsOwnClassByte)
{
    HeroIs(Gladiator);

    const auto drawn = Draw(ExcellentWeapon());

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    const TooltipLines& lines = Lines();
    ASSERT_EQ(lines.next, 15);
    EXPECT_EQ(lines.text[9], "\n");
    EXPECT_EQ(lines.text[10], "Si: Clase D");
    EXPECT_EQ(lines.colors[10], 0);
    EXPECT_EQ(lines.text[11], "\n");
}

TEST_F(ItemTooltipsTest, ASetArmorListsTheStatAndTheOptionsOfItsSet)
{
    Client::Player::ItemSetState& state = world.ItemSets();
    const std::string_view name = "Conjunto Gris";
    for (std::size_t i = 0; i < name.size(); ++i)
        state.names[0][i] = name[i];
    // The hero wears the two options of the first pair, in that order.
    state.list.count9348 = 2;
    state.list.entries[0] = {0, 0, 0};
    state.list.entries[1] = {1, 0, 0};
    ItemState item;
    item.id = SetArmor;
    item.word12 = 30;
    item.durability = 40;
    item.word1e = 50;
    item.flags1c = 1 | (2 << 2);
    const CrtRandom expected = Advanced(random, OwnRecordCloseRandoms + NpcShopQuery + SkillAccess);

    const auto drawn = Draw(item);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    // The name carries the set's in front (0x40cf30) and 9; the durability, 20 more for the set. The set's stat
    // (0x40dd80) and the lines of 0x40eee0 do not write the bold.
    ExpectLines({{"\n", 0, StaleBold},         {"Conjunto Gris Coraza", 9, 1},
                 {"\n", 0, StaleBold},         {"Aguante 30", 0, 0},
                 {"Vida util 40/60", 0, 0},    {"Fuerza pedida 50", 0, 0},
                 {"\n", 0, StaleBold},         {"Si: Clase B", 0, 0},
                 {"Stat B +10", 1, StaleBold}, {"\n", 0, StaleBold},
                 {"\n", 0, StaleBold},         {"Grupo fin", 3, StaleBold},
                 {"\n", 0, StaleBold},         {"\n", 0, StaleBold},
                 {"Stat A +10", 1, 0},         {"Stat B +15", 1, 0},
                 {"Stat C 5", 10, 0},          {"Stat D +20", 10, 0},
                 {"\n", 0, StaleBold},         {"\n", 0, StaleBold}},
                4);
    EXPECT_EQ(state.byte9306, ArmorSet);
    EXPECT_EQ(random.State(), expected.State());
}

TEST_F(ItemTooltipsTest, AWingOfASecondClassShowsItsBonusesAndItsLevel)
{
    HeroIs(SecondElf);
    ItemState item;
    item.id = Wing;
    item.scalar1Low = 3u << 3u;
    item.durability = 45;
    item.word28 = 180;
    item.optionCount = 1;
    item.optionBytes[0] = 0x5d;

    const auto drawn = Draw(item);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    // The wings always show the durability: 50 + 3 from the levels. Class C asks for evolution 1 (byte 2: the name is
    // 15 + 5 · 2 + 2).
    ExpectLines({{"\n", 0, StaleBold},
                 {"Ala Leve +3", 1, 1},
                 {"\n", 0, StaleBold},
                 {"Golpe extra 18", 0, 0},
                 {"Absorbe 18", 0, 0},
                 {"Cola", 0, 0},
                 {"Vida util 45/53", 0, 0},
                 {"Nivel pedido 180", 0, 0},
                 {"\n", 0, StaleBold},
                 {"Si: Clase A", 0, 0},
                 {"Si: Clase C2", 0, 0},
                 {"\n", 0, StaleBold},
                 {"Opcion leve", 1, 0},
                 {"\n", 0, StaleBold}},
                5);
}

TEST_F(ItemTooltipsTest, AJewelHasItsNoteAndNoDurability)
{
    ItemState item;
    item.id = Jewel;
    item.durability = 1;
    const CrtRandom expected = Advanced(random, OwnRecordCloseRandoms + NpcShopQuery + SkillAccess);

    const auto drawn = Draw(item);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    ExpectLines({{"\n", 0, StaleBold},
                 {"Gema Uno", 3, 1},
                 {"\n", 0, StaleBold},
                 {"Gema para mejorar", 0, 0},
                 {"\n", 0, StaleBold}},
                3);
    EXPECT_EQ(random.State(), expected.State());
}

// 0x5b63b7..0x5b69e7: 0x1a1d writes the Chaos Castle table of 0x6ba598, the one of the hero's class, with its level's
// line in color 8. The texts are the Text.bmd ones; the formats, those of 0x6c9b80 and 0x6c9b98.
constexpr std::string_view ChaosCastleHeader = "Chaos Castle Nivel    Minimo      Valor    ";
constexpr std::array<std::string_view, 6> ChaosCastleRows{
    "        1              15~ 49      25,000", "        2              50~119      80,000",
    "        3             120~179     150,000", "        4             180~239     250,000",
    "        5             240~299     400,000", "        6             300~400     650,000"};
// Classes 3 and 4 read from the seventh row: the same prices.
constexpr std::array<std::string_view, 6> ChaosCastleRowsOfClasses3And4{
    "        1              15~ 29      25,000", "        2              30~ 99      80,000",
    "        3             100~159     150,000", "        4             160~219     250,000",
    "        5             220~279     400,000", "        6             280~400     650,000"};
constexpr std::int32_t HighlightedRow = 8;
// The table's header line: after the "\n", the name and its "\n".
constexpr std::size_t ChaosCastleHeaderLine = 3;
constexpr std::size_t FirstChaosCastleRow = ChaosCastleHeaderLine + 1;

ItemState ChaosCastleItem()
{
    ItemState item;
    item.id = ChaosCastleArmor;
    item.durability = 1;
    return item;
}

// The whole body: the table, the "\n" (without color or bold), 1157 in color 7 without writing the bold and the "\n" of
// 0x5b7da7. Without the durability (0x5b6ab5) or the classes.
std::vector<Line> ChaosCastleLines(const std::array<std::string_view, 6>& rows, std::optional<std::size_t> highlighted)
{
    std::vector<Line> lines{{"\n", 0, StaleBold}, {"Armadura", 0, 1}, {"\n", 0, StaleBold}};
    lines.push_back({std::string(ChaosCastleHeader), 0, 0});
    for (std::size_t i = 0; i < rows.size(); ++i)
        lines.push_back({std::string(rows[i]), highlighted == i ? HighlightedRow : 0, 0});
    lines.push_back({"\n", 0, StaleBold});
    lines.push_back({"Clic para entrar", Client::Tooltips::TooltipWhiteOnRed, StaleBold});
    lines.push_back({"\n", 0, StaleBold});
    return lines;
}

TEST_F(ItemTooltipsTest, TheChaosCastleArmorListsTheLevelsAndMarksTheOneOfTheHero)
{
    world.OwnCharacter().level = 110;
    const CrtRandom expected = Advanced(random, OwnRecordCloseRandoms + NpcShopQuery + SkillAccess);

    const auto drawn = Draw(ChaosCastleItem());

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    ExpectLines(ChaosCastleLines(ChaosCastleRows, 1), 4);
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
}

// 0x5b63c0..0x5b63d0: the hero's class (+0x1d4 & 7), without the evolution.
TEST_F(ItemTooltipsTest, TheChaosCastleArmorReadsItsOwnTableForClasses3And4)
{
    world.OwnCharacter().level = 110;
    for (const std::uint8_t heroClass : {Gladiator, std::uint8_t{Gladiator | (1 << 3)}, std::uint8_t{4}})
    {
        HeroIs(heroClass);

        const auto drawn = Draw(ChaosCastleItem());

        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        ExpectLines(ChaosCastleLines(ChaosCastleRowsOfClasses3And4, 2), 4);
    }
}

// 0x437d49..0x4381e0: the Lost Map (0x1c1c) in 0x437c90, which 0x5b6a2f calls with the line and keeps the one it
// returns in 0x78abb30 without touching 0x78abb34: its "\n" are not half lines. It opens the own record to read the
// level (+0xe) and closes it; the header "%s %s       %s    " (0x6c7468) with 58, 368 and 935, in color 0 and without
// bold; Kalima's seven rows from 0x6b8b18 (the maximum written up to 400, JLE) with
// "        %d             %3d~%3d     " (0x6c7444), the map level's one in color 8; a "\n" without color or bold;
// 1184 in color 7, without writing the bold; and, with the own level below the first row's minimum (JGE), another
// "\n" and 1123 with that minimum in color 5.
constexpr std::string_view KalimaHeader = "Kalima Nivel       Minimo    ";
constexpr std::array<std::string_view, 7> KalimaRows{
    "        1              40~400     ", "        2             131~400     ", "        3             181~400     ",
    "        4             231~400     ", "        5             281~400     ", "        6             331~400     ",
    "        7             350~400     "};
// 0x437d14..0x437d26: classes 3 and 4 (+0x1d4 & 7) read from the eighth row ([EBP − 4] at 7).
constexpr std::array<std::string_view, 7> KalimaRowsOfClasses3And4{
    "        1              20~400     ", "        2             111~400     ", "        3             161~400     ",
    "        4             211~400     ", "        5             261~400     ", "        6             311~400     ",
    "        7             350~400     "};

ItemState LostMapItem(std::int32_t level)
{
    ItemState item;
    item.id = LostMap;
    item.scalar1Low = static_cast<std::uint32_t>(level) << 3u;
    return item;
}

// The whole body. The name, from 0x5b42cd: "%s +%d" (0x6c9a68) with the name and the level, in gold ([EBP − 0x1c] at
// 3). Then, the lines of 0x437c90 and the "\n" of 0x5b7da7, without the durability (0x5b6ab5) or the classes. Half
// lines: only the first one, the one after the name and the last.
std::vector<Line> KalimaLines(const std::array<std::string_view, 7>& rows, std::int32_t mapLevel,
                              std::optional<std::string> warning)
{
    std::vector<Line> lines{{"\n", 0, StaleBold},
                            {"Mapa +" + std::to_string(mapLevel), Client::Tooltips::TooltipGold, 1},
                            {"\n", 0, StaleBold}};
    lines.push_back({std::string(KalimaHeader), 0, 0});
    for (std::size_t i = 0; i < rows.size(); ++i)
        lines.push_back({std::string(rows[i]), static_cast<std::int32_t>(i + 1) == mapLevel ? HighlightedRow : 0, 0});
    lines.push_back({"\n", 0, StaleBold});
    lines.push_back({"Tira la piedra", Client::Tooltips::TooltipWhiteOnRed, StaleBold});
    if (warning)
    {
        lines.push_back({"\n", 0, StaleBold});
        lines.push_back({*warning, Client::Tooltips::TooltipWhiteOnBlue, StaleBold});
    }
    lines.push_back({"\n", 0, StaleBold});
    return lines;
}

TEST_F(ItemTooltipsTest, TheLostMapListsTheKalimasAndMarksTheOneOfItsLevel)
{
    world.OwnCharacter().level = 100;
    const CrtRandom expected = Advanced(random, OwnRecordCloseRandoms + NpcShopQuery + SkillAccess);

    const auto drawn = Draw(LostMapItem(2));

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    ExpectLines(KalimaLines(KalimaRows, 2, std::nullopt), 3);
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
}

// 0x43818b..0x4381d8: with the own level below the minimum of its table's first row (40, or 20 with classes 3 and 4),
// the "\n" and 1123 with that minimum in color 5; at the minimum, no.
TEST_F(ItemTooltipsTest, TheLostMapWarnsBelowTheFirstKalima)
{
    world.OwnCharacter().level = 39;
    ASSERT_TRUE(Draw(LostMapItem(1)).HasValue());
    ExpectLines(KalimaLines(KalimaRows, 1, "Desde el nivel 40"), 3);

    world.OwnCharacter().level = 40;
    ASSERT_TRUE(Draw(LostMapItem(1)).HasValue());
    ExpectLines(KalimaLines(KalimaRows, 1, std::nullopt), 3);

    HeroIs(Gladiator);
    world.OwnCharacter().level = 19;
    ASSERT_TRUE(Draw(LostMapItem(7)).HasValue());
    ExpectLines(KalimaLines(KalimaRowsOfClasses3And4, 7, "Desde el nivel 20"), 3);
}

// 0x4380fd: the map's level against the row number; with 0 it marks none.
TEST_F(ItemTooltipsTest, TheLostMapOfLevelZeroMarksNoRow)
{
    world.OwnCharacter().level = 100;
    ASSERT_TRUE(Draw(LostMapItem(0)).HasValue());
    ExpectLines(KalimaLines(KalimaRows, 0, std::nullopt), 3);
}

// 0x5b690a..0x5b691d: the level against the minimum and the maximum, both included; outside the table, none.
TEST_F(ItemTooltipsTest, TheChaosCastleArmorMarksTheRowOfTheLevelWithItsEdges)
{
    struct LevelCase
    {
        std::uint16_t level = 0;
        std::optional<std::size_t> row;
    };
    const std::array<LevelCase, 7> cases{
        {{14, std::nullopt}, {15, 0}, {49, 0}, {50, 1}, {300, 5}, {400, 5}, {401, std::nullopt}}};
    for (const auto& [level, row] : cases)
    {
        world.OwnCharacter().level = level;

        const auto drawn = Draw(ChaosCastleItem());

        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        for (std::size_t i = 0; i < ChaosCastleRows.size(); ++i)
            EXPECT_EQ(Lines().colors[FirstChaosCastleRow + i], row == i ? HighlightedRow : 0)
                << "nivel " << level << ", fila " << i;
    }
}

// 0x5b63d7..0x5b6871: opens the own record to read the level (+0xe) and closes it. Without the access of 0x5f7bc0
// around it, the close of 0x1a1d leaves the count at 0 and spends its 3848 rand().
TEST_F(ItemTooltipsTest, TheChaosCastleArmorOpensTheOwnRecordForTheLevel)
{
    world.OwnCharacter().level = 110;
    const CrtRandom expected = Advanced(random, OwnRecordCloseRandoms + NpcShopQuery + SkillAccess);
    GameHudContext context = Context();
    ItemState item = ChaosCastleItem();

    const auto drawn = Client::Tooltips::DrawTheItemTooltip(
        Client::Tooltips::ItemTooltipContext{.hud = context, .x = 320, .y = 300, .item = item});

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(Lines().colors[FirstChaosCastleRow + 1], HighlightedRow);
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
}

// 0x5b3824..0x5b3852: the level 2 ticket (0x1c0c), sprintf(line, "%s" (0x6c99e4), 0x6c9350 + D+0x1a · 100): the prize
// by the item's durability, not by Item.bmd+0x2d like 0x5b1c70. 7 is "뮤 머그컵", with its EUC-KR bytes. From 10 it
// reads the .data that follows the ten prizes, which MuRet does not have: it cuts.
TEST_F(ItemTooltipsTest, TheTicketOfLevelTwoNamesThePrizeOfItsDurability)
{
    ItemState ticket;
    ticket.id = 0x1c0c;
    ticket.scalar1Low = 2u << 3u;
    ticket.durability = 7;

    const auto drawn = Draw(ticket);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(Lines().text[1], "\xb9\xc2 \xb8\xd3\xb1\xd7\xc4\xc5");

    ticket.durability = 10;
    const auto outside = Draw(ticket);
    ASSERT_FALSE(outside.HasValue());
    EXPECT_NE(outside.Error().find("0x5b3835"), std::string::npos) << outside.Error();
}

// 0x5b41d0..0x5b421a: the title of the transformation ring (0x1a0a): the level's monster in 0x6c9338 and the first
// record of 0x7681eb8 with that type (it cuts at the first), "%s %s" with its name and 103. With none (level 2's 14),
// it jumps to 0x5b459d without writing the line, which 0x5b2aa0 emptied at the start.
TEST_F(ItemTooltipsTest, TheTransformationRingNamesItsFirstMonster)
{
    Client::Monsters::NpcNameTable& names = world.Runtime().npcNames;
    names[3].type = 7;
    std::ranges::copy(std::string_view("Gigante A"), names[3].name.begin());
    names[10].type = 7;
    std::ranges::copy(std::string_view("Gigante B"), names[10].name.begin());
    ItemState ring;
    ring.id = 0x1a0a;
    ring.scalar1Low = 1u << 3u;

    const auto drawn = Draw(ring);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(Lines().text[1], "Gigante A Anillo");

    ring.scalar1Low = 2u << 3u;
    const auto unnamed = Draw(ring);
    ASSERT_TRUE(unnamed.HasValue()) << unnamed.Error();
    EXPECT_EQ(Lines().text[1], "");
}

TEST_F(ItemTooltipsTest, TheTooltipsDrawTheBodyOfTheHoveredItemWithTheOwnRecordOpen)
{
    ItemState& cell = world.Inventory().cells[3];
    cell.id = Jewel;
    Client::Ui::GameUiState& ui = world.Runtime().ui;
    ui.hoveredItem = Client::Ui::HoveredItem{.place = Client::Ui::HoveredItem::Place::InventoryCell, .index = 3};
    ui.skillTooltipX = 320;
    ui.skillTooltipY = 300;
    const CrtRandom expected = Advanced(random, OwnRecordCloseRandoms + NpcShopQuery + SkillAccess);
    GameHudContext context = Context();

    const auto drawn = Client::Tooltips::DrawTheTooltips(context);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(DrawnTexts(), (std::vector<std::string>{"Gema Uno", "Gema para mejorar"}));
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
}

TEST_F(ItemTooltipsTest, TheBoxGoesAboveTheItemOrBelowItWithoutRoom)
{
    ItemState item;
    item.id = Jewel;

    ASSERT_TRUE(Draw(item, 320, 300).HasValue());
    // 0x5b8043: (5 − 3) · 13 + 3 · 13 / 2 = 45 above y; the name goes half a line (6.5 · 1.1) lower.
    ASSERT_FALSE(text.texts.empty());
    EXPECT_EQ(text.texts[0].y, 300 - 45 + 7);

    text.texts.clear();
    ASSERT_TRUE(Draw(item, 320, 20).HasValue());
    // No room above: below, at its Item.bmd height (2) times 20.
    ASSERT_FALSE(text.texts.empty());
    EXPECT_EQ(text.texts[0].y, 20 + 2 * 20 + 7);
}

TEST_F(ItemTooltipsTest, TheBlueBoxReadsTheManaOfItsSkillAndCountsItsLineAsAHalf)
{
    ItemState item;
    item.id = BlueBox;
    item.flags1b = 1;
    const CrtRandom expected = Advanced(random, OwnRecordCloseRandoms + NpcShopQuery + 2 * SkillAccess);

    const auto drawn = Draw(item);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    ExpectLines({{"\n", 0, StaleBold},
                 {"Caja Azul Sello uno", 1, 1},
                 {"\n", 0, StaleBold},
                 {"\n", 0, StaleBold},
                 {"Coste 85", 1, 0},
                 {"Suerte 10", 1, StaleBold},
                 {"Cola", 1, StaleBold},
                 {"\n", 0, StaleBold},
                 {"Abrir", 3, StaleBold}},
                5);
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(world.Runtime().movement.skillDepths[BlueBoxSkill], 0);
}

TEST_F(ItemTooltipsTest, TheTicketKeepsItsTooltipWithTheNpcShopOpen)
{
    world.Runtime().panels.npcShop = 1;
    ItemState item;
    item.id = Ticket;
    item.scalar1Low = 1u << 3u;

    const auto drawn = Draw(item);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(Lines().text[1], "Boleto +1");
}

// 0x5b2d23..0x5b2e41: from the NPC shop grid (0x78aba27), 1620 with the price of 0x54d0f0 in mode 0 with the tax
// (0x5b1200: 3 %, 64 bits and signed division) and without it, with the name's color and without writing the bold, and
// a "\n" after. In Lorencia, Devias and Noria.
TEST_F(ItemTooltipsTest, TheNpcShopGridShowsTheBuyingPriceWithTheTax)
{
    for (const std::int32_t map : {0, 2, 3})
    {
        SCOPED_TRACE(map);
        world.Runtime().map = map;
        world.Runtime().panels.npcShop = 1;
        world.Runtime().npcWindows.taxRate = 3;
        const ItemState item = ExcellentWeapon();
        const auto price = Client::Items::ItemPrice(item, items, Client::Items::ShopGridPrice, std::nullopt);
        ASSERT_TRUE(price.HasValue()) << price.Error();
        const std::uint32_t taxed = price.Value() + static_cast<std::uint32_t>(std::int64_t{3} * price.Value() / 100);
        const CrtRandom expected = Advanced(random, OwnRecordCloseRandoms + NpcShopQuery + SkillAccess);

        const auto drawn = Draw(item, 320, 300, 1);

        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_EQ(random.State(), expected.State());
        const TooltipLines& lines = Lines();
        EXPECT_EQ(lines.text[0], "\n");
        EXPECT_EQ(lines.text[1],
                  "Compra: " + Client::Ui::FormatZen(taxed) + "(" + Client::Ui::FormatZen(price.Value()) + ")");
        EXPECT_EQ(lines.colors[1], 4);
        EXPECT_EQ(lines.bold[1], StaleBold);
        EXPECT_EQ(lines.text[2], "\n");
        EXPECT_EQ(lines.text[3], "Brillante Hoja Gris +9");
        EXPECT_EQ(lines.next, 18);
        EXPECT_EQ(lines.halves, 6);
    }
}

// 0x5b2e46..0x5b2f34: from the inventory with the shop open, 63 with the sell price (mode 1); without the tax. A price
// of 1000 or more carries the commas of 0x5b1130.
TEST_F(ItemTooltipsTest, TheInventoryShowsTheSellingPriceWithTheShopOpen)
{
    world.Runtime().panels.npcShop = 1;
    world.Runtime().npcWindows.taxRate = 50;
    const ItemState item = ExcellentWeapon();
    const auto price = Client::Items::ItemPrice(item, items, Client::Items::InventoryPrice, std::nullopt);
    ASSERT_TRUE(price.HasValue()) << price.Error();
    ASSERT_GE(price.Value(), 1000u);

    const auto drawn = Draw(item);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(Lines().text[1], "Precio " + Client::Ui::FormatZen(price.Value()));
    EXPECT_NE(Lines().text[1].find(','), std::string::npos);
    EXPECT_EQ(Lines().colors[1], 4);
    EXPECT_EQ(Lines().text[2], "\n");
}

// 0x5b2d03: the lucky box (0x1c0b) shows no price with the shop open: the tooltip and the rand() are the same as with
// the shop closed (the IsOpen(0x10000) draws its own anyway).
TEST_F(ItemTooltipsTest, TheLuckyBoxHasNoPriceInTheShop)
{
    ItemState box;
    box.id = 0x1c0b;
    const CrtRandom before = random;
    ASSERT_TRUE(Draw(box).HasValue());
    const std::uint32_t closedState = random.State();
    const auto closedLines = Lines().text;

    random = before;
    world.Runtime().panels.npcShop = 1;
    const auto drawn = Draw(box);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(random.State(), closedState);
    EXPECT_EQ(Lines().text, closedLines);
}

// 0x5b2b0d..0x5b2b17: with 0x1a04, 0x422990 (Tooltips/PetTooltip.h) is the whole tooltip. The first time it requests
// its A9 and draws nothing, with the lines already clear.
TEST_F(ItemTooltipsTest, ThePetTooltipTakesOverAfterClearingTheLines)
{
    Lines().text[4] = "viejo";
    ItemState item;
    item.id = Pet;

    const auto drawn = Draw(item);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_TRUE(Lines().text[4].empty());
    EXPECT_EQ(Lines().next, 0);
    EXPECT_EQ(world.Runtime().petTooltipQuery.type, Pet);
    EXPECT_TRUE(text.texts.empty());
}

TEST_F(ItemTooltipsTest, TheEmptyItemDrawsNothing)
{
    Lines().text[0] = "viejo";
    ItemState item;
    item.id = 0xffff;

    ASSERT_TRUE(Draw(item).HasValue());
    EXPECT_EQ(Lines().text[0], "viejo");
    EXPECT_TRUE(text.texts.empty());
}

TEST_F(ItemTooltipsTest, ALineBeyondTheThirtiethCuts)
{
    // Nine 0x54 options, of two lines each.
    ItemState item = ExcellentWeapon();
    item.optionCount = 9;
    for (std::size_t i = 0; i < 9; ++i)
        item.optionBytes[i] = 0x54;

    const auto drawn = Draw(item);

    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("fuera de los 30"), std::string::npos) << drawn.Error();
}

TEST_F(ItemTooltipsTest, EveryItemReadsItemSetTypeForItsSetOptions)
{
    withSets = false;
    ItemState item;
    item.id = Jewel;

    const auto drawn = Draw(item);

    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x40ef05"), std::string::npos) << drawn.Error();
}
// The personal shop's price (0x5b2f7f..0x5b3716, CL9 milestone 7, phase 6a) in Lorencia (0) and Devias (2): with the
// byte of the two shops (0x7d5370f, without rand()), its grid's item under the mouse (0x78abaa4) and the maps built.
class PersonalShopPriceTest : public ItemTooltipsTest, public ::testing::WithParamInterface<std::uint8_t>
{
protected:
    void SetUp() override
    {
        world.Runtime().map = GetParam();
    }

    // The gem in cell (2, 1) of the grid, with the shop in `mode` (0x6c931c) and the mouse over it.
    ItemState OnTheShop(std::int32_t mode)
    {
        Client::Kernel::WorldRuntime& runtime = world.Runtime();
        runtime.panels.personalShop = 1;
        runtime.social.personalShop.mode = mode;
        runtime.social.personalShop.pricesBuilt = true;
        runtime.ui.shopGridHovered = 1;
        ItemState item;
        item.id = Jewel;
        item.gridColumn = 2;
        item.gridRow = 1;
        return item;
    }

    // Cell (2, 1): 2 + 1 · 8 + 0x4c.
    static constexpr std::int32_t Place = 2 + 8 + 0x4c;

    Client::Core::Expected<void, std::string> DrawWithTheRecordClosed(ItemState& item)
    {
        GameHudContext context = Context();
        return Client::Tooltips::DrawTheItemTooltip(
            Client::Tooltips::ItemTooltipContext{.hud = context, .x = 320, .y = 300, .item = item});
    }
};

// The own one: 63 with its place's price, gold from one million and in bold, and a "\n". The zen is not enough, but
// 423 is only another's. No rand() with the record open around.
TEST_P(PersonalShopPriceTest, TheOwnShopShowsThePriceOfTheCell)
{
    const ItemState item = OnTheShop(Client::Social::OwnShopView);
    world.Runtime().social.personalShop.ownPrices = {{Place - 1, 7}, {Place, 1500000}, {Place + 1, 9}};
    world.Runtime().social.personalShop.otherPrices = {{Place, 3}};
    const CrtRandom expected = Advanced(random, OwnRecordCloseRandoms + NpcShopQuery + SkillAccess);

    const auto drawn = Draw(item);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    ExpectLines({{"\n", 0, StaleBold},
                 {"Precio 1,500,000", 3, 1},
                 {"\n", 0, StaleBold},
                 {"Gema Uno", 3, 1},
                 {"\n", 0, StaleBold},
                 {"Gema para mejorar", 0, 0},
                 {"\n", 0, StaleBold}},
                4);
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
}

// Another's with less zen (unsigned): the price in red from ten million and 423 in red and bold.
TEST_P(PersonalShopPriceTest, TheOtherShopWarnsWithoutEnoughZen)
{
    const ItemState item = OnTheShop(Client::Social::OtherShopView);
    world.Runtime().social.personalShop.otherPrices = {{Place, 25000000}};
    world.OwnCharacter().zen = 24999999;

    const auto drawn = Draw(item);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    ExpectLines({{"\n", 0, StaleBold},
                 {"Precio 25,000,000", 2, 1},
                 {"\n", 0, StaleBold},
                 {"Te falta zen", 2, 1},
                 {"\n", 0, StaleBold},
                 {"Gema Uno", 3, 1},
                 {"\n", 0, StaleBold},
                 {"Gema para mejorar", 0, 0},
                 {"\n", 0, StaleBold}},
                5);
}

// With just enough zen it does not warn; green from a hundred thousand, and below, without color.
TEST_P(PersonalShopPriceTest, TheOtherShopWithEnoughZenOnlyShowsThePrice)
{
    const ItemState item = OnTheShop(Client::Social::OtherShopView);
    world.Runtime().social.personalShop.otherPrices = {{Place, 150000}};
    world.OwnCharacter().zen = 150000;

    ASSERT_TRUE(Draw(item).HasValue());
    EXPECT_EQ(Lines().text[1], "Precio 150,000");
    EXPECT_EQ(Lines().colors[1], 4);
    EXPECT_EQ(Lines().text[3], "Gema Uno");

    world.Runtime().social.personalShop.otherPrices = {{Place, 99999}};
    ASSERT_TRUE(Draw(item).HasValue());
    EXPECT_EQ(Lines().text[1], "Precio 99,999");
    EXPECT_EQ(Lines().colors[1], 0);
    EXPECT_EQ(Lines().text[3], "Gema Uno");
}

// A negative price comes out without its sign (0x5b3005, DIV) and without color (0x5b30e4, JL), and the zen is always
// less.
TEST_P(PersonalShopPriceTest, ANegativePriceIsWrittenWithoutItsSign)
{
    const ItemState item = OnTheShop(Client::Social::OtherShopView);
    world.Runtime().social.personalShop.otherPrices = {{Place, -1}};
    world.OwnCharacter().zen = 2000000000;

    ASSERT_TRUE(Draw(item).HasValue());
    EXPECT_EQ(Lines().text[1], "Precio 4,294,967,295");
    EXPECT_EQ(Lines().colors[1], 0);
    EXPECT_EQ(Lines().text[3], "Te falta zen");
}

// Without a price in its place: the own one writes 1101 in red and bold; another's, nothing.
TEST_P(PersonalShopPriceTest, WithoutAPriceOnlyTheOwnShopSaysSo)
{
    ItemState item = OnTheShop(Client::Social::OwnShopView);
    world.Runtime().social.personalShop.ownPrices = {{Place + 1, 5}};

    ASSERT_TRUE(Draw(item).HasValue());
    ExpectLines({{"\n", 0, StaleBold},
                 {"Sin precio", 2, 1},
                 {"\n", 0, StaleBold},
                 {"Gema Uno", 3, 1},
                 {"\n", 0, StaleBold},
                 {"Gema para mejorar", 0, 0},
                 {"\n", 0, StaleBold}},
                4);

    item = OnTheShop(Client::Social::OtherShopView);
    ASSERT_TRUE(Draw(item).HasValue());
    EXPECT_EQ(Lines().text[1], "Gema Uno");

    // Before the first Open, without the maps (0x5b2fba), the own one also says so.
    item = OnTheShop(Client::Social::OwnShopView);
    world.Runtime().social.personalShop.pricesBuilt = false;
    world.Runtime().social.personalShop.ownPrices = {{Place, 5}};
    ASSERT_TRUE(Draw(item).HasValue());
    EXPECT_EQ(Lines().text[1], "Sin precio");
}

// Without the grid's item under the mouse (from the inventory, 0x5b2f86) or without the shop, there is no price.
TEST_P(PersonalShopPriceTest, OnlyTheShopGridHasAPrice)
{
    ItemState item = OnTheShop(Client::Social::OwnShopView);
    world.Runtime().social.personalShop.ownPrices = {{Place, 5}};
    world.Runtime().ui.shopGridHovered = 0;
    ASSERT_TRUE(Draw(item).HasValue());
    EXPECT_EQ(Lines().text[1], "Gema Uno");

    item = OnTheShop(Client::Social::OwnShopView);
    world.Runtime().panels.personalShop = 0;
    ASSERT_TRUE(Draw(item).HasValue());
    EXPECT_EQ(Lines().text[1], "Gema Uno");
}

// With a price, the zen is read with the own record's access opened and closed: without the record open around, its
// close draws the extra 3848 rand(); without a price, it does not open it.
TEST_P(PersonalShopPriceTest, ThePriceOpensTheOwnRecord)
{
    ItemState item = OnTheShop(Client::Social::OtherShopView);
    world.Runtime().social.personalShop.otherPrices = {{Place + 1, 5}};
    const CrtRandom before = random;
    ASSERT_TRUE(DrawWithTheRecordClosed(item).HasValue());
    const CrtRandom withoutPrice = random;

    world.Runtime().social.personalShop.otherPrices = {{Place, 5}};
    random = before;
    ASSERT_TRUE(DrawWithTheRecordClosed(item).HasValue());
    EXPECT_EQ(random.State(), Advanced(withoutPrice, OwnRecordCloseRandoms).State());
    EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
}

INSTANTIATE_TEST_SUITE_P(Maps, PersonalShopPriceTest, ::testing::Values(std::uint8_t{0}, std::uint8_t{2}));

TEST_F(ItemTooltipsTest, TheRepairTooltipOfAFullWeaponHasNoPrice)
{
    world.Runtime().panels.repairMode = 2;
    ItemState item = ExcellentWeapon();
    item.durability = 51;
    const CrtRandom expected = Advanced(random, OwnRecordCloseRandoms);

    const auto drawn = Repair(item);

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    // 0x5b8150: the price "0" and the name in the name's color and in bold; it does not clear the colors.
    ExpectLines({{"\n", 0, StaleBold},
                 {"Reparar: 0", 4, 1},
                 {"\n", 0, StaleBold},
                 {"Brillante Hoja Gris +9", 4, 1},
                 {"\n", 0, StaleBold},
                 {"Vida util 51/51", 0, 0},
                 {"\n", 0, StaleBold}},
                4);
    EXPECT_EQ(world.Runtime().panels.repairMode, 1);
    EXPECT_EQ(random.State(), expected.State());
    // (3 · 13 + 4 · 13 / 2) · 480 / 480 = 65 above y, and the price half a line lower.
    ASSERT_FALSE(text.texts.empty());
    EXPECT_EQ(text.texts[0].text, "Reparar: 0");
    EXPECT_EQ(text.texts[0].y, 300 - 65 + 7);
}

TEST_F(ItemTooltipsTest, TheRepairTooltipOfADamagedWeaponHasItsPrice)
{
    const CrtRandom expected = Advanced(random, OwnRecordCloseRandoms);

    const auto drawn = Repair(ExcellentWeapon());

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    // 0x5b838d: 0x54d0f0(item, 2) gives 229300, and 0x5b13e0 charges 3 · 229300^(3/4) · (1 − 25 / 51) + 1 = 16055,
    // rounded to 100.
    ExpectLines({{"\n", 0, StaleBold},
                 {"Reparar: 16,000", 4, 1},
                 {"\n", 0, StaleBold},
                 {"Brillante Hoja Gris +9", 4, 1},
                 {"\n", 0, StaleBold},
                 {"Vida util 25/51", 0, 0},
                 {"\n", 0, StaleBold}},
                4);
    EXPECT_EQ(world.Runtime().panels.repairMode, 2);
    EXPECT_EQ(random.State(), expected.State());
}

TEST_F(ItemTooltipsTest, TheRepairPriceOfADamagedWeaponReadsTheSelfRepairFlag)
{
    // With the low byte of [0x78abb10] at 1, 0x5b13e0 adds 0.05f · 16055.
    world.Runtime().field78abb10 = 0x101;

    ASSERT_TRUE(Repair(ExcellentWeapon()).HasValue());

    EXPECT_EQ(Lines().text[1], "Reparar: 16,800");
}

TEST_F(ItemTooltipsTest, AProjectedRepairTooltipLeavesTheRepairModeAlone)
{
    projecting = true;
    world.Runtime().panels.repairMode = 0;

    ASSERT_TRUE(Repair(ExcellentWeapon()).HasValue());

    EXPECT_EQ(Lines().text[1], "Reparar: 16,000");
    EXPECT_EQ(world.Runtime().panels.repairMode, 0);
}

TEST_F(ItemTooltipsTest, SomeItemsHaveNoRepairTooltip)
{
    Lines().text[0] = "viejo";
    for (const std::uint16_t type : {Jewel, Pet, BlueBox, std::uint16_t{0x807}, std::uint16_t{0x180b}})
    {
        ItemState item;
        item.id = type;
        ASSERT_TRUE(Repair(item).HasValue());
    }
    EXPECT_EQ(Lines().text[0], "viejo");
    EXPECT_TRUE(text.texts.empty());
}
} // namespace
