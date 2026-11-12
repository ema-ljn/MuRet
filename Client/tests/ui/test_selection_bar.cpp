#include "UI/SelectionBar.h"

#include "App/WindowState.h"
#include "Input/InputState.h"
#include "Render/SpriteBatch.h"
#include "UI/NewUi.h"
#include "UI/Widget.h"

#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"
#include "support/UiInput.h"
#include "support/UiPrinters.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <vector>

// Client::UI::SelectionBar: la barra de la selección (+0x7f50: 0x63e820, 0x63eb40, 0x63eb90 y 0x63ec00).
namespace
{
using Client::App::PixelSize;
using Client::Input::InputState;
using Client::Tests::ClickAt;
using Client::UI::LayoutSelectionBar;
using Client::UI::Point;
using Client::UI::Rect;
using Client::UI::SelectionBar;
using Client::UI::SelectionBarClick;
using Client::UI::SelectionBarLayout;
using Client::UI::WindowHit;

constexpr PixelSize Screen640{640, 480};

Client::UI::TextureSizes LabSizes()
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

// 0x65f4b0: la y es −ftol(H · −0,945f) − 54 (399 en 480 y 512 en 600: la x87 no redondea el
// producto), el adorno en (W − 189, y − 46), la franja en (22, y + 33) y los botones en 22, 77,
// W − 131 y W − 76.
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

// 0x63eb40: crear sin lugar libre, y conectar y borrar sin elegido, van deshabilitados (el cuadro 3)
// y sin clic; el menú, nunca.
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

// La barra tiene el mouse en su rectángulo, (22, y) de (W − 44) × 54, y nada más: el adorno de
// arriba no. No se arrastra (el título no cuenta), y cerrada no lo tiene.
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

// La textura de cada quad del lote, en orden: 0xffff sin textura.
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

// 0x63ec00: el adorno, la franja negra de alfa 0x8f encima, el texto 1710 en (W − 464, y + 40) con
// 0xff27d9ff, y los cuatro botones: crear, menú, conectar y borrar.
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
    // La franja: negra con el alfa 0x8f. El texto, con la textura y el blanco que vuelven a poner
    // 0x620a00(1) y glColor4f.
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

// 0x63ec45..0x63ecbf: el aviso del servidor (+0x511, el primer 0D con 1 en la selección) va antes del 1710, en
// (x + 7, y + 40) de la barra, blanco sobre 0. El texto es inventado.
TEST(SelectionBarTest, TheServerNoticeGoesBeforeTheText)
{
    const auto texts = Client::Tests::MakeTextTable({{1710, "Synthetic"}});
    SelectionBar bar(LabSizes(), &texts);
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

// Con el +0x543, los textos 436 y 437 entre el 1710 y los botones, centrados en 320 de 640 × 480
// (0x623690, con 6 columnas por letra del texto falso), en 330 y 348, en negro sobre 0x8000ffff.
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
