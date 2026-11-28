#include "Messenger/FriendWindowsDraw.h"

#include "Core/Log.h"
#include "Core/ILogSink.h"
#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextBox.h"
#include "Render/Text/TextCache.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Chat/ChatInput.h"
#include "Messenger/FriendModel.h"
#include "Messenger/FriendWindows.h"
#include "Ui/GameUiState.h"
#include "Messenger/LetterModel.h"
#include "Messenger/LetterWindows.h"
#include "Ui/OldControls.h"
#include "Ui/OldWindowBase.h"
#include "Ui/OldWindowDraw.h"
#include "Ui/OldWindowDrawParts.h"
#include "Ui/OldWindows.h"
#include "Render/Frame/TextCacheDrawer.h"
#include "Audio/SoundId.h"
#include "support/FakeTextureStore.h"
#include "support/RecordingSink.h"
#include "support/RecordingTextDrawer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

// The friends window's draw (CL9 milestone 9; 06 § HUD y ventanas, "Los amigos"): 0x4a2620 with the headers
// (0x4a2460), the state button (0x463090), the friends tab (0x49cc20), the letters tab (0x4a0a30, CL9 milestone 10)
// or the windows tab (0x49e7f0) with its list (0x47a810, the bar 0x461890 and the rows 0x461cd0, 0x4641f0 and
// 0x462a60), and the add box (0x4a3750) with its EDIT (0x468320). The friends window sits at (0x186, 0x106), 250 ×
// 170: the inner area at (395, 283) of 240 × 144, the tabs at (395, 304) of 240 × 125 and their lists with the bottom
// edge at 407, 86 high. The names and texts are made up.
namespace
{
using Client::Chat::ChatFocus;
using Client::Core::PixelSize;
using Client::Render::BlendMode;
using Client::Render::Color;
using Client::Render::DrawCommand;
using Client::Render::SpriteBatch;
using Client::Render::SpriteVertex;
using Client::Render::TextureSlot;
using Client::Render::Text::FontKind;
using Client::Render::Text::ITextMeasure;
using Client::Render::Text::TextColors;
using Client::Render::Text::TextExtent;
using Client::Tests::RecordingTextDrawer;
using Client::Tests::TextMarkerSlot;
using Client::Ui::FriendSend;
using Client::Ui::OldEdit;
using Client::Ui::OldList;
using Client::Ui::OldWindow;
using Client::Ui::OldWindowDrawContext;
using Client::Ui::OldWindowDrawView;
using Client::Ui::OldWindowGlobals;
using Client::Ui::OldWindowManager;
using Client::Ui::OldWindowMeasure;
using Client::Ui::OldWindowStepView;
using Client::Ui::OldWindowTexts;

constexpr PixelSize Screen640{640, 480};
constexpr TextureSlot ScrollbarSlot{0x649};
constexpr TextureSlot CheckSlot{0x64a};
constexpr std::uint32_t Dim = 0xffc8dce6u;
constexpr std::uint32_t White = 0xffffffffu;
constexpr std::uint32_t Black = 0xff000000u;
constexpr std::uint8_t EnterKey = 0x0d;

class LetterMeasure final : public ITextMeasure
{
public:
    [[nodiscard]] TextExtent Measure(FontKind /*font*/, std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * RecordingTextDrawer::LetterWidth, RecordingTextDrawer::Height};
    }
};

class PixelCaretRasterizer final : public Client::Render::Text::IGlyphRasterizer
{
public:
    [[nodiscard]] TextExtent Measure(FontKind /*font*/, std::string_view text) const override
    {
        int width = 0;
        for (const char character : text)
            width += Advance(character);
        return {width, 13};
    }

    void DrawInk(FontKind /*font*/, std::string_view text, int firstColumn,
                 Client::Render::Text::InkMask ink) const override
    {
        int glyphLeft = 0;
        for (const char character : text)
        {
            SetInk(ink, glyphLeft - firstColumn, 0);
            if (character == 'c')
            {
                for (int row = 3; row < 6; ++row)
                    SetInk(ink, glyphLeft + 1 - firstColumn, row);
            }
            glyphLeft += Advance(character);
        }
    }

private:
    static int Advance(char character)
    {
        return character == 'c' ? 5 : 6;
    }

    static void SetInk(Client::Render::Text::InkMask ink, int column, int row)
    {
        if (column >= 0 && column < Client::Render::Text::TextBoxWidth && row >= 0 &&
            row < Client::Render::Text::TextBoxHeight)
        {
            ink[static_cast<std::size_t>(row * Client::Render::Text::TextBoxWidth + column)] = 0xff;
        }
    }
};

std::string TextOf(std::size_t index)
{
    return "t" + std::to_string(index);
}

// A quad at 640 × 480 with y down: vertex 0 is the top-left one and 2 the bottom-right one.
struct Box
{
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

void ExpectBox(const Box& box, float left, float top, float right, float bottom)
{
    EXPECT_NEAR(box.left, left, 1e-4f);
    EXPECT_NEAR(box.top, top, 1e-4f);
    EXPECT_NEAR(box.right, right, 1e-4f);
    EXPECT_NEAR(box.bottom, bottom, 1e-4f);
}

// 0x491960's color with alpha at 1.
Color PaletteOf(std::int32_t index)
{
    return *Client::Ui::OldWindowPalette(index, 1.0f);
}

struct ExpectedCommand
{
    std::optional<TextureSlot> texture;
    BlendMode blend;
    std::size_t quads;
};

// A model friend: an aggregate and not a std::pair, which with an int literal in the state would convert to
// std::uint8_t inside <utility> (C4244 with /WX in MSVC).
struct NewFriend
{
    std::string name;
    std::uint8_t state = 0;
};

// A text: what it says, where and in which color.
using ExpectedText = std::tuple<std::string, int, int, std::uint32_t>;

class FriendWindowsDrawTest : public ::testing::Test
{
protected:
    FriendWindowsDrawTest() : m_text(m_sprites)
    {
        m_sprites.Begin(Screen640);
    }

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

    OldWindowStepView StepView()
    {
        return OldWindowStepView{.mouseX = 639,
                                 .mouseY = 479,
                                 .keyEdge = [this](std::uint8_t key)
                                 { return key == EnterKey && Client::Ui::LatchTheKey(m_enterLatch, m_enterHeld); },
                                 .keyDown = {},
                                 .chatOpen = false,
                                 .texts = &m_texts,
                                 .measure = &m_measure,
                                 .send = [this](const FriendSend& send) { m_sent.push_back(send); },
                                 .whisperTo = {},
                                 .openFriends = {},
                                 .portrait = [this](Client::Ui::OldPortraitPart& /*part*/,
                                                    Client::Ui::PortraitCallKind call,
                                                    std::int32_t /*delta*/) -> Client::Core::Expected<void, std::string>
                                 {
                                     m_portraitCalls.push_back(call);
                                     return {};
                                 }};
    }

    // 0x491080 the first time, with the friends in the model and copied to the tab (0x49ddd0).
    void OpenFriends(const std::vector<NewFriend>& friends = {})
    {
        const auto opened = Client::Messenger::OpenTheFriendsWindow(
            m_manager, Client::Messenger::FriendsWindowX, Client::Messenger::FriendsWindowY, Globals(), StepView());
        ASSERT_TRUE(opened.HasValue()) << opened.Error();
        for (const NewFriend& entry : friends)
            Client::Messenger::AddAFriend(m_manager.friendModel, entry.name, 0, entry.state);
        Client::Messenger::RefreshTheFriendTab(m_manager);
    }

    OldWindow& Friends()
    {
        return m_manager.windows.at(m_manager.friends);
    }

    OldList& FriendList()
    {
        return Friends().tabs[Client::Messenger::FriendTabNumber].tab.list;
    }

    OldWindowDrawContext Context()
    {
        return OldWindowDrawContext{.sprites = m_sprites, .text = m_text, .dcFont = m_dcFont, .textColors = m_colors};
    }

    // 0x490390 with the friends window open.
    Client::Core::Expected<void, std::string> Draw(std::int32_t x = 0, std::int32_t y = 0)
    {
        const OldWindowGlobals globals = Globals();
        const OldWindowStepView stepView = StepView();
        const OldWindowDrawView view{
            .mouseX = x,
            .mouseY = y,
            .leftClick = m_leftClick,
            .friendsOpen = true,
            .texts = &m_texts,
            .focusedList = m_focusedList,
            .focusedEdit = m_focusedEdit,
            .portrait = [this](Client::Ui::OldPortraitPart& /*part*/) -> Client::Core::Expected<void, std::string>
            {
                ++m_portraitDraws;
                return {};
            },
            .globals = m_withStepContext ? &globals : nullptr,
            .stepView = m_withStepContext ? &stepView : nullptr};
        return Client::Ui::DrawTheOldWindows(m_manager, Context(), view);
    }

    const SpriteVertex& VertexOf(std::size_t command, std::size_t quad = 0, std::size_t corner = 0) const
    {
        return m_sprites.Vertices()[m_sprites.Commands()[command].firstVertex + quad * 6 + corner];
    }

    Box QuadOf(std::size_t command, std::size_t quad = 0) const
    {
        const SpriteVertex& first = VertexOf(command, quad, 0);
        const SpriteVertex& last = VertexOf(command, quad, 2);
        return Box{.left = first.x, .top = 480.0f - first.y, .right = last.x, .bottom = 480.0f - last.y};
    }

    void ExpectCommands(const std::vector<ExpectedCommand>& expected) const
    {
        const std::span<const DrawCommand> commands = m_sprites.Commands();
        ASSERT_GE(commands.size(), expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            SCOPED_TRACE(i);
            EXPECT_EQ(commands[i].texture, expected[i].texture);
            EXPECT_EQ(commands[i].blend, expected[i].blend);
            EXPECT_EQ(commands[i].vertexCount, expected[i].quads * 6);
        }
    }

    void ExpectTexts(std::size_t from, const std::vector<ExpectedText>& expected) const
    {
        ASSERT_GE(m_text.texts.size(), from + expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            SCOPED_TRACE(from + i);
            const auto& text = m_text.texts[from + i];
            EXPECT_EQ(text.text, std::get<0>(expected[i]));
            EXPECT_EQ(text.x, std::get<1>(expected[i]));
            EXPECT_EQ(text.y, std::get<2>(expected[i]));
            EXPECT_EQ(text.style.colors, (TextColors{.text = std::get<3>(expected[i]), .background = 0}));
            EXPECT_EQ(text.style.font, FontKind::Normal);
        }
    }

    bool m_withStepContext = false;
    std::vector<Client::Ui::PortraitCallKind> m_portraitCalls;
    int m_portraitDraws = 0;
    bool m_enterHeld = false;
    std::int32_t m_enterLatch = 0;
    std::vector<FriendSend> m_sent;
    LetterMeasure m_letters;
    const OldWindowTexts m_texts{.notice = "Aviso", .question = "Pregunta", .yes = "Si", .no = "No", .lookup = TextOf};
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
    std::int32_t m_mouseUpdateTime = 0;
    std::int32_t m_mouseUpdateTimeMax = 6;
    std::int32_t m_focusedEdit = 0;
    FontKind m_dcFont = FontKind::Bold;
    TextColors m_colors{};
};

// The friends tab with two friends and the second chosen, drawn by 0x4a2620 before the frame: the black below, the
// list's bar without blend (0x49cc20 leaves it), the rows from bottom to top (the chosen one with its stripe and in
// black), the four buttons, the columns' stripe with its lines and titles; then the headers' black stripe, the three
// headers, their titles, the state's and its checkbox.
TEST_F(FriendWindowsDrawTest, TheFriendTabIsDrawnLikeTheOriginal)
{
    OpenFriends({{"Zorzal", 2}, {"Hornero", 0xff}});
    Client::Ui::SelectInTheList(FriendList(), 2, 2);

    const auto drawn = Draw();
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();

    const TextureSlot button{Client::Ui::WindowButtonSlot};
    ExpectCommands({
        {std::nullopt, BlendMode::Alpha, 2},   // the frame's shadow and the black under the list
        {ScrollbarSlot, BlendMode::Opaque, 2}, // the arrows
        {std::nullopt, BlendMode::Opaque, 2},  // the track's edges
        {ScrollbarSlot, BlendMode::Opaque, 3}, // the thumb
        {TextMarkerSlot, BlendMode::Alpha, 2}, // "Zorzal" and its location
        {std::nullopt, BlendMode::Alpha, 1},   // the chosen one's stripe
        {TextMarkerSlot, BlendMode::Alpha, 2}, // "Hornero" and its location
        {button, BlendMode::Alpha, 1},
        {TextMarkerSlot, BlendMode::Alpha, 1},
        {button, BlendMode::Alpha, 1},
        {TextMarkerSlot, BlendMode::Alpha, 1},
        {button, BlendMode::Alpha, 1},
        {TextMarkerSlot, BlendMode::Alpha, 1},
        {button, BlendMode::Alpha, 1},
        {TextMarkerSlot, BlendMode::Alpha, 1},
        {std::nullopt, BlendMode::Alpha, 5},   // the lines, the columns' stripe, the middle line and its mark
        {TextMarkerSlot, BlendMode::Alpha, 2}, // the column titles
        {std::nullopt, BlendMode::Alpha, 14},  // the headers' stripe, the three and the line
        {TextMarkerSlot, BlendMode::Alpha, 4}, // the three titles and the state's
        {std::nullopt, BlendMode::Opaque, 4},  // the checkbox
        // The frame continues with the normal blend that 0x463090 left.
        {TextureSlot{Client::Ui::WindowTitleSlot}, BlendMode::Alpha, 1},
    });

    // 0x49cc20: the 7 at (395, 304 + 86 + 0x12) of 240 × (125 − 86 − 18).
    ExpectBox(QuadOf(0, 1), 395.0f, 408.0f, 635.0f, 429.0f);
    EXPECT_EQ(VertexOf(0, 1).color, PaletteOf(7));
    // 0x461890: the 13 × 13 arrows at (x + w − 12, y − h − 1) and (x + w − 12, y − 12).
    ExpectBox(QuadOf(1, 0), 623.0f, 320.0f, 636.0f, 333.0f);
    EXPECT_FLOAT_EQ(VertexOf(1, 0).u, 0.0f);
    EXPECT_FLOAT_EQ(VertexOf(1, 0).v, 0.09375f);
    ExpectBox(QuadOf(1, 1), 623.0f, 395.0f, 636.0f, 408.0f);
    EXPECT_FLOAT_EQ(VertexOf(1, 1).v, 0.5f);
    // With fewer friends than rows, the thumb is the whole track, with its ends of 1.
    ExpectBox(QuadOf(2, 0), 623.0f, 333.0f, 624.0f, 395.0f);
    ExpectBox(QuadOf(2, 1), 635.0f, 333.0f, 636.0f, 395.0f);
    EXPECT_EQ(VertexOf(2).color, PaletteOf(2));
    ExpectBox(QuadOf(3, 0), 624.0f, 333.0f, 635.0f, 395.0f);
    EXPECT_FLOAT_EQ(VertexOf(3, 0).v, 0.03125f);
    ExpectBox(QuadOf(3, 1), 624.0f, 333.0f, 635.0f, 334.0f);
    ExpectBox(QuadOf(3, 2), 624.0f, 394.0f, 635.0f, 395.0f);
    EXPECT_FLOAT_EQ(VertexOf(3, 2).v, 0.0625f);
    // 0x461cd0: the chosen one's stripe, 3 above its row, of w − 13 + 1 × 13, at 0.7 because 0x491080 gave the keyboard
    // arrows to the friend list.
    EXPECT_EQ(m_focusedList, FriendList().base.id);
    ExpectBox(QuadOf(5), 395.0f, 321.0f, 623.0f, 334.0f);
    EXPECT_FLOAT_EQ(VertexOf(5).color.alpha, 0.7f);
    // 0x49cc20: the line of the 2 at y + 0x10, the stripe of the 5 of 16, the line over the buttons, the middle one at
    // x + 68 (the name column's width) of 125 − 22 − 17 and its mark of the 14, of 10, at y + 3.
    ExpectBox(QuadOf(15, 0), 395.0f, 320.0f, 635.0f, 321.0f);
    ExpectBox(QuadOf(15, 1), 395.0f, 304.0f, 635.0f, 320.0f);
    EXPECT_EQ(VertexOf(15, 1).color, PaletteOf(5));
    ExpectBox(QuadOf(15, 2), 395.0f, 407.0f, 635.0f, 408.0f);
    ExpectBox(QuadOf(15, 3), 463.0f, 321.0f, 464.0f, 407.0f);
    EXPECT_EQ(VertexOf(15, 3).color, PaletteOf(2));
    ExpectBox(QuadOf(15, 4), 463.0f, 307.0f, 464.0f, 317.0f);
    EXPECT_EQ(VertexOf(15, 4).color, PaletteOf(14));
    // 0x4a2620: the 7 of 20 at (395, 283); 0x4a2460(395, 285, 0x35, 0x13, 3, 0): the view's with its top, left and
    // right edges and the fill of the 5, the others with the top, right and bottom ones, 1 lower, and the fill of the
    // 6; the line of the 2 from x + 0x9f, 0x12 below.
    ExpectBox(QuadOf(17, 0), 395.0f, 283.0f, 635.0f, 303.0f);
    ExpectBox(QuadOf(17, 1), 395.0f, 285.0f, 448.0f, 286.0f);
    ExpectBox(QuadOf(17, 2), 394.0f, 285.0f, 395.0f, 304.0f);
    ExpectBox(QuadOf(17, 3), 447.0f, 285.0f, 448.0f, 304.0f);
    ExpectBox(QuadOf(17, 4), 395.0f, 286.0f, 447.0f, 304.0f);
    EXPECT_EQ(VertexOf(17, 4).color, PaletteOf(5));
    ExpectBox(QuadOf(17, 5), 448.0f, 286.0f, 501.0f, 287.0f);
    ExpectBox(QuadOf(17, 6), 500.0f, 286.0f, 501.0f, 304.0f);
    ExpectBox(QuadOf(17, 7), 448.0f, 303.0f, 501.0f, 304.0f);
    ExpectBox(QuadOf(17, 8), 448.0f, 287.0f, 500.0f, 303.0f);
    EXPECT_EQ(VertexOf(17, 8).color, PaletteOf(6));
    ExpectBox(QuadOf(17, 9), 501.0f, 286.0f, 554.0f, 287.0f);
    ExpectBox(QuadOf(17, 13), 554.0f, 303.0f, 635.0f, 304.0f);
    // 0x463090: the 9 × 9 checkbox of the 0, 14 + 1 left of the state text and 1 higher.
    ExpectBox(QuadOf(19, 0), 588.0f, 287.0f, 597.0f, 288.0f);
    ExpectBox(QuadOf(19, 1), 588.0f, 295.0f, 597.0f, 296.0f);
    ExpectBox(QuadOf(19, 2), 588.0f, 287.0f, 589.0f, 296.0f);
    ExpectBox(QuadOf(19, 3), 596.0f, 287.0f, 597.0f, 296.0f);
    EXPECT_EQ(VertexOf(19).color, PaletteOf(0));

    // The rows from bottom to top: the deque's first at the bottom. The location, 4 after the name column: server 2
    // with format 1042 and offline with the 1039.
    ExpectTexts(0, {
                       {"Zorzal", 398, 337, Dim},
                       {"t1042", 470, 337, Dim},
                       {"Hornero", 398, 324, Black},
                       {"t1039", 470, 324, Black},
                   });
    // The column titles at (x + 4, y + 3), the sort one in white; the headers' centered in their place ((52 − 30 + 0.5)
    // · 0.5 and (24 − 13 + 0.5) · 0.5), the view's in white; the state's against the edge.
    ExpectTexts(8, {
                       {"t1021", 399, 307, White},
                       {"t1022", 467, 307, Dim},
                       {"t1032", 406, 288, White},
                       {"t1034", 460, 288, Dim},
                       {"t1033", 513, 288, Dim},
                       {"t1035", 603, 288, Dim},
                   });
    EXPECT_EQ(m_text.texts.size(), 15u);
    EXPECT_EQ(m_dcFont, FontKind::Normal);
}

// 0x49cf3c..0x49d099: the column title under the mouse and the sort one go in white, and after each the color goes
// back to 0xffc8dce6.
TEST_F(FriendWindowsDrawTest, TheColumnTitleUnderTheMouseAndTheSortedOneAreWhite)
{
    OpenFriends({{"Zorzal", 2}});
    m_manager.friendModel.sortMode = Client::Messenger::ByStateSort;

    ASSERT_TRUE(Draw(400, 310).HasValue());

    // The row and its location, the four buttons and the two titles.
    ExpectTexts(6, {{"t1021", 399, 307, White}, {"t1022", 467, 307, White}});
}

// With the list scrolled and the click on the top arrow: the arrow flipped and the bar's thumb in its place (+0x110
// and +0x118 of 0x47b9c0), with its ends.
TEST_F(FriendWindowsDrawTest, TheBarArrowTurnsOverWithTheClickAndTheThumbFollowsTheScroll)
{
    OpenFriends({{"Ana", 0},
                 {"Bruno", 0},
                 {"Carla", 0},
                 {"Dario", 0},
                 {"Elsa", 0},
                 {"Fabio", 0},
                 {"Gala", 0},
                 {"Hugo", 0},
                 {"Irene", 0},
                 {"Juan", 0}});
    FriendList().scroll = 2;
    m_leftClick = true;

    ASSERT_TRUE(Draw(625, 322).HasValue());

    // The top one from (0.8125, 0.90625) with −0.8125 × −0.40625; the bottom one, without the mouse, as always.
    EXPECT_FLOAT_EQ(VertexOf(1, 0, 0).u, 0.8125f);
    EXPECT_FLOAT_EQ(VertexOf(1, 0, 0).v, 0.90625f);
    EXPECT_FLOAT_EQ(VertexOf(1, 0, 2).u, 0.0f);
    EXPECT_FLOAT_EQ(VertexOf(1, 0, 2).v, 0.5f);
    EXPECT_FLOAT_EQ(VertexOf(1, 1, 0).u, 0.0f);
    EXPECT_FLOAT_EQ(VertexOf(1, 1, 0).v, 0.5f);
    const OldList& list = FriendList();
    ASSERT_LT(list.thumbHeight, list.barBottom - list.barTop);
    ExpectBox(QuadOf(3, 0), 624.0f, list.thumbY, 635.0f, list.thumbY + list.thumbHeight);
    ExpectBox(QuadOf(3, 1), 624.0f, list.thumbY, 635.0f, list.thumbY + 1.0f);
    ExpectBox(QuadOf(3, 2), 624.0f, list.thumbY + list.thumbHeight - 1.0f, 635.0f, list.thumbY + list.thumbHeight);
    // Six rows from the deque's third.
    ASSERT_GE(m_text.texts.size(), 12u);
    EXPECT_EQ(m_text.texts[0].text, Friends().tabs[0].tab.friends[2].name);
    EXPECT_EQ(m_text.texts[10].text, Friends().tabs[0].tab.friends[7].name);
}

// With +0xe0 at 1 (went offline), the checkbox carries win_check.tga's mark (0x64a) of 5 × 5 at (x + 2, y + 2).
TEST_F(FriendWindowsDrawTest, TheStateBoxIsMarkedOffline)
{
    OpenFriends();
    m_manager.offlineSent = 1;

    ASSERT_TRUE(Draw().HasValue());

    const std::span<const DrawCommand> commands = m_sprites.Commands();
    const auto mark = std::find_if(commands.begin(), commands.end(),
                                   [](const DrawCommand& command) { return command.texture == CheckSlot; });
    ASSERT_NE(mark, commands.end());
    const auto at = static_cast<std::size_t>(mark - commands.begin());
    EXPECT_EQ(mark->blend, BlendMode::Alpha);
    ExpectBox(QuadOf(at), 590.0f, 289.0f, 595.0f, 294.0f);
    EXPECT_FLOAT_EQ(VertexOf(at, 0, 2).u, 0.625f);
}

// 0x49e7f0: the windows tab does not go back to the opaque blend after the black, draws each box's title at x + 8,
// its button, the lines without the middle one and title 1026 at (x + 8, y + 3). The chosen one's stripe goes at 0.3:
// the keyboard arrows belong to the friend list.
TEST_F(FriendWindowsDrawTest, TheWindowsTabDrawsTheBoxTitles)
{
    OpenFriends();
    const auto opened = Client::Ui::CreateTheOldWindow(
        m_manager,
        Client::Ui::OldWindowRequest{
            .kind = Client::Ui::NoticeCase, .x = -1, .y = -1, .data = "aviso uno", .aux = 0, .flags = 0},
        m_texts, m_measure);
    ASSERT_TRUE(opened.HasValue()) << opened.Error();
    Friends().friends.currentTab = Client::Messenger::WindowsTabNumber;
    Friends().friends.hoverTab = Client::Messenger::WindowsTabNumber;

    ASSERT_TRUE(Draw().HasValue());

    ExpectCommands({
        {std::nullopt, BlendMode::Alpha, 2}, // the shadow and the black under the list
        {ScrollbarSlot, BlendMode::Alpha, 2},
        {std::nullopt, BlendMode::Alpha, 2},
        {ScrollbarSlot, BlendMode::Alpha, 3},
        {std::nullopt, BlendMode::Alpha, 1},   // the chosen one's stripe
        {TextMarkerSlot, BlendMode::Alpha, 1}, // "aviso uno"
        {TextureSlot{Client::Ui::WindowButtonSlot}, BlendMode::Alpha, 1},
        {TextMarkerSlot, BlendMode::Alpha, 1},
        {std::nullopt, BlendMode::Alpha, 3},   // the two lines and the column's stripe
        {TextMarkerSlot, BlendMode::Alpha, 1}, // "t1026"
        {std::nullopt, BlendMode::Alpha, 14},
    });
    EXPECT_FLOAT_EQ(VertexOf(4).color.alpha, 0.3f);
    // The view's header is the third: its edges from 501 and the fill of the 5.
    ExpectBox(QuadOf(10, 9), 501.0f, 285.0f, 554.0f, 286.0f);
    ExpectBox(QuadOf(10, 12), 501.0f, 286.0f, 553.0f, 304.0f);
    EXPECT_EQ(VertexOf(10, 12).color, PaletteOf(5));
    ExpectTexts(0, {{"aviso uno", 403, 324, Black}});
    ExpectTexts(2, {
                       {"t1026", 403, 307, Dim},
                       {"t1032", 406, 288, Dim},
                       {"t1034", 460, 288, Dim},
                       {"t1033", 513, 288, White},
                   });
}

// Without the focus on the view's, the header title under the mouse (+0xb4) also goes in white.
TEST_F(FriendWindowsDrawTest, TheHeaderUnderTheMouseIsWhite)
{
    OpenFriends();
    Friends().friends.hoverTab = Client::Messenger::WindowsTabNumber;

    ASSERT_TRUE(Draw().HasValue());

    ExpectTexts(6, {
                       {"t1032", 406, 288, White},
                       {"t1034", 460, 288, Dim},
                       {"t1033", 513, 288, White},
                   });
}

// 0x4a2620 calls 0x45aa50: the step with 1 handles the functional queue before repositioning; it does not defer the
// command. The 4 (0x49dbd4) opens the write-a-letter window in the same draw.
TEST_F(FriendWindowsDrawTest, MovingTheWindowProcessesThePendingTabCommandDuringDrawing)
{
    m_withStepContext = true;
    OpenFriends();
    Friends().base.state = Client::Ui::DraggedWindow;
    Client::Ui::PostToTheWindow(m_manager, Friends().tabs[0].base.id, Client::Ui::CommandMessage, 4, 0);

    const auto drawn = Draw();

    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_TRUE(Friends().tabs[0].base.queue.empty());
    // 0x497f50: the write window's portrait, with the game's hook also in the draw.
    EXPECT_FALSE(m_portraitCalls.empty());
    EXPECT_TRUE(std::ranges::any_of(m_manager.windows, [](const auto& entry)
                                    { return entry.second.kind == Client::Ui::OldWindowKind::LetterWriteWindow; }));
}

// 0x49d422 and 0x49d118: Name and Answer keep their order, send during the real pass and do not come back on the next
// step.
TEST_F(FriendWindowsDrawTest, DrawingSendsPendingNamesAndDeleteAnswersInOrderOnce)
{
    m_withStepContext = true;
    OpenFriends({{"Tero", 1}});
    Client::Ui::SelectInTheList(FriendList(), 1, 1);
    Friends().base.state = Client::Ui::DraggedWindow;
    Friends().tabs[0].base.queue.push_back(
        Client::Ui::OldMessage{.type = Client::Ui::NameMessage, .a = 0, .b = 1, .text = "Tordo"});
    Client::Ui::PostToTheWindow(m_manager, Friends().tabs[0].base.id, Client::Ui::AnswerMessage, 0, 1);

    ASSERT_TRUE(Draw().HasValue());

    EXPECT_EQ(m_sent,
              (std::vector<FriendSend>{{.kind = Client::Ui::FriendSendKind::Add, .value = 0, .name = "Tordo"},
                                       {.kind = Client::Ui::FriendSendKind::Delete, .value = 0, .name = "Tero"}}));
    EXPECT_TRUE(Friends().tabs[0].base.queue.empty());
    ASSERT_TRUE(Client::Ui::StepTheOldWindows(m_manager, Globals(), StepView()).HasValue());
    EXPECT_EQ(m_sent.size(), 2u);
}

// 0x4a43eb checks Enter even with arg1: the just-created question answers and the tab sends C3 in Draw.
TEST_F(FriendWindowsDrawTest, DrawingADeleteCommandReadsEnterAndKeepsTheManagersClosePending)
{
    m_withStepContext = true;
    OpenFriends({{"Tero", 1}});
    Client::Ui::SelectInTheList(FriendList(), 1, 1);
    Friends().base.state = Client::Ui::DraggedWindow;
    m_enterHeld = true;
    Client::Ui::PostToTheWindow(m_manager, Friends().tabs[0].base.id, Client::Ui::CommandMessage, 2, 0);

    ASSERT_TRUE(Draw().HasValue());

    ASSERT_EQ(m_sent.size(), 1u);
    EXPECT_EQ(m_sent[0], (FriendSend{.kind = Client::Ui::FriendSendKind::Delete, .value = 0, .name = "Tero"}));
    EXPECT_EQ(m_enterLatch, 1);
    ASSERT_NE(m_manager.modal, 0);
    EXPECT_TRUE(m_manager.windows.contains(m_manager.modal));
    EXPECT_TRUE(
        std::ranges::any_of(m_manager.queue, [this](const Client::Ui::OldMessage& message)
                            { return message.type == Client::Ui::CloseMessage && message.a == m_manager.modal; }));
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(m_sent.size(), 1u);
}

// 0x490390 advances the list after drawing: Command1 adds the box; the empty caret stays clipped by the bitmap.
TEST_F(FriendWindowsDrawTest, DrawingAnAddCommandClipsTheNewEmptyEditCaretInTheSamePass)
{
    m_withStepContext = true;
    OpenFriends();
    Friends().base.state = Client::Ui::DraggedWindow;
    Client::Ui::PostToTheWindow(m_manager, Friends().tabs[0].base.id, Client::Ui::CommandMessage, 1, 0);

    ASSERT_TRUE(Draw().HasValue());

    ASSERT_NE(m_manager.letter, 0);
    const OldEdit& edit = m_manager.windows.at(m_manager.letter).add.edit;
    EXPECT_EQ(m_focus, ChatFocus::OldWindow);
    EXPECT_EQ(m_manager.focusedEdit, edit.base.id);
    EXPECT_EQ(edit.draws, 1);
    const auto caret = std::ranges::find_if(
        m_sprites.Vertices(), [](const SpriteVertex& vertex)
        { return vertex.color == Color{200.0f / 255.0f, 200.0f / 255.0f, 200.0f / 255.0f, 1.0f}; });
    EXPECT_EQ(caret, m_sprites.Vertices().end());
}

TEST_F(FriendWindowsDrawTest, AFunctionalDrawMessageWithoutStepServicesIsAnExplicitError)
{
    OpenFriends();
    Friends().base.state = Client::Ui::DraggedWindow;
    Client::Ui::PostToTheWindow(m_manager, Friends().tabs[0].base.id, Client::Ui::CommandMessage, 1, 0);

    const auto drawn = Draw();

    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x45aa50"), std::string::npos);
    ASSERT_FALSE(Friends().tabs[0].base.queue.empty());
    EXPECT_EQ(Friends().tabs[0].base.queue.front().type, Client::Ui::CommandMessage);
}

// The letters tab (0x4a0a30, CL9 milestone 10) with two letters, the first marked and unread, the second read and
// chosen, all marked and sorted by date: the black below, the list's bar without blend, the rows (0x4641f0) from bottom
// to top with their checkbox, envelope and three texts in boxes, the columns' stripe with its three lines and marks,
// the all checkbox and the envelope, the titles (the sort one in white) and the four buttons; then, the friends
// window's.
TEST_F(FriendWindowsDrawTest, TheLettersTabIsDrawnLikeTheOriginal)
{
    OpenFriends();
    Client::Messenger::AddALetter(m_manager.letterModel, 1, "Tordo", "Asado", "2026/11/13", "21:30", 0);
    Client::Messenger::AddALetter(m_manager.letterModel, 2, "Calandria", "Mate", "2026/11/12", "08:15", 1);
    Client::Messenger::RefreshTheLettersTab(m_manager);
    OldWindow& tab = Friends().tabs[Client::Messenger::LettersTabNumber];
    ASSERT_EQ(tab.tab.letters.size(), 2u);
    tab.tab.letters[0].selected = 1;
    tab.tab.allChecked = 1;
    Client::Ui::SelectInTheList(tab.tab.list, 2, 2);
    m_manager.letterModel.sortMode = Client::Messenger::ByDateLetters;
    Friends().friends.currentTab = Client::Messenger::LettersTabNumber;
    // 0x463190: the mark column of 0x19 and the text ones of 5 letters, + 8.
    ASSERT_EQ(tab.tab.list.headers, 0x19);
    ASSERT_EQ(tab.tab.list.nameWidth, 38);
    ASSERT_EQ(tab.tab.list.placeWidth, 38);
    ASSERT_EQ(tab.tab.list.subjectWidth, 38);

    const auto drawn = Draw();
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();

    const TextureSlot button{Client::Ui::WindowButtonSlot};
    const TextureSlot mail{0x64b};
    std::vector<ExpectedCommand> expected{
        {std::nullopt, BlendMode::Alpha, 2},   // the frame's shadow and the black under the list
        {ScrollbarSlot, BlendMode::Opaque, 2}, // the bar
        {std::nullopt, BlendMode::Opaque, 2},
        {ScrollbarSlot, BlendMode::Opaque, 3},
        {std::nullopt, BlendMode::Opaque, 4}, // "Tordo": the checkbox, marked, the envelope and the texts
        {CheckSlot, BlendMode::Alpha, 1},
        {mail, BlendMode::Alpha, 1},
        {TextMarkerSlot, BlendMode::Alpha, 3},
        {std::nullopt, BlendMode::Alpha, 1}, // "Calandria", chosen: the stripe
        {std::nullopt, BlendMode::Opaque, 4},
        {mail, BlendMode::Alpha, 1},
        {TextMarkerSlot, BlendMode::Alpha, 3},
        {std::nullopt, BlendMode::Alpha, 9},  // the columns' stripe, the three lines and the three marks
        {std::nullopt, BlendMode::Opaque, 4}, // the all checkbox, marked, and the envelope
        {CheckSlot, BlendMode::Alpha, 1},
        {mail, BlendMode::Alpha, 1},
        {TextMarkerSlot, BlendMode::Alpha, 3}, // the column titles
    };
    for (std::size_t at = 0; at < 4; ++at)
    {
        expected.push_back({button, BlendMode::Alpha, 1});
        expected.push_back({TextMarkerSlot, BlendMode::Alpha, 1});
    }
    expected.push_back({std::nullopt, BlendMode::Alpha, 14});
    expected.push_back({TextMarkerSlot, BlendMode::Alpha, 4});
    expected.push_back({std::nullopt, BlendMode::Opaque, 4});
    expected.push_back({TextureSlot{Client::Ui::WindowTitleSlot}, BlendMode::Alpha, 1});
    ExpectCommands(expected);

    // 0x4a0ac1: the 7 at (395, 304 + 86 + 0x12) of 240 × (125 − 86 − 18).
    ExpectBox(QuadOf(0, 1), 395.0f, 408.0f, 635.0f, 429.0f);
    // 0x4641f0: the checkbox at (x + 1, y − 1) and the 13 × 9 envelope at (x + 0xb, y − 1); the read one's, 13 × 10 at
    // y − 2 and from 0.28125 of the texture.
    ExpectBox(QuadOf(4, 0), 396.0f, 336.0f, 405.0f, 337.0f);
    ExpectBox(QuadOf(6), 406.0f, 336.0f, 419.0f, 345.0f);
    EXPECT_FLOAT_EQ(VertexOf(6, 0, 0).v, 0.0f);
    EXPECT_FLOAT_EQ(VertexOf(6, 0, 2).u, 0.8125f);
    EXPECT_FLOAT_EQ(VertexOf(6, 0, 2).v, 0.28125f);
    ExpectBox(QuadOf(8), 395.0f, 321.0f, 623.0f, 334.0f);
    ExpectBox(QuadOf(9, 0), 396.0f, 323.0f, 405.0f, 324.0f);
    ExpectBox(QuadOf(10), 406.0f, 322.0f, 419.0f, 332.0f);
    EXPECT_FLOAT_EQ(VertexOf(10, 0, 0).v, 0.28125f);
    EXPECT_FLOAT_EQ(VertexOf(10, 0, 2).v, 0.59375f);
    // 0x4a0c53..0x4a0e70: the lines at x + 0x19, x + 0x19 + 38 and x + 0x19 + 76, of 125 − 22 − 17 from y + 0x11, and
    // their marks of the 14, of 10, at y + 3.
    ExpectBox(QuadOf(12, 3), 420.0f, 321.0f, 421.0f, 407.0f);
    ExpectBox(QuadOf(12, 4), 458.0f, 321.0f, 459.0f, 407.0f);
    ExpectBox(QuadOf(12, 5), 496.0f, 321.0f, 497.0f, 407.0f);
    EXPECT_EQ(VertexOf(12, 5).color, PaletteOf(2));
    ExpectBox(QuadOf(12, 6), 420.0f, 307.0f, 421.0f, 317.0f);
    ExpectBox(QuadOf(12, 8), 496.0f, 307.0f, 497.0f, 317.0f);
    EXPECT_EQ(VertexOf(12, 6).color, PaletteOf(14));
    // 0x4a0e91..0x4a0efd: the all checkbox at (x + 1, y + 3) and the envelope at (x + 0xb, y + 3).
    ExpectBox(QuadOf(13, 0), 396.0f, 307.0f, 405.0f, 308.0f);
    ExpectBox(QuadOf(15), 406.0f, 307.0f, 419.0f, 316.0f);

    // The name 4 after the mark in a box of 38 − 4, the date without "20" centered in the 38 one (48 columns, from the
    // 5th) and the subject 4 after the date, in what is left up to the bar: 240 − 13 − 101 − 4.
    ExpectTexts(0, {
                       {"Tordo", 424, 337, Dim},
                       {"26/11/13", 458, 337, Dim},
                       {"Asado", 500, 337, Dim},
                       {"Calandria", 424, 324, Black},
                       {"26/11/12", 458, 324, Black},
                       {"Mate", 500, 324, Black},
                       {"t1028", 424, 307, Dim},
                       {"t1029", 462, 307, White},
                       {"t1030", 500, 307, Dim},
                   });
    const std::vector<std::tuple<int, int>> boxes{{0, 34}, {5, 38}, {0, 122}, {0, 34}, {5, 38}, {0, 122}};
    for (std::size_t at = 0; at < boxes.size(); ++at)
    {
        SCOPED_TRACE(at);
        EXPECT_TRUE(m_text.texts[at].windowed);
        EXPECT_EQ(m_text.texts[at].firstColumn, std::get<0>(boxes[at]));
        EXPECT_EQ(m_text.texts[at].columns, std::get<1>(boxes[at]));
    }
}

// At 800 × 600 the one-row text boxes go in pixels: ftol(38 · 1.25 − 4), ftol(38 · 1.25) and ftol(122 · 1.25). The
// title under the mouse goes in white.
TEST_F(FriendWindowsDrawTest, TheLetterBoxesAreInPixels)
{
    OpenFriends();
    Client::Messenger::AddALetter(m_manager.letterModel, 1, "Tordo", "Asado", "2026/11/13", "21:30", 0);
    Client::Messenger::RefreshTheLettersTab(m_manager);
    Friends().friends.currentTab = Client::Messenger::LettersTabNumber;
    m_sprites.Begin(PixelSize{800, 600});

    ASSERT_TRUE(Client::Messenger::DrawTheFriendsWindow(
                    m_manager, Friends(), Context(),
                    OldWindowDrawView{.mouseX = 500, .mouseY = 310, .friendsOpen = true, .texts = &m_texts})
                    .HasValue());

    ASSERT_GE(m_text.texts.size(), 6u);
    EXPECT_EQ(m_text.texts[0].columns, 43);
    // (47 − 48) / 2 truncates toward 0: the date from its column 0, with its 48.
    EXPECT_EQ(m_text.texts[1].firstColumn, 0);
    EXPECT_EQ(m_text.texts[1].columns, 48);
    EXPECT_EQ(m_text.texts[2].columns, 152);
    // The mouse at (500, 310): over the subject column, from x + 0x19 + 76 of 38 × 0x13.
    ExpectTexts(3, {{"t1028", 424, 307, Dim}, {"t1029", 462, 307, Dim}, {"t1030", 500, 307, White}});
}

// The add box (0x4a3750): the two buttons and, with the normal blend, the EDIT (0x468320): with +0x4c & 8 the black of
// w × the height of "1", the bitmap of 1.5 + 1.5 + cx wide with the background at 0, the text and, with the focus,
// the caret in the column after the text. Ends without blend, and the frame's title bar comes out like that.
TEST_F(FriendWindowsDrawTest, TheAddBoxDrawsItsEditWithTheCaret)
{
    OpenFriends();
    ASSERT_TRUE(Client::Ui::StepTheOldWindows(m_manager, Globals(), StepView()).HasValue());
    Client::Ui::PostToTheWindow(m_manager, Friends().tabs[0].base.id, Client::Ui::CommandMessage, 1, 0);
    ASSERT_TRUE(Client::Ui::StepTheOldWindows(m_manager, Globals(), StepView()).HasValue());
    ASSERT_NE(m_manager.letter, 0);
    OldEdit& edit = m_manager.windows.at(m_manager.letter).add.edit;
    ASSERT_EQ(edit.base.x, 35);
    ASSERT_EQ(edit.base.y, 367);
    edit.text.Type("Tero");
    edit.text.SetCursor(2);
    m_focusedEdit = edit.base.id;

    ASSERT_TRUE(Draw().HasValue());

    const std::span<const DrawCommand> commands = m_sprites.Commands();
    std::optional<std::size_t> black;
    for (std::size_t i = 0; i + 1 < commands.size(); ++i)
    {
        if (!commands[i].texture && commands[i].vertexCount == 12 &&
            VertexOf(i).color == Color{.red = 0.0f, .green = 0.0f, .blue = 0.0f, .alpha = 1.0f} &&
            commands[i + 1].texture == TextMarkerSlot)
            black = i;
    }
    ASSERT_TRUE(black.has_value());
    ASSERT_GE(commands.size(), *black + 3);
    EXPECT_EQ(commands[*black].blend, BlendMode::Alpha);
    ExpectBox(QuadOf(*black, 0), 35.0f, 367.0f, 115.0f, 380.0f);
    ExpectBox(QuadOf(*black, 1), 35.0f, 367.0f, 62.0f, 381.0f);
    EXPECT_FLOAT_EQ(VertexOf(*black, 1).color.alpha, 0.0f);
    ASSERT_EQ(commands[*black + 1].texture, TextMarkerSlot);
    ASSERT_EQ(commands[*black + 1].vertexCount, 18u);
    ExpectBox(QuadOf(*black + 1, 0), 38.0f, 367.0f, 51.0f, 380.0f);
    ExpectBox(QuadOf(*black + 1, 1), 52.0f, 367.0f, 62.0f, 380.0f);
    ExpectBox(QuadOf(*black + 1, 2), 51.0f, 367.0f, 52.0f, 380.0f);
    EXPECT_EQ(commands[*black + 2].texture, TextureSlot{Client::Ui::WindowTitleSlot});
    EXPECT_EQ(commands[*black + 2].blend, BlendMode::Opaque);
    EXPECT_EQ(edit.draws, 1);
    EXPECT_FLOAT_EQ(edit.caretHeight, 13.0f);
    const auto tero =
        std::find_if(m_text.texts.begin(), m_text.texts.end(), [](const auto& text) { return text.text == "Tero"; });
    ASSERT_NE(tero, m_text.texts.end());
    EXPECT_EQ(tero->x, 38);
    EXPECT_EQ(tero->y, 367);
    EXPECT_TRUE(tero->pixelPosition);
    EXPECT_EQ(tero->style.colors, (TextColors{.text = White, .background = 0}));
    EXPECT_EQ(std::ranges::count_if(m_text.texts, [](const auto& text) { return text.text == "Tero"; }), 3);
    EXPECT_EQ(tero->firstColumn, 0);
    EXPECT_EQ(tero->columns, 13);
    const auto trailing = std::find_if(m_text.texts.begin(), m_text.texts.end(),
                                       [](const auto& text) { return text.text == "Tero" && text.firstColumn == 14; });
    ASSERT_NE(trailing, m_text.texts.end());
    EXPECT_EQ(trailing->x, 52);
    EXPECT_EQ(trailing->columns, 10);
    const auto caret = std::find_if(m_text.texts.begin(), m_text.texts.end(),
                                    [](const auto& text) { return text.text == "Tero" && text.columns == 1; });
    ASSERT_NE(caret, m_text.texts.end());
    EXPECT_EQ(caret->x, 51);
    EXPECT_EQ(caret->firstColumn, 13);
    EXPECT_EQ(caret->style.colors, (TextColors{.text = edit.backgroundColor, .background = 0xffc8c8c8u}));
}

TEST(OldEditBitmapIntegrationTest, PixelCaretInvertsTheRasterizedGlyphColumn)
{
    using Client::Core::Log;
    using Client::Core::LogLevel;
    using Client::Core::PixelSize;
    using Client::Render::SpriteBatch;
    using Client::Render::TextureSlot;
    using Client::Render::Frame::TextCacheDrawer;
    using Client::Render::Text::TextCache;
    using Client::Tests::FakeTextureStore;
    using Client::Tests::RecordingSink;

    SpriteBatch sprites;
    sprites.Begin(PixelSize{1024, 768});
    PixelCaretRasterizer rasterizer;
    FakeTextureStore textures;
    RecordingSink sink;
    Log log(sink, LogLevel::Debug);
    TextCache cache(rasterizer, textures);
    TextCacheDrawer text(cache, sprites, log);

    OldEdit edit;
    edit.base.x = 149;
    edit.base.y = 40;
    edit.base.width = 80;
    edit.base.height = 14;
    edit.text.SetText("abcd");
    edit.text.Select(0, 2);
    edit.textColor = 0xff123456u;
    edit.backgroundColor = 0x00000000u;
    FontKind dcFont = FontKind::Normal;
    TextColors textColors{};
    const OldWindowDrawContext context{.sprites = sprites, .text = text, .dcFont = dcFont, .textColors = textColors};

    Client::Ui::OldDraw::DrawTheOldEdit(context, edit, true);

    const TextureSlot selectedSlot{static_cast<std::uint16_t>(Client::Render::FirstTextTextureSlot.value)};
    const auto selected = textures.Find(selectedSlot);
    ASSERT_TRUE(selected.has_value());
    const auto& selectedPixels = selected->texture.pixels;
    EXPECT_EQ(selectedPixels[0], 0x56);
    EXPECT_EQ(selectedPixels[1], 0x34);
    EXPECT_EQ(selectedPixels[2], 0x12);
    EXPECT_EQ(selectedPixels[3], 0xff);
    const std::size_t selectedBackground = 256u * 4u;
    EXPECT_EQ(selectedPixels[selectedBackground], 0x96);
    EXPECT_EQ(selectedPixels[selectedBackground + 1], 0x96);
    EXPECT_EQ(selectedPixels[selectedBackground + 2], 0x96);
    EXPECT_EQ(selectedPixels[selectedBackground + 3], 0xff);
    EXPECT_EQ(edit.textColor, 0xff123456u);

    const TextureSlot caretSlot{static_cast<std::uint16_t>(Client::Render::FirstTextTextureSlot.value + 3)};
    const auto caret = textures.Find(caretSlot);
    ASSERT_TRUE(caret.has_value());
    const auto& caretPixels = caret->texture.pixels;
    std::size_t transparentRows = 0;
    std::size_t grayRows = 0;
    for (int row = 0; row < 13; ++row)
    {
        const std::size_t pixel = static_cast<std::size_t>(row * 256 * 4);
        const bool glyphPixel = row >= 3 && row < 6;
        if (glyphPixel)
        {
            EXPECT_EQ(caretPixels[pixel], 0);
            EXPECT_EQ(caretPixels[pixel + 1], 0);
            EXPECT_EQ(caretPixels[pixel + 2], 0);
            EXPECT_EQ(caretPixels[pixel + 3], 0);
            ++transparentRows;
        }
        else
        {
            EXPECT_EQ(caretPixels[pixel], 0xc8);
            EXPECT_EQ(caretPixels[pixel + 1], 0xc8);
            EXPECT_EQ(caretPixels[pixel + 2], 0xc8);
            EXPECT_EQ(caretPixels[pixel + 3], 0xff);
            ++grayRows;
        }
    }
    EXPECT_EQ(transparentRows, 3u);
    EXPECT_EQ(grayRows, 10u);

    std::vector<const Client::Render::DrawCommand*> caretCommands;
    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        if (command.texture == caretSlot)
            caretCommands.push_back(&command);
    }
    ASSERT_EQ(caretCommands.size(), 1u);
    const Client::Render::DrawCommand& caretCommand = *caretCommands.front();
    ASSERT_EQ(caretCommand.vertexCount, 6u);
    const auto vertices = sprites.Vertices();
    const Client::Render::SpriteVertex& topLeft = vertices[caretCommand.firstVertex];
    const Client::Render::SpriteVertex& bottomRight = vertices[caretCommand.firstVertex + 2];
    EXPECT_FLOAT_EQ(topLeft.x, 254.0f);
    EXPECT_FLOAT_EQ(bottomRight.x, 255.0f);
    EXPECT_FLOAT_EQ(768.0f - topLeft.y, 64.0f);
    EXPECT_FLOAT_EQ(768.0f - bottomRight.y, 77.0f);

    for (const Client::Render::DrawCommand& command : sprites.Commands())
    {
        if (!command.texture)
            continue;
        for (std::size_t first = command.firstVertex; first < command.firstVertex + command.vertexCount; first += 6)
        {
            const float left = vertices[first].x;
            const float right = vertices[first + 2].x;
            if (left <= 254.0f && right > 254.0f)
            {
                EXPECT_EQ(command.texture, std::optional<TextureSlot>{caretSlot});
            }
        }
    }
    EXPECT_TRUE(sink.lines.empty());
}

TEST_F(FriendWindowsDrawTest, TheEditDrawsTextAtItsPixelMarginAndCaretAtTheByteCursor)
{
    OldEdit edit;
    edit.base.x = 20;
    edit.base.y = 40;
    edit.base.width = 80;
    edit.base.height = 14;
    edit.text.SetText("abcd");
    edit.text.SetCursor(2);

    Client::Ui::OldDraw::DrawTheOldEdit(Context(), edit, true);

    ASSERT_EQ(m_text.texts.size(), 3u);
    EXPECT_EQ(m_text.texts[0].text, "abcd");
    EXPECT_EQ(m_text.texts[1].text, "abcd");
    EXPECT_EQ(m_text.texts[2].text, "abcd");
    EXPECT_TRUE(m_text.texts[0].pixelPosition);
    EXPECT_TRUE(m_text.texts[1].pixelPosition);
    EXPECT_TRUE(m_text.texts[2].pixelPosition);
    EXPECT_EQ(m_text.texts[0].x, 23);
    EXPECT_EQ(m_text.texts[0].y, 40);
    EXPECT_EQ(m_text.texts[0].firstColumn, 0);
    EXPECT_EQ(m_text.texts[0].columns, 13);
    EXPECT_EQ(m_text.texts[1].x, 37);
    EXPECT_EQ(m_text.texts[1].firstColumn, 14);
    EXPECT_EQ(m_text.texts[1].columns, 10);
    EXPECT_EQ(m_text.texts[2].x, 36);
    EXPECT_EQ(m_text.texts[2].firstColumn, 13);
    EXPECT_EQ(m_text.texts[2].columns, 1);
    EXPECT_EQ(m_text.texts[2].style.colors, (TextColors{.text = Black, .background = 0xffc8c8c8u}));
    ASSERT_EQ(m_sprites.Commands().size(), 2u);
    ASSERT_EQ(m_sprites.Vertices().size(), 24u);
    ExpectBox(QuadOf(1, 2), 36.0f, 40.0f, 37.0f, 53.0f);
}

TEST_F(FriendWindowsDrawTest, TheEditClipsTheCaretAtTheEndOfText)
{
    OldEdit edit;
    edit.base.x = 20;
    edit.base.y = 40;
    edit.base.width = 80;
    edit.base.height = 14;
    edit.text.SetText("abcd");

    Client::Ui::OldDraw::DrawTheOldEdit(Context(), edit, true);

    ASSERT_EQ(m_text.texts.size(), 1u);
    EXPECT_EQ(m_sprites.Vertices().size(), 12u);
}

TEST_F(FriendWindowsDrawTest, TheEditClipsTheCaretForEmptyText)
{
    OldEdit edit;
    edit.base.x = 20;
    edit.base.y = 40;
    edit.base.width = 80;
    edit.base.height = 14;

    Client::Ui::OldDraw::DrawTheOldEdit(Context(), edit, true);

    EXPECT_TRUE(m_text.texts.empty());
    EXPECT_EQ(m_sprites.Vertices().size(), 6u);
}

TEST_F(FriendWindowsDrawTest, TheEditDrawsSelectionAndCaretFromFullTextColumns)
{
    OldEdit edit;
    edit.base.x = 20;
    edit.base.y = 40;
    edit.base.width = 80;
    edit.base.height = 14;
    edit.text.SetText("abcd");
    edit.text.Select(1, 3);

    Client::Ui::OldDraw::DrawTheOldEdit(Context(), edit, true);

    ASSERT_EQ(m_text.texts.size(), 5u);
    EXPECT_EQ(m_text.texts[0].text, "abcd");
    EXPECT_EQ(m_text.texts[0].x, 23);
    EXPECT_EQ(m_text.texts[0].firstColumn, 0);
    EXPECT_EQ(m_text.texts[0].columns, 6);
    EXPECT_EQ(m_text.texts[0].style.colors, (TextColors{.text = White, .background = 0}));
    EXPECT_EQ(m_text.texts[1].text, "abcd");
    EXPECT_EQ(m_text.texts[1].x, 29);
    EXPECT_EQ(m_text.texts[1].firstColumn, 6);
    EXPECT_EQ(m_text.texts[1].columns, 12);
    EXPECT_EQ(m_text.texts[1].style.colors, (TextColors{.text = White, .background = 0xff969696u}));
    EXPECT_EQ(m_text.texts[2].text, "abcd");
    EXPECT_EQ(m_text.texts[2].x, 41);
    EXPECT_EQ(m_text.texts[2].firstColumn, 18);
    EXPECT_EQ(m_text.texts[2].columns, 1);
    EXPECT_EQ(m_text.texts[2].style.colors, (TextColors{.text = White, .background = 0}));
    EXPECT_EQ(m_text.texts[3].x, 43);
    EXPECT_EQ(m_text.texts[3].firstColumn, 20);
    EXPECT_EQ(m_text.texts[3].columns, 4);
    EXPECT_EQ(m_text.texts[3].style.colors, (TextColors{.text = White, .background = 0}));
    EXPECT_TRUE(m_text.texts[0].pixelPosition);
    EXPECT_TRUE(m_text.texts[1].pixelPosition);
    EXPECT_TRUE(m_text.texts[2].pixelPosition);
    EXPECT_EQ(m_text.texts[4].text, "abcd");
    EXPECT_EQ(m_text.texts[4].x, 42);
    EXPECT_EQ(m_text.texts[4].firstColumn, 19);
    EXPECT_EQ(m_text.texts[4].columns, 1);
    EXPECT_EQ(m_text.texts[4].style.colors, (TextColors{.text = Black, .background = 0xffc8c8c8u}));
    EXPECT_TRUE(m_text.texts[4].pixelPosition);
    ASSERT_EQ(m_sprites.Vertices().size(), 36u);
    ExpectBox(QuadOf(1, 4), 42.0f, 40.0f, 43.0f, 53.0f);
}

TEST_F(FriendWindowsDrawTest, TheEditSelectionUsesTheEditTextColor)
{
    OldEdit edit;
    edit.base.x = 20;
    edit.base.y = 40;
    edit.base.width = 80;
    edit.base.height = 14;
    edit.textColor = 0xff123456u;
    edit.text.SetText("abcd");
    edit.text.Select(1, 3);

    Client::Ui::OldDraw::DrawTheOldEdit(Context(), edit, true);

    ASSERT_EQ(m_text.texts.size(), 5u);
    EXPECT_EQ(m_text.texts[0].style.colors.text, 0xff123456u);
    EXPECT_EQ(m_text.texts[1].style.colors.text, 0xff123456u);
    EXPECT_EQ(m_text.texts[1].style.colors.background, 0xff969696u);
    EXPECT_EQ(m_text.texts[2].style.colors.text, 0xff123456u);
    EXPECT_EQ(m_text.texts[3].style.colors.text, 0xff123456u);
    EXPECT_EQ(m_text.texts[4].style.colors.text, edit.backgroundColor);
}

TEST_F(FriendWindowsDrawTest, TheEditUsesTheActiveCaretForReverseSelection)
{
    OldEdit edit;
    edit.base.x = 20;
    edit.base.y = 40;
    edit.base.width = 80;
    edit.base.height = 14;
    edit.text.SetText("abcd");
    edit.text.Select(3, 1);

    Client::Ui::OldDraw::DrawTheOldEdit(Context(), edit, true);

    ASSERT_EQ(m_text.texts.size(), 5u);
    EXPECT_EQ(m_text.texts[4].x, 30);
    EXPECT_EQ(m_text.texts[4].firstColumn, 7);
    EXPECT_EQ(m_text.texts[4].columns, 1);
    EXPECT_EQ(m_text.texts[4].style.colors, (TextColors{.text = Black, .background = 0xffc8c8c8u}));
    ExpectBox(QuadOf(1, 4), 30.0f, 40.0f, 31.0f, 53.0f);
}

TEST_F(FriendWindowsDrawTest, TheEditHidesSelectionWhenUnfocusedButKeepsItInTheModel)
{
    OldEdit edit;
    edit.base.x = 20;
    edit.base.y = 40;
    edit.base.width = 80;
    edit.base.height = 14;
    edit.text.SetText("abcd");
    edit.text.Select(1, 3);

    Client::Ui::OldDraw::DrawTheOldEdit(Context(), edit, false);

    ASSERT_EQ(m_text.texts.size(), 1u);
    EXPECT_TRUE(edit.text.HasSelection());
    EXPECT_EQ(m_text.texts[0].text, "abcd");
    EXPECT_EQ(m_text.texts[0].style.colors, (TextColors{.text = White, .background = 0}));
    EXPECT_EQ(m_sprites.Vertices().size(), 12u);
}

// 0x4684bc..0x4684e4: the caret shows with the Windows focus and +0x80 % 24 < 12; hidden (the vtable + 4 with state
// 3), the EDIT neither draws nor counts.
TEST_F(FriendWindowsDrawTest, TheEditCaretBlinksAndAHiddenEditDrawsNothing)
{
    OldEdit edit;
    edit.base.x = 20;
    edit.base.y = 40;
    edit.base.width = 80;
    edit.base.height = 14;
    edit.text.Type("ab");
    edit.text.SetCursor(1);
    const auto quads = [this] { return m_sprites.Vertices().size() / 6; };

    edit.draws = 11;
    Client::Ui::OldDraw::DrawTheOldEdit(Context(), edit, true);
    // The bitmap, the two text spans and the caret column.
    EXPECT_EQ(quads(), 4u);
    EXPECT_EQ(edit.draws, 12);

    m_sprites.Begin(Screen640);
    Client::Ui::OldDraw::DrawTheOldEdit(Context(), edit, true);
    EXPECT_EQ(quads(), 2u);
    EXPECT_EQ(edit.draws, 13);

    m_sprites.Begin(Screen640);
    edit.draws = 24;
    Client::Ui::OldDraw::DrawTheOldEdit(Context(), edit, false);
    EXPECT_EQ(quads(), 2u);

    m_sprites.Begin(Screen640);
    edit.base.state = Client::Ui::HiddenWindow;
    Client::Ui::OldDraw::DrawTheOldEdit(Context(), edit, true);
    EXPECT_EQ(quads(), 0u);
    EXPECT_EQ(edit.draws, 25);
}

// 0x46854d: GetWindowTextA's WM_GETTEXT goes through the subclass (0x467b10), which with an arrow held sets +0x80 to 0
// after the draw chose the caret; the INC leaves it at 1, and the caret stays on while it is held. Hidden, the EDIT
// leaves earlier (IsWindowVisible) and does not count.
TEST_F(FriendWindowsDrawTest, AHeldArrowKeepsTheEditCaretOn)
{
    OldEdit edit;
    edit.base.x = 20;
    edit.base.y = 40;
    edit.base.width = 80;
    edit.base.height = 14;
    edit.text.Type("ab");
    // The caret at the start: at the end of the text it would be outside the bitmap.
    edit.text.SetCursor(0);
    const auto caretDrawn = [this]
    {
        return std::ranges::any_of(m_text.texts, [](const auto& text)
                                   { return text.columns == 1 && text.style.colors.background == 0xffc8c8c8u; });
    };
    OldWindowDrawContext held = Context();
    held.arrowHeld = true;

    edit.draws = 13;
    Client::Ui::OldDraw::DrawTheOldEdit(held, edit, true);
    // The bitmap and the text: the caret was chosen with 13.
    EXPECT_FALSE(caretDrawn());
    EXPECT_EQ(edit.draws, 1);

    m_sprites.Begin(Screen640);
    m_text.texts.clear();
    Client::Ui::OldDraw::DrawTheOldEdit(held, edit, true);
    EXPECT_TRUE(caretDrawn());
    EXPECT_EQ(edit.draws, 1);

    // Without the focus the WM_GETTEXT arrives the same.
    edit.draws = 20;
    Client::Ui::OldDraw::DrawTheOldEdit(held, edit, false);
    EXPECT_EQ(edit.draws, 1);

    edit.draws = 20;
    edit.base.state = Client::Ui::HiddenWindow;
    Client::Ui::OldDraw::DrawTheOldEdit(held, edit, true);
    EXPECT_EQ(edit.draws, 20);
}

// 0x468584..0x4685ec, the EDIT with ES_PASSWORD (the dialog's number 0x7d, CL9 milestone 8): the text is one '*' per
// byte, the bitmap measures the '*' in the bold font and the DC stays with the normal one; the caret, at the end of
// the '*'.
TEST_F(FriendWindowsDrawTest, AMaskedEditDrawsStarsAndLeavesTheNormalFont)
{
    OldEdit edit = Client::Chat::NumberFieldEdit();
    edit.base.state = 0;
    edit.base.x = 0xf3;
    edit.base.y = 0x62;
    edit.base.width = 0x46;
    edit.base.height = 0xe;
    edit.text.Type("4321");
    m_dcFont = FontKind::Bold;

    Client::Ui::OldDraw::DrawTheOldEdit(Context(), edit, true);

    EXPECT_EQ(m_dcFont, FontKind::Normal);
    ASSERT_EQ(m_text.texts.size(), 1u);
    EXPECT_EQ(m_text.texts[0].text, "****");
    EXPECT_EQ(m_text.texts[0].x, 0xf3 + 3);
    EXPECT_EQ(m_text.texts[0].columns, 24);
    EXPECT_EQ(m_text.texts[0].style.font, FontKind::Normal);
    EXPECT_EQ(edit.text.Value(), "4321");
    // The bitmap (ftol(1.5 + 1.5 + 24) = 27 wide, measured with the '*' in bold) and the text's 24 columns 3 further
    // in. The caret, at column 3 + 24 + 1 of the normal font, is outside the bitmap: it is not drawn.
    const std::span<const SpriteVertex> vertices = m_sprites.Vertices();
    ASSERT_EQ(vertices.size(), 2u * 6u);
    EXPECT_FLOAT_EQ(vertices[0].x, 243.0f);
    const float right = std::max({vertices[0].x, vertices[1].x, vertices[2].x, vertices[3].x});
    EXPECT_FLOAT_EQ(right, 243.0f + 27.0f);
    EXPECT_FLOAT_EQ(vertices[6].x, 243.0f + 3.0f);
    EXPECT_EQ(edit.draws, 1);
}
} // namespace
