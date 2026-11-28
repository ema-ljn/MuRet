#include "Tooltips/GameTooltips.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Tables/ItemSetTables.h"
#include "Tables/TextTable.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Kernel/ActorState.h"
#include "Ui/GameHudContext.h"
#include "Player/ItemSetState.h"
#include "Tooltips/TooltipLines.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/Widget.h"
#include "Audio/SoundId.h"
#include "support/ItemSetFiles.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"
#include "Ui/PanelRegistry.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The tooltips (0x5f7bc0), their box (0x5af090), the set window (0x40e850) and a set's label (0x40eca0), CL8
// milestone 5, with invented texts and sets and the fake text of RecordingTextDrawer (6 columns per letter, 13 high) at
// 640 × 480, where rx and ry are 1.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::WorldState;
using Client::Render::BlendMode;
using Client::Render::Color;
using Client::Render::SpriteBatch;
using Client::Tooltips::TooltipLines;
using Client::Ui::GameHudContext;

constexpr Client::Core::PixelSize Screen640{640, 480};
constexpr std::uint8_t None = 0xff;

// The format texts of a set's options (0x40d530) and of the title (1089 and 159), invented.
Client::Tables::TextTable Texts()
{
    return Client::Tests::MakeTextTable({{159, "fin"},
                                         {632, "Magia +% d %%"},
                                         {950, "Fuerza +%d"},
                                         {951, "Agilidad +%d"},
                                         {970, "Ignora %d%%"},
                                         {957, "Opcion ocho %d"},
                                         {1089, "Set"}});
}

// ItemSetOption.bmd in clear, the 64 unused (0xff), like the real file.
class SetOptions
{
public:
    SetOptions() : m_plain(Client::Tables::ItemSetOptionTableSize, std::byte{None}) {}

    SetOptions& Named(std::size_t set, std::string_view name, std::uint8_t pieces)
    {
        for (std::size_t i = 0; i < name.size(); ++i)
            At(set, i) = static_cast<std::byte>(name[i]);
        At(set, name.size()) = std::byte{0};
        At(set, 0x3c) = std::byte{pieces};
        return *this;
    }

    SetOptions& Byte(std::size_t set, std::size_t offset, std::uint8_t value)
    {
        At(set, offset) = std::byte{value};
        return *this;
    }

    [[nodiscard]] Client::Tables::ItemSetTables Tables() const
    {
        return Client::Tests::ItemSetTablesFrom(
            std::vector<std::byte>(Client::Tables::ItemSetTypeTableSize, std::byte{0}), m_plain);
    }

private:
    std::byte& At(std::size_t set, std::size_t offset)
    {
        return m_plain[set * Client::Tables::ItemSetOptionRecordSize + offset];
    }

    std::vector<std::byte> m_plain;
};

class Tooltips
{
public:
    explicit Tooltips(const Client::Tables::ItemSetOptionTable* setOptions = nullptr) : m_setOptions(setOptions)
    {
        sprites.Begin(Screen640);
    }

    GameHudContext Context(bool projecting = false)
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
                              .setOptions = m_setOptions,
                              .mouse = mouse};
    }

    TooltipLines& Lines()
    {
        return world.Runtime().hud.tooltip;
    }

    WorldState world;
    CrtRandom random;
    std::optional<Client::Audio::SoundId> sound;
    SpriteBatch sprites;
    Client::Tests::RecordingTextDrawer text{sprites};
    Client::Tables::TextTable texts = Texts();
    Client::Ui::Point mouse{};

private:
    const Client::Tables::ItemSetOptionTable* m_setOptions = nullptr;
};

struct Quad
{
    Color color{};
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

// The untextured quads (the edges and the background of 0x621aa0), with their color.
std::vector<Quad> ColorQuadsOf(const SpriteBatch& sprites)
{
    std::vector<Quad> quads;
    const auto vertices = sprites.Vertices();
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        if (command.texture)
            continue;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
        {
            const std::size_t first = command.firstVertex + quad * 6;
            // In GL pixels, with y upward: corner 0 is the top-left one.
            quads.push_back(Quad{.color = vertices[first].color,
                                 .left = vertices[first].x,
                                 .top = 480.0f - vertices[first].y,
                                 .right = vertices[first + 2].x,
                                 .bottom = 480.0f - vertices[first + 2].y});
        }
    }
    return quads;
}

// The color of each text's mark: the glColor3f there was when drawing it.
std::vector<Color> TextColorsOf(const SpriteBatch& sprites)
{
    std::vector<Color> colors;
    const auto vertices = sprites.Vertices();
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        if (command.texture != Client::Tests::TextMarkerSlot)
            continue;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
            colors.push_back(vertices[command.firstVertex + quad * 6].color);
    }
    return colors;
}

void ExpectColor(const Color& color, float red, float green, float blue, float alpha = 1.0f)
{
    EXPECT_FLOAT_EQ(color.red, red);
    EXPECT_FLOAT_EQ(color.green, green);
    EXPECT_FLOAT_EQ(color.blue, blue);
    EXPECT_FLOAT_EQ(color.alpha, alpha);
}

void ExpectQuad(const Quad& quad, float left, float top, float width, float height)
{
    EXPECT_FLOAT_EQ(quad.left, left);
    EXPECT_FLOAT_EQ(quad.top, top);
    EXPECT_FLOAT_EQ(quad.right, left + width);
    EXPECT_FLOAT_EQ(quad.bottom, top + height);
}

// 0x5af090: three lines ("\n", a gold bold one and a blue one) at (320, 100) with the frame.
TEST(GameTooltipsTest, TheBoxMeasuresFramesAndCentersEachLine)
{
    Tooltips tested;
    TooltipLines& lines = tested.Lines();
    lines.text[0] = "\n";
    lines.text[1] = "Uno";
    lines.colors[1] = Client::Tooltips::TooltipGold;
    lines.bold[1] = 1;
    lines.text[2] = "Dos";
    lines.colors[2] = Client::Tooltips::TooltipBlue;
    GameHudContext context = tested.Context();
    const auto drawn = Client::Tooltips::DrawTheTooltipBox(context, 320, 100, 3, 0, 2, 1);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();

    // The width, 18 / rx + 4 = 22, centered on 320: from 309. The height, (13 · 0.5 + 2 · 13) / 0.90909088.
    const float height = static_cast<float>(32.5 / static_cast<double>(0.90909088f));
    const std::vector<Quad> quads = ColorQuadsOf(tested.sprites);
    ASSERT_EQ(quads.size(), 5u);
    ExpectQuad(quads[0], 308.0f, 99.0f, 23.0f, 1.0f);
    ExpectQuad(quads[1], 308.0f, 99.0f, 1.0f, height + 1.0f);
    ExpectQuad(quads[2], 331.0f, 99.0f, 1.0f, height + 1.0f);
    ExpectQuad(quads[3], 308.0f, 100.0f + height, 24.0f, 1.0f);
    ExpectQuad(quads[4], 309.0f, 100.0f, 22.0f, height);
    for (std::size_t i = 0; i < 4; ++i)
        ExpectColor(quads[i].color, 0.0f, 0.0f, 0.0f);
    ExpectColor(quads[4].color, 0.0f, 0.0f, 0.0f, 0.8f);

    // The "\n" moves down 6.5 · 1.1; each text goes in the box of 20 columns from 310 (0x467060 with mode 2), shifted
    // (20 − 18) / 2, and moves down 13 · 1.1.
    const auto& texts = tested.text.texts;
    ASSERT_EQ(texts.size(), 2u);
    EXPECT_EQ(texts[0].text, "Uno");
    EXPECT_EQ(texts[0].x, 310);
    EXPECT_EQ(texts[0].y, 107);
    EXPECT_TRUE(texts[0].windowed);
    EXPECT_EQ(texts[0].firstColumn, -1);
    EXPECT_EQ(texts[0].columns, 20);
    EXPECT_EQ(texts[0].style.font, Client::Render::Text::FontKind::Bold);
    EXPECT_EQ(texts[1].text, "Dos");
    EXPECT_EQ(texts[1].x, 310);
    EXPECT_EQ(texts[1].y, 121);
    EXPECT_EQ(texts[1].firstColumn, -1);
    EXPECT_EQ(texts[1].columns, 20);
    EXPECT_EQ(texts[1].style.font, Client::Render::Text::FontKind::Normal);
    EXPECT_EQ(texts[1].style.colors.text, 0xffffffffu);
    EXPECT_EQ(texts[1].style.colors.background, 0u);
    const std::vector<Color> colors = TextColorsOf(tested.sprites);
    ASSERT_EQ(colors.size(), 2u);
    ExpectColor(colors[0], 1.0f, 0.8f, 0.1f);
    ExpectColor(colors[1], 0.5f, 0.7f, 1.0f);
    EXPECT_EQ(tested.sprites.Blend(), BlendMode::Opaque);
}

// 0x5af0c5: the first empty line cuts the box; 0x5af3d7: 9 is green on red, and 5 has a blue background.
TEST(GameTooltipsTest, AnEmptyLineEndsTheBoxAndSomeColorsCarryABackground)
{
    Tooltips tested;
    TooltipLines& lines = tested.Lines();
    lines.text[0] = "Set";
    lines.colors[0] = Client::Tooltips::TooltipGreenOnRed;
    lines.text[1] = "Azul";
    lines.colors[1] = Client::Tooltips::TooltipWhiteOnBlue;
    lines.text[3] = "No sale";
    GameHudContext context = tested.Context();
    ASSERT_TRUE(Client::Tooltips::DrawTheTooltipBox(context, 0, 0, 5, 0, 2, 0).HasValue());
    const auto& texts = tested.text.texts;
    ASSERT_EQ(texts.size(), 2u);
    EXPECT_EQ(texts[0].style.colors.text, 0xff00ff00u);
    EXPECT_EQ(texts[0].style.colors.background, 0xffc83c3cu);
    EXPECT_EQ(texts[1].style.colors.background, 0xff0000a0u);
    // Without a frame, no untextured quad; against the left edge, the box starts at 0 and the text at 1. The background
    // fills the box's 26 columns, also those beside the text (P-C145).
    EXPECT_TRUE(ColorQuadsOf(tested.sprites).empty());
    EXPECT_EQ(texts[0].x, 1);
    EXPECT_EQ(texts[0].firstColumn, -(26 - 18) / 2);
    EXPECT_EQ(texts[0].columns, 26);
    EXPECT_EQ(texts[1].x, 1);
    EXPECT_EQ(texts[1].firstColumn, -(26 - 24) / 2);
    EXPECT_EQ(texts[1].columns, 26);
}

// 0x5af090 with mode 3, the one of the item help's columns (0x5af9b0): each text against the box's right edge
// (0x467134..0x467144), at column box − cx of a box of exactly 20 columns, with the background over all of it.
TEST(GameTooltipsTest, TheModeThreePutsEachLineAgainstTheRightEdge)
{
    Tooltips tested;
    TooltipLines& lines = tested.Lines();
    lines.text[0] = "1";
    lines.colors[0] = Client::Tooltips::TooltipRed;
    lines.text[1] = "100";
    lines.colors[1] = Client::Tooltips::TooltipWhiteOnRed;
    GameHudContext context = tested.Context();
    ASSERT_TRUE(Client::Tooltips::DrawTheTooltipBox(context, 100, 50, 2, 0, 3, 0).HasValue());
    // The width, 18 + 4, centered on 100: from 89, and the texts from 90 in 20 columns.
    const auto& texts = tested.text.texts;
    ASSERT_EQ(texts.size(), 2u);
    EXPECT_EQ(texts[0].x, 90);
    EXPECT_EQ(texts[0].y, 50);
    EXPECT_EQ(texts[0].firstColumn, 6 - 20);
    EXPECT_EQ(texts[0].columns, 20);
    EXPECT_EQ(texts[0].style.colors.background, 0u);
    EXPECT_EQ(texts[1].x, 90);
    EXPECT_EQ(texts[1].y, 64);
    EXPECT_EQ(texts[1].firstColumn, 18 - 20);
    EXPECT_EQ(texts[1].columns, 20);
    EXPECT_EQ(texts[1].style.colors.background, 0xffa00000u);
    EXPECT_TRUE(ColorQuadsOf(tested.sprites).empty());
    const std::vector<Color> colors = TextColorsOf(tested.sprites);
    ASSERT_EQ(colors.size(), 2u);
    ExpectColor(colors[0], 1.0f, 0.2f, 0.1f);
    ExpectColor(colors[1], 1.0f, 1.0f, 1.0f);
    EXPECT_EQ(tested.sprites.Blend(), BlendMode::Opaque);
}

// 0x5af090 with mode 1, the one of the cards' portrait help (0x497f3f): each text against the box's left edge
// (0x467102..0x467119), at column 0 of a box of exactly 20 columns, with the frame.
TEST(GameTooltipsTest, TheModeOnePutsEachLineAgainstTheLeftEdge)
{
    Tooltips tested;
    TooltipLines& lines = tested.Lines();
    lines.text[0] = "1";
    lines.text[1] = "100";
    lines.colors[1] = Client::Tooltips::TooltipWhiteOnRed;
    GameHudContext context = tested.Context();
    ASSERT_TRUE(Client::Tooltips::DrawTheTooltipBox(context, 100, 50, 2, 0, 1, 1).HasValue());
    // The width, 18 + 4, centered on 100: from 89, and the texts from 90 in 20 columns, at the first one.
    const auto& texts = tested.text.texts;
    ASSERT_EQ(texts.size(), 2u);
    EXPECT_EQ(texts[0].x, 90);
    EXPECT_EQ(texts[0].y, 50);
    EXPECT_EQ(texts[0].firstColumn, 0);
    EXPECT_EQ(texts[0].columns, 20);
    EXPECT_EQ(texts[1].x, 90);
    EXPECT_EQ(texts[1].y, 64);
    EXPECT_EQ(texts[1].firstColumn, 0);
    EXPECT_EQ(texts[1].columns, 20);
    EXPECT_EQ(texts[1].style.colors.background, 0xffa00000u);
    const std::vector<Quad> quads = ColorQuadsOf(tested.sprites);
    ASSERT_EQ(quads.size(), 5u);
    ExpectQuad(quads[4], 89.0f, 50.0f, 22.0f, quads[4].bottom - 50.0f);
    EXPECT_EQ(tested.sprites.Blend(), BlendMode::Opaque);
    // Another mode cuts before drawing.
    EXPECT_FALSE(Client::Tooltips::DrawTheTooltipBox(context, 100, 50, 2, 0, 4, 1).HasValue());
}

// At 800 × 600 (rx = 1.25) the text box is in pixels and x is the 640 × 480 one without going through the screen: with
// "Uno" (18 pixels) at x 320, the box measures 18 / 1.25 + 4 = 18.4 from 310 and the text goes from 311, in a box of
// _ftol(16.4 · 1.25) = 20 columns. With mode 3, the text at column 20 − 18.
TEST(GameTooltipsTest, TheTextBoxIsInPixelsAtAnotherResolution)
{
    for (const std::int32_t mode : {2, 3})
    {
        Tooltips tested;
        tested.sprites.Begin(Client::Core::PixelSize{800, 600});
        TooltipLines& lines = tested.Lines();
        lines.text[0] = "Uno";
        lines.colors[0] = Client::Tooltips::TooltipGreenOnRed;
        GameHudContext context = tested.Context();
        ASSERT_TRUE(Client::Tooltips::DrawTheTooltipBox(context, 320, 100, 1, 0, mode, 0).HasValue()) << mode;
        const auto& texts = tested.text.texts;
        ASSERT_EQ(texts.size(), 1u) << mode;
        EXPECT_EQ(texts[0].x, 311) << mode;
        EXPECT_EQ(texts[0].y, 100) << mode;
        EXPECT_EQ(texts[0].firstColumn, mode == 2 ? -1 : -2) << mode;
        EXPECT_EQ(texts[0].columns, 20) << mode;
        EXPECT_EQ(texts[0].style.colors.background, 0xffc83c3cu) << mode;
    }
}

// 0x40d530: the option picks the format text; with one above 0x23 the line stays.
TEST(GameTooltipsTest, ASetOptionPicksItsFormatText)
{
    const Client::Tables::TextTable texts = Texts();
    std::string line = "viejo";
    ASSERT_TRUE(Client::Tooltips::WriteTheSetOption(&texts, line, 0, 12).HasValue());
    EXPECT_EQ(line, "Fuerza +12");
    ASSERT_TRUE(Client::Tooltips::WriteTheSetOption(&texts, line, 7, 3).HasValue());
    EXPECT_EQ(line, "Magia + 3 %");
    ASSERT_TRUE(Client::Tooltips::WriteTheSetOption(&texts, line, 8, 4).HasValue());
    EXPECT_EQ(line, "Opcion ocho 4");
    ASSERT_TRUE(Client::Tooltips::WriteTheSetOption(&texts, line, 0x15, 9).HasValue());
    EXPECT_EQ(line, "Ignora 9%");
    ASSERT_TRUE(Client::Tooltips::WriteTheSetOption(&texts, line, 0x24, 9).HasValue());
    EXPECT_EQ(line, "Ignora 9%");
    EXPECT_FALSE(Client::Tooltips::WriteTheSetOption(nullptr, line, 2, 9).HasValue());
}

// 0x40e850 from 0x5f7bc0: with the inventory open and +0x9304, the worn set and its options; one of 0xa5 or more
// without the mark goes in red and does not write its text. Then, +0x9304 at 0.
TEST(GameTooltipsTest, TheSetWindowListsTheWornSetAndTurnsItselfOff)
{
    Tooltips tested;
    Client::Kernel::WorldRuntime& runtime = tested.world.Runtime();
    runtime.panels.inventory = 1;
    runtime.hud.inventoryX = 0x1c2;
    Client::Player::ItemSetState& sets = tested.world.ItemSets();
    sets.byte9304 = 1;
    sets.names[0] = {'A', 'l', 'f', 'a'};
    sets.list.count9348 = 2;
    sets.list.entries[0] = {0, 1, 0};
    sets.list.values[0] = 0x105;
    sets.list.entries[1] = {0xa5, 0, 0};
    sets.list.values[1] = 3;
    tested.Lines().text[4] = "viejo";
    GameHudContext context = tested.Context();
    const auto drawn = Client::Tooltips::DrawTheTooltips(context);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();

    const TooltipLines& lines = tested.Lines();
    EXPECT_EQ(lines.text[0], "\n");
    EXPECT_EQ(lines.text[3], "Alfa Set");
    EXPECT_EQ(lines.colors[3], Client::Tooltips::TooltipGold);
    EXPECT_EQ(lines.bold[3], 1);
    EXPECT_EQ(lines.text[4], "Fuerza +5");
    EXPECT_EQ(lines.colors[4], Client::Tooltips::TooltipBlue);
    EXPECT_EQ(lines.text[5], "");
    EXPECT_EQ(lines.colors[5], Client::Tooltips::TooltipRed);
    EXPECT_EQ(sets.byte9304, 0);
    // The empty line cuts the box: the title and the option come out.
    ASSERT_EQ(tested.text.texts.size(), 2u);
    EXPECT_EQ(tested.text.texts[0].text, "Alfa Set");
}

// A MuRet intermediate drawing draws the window but does not turn off +0x9304.
TEST(GameTooltipsTest, AProjectedFrameKeepsTheSetWindowOn)
{
    Tooltips tested;
    tested.world.Runtime().panels.inventory = 1;
    Client::Player::ItemSetState& sets = tested.world.ItemSets();
    sets.byte9304 = 1;
    sets.list.count9349 = 1;
    sets.names[1] = {'B'};
    GameHudContext context = tested.Context(true);
    ASSERT_TRUE(Client::Tooltips::DrawTheTooltips(context).HasValue());
    EXPECT_EQ(sets.byte9304, 1);
    EXPECT_EQ(tested.Lines().text[3], "\n");
    EXPECT_EQ(tested.Lines().text[4], "B Set");
}

// 0x40eca0: the name with 1089 and 159, the pairs, the extras and the full set, in that order.
TEST(GameTooltipsTest, TheSetLabelListsTheOptionsOfTheSet)
{
    SetOptions options;
    options.Named(1, "Beta", 3)
        .Byte(1, 0x20, 0)
        .Byte(1, 0x2c, 10)
        .Byte(1, 0x21, 1)
        .Byte(1, 0x2d, 20)
        .Byte(1, 0x38, 7)
        .Byte(1, 0x3a, 2)
        .Byte(1, 0x3d, 0x15)
        .Byte(1, 0x42, 6);
    const Client::Tables::ItemSetTables tables = options.Tables();
    Tooltips tested(&tables.options);
    tested.world.ItemSets().byte9305 = 2;
    tested.Lines().colors[25] = 4;
    // Q-C596: the bold that a previous body left on the title stays.
    tested.Lines().bold[1] = 1;
    GameHudContext context = tested.Context();
    const auto drawn = Client::Tooltips::DrawTheSetLabel(context);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();

    const TooltipLines& lines = tested.Lines();
    const std::vector<std::string> expected{"\n",           "Beta Set fin", "\n",        "\n", "Fuerza +10",
                                            "Agilidad +20", "Magia + 2 %",  "Ignora 6%", "\n", "\n"};
    for (std::size_t i = 0; i < expected.size(); ++i)
        EXPECT_EQ(lines.text[i], expected[i]) << i;
    EXPECT_EQ(lines.text[expected.size()], "");
    EXPECT_EQ(lines.colors[1], Client::Tooltips::TooltipGold);
    EXPECT_EQ(lines.colors[4], Client::Tooltips::TooltipBlue);
    EXPECT_EQ(lines.colors[7], Client::Tooltips::TooltipBlue);
    // It clears 20 colors of the 30: 25 stays.
    EXPECT_EQ(lines.colors[25], 4);
    EXPECT_EQ(lines.bold[1], 1);
    EXPECT_EQ(tested.world.ItemSets().byte9305, 2);
    // The box goes top left: the widest ("Agilidad +20", 72) + 4, against 0.
    ASSERT_FALSE(tested.text.texts.empty());
    EXPECT_EQ(tested.text.texts[0].text, "Beta Set fin");
    EXPECT_EQ(tested.text.texts[0].x, 1);
    EXPECT_EQ(tested.text.texts[0].firstColumn, -(74 - 72) / 2);
    EXPECT_EQ(tested.text.texts[0].columns, 74);
}

// 0x40ed11..0x40ee15 (Q-C592): after the sixth pair the second option stays that pair's, and comes out again in each
// of the seven places that follow.
TEST(GameTooltipsTest, TheSecondOptionOfTheSixthPairRepeatsInTheSetLabel)
{
    SetOptions options;
    options.Named(0, "Gamma", 2).Byte(0, 0x2b, 1).Byte(0, 0x37, 9);
    const Client::Tables::ItemSetTables tables = options.Tables();
    Tooltips tested(&tables.options);
    tested.world.ItemSets().byte9305 = 1;
    GameHudContext context = tested.Context();
    ASSERT_TRUE(Client::Tooltips::DrawTheSetLabel(context).HasValue());
    const TooltipLines& lines = tested.Lines();
    for (std::size_t i = 4; i < 12; ++i)
        EXPECT_EQ(lines.text[i], "Agilidad +9") << i;
    EXPECT_EQ(lines.text[12], "\n");
    EXPECT_EQ(lines.text[13], "\n");
}

// 0x40ecf3: an unused set turns the label off without drawing anything.
TEST(GameTooltipsTest, AnUnusedSetTurnsTheLabelOff)
{
    const Client::Tables::ItemSetTables tables = SetOptions().Tables();
    Tooltips tested(&tables.options);
    tested.world.ItemSets().byte9305 = 3;
    GameHudContext context = tested.Context();
    ASSERT_TRUE(Client::Tooltips::DrawTheSetLabel(context).HasValue());
    EXPECT_EQ(tested.world.ItemSets().byte9305, 0);
    EXPECT_TRUE(tested.text.texts.empty());
}

// 0x5f7bc0: with the repair mode at 2, it goes back to 1 (outside an intermediate drawing) and turns off 0x77f3b66.
TEST(GameTooltipsTest, TheTooltipsResetTheRepairModeAndTheDurabilityWarning)
{
    Tooltips tested;
    Client::Kernel::WorldRuntime& runtime = tested.world.Runtime();
    runtime.panels.repairMode = 2;
    runtime.hud.durabilityWarning = 1;
    GameHudContext projected = tested.Context(true);
    ASSERT_TRUE(Client::Tooltips::DrawTheTooltips(projected).HasValue());
    EXPECT_EQ(runtime.panels.repairMode, 2);
    GameHudContext context = tested.Context();
    ASSERT_TRUE(Client::Tooltips::DrawTheTooltips(context).HasValue());
    EXPECT_EQ(runtime.panels.repairMode, 1);
    EXPECT_EQ(runtime.hud.durabilityWarning, 0);
}
} // namespace
