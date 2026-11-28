#include "FrontEnd/SelectionBar.h"

#include "Core/PixelSize.h"
#include "Input/InputState.h"
#include "Render/SpriteBatch.h"
#include "Ui/NewUi.h"
#include "Ui/Widget.h"

#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"
#include "support/UiInput.h"
#include "support/UiPrinters.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <vector>

// Client::FrontEnd::SelectionBar: the selection's bar (+0x7f50: 0x63e820, 0x63eb40, 0x63eb90 and 0x63ec00).
namespace
{
using Client::Core::PixelSize;
using Client::FrontEnd::LayoutSelectionBar;
using Client::FrontEnd::SelectionBar;
using Client::FrontEnd::SelectionBarClick;
using Client::FrontEnd::SelectionBarLayout;
using Client::Input::InputState;
using Client::Tests::ClickAt;
using Client::Ui::Point;
using Client::Ui::Rect;
using Client::Ui::WindowHit;

constexpr PixelSize Screen640{640, 480};

Client::Ui::TextureSizes LabSizes()
{
    return Client::Tests::FakeTextureSizes(
        {{0xe, {256, 128}}, {0xf, {64, 128}}, {0x10, {64, 128}}, {0x11, {64, 128}}, {0x12, {64, 128}}});
}

std::optional<SelectionBarClick> StepOnce(SelectionBar& bar, InputState& input)
{
    std::optional<SelectionBarClick> click = bar.Step(input, Screen640);
    input.ConsumeEdges();
    return click;
}

// 0x65f4b0: y is −ftol(H · −0.945f) − 54 (399 at 480 and 512 at 600: the x87 does not round the product), the
// ornament at (W − 189, y − 46), the strip at (22, y + 33) and the buttons at 22, 77, W − 131 and W − 76.
TEST(SelectionBarTest, TheLayoutFollowsTheScreen)
{
    const SelectionBarLayout at640 = LayoutSelectionBar(Screen640);
    EXPECT_EQ(at640.bar, (Rect{.x = 22, .y = 399, .width = 596, .height = 54}));
    EXPECT_EQ(at640.deco, (Rect{.x = 451, .y = 353, .width = 189, .height = 103}));
    EXPECT_EQ(at640.strip, (Rect{.x = 22, .y = 432, .width = 596, .height = 21}));
    EXPECT_EQ(at640.create, (Rect{.x = 22, .y = 399, .width = 54, .height = 30}));
    EXPECT_EQ(at640.menu, (Rect{.x = 77, .y = 399, .width = 54, .height = 30}));
    EXPECT_EQ(at640.connect, (Rect{.x = 509, .y = 399, .width = 54, .height = 30}));
    EXPECT_EQ(at640.remove, (Rect{.x = 564, .y = 399, .width = 54, .height = 30}));
    EXPECT_EQ(LayoutSelectionBar(PixelSize{800, 600}).bar.y, 512);
    const SelectionBarLayout at1024 = LayoutSelectionBar(PixelSize{1024, 768});
    EXPECT_EQ(at1024.bar.y, 671);
    EXPECT_EQ(at1024.connect.x, 893);
    EXPECT_EQ(at1024.remove.x, 948);
    EXPECT_EQ(at1024.deco, (Rect{.x = 835, .y = 625, .width = 189, .height = 103}));
    EXPECT_EQ(LayoutSelectionBar(PixelSize{1280, 1024}).bar.y, 913);
}

// 0x63eb40: create without a free slot, and connect and delete without a chosen one, are disabled (frame 3) and
// without click; the menu, never.
TEST(SelectionBarTest, TheButtonsFollowTheSlotsAndTheChoice)
{
    SelectionBar bar(LabSizes(), nullptr);
    bar.Open();
    bar.Enable(false, false);
    InputState input;
    for (const Point at : {Point{30, 405}, Point{515, 405}, Point{570, 405}})
    {
        ClickAt(input, at.x, at.y);
        EXPECT_EQ(StepOnce(bar, input), std::nullopt) << at.x;
    }
    EXPECT_EQ(bar.CreateButton().Frame(), 3);
    EXPECT_EQ(bar.ConnectButton().Frame(), 3);
    EXPECT_EQ(bar.DeleteButton().Frame(), 3);
    ClickAt(input, 85, 405);
    EXPECT_EQ(StepOnce(bar, input), std::optional{SelectionBarClick::Menu});

    bar.Enable(true, true);
    ClickAt(input, 30, 405);
    EXPECT_EQ(StepOnce(bar, input), std::optional{SelectionBarClick::Create});
    ClickAt(input, 515, 405);
    EXPECT_EQ(StepOnce(bar, input), std::optional{SelectionBarClick::Connect});
    ClickAt(input, 570, 405);
    EXPECT_EQ(StepOnce(bar, input), std::optional{SelectionBarClick::Delete});
}

// The bar has the mouse in its rectangle, (22, y) of (W − 44) × 54, and nothing else: the ornament above does not.
// It is not dragged (the title does not count), and closed it does not have it.
TEST(SelectionBarTest, TheBarHasTheMouseInItsRectangle)
{
    SelectionBar bar(LabSizes(), nullptr);
    EXPECT_FALSE(bar.Hit(WindowHit::Area, Point{22, 399}, Screen640));
    bar.Open();
    EXPECT_TRUE(bar.Hit(WindowHit::Area, Point{22, 399}, Screen640));
    EXPECT_TRUE(bar.Hit(WindowHit::Area, Point{617, 452}, Screen640));
    EXPECT_FALSE(bar.Hit(WindowHit::Area, Point{21, 420}, Screen640));
    EXPECT_FALSE(bar.Hit(WindowHit::Area, Point{500, 398}, Screen640));
    EXPECT_FALSE(bar.Hit(WindowHit::Area, Point{600, 380}, Screen640));
    EXPECT_FALSE(bar.Hit(WindowHit::Title, Point{22, 399}, Screen640));
}

// 0xffff without a texture.
std::vector<std::uint16_t> QuadSlots(const Client::Render::SpriteBatch& batch)
{
    std::vector<std::uint16_t> slots;
    for (const auto& command : batch.Commands())
    {
        const std::uint16_t slot = command.texture ? command.texture->value : std::uint16_t{0xffff};
        slots.insert(slots.end(), command.vertexCount / 6, slot);
    }
    return slots;
}

constexpr std::uint16_t NoTexture = 0xffff;
const std::uint16_t Marker = Client::Tests::TextMarkerSlot.value;

// 0x63ec00: the ornament, the black strip of alpha 0x8f on top, text 1710 at (W − 464, y + 40) with 0xff27d9ff, and
// the four buttons: create, menu, connect and delete.
TEST(SelectionBarTest, TheDrawGoesDecoStripTextAndButtons)
{
    const auto texts = Client::Tests::MakeTextTable({{1710, "Synthetic"}, {436, "Blocked"}, {437, "Site"}});
    SelectionBar bar(LabSizes(), &texts);
    bar.Open();
    Client::Render::SpriteBatch batch;
    batch.Begin(Screen640);
    Client::Tests::RecordingTextDrawer text(batch);

    bar.Draw(batch, text, Screen640);

    ASSERT_EQ(text.texts.size(), 1u);
    EXPECT_EQ(text.texts[0].text, "Synthetic");
    EXPECT_EQ(text.texts[0].x, 640 - 464);
    EXPECT_EQ(text.texts[0].y, 399 + 40);
    EXPECT_EQ(text.texts[0].style.colors.text, 0xff27d9ffu);
    EXPECT_EQ(text.texts[0].style.colors.background, 0u);
    EXPECT_EQ(QuadSlots(batch), (std::vector<std::uint16_t>{0xe, NoTexture, Marker, 0xf, 0x10, 0x11, 0x12}));
    // The strip: black with alpha 0x8f. The text, with the texture and the white that 0x620a00(1) and glColor4f set
    // again.
    const auto vertices = batch.Vertices();
    ASSERT_GE(vertices.size(), 18u);
    EXPECT_FLOAT_EQ(vertices[6].color.alpha, 143.0f / 255.0f);
    EXPECT_FLOAT_EQ(vertices[6].color.red, 0.0f);
    EXPECT_FLOAT_EQ(vertices[12].color.alpha, 1.0f);
    EXPECT_FLOAT_EQ(vertices[12].color.red, 1.0f);
    for (const auto& command : batch.Commands())
    {
        EXPECT_EQ(command.blend, Client::Render::BlendMode::Alpha);
    }
}

// 0x63ec45..0x63ecbf: the server notice (+0x511, the first 0D with 1 in the selection) goes before 1710, at
// (x + 7, y + 40) of the bar, white on 0. The text is made up.
TEST(SelectionBarTest, TheServerNoticeGoesBeforeTheText)
{
    const auto texts = Client::Tests::MakeTextTable({{1710, "Synthetic"}});
    SelectionBar bar(LabSizes(), &texts);
    bar.Open();
    Client::Render::SpriteBatch batch;
    batch.Begin(Screen640);
    Client::Tests::RecordingTextDrawer text(batch);

    bar.Draw(batch, text, Screen640, "Aviso de prueba");

    const Rect strip = LayoutSelectionBar(Screen640).bar;
    ASSERT_EQ(text.texts.size(), 2u);
    EXPECT_EQ(text.texts[0].text, "Aviso de prueba");
    EXPECT_EQ(text.texts[0].x, strip.x + 7);
    EXPECT_EQ(text.texts[0].y, strip.y + 40);
    EXPECT_EQ(text.texts[0].style.colors.text, 0xffffffffu);
    EXPECT_EQ(text.texts[0].style.colors.background, 0u);
    EXPECT_EQ(text.texts[1].text, "Synthetic");
}

// With +0x543, texts 436 and 437 between 1710 and the buttons, centered at 320 of 640 × 480 (0x623690, with 6
// columns per letter of the fake text), at 330 and 348, in black on 0x8000ffff.
TEST(SelectionBarTest, ABlockedAccountShowsTheTwoTexts)
{
    const auto texts = Client::Tests::MakeTextTable({{436, "Blocked"}, {437, "Site"}});
    SelectionBar bar(LabSizes(), &texts);
    bar.Open();
    bar.SetBlockedAccount(true);
    Client::Render::SpriteBatch batch;
    batch.Begin(Screen640);
    Client::Tests::RecordingTextDrawer text(batch);

    bar.Draw(batch, text, Screen640);

    ASSERT_EQ(text.texts.size(), 2u);
    EXPECT_EQ(text.texts[0].text, "Blocked");
    EXPECT_EQ(text.texts[0].x, 320 - 7 * 6 / 2);
    EXPECT_EQ(text.texts[0].y, 330);
    EXPECT_EQ(text.texts[1].text, "Site");
    EXPECT_EQ(text.texts[1].x, 320 - 4 * 6 / 2);
    EXPECT_EQ(text.texts[1].y, 348);
    EXPECT_EQ(text.texts[0].style.colors.text, 0xff000000u);
    EXPECT_EQ(text.texts[0].style.colors.background, 0x8000ffffu);
    EXPECT_EQ(QuadSlots(batch), (std::vector<std::uint16_t>{0xe, NoTexture, Marker, Marker, 0xf, 0x10, 0x11, 0x12}));
}
} // namespace
