#include "FrontEnd/CreditsWindow.h"

#include "Core/PixelSize.h"
#include "Tables/Credits.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Render/SpriteBatch.h"
#include "Ui/NewUi.h"
#include "Ui/Widget.h"

#include "support/RecordingTextDrawer.h"
#include "support/UiInput.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Client::FrontEnd::CreditsWindow: the login's credits (+0x4578: 0x63f020, 0x63f3a0, 0x63f5a0, 0x63fbf0, 0x63f630,
// 0x63ffe0, 0x6400b0, 0x63fe20 and 0x63f6b0; CL8, milestone 8). The tables are made up.
namespace
{
using Client::Core::PixelSize;
using Client::FrontEnd::CreditsLayout;
using Client::FrontEnd::CreditsStep;
using Client::FrontEnd::CreditsWindow;
using Client::FrontEnd::LayoutCredits;
using Client::Input::InputState;
using Client::Input::Key;
using Client::Tables::CreditEntry;
using Client::Ui::NewUiTurn;
using Client::Ui::Rect;

constexpr PixelSize Screen640{640, 480};
constexpr PixelSize Screen800{800, 600};
// One logic step: 40 ms (Kernel::LogicStep).
constexpr double StepMs = 40.0;
// At 40 ms, a bar leaves or returns in 8 steps (34 per step) and the names stay 58 (2320 ms).
constexpr int BarSteps = 8;
constexpr int NameSteps = 58;

CreditEntry Entry(std::uint8_t kind, std::string text)
{
    return CreditEntry{.kind = kind, .text = std::move(text)};
}

// A title, a role, two names; another title with one name, and the end.
std::vector<CreditEntry> ShortTable()
{
    return {Entry(1, "Parte"), Entry(2, "Rol"),  Entry(3, "Uno"), Entry(3, "Dos"),
            Entry(1, "Otra"),  Entry(3, "Tres"), Entry(0, "")};
}

// A title, a role and 300 names: 75 groups of four, with no end inside.
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

// `count` steps with nothing, adding up what happened.
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

// 0x63f3a0: W / 800 and H / 600 in float, and the ornament at (W − 189, _ftol(H · 0.878333f) − 103): with 600, the
// product on the x87 gives 526.99999… and the ornament moves up a pixel (Q-C500). The button, at (122, 63) from the
// ornament.
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

// 0x63fbf0: the first illustration (which is loaded, transparent), the three lines appearing and the first group: the
// title, the role and the names that follow, up to four.
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

// With the empty table (or a 0 at the start), 0x63ffe0 closes them on opening them.
TEST(CreditsWindowTest, AnEmptyTableClosesThemAtOnce)
{
    const std::vector<CreditEntry> table;
    CreditsStep opened;
    const CreditsWindow window = OpenWindow(table, &opened);

    EXPECT_EQ(opened.closes, 1);
    EXPECT_FALSE(window.IsOpen());
}

// 0x6400b0: the bars leave at 34 per step; at 0, the line stays. The names, 2.3 s; then they leave, the role if what
// follows is not a name and the title if it is not a role either. When the names bar returns, the next group: a title
// without a role leaves the previous role.
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

    // The second group, whole: when the names bar returns, the end closes them.
    const CreditsStep last = StepMany(window, input, BarSteps + NameSteps + BarSteps);
    EXPECT_EQ(last.closes, 1);
    EXPECT_FALSE(window.IsOpen());
}

// With more than four names, the rest go in the next group, with the same title and the same role: only the names
// line leaves.
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

// 0x63fe20: the illustration appears at 5 per step (51 steps), stays 22 s (551 steps, which add up to 22040) and
// leaves at 5 per step; at 0, the next one, which is loaded.
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

// 0x63f630: Esc closes (0x63f9a0) and the step goes on: the bars move anyway, the illustration does not (0x63f5a0
// with 0 left it still).
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

// An Esc on the step in which the names bar returns with the end closes twice: two F4 06 (Q-C501).
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

// Without the focus, 0x63f630 does not run: neither Esc nor the bars.
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

// 0x63f6b0: the title at 20 and the role at 46, centered across the whole width; two names at 72, in the second and
// third quarters (160 and 320 of 640, W / 4 wide), with their color.
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

    // The base, the two halves, the ornament, the logo, the four texts, the three bars and the button.
    std::size_t quads = 0;
    for (const auto& command : batch.Commands())
    {
        quads += command.vertexCount / 6;
    }
    EXPECT_EQ(quads, 1U + 2U + 1U + 1U + 4U + 3U + 1U);
}
} // namespace
