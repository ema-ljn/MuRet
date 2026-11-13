#include "Scenes/ItemTooltips.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Tables/ItemSetTables.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Tables/SkillTable.h"
#include "Data/Text/TextTable.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/SpriteBatch.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameHud.h"
#include "Scenes/GameTooltips.h"
#include "Scenes/ItemSetState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/SocialState.h"
#include "Scenes/TooltipLines.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameUiState.h"
#include "UI/Widget.h"
#include "World/ItemState.h"
#include "support/ItemSetFiles.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"
#include "support/TableFiles.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// El cuerpo del tooltip de un ítem (0x5b2aa0), CL8 hito 5, con Text.bmd, Item.bmd, Skill.bmd y los conjuntos
// inventados, el texto falso de RecordingTextDrawer (13 de alto) y 640 × 480, donde ry es 1. Cada prueba abre el
// registro propio alrededor como 0x5f7bc0: su cierre gasta 3.848 rand().
namespace
{
using Client::Render::SpriteBatch;
using Client::Render::Models::CrtRandom;
using Client::Render::Models::OwnRecordCloseRandoms;
using Client::Scenes::GameHudContext;
using Client::Scenes::TooltipLines;
using Client::Scenes::WorldState;
using Client::World::ItemState;

constexpr Client::App::PixelSize Screen640{640, 480};
// Un acceso de Skill.bmd que cierra en 0 (0x3c bytes) y el IsOpen(0x10000) del precio (un byte protegido).
constexpr int SkillAccess = 60;
constexpr int NpcShopQuery = 1;
// Lo que había en las negritas antes del cuerpo: ningún cuerpo las limpia.
constexpr std::int32_t StaleBold = 7;

// Los tipos inventados.
constexpr std::uint16_t Weapon = 0x0005;
constexpr std::uint16_t SetArmor = 0x1005;
constexpr std::uint16_t Wing = 0x1801;
constexpr std::uint16_t Jewel = 0x1c0d;
constexpr std::uint16_t BlueBox = 0x1a25;
constexpr std::uint16_t Ticket = 0x1c14;
constexpr std::uint16_t Pet = 0x1a04;
constexpr std::uint8_t ArmorSet = 3;
constexpr std::uint8_t BlueBoxSkill = 0x4c;
constexpr std::uint16_t BlueBoxMana = 85;

// Las clases del héroe: & 7, y la evolución en >> 3.
constexpr std::uint8_t Wizard = 0;
constexpr std::uint8_t Knight = 1;
constexpr std::uint8_t SecondElf = 2 | (1 << 3);
constexpr std::uint8_t Gladiator = 3;

Client::Data::Text::TextTable Texts()
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
                                         {159, "fin"},
                                         {238, "Reparar: %s"},
                                         {572, "Gema para mejorar"},
                                         {577, "Golpe extra %d"},
                                         {578, "Absorbe %d"},
                                         {579, "Cola"},
                                         {620, "Brillante"},
                                         {629, "Opcion leve"},
                                         {631, "Opcion fija"},
                                         {632, "Stat D +%d"},
                                         {950, "Stat A +%d"},
                                         {951, "Stat B +%d"},
                                         {970, "Stat C %d"},
                                         {423, "Te falta zen"},
                                         {1089, "Grupo"},
                                         {1101, "Sin precio"},
                                         {1860, "Suerte %d"},
                                         {1863, "Sello uno"},
                                         {1920, "Abrir"},
                                         {1928, "Coste %d"}});
}

// Un registro de Item.bmd en claro.
class ItemRecords
{
public:
    ItemRecords() : m_plain(Client::Data::Tables::ItemTableSize) {}

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

    [[nodiscard]] Client::Data::Tables::ItemTable Table() const
    {
        const std::vector<std::byte> encrypted =
            Client::Tests::EncryptRecords(m_plain, Client::Data::Tables::ItemRecordSize);
        return Client::Data::Tables::ItemTable(std::span<const std::byte>(encrypted));
    }

private:
    std::byte& At(std::uint16_t type, std::size_t offset)
    {
        return m_plain[std::size_t{type} * Client::Data::Tables::ItemRecordSize + offset];
    }

    std::vector<std::byte> m_plain;
};

Client::Data::Tables::ItemTable Items()
{
    return ItemRecords()
        // Un arma de una mano para las clases B y D, con su velocidad y su durabilidad.
        .Named(Weapon, "Hoja Gris")
        .Byte(Weapon, 0x2b, 20)
        .Byte(Weapon, 0x2d, 22)
        .Byte(Weapon, 0x25, 3)
        .Byte(Weapon, 0x46, 1)
        .Byte(Weapon, 0x48, 1)
        // Una armadura de conjunto de la clase B, con el stat del conjunto 2.
        .Named(SetArmor, "Coraza")
        .Byte(SetArmor, 0x2d, 40)
        .Byte(SetArmor, 0x44, 2)
        .Byte(SetArmor, 0x46, 1)
        // Un ala de las clases A y C (la C desde su evolución 1).
        .Named(Wing, "Ala Leve")
        .Byte(Wing, 0x2d, 50)
        .Byte(Wing, 0x45, 1)
        .Byte(Wing, 0x47, 2)
        .Named(Jewel, "Gema Uno")
        .Byte(Jewel, 0x25, 2)
        .Named(BlueBox, "Caja Azul")
        .Named(Ticket, "Boleto")
        .Named(Pet, "Mascota")
        .Table();
}

// Skill.bmd en claro, con el maná (+0x22) de la skill de la caja azul.
Client::Data::Tables::SkillTable Skills()
{
    std::vector<std::byte> plain(Client::Data::Tables::SkillTableSize);
    const std::size_t mana = std::size_t{BlueBoxSkill} * Client::Data::Tables::SkillRecordSize + 0x22;
    plain[mana] = std::byte{BlueBoxMana & 0xff};
    plain[mana + 1] = std::byte{BlueBoxMana >> 8};
    const std::vector<std::byte> encrypted =
        Client::Tests::EncryptRecords(std::move(plain), Client::Data::Tables::SkillRecordSize);
    return Client::Data::Tables::SkillTable(std::span<const std::byte>(encrypted));
}

// ItemSetType.bmd con el conjunto 3 en la pieza 1 de la armadura, e ItemSetOption.bmd con el conjunto 3: dos pares
// (el segundo con una sola opción) y una opción del conjunto completo; todo lo demás sin usar (0xff).
Client::Data::Tables::ItemSetTables Sets()
{
    std::vector<std::byte> types(Client::Data::Tables::ItemSetTypeTableSize);
    types[std::size_t{SetArmor} * Client::Data::Tables::ItemSetTypeRecordSize] = std::byte{ArmorSet};
    std::vector<std::byte> options(Client::Data::Tables::ItemSetOptionTableSize, std::byte{0xff});
    const std::size_t base = std::size_t{ArmorSet} * Client::Data::Tables::ItemSetOptionRecordSize;
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
        Client::Scenes::OwnCharacterRecord& own = world.OwnCharacter();
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
        Client::Scenes::WorldRuntime& runtime = world.Runtime();
        Client::Scenes::ActorState& hero = world.Actors()[runtime.hero];
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

    // 0x5f7bc0: el cuerpo con el registro propio abierto alrededor.
    Client::Core::Expected<void, std::string> Draw(const ItemState& item, std::int32_t x = 320, std::int32_t y = 300)
    {
        GameHudContext context = Context();
        const Client::Render::Models::LogicalAccess own(world.Runtime().ownRecordDepth, random, OwnRecordCloseRandoms);
        return Client::Scenes::DrawTheItemTooltip(
            Client::Scenes::ItemTooltipContext{.hud = context, .x = x, .y = y, .item = item});
    }

    void HeroIs(std::uint8_t characterClass)
    {
        world.Actors()[world.Runtime().hero].characterClass = characterClass;
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
        for (std::size_t i = expected.size(); i < Client::Scenes::TooltipLineCount; ++i)
            EXPECT_TRUE(lines.text[i].empty()) << "renglón " << i;
    }

    Client::Core::Expected<void, std::string> Repair(const ItemState& item, std::int32_t x = 320, std::int32_t y = 300)
    {
        GameHudContext context = Context();
        const Client::Render::Models::LogicalAccess own(world.Runtime().ownRecordDepth, random, OwnRecordCloseRandoms);
        return Client::Scenes::DrawTheRepairTooltip(
            Client::Scenes::ItemTooltipContext{.hud = context, .x = x, .y = y, .item = item});
    }

    static CrtRandom Advanced(CrtRandom from, int count)
    {
        for (int i = 0; i < count; ++i)
            static_cast<void>(from.Next());
        return from;
    }

    // Lo que dibujó la caja, sin los "\n".
    std::vector<std::string> DrawnTexts() const
    {
        std::vector<std::string> drawn;
        for (const auto& recorded : text.texts)
            drawn.push_back(recorded.text);
        return drawn;
    }

    WorldState world;
    CrtRandom random;
    std::optional<std::int32_t> sound;
    SpriteBatch sprites;
    Client::Tests::RecordingTextDrawer text{sprites};
    Client::Data::Text::TextTable texts = Texts();
    Client::Data::Tables::ItemTable items = Items();
    Client::Data::Tables::SkillTable skills = Skills();
    Client::Data::Tables::ItemSetTables sets = Sets();
    bool withSets = true;
    bool projecting = false;
    Client::UI::Point mouse{};
};

// El arma +9 excelente con dos opciones: el 0x50 (con su valor) y el 0x5f (sin argumentos).
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
    item.optionBytes[Client::World::ItemOptionValueOffset] = 4;
    return item;
}

TEST_F(ItemTooltipsTest, AnExcellentWeaponListsItsLinesForAKnightOfItsClass)
{
    const CrtRandom expected = Advanced(random, OwnRecordCloseRandoms + NpcShopQuery + SkillAccess);

    const auto drawn = Draw(ExcellentWeapon());

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    // 0x5b1ba0: 22 + 4 · 1 + 5 · 2 de los niveles + 15 de la excelente.
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
    // La caja dibuja los renglones que no son "\n", hasta el primero vacío.
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
    // Su clase no lo usa (Item.bmd+0x45 en 0): lo que no cumple va en el color 5.
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
    Client::Scenes::ItemSetState& state = world.ItemSets();
    const std::string_view name = "Conjunto Gris";
    for (std::size_t i = 0; i < name.size(); ++i)
        state.names[0][i] = name[i];
    // El héroe lleva puestas las dos opciones del primer par, en ese orden.
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
    // El nombre lleva el del conjunto delante (0x40cf30) y el 9; la durabilidad, 20 más por el conjunto. El stat del
    // conjunto (0x40dd80) y los renglones de 0x40eee0 no escriben la negrita.
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
    // Las alas muestran siempre la durabilidad: 50 + 3 de los niveles. La clase C pide la evolución 1 (el byte 2: el
    // nombre es el 15 + 5 · 2 + 2).
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

TEST_F(ItemTooltipsTest, TheTooltipsDrawTheBodyOfTheHoveredItemWithTheOwnRecordOpen)
{
    ItemState& cell = world.Inventory().cells[3];
    cell.id = Jewel;
    Client::UI::GameUiState& ui = world.Runtime().ui;
    ui.hoveredItem = Client::UI::HoveredItem{.place = Client::UI::HoveredItem::Place::InventoryCell, .index = 3};
    ui.skillTooltipX = 320;
    ui.skillTooltipY = 300;
    const CrtRandom expected = Advanced(random, OwnRecordCloseRandoms + NpcShopQuery + SkillAccess);
    GameHudContext context = Context();

    const auto drawn = Client::Scenes::DrawTheTooltips(context);

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
    // 0x5b8043: (5 − 3) · 13 + 3 · 13 / 2 = 45 arriba de y; el nombre va medio renglón (6,5 · 1,1) más abajo.
    ASSERT_FALSE(text.texts.empty());
    EXPECT_EQ(text.texts[0].y, 300 - 45 + 7);

    text.texts.clear();
    ASSERT_TRUE(Draw(item, 320, 20).HasValue());
    // Sin lugar arriba: debajo, a su alto de Item.bmd (2) por 20.
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

TEST_F(ItemTooltipsTest, ThePriceOfTheNpcShopCuts)
{
    world.Runtime().panels.npcShop = 1;
    const CrtRandom expected = Advanced(random, OwnRecordCloseRandoms + NpcShopQuery);

    const auto drawn = Draw(ExcellentWeapon());

    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x5b2d23"), std::string::npos) << drawn.Error();
    EXPECT_EQ(random.State(), expected.State());
}

TEST_F(ItemTooltipsTest, ThePetTooltipCutsAfterClearingTheLines)
{
    Lines().text[4] = "viejo";
    ItemState item;
    item.id = Pet;

    const auto drawn = Draw(item);

    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x4229d4"), std::string::npos) << drawn.Error();
    EXPECT_TRUE(Lines().text[4].empty());
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
    // Nueve opciones 0x54, de dos renglones cada una.
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
// El precio de la tienda personal (0x5b2f7f..0x5b3716, CL9 hito 7, fase 6a) en Lorencia (0) y Devias (2): con el byte
// de las dos tiendas (0x7d5370f, sin rand()), el ítem de su grilla bajo el mouse (0x78abaa4) y los mapas armados.
class PersonalShopPriceTest : public ItemTooltipsTest, public ::testing::WithParamInterface<std::uint8_t>
{
protected:
    void SetUp() override
    {
        world.Runtime().map = GetParam();
    }

    // La gema en la casilla (2, 1) de la grilla, con la tienda en `mode` (0x6c931c) y el mouse sobre ella.
    ItemState OnTheShop(std::int32_t mode)
    {
        Client::Scenes::WorldRuntime& runtime = world.Runtime();
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

    // El lugar de la casilla (2, 1): 2 + 1 · 8 + 0x4c.
    static constexpr std::int32_t Place = 2 + 8 + 0x4c;

    // El cuerpo sin el registro propio abierto alrededor.
    Client::Core::Expected<void, std::string> DrawWithTheRecordClosed(const ItemState& item)
    {
        GameHudContext context = Context();
        return Client::Scenes::DrawTheItemTooltip(
            Client::Scenes::ItemTooltipContext{.hud = context, .x = 320, .y = 300, .item = item});
    }
};

// La propia: el 63 con el precio de su lugar, dorado desde un millón y en negrita, y un "\n". El zen no alcanza, pero
// el 423 es solo de la de otro. Sin rand() con el registro abierto alrededor.
TEST_P(PersonalShopPriceTest, TheOwnShopShowsThePriceOfTheCell)
{
    const ItemState item = OnTheShop(Client::Scenes::OwnShopView);
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

// La de otro con menos zen (sin signo): el precio en rojo desde diez millones y el 423 en rojo y en negrita.
TEST_P(PersonalShopPriceTest, TheOtherShopWarnsWithoutEnoughZen)
{
    const ItemState item = OnTheShop(Client::Scenes::OtherShopView);
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

// Con el zen justo no avisa; el verde desde cien mil, y debajo, sin color.
TEST_P(PersonalShopPriceTest, TheOtherShopWithEnoughZenOnlyShowsThePrice)
{
    const ItemState item = OnTheShop(Client::Scenes::OtherShopView);
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

// Un precio negativo sale sin signo (0x5b3005, DIV) y sin color (0x5b30e4, JL), y el zen siempre es menos.
TEST_P(PersonalShopPriceTest, ANegativePriceIsWrittenWithoutItsSign)
{
    const ItemState item = OnTheShop(Client::Scenes::OtherShopView);
    world.Runtime().social.personalShop.otherPrices = {{Place, -1}};
    world.OwnCharacter().zen = 2000000000;

    ASSERT_TRUE(Draw(item).HasValue());
    EXPECT_EQ(Lines().text[1], "Precio 4,294,967,295");
    EXPECT_EQ(Lines().colors[1], 0);
    EXPECT_EQ(Lines().text[3], "Te falta zen");
}

// Sin precio en su lugar: la propia escribe el 1101 en rojo y en negrita; la de otro, nada.
TEST_P(PersonalShopPriceTest, WithoutAPriceOnlyTheOwnShopSaysSo)
{
    ItemState item = OnTheShop(Client::Scenes::OwnShopView);
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

    item = OnTheShop(Client::Scenes::OtherShopView);
    ASSERT_TRUE(Draw(item).HasValue());
    EXPECT_EQ(Lines().text[1], "Gema Uno");

    // Antes del primer Open, sin los mapas (0x5b2fba), la propia también lo dice.
    item = OnTheShop(Client::Scenes::OwnShopView);
    world.Runtime().social.personalShop.pricesBuilt = false;
    world.Runtime().social.personalShop.ownPrices = {{Place, 5}};
    ASSERT_TRUE(Draw(item).HasValue());
    EXPECT_EQ(Lines().text[1], "Sin precio");
}

// Sin el ítem de la grilla bajo el mouse (desde el inventario, 0x5b2f86) o sin la tienda, no hay precio.
TEST_P(PersonalShopPriceTest, OnlyTheShopGridHasAPrice)
{
    ItemState item = OnTheShop(Client::Scenes::OwnShopView);
    world.Runtime().social.personalShop.ownPrices = {{Place, 5}};
    world.Runtime().ui.shopGridHovered = 0;
    ASSERT_TRUE(Draw(item).HasValue());
    EXPECT_EQ(Lines().text[1], "Gema Uno");

    item = OnTheShop(Client::Scenes::OwnShopView);
    world.Runtime().panels.personalShop = 0;
    ASSERT_TRUE(Draw(item).HasValue());
    EXPECT_EQ(Lines().text[1], "Gema Uno");
}

// Con precio, el zen se lee con el acceso del registro propio abierto y cerrado: sin el registro abierto alrededor, su
// cierre saca los 3.848 rand() de más; sin precio, no lo abre.
TEST_P(PersonalShopPriceTest, ThePriceOpensTheOwnRecord)
{
    ItemState item = OnTheShop(Client::Scenes::OtherShopView);
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
    // 0x5b8150: el precio "0" y el nombre en el color del nombre y en negrita; no limpia los colores.
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
    // (3 · 13 + 4 · 13 / 2) · 480 / 480 = 65 arriba de y, y el precio medio renglón más abajo.
    ASSERT_FALSE(text.texts.empty());
    EXPECT_EQ(text.texts[0].text, "Reparar: 0");
    EXPECT_EQ(text.texts[0].y, 300 - 65 + 7);
}

TEST_F(ItemTooltipsTest, TheRepairTooltipOfADamagedWeaponHasItsPrice)
{
    const CrtRandom expected = Advanced(random, OwnRecordCloseRandoms);

    const auto drawn = Repair(ExcellentWeapon());

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    // 0x5b838d: 0x54d0f0(ítem, 2) da 229.300, y 0x5b13e0 cobra 3 · 229.300^(3/4) · (1 − 25 / 51) + 1 = 16.055,
    // redondeado a 100.
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
    // Con el byte bajo de [0x78abb10] en 1, 0x5b13e0 suma 0,05f · 16.055.
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
