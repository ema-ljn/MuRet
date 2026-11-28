#include "PersonalShop/ShopSigns.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Crt/CrtRandom.h"
#include "Tables/TextTable.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextBox.h"
#include "Render/Text/TextStyle.h"
#include "Kernel/ActorState.h"
#include "Hud/GamePanels.h"
#include "Scenes/GameProjection.h"
#include "Social/SocialState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/Widget.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The personal shops' signs (CL9 milestone 7, phase 6b; 06 § Lo social en pantalla, "Los carteles"): 0x451c20 with
// 0x451d60, 0x452110, 0x451e40, 0x452a10 and 0x452e50, the measure (0x452540, 0x452f80 and 0x4531b0), the bubble's
// short title (0x4518b0) and the title's colors (0x4517c0, 0x451af0, 0x451b50 and 0x451bb0), against their ASM, in
// Lorencia (0) and in Devias (2). The projection puts 640 × 480's x at the center minus the owner's x and y at the
// center plus its top, its z and 60, minus 300. The text measures 6 columns per letter. Names, titles and text 1104,
// made up.
namespace
{
using Client::Core::PixelSize;
using Client::Crt::CrtRandom;
using Client::Kernel::WorldState;
using Client::PersonalShop::ShopSignBox;
using Client::PersonalShop::ShopSignColors;
using Client::PersonalShop::ShopTitleLines;
using Client::Render::SpriteBatch;
using Client::Render::Text::BoxShape;
using Client::Render::Text::FontKind;
using Client::Render::Text::TextColors;
using Client::Scenes::GameProjection;
using Client::Social::ShopTitle;
using Client::Tests::RecordedText;
using Client::Tests::RecordingTextDrawer;

constexpr std::int32_t Lorencia = 0;
constexpr std::int32_t Devias = 2;
constexpr std::uint32_t Seed = 11;
constexpr PixelSize Screen640{640, 480};
constexpr PixelSize Screen1024{1024, 768};
constexpr int Step640 = 12;
constexpr int Step1024 = 14;
constexpr float PlayerTop = 120.0f;
constexpr std::size_t Seller = 7;
constexpr std::size_t Buyer = 9;
// Text 1104: 8 letters, 48 columns.
constexpr std::string_view Header = "[Puesto]";
constexpr int HeaderWidth = 48;
// 0x452a10: the colors without PK 4, 5 or 6, still and chosen, and the lines'.
constexpr ShopSignColors Plain{.name = 0xff00e6e6u, .header = 0xff0096fau, .background = 0x8029396cu};
constexpr ShopSignColors Hovered{.name = 0xffffe6e6u, .header = 0xff29396cu, .background = 0x800096fau};
constexpr TextColors LineColors{.text = 0xffffffffu, .background = 0xc829396cu};

GameProjection FlatProjection(PixelSize screen)
{
    GameProjection projection;
    projection.view = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, -300.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    projection.horizontalScale = 1.0f;
    projection.verticalScale = 1.0f;
    projection.centerX = screen.width / 2;
    projection.centerY = screen.height / 2;
    projection.screenWidth = screen.width;
    projection.screenHeight = screen.height;
    return projection;
}

int Draws(const CrtRandom& random)
{
    CrtRandom reference(Seed);
    for (int draws = 0; draws <= 1000; ++draws)
    {
        if (reference.State() == random.State())
            return draws;
        static_cast<void>(reference.Next());
    }
    return -1;
}

void ExpectRow(const RecordedText& row, std::string_view text, int x, int y, FontKind font, TextColors colors,
               int width, int rows)
{
    SCOPED_TRACE(text);
    EXPECT_EQ(row.text, text);
    EXPECT_EQ(row.x, x);
    EXPECT_EQ(row.y, y);
    EXPECT_EQ(row.style.font, font);
    EXPECT_EQ(row.style.colors, colors);
    ASSERT_TRUE(row.box.has_value());
    EXPECT_EQ(*row.box, (BoxShape{.width = width, .rows = rows, .prefixColumns = 0, .prefixColor = 0}));
    EXPECT_FALSE(row.clampY);
}

TextColors NameColors(const ShopSignColors& colors)
{
    return TextColors{.text = colors.name, .background = colors.background};
}

TextColors HeaderColors(const ShopSignColors& colors)
{
    return TextColors{.text = colors.header, .background = colors.background};
}

class ShopSignsTest : public ::testing::TestWithParam<std::int32_t>
{
protected:
    ShopSignsTest() : m_text(m_sprites)
    {
        Runtime().map = GetParam();
        UseTheScreen(Screen640);
        Actor(Seller, glm::vec3(100.0f, 7.0f, 10.0f));
        Actor(Buyer, glm::vec3(100.0f, 7.0f, 10.0f));
    }

    Client::Kernel::WorldRuntime& Runtime()
    {
        return m_world->Runtime();
    }

    Client::Social::PersonalShopState& Shops()
    {
        return Runtime().social.personalShop;
    }

    void UseTheScreen(PixelSize screen)
    {
        m_sprites.Begin(screen);
        m_projection = FlatProjection(screen);
    }

    Client::Kernel::ActorState& Actor(std::size_t slot, glm::vec3 position)
    {
        Client::Kernel::ActorState& actor = m_world->Actors()[slot];
        actor.alive = true;
        actor.seen = true;
        actor.position = position;
        actor.boundsTop = PlayerTop;
        actor.pk = 3;
        return actor;
    }

    ShopTitle& Sign(std::size_t slot, std::string name, std::string title)
    {
        ShopTitle& sign = Shops().titles[slot];
        sign.key = static_cast<std::int32_t>(0x0200 + slot);
        sign.name = std::move(name);
        sign.title = std::move(title);
        return sign;
    }

    Client::Core::Expected<void, std::string> Draw(bool projecting = false, bool withProjection = true,
                                                   bool withTexts = true)
    {
        Client::PersonalShop::ShopSignDrawContext context{.world = *m_world,
                                                          .panels = m_panels.Registry(),
                                                          .projection = withProjection ? &m_projection : nullptr,
                                                          .sprites = m_sprites,
                                                          .text = m_text,
                                                          .dcFont = m_dcFont,
                                                          .texts = withTexts ? &m_texts : nullptr,
                                                          .projecting = projecting};
        return Client::PersonalShop::DrawTheShopSigns(context);
    }

    [[nodiscard]] const std::vector<RecordedText>& Texts() const
    {
        return m_text.texts;
    }

    std::unique_ptr<WorldState> m_world = std::make_unique<WorldState>();
    CrtRandom m_random{Seed};
    Client::Hud::GamePanels m_panels{*m_world, m_random};
    SpriteBatch m_sprites;
    RecordingTextDrawer m_text;
    GameProjection m_projection{};
    FontKind m_dcFont = FontKind::BigBold;
    Client::Tables::TextTable m_texts = Client::Tests::MakeTextTable({{1104, std::string(Header)}});
};

// 0x452f80: with 20 bytes or fewer, whole in the first; with more, the first up to the first cut from the middle that
// does not split a two-byte letter, and the second with the rest.
TEST_P(ShopSignsTest, TheTitleSplitsFromTheMiddle)
{
    using Client::PersonalShop::SplitTheShopTitle;
    EXPECT_EQ(SplitTheShopTitle("Arcos"), (ShopTitleLines{.first = "Arcos", .second = ""}));
    EXPECT_EQ(SplitTheShopTitle("abcdefghijklmnopqrst"),
              (ShopTitleLines{.first = "abcdefghijklmnopqrst", .second = ""}));
    EXPECT_EQ(SplitTheShopTitle("abcdefghijklmnopqrstu"),
              (ShopTitleLines{.first = "abcdefghij", .second = "klmnopqrstu"}));
    // 23 bytes, the middle at 11: the two-byte letter at 10 and 11 goes whole in the first.
    const std::string wide = std::string(10, 'a') + "\xb0\xa1" + std::string(11, 'b');
    EXPECT_EQ(SplitTheShopTitle(wide),
              (ShopTitleLines{.first = std::string(10, 'a') + "\xb0\xa1", .second = std::string(11, 'b')}));
}

// 0x4518b0: with 14 bytes or fewer, whole; otherwise, the first 12 (13 if the 12th is the second half of a two-byte
// letter) and "..". Without a sign, empty.
TEST_P(ShopSignsTest, TheShortTitleKeepsTwelveBytesAndTwoDots)
{
    using Client::PersonalShop::ShortShopTitle;
    EXPECT_EQ(ShortShopTitle("Arcos y flecha"), "Arcos y flecha");
    EXPECT_EQ(ShortShopTitle("Arcos y flechas"), "Arcos y flec..");
    const std::string wide = std::string(11, 'a') + "\xb0\xa1" + std::string(5, 'b');
    EXPECT_EQ(ShortShopTitle(wide), std::string(11, 'a') + "\xb0\xa1..");
    Sign(Seller, "Brezal", "Arcos y flechas");
    EXPECT_EQ(ShortShopTitle(Shops(), Seller), "Arcos y flec..");
    EXPECT_EQ(ShortShopTitle(Shops(), Buyer), "");
}

// 0x452540 and 0x4531b0: text 1104 and the name in bold, the lines in the normal font; the width is the largest and
// the height, one row for each there is.
TEST_P(ShopSignsTest, TheMeasureCountsTheRows)
{
    using Client::PersonalShop::MeasureTheShopSign;
    const auto measure = [&](std::string name, std::string title, int step)
    { return MeasureTheShopSign(ShopTitle{.name = std::move(name), .title = std::move(title)}, Header, m_text, step); };

    auto layout = measure("Brezal", "", Step640);
    EXPECT_EQ(layout.header, HeaderWidth);
    EXPECT_EQ(layout.width, HeaderWidth + 36);
    EXPECT_EQ(layout.height, Step640);
    layout = measure("Brezal", "Arcos de caza mayor!", Step640);
    EXPECT_EQ(layout.width, 120);
    EXPECT_EQ(layout.height, 2 * Step640);
    layout = measure("Bo", "Arcos para la caza mayor", Step1024);
    EXPECT_EQ(layout.lines, (ShopTitleLines{.first = "Arcos para l", .second = "a caza mayor"}));
    EXPECT_EQ(layout.width, 72);
    EXPECT_EQ(layout.height, 3 * Step1024);
}

// 0x451c27: with the map empty there is nothing, neither 0x5bb8d0's rand() nor the +0x10 count; not even the
// projection is needed.
TEST_P(ShopSignsTest, WithoutSignsNothingHappens)
{
    Runtime().panels.inventory = 1;
    ASSERT_TRUE(Draw(false, false, false).HasValue());
    EXPECT_TRUE(Texts().empty());
    EXPECT_EQ(Shops().signBlink, 0);
    EXPECT_EQ(Draws(m_random), 0);
    EXPECT_EQ(m_dcFont, FontKind::BigBold);
}

// 0x452110 and 0x452a10: 60 above the owner's top, centered and 36 rows higher; text 1104 and the name in bold and the
// title below, in white, each with its quad. The DC stays in the normal font and +0x10 counts 1.
TEST_P(ShopSignsTest, ASignGoesAboveItsOwnerWithItsRows)
{
    Sign(Seller, "Brezal", "Arcos");

    ASSERT_TRUE(Draw().HasValue());

    // x: 320 − 100 − 84 / 2; y: 240 + (120 + 10 + 60 − 300) − 36.
    ASSERT_EQ(Texts().size(), 3u);
    ExpectRow(Texts()[0], Header, 178, 94, FontKind::Bold, HeaderColors(Plain), HeaderWidth, Step640);
    ExpectRow(Texts()[1], "Brezal", 178 + HeaderWidth, 94, FontKind::Bold, NameColors(Plain), 88 - HeaderWidth,
              Step640);
    ExpectRow(Texts()[2], "Arcos", 178, 94 + Step640, FontKind::Normal, LineColors, 88, Step640);
    // 0x4527d0: the position stays in +0x48 and +0x4c.
    EXPECT_EQ(Shops().titles[Seller].x, 178);
    EXPECT_EQ(Shops().titles[Seller].y, 94);
    EXPECT_EQ(m_dcFont, FontKind::Normal);
    EXPECT_EQ(Shops().signBlink, 1);
    EXPECT_EQ(Draws(m_random), 0);

    // Without a title, the DC stays in the name's bold font.
    Shops().titles[Seller].title.clear();
    m_text.texts.clear();
    ASSERT_TRUE(Draw().HasValue());
    ASSERT_EQ(Texts().size(), 2u);
    EXPECT_EQ(m_dcFont, FontKind::Bold);
}

// At 1024 × 768, the 640 × 480 x and y become pixels with IMUL and the signed division, with step 14 and the two
// lines.
TEST_P(ShopSignsTest, OnAWiderScreenTheSignIsInPixels)
{
    UseTheScreen(Screen1024);
    Sign(Seller, "Brezal", "Arcos y flechas de caza");

    ASSERT_TRUE(Draw().HasValue());

    // (512 − 100) · 640 / 1024 = 257 and (384 − 110) · 480 / 768 = 171; 1024 · 257 / 640 − 42 and 768 · 171 / 480 − 57.
    ASSERT_EQ(Texts().size(), 4u);
    ExpectRow(Texts()[0], Header, 369, 216, FontKind::Bold, HeaderColors(Plain), HeaderWidth, Step1024);
    ExpectRow(Texts()[1], "Brezal", 369 + HeaderWidth, 216, FontKind::Bold, NameColors(Plain), 88 - HeaderWidth,
              Step1024);
    ExpectRow(Texts()[2], "Arcos y fle", 369, 216 + Step1024, FontKind::Normal, LineColors, 88, Step1024);
    ExpectRow(Texts()[3], "chas de caza", 369, 216 + 2 * Step1024, FontKind::Normal, LineColors, 88, Step1024);
}

// 0x451d6a: 0x5bb8d0 with the inventory open queries the NPC shop (0x10000) and the trade (0x20): 2 rand() per draw,
// even if the signs are off.
TEST_P(ShopSignsTest, TheWorldWidthDrawsItsQueries)
{
    Sign(Seller, "Brezal", "Arcos");
    Runtime().panels.inventory = 1;
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(Draws(m_random), 2);
    Shops().signsShown = 0;
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(Draws(m_random), 4);
}

// 0x451e40: the first of an overlapping pair goes down to the other's bottom edge if its bottom edge is not before the
// other's middle, and otherwise goes up until its bottom edge touches the other's top.
TEST_P(ShopSignsTest, OverlappingSignsMoveDownOrUp)
{
    Sign(Seller, "Brezal", "Arcos");
    Sign(Buyer, "Cardon", "Botas");

    ASSERT_TRUE(Draw().HasValue());
    ASSERT_EQ(Texts().size(), 6u);
    EXPECT_EQ(Texts()[0].y, 94 + 24);
    EXPECT_EQ(Texts()[3].y, 94);
    // What stays in +0x4c is the position already spread.
    EXPECT_EQ(Shops().titles[Seller].y, 94 + 24);
    EXPECT_EQ(Shops().titles[Buyer].y, 94);

    // The seller's, 15 higher (79..103), ends before the other's middle (106): it goes up to 70.
    m_world->Actors()[Seller].position.z = -5.0f;
    m_text.texts.clear();
    ASSERT_TRUE(Draw().HasValue());
    ASSERT_EQ(Texts().size(), 6u);
    EXPECT_EQ(Texts()[0].y, 70);
    EXPECT_EQ(Texts()[3].y, 94);
}

// A hidden sign (+0x44) is also placed and pushes: the spread does not look at whether it shows.
TEST_P(ShopSignsTest, AHiddenSignStillPushes)
{
    Sign(Seller, "Brezal", "Arcos");
    Sign(Buyer, "Cardon", "Botas").visible = false;

    ASSERT_TRUE(Draw().HasValue());
    ASSERT_EQ(Texts().size(), 3u);
    EXPECT_EQ(Texts()[0].text, Header);
    EXPECT_EQ(Texts()[1].text, "Brezal");
    EXPECT_EQ(Texts()[0].y, 94 + 24);

    // 0x4516c0: with +0x14 off ("/ver puestos") none is drawn, but +0x10 counts.
    Shops().signsShown = 0;
    m_text.texts.clear();
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_TRUE(Texts().empty());
    EXPECT_EQ(Shops().signBlink, 2);
}

// 0x451f1a: against the edges, x at 0 or, from W − width (unsigned), at W − width − 4; y at 0 or at H − height. A
// sign wider than the screen gives a negative W − width, which unsigned is huge: it does not move.
TEST_P(ShopSignsTest, TheEdgesKeepTheSignsOnTheScreen)
{
    std::array<ShopSignBox, 5> signs{{{.x = -5, .y = -3, .width = 84, .height = 24},
                                      {.x = 600, .y = 470, .width = 84, .height = 24},
                                      {.x = 555, .y = 455, .width = 84, .height = 24},
                                      {.x = 556, .y = 456, .width = 84, .height = 24},
                                      {.x = 10, .y = 5, .width = 700, .height = 500}}};
    // One at a time, so they do not overlap.
    for (ShopSignBox& sign : signs)
        Client::PersonalShop::SpreadTheShopSigns(std::span<ShopSignBox>(&sign, 1), Screen640);
    EXPECT_EQ(signs[0], (ShopSignBox{.x = 0, .y = 0, .width = 84, .height = 24}));
    EXPECT_EQ(signs[1], (ShopSignBox{.x = 552, .y = 456, .width = 84, .height = 24}));
    EXPECT_EQ(signs[2], (ShopSignBox{.x = 555, .y = 455, .width = 84, .height = 24}));
    EXPECT_EQ(signs[3], (ShopSignBox{.x = 552, .y = 456, .width = 84, .height = 24}));
    EXPECT_EQ(signs[4], (ShopSignBox{.x = 10, .y = 5, .width = 700, .height = 500}));
}

// Q-C976, 0x452e50: x against W; y, if its row passes H − H · 47 / 480, stays at that cap (not at the cap minus the
// height, nor with 0x554000's 640).
TEST_P(ShopSignsTest, TheRowStopsAtTheBarLine)
{
    using Client::PersonalShop::ShopSignRowAt;
    using Client::Ui::Point;
    EXPECT_EQ(ShopSignRowAt(Point{.x = -10, .y = -10}, 88, Step640, Screen640), (Point{.x = 0, .y = 0}));
    EXPECT_EQ(ShopSignRowAt(Point{.x = 600, .y = 421}, 88, Step640, Screen640), (Point{.x = 552, .y = 421}));
    EXPECT_EQ(ShopSignRowAt(Point{.x = 552, .y = 425}, 88, Step640, Screen640), (Point{.x = 552, .y = 433}));
    EXPECT_EQ(ShopSignRowAt(Point{.x = 0, .y = 690}, 88, Step1024, Screen1024), (Point{.x = 0, .y = 693}));

    // A sign at the very bottom: the spread leaves it at H − height (456) and the title's row goes down to the cap
    // (433).
    Sign(Seller, "Brezal", "Arcos");
    m_world->Actors()[Seller].position.z = 400.0f;
    ASSERT_TRUE(Draw().HasValue());
    ASSERT_EQ(Texts().size(), 3u);
    EXPECT_EQ(Texts()[0].y, 433);
    EXPECT_EQ(Texts()[2].y, 433);
}

// 0x451c67..0x451cc0: with a chosen one (0x6c8e30), +0x10 below 3, the commands window open and its command 2, the
// chosen one's sign turns on +0x60 and is drawn with the other colors; the others keep theirs (Q-C977). Otherwise,
// all at 0. +0x10 counts from 0 to 7.
TEST_P(ShopSignsTest, TheChosenSignBlinksWithTheShopCommand)
{
    Sign(Seller, "Brezal", "Arcos");
    Sign(Buyer, "Cardon", "").hovered = 1;
    m_world->Actors()[Buyer].position.x = -200.0f;
    Runtime().selectedTargets[2] = static_cast<std::int32_t>(Seller);
    Runtime().panels.delegated.commands = 1;
    Runtime().panels.delegated.commandsWindow.chosen = 2;

    std::vector<int> hovered;
    for (int frame = 0; frame < 9; ++frame)
    {
        m_text.texts.clear();
        ASSERT_TRUE(Draw().HasValue());
        hovered.push_back(Shops().titles[Seller].hovered * 10 + Shops().titles[Buyer].hovered);
        if (frame == 0)
        {
            ASSERT_EQ(Texts().size(), 5u);
            EXPECT_EQ(Texts()[0].style.colors, HeaderColors(Hovered));
            EXPECT_EQ(Texts()[1].style.colors, NameColors(Hovered));
            EXPECT_EQ(Texts()[2].style.colors, LineColors);
            EXPECT_EQ(Texts()[3].style.colors, HeaderColors(Hovered));
        }
    }
    // +0x10: 1, 2, 3, 4, 5, 6, 7, 0, 1.
    EXPECT_EQ(hovered, (std::vector<int>{11, 11, 0, 0, 0, 0, 0, 10, 10}));
    EXPECT_EQ(Shops().signBlink, 1);

    // With another command, with the window closed or without a chosen one, at 0.
    const auto hoveredAfter = [&]
    {
        Shops().signBlink = 0;
        Shops().titles[Seller].hovered = 1;
        EXPECT_TRUE(Draw().HasValue());
        return Shops().titles[Seller].hovered;
    };
    EXPECT_EQ(hoveredAfter(), 1);
    Runtime().panels.delegated.commandsWindow.chosen = 1;
    EXPECT_EQ(hoveredAfter(), 0);
    Runtime().panels.delegated.commandsWindow.chosen = 2;
    Runtime().panels.delegated.commands = 0;
    EXPECT_EQ(hoveredAfter(), 0);
    Runtime().panels.delegated.commands = 1;
    Runtime().selectedTargets[2] = -1;
    EXPECT_EQ(hoveredAfter(), 0);
}

// 0x452a10: with PK 4, 5 or 6, text 1104 and the name in the same color and the background of the three cases.
TEST_P(ShopSignsTest, ThePkColorsTheSign)
{
    using Client::PersonalShop::ShopSignColorsOf;
    EXPECT_EQ(ShopSignColorsOf(3, false), Plain);
    EXPECT_EQ(ShopSignColorsOf(0, true), Hovered);
    EXPECT_EQ(ShopSignColorsOf(4, false),
              (ShopSignColors{.name = 0xff00b4e6u, .header = 0xff00b4e6u, .background = 0x8029396cu}));
    EXPECT_EQ(ShopSignColorsOf(5, true),
              (ShopSignColors{.name = 0xff006ee6u, .header = 0xff006ee6u, .background = 0x80527ab6u}));
    EXPECT_EQ(ShopSignColorsOf(6, false),
              (ShopSignColors{.name = 0xff0014f0u, .header = 0xff0014f0u, .background = 0x8029396cu}));

    Sign(Seller, "Brezal", "Arcos");
    m_world->Actors()[Seller].pk = 6;
    ASSERT_TRUE(Draw().HasValue());
    ASSERT_EQ(Texts().size(), 3u);
    EXPECT_EQ(Texts()[0].style.colors, (TextColors{.text = 0xff0014f0u, .background = 0x8029396cu}));
    EXPECT_EQ(Texts()[1].style.colors, (TextColors{.text = 0xff0014f0u, .background = 0x8029396cu}));
    EXPECT_EQ(Texts()[2].style.colors, LineColors);
}

// 0x4517c0, 0x451af0, 0x451b50 and 0x451bb0: the character's sign's +0x60 (false without a sign), and the sign's
// colors with +0x370 at 2 or 8 as PK 6.
TEST_P(ShopSignsTest, TheBubbleColorsFollowTheSign)
{
    using Client::PersonalShop::ShopBubbleColorsOf;
    Sign(Seller, "Brezal", "Arcos").hovered = 1;
    EXPECT_TRUE(Client::PersonalShop::ShopSignHovered(Shops(), Seller));
    EXPECT_FALSE(Client::PersonalShop::ShopSignHovered(Shops(), Buyer));

    Client::Kernel::ActorState& owner = m_world->Actors()[Seller];
    EXPECT_EQ(ShopBubbleColorsOf(owner, false), Plain);
    owner.byte370 = 2;
    EXPECT_EQ(ShopBubbleColorsOf(owner, true),
              (ShopSignColors{.name = 0xff0014f0u, .header = 0xff0014f0u, .background = 0x80527ab6u}));
    owner.byte370 = 8;
    owner.pk = 4;
    EXPECT_EQ(ShopBubbleColorsOf(owner, false).name, 0xff0014f0u);
    owner.byte370 = 1;
    EXPECT_EQ(ShopBubbleColorsOf(owner, false).name, 0xff00b4e6u);
}

// A MuRet intermediate draw draws the same, but does not write +0x10, the +0x60 or the positions.
TEST_P(ShopSignsTest, AnIntermediateDrawLeavesTheStateAlone)
{
    Sign(Seller, "Brezal", "Arcos").hovered = 1;

    ASSERT_TRUE(Draw(true).HasValue());

    ASSERT_EQ(Texts().size(), 3u);
    EXPECT_EQ(Texts()[1].style.colors, NameColors(Plain));
    EXPECT_EQ(Shops().signBlink, 0);
    EXPECT_EQ(Shops().titles[Seller].hovered, 1);
    EXPECT_EQ(Shops().titles[Seller].x, -1);
    EXPECT_EQ(Shops().titles[Seller].y, -1);
}

// Without the world pass's projection or without Text.bmd, a sign stops with its address.
TEST_P(ShopSignsTest, WithoutTheProjectionOrTheTextsASignIsCut)
{
    Sign(Seller, "Brezal", "Arcos");
    const auto projection = Draw(false, false, true);
    ASSERT_FALSE(projection.HasValue());
    EXPECT_NE(projection.Error().find("0x452146"), std::string::npos);
    const auto texts = Draw(false, true, false);
    ASSERT_FALSE(texts.HasValue());
    EXPECT_NE(texts.Error().find("0x452776"), std::string::npos);
}

INSTANTIATE_TEST_SUITE_P(TwoMaps, ShopSignsTest, ::testing::Values(Lorencia, Devias));
} // namespace
