#include "UI/CreditsWindow.h"

#include "App/WindowState.h"
#include "Data/Text/Credits.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Render/SpriteBatch.h"
#include "UI/NewUi.h"
#include "UI/Widget.h"

#include "support/RecordingTextDrawer.h"
#include "support/UiInput.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Client::UI::CreditsWindow: los créditos del login (+0x4578: 0x63f020, 0x63f3a0, 0x63f5a0, 0x63fbf0,
// 0x63f630, 0x63ffe0, 0x6400b0, 0x63fe20 y 0x63f6b0; CL8, hito 8). Las tablas son inventadas.
namespace
{
using Client::App::PixelSize;
using Client::Data::Text::CreditEntry;
using Client::Input::InputState;
using Client::Input::Key;
using Client::UI::CreditsLayout;
using Client::UI::CreditsStep;
using Client::UI::CreditsWindow;
using Client::UI::LayoutCredits;
using Client::UI::NewUiTurn;
using Client::UI::Rect;

constexpr PixelSize Screen640{640, 480};
constexpr PixelSize Screen800{800, 600};
// Un paso de la lógica: 40 ms (App::LogicStep).
constexpr double StepMs = 40.0;
// A 40 ms, una barra se va o vuelve en 8 pasos (34 por paso) y los nombres se quedan 58 (2.320 ms).
constexpr int BarSteps = 8;
constexpr int NameSteps = 58;

CreditEntry Entry(std::uint8_t kind, std::string text)
{
    return CreditEntry{.kind = kind, .text = std::move(text)};
}

// Un título, un rol, dos nombres; otro título con un nombre, y el final.
std::vector<CreditEntry> ShortTable()
{
    return {Entry(1, "Parte"), Entry(2, "Rol"),  Entry(3, "Uno"), Entry(3, "Dos"),
            Entry(1, "Otra"),  Entry(3, "Tres"), Entry(0, "")};
}

// Un título, un rol y 300 nombres: 75 grupos de cuatro, sin final adentro.
std::vector<CreditEntry> LongTable()
{
    std::vector<CreditEntry> table{Entry(1, "Parte"), Entry(2, "Rol")};
    for (int i = 0; i < 300; ++i)
    {
        table.push_back(Entry(3, "Nombre"));
    }
    return table;
}

CreditsWindow OpenWindow(const std::vector<CreditEntry>& table, CreditsStep* opened = nullptr)
{
    CreditsWindow window(Client::Tests::FakeTextureSizes({{0x726, {64, 128}}}), table);
    window.Arm();
    const CreditsStep step = window.Open();
    if (opened != nullptr)
    {
        *opened = step;
    }
    return window;
}

CreditsStep StepOnce(CreditsWindow& window, InputState& input, PixelSize screen = Screen640)
{
    CreditsStep step = window.Step(input, screen, StepMs);
    input.ConsumeEdges();
    return step;
}

// Los pasos `count` sin nada, sumando lo que pasó.
CreditsStep StepMany(CreditsWindow& window, InputState& input, int count)
{
    CreditsStep total;
    for (int i = 0; i < count; ++i)
    {
        const CreditsStep step = StepOnce(window, input);
        total.closes += step.closes;
        if (step.illustration)
        {
            total.illustration = step.illustration;
        }
    }
    return total;
}

// 0x63f3a0: W / 800 y H / 600 en float, y el adorno en (W − 189, _ftol(H · 0,878333f) − 103): con 600, el
// producto en la x87 da 526,99999… y el adorno sube un píxel (Q-C500). El botón, a (122, 63) del adorno.
TEST(CreditsWindowTest, TheDecoGoesByTheFloat)
{
    const CreditsLayout big = LayoutCredits(Screen800);
    EXPECT_FLOAT_EQ(big.scaleX, 1.0f);
    EXPECT_FLOAT_EQ(big.scaleY, 1.0f);
    EXPECT_EQ(big.deco, (Rect{611, 423, 189, 103}));
    EXPECT_EQ(big.close, (Rect{733, 486, 54, 30}));

    const CreditsLayout small = LayoutCredits(Screen640);
    EXPECT_FLOAT_EQ(small.scaleX, 0.8f);
    EXPECT_FLOAT_EQ(small.scaleY, 0.8f);
    EXPECT_EQ(small.deco, (Rect{451, 318, 189, 103}));
    EXPECT_EQ(small.close, (Rect{573, 381, 54, 30}));
}

// 0x63fbf0: la primera ilustración (que se carga, transparente), los tres renglones apareciendo y el primer
// grupo: el título, el rol y los nombres que siguen, hasta cuatro.
TEST(CreditsWindowTest, OpeningReadsTheFirstGroup)
{
    const std::vector<CreditEntry> table = ShortTable();
    CreditsStep opened;
    const CreditsWindow window = OpenWindow(table, &opened);

    EXPECT_EQ(opened, (CreditsStep{.closes = 0, .illustration = std::uint8_t{0}}));
    EXPECT_TRUE(window.IsOpen());
    EXPECT_EQ(window.IllustrationState(), 1);
    EXPECT_EQ(window.IllustrationAlpha(), 0);
    EXPECT_EQ(window.TitleIndex(), 0U);
    EXPECT_EQ(window.RoleIndex(), 1U);
    ASSERT_EQ(window.NameIndices().size(), 2U);
    EXPECT_EQ(window.NameIndices()[0], 2U);
    EXPECT_EQ(window.NameIndices()[1], 3U);
    EXPECT_EQ(window.NextIndex(), 4U);
    for (std::size_t row = 0; row < 3; ++row)
    {
        EXPECT_EQ(window.RowState(row), 1);
        EXPECT_EQ(window.BarAlpha(row), 0xff);
    }
}

// Con la tabla vacía (o un 0 al principio), 0x63ffe0 los cierra al abrirlos.
TEST(CreditsWindowTest, AnEmptyTableClosesThemAtOnce)
{
    const std::vector<CreditEntry> table;
    CreditsStep opened;
    const CreditsWindow window = OpenWindow(table, &opened);

    EXPECT_EQ(opened.closes, 1);
    EXPECT_FALSE(window.IsOpen());
}

// 0x6400b0: las barras se van a 34 por paso; a 0, el renglón se queda. Los nombres, 2,3 s; después se van
// ellos, el rol si lo que sigue no es un nombre y el título si tampoco es un rol. Al volver la barra de los
// nombres, el grupo que sigue: un título sin rol deja el rol de antes.
TEST(CreditsWindowTest, TheRowsComeAndGo)
{
    const std::vector<CreditEntry> table = ShortTable();
    CreditsWindow window = OpenWindow(table);
    InputState input;

    StepOnce(window, input);
    EXPECT_EQ(window.BarAlpha(0), 221);
    StepMany(window, input, BarSteps - 1);
    for (std::size_t row = 0; row < 3; ++row)
    {
        EXPECT_EQ(window.RowState(row), 2);
        EXPECT_EQ(window.BarAlpha(row), 0);
    }
    StepMany(window, input, NameSteps - 1);
    EXPECT_EQ(window.RowState(2), 2);
    StepOnce(window, input);
    EXPECT_EQ(window.RowState(0), 3);
    EXPECT_EQ(window.RowState(1), 3);
    EXPECT_EQ(window.RowState(2), 3);

    StepMany(window, input, BarSteps - 1);
    EXPECT_EQ(window.BarAlpha(2), 238);
    EXPECT_EQ(window.TitleIndex(), 0U);
    StepOnce(window, input);
    EXPECT_EQ(window.RowState(2), 1);
    EXPECT_EQ(window.BarAlpha(2), 0xff);
    EXPECT_EQ(window.TitleIndex(), 4U);
    EXPECT_EQ(window.RoleIndex(), 1U);
    ASSERT_EQ(window.NameIndices().size(), 1U);
    EXPECT_EQ(window.NameIndices()[0], 5U);
    EXPECT_TRUE(window.IsOpen());

    // El segundo grupo, entero: al volver la barra de los nombres, el final los cierra.
    const CreditsStep last = StepMany(window, input, BarSteps + NameSteps + BarSteps);
    EXPECT_EQ(last.closes, 1);
    EXPECT_FALSE(window.IsOpen());
}

// Con más de cuatro nombres, el resto va en el grupo que sigue, con el mismo título y el mismo rol: solo se
// va el renglón de los nombres.
TEST(CreditsWindowTest, MoreNamesKeepTheTitleAndTheRole)
{
    const std::vector<CreditEntry> table = LongTable();
    CreditsWindow window = OpenWindow(table);
    InputState input;

    StepMany(window, input, BarSteps + NameSteps);
    EXPECT_EQ(window.RowState(0), 2);
    EXPECT_EQ(window.RowState(1), 2);
    EXPECT_EQ(window.RowState(2), 3);
    StepMany(window, input, BarSteps);
    EXPECT_EQ(window.TitleIndex(), 0U);
    EXPECT_EQ(window.NameIndices()[0], 6U);
    EXPECT_EQ(window.NextIndex(), 10U);
}

// 0x63fe20: la ilustración aparece a 5 por paso (51 pasos), se queda 22 s (551 pasos, que suman 22.040) y se
// va a 5 por paso; en 0, la siguiente, que se carga.
TEST(CreditsWindowTest, TheIllustrationsTakeTurns)
{
    const std::vector<CreditEntry> table = LongTable();
    CreditsWindow window = OpenWindow(table);
    InputState input;

    StepOnce(window, input);
    EXPECT_EQ(window.IllustrationAlpha(), 5);
    StepMany(window, input, 50);
    EXPECT_EQ(window.IllustrationState(), 2);
    EXPECT_EQ(window.IllustrationAlpha(), 0xff);
    StepMany(window, input, 550);
    EXPECT_EQ(window.IllustrationState(), 2);
    StepOnce(window, input);
    EXPECT_EQ(window.IllustrationState(), 3);
    const CreditsStep next = StepMany(window, input, 51);
    EXPECT_EQ(next.illustration, std::uint8_t{1});
    EXPECT_EQ(window.Illustration(), 1);
    EXPECT_EQ(window.IllustrationState(), 1);
    EXPECT_EQ(window.IllustrationAlpha(), 0);
}

// 0x63f630: Esc cierra (0x63f9a0) y el paso sigue: las barras se mueven igual, la ilustración no (0x63f5a0
// con 0 la dejó quieta).
TEST(CreditsWindowTest, EscapeClosesThemAndTheRowsStillMove)
{
    const std::vector<CreditEntry> table = ShortTable();
    CreditsWindow window = OpenWindow(table);
    InputState input;
    Client::Tests::PressKey(input, Key::Escape);

    const CreditsStep step = StepOnce(window, input);

    EXPECT_EQ(step.closes, 1);
    EXPECT_FALSE(window.IsOpen());
    EXPECT_EQ(window.BarAlpha(0), 221);
    EXPECT_EQ(window.IllustrationState(), 0);
    EXPECT_EQ(window.IllustrationAlpha(), 0);
    EXPECT_FALSE(input.TakePress(Key::Escape));
}

TEST(CreditsWindowTest, TheButtonClosesThem)
{
    const std::vector<CreditEntry> table = ShortTable();
    CreditsWindow window = OpenWindow(table);
    InputState input;
    StepOnce(window, input);
    const Rect button = window.CloseButton().Area();
    EXPECT_EQ(button, (Rect{573, 381, 54, 30}));
    Client::Tests::ClickAt(input, button.x + 5, button.y + 5);

    EXPECT_EQ(StepOnce(window, input).closes, 1);
    EXPECT_FALSE(window.IsOpen());
}

// Un Esc en el paso en que la barra de los nombres vuelve con el final cierra dos veces: dos F4 06 (Q-C501).
TEST(CreditsWindowTest, EscapeAtTheEndClosesTwice)
{
    const std::vector<CreditEntry> table{Entry(1, "Parte"), Entry(3, "Uno"), Entry(0, "")};
    CreditsWindow window = OpenWindow(table);
    InputState input;
    StepMany(window, input, BarSteps + NameSteps + BarSteps - 1);
    ASSERT_TRUE(window.IsOpen());
    Client::Tests::PressKey(input, Key::Escape);

    EXPECT_EQ(StepOnce(window, input).closes, 2);
    EXPECT_FALSE(window.IsOpen());
}

// Sin el foco, 0x63f630 no corre: ni Esc ni las barras.
TEST(CreditsWindowTest, WithoutTheFocusNothingMoves)
{
    const std::vector<CreditEntry> table = ShortTable();
    CreditsWindow window = OpenWindow(table);
    InputState input;
    Client::Tests::PressKey(input, Key::Escape);

    EXPECT_EQ(window.Step(input, Screen640, StepMs, NewUiTurn{.mouse = false, .logic = false}).closes, 0);
    EXPECT_TRUE(window.IsOpen());
    EXPECT_EQ(window.BarAlpha(0), 0xff);
    EXPECT_TRUE(input.TakePress(Key::Escape));
}

// 0x63f6b0: el título a 20 y el rol a 46, centrados en todo el ancho; dos nombres a 72, en el segundo y el
// tercer cuarto (160 y 320 de 640, de W / 4 de ancho), con su color.
TEST(CreditsWindowTest, TheTextsGoInColumns)
{
    const std::vector<CreditEntry> table = ShortTable();
    CreditsWindow window = OpenWindow(table);
    Client::Render::SpriteBatch batch;
    batch.Begin(Screen640);
    Client::Tests::RecordingTextDrawer text(batch);

    window.Draw(batch, text, Screen640);

    ASSERT_EQ(text.texts.size(), 4U);
    EXPECT_EQ(text.texts[0].text, "Parte");
    EXPECT_EQ(text.texts[0].x, 305);
    EXPECT_EQ(text.texts[0].y, 20);
    EXPECT_EQ(text.texts[0].style.colors.text, 0xffe2e2e2U);
    EXPECT_EQ(text.texts[1].text, "Rol");
    EXPECT_EQ(text.texts[1].y, 46);
    EXPECT_EQ(text.texts[2].text, "Uno");
    EXPECT_EQ(text.texts[2].x, 231);
    EXPECT_EQ(text.texts[2].y, 72);
    EXPECT_EQ(text.texts[2].style.colors.text, 0xffc1eeffU);
    EXPECT_EQ(text.texts[3].text, "Dos");
    EXPECT_EQ(text.texts[3].x, 391);

    // La base, las dos mitades, el adorno, el logo, los cuatro textos, las tres barras y el botón.
    std::size_t quads = 0;
    for (const auto& command : batch.Commands())
    {
        quads += command.vertexCount / 6;
    }
    EXPECT_EQ(quads, 1U + 2U + 1U + 1U + 4U + 3U + 1U);
}
} // namespace
