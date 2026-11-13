#include "Scenes/GameHelps.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Tables/ItemSetTables.h"
#include "Data/Tables/ItemTable.h"
#include "Data/Text/TextTable.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameHud.h"
#include "Scenes/ItemHelpLevels.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/TooltipLines.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/PanelRegistry.h"
#include "support/ItemSetFiles.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"
#include "support/TableFiles.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// La ayuda del ítem (CL8; 06 § HUD y ventanas, "La ayuda del ítem"): el modo 3 de 0x5b0550, con la tabla por nivel
// de 0x5afc00, la caja de arriba (0x5af090 con marco) y las columnas de 0x5afb00 y 0x5af9b0. El texto mide como el
// rasterizador falso: 6 columnas por letra y 13 filas. Los ítems, los textos y el personaje son inventados.
namespace
{
using Client::Render::BlendMode;
using Client::Render::Color;
using Client::Render::SpriteBatch;
using Client::Render::Models::CrtRandom;
using Client::Render::Text::FontKind;
using Client::Scenes::GameHudContext;
using Client::Scenes::ItemHelpLevels;
using Client::Scenes::WorldState;
using Client::Tests::RecordedText;
using Client::Tests::RecordingTextDrawer;
using Client::Tests::TextMarkerSlot;

constexpr Client::App::PixelSize Screen640{640, 480};
constexpr Client::App::PixelSize Screen800{800, 600};
constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};
constexpr Color Red{1.0f, 0.2f, 0.1f, 1.0f};
constexpr Color Green{0.1f, 1.0f, 0.5f, 1.0f};
constexpr Color LightBlue{0.5f, 0.7f, 1.0f, 1.0f};
// Una tabla nueva: 14 cierres del registro propio, de 3.848 rand() cada uno.
constexpr std::uint32_t FillRandoms = 14 * Client::Render::Models::OwnRecordCloseRandoms;

// Una espada (la franja 1), un escudo y una armadura de conjunto (la 3 y la 4), un bastón y un cetro, un pergamino
// (la 5) y un ítem con requisitos que envuelven.
constexpr std::uint16_t Sword = 0x0010;
constexpr std::uint16_t Staff = 0x0a05;
constexpr std::uint16_t Scepter = 0x0408;
constexpr std::uint16_t Shield = 0x0c02;
constexpr std::uint16_t Armor = 0x0e03;
constexpr std::uint16_t SetArmor = 0x0e10;
constexpr std::uint16_t Scroll = 0x1e05;
constexpr std::uint16_t Heavy = 0x0011;
constexpr std::uint8_t FirstSet = 3;
constexpr std::uint8_t SecondSet = 5;

std::map<std::size_t, std::string> HelpTexts()
{
    return {{20, "Clase A"}, {60, "No usa %s"}, {61, "Usa %s"},  {160, "Ayuda de objeto"},
            {161, "Niv"},    {162, "Dano"},     {163, "Mag"},    {164, "Def"},
            {165, "Tasa"},   {166, "Fue"},      {167, "Agi"},    {168, "Ene"},
            {169, "Vit"},    {591, "Mezcla"},   {1089, "Grupo"}, {1090, "Nivel"},
            {1239, "Cetro"}, {1900, "Lid"},     {1931, "Req"}};
}

// Item.bmd en claro.
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

    ItemRecords& Word(std::uint16_t type, std::size_t offset, std::uint16_t value)
    {
        At(type, offset) = static_cast<std::byte>(value & 0xffU);
        At(type, offset + 1) = static_cast<std::byte>(value >> 8U);
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
        // Daño 10 a 15, nivel 20, fuerza 40, agilidad 30 y energía 10; la usa la clase 0.
        .Named(Sword, "Hoja Prueba")
        .Word(Sword, 0x20, 20)
        .Byte(Sword, 0x26, 10)
        .Byte(Sword, 0x27, 15)
        .Word(Sword, 0x30, 40)
        .Word(Sword, 0x32, 30)
        .Word(Sword, 0x34, 10)
        .Byte(Sword, 0x45, 1)
        .Named(Staff, "Vara")
        .Byte(Staff, 0x2f, 12)
        .Named(Scepter, "Cetro Gris")
        .Byte(Scepter, 0x2f, 12)
        .Named(Shield, "Rodela")
        .Byte(Shield, 0x28, 8)
        .Byte(Shield, 0x29, 5)
        .Named(Armor, "Peto")
        .Byte(Armor, 0x29, 5)
        .Named(SetArmor, "Coraza Prueba")
        .Byte(SetArmor, 0x29, 6)
        // Energía 15 y nivel 30, desde el nivel 10.
        .Named(Scroll, "Pergamino")
        .Word(Scroll, 0x20, 10)
        .Word(Scroll, 0x34, 15)
        .Word(Scroll, 0x3a, 30)
        .Named(Heavy, "Mole")
        .Word(Heavy, 0x20, 0xffff)
        .Word(Heavy, 0x30, 0xffff)
        .Table();
}

// La armadura de conjunto: el 3 en la pieza 1 y el 5 en la 2, con las mezclas 1 (byte 2) y 2 (byte 3).
Client::Data::Tables::ItemSetTables Sets()
{
    std::vector<std::byte> types(Client::Data::Tables::ItemSetTypeTableSize);
    const std::size_t type = std::size_t{SetArmor} * Client::Data::Tables::ItemSetTypeRecordSize;
    types[type] = std::byte{FirstSet};
    types[type + 1] = std::byte{SecondSet};
    types[type + 2] = std::byte{1};
    types[type + 3] = std::byte{2};
    std::vector<std::byte> options(Client::Data::Tables::ItemSetOptionTableSize, std::byte{0xff});
    const auto name = [&](std::uint8_t set, std::string_view text)
    {
        const std::size_t base = std::size_t{set} * Client::Data::Tables::ItemSetOptionRecordSize;
        for (std::size_t i = 0; i < text.size(); ++i)
            options[base + i] = static_cast<std::byte>(text[i]);
        options[base + text.size()] = std::byte{0};
    };
    name(FirstSet, "Conjunto Gris");
    name(SecondSet, "Otro");
    return Client::Tests::ItemSetTablesFrom(std::move(types), std::move(options));
}

// El color de la marca de cada texto, en orden.
std::vector<Color> TextColorsOf(const SpriteBatch& sprites)
{
    std::vector<Color> colors;
    const auto vertices = sprites.Vertices();
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        if (command.texture != TextMarkerSlot)
            continue;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
            colors.push_back(vertices[command.firstVertex + quad * 6].color);
    }
    return colors;
}

// El fondo de la caja con marco: el quinto quad sin textura (los cuatro bordes van antes).
std::pair<float, float> BackgroundXOf(const SpriteBatch& sprites)
{
    std::vector<float> xs;
    const auto vertices = sprites.Vertices();
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        if (command.texture.has_value())
            continue;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
        {
            xs.push_back(vertices[command.firstVertex + quad * 6].x);
            xs.push_back(vertices[command.firstVertex + quad * 6 + 2].x);
        }
    }
    if (xs.size() < 10)
        return {-1.0f, -1.0f};
    return {xs[8], xs[9]};
}

class GameItemHelpTest : public ::testing::TestWithParam<std::int32_t>
{
protected:
    GameItemHelpTest() : m_world(std::make_unique<WorldState>()), m_text(m_sprites)
    {
        m_sprites.Begin(Screen640);
        Client::Scenes::WorldRuntime& runtime = m_world->Runtime();
        runtime.map = GetParam();
        runtime.panels.overlayMode = Client::UI::ItemHelpMode;
        m_world->Actors()[runtime.hero].characterClass = 0;
        Client::Scenes::OwnCharacterRecord& own = m_world->OwnCharacter();
        own.level = 10;
        own.strength = 50;
        own.word2a = 5;
        own.agility = 60;
        own.energy = 100;
    }

    GameHudContext Context()
    {
        Client::Scenes::WorldRuntime& runtime = m_world->Runtime();
        Client::Scenes::ActorState& hero = m_world->Actors()[runtime.hero];
        return GameHudContext{.world = *m_world,
                              .random = m_random,
                              .ownRecordDepth = runtime.ownRecordDepth,
                              .cellXDepth = hero.cellXAccesses,
                              .cellYDepth = hero.cellYAccesses,
                              .hud = runtime.hud,
                              .crywolfSandOverlay = runtime.crywolf.sandOverlay,
                              .sound = m_sound,
                              .sprites = m_sprites,
                              .text = m_text,
                              .texts = m_withTexts ? &m_texts : nullptr,
                              .items = m_withItems ? &m_items : nullptr,
                              .setOptions = &m_sets.options,
                              .setTypes = m_withSets ? &m_sets.types : nullptr};
    }

    Client::Core::Expected<void, std::string> Help(std::uint16_t item)
    {
        m_world->Runtime().panels.helpItem = item;
        m_text.texts.clear();
        m_sprites.Begin(m_sprites.Screen());
        GameHudContext context = Context();
        return Client::Scenes::DrawTheOverlays(context);
    }

    [[nodiscard]] Client::Scenes::HudState& Hud()
    {
        return m_world->Runtime().hud;
    }

    [[nodiscard]] const ItemHelpLevels& Levels()
    {
        return Hud().itemHelp;
    }

    // El primer texto `text` dibujado, o el tamaño de la lista.
    [[nodiscard]] std::size_t Find(std::string_view text) const
    {
        for (std::size_t i = 0; i < m_text.texts.size(); ++i)
        {
            if (m_text.texts[i].text == text)
                return i;
        }
        return m_text.texts.size();
    }

    std::unique_ptr<WorldState> m_world;
    CrtRandom m_random{0x5eed1234u};
    std::optional<std::int32_t> m_sound{};
    SpriteBatch m_sprites;
    RecordingTextDrawer m_text;
    Client::Data::Text::TextTable m_texts = Client::Tests::MakeTextTable(HelpTexts());
    Client::Data::Tables::ItemTable m_items = Items();
    Client::Data::Tables::ItemSetTables m_sets = Sets();
    bool m_withTexts = true;
    bool m_withItems = true;
    bool m_withSets = true;
};

// 0x5afc00: cada nivel suma 3 hasta el 9 y 4, 9, 15 y 22 más del 10 al 13; los requisitos, (20 + 3 · nivel) ·
// requisito · 3 (4 la energía) / 100 + 20; la columna 1, si el registro propio cumple (fuerza 50 + 5).
TEST_P(GameItemHelpTest, TheTableHasEachLevel)
{
    ASSERT_TRUE(Help(Sword).HasValue());
    const ItemHelpLevels& levels = Levels();
    EXPECT_EQ(levels.item, Sword);
    using Client::Scenes::ItemHelpRow;
    const ItemHelpRow& first = levels.rows[0];
    EXPECT_EQ(first[Client::Scenes::ItemHelpLevel], 0);
    EXPECT_EQ(first[Client::Scenes::ItemHelpMinDamage], 10);
    EXPECT_EQ(first[Client::Scenes::ItemHelpMaxDamage], 15);
    EXPECT_EQ(first[Client::Scenes::ItemHelpStrength], 44);
    EXPECT_EQ(first[Client::Scenes::ItemHelpAgility], 38);
    EXPECT_EQ(first[Client::Scenes::ItemHelpEnergy], 28);
    EXPECT_EQ(first[Client::Scenes::ItemHelpUsable], 1);
    EXPECT_EQ(levels.rows[9][Client::Scenes::ItemHelpMinDamage], 37);
    EXPECT_EQ(levels.rows[10][Client::Scenes::ItemHelpMinDamage], 41);
    EXPECT_EQ(levels.rows[11][Client::Scenes::ItemHelpMinDamage], 46);
    EXPECT_EQ(levels.rows[12][Client::Scenes::ItemHelpMinDamage], 52);
    const ItemHelpRow& last = levels.rows[13];
    EXPECT_EQ(last[Client::Scenes::ItemHelpLevel], 13);
    EXPECT_EQ(last[Client::Scenes::ItemHelpMinDamage], 59);
    EXPECT_EQ(last[Client::Scenes::ItemHelpMaxDamage], 64);
    EXPECT_EQ(last[Client::Scenes::ItemHelpStrength], 90);
    EXPECT_EQ(last[Client::Scenes::ItemHelpAgility], 73);
    EXPECT_EQ(last[Client::Scenes::ItemHelpEnergy], 43);
    // La fuerza pide 54 en el nivel 3 y 58 en el 4.
    EXPECT_EQ(levels.rows[3][Client::Scenes::ItemHelpUsable], 1);
    EXPECT_EQ(levels.rows[4][Client::Scenes::ItemHelpUsable], 0);
    EXPECT_EQ(last[Client::Scenes::ItemHelpUsable], 0);
    // Las columnas que nadie escribe.
    EXPECT_EQ(first[11], 0);
    EXPECT_EQ(first[12], 0);
    EXPECT_EQ(first[16], 0);
}

// 0x5afce1..0x5afd8d: el poder a la mitad más 2 · nivel (salvo los cetros, en la columna 5), la defensa del escudo
// más el nivel y la de los demás como el daño; 0x5afdea: en 32 bits con signo, un requisito que envuelve queda
// negativo y no pide nada.
TEST_P(GameItemHelpTest, TheMagicTheDefenseAndTheWrappedRequirements)
{
    ASSERT_TRUE(Help(Staff).HasValue());
    EXPECT_EQ(Levels().rows[0][Client::Scenes::ItemHelpMagic], 6);
    EXPECT_EQ(Levels().rows[13][Client::Scenes::ItemHelpMagic], 30 + 26);
    EXPECT_EQ(Levels().rows[13][Client::Scenes::ItemHelpScepterMagic], 0);
    ASSERT_TRUE(Help(Scepter).HasValue());
    EXPECT_EQ(Levels().rows[13][Client::Scenes::ItemHelpMagic], 0);
    EXPECT_EQ(Levels().rows[13][Client::Scenes::ItemHelpScepterMagic], 30);
    ASSERT_TRUE(Help(Shield).HasValue());
    EXPECT_EQ(Levels().rows[13][Client::Scenes::ItemHelpDefense], 18);
    EXPECT_EQ(Levels().rows[13][Client::Scenes::ItemHelpDefenseRate], 57);
    ASSERT_TRUE(Help(Armor).HasValue());
    EXPECT_EQ(Levels().rows[13][Client::Scenes::ItemHelpDefense], 54);
    // 65.535 · 65.535 · 3 envuelve a −393.213: / 100 + 20.
    ASSERT_TRUE(Help(Heavy).HasValue());
    EXPECT_EQ(Levels().rows[0][Client::Scenes::ItemHelpStrength], -3912);
    EXPECT_EQ(Levels().rows[0][Client::Scenes::ItemHelpUsable], 1);
}

// 0x5afc09 y 0x5aff9b..0x5b04c3: la tabla se llena una vez por ítem, con 14 cierres del registro propio; con el
// mismo ítem no saca nada aunque cambien los stats (Q-C750), y con el registro ya abierto, tampoco.
TEST_P(GameItemHelpTest, TheTableFillsOncePerItem)
{
    CrtRandom expected = m_random;
    ASSERT_TRUE(Help(Sword).HasValue());
    expected.Discard(FillRandoms);
    EXPECT_EQ(m_random.State(), expected.State());

    m_world->OwnCharacter().strength = 1000;
    ASSERT_TRUE(Help(Sword).HasValue());
    EXPECT_EQ(m_random.State(), expected.State());
    EXPECT_EQ(Levels().rows[13][Client::Scenes::ItemHelpUsable], 0);

    ASSERT_TRUE(Help(Shield).HasValue());
    expected.Discard(FillRandoms);
    EXPECT_EQ(m_random.State(), expected.State());

    m_world->OwnCharacter().agility = 1000;
    m_world->Runtime().ownRecordDepth = 1;
    ASSERT_TRUE(Help(Sword).HasValue());
    EXPECT_EQ(m_random.State(), expected.State());
    EXPECT_EQ(Levels().rows[13][Client::Scenes::ItemHelpUsable], 1);
}

// 0x5b09b0..0x5b0dea: "\n", el 160 en azul y negrita, el nombre en negrita, "\n", " ", "\n", 46 espacios, 13 " ",
// las clases y "\n"; la caja de 2 · 104 + 4 (el ancho de 6,1 columnas de 17) con marco, arriba a la izquierda.
TEST_P(GameItemHelpTest, TheBoxHasTheTitleTheNameAndTheBlankRows)
{
    ASSERT_TRUE(Help(Sword).HasValue());
    EXPECT_EQ(Hud().durabilityWarning, 1);
    const Client::Scenes::TooltipLines& lines = Hud().tooltip;
    // Los renglones de abajo de las columnas quedan como los dejó la caja.
    for (std::size_t line = 14; line <= 19; ++line)
        EXPECT_EQ(lines.text[line], " ") << line;
    EXPECT_EQ(lines.text[20], "\n");
    EXPECT_EQ(lines.text[21], "Usa Clase A");
    EXPECT_EQ(lines.text[22], "\n");
    EXPECT_EQ(lines.next, 0);

    const auto [left, right] = BackgroundXOf(m_sprites);
    EXPECT_FLOAT_EQ(left, 0.0f);
    EXPECT_FLOAT_EQ(right, 212.0f);
    ASSERT_GE(m_text.texts.size(), 4U);
    const RecordedText& title = m_text.texts[0];
    EXPECT_EQ(title.text, "Ayuda de objeto");
    EXPECT_EQ(title.x, 1);
    EXPECT_EQ(title.y, 8);
    EXPECT_EQ(title.firstColumn, -(210 - 90) / 2);
    EXPECT_EQ(title.columns, 210);
    EXPECT_EQ(title.style.font, FontKind::Bold);
    const RecordedText& name = m_text.texts[1];
    EXPECT_EQ(name.text, "Hoja Prueba");
    EXPECT_EQ(name.y, 22);
    EXPECT_EQ(name.style.font, FontKind::Bold);
    EXPECT_EQ(m_text.texts[2].text, std::string(46, ' '));
    EXPECT_EQ(m_text.texts[2].y, 65);
    EXPECT_EQ(m_text.texts[3].text, "Usa Clase A");
    EXPECT_EQ(m_text.texts[3].y, 272);
    const std::vector<Color> colors = TextColorsOf(m_sprites);
    ASSERT_GE(colors.size(), 4U);
    EXPECT_EQ(colors[0], LightBlue);
    EXPECT_EQ(colors[1], White);
    EXPECT_EQ(m_sprites.Blend(), BlendMode::Opaque);
}

// 0x5b0dfb..0x5b10b6 en 640 × 480: desde x 24 (0x870 / 90), el nivel, el daño (el mínimo, el título, "~" y el
// máximo), la fuerza, la agilidad y la energía; los títulos en y 38 y las filas en y 52, cada caja centrada en su x y
// el texto contra su borde. Las filas que el registro propio no cumple, en rojo.
TEST_P(GameItemHelpTest, TheColumnsGoLeftToRight)
{
    ASSERT_TRUE(Help(Sword).HasValue());
    ASSERT_EQ(m_text.texts.size(), 4U + 103U);
    struct Expected
    {
        std::string_view text;
        std::int32_t x = 0;
        std::int32_t y = 0;
        std::int32_t firstColumn = 0;
        std::int32_t columns = 0;
    };
    // Cada caja: el más ancho + 4 desde x − la mitad; el texto en left + 1, en las columnas de la caja − 2.
    const std::vector<std::pair<std::size_t, Expected>> expected{
        {4, {"Niv", 14, 38, -2, 20}},    {5, {"+0", 14, 52, -8, 20}},    {6, {"+1", 14, 66, -8, 20}},
        {18, {"+13", 14, 237, -2, 20}},  {19, {" 10", 50, 52, -2, 20}},  {32, {" 59", 50, 237, -2, 20}},
        {33, {"Dano", 65, 38, -2, 26}},  {34, {"~", 74, 52, -2, 8}},     {48, {" 15", 86, 52, -2, 20}},
        {62, {"Fue", 116, 38, -2, 20}},  {63, {" 44", 116, 52, -2, 20}}, {77, {"Agi", 146, 38, -2, 20}},
        {78, {" 38", 146, 52, -2, 20}},  {92, {"Ene", 176, 38, -2, 20}}, {93, {" 28", 176, 52, -2, 20}},
        {106, {" 43", 176, 237, -2, 20}}};
    for (const auto& [index, text] : expected)
    {
        const RecordedText& drawn = m_text.texts[index];
        EXPECT_EQ(drawn.text, text.text) << index;
        EXPECT_EQ(drawn.x, text.x) << index;
        EXPECT_EQ(drawn.y, text.y) << index;
        EXPECT_EQ(drawn.firstColumn, text.firstColumn) << index;
        EXPECT_EQ(drawn.columns, text.columns) << index;
    }
    const std::vector<Color> colors = TextColorsOf(m_sprites);
    ASSERT_EQ(colors.size(), m_text.texts.size());
    EXPECT_EQ(colors[4], LightBlue);
    for (std::size_t row = 0; row < 14; ++row)
        EXPECT_EQ(colors[5 + row], row <= 3 ? White : Red) << row;
    EXPECT_EQ(m_text.texts[5].style.font, FontKind::Normal);
    EXPECT_EQ(m_sprites.Blend(), BlendMode::Opaque);
    EXPECT_EQ(Hud().dcFont, FontKind::Normal);
}

// En 800 × 600: el mismo divisor (90) y la misma x, con los títulos en y 33 y las filas en y 47; la caja de arriba de
// 2 · 104 / 1,25 + 4 y cada columna corre lo que mide su muestra / 1,25.
TEST_P(GameItemHelpTest, AtAnotherResolution)
{
    m_sprites.Begin(Screen800);
    ASSERT_TRUE(Help(Sword).HasValue());
    ASSERT_EQ(m_text.texts.size(), 4U + 103U);
    const auto [left, right] = BackgroundXOf(m_sprites);
    EXPECT_FLOAT_EQ(left, 0.0f);
    EXPECT_NEAR(right, 170.4f * 1.25f, 1e-3f);
    EXPECT_EQ(m_text.texts[0].columns, 210);
    EXPECT_EQ(m_text.texts[2].text, std::string(46, ' '));
    const RecordedText& level = m_text.texts[4];
    EXPECT_EQ(level.text, "Niv");
    EXPECT_EQ(level.x, 15);
    EXPECT_EQ(level.y, 33);
    EXPECT_EQ(level.columns, 20);
    EXPECT_EQ(m_text.texts[5].y, 47);
    // "000000" mide 36 / 1,25 = 28,8: el mínimo va en 24 + 28, con su caja de 18,4 desde 52 − 9,2.
    const std::size_t damage = Find(" 10");
    ASSERT_LT(damage, m_text.texts.size());
    EXPECT_EQ(m_text.texts[damage].x, 43);
    // Y la energía en 52 + 14 + 14 + 24 + 24 + 24.
    const std::size_t energy = Find("Ene");
    ASSERT_LT(energy, m_text.texts.size());
    EXPECT_EQ(m_text.texts[energy].x, 143);
}

// 0x5b0bf2: con más de 800 de ancho, 51 espacios; en 1.024, el divisor 103 y la x 2.160 / 103.
TEST_P(GameItemHelpTest, AWiderScreenHasMoreSpaces)
{
    m_sprites.Begin(Client::App::PixelSize{1024, 768});
    ASSERT_TRUE(Help(Sword).HasValue());
    EXPECT_EQ(m_text.texts[2].text, std::string(51, ' '));
    EXPECT_EQ(m_text.texts[4].text, "Niv");
}

// La franja 5 (0x1e00..0x1fff): sin renglones en blanco, desde x 66 (0x1734 / 90); el nivel y la energía con una
// sola fila y el nivel pedido con las 14, que pasan la caja de arriba (Q-C752). Sin daño y sin agilidad.
TEST_P(GameItemHelpTest, AScrollHasOneRowInItsLevelColumns)
{
    ASSERT_TRUE(Help(Scroll).HasValue());
    ASSERT_EQ(m_text.texts.size(), 3U + 19U);
    EXPECT_EQ(m_text.texts[0].text, "Ayuda de objeto");
    EXPECT_EQ(m_text.texts[1].text, "Pergamino");
    EXPECT_EQ(m_text.texts[3].text, "Niv");
    EXPECT_EQ(m_text.texts[4].text, "+0");
    EXPECT_EQ(m_text.texts[4].x, 66 - 8 + 1);
    EXPECT_EQ(m_text.texts[5].text, "Req");
    EXPECT_EQ(m_text.texts[5].x, 104 - 11 + 1);
    for (std::size_t row = 0; row < 14; ++row)
        EXPECT_EQ(m_text.texts[6 + row].text, " 30") << row;
    EXPECT_EQ(m_text.texts[19].y, 237);
    EXPECT_EQ(m_text.texts[20].text, "Ene");
    EXPECT_EQ(m_text.texts[20].x, 144 - 11 + 1);
    EXPECT_EQ(m_text.texts[21].text, " 26");
    // El nivel pedido (30) pasa el del personaje (10): rojo.
    const std::vector<Color> colors = TextColorsOf(m_sprites);
    EXPECT_EQ(colors[21], Red);
    // La caja de arriba: 2 · 90 + 4.
    EXPECT_FLOAT_EQ(BackgroundXOf(m_sprites).second, 184.0f);
}

// 0x5b0c97..0x5b0d9f: un renglón verde por pieza con su conjunto: la 1 con el byte 3 de ItemSetType.bmd y la 2 con
// el 2; desde x 20 (0x708 / 90), con la defensa y la agilidad (que se dibuja siempre fuera de la franja 5).
TEST_P(GameItemHelpTest, ASetItemListsItsPieces)
{
    ASSERT_TRUE(Help(SetArmor).HasValue());
    const Client::Scenes::TooltipLines& lines = Hud().tooltip;
    EXPECT_EQ(lines.text[20], "Conjunto Gris  Grupo Mezcla:(Nivel+2)");
    EXPECT_EQ(lines.text[21], "Otro  Grupo Mezcla:(Nivel+1)");
    EXPECT_EQ(lines.colors[20], Client::Scenes::TooltipGreen);
    EXPECT_EQ(lines.colors[21], Client::Scenes::TooltipGreen);
    EXPECT_EQ(lines.text[22], "\n");
    EXPECT_EQ(m_text.texts[3].text, "Conjunto Gris  Grupo Mezcla:(Nivel+2)");
    const std::vector<Color> colors = TextColorsOf(m_sprites);
    EXPECT_EQ(colors[3], Green);
    EXPECT_EQ(colors[4], Green);
    const std::size_t defense = Find("Def");
    ASSERT_LT(defense, m_text.texts.size());
    EXPECT_EQ(m_text.texts[defense].x, 56 - 11 + 1);
    EXPECT_EQ(m_text.texts[defense + 1].text, "  6");
    const std::size_t agility = Find("Agi");
    ASSERT_LT(agility + 1, m_text.texts.size());
    EXPECT_EQ(m_text.texts[agility].x, 92 - 11 + 1);
    EXPECT_EQ(m_text.texts[agility + 1].text, "  0");
}

// Q-C751: el título de la primera columna usa la negrita que haya en el renglón 0; las filas de 0x5af9b0 la ponen
// en 0, y desde el segundo dibujo va sin negrita.
TEST_P(GameItemHelpTest, TheFirstTitleKeepsTheBoldOfLineZero)
{
    Hud().tooltip.bold[0] = 1;
    ASSERT_TRUE(Help(Sword).HasValue());
    ASSERT_EQ(m_text.texts[4].text, "Niv");
    EXPECT_EQ(m_text.texts[4].style.font, FontKind::Bold);
    EXPECT_EQ(m_text.texts[33].text, "Dano");
    EXPECT_EQ(m_text.texts[33].style.font, FontKind::Normal);
    ASSERT_TRUE(Help(Sword).HasValue());
    EXPECT_EQ(m_text.texts[4].style.font, FontKind::Normal);
}

// Lo que corta: sin Item.bmd (0x5b098f), sin el texto 160 (0x5b09f0) o con uno de 100 bytes (0x5b0a03, Q-C613), sin
// ItemSetType.bmd (0x40cf13).
TEST_P(GameItemHelpTest, WhatItCannotReadCuts)
{
    const auto cutsAt = [this](std::string_view where)
    {
        const auto drawn = Help(Sword);
        ASSERT_FALSE(drawn.HasValue()) << where;
        EXPECT_NE(drawn.Error().find(where), std::string::npos) << drawn.Error();
    };
    m_withItems = false;
    cutsAt("0x5b098f");
    m_withItems = true;
    m_withTexts = false;
    cutsAt("0x5b09f0");
    m_withTexts = true;
    std::map<std::size_t, std::string> texts = HelpTexts();
    texts[160] = std::string(100, 'x');
    m_texts = Client::Tests::MakeTextTable(texts);
    cutsAt("Q-C613");
    m_texts = Client::Tests::MakeTextTable(HelpTexts());
    m_withSets = false;
    cutsAt("0x40cf13");
}

// 0x5afb00 con un texto vacío en Text.bmd: el renglón 0 queda vacío y su caja (0x5af090 hasta el primero vacío) no
// dibuja nada; la columna sigue igual.
TEST_P(GameItemHelpTest, AnEmptyTitleDrawsNothing)
{
    std::map<std::size_t, std::string> texts = HelpTexts();
    texts.erase(1931);
    m_texts = Client::Tests::MakeTextTable(texts);
    ASSERT_TRUE(Help(Scroll).HasValue());
    ASSERT_EQ(m_text.texts.size(), 3U + 18U);
    EXPECT_EQ(m_text.texts[4].text, "+0");
    EXPECT_EQ(m_text.texts[5].text, " 30");
}

INSTANTIATE_TEST_SUITE_P(LorenciaAndDevias, GameItemHelpTest, ::testing::Values(0, 2));
} // namespace
