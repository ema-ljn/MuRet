#include "Tooltips/GameHelps.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Tables/ItemSetTables.h"
#include "Tables/ItemTable.h"
#include "Tables/TextTable.h"
#include "Models/LogicalAccess.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Kernel/ActorState.h"
#include "Hud/GameHud.h"
#include "Tooltips/ItemHelpLevels.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Tooltips/TooltipLines.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/PanelRegistry.h"
#include "Audio/SoundId.h"
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

// The item help (CL8; 06 § HUD y ventanas, "La ayuda del ítem"): mode 3 of 0x5b0550, with the per-level table of
// 0x5afc00, the top box (0x5af090, framed) and the columns of 0x5afb00 and 0x5af9b0. The text measures like the fake
// rasterizer: 6 columns per letter and 13 rows. The items, the texts and the character are invented.
namespace
{
using Client::Crt::CrtRandom;
using Client::Hud::GameHudContext;
using Client::Kernel::WorldState;
using Client::Render::BlendMode;
using Client::Render::Color;
using Client::Render::SpriteBatch;
using Client::Render::Text::FontKind;
using Client::Tests::RecordedText;
using Client::Tests::RecordingTextDrawer;
using Client::Tests::TextMarkerSlot;
using Client::Tooltips::ItemHelpLevels;

constexpr Client::Core::PixelSize Screen640{640, 480};
constexpr Client::Core::PixelSize Screen800{800, 600};
constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};
constexpr Color Red{1.0f, 0.2f, 0.1f, 1.0f};
constexpr Color Green{0.1f, 1.0f, 0.5f, 1.0f};
constexpr Color LightBlue{0.5f, 0.7f, 1.0f, 1.0f};
// A new table: 14 closes of the own record, of 3848 rand() each.
constexpr std::uint32_t FillRandoms = 14 * Client::Models::OwnRecordCloseRandoms;

// A sword (range 1), a shield and a set armor (3 and 4), a staff and a scepter, a scroll (5) and an item with
// requirements that wrap.
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

// Item.bmd in clear.
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

    ItemRecords& Word(std::uint16_t type, std::size_t offset, std::uint16_t value)
    {
        At(type, offset) = static_cast<std::byte>(value & 0xffU);
        At(type, offset + 1) = static_cast<std::byte>(value >> 8U);
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
        // Damage 10 to 15, level 20, strength 40, agility 30 and energy 10; class 0 uses it.
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
        // Energy 15 and level 30, from level 10.
        .Named(Scroll, "Pergamino")
        .Word(Scroll, 0x20, 10)
        .Word(Scroll, 0x34, 15)
        .Word(Scroll, 0x3a, 30)
        .Named(Heavy, "Mole")
        .Word(Heavy, 0x20, 0xffff)
        .Word(Heavy, 0x30, 0xffff)
        .Table();
}

// The set armor: 3 in piece 1 and 5 in piece 2, with mixes 1 (byte 2) and 2 (byte 3).
Client::Tables::ItemSetTables Sets()
{
    std::vector<std::byte> types(Client::Tables::ItemSetTypeTableSize);
    const std::size_t type = std::size_t{SetArmor} * Client::Tables::ItemSetTypeRecordSize;
    types[type] = std::byte{FirstSet};
    types[type + 1] = std::byte{SecondSet};
    types[type + 2] = std::byte{1};
    types[type + 3] = std::byte{2};
    std::vector<std::byte> options(Client::Tables::ItemSetOptionTableSize, std::byte{0xff});
    const auto name = [&](std::uint8_t set, std::string_view text)
    {
        const std::size_t base = std::size_t{set} * Client::Tables::ItemSetOptionRecordSize;
        for (std::size_t i = 0; i < text.size(); ++i)
            options[base + i] = static_cast<std::byte>(text[i]);
        options[base + text.size()] = std::byte{0};
    };
    name(FirstSet, "Conjunto Gris");
    name(SecondSet, "Otro");
    return Client::Tests::ItemSetTablesFrom(std::move(types), std::move(options));
}

// The color of each text's mark, in order.
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

// The framed box's background: the fifth untextured quad (the four edges go first).
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
        Client::Kernel::WorldRuntime& runtime = m_world->Runtime();
        runtime.map = GetParam();
        runtime.panels.overlayMode = Client::Ui::ItemHelpMode;
        m_world->Hero().characterClass = 0;
        Client::Kernel::OwnCharacterRecord& own = m_world->OwnCharacter();
        own.level = 10;
        own.strength = 50;
        own.word2a = 5;
        own.agility = 60;
        own.energy = 100;
    }

    GameHudContext Context()
    {
        Client::Kernel::WorldRuntime& runtime = m_world->Runtime();
        Client::Kernel::ActorState& hero = m_world->Hero();
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
        return Client::Hud::DrawTheOverlays(context);
    }

    [[nodiscard]] Client::Kernel::HudState& Hud()
    {
        return m_world->Runtime().hud;
    }

    [[nodiscard]] const ItemHelpLevels& Levels()
    {
        return Hud().itemHelp;
    }

    // The first drawn text `text`, or the list's size.
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
    std::optional<Client::Audio::SoundId> m_sound{};
    SpriteBatch m_sprites;
    RecordingTextDrawer m_text;
    Client::Tables::TextTable m_texts = Client::Tests::MakeTextTable(HelpTexts());
    Client::Tables::ItemTable m_items = Items();
    Client::Tables::ItemSetTables m_sets = Sets();
    bool m_withTexts = true;
    bool m_withItems = true;
    bool m_withSets = true;
};

// 0x5afc00: each level adds 3 up to 9 and 4, 9, 15 and 22 more from 10 to 13; the requirements, (20 + 3 · level) ·
// requirement · 3 (4 for energy) / 100 + 20; column 1, whether the own record meets them (strength 50 + 5).
TEST_P(GameItemHelpTest, TheTableHasEachLevel)
{
    ASSERT_TRUE(Help(Sword).HasValue());
    const ItemHelpLevels& levels = Levels();
    EXPECT_EQ(levels.item, Sword);
    using Client::Tooltips::ItemHelpRow;
    const ItemHelpRow& first = levels.rows[0];
    EXPECT_EQ(first[Client::Tooltips::ItemHelpLevel], 0);
    EXPECT_EQ(first[Client::Tooltips::ItemHelpMinDamage], 10);
    EXPECT_EQ(first[Client::Tooltips::ItemHelpMaxDamage], 15);
    EXPECT_EQ(first[Client::Tooltips::ItemHelpStrength], 44);
    EXPECT_EQ(first[Client::Tooltips::ItemHelpAgility], 38);
    EXPECT_EQ(first[Client::Tooltips::ItemHelpEnergy], 28);
    EXPECT_EQ(first[Client::Tooltips::ItemHelpUsable], 1);
    EXPECT_EQ(levels.rows[9][Client::Tooltips::ItemHelpMinDamage], 37);
    EXPECT_EQ(levels.rows[10][Client::Tooltips::ItemHelpMinDamage], 41);
    EXPECT_EQ(levels.rows[11][Client::Tooltips::ItemHelpMinDamage], 46);
    EXPECT_EQ(levels.rows[12][Client::Tooltips::ItemHelpMinDamage], 52);
    const ItemHelpRow& last = levels.rows[13];
    EXPECT_EQ(last[Client::Tooltips::ItemHelpLevel], 13);
    EXPECT_EQ(last[Client::Tooltips::ItemHelpMinDamage], 59);
    EXPECT_EQ(last[Client::Tooltips::ItemHelpMaxDamage], 64);
    EXPECT_EQ(last[Client::Tooltips::ItemHelpStrength], 90);
    EXPECT_EQ(last[Client::Tooltips::ItemHelpAgility], 73);
    EXPECT_EQ(last[Client::Tooltips::ItemHelpEnergy], 43);
    // Strength asks for 54 at level 3 and 58 at 4.
    EXPECT_EQ(levels.rows[3][Client::Tooltips::ItemHelpUsable], 1);
    EXPECT_EQ(levels.rows[4][Client::Tooltips::ItemHelpUsable], 0);
    EXPECT_EQ(last[Client::Tooltips::ItemHelpUsable], 0);
    // The columns nobody writes.
    EXPECT_EQ(first[11], 0);
    EXPECT_EQ(first[12], 0);
    EXPECT_EQ(first[16], 0);
}

// 0x5afce1..0x5afd8d: the power halved plus 2 · level (except the scepters, in column 5), the shield's defense plus the
// level and the others' like the damage; 0x5afdea: in signed 32 bits, a requirement that wraps stays negative and asks
// for nothing.
TEST_P(GameItemHelpTest, TheMagicTheDefenseAndTheWrappedRequirements)
{
    ASSERT_TRUE(Help(Staff).HasValue());
    EXPECT_EQ(Levels().rows[0][Client::Tooltips::ItemHelpMagic], 6);
    EXPECT_EQ(Levels().rows[13][Client::Tooltips::ItemHelpMagic], 30 + 26);
    EXPECT_EQ(Levels().rows[13][Client::Tooltips::ItemHelpScepterMagic], 0);
    ASSERT_TRUE(Help(Scepter).HasValue());
    EXPECT_EQ(Levels().rows[13][Client::Tooltips::ItemHelpMagic], 0);
    EXPECT_EQ(Levels().rows[13][Client::Tooltips::ItemHelpScepterMagic], 30);
    ASSERT_TRUE(Help(Shield).HasValue());
    EXPECT_EQ(Levels().rows[13][Client::Tooltips::ItemHelpDefense], 18);
    EXPECT_EQ(Levels().rows[13][Client::Tooltips::ItemHelpDefenseRate], 57);
    ASSERT_TRUE(Help(Armor).HasValue());
    EXPECT_EQ(Levels().rows[13][Client::Tooltips::ItemHelpDefense], 54);
    // 65535 · 65535 · 3 wraps to −393213: / 100 + 20.
    ASSERT_TRUE(Help(Heavy).HasValue());
    EXPECT_EQ(Levels().rows[0][Client::Tooltips::ItemHelpStrength], -3912);
    EXPECT_EQ(Levels().rows[0][Client::Tooltips::ItemHelpUsable], 1);
}

// 0x5afc09 and 0x5aff9b..0x5b04c3: the table fills once per item, with 14 closes of the own record; with the same item
// it draws nothing even if the stats change (Q-C750), and with the record already open, neither.
TEST_P(GameItemHelpTest, TheTableFillsOncePerItem)
{
    CrtRandom expected = m_random;
    ASSERT_TRUE(Help(Sword).HasValue());
    expected.Discard(FillRandoms);
    EXPECT_EQ(m_random.State(), expected.State());

    m_world->OwnCharacter().strength = 1000;
    ASSERT_TRUE(Help(Sword).HasValue());
    EXPECT_EQ(m_random.State(), expected.State());
    EXPECT_EQ(Levels().rows[13][Client::Tooltips::ItemHelpUsable], 0);

    ASSERT_TRUE(Help(Shield).HasValue());
    expected.Discard(FillRandoms);
    EXPECT_EQ(m_random.State(), expected.State());

    m_world->OwnCharacter().agility = 1000;
    m_world->Runtime().ownRecordDepth = 1;
    ASSERT_TRUE(Help(Sword).HasValue());
    EXPECT_EQ(m_random.State(), expected.State());
    EXPECT_EQ(Levels().rows[13][Client::Tooltips::ItemHelpUsable], 1);
}

// 0x5b09b0..0x5b0dea: "\n", 160 in blue and bold, the name in bold, "\n", " ", "\n", 46 spaces, 13 " ", the classes
// and "\n"; the box of 2 · 104 + 4 (the width of 6.1 columns of 17), framed, top left.
TEST_P(GameItemHelpTest, TheBoxHasTheTitleTheNameAndTheBlankRows)
{
    ASSERT_TRUE(Help(Sword).HasValue());
    EXPECT_EQ(Hud().durabilityWarning, 1);
    const Client::Tooltips::TooltipLines& lines = Hud().tooltip;
    // The lines below the columns stay as the box left them.
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

// 0x5b0dfb..0x5b10b6 at 640 × 480: from x 24 (0x870 / 90), the level, the damage (the minimum, the title, "~" and the
// maximum), the strength, the agility and the energy; the titles at y 38 and the rows at y 52, each box centered on
// its x and the text against its edge. The rows the own record does not meet, in red.
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
    // Each box: the widest + 4 from x − half; the text at left + 1, in the box's columns − 2.
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

// At 800 × 600: the same divisor (90) and the same x, with the titles at y 33 and the rows at y 47; the top box of 2 ·
// 104 / 1.25 + 4 and each column moves by what its sample measures / 1.25.
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
    // "000000" measures 36 / 1.25 = 28.8: the minimum goes at 24 + 28, with its box of 18.4 from 52 − 9.2.
    const std::size_t damage = Find(" 10");
    ASSERT_LT(damage, m_text.texts.size());
    EXPECT_EQ(m_text.texts[damage].x, 43);
    // And the energy at 52 + 14 + 14 + 24 + 24 + 24.
    const std::size_t energy = Find("Ene");
    ASSERT_LT(energy, m_text.texts.size());
    EXPECT_EQ(m_text.texts[energy].x, 143);
}

// 0x5b0bf2: with a width above 800, 51 spaces; at 1024, divisor 103 and x 2160 / 103.
TEST_P(GameItemHelpTest, AWiderScreenHasMoreSpaces)
{
    m_sprites.Begin(Client::Core::PixelSize{1024, 768});
    ASSERT_TRUE(Help(Sword).HasValue());
    EXPECT_EQ(m_text.texts[2].text, std::string(51, ' '));
    EXPECT_EQ(m_text.texts[4].text, "Niv");
}

// Range 5 (0x1e00..0x1fff): no blank lines, from x 66 (0x1734 / 90); the level and the energy with a single row and
// the required level with the 14, which pass the top box (Q-C752). No damage and no agility.
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
    // The required level (30) passes the character's (10): red.
    const std::vector<Color> colors = TextColorsOf(m_sprites);
    EXPECT_EQ(colors[21], Red);
    // The top box: 2 · 90 + 4.
    EXPECT_FLOAT_EQ(BackgroundXOf(m_sprites).second, 184.0f);
}

// 0x5b0c97..0x5b0d9f: one green line per piece with its set: 1 with byte 3 of ItemSetType.bmd and 2 with byte 2; from
// x 20 (0x708 / 90), with the defense and the agility (which is always drawn outside range 5).
TEST_P(GameItemHelpTest, ASetItemListsItsPieces)
{
    ASSERT_TRUE(Help(SetArmor).HasValue());
    const Client::Tooltips::TooltipLines& lines = Hud().tooltip;
    EXPECT_EQ(lines.text[20], "Conjunto Gris  Grupo Mezcla:(Nivel+2)");
    EXPECT_EQ(lines.text[21], "Otro  Grupo Mezcla:(Nivel+1)");
    EXPECT_EQ(lines.colors[20], Client::Tooltips::TooltipGreen);
    EXPECT_EQ(lines.colors[21], Client::Tooltips::TooltipGreen);
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

// Q-C751: the first column's title uses whatever bold is on line 0; the rows of 0x5af9b0 set it to 0, and from the
// second drawing on it goes without bold.
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

// What cuts: without Item.bmd (0x5b098f), without text 160 (0x5b09f0) or with one of 100 bytes (0x5b0a03, Q-C613),
// without ItemSetType.bmd (0x40cf13).
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

// 0x5afb00 with an empty text in Text.bmd: line 0 stays empty and its box (0x5af090 up to the first empty one) draws
// nothing; the column goes on the same.
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
