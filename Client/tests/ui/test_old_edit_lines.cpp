#include "Ui/OldEditLines.h"

#include "Core/PixelSize.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Chat/ChatInput.h"
#include "Ui/OldControls.h"
#include "Ui/OldWindowDraw.h"
#include "Ui/OldWindowDrawParts.h"
#include "Ui/OldWindowSteps.h"
#include "Ui/OldWindows.h"
#include "Ui/TextField.h"
#include "Audio/SoundId.h"
#include "support/RecordingTextDrawer.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The multi-line EDIT (CL9 milestone 10; 06 § HUD y ventanas, "Las cartas y el chat con un amigo"): 0x467fe0's +0xc4
// and +0xd0, the Windows EDIT's lines (*inferred*, P-C217), its drawing (0x468320 with +0xc4 and the 0x468970 bar),
// its step (0x468d50) and Enter (0x467b10). The texts are made up.
namespace
{
using Client::Core::PixelSize;
using Client::Render::BlendMode;
using Client::Render::DrawCommand;
using Client::Render::SpriteBatch;
using Client::Render::SpriteVertex;
using Client::Render::TextureSlot;
using Client::Render::Text::FontKind;
using Client::Render::Text::ITextMeasure;
using Client::Render::Text::TextExtent;
using Client::Tests::RecordingTextDrawer;
using Client::Tests::TextMarkerSlot;
using Client::Ui::ChatFocus;
using Client::Ui::OldEdit;
using Client::Ui::OldWindowDrawContext;
using Client::Ui::OldWindowDrawView;
using Client::Ui::OldWindowGlobals;
using Client::Ui::OldWindowManager;
using Client::Ui::OldWindowMeasure;
using Client::Ui::OldWindowStepView;

constexpr PixelSize Screen640{640, 480};
constexpr PixelSize Screen800{800, 600};
constexpr TextureSlot ScrollbarSlot{0x649};

// 6 columns per letter and 13 rows, like RecordingTextDrawer.
class LetterMeasure final : public ITextMeasure
{
public:
    [[nodiscard]] TextExtent Measure(FontKind /*font*/, std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * RecordingTextDrawer::LetterWidth, RecordingTextDrawer::Height};
    }
};

// 0x497f50's letter body: 0xee × 0x87.
OldEdit TheBody(PixelSize screen, std::size_t limit = 1000)
{
    OldEdit edit;
    edit.base.id = 7;
    edit.base.x = 60;
    edit.base.y = 100;
    edit.base.width = 0xee;
    edit.base.height = 0x87;
    edit.text = Client::Ui::TextField(limit, false, Client::Ui::TextField::Charset::Latin1);
    const LetterMeasure measure;
    Client::Ui::MakeTheEditMultiline(edit, measure, FontKind::Normal, screen);
    return edit;
}

std::vector<std::string> LinesOf(const OldEdit& edit)
{
    const LetterMeasure measure;
    std::vector<std::string> lines;
    for (const std::string_view line : Client::Ui::EditLines(edit, measure))
        lines.emplace_back(line);
    return lines;
}

// 0x467fe0: +0xd0 is ftol(0x87 · ry / 13); the window, ftol((0xee + ftol(−5 · rx)) · rx), and the text area 16 less
// and the margins of 3 on each side.
TEST(OldEditLinesTest, TheBodyKnowsTheLinesThatFitAndItsTextWidth)
{
    const OldEdit at640 = TheBody(Screen640);
    EXPECT_EQ(at640.multiline, 1);
    EXPECT_EQ(at640.visibleLines, 10);
    EXPECT_EQ(at640.textWidth, 0xee - 5 - 16 - 6);
    // 800 × 600: rx = ry = 1.25; ftol(−6.25) = −6, ftol(232 · 1.25) = 290 and ftol(168.75 / 13) = 12.
    const OldEdit at800 = TheBody(Screen800);
    EXPECT_EQ(at800.visibleLines, 12);
    EXPECT_EQ(at800.textWidth, 290 - 16 - 6);
}

// The lines: "\r\n" breaks; what does not fit in 211 pixels (35 letters) is cut at the last space that fits, the
// extra spaces stay in the line, and a word longer than the line is cut at the last letter that fits. A one-line EDIT
// is one line; an empty one or one that ends in "\r\n", with the empty last line.
TEST(OldEditLinesTest, LinesBreakAtCrLfAndWrapAtTheLastSpace)
{
    OldEdit edit = TheBody(Screen640);
    EXPECT_EQ(LinesOf(edit), std::vector<std::string>{""});
    edit.text.SetText("hola tero\r\n\r\nchau");
    EXPECT_EQ(LinesOf(edit), (std::vector<std::string>{"hola tero", "", "chau"}));
    edit.text.SetText(std::string(30, 'a') + " " + std::string(10, 'b') + "\r\n");
    EXPECT_EQ(LinesOf(edit), (std::vector<std::string>{std::string(30, 'a') + " ", std::string(10, 'b'), ""}));
    edit.text.SetText(std::string(35, 'c') + "   d");
    EXPECT_EQ(LinesOf(edit), (std::vector<std::string>{std::string(35, 'c') + "   ", "d"}));
    edit.text.SetText(std::string(80, 'e'));
    EXPECT_EQ(LinesOf(edit),
              (std::vector<std::string>{std::string(35, 'e'), std::string(35, 'e'), std::string(10, 'e')}));

    OldEdit single;
    single.text.Type("uno dos");
    EXPECT_EQ(LinesOf(single), std::vector<std::string>{"uno dos"});
}

// EM_SCROLL and EM_LINESCROLL: between 0 and lines − +0xd0; the page is the ones that fit. ES_AUTOVSCROLL brings the
// last line into view after a text change. In a one-line EDIT, nothing.
TEST(OldEditLinesTest, TheScrollStaysBetweenTheFirstAndTheLastPage)
{
    OldEdit edit = TheBody(Screen640);
    Client::Ui::ScrollTheEdit(edit, 25, Client::Ui::EditLineDown);
    EXPECT_EQ(edit.firstLine, 1);
    Client::Ui::ScrollTheEdit(edit, 25, Client::Ui::EditPageDown);
    EXPECT_EQ(edit.firstLine, 11);
    Client::Ui::ScrollTheEdit(edit, 25, Client::Ui::EditPageDown);
    EXPECT_EQ(edit.firstLine, 15);
    Client::Ui::ScrollTheEdit(edit, 25, Client::Ui::EditPageUp);
    EXPECT_EQ(edit.firstLine, 5);
    Client::Ui::ScrollTheEditLines(edit, 25, -10000);
    EXPECT_EQ(edit.firstLine, 0);
    Client::Ui::ScrollTheEdit(edit, 6, Client::Ui::EditLineDown);
    EXPECT_EQ(edit.firstLine, 0);

    for (int line = 0; line < 14; ++line)
    {
        Client::Ui::TypeInTheEdit(edit, "x");
        Client::Ui::BreakTheEditLine(edit);
    }
    Client::Ui::FollowTheEditCaret(edit, 15);
    EXPECT_EQ(edit.firstLine, 5);
    EXPECT_FALSE(edit.followCaret);
    Client::Ui::ScrollTheEditLines(edit, 15, -10000);
    Client::Ui::FollowTheEditCaret(edit, 15);
    EXPECT_EQ(edit.firstLine, 0);

    OldEdit single;
    Client::Ui::ScrollTheEdit(single, 1, Client::Ui::EditLineDown);
    EXPECT_EQ(single.firstLine, 0);
}

// 0x467b10: each WM_CHAR sets +0x80 to 0; Enter is "\r\n" in the multi-line one if it fits whole, and backspace
// erases it whole; in the one-line one, Enter writes nothing.
TEST(OldEditLinesTest, EnterBreaksTheLineOnlyInTheMultilineEdit)
{
    OldEdit edit = TheBody(Screen640, 5);
    edit.draws = 9;
    Client::Ui::TypeInTheEdit(edit, "abc");
    EXPECT_EQ(edit.draws, 0);
    edit.draws = 9;
    Client::Ui::BreakTheEditLine(edit);
    EXPECT_EQ(edit.draws, 0);
    EXPECT_EQ(edit.text.Value(), "abc\r\n");
    Client::Ui::BreakTheEditLine(edit);
    EXPECT_EQ(edit.text.Value(), "abc\r\n");
    Client::Ui::BackspaceInTheEdit(edit);
    EXPECT_EQ(edit.text.Value(), "abc");
    Client::Ui::BackspaceInTheEdit(edit);
    EXPECT_EQ(edit.text.Value(), "ab");

    OldEdit single;
    single.text.Type("ab");
    Client::Ui::BreakTheEditLine(single);
    EXPECT_EQ(single.text.Value(), "ab");
}

// The Windows EDIT with +0xc4 (*inferred*, P-C217): Delete and the side arrows do not split the "\r\n"; up and down
// go to the letter nearest the same x; Home and End, to the line's edges; PgUp and PgDn scroll the ones that fit. All
// of them ask to show the caret.
TEST(OldEditLinesTest, TheCaretMovesByLinesAndSkipsTheWholeBreak)
{
    const LetterMeasure measure;
    using Client::Ui::EditCaretKey;
    OldEdit edit = TheBody(Screen640);
    edit.text.SetText("ab\r\ncd");
    edit.text.SetCursor(2);
    Client::Ui::DeleteInTheEdit(edit);
    EXPECT_EQ(edit.text.Value(), "abcd");
    EXPECT_TRUE(edit.followCaret);

    edit.text.SetText("abcdef\r\nxy\r\nlarguisimo");
    edit.text.SetCursor(8);
    Client::Ui::MoveTheEditCaret(edit, measure, EditCaretKey::Left, false);
    EXPECT_EQ(edit.text.Cursor(), 6u);
    Client::Ui::MoveTheEditCaret(edit, measure, EditCaretKey::Right, false);
    EXPECT_EQ(edit.text.Cursor(), 8u);
    edit.text.SetCursor(4);
    Client::Ui::MoveTheEditCaret(edit, measure, EditCaretKey::Down, false);
    EXPECT_EQ(edit.text.Cursor(), 10u);
    Client::Ui::MoveTheEditCaret(edit, measure, EditCaretKey::Down, true);
    EXPECT_EQ(edit.text.Cursor(), 14u);
    EXPECT_EQ(edit.text.SelectionBegin(), 10u);
    Client::Ui::MoveTheEditCaret(edit, measure, EditCaretKey::End, false);
    EXPECT_EQ(edit.text.Cursor(), edit.text.Size());
    Client::Ui::MoveTheEditCaret(edit, measure, EditCaretKey::Home, false);
    EXPECT_EQ(edit.text.Cursor(), 12u);
    Client::Ui::MoveTheEditCaret(edit, measure, EditCaretKey::Up, false);
    Client::Ui::MoveTheEditCaret(edit, measure, EditCaretKey::Up, false);
    Client::Ui::MoveTheEditCaret(edit, measure, EditCaretKey::Up, false);
    EXPECT_EQ(edit.text.Cursor(), 0u);

    std::string many;
    for (int line = 0; line < 24; ++line)
        many += "x\r\n";
    edit.text.SetText(many + "x");
    edit.text.SetCursor(0);
    edit.firstLine = 0;
    Client::Ui::MoveTheEditCaret(edit, measure, EditCaretKey::PageDown, false);
    EXPECT_EQ(edit.text.Cursor(), 30u);
    EXPECT_EQ(edit.firstLine, 10);
    Client::Ui::MoveTheEditCaret(edit, measure, EditCaretKey::PageUp, false);
    EXPECT_EQ(edit.text.Cursor(), 0u);
    EXPECT_EQ(edit.firstLine, 0);
}

// WM_PASTE with +0xc4: each line end goes in as "\r\n", up to the cap, without leaving a lone '\r'; the one-line one
// cuts at the first CR.
TEST(OldEditLinesTest, PastingKeepsTheLinesUpToTheLimit)
{
    OldEdit edit = TheBody(Screen640);
    Client::Ui::PasteInTheEdit(edit, "uno\ndos\r\ntres\rcuatro");
    EXPECT_EQ(edit.text.Value(), "uno\r\ndos\r\ntres\r\ncuatro");
    EXPECT_TRUE(edit.followCaret);
    OldEdit tight = TheBody(Screen640, 3);
    Client::Ui::PasteInTheEdit(tight, "ab\ncd");
    EXPECT_EQ(tight.text.Value(), "ab");

    OldEdit single;
    Client::Ui::PasteInTheEdit(single, "ab\ncd");
    EXPECT_EQ(single.text.Value(), "ab\ncd");
    OldEdit cut;
    Client::Ui::PasteInTheEdit(cut, "ab\rcd");
    EXPECT_EQ(cut.text.Value(), "ab");
}

class OldEditTest : public ::testing::Test
{
protected:
    OldEditTest() : m_text(m_sprites) {}

    OldWindowGlobals Globals()
    {
        return OldWindowGlobals{.leftHeld = m_leftHeld,
                                .leftClick = m_leftClick,
                                .releasedClick = m_released,
                                .mouseOnUi = m_mouseOnUi,
                                .hovered = m_hovered,
                                .capture = m_capture,
                                .pressedButton = m_pressedButton,
                                .keyboardFocus = m_focus,
                                .sound = m_sound,
                                .focusedList = m_focusedList,
                                .wheel = m_wheel,
                                .rightClick = m_rightClick,
                                .doubleClick = m_doubleClick,
                                .mouseUpdateTime = &m_mouseUpdateTime,
                                .mouseUpdateTimeMax = &m_mouseUpdateTimeMax};
    }

    OldWindowStepView StepView(std::int32_t x, std::int32_t y) const
    {
        return OldWindowStepView{.mouseX = x, .mouseY = y, .measure = &m_measure};
    }

    void Step(OldEdit& edit, std::int32_t x, std::int32_t y)
    {
        Client::Ui::Steps::StepTheEditMouse(m_manager, edit, Globals(), StepView(x, y));
    }

    OldWindowDrawContext Context()
    {
        return OldWindowDrawContext{.sprites = m_sprites, .text = m_text, .dcFont = m_dcFont, .textColors = m_colors};
    }

    void Draw(OldEdit& edit, PixelSize screen, bool focused, std::int32_t x = 0, std::int32_t y = 0)
    {
        m_sprites.Begin(screen);
        m_text.texts.clear();
        Client::Ui::OldDraw::DrawTheOldEdit(Context(), edit, focused,
                                            OldWindowDrawView{.mouseX = x, .mouseY = y, .leftClick = m_leftClick});
    }

    const SpriteVertex& VertexOf(std::size_t command, std::size_t quad = 0, std::size_t corner = 0) const
    {
        return m_sprites.Vertices()[m_sprites.Commands()[command].firstVertex + quad * 6 + corner];
    }

    // The quad in 640 × 480 (the vertices' x and y, with y down, scaled by the screen).
    void ExpectQuadAt(std::size_t command, std::size_t quad, float left, float top, float right, float bottom) const
    {
        const PixelSize screen = m_sprites.Screen();
        const float sx = static_cast<float>(screen.width) / 640.0f;
        const float sy = static_cast<float>(screen.height) / 480.0f;
        const SpriteVertex& first = VertexOf(command, quad, 0);
        const SpriteVertex& last = VertexOf(command, quad, 2);
        EXPECT_NEAR(first.x / sx, left, 1e-3f);
        EXPECT_NEAR((static_cast<float>(screen.height) - first.y) / sy, top, 1e-3f);
        EXPECT_NEAR(last.x / sx, right, 1e-3f);
        EXPECT_NEAR((static_cast<float>(screen.height) - last.y) / sy, bottom, 1e-3f);
    }

    LetterMeasure m_letters;
    const OldWindowMeasure m_measure{.measure = m_letters, .dcFont = FontKind::Normal, .screen = Screen640};
    OldWindowManager m_manager;
    SpriteBatch m_sprites;
    RecordingTextDrawer m_text;
    bool m_leftHeld = false;
    bool m_leftClick = false;
    bool m_released = false;
    std::uint8_t m_mouseOnUi = 0;
    std::int32_t m_hovered = 0;
    std::int32_t m_capture = 0;
    std::int32_t m_pressedButton = 0;
    ChatFocus m_focus = ChatFocus::Game;
    std::optional<Client::Audio::SoundId> m_sound;
    std::int32_t m_focusedList = 0;
    std::int32_t m_wheel = 0;
    bool m_rightClick = false;
    bool m_doubleClick = false;
    std::int32_t m_mouseUpdateTime = 4;
    std::int32_t m_mouseUpdateTimeMax = 9;
    FontKind m_dcFont = FontKind::Normal;
    Client::Render::Text::TextColors m_colors{};
};

// 0x468320 with +0xc4 at 1 and 3 lines (fewer than +0xd0): the bitmap of the whole width and 3 · 13 tall, the lines
// 13 apart and the caret at the end of the last one; then the bar (0x468970): the arrows at (x + w − 15, y − 4) and
// (x + w − 15, y + h − 9), the edges with the normal blend and the thumb, without blending, the whole track
// (+0xd0 / 3 > 1).
TEST_F(OldEditTest, AMultilineEditDrawsItsLinesItsCaretAndItsBar)
{
    OldEdit edit = TheBody(Screen640);
    edit.text.SetText("uno\r\ndos\r\ntres");
    edit.draws = 0;

    Draw(edit, Screen640, true);
    const std::span<const DrawCommand> commands = m_sprites.Commands();
    ASSERT_EQ(commands.size(), 5u);
    // The background (+0xa0, black), 238 × 39 at (60, 100).
    EXPECT_FALSE(commands[0].texture.has_value());
    ExpectQuadAt(0, 0, 60.0f, 100.0f, 298.0f, 139.0f);
    // The lines, 3 from the edge (the XP EDIT's margin), and the caret in the same batch as the text.
    EXPECT_EQ(commands[1].texture, TextMarkerSlot);
    ASSERT_EQ(m_text.texts.size(), 4u);
    EXPECT_EQ(m_text.texts[0].text, "uno");
    EXPECT_EQ(m_text.texts[0].x, 63);
    EXPECT_EQ(m_text.texts[1].y, 113);
    EXPECT_EQ(m_text.texts[2].text, "tres");
    EXPECT_EQ(m_text.texts[2].y, 126);
    // The caret, column 4 · 6 + 1 of the third line inverted (the background over 0xffc8c8c8), like the one-line one's.
    EXPECT_EQ(m_text.texts[3].text, "tres");
    EXPECT_EQ(m_text.texts[3].x, 63 + 25);
    EXPECT_EQ(m_text.texts[3].y, 126);
    EXPECT_EQ(m_text.texts[3].firstColumn, 25);
    EXPECT_EQ(m_text.texts[3].columns, 1);
    EXPECT_EQ(m_text.texts[3].style.colors.background, 0xffc8c8c8u);
    // The arrows.
    EXPECT_EQ(commands[2].texture, ScrollbarSlot);
    EXPECT_EQ(commands[2].vertexCount, 12u);
    ExpectQuadAt(2, 0, 283.0f, 96.0f, 296.0f, 109.0f);
    EXPECT_FLOAT_EQ(VertexOf(2, 0).v, 0.09375f);
    ExpectQuadAt(2, 1, 283.0f, 226.0f, 296.0f, 239.0f);
    EXPECT_FLOAT_EQ(VertexOf(2, 1).v, 0.5f);
    // The track's edges from y + 9 to y + h − 9, at x + w − 15 and x + w − 3.
    EXPECT_EQ(commands[3].blend, BlendMode::Alpha);
    ExpectQuadAt(3, 0, 283.0f, 109.0f, 284.0f, 226.0f);
    ExpectQuadAt(3, 1, 295.0f, 109.0f, 296.0f, 226.0f);
    EXPECT_EQ(VertexOf(3).color, *Client::Ui::OldWindowPalette(2, 1.0f));
    // The bar's thumb, 11 wide at x + w − 14, with its tips.
    EXPECT_EQ(commands[4].blend, BlendMode::Opaque);
    EXPECT_EQ(commands[4].vertexCount, 18u);
    ExpectQuadAt(4, 0, 284.0f, 109.0f, 295.0f, 226.0f);
    ExpectQuadAt(4, 1, 284.0f, 109.0f, 295.0f, 110.0f);
    ExpectQuadAt(4, 2, 284.0f, 225.0f, 295.0f, 226.0f);
    EXPECT_FLOAT_EQ(edit.barWidth, 13.0f);
    EXPECT_FLOAT_EQ(edit.thumbHeight, 117.0f);
    EXPECT_EQ(edit.draws, 1);
}

// With 20 lines and the third at the top: the bitmap of ftol(h · ry), the 10 lines that fit from the third, without
// the caret (the last one is not in view), and the bar's thumb of 117 · 10 / 20 at 109 + 117 · 3 / 20. The down
// arrow, with the click on it, flipped. In 800 × 600, the same at scale.
TEST_F(OldEditTest, AScrolledEditDrawsTheLinesInViewAndItsThumb)
{
    // The bitmap of ftol(238 · rx) × ftol(135 · ry) pixels: in 800 × 600, 297 × 168.
    struct Case
    {
        PixelSize screen;
        float right;
        float bottom;
    };
    for (const Case& at : {Case{Screen640, 298.0f, 235.0f}, Case{Screen800, 297.6f, 234.4f}})
    {
        const PixelSize screen = at.screen;
        SCOPED_TRACE(screen.width);
        OldEdit edit = TheBody(screen);
        std::string text;
        for (int line = 0; line < 20; ++line)
            text += "renglon " + std::to_string(line) + (line < 19 ? "\r\n" : "");
        edit.text.SetText(text);
        edit.firstLine = 3;
        m_leftClick = true;

        Draw(edit, screen, true, 290, 230);

        const std::span<const DrawCommand> commands = m_sprites.Commands();
        ASSERT_EQ(commands.size(), 5u);
        ExpectQuadAt(0, 0, 60.0f, 100.0f, at.right, at.bottom);
        const auto shown = static_cast<std::size_t>(edit.visibleLines);
        ASSERT_EQ(m_text.texts.size(), shown);
        EXPECT_EQ(m_text.texts.front().text, "renglon 3");
        EXPECT_EQ(m_text.texts.back().text, "renglon " + std::to_string(2 + shown));
        EXPECT_EQ(commands[2].texture, ScrollbarSlot);
        EXPECT_FLOAT_EQ(VertexOf(2, 1, 0).u, 0.8125f);
        EXPECT_FLOAT_EQ(VertexOf(2, 1, 0).v, 0.5f);
        EXPECT_FLOAT_EQ(edit.thumbHeight, 117.0f * static_cast<float>(edit.visibleLines) / 20.0f);
        EXPECT_FLOAT_EQ(edit.thumbY, 109.0f + 117.0f * 3.0f / 20.0f);
    }
}

// 0x468d50 on the down arrow: the first time it scrolls one line with sound 0x19 and starts the counter, which rises
// up to 15 (without a parent) and from 16 scrolls on every step; without the click, the counters go back to 0. The
// click gives the focus (EM_SETSEL(−2, −1): nothing selected), the mouse to the UI and 0x77f3b10 to 0 and 0x6c8dcc
// to 6.
TEST_F(OldEditTest, TheDownArrowScrollsWithTheSoundAndRepeats)
{
    OldEdit edit = TheBody(Screen640);
    std::string text;
    for (int line = 0; line < 30; ++line)
        text += "r\r\n";
    edit.text.SetText(text);
    edit.text.SelectAll();
    m_leftClick = true;

    Step(edit, 290, 230);

    EXPECT_EQ(edit.firstLine, 1);
    EXPECT_EQ(m_sound, Client::Audio::SoundId{0x19});
    EXPECT_EQ(edit.arrowRepeat, 1);
    EXPECT_EQ(m_mouseOnUi, 1);
    EXPECT_EQ(m_focus, ChatFocus::OldWindow);
    EXPECT_EQ(m_manager.focusedEdit, 7);
    EXPECT_EQ(m_focusedList, 7);
    EXPECT_FALSE(edit.text.AllSelected());
    EXPECT_EQ(m_mouseUpdateTime, 0);
    EXPECT_EQ(m_mouseUpdateTimeMax, 6);
    m_sound.reset();
    for (int step = 0; step < 15; ++step)
        Step(edit, 290, 230);
    EXPECT_EQ(edit.firstLine, 1);
    EXPECT_EQ(edit.arrowRepeat, 16);
    Step(edit, 290, 230);
    EXPECT_EQ(edit.firstLine, 2);
    EXPECT_FALSE(m_sound.has_value());
    m_leftClick = false;
    Step(edit, 290, 230);
    EXPECT_EQ(edit.arrowRepeat, 0);
}

// With the bar drawn (+0xd4..+0xe4): the click on the track, below the thumb, turns a page, without sound; the click
// on the thumb grabs it (state 2, the EDIT's mouse and +0xe8) and the mouse drags it: EM_LINESCROLL to 0 and to
// ftol(31 · ((y − 109 + 0.5) / 117)); without the click, it lets go.
TEST_F(OldEditTest, TheThumbIsDraggedAndTheTrackPages)
{
    OldEdit edit = TheBody(Screen640);
    std::string text;
    for (int line = 0; line < 30; ++line)
        text += "r\r\n";
    edit.text.SetText(text);
    Draw(edit, Screen640, false);
    ASSERT_FLOAT_EQ(edit.thumbY, 109.0f);
    ASSERT_FLOAT_EQ(edit.thumbHeight, 117.0f * 10.0f / 31.0f);
    m_leftClick = true;

    Step(edit, 290, 150);
    EXPECT_EQ(edit.firstLine, 10);
    EXPECT_EQ(edit.trackRepeat, 1);
    EXPECT_FALSE(m_sound.has_value());

    Draw(edit, Screen640, false);
    edit.trackRepeat = 0;
    const std::int32_t grabY = static_cast<std::int32_t>(edit.thumbY) + 2;
    Step(edit, 290, grabY);
    EXPECT_EQ(edit.base.state, 2);
    EXPECT_EQ(m_capture, 7);
    EXPECT_FLOAT_EQ(edit.dragOffset, static_cast<float>(grabY) - edit.thumbY);

    Step(edit, 0, grabY + 30);
    const float thumb = static_cast<float>(grabY + 30) - edit.dragOffset;
    EXPECT_FLOAT_EQ(edit.thumbY, thumb);
    EXPECT_EQ(edit.firstLine, static_cast<std::int32_t>(31.0 * ((static_cast<double>(thumb) - 109.0 + 0.5) / 117.0)));
    m_leftClick = false;
    Step(edit, 0, grabY + 30);
    EXPECT_EQ(edit.base.state, 0);
    EXPECT_EQ(m_capture, 0);
}

// In a one-line EDIT (the chat's), the click on the right edge also goes through the arrows: EM_SCROLL does nothing
// but the 0x19 plays (Q-C1009); the thumb and the track, without 0x468970, are 0 × 0.
TEST_F(OldEditTest, TheRightEdgeOfASingleLineEditSounds)
{
    OldEdit edit;
    edit.base.id = 3;
    edit.base.x = 20;
    edit.base.y = 40;
    edit.base.width = 0xb4;
    edit.base.height = 0xe;
    m_leftClick = true;

    Step(edit, 20 + 0xb4 - 10, 42);

    EXPECT_EQ(m_sound, Client::Audio::SoundId{0x19});
    EXPECT_EQ(edit.firstLine, 0);
    EXPECT_EQ(edit.base.state, 0);
    EXPECT_EQ(m_manager.focusedEdit, 3);
}
} // namespace
