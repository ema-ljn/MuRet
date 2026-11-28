#include "Tooltips/GameHelps.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Tables/TextTable.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Kernel/ActorState.h"
#include "Hud/GameHud.h"
#include "Tooltips/GameTooltips.h"
#include "Tooltips/TooltipLines.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/PanelRegistry.h"
#include "Audio/SoundId.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// The overlay helps (CL8 milestone 6; 06 § HUD y ventanas, "Las ayudas de F1"): modes 1, 2 and 3 of 0x5b0550, which
// draw with the tooltip box (0x5af090, Tooltips::DrawTheTooltipBox). The text measures like the fake rasterizer: 6
// columns per letter and 13 rows (also the '\n'). The texts are invented.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::WorldState;
using Client::Render::BlendMode;
using Client::Render::Color;
using Client::Render::SpriteBatch;
using Client::Render::SpriteVertex;
using Client::Tests::RecordingTextDrawer;
using Client::Tests::TextMarkerSlot;
using Client::Ui::GameHudContext;

constexpr Client::Core::PixelSize Screen640{640, 480};
constexpr Color White{1.0f, 1.0f, 1.0f, 1.0f};
constexpr Color LightBlue{0.5f, 0.7f, 1.0f, 1.0f};

// The texts of the two helps: 125 is the widest of the first (24 letters, 144 columns).
std::map<std::size_t, std::string> HelpTexts()
{
    std::map<std::size_t, std::string> texts;
    texts[120] = "Ayuda";
    for (std::size_t index = 121; index <= 139; ++index)
        texts[index] = "linea " + std::to_string(index);
    texts[125] = "una linea bastante larga";
    texts[140] = "B";
    for (std::size_t index = 141; index <= 156; ++index)
        texts[index] = "otra " + std::to_string(index);
    return texts;
}

struct DrawnQuad
{
    Color color{};
    SpriteVertex first{};
    SpriteVertex third{};
};

// The batch's untextured quads (those of 0x621aa0), in order.
std::vector<DrawnQuad> ColorQuadsOf(const SpriteBatch& sprites)
{
    std::vector<DrawnQuad> quads;
    const auto vertices = sprites.Vertices();
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        if (command.texture.has_value())
            continue;
        for (std::size_t quad = 0; quad < command.vertexCount / 6; ++quad)
        {
            const std::size_t first = command.firstVertex + quad * 6;
            quads.push_back(
                DrawnQuad{.color = vertices[first].color, .first = vertices[first], .third = vertices[first + 2]});
        }
    }
    return quads;
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

class GameHelpsTest : public ::testing::Test
{
protected:
    GameHelpsTest() : m_world(std::make_unique<WorldState>()), m_text(m_sprites)
    {
        m_sprites.Begin(Screen640);
    }

    GameHudContext Context(bool projecting = false)
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
                              .projecting = projecting,
                              .sprites = m_sprites,
                              .text = m_text,
                              .texts = &m_texts};
    }

    Client::Core::Expected<void, std::string> Overlays(bool projecting = false)
    {
        GameHudContext context = Context(projecting);
        return Client::Hud::DrawTheOverlays(context);
    }

    [[nodiscard]] std::int32_t& Mode()
    {
        return m_world->Runtime().panels.overlayMode;
    }

    [[nodiscard]] Client::Kernel::HudState& Hud()
    {
        return m_world->Runtime().hud;
    }

    std::unique_ptr<WorldState> m_world;
    CrtRandom m_random{0x5eed1234u};
    std::optional<Client::Audio::SoundId> m_sound{};
    SpriteBatch m_sprites;
    RecordingTextDrawer m_text;
    Client::Tables::TextTable m_texts = Client::Tests::MakeTextTable(HelpTexts());
};

// 0x5b0559..0x5b069d: the first help's lines ("\n", the title, "\n", the 19 texts and "\n"), 0x78abb30 at 23 and
// 0x77f3b66.
TEST_F(GameHelpsTest, TheFirstHelpFillsTheTooltipLines)
{
    Mode() = Client::Ui::FirstHelpMode;
    ASSERT_TRUE(Overlays().HasValue());
    const Client::Tooltips::TooltipLines& lines = Hud().tooltip;
    ASSERT_EQ(lines.next, 23);
    EXPECT_EQ(lines.text[0], "\n");
    EXPECT_EQ(lines.text[1], "Ayuda");
    EXPECT_EQ(lines.colors[1], Client::Tooltips::TooltipBlue);
    EXPECT_EQ(lines.bold[1], 1);
    EXPECT_EQ(lines.text[2], "\n");
    EXPECT_EQ(lines.text[3], "linea 121");
    EXPECT_EQ(lines.colors[3], Client::Tooltips::TooltipWhite);
    EXPECT_EQ(lines.bold[3], 0);
    EXPECT_EQ(lines.text[21], "linea 139");
    EXPECT_EQ(lines.text[22], "\n");
    EXPECT_EQ(Hud().durabilityWarning, 1);
    EXPECT_EQ(Mode(), Client::Ui::FirstHelpMode);
}

// 0x5af090(1, 1, 23, 0, 2, 1): the box of 144 + 4 against the left edge, (3 · 13 · 0.5 + 20 · 13) / 0.9090909 high,
// with the black frame and the background at 80 %; each text centered in the box of 146 columns from x 1 and moving
// down 1.1 times its height (the '\n', half). The title in bold and light blue; the rest in white. It ends in white and
// without blending.
TEST_F(GameHelpsTest, TheFirstHelpDrawsItsBox)
{
    Mode() = Client::Ui::FirstHelpMode;
    ASSERT_TRUE(Overlays().HasValue());

    const std::vector<DrawnQuad> quads = ColorQuadsOf(m_sprites);
    ASSERT_EQ(quads.size(), 5U);
    for (std::size_t i = 0; i < 4; ++i)
        EXPECT_EQ(quads[i].color, (Color{0.0f, 0.0f, 0.0f, 1.0f})) << i;
    EXPECT_EQ(quads[4].color, (Color{0.0f, 0.0f, 0.0f, 0.8f}));
    // The background: x from 0 to 148 and the GL y from 480 − 1 to 480 − (1 + 307.45).
    EXPECT_FLOAT_EQ(quads[4].first.x, 0.0f);
    EXPECT_FLOAT_EQ(quads[4].third.x, 148.0f);
    EXPECT_FLOAT_EQ(quads[4].first.y, 479.0f);
    EXPECT_NEAR(quads[4].third.y, 480.0f - 308.45f, 1e-3f);
    // The right frame, at x = −1 + 148 + 1.
    EXPECT_FLOAT_EQ(quads[2].first.x, 148.0f);

    const auto& texts = m_text.texts;
    ASSERT_EQ(texts.size(), 20U);
    EXPECT_EQ(texts[0].text, "Ayuda");
    EXPECT_EQ(texts[0].x, 1);
    EXPECT_EQ(texts[0].firstColumn, -(146 - 30) / 2);
    EXPECT_EQ(texts[0].columns, 146);
    EXPECT_EQ(texts[0].y, 8);
    EXPECT_EQ(texts[0].style.font, Client::Render::Text::FontKind::Bold);
    EXPECT_EQ(texts[0].style.colors.text, 0xffffffffu);
    EXPECT_EQ(texts[0].style.colors.background, 0u);
    EXPECT_EQ(texts[1].text, "linea 121");
    EXPECT_EQ(texts[1].x, 1);
    EXPECT_EQ(texts[1].firstColumn, -(146 - 54) / 2);
    EXPECT_EQ(texts[1].columns, 146);
    EXPECT_EQ(texts[1].y, 29);
    EXPECT_EQ(texts[1].style.font, Client::Render::Text::FontKind::Normal);
    EXPECT_EQ(texts[19].text, "linea 139");
    EXPECT_EQ(texts[19].y, 287);

    const std::vector<Color> colors = TextColorsOf(m_sprites);
    ASSERT_EQ(colors.size(), 20U);
    EXPECT_EQ(colors[0], LightBlue);
    EXPECT_EQ(colors[1], White);
    EXPECT_EQ(m_sprites.Blend(), BlendMode::Opaque);
    EXPECT_EQ(Hud().dcFont, Client::Render::Text::FontKind::Normal);
}

// 0x5b06ac..0x5b07ef: the second one, from 140 to 156.
TEST_F(GameHelpsTest, TheSecondHelpListsItsTexts)
{
    Mode() = Client::Ui::SecondHelpMode;
    ASSERT_TRUE(Overlays().HasValue());
    const Client::Tooltips::TooltipLines& lines = Hud().tooltip;
    ASSERT_EQ(lines.next, 20);
    EXPECT_EQ(lines.text[1], "B");
    EXPECT_EQ(lines.text[3], "otra 141");
    EXPECT_EQ(lines.text[18], "otra 156");
    EXPECT_EQ(m_text.texts.size(), 17U);
}

// Q-C613: a text of 100 bytes or more does not fit in its line.
TEST_F(GameHelpsTest, ALongTextCuts)
{
    std::map<std::size_t, std::string> texts = HelpTexts();
    texts[130] = std::string(100, 'x');
    m_texts = Client::Tests::MakeTextTable(texts);
    Mode() = Client::Ui::FirstHelpMode;
    const auto drawn = Overlays();
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("Q-C613"), std::string::npos);
}

// 0x5b089c..0x5b0944: the arrows, the bolts and the ones without a range close the item help.
TEST_F(GameHelpsTest, TheItemHelpClosesForArrowsAndUnknownItems)
{
    for (const std::int32_t item : {0x807, 0x80f, 0x1800, 0x1dff, 0x2000, -1})
    {
        Mode() = Client::Ui::ItemHelpMode;
        m_world->Runtime().panels.helpItem = item;
        ASSERT_TRUE(Overlays().HasValue()) << item;
        EXPECT_EQ(Mode(), 0) << item;
    }
    EXPECT_EQ(m_sprites.Blend(), BlendMode::Opaque);
    EXPECT_TRUE(m_text.texts.empty());
    // An intermediate drawing does not close it.
    Mode() = Client::Ui::ItemHelpMode;
    ASSERT_TRUE(Overlays(true).HasValue());
    EXPECT_EQ(Mode(), Client::Ui::ItemHelpMode);
}

// 0x5b097b and 0x5b098f: at a width of 1600, ranges 1 to 4 divide by 0; with 640, or with range 5 (which does not
// divide), without Item.bmd it cuts where it reads it (the drawn help is in GameItemHelpTest).
TEST_F(GameHelpsTest, TheItemHelpCutsWhereItDraws)
{
    Mode() = Client::Ui::ItemHelpMode;
    m_world->Runtime().panels.helpItem = 0x10;
    const auto drawn = Overlays();
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x5b098f"), std::string::npos);

    m_sprites.Begin(Client::Core::PixelSize{1600, 1200});
    const auto divided = Overlays();
    ASSERT_FALSE(divided.HasValue());
    EXPECT_NE(divided.Error().find("0x5b097b"), std::string::npos);

    m_world->Runtime().panels.helpItem = 0x1e05;
    const auto fifth = Overlays();
    ASSERT_FALSE(fifth.HasValue());
    EXPECT_NE(fifth.Error().find("0x5b098f"), std::string::npos);
}

// 0x5af471 and 0x5af0c5: the box cuts with a 0x467060 mode other than 1, 2 and 3, and with more lines than the 30.
TEST_F(GameHelpsTest, TheBoxCutsWithAnotherModeAndPastItsLines)
{
    Hud().tooltip.text[0] = "algo";
    GameHudContext context = Context();
    const auto other = Client::Tooltips::DrawTheTooltipBox(context, 1, 1, 1, 0, 4, 1);
    ASSERT_FALSE(other.HasValue());
    EXPECT_NE(other.Error().find("0x5af471"), std::string::npos);
    const auto past = Client::Tooltips::DrawTheTooltipBox(
        context, 1, 1, static_cast<std::int32_t>(Client::Tooltips::TooltipLineCount) + 1, 0, 2, 1);
    ASSERT_FALSE(past.HasValue());
    EXPECT_NE(past.Error().find("0x5af0c5"), std::string::npos);
    EXPECT_TRUE(m_text.texts.empty());
}

// 0x5af0c5..0x5af13d with what the helps do not use: a space only moves down a line without being drawn and, with a
// width, the box measures its double plus 4 and centers on x.
TEST_F(GameHelpsTest, ASpaceOnlyMovesDownAndAWidthSetsTheBox)
{
    Client::Tooltips::TooltipLines& lines = Hud().tooltip;
    lines.text[0] = " ";
    lines.text[1] = "dos";
    lines.colors[1] = Client::Tooltips::TooltipGreen;
    lines.bold[1] = 1;
    lines.text[2].clear();
    lines.text[3] = "nunca";
    GameHudContext context = Context();
    ASSERT_TRUE(Client::Tooltips::DrawTheTooltipBox(context, 320, 50, 4, 50, 2, 0).HasValue());
    ASSERT_EQ(m_text.texts.size(), 1U);
    EXPECT_EQ(m_text.texts[0].text, "dos");
    // The box: 2 · 50 + 4 = 104 wide from 320 − 52; the text in its 102 columns from 269.
    EXPECT_EQ(m_text.texts[0].x, 269);
    EXPECT_EQ(m_text.texts[0].firstColumn, -(102 - 18) / 2);
    EXPECT_EQ(m_text.texts[0].columns, 102);
    // 50 + 13 · 1.1.
    EXPECT_EQ(m_text.texts[0].y, 64);
    EXPECT_TRUE(ColorQuadsOf(m_sprites).empty());
    EXPECT_EQ(TextColorsOf(m_sprites)[0], (Color{0.1f, 1.0f, 0.5f, 1.0f}));
    EXPECT_EQ(Hud().dcFont, Client::Render::Text::FontKind::Bold);
}
} // namespace
