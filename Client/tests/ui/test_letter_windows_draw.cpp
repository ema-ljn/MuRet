#include "Messenger/LetterWindowsDraw.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Chat/ChatInput.h"
#include "Messenger/FriendModel.h"
#include "Messenger/FriendWindows.h"
#include "Messenger/LetterModel.h"
#include "Messenger/LetterWindows.h"
#include "Ui/OldControls.h"
#include "Ui/OldWindowBase.h"
#include "Ui/OldWindowDraw.h"
#include "Ui/OldWindows.h"
#include "Audio/SoundId.h"
#include "support/RecordingTextDrawer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

// The drawing of the chat with a friend and of a letter's windows (CL9 milestone 10; 06 § HUD y ventanas, "Las cartas
// y el chat con un amigo"): 0x493fc0 with its rows (0x45f9c0, 0x460550), the members and the invitees (0x47a810 with
// 0x461cd0) and their lines (0x491ba0). The chat sits at (100, 80), 250 × 170: the inner area at (105, 101) of 240 ×
// 144, the rows with the bottom edge at 229 and 128 high, the EDIT at (107, 233) and the invite button at (291, 231).
// The read window (0x499d60, with the body 0x464d90 and 0x465770) too, 370 × 182 with the portrait: the inner area of
// 360 × 156 and the body with the bottom edge at 237, of 240 × 120. The names and texts are made up.
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
using Client::Ui::OldChatPart;
using Client::Ui::OldPortraitPart;
using Client::Ui::OldWindow;
using Client::Ui::OldWindowDrawContext;
using Client::Ui::OldWindowDrawView;
using Client::Ui::OldWindowGlobals;
using Client::Ui::OldWindowManager;
using Client::Ui::OldWindowMeasure;
using Client::Ui::OldWindowStepView;
using Client::Ui::OldWindowTexts;
using Client::Ui::PortraitCallKind;

constexpr PixelSize Screen640{640, 480};
constexpr TextureSlot ScrollbarSlot{0x649};
constexpr TextureSlot ButtonSlot{Client::Ui::WindowButtonSlot};
constexpr std::uint32_t Dim = 0xffc8dce6u;
constexpr std::string_view Hero = "Heroico";
constexpr std::int32_t ChatX = 100;
constexpr std::int32_t ChatY = 80;

class LetterMeasure final : public ITextMeasure
{
public:
    [[nodiscard]] TextExtent Measure(FontKind /*font*/, std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * RecordingTextDrawer::LetterWidth, RecordingTextDrawer::Height};
    }
};

// Each text is "t<index>", except the format of the read window's top row (1014) and the write window's labels (1000
// and 1005), of different widths.
std::string TextOf(std::size_t index)
{
    if (index == 1014)
        return "De %s el %s a las %s";
    if (index == 1000)
        return "Destinatario";
    if (index == 1005)
        return "Tema";
    return "t" + std::to_string(index);
}

// A quad at 640 × 480 with y down.
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

// A room member: an aggregate and not a std::pair (C4244 with an int literal in a std::uint8_t).
struct NewMember
{
    std::string name;
    std::uint8_t index = 0;
};

// A chat row: who (0xff, nobody), the text and the kind.
struct NewLine
{
    std::uint8_t speaker = 0xff;
    std::string text;
    std::int32_t kind = 0;
};

// A text: what it says, where and its colors.
using ExpectedText = std::tuple<std::string, int, int, TextColors>;

class LetterWindowsDrawTest : public ::testing::Test
{
protected:
    LetterWindowsDrawTest() : m_text(m_sprites)
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

    // The step with a test portrait that builds the character without drawing rand().
    OldWindowStepView StepView()
    {
        OldWindowStepView view{.mouseX = 639,
                               .mouseY = 479,
                               .keyEdge = [](std::uint8_t /*key*/) { return false; },
                               .keyDown = {},
                               .chatOpen = false,
                               .texts = &m_texts,
                               .measure = &m_measure,
                               .send = [](const FriendSend& /*send*/) {},
                               .whisperTo = {},
                               .openFriends = {}};
        view.portrait = [](OldPortraitPart& portrait, PortraitCallKind kind,
                           std::int32_t /*arg*/) -> Client::Core::Expected<void, std::string>
        {
            if (kind == PortraitCallKind::Build)
                portrait.built = 1;
            return {};
        };
        view.heroName = std::string(Hero);
        return view;
    }

    // 0x48ee60 with the 1 at (100, 80): the hero and the members (0x493c90) and the rows (0x493f80).
    OldWindow& OpenAChat(const std::vector<NewMember>& members, const std::vector<NewLine>& lines)
    {
        const OldWindowGlobals globals = Globals();
        const OldWindowStepView view = StepView();
        const auto opened = Client::Ui::CreateTheOldWindow(
            m_manager,
            Client::Ui::OldWindowRequest{
                .kind = Client::Ui::ChatCase, .x = ChatX, .y = ChatY, .data = "t994", .aux = 0, .flags = 0},
            m_texts, m_measure, &globals, &view);
        EXPECT_TRUE(opened.HasValue()) << opened.Error();
        m_chat = opened.HasValue() ? opened.Value() : 0;
        OldWindow& chat = Chat();
        EXPECT_TRUE(
            Client::Messenger::AddAChatMember(m_manager, chat, Hero, 0, 0, m_texts, Hero, &m_measure).HasValue());
        for (const NewMember& member : members)
            EXPECT_TRUE(Client::Messenger::AddAChatMember(m_manager, chat, member.name, member.index, 0, m_texts, Hero,
                                                          &m_measure)
                            .HasValue());
        for (const NewLine& line : lines)
        {
            const auto added =
                Client::Messenger::AddAChatLine(m_manager, chat, line.speaker, line.text, line.kind, 0, &m_measure);
            EXPECT_TRUE(added.HasValue()) << added.Error();
        }
        return chat;
    }

    OldWindow& Chat()
    {
        return m_manager.windows.at(m_chat);
    }

    // 0x48ee60 with the 6 at (100, 80) and the letter (0x499cb0) with its body.
    OldWindow& OpenALetter(std::string_view body)
    {
        const OldWindowGlobals globals = Globals();
        const OldWindowStepView view = StepView();
        const auto opened = Client::Ui::CreateTheOldWindow(
            m_manager,
            Client::Ui::OldWindowRequest{
                .kind = Client::Ui::LetterReadCase, .x = ChatX, .y = ChatY, .data = "t1054", .aux = 0, .flags = 0},
            m_texts, m_measure, &globals, &view);
        EXPECT_TRUE(opened.HasValue()) << opened.Error();
        m_letter = opened.HasValue() ? opened.Value() : 0;
        OldWindow& read = Letter();
        const auto set = Client::Messenger::SetTheLetter(m_manager, read,
                                                         Client::Messenger::LetterEntry{.selected = 0,
                                                                                        .index = 3,
                                                                                        .name = "Tordo",
                                                                                        .subject = "Asado",
                                                                                        .date = "2026/11/13",
                                                                                        .time = "21:30",
                                                                                        .type = 1},
                                                         body, &m_measure);
        EXPECT_TRUE(set.HasValue()) << set.Error();
        return read;
    }

    OldWindow& Letter()
    {
        return m_manager.windows.at(m_letter);
    }

    // 0x48ee60 with the 7 at (100, 80): the test portrait builds it and 0x497f50 leaves +0xb0 at 1.
    OldWindow& OpenAWrite()
    {
        const OldWindowGlobals globals = Globals();
        const OldWindowStepView view = StepView();
        const auto opened = Client::Ui::CreateTheOldWindow(
            m_manager,
            Client::Ui::OldWindowRequest{
                .kind = Client::Ui::LetterWriteCase, .x = ChatX, .y = ChatY, .data = "t1053", .aux = 0, .flags = 0},
            m_texts, m_measure, &globals, &view);
        EXPECT_TRUE(opened.HasValue()) << opened.Error();
        m_letter = opened.HasValue() ? opened.Value() : 0;
        return Letter();
    }

    // The windows' step, with the manager's queue.
    void Step()
    {
        const auto stepped = Client::Ui::StepTheOldWindows(m_manager, Globals(), StepView());
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    }

    OldWindowDrawContext Context()
    {
        return OldWindowDrawContext{.sprites = m_sprites, .text = m_text, .dcFont = m_dcFont, .textColors = m_colors};
    }

    // 0x490390 with the friends window open.
    Client::Core::Expected<void, std::string> Draw(std::int32_t x = 0, std::int32_t y = 0)
    {
        const OldWindowDrawView view{.mouseX = x,
                                     .mouseY = y,
                                     .leftClick = m_leftClick,
                                     .friendsOpen = true,
                                     .texts = &m_texts,
                                     .focusedList = m_focusedList,
                                     .focusedEdit = m_focusedEdit,
                                     .chatPlacement = m_placement,
                                     .portrait = m_portrait};
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
            EXPECT_EQ(text.style.colors, std::get<3>(expected[i]));
            EXPECT_EQ(text.style.font, FontKind::Normal);
        }
    }

    LetterMeasure m_letters;
    const OldWindowTexts m_texts{.notice = "Aviso", .question = "Pregunta", .yes = "Si", .no = "No", .lookup = TextOf};
    OldWindowMeasure m_measure{.measure = m_letters, .dcFont = FontKind::Normal, .screen = Screen640};
    OldWindowManager m_manager;
    SpriteBatch m_sprites;
    RecordingTextDrawer m_text;
    std::int32_t m_chat = 0;
    std::int32_t m_letter = 0;
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
    std::int32_t m_placement = 0;
    // The portrait's vtable + 0x14 that the game gives; empty, the portrait in view stops.
    std::function<Client::Core::Expected<void, std::string>(OldPortraitPart&)> m_portrait;
    FontKind m_dcFont = FontKind::Bold;
    TextColors m_colors{};
};

// With two members (the hero and a friend) and without the invite list: the frame's shadow, the rows' bar, the rows
// from bottom to top (the last at the bottom: the system one without a name and the friend's with theirs), the line
// of the 2 and the black below, the invite button and the EDIT without blend (0x45b2e0 removes it), and the frame.
TEST_F(LetterWindowsDrawTest, TheChatDrawsItsLinesItsBottomItsButtonAndItsEdit)
{
    OldWindow& chat = OpenAChat({{"Tordo", 1}}, {{1, "hola che", 3}, {0xff, "se sumo alguien", 1}});
    const OldChatPart& part = chat.chat;
    ASSERT_EQ(part.lines.base.x, 105);
    ASSERT_EQ(part.lines.base.y, 229);
    ASSERT_EQ(part.lines.base.width, 240);
    ASSERT_EQ(part.lines.base.height, 128);
    ASSERT_EQ(part.edit.base.x, 107);
    ASSERT_EQ(part.edit.base.y, 233);
    ASSERT_EQ(part.inviteButton.base.x, 291);
    ASSERT_EQ(part.inviteButton.base.y, 231);
    m_focusedEdit = part.edit.base.id;

    const auto drawn = Draw();
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();

    ExpectCommands({
        {std::nullopt, BlendMode::Alpha, 1},   // the frame's shadow
        {ScrollbarSlot, BlendMode::Alpha, 2},  // the rows' arrows
        {std::nullopt, BlendMode::Alpha, 2},   // the track's edges
        {ScrollbarSlot, BlendMode::Alpha, 3},  // the thumb
        {TextMarkerSlot, BlendMode::Alpha, 3}, // "se sumo alguien", "Tordo: " and "hola che"
        {std::nullopt, BlendMode::Alpha, 2},   // the line and the black below
        {ButtonSlot, BlendMode::Alpha, 1},
        {TextMarkerSlot, BlendMode::Alpha, 1}, // "t993"
        {std::nullopt, BlendMode::Opaque, 1},  // the EDIT's bitmap, without blend; the empty caret stays clipped
        {TextureSlot{Client::Ui::WindowTitleSlot}, BlendMode::Opaque, 1},
    });
    // 0x45fac0: the arrows at (x + w − 12, y − h − 1) and (x + w − 12, y − 12).
    ExpectBox(QuadOf(1, 0), 333.0f, 100.0f, 346.0f, 113.0f);
    ExpectBox(QuadOf(1, 1), 333.0f, 217.0f, 346.0f, 230.0f);
    // 0x4941ae..0x494281: the line of the 2 at (105, 144 + 101 − 16) of 240 × 1 and the 7 at (105, 230) of 240 × 15.
    ExpectBox(QuadOf(5, 0), 105.0f, 229.0f, 345.0f, 230.0f);
    EXPECT_EQ(VertexOf(5, 0).color, PaletteOf(2));
    ExpectBox(QuadOf(5, 1), 105.0f, 230.0f, 345.0f, 245.0f);
    EXPECT_EQ(VertexOf(5, 1).color, PaletteOf(7));
    ExpectBox(QuadOf(6), 291.0f, 231.0f, 344.0f, 244.0f);
    EXPECT_FLOAT_EQ(QuadOf(8).left, 107.0f);
    EXPECT_FLOAT_EQ(QuadOf(8).top, 233.0f);

    // 0x460550: row 0 at y − 0x10 and row 1, 13 higher; the name at x + 8 with ": " and the text after its width (7
    // letters of 6); the system (1) in 0xff46a5d2, a member's name (3) in 0xffefdccd and their text in 0xffc8dce6, all
    // without background.
    ExpectTexts(0, {
                       {"se sumo alguien", 113, 213, TextColors{.text = 0xff46a5d2u, .background = 0}},
                       {"Tordo: ", 113, 200, TextColors{.text = 0xffefdccdu, .background = 0}},
                       {"hola che", 155, 200, TextColors{.text = Dim, .background = 0}},
                   });
    EXPECT_EQ(m_dcFont, FontKind::Normal);
}

// With the invite list (+0xb0 at 2, the 10 with 1 of 0x4944c0): the rows 0xa0 narrower, the members and the invitees
// with their bars and the chosen one's stripe, 0x491ba0's line at 0xa0 from the right edge (with the normal blend)
// and the 0x50 one, and the send-invitation button after the EDIT.
TEST_F(LetterWindowsDrawTest, TheInvitesShowTheMembersTheirSeparatorsAndTheSendButton)
{
    Client::Messenger::AddAFriend(m_manager.friendModel, "Zorzal", 0, 3);
    Client::Messenger::AddAFriend(m_manager.friendModel, "Calandria", 0, 1);
    OpenAChat({{"Tordo", 1}}, {});
    Client::Ui::PostToTheWindow(m_manager, m_chat, Client::Ui::CommandMessage, 1, 0);
    Step();
    const OldChatPart& part = Chat().chat;
    ASSERT_EQ(part.mode, 2);
    ASSERT_EQ(part.invitable.size(), 2u);
    // 0x4944c0 makes it 0x50 wider: the inner area, 320.
    ASSERT_EQ(Chat().base.width, 0x14a);
    ASSERT_EQ(part.lines.base.width, 160);
    ASSERT_EQ(part.users.base.x, 270);
    ASSERT_EQ(part.invite.base.x, 350);

    ASSERT_TRUE(Draw().HasValue());

    ExpectCommands({
        {std::nullopt, BlendMode::Alpha, 1},
        {ScrollbarSlot, BlendMode::Alpha, 2}, // the rows, with none
        {std::nullopt, BlendMode::Alpha, 2},
        {ScrollbarSlot, BlendMode::Alpha, 5}, // its thumb and the members' arrows
        {std::nullopt, BlendMode::Alpha, 2},
        {ScrollbarSlot, BlendMode::Alpha, 3},
        {TextMarkerSlot, BlendMode::Alpha, 1}, // "Tordo", the last one in, first (0x460e70)
        {std::nullopt, BlendMode::Alpha, 1},   // the stripe of "Heroico", the deque's last (the 0xe of 0x4944c0)
        {TextMarkerSlot, BlendMode::Alpha, 1},
        {ScrollbarSlot, BlendMode::Opaque, 2}, // the invitees: 0x461cd0 leaves the opaque blend
        {std::nullopt, BlendMode::Opaque, 2},
        {ScrollbarSlot, BlendMode::Opaque, 3},
        {TextMarkerSlot, BlendMode::Alpha, 1},
        {std::nullopt, BlendMode::Alpha, 1},
        {TextMarkerSlot, BlendMode::Alpha, 1},
        {std::nullopt, BlendMode::Alpha, 8}, // the two lines, the bottom one and the black
        {ButtonSlot, BlendMode::Alpha, 1},
        {TextMarkerSlot, BlendMode::Alpha, 1}, // "t996"
        {std::nullopt, BlendMode::Opaque, 1},  // the EDIT, without the focus
        {ButtonSlot, BlendMode::Alpha, 1},
        {TextMarkerSlot, BlendMode::Alpha, 1}, // "t993"
        {TextureSlot{Client::Ui::WindowTitleSlot}, BlendMode::Opaque, 1},
    });
    // 0x491ba0(105 + 320 − 0xa0, 101, 144 − 16): the edges of the 2 at x and x + 4 and the middle of the 1 at x + 1
    // of 3.
    ExpectBox(QuadOf(15, 0), 265.0f, 101.0f, 266.0f, 229.0f);
    EXPECT_EQ(VertexOf(15, 0).color, PaletteOf(2));
    ExpectBox(QuadOf(15, 1), 269.0f, 101.0f, 270.0f, 229.0f);
    ExpectBox(QuadOf(15, 2), 266.0f, 101.0f, 269.0f, 229.0f);
    EXPECT_EQ(VertexOf(15, 2).color, PaletteOf(1));
    // And at 0x50 from the edge, and the bottom line of the whole width.
    ExpectBox(QuadOf(15, 3), 345.0f, 101.0f, 346.0f, 229.0f);
    ExpectBox(QuadOf(15, 5), 346.0f, 101.0f, 349.0f, 229.0f);
    ExpectBox(QuadOf(15, 6), 105.0f, 229.0f, 425.0f, 230.0f);
    // The send button at x + w − 0x4a, 0xe above the bottom edge.
    ExpectBox(QuadOf(19), 351.0f, 231.0f, 424.0f, 244.0f);
    // 0x461cd0: the members from x + 3, the deque's first at the bottom; without +0x130, without the location.
    ExpectTexts(0, {
                       {"Tordo", 273, 117, TextColors{.text = Dim, .background = 0}},
                       {"Heroico", 273, 104, TextColors{.text = 0xff000000u, .background = 0}},
                       {"Zorzal", 353, 117, TextColors{.text = Dim, .background = 0}},
                       {"Calandria", 353, 104, TextColors{.text = 0xff000000u, .background = 0}},
                   });
}

// With three members the rows get 0x50 narrower and the members show with the 0x50 line; without the invite list,
// neither it nor its line nor the send button.
TEST_F(LetterWindowsDrawTest, ThreeMembersShowTheMembersWithoutTheInvites)
{
    OpenAChat({{"Tordo", 1}, {"Zorzal", 2}}, {});
    ASSERT_EQ(Chat().chat.lines.base.width, 160);

    ASSERT_TRUE(Draw().HasValue());

    ExpectCommands({
        {std::nullopt, BlendMode::Alpha, 1},
        {ScrollbarSlot, BlendMode::Alpha, 2},
        {std::nullopt, BlendMode::Alpha, 2},
        {ScrollbarSlot, BlendMode::Alpha, 5},
        {std::nullopt, BlendMode::Alpha, 2},
        {ScrollbarSlot, BlendMode::Alpha, 3},
        // 0x460e70 (0x4610c9..0x4610de) chooses the first on each add: the stripe of "Zorzal", the last one in.
        {std::nullopt, BlendMode::Alpha, 1},
        {TextMarkerSlot, BlendMode::Alpha, 1},
        {TextMarkerSlot, BlendMode::Alpha, 2},
        {std::nullopt, BlendMode::Opaque, 3}, // the 0x50 line without blend (0x461cd0 leaves it)
        {std::nullopt, BlendMode::Alpha, 2},  // the bottom one and the black
    });
    ExpectBox(QuadOf(9, 0), 265.0f, 101.0f, 266.0f, 229.0f);
    EXPECT_EQ(Chat().chat.users.selection, 1);
    EXPECT_EQ(Chat().chat.members.front().name, "Zorzal");
}

// 0x460550: the name and text colors by the kind (+0x110), with no case for 6 or more (the previous ones stay); with
// 0x6c7a88 at 1, a text's black background stays at 0, and the name's does not.
TEST_F(LetterWindowsDrawTest, TheChatRowColorsGoByTheKind)
{
    OpenAChat({{"Tordo", 1}}, {{1, "a", 7}, {1, "b", 5}, {1, "c", 4}, {1, "d", 2}, {1, "e", 0}});
    m_colors = TextColors{.text = 0xff123456u, .background = 0xff000000u};
    m_placement = 1;

    ASSERT_TRUE(Draw().HasValue());

    ExpectTexts(0, {
                       {"Tordo: ", 113, 213, TextColors{.text = 0xff000000u, .background = 0x32c8ffu}},
                       {"e", 155, 213, TextColors{.text = 0xff000000u, .background = 0x32c8ffu}},
                       {"Tordo: ", 113, 200, TextColors{.text = 0xff001effu, .background = 0}},
                       {"d", 155, 200, TextColors{.text = 0xff001effu, .background = 0}},
                       {"Tordo: ", 113, 187, TextColors{.text = 0xff000000u, .background = 0xffc800u}},
                       {"c", 155, 187, TextColors{.text = 0xff000000u, .background = 0xffc800u}},
                       {"Tordo: ", 113, 174, TextColors{.text = 0xff000000u, .background = 0x96ff00u}},
                       {"b", 155, 174, TextColors{.text = 0xff000000u, .background = 0x96ff00u}},
                   });

    // The 7 first (the last row): its colors are whatever there are.
    m_sprites.Begin(Screen640);
    m_text.texts.clear();
    m_colors = TextColors{.text = 0xff123456u, .background = 0xff000000u};
    // Only the first that arrived, which is the second deque's last.
    std::deque<Client::Ui::ChatLine>& wrapped = Chat().chat.wrappedRows;
    wrapped.erase(wrapped.begin(), wrapped.end() - 1);
    Chat().chat.lines.fixedCountValue = 1;
    ASSERT_TRUE(Draw().HasValue());
    ExpectTexts(0, {
                       {"Tordo: ", 113, 213, TextColors{.text = 0xff123456u, .background = 0xff000000u}},
                       {"a", 155, 213, TextColors{.text = 0xff123456u, .background = 0}},
                   });
}

// At 800 × 600 the name's width is ftol(cx / rx): 7 letters of 6 over 1.25.
TEST_F(LetterWindowsDrawTest, TheNameWidthIsIn640)
{
    OpenAChat({{"Tordo", 1}}, {{1, "hola", 3}});
    m_sprites.Begin(PixelSize{800, 600});

    ASSERT_TRUE(Draw().HasValue());

    ExpectTexts(0, {
                       {"Tordo: ", 113, 213, TextColors{.text = 0xffefdccdu, .background = 0}},
                       {"hola", 113 + 33, 213, TextColors{.text = Dim, .background = 0}},
                   });
}

// With more rows than fit and the list scrolled, the first one shown is the scroll's counting from the last
// (0x460130's deque has the last first).
TEST_F(LetterWindowsDrawTest, TheScrolledLinesStartFromTheLast)
{
    std::vector<NewLine> lines;
    for (char letter = 'a'; letter <= 'l'; ++letter)
        lines.push_back(NewLine{.speaker = 0xff, .text = std::string(1, letter), .kind = 1});
    OldWindow& chat = OpenAChat({{"Tordo", 1}}, lines);
    const std::int32_t rows = chat.chat.lines.rows;
    ASSERT_LT(rows, 12);
    chat.chat.lines.scroll = 2;

    ASSERT_TRUE(Draw().HasValue());

    ASSERT_GE(m_text.texts.size(), static_cast<std::size_t>(rows));
    EXPECT_EQ(m_text.texts[0].text, "j");
    EXPECT_EQ(m_text.texts[0].y, 213);
    EXPECT_EQ(m_text.texts[1].text, "i");
    EXPECT_EQ(m_text.texts[static_cast<std::size_t>(rows) - 1].text,
              std::string(1, static_cast<char>('j' - (rows - 1))));
}

// 0x45f6f0 → 0x45fea0 → 0x460130: with 20 bytes or more between the name and the text, the text splits at the rows'
// width minus 0x1e (240 − 30) with the indent ftol((cx of "Tordo" + 5) / rx) = 35 on the first: 29 letters of 6 and
// the rest. The first goes with the name and the second without it, ahead: the draw puts the second below and the
// first, with "Tordo: " (42 wide, not 35), above.
TEST_F(LetterWindowsDrawTest, ALongChatLineIsWrappedWithTheIndentOfTheName)
{
    constexpr std::string_view Said = "el asado del domingo se hace en la casa de la abuela con todos";
    OldWindow& chat = OpenAChat({{"Tordo", 1}}, {{1, std::string(Said), 3}});
    const OldChatPart& part = chat.chat;
    ASSERT_EQ(part.lines.base.width, 240);
    ASSERT_EQ(part.wrappedRows.size(), 2u);
    EXPECT_EQ(part.wrappedRows[0],
              (Client::Ui::ChatLine{.name = "", .text = std::string(Said.substr(29)), .kind = 3, .extra = 0}));
    EXPECT_EQ(part.wrappedRows[1],
              (Client::Ui::ChatLine{.name = "Tordo", .text = std::string(Said.substr(0, 29)), .kind = 3, .extra = 0}));
    EXPECT_EQ(part.lines.fixedCountValue, 2);

    ASSERT_TRUE(Draw().HasValue());
    ExpectTexts(0, {
                       {std::string(Said.substr(29)), 113, 213, TextColors{.text = Dim, .background = 0}},
                       {"Tordo: ", 113, 200, TextColors{.text = 0xffefdccdu, .background = 0}},
                       {std::string(Said.substr(0, 29)), 155, 200, TextColors{.text = Dim, .background = 0}},
                   });
}

// At 800 × 600 (rx = 1.25) the indent is ftol(35 / 1.25) = 28 and 0x459c70 measures in 640: the first takes 38
// letters.
TEST_F(LetterWindowsDrawTest, TheWrappingMeasuresIn640)
{
    constexpr std::string_view Said = "el asado del domingo se hace en la casa de la abuela con todos";
    m_measure.screen = PixelSize{800, 600};
    const OldChatPart& part = OpenAChat({{"Tordo", 1}}, {{1, std::string(Said), 3}}).chat;
    ASSERT_EQ(part.wrappedRows.size(), 2u);
    EXPECT_EQ(part.wrappedRows[0].text, Said.substr(38));
    EXPECT_EQ(part.wrappedRows[1].text, Said.substr(0, 38));
}

// The third member (0x493c90) narrows the rows by 0x50 and sends them the 9 (0x479240), which first splits them again
// (0x45fea0, at 160 − 30): 16, 21, 21 and 4 letters, and then counts the rows with the height already measured.
TEST_F(LetterWindowsDrawTest, TheThirdMemberNarrowsTheLinesAndWrapsThemAgain)
{
    constexpr std::string_view Said = "el asado del domingo se hace en la casa de la abuela con todos";
    OldWindow& chat = OpenAChat({{"Tordo", 1}}, {{1, std::string(Said), 3}, {0xff, "listo", 1}});
    ASSERT_EQ(chat.chat.wrappedRows.size(), 3u);
    ASSERT_TRUE(
        Client::Messenger::AddAChatMember(m_manager, chat, "Zorzal", 2, 0, m_texts, Hero, &m_measure).HasValue());
    const OldChatPart& part = chat.chat;
    EXPECT_EQ(part.lines.base.width, 160);
    std::deque<std::string> texts;
    for (const Client::Ui::ChatLine& line : part.wrappedRows)
        texts.push_back(line.text);
    EXPECT_EQ(texts, (std::deque<std::string>{"listo", std::string(Said.substr(58)), std::string(Said.substr(37, 21)),
                                              std::string(Said.substr(16, 21)), std::string(Said.substr(0, 16))}));
    EXPECT_EQ(part.wrappedRows[4].name, "Tordo");
    EXPECT_EQ(part.wrappedRows[3].name, "");
    EXPECT_EQ(part.lines.fixedCountValue, 5);
}

// 0x460308: a text that does not fit in 10 rows makes the original read the extra one outside its stack buffer
// (Q-C1012); MuRet keeps the 10 and drops the extra one. 0x45f6f0 keeps up to 0x100 bytes, which with 3 members (the
// narrower list) do not fit.
TEST_F(LetterWindowsDrawTest, AChatLineInMoreThanTenRowsKeepsTen)
{
    OldWindow& chat = OpenAChat({{"Tordo", 1}, {"Zorzal", 2}}, {});
    std::string said;
    while (said.size() < 0x120)
        said += "la ronda de mate sigue en la plaza hasta tarde ";
    const auto added = Client::Messenger::AddAChatLine(m_manager, chat, 1, said, 3, 0, &m_measure);
    ASSERT_TRUE(added.HasValue()) << added.Error();
    ASSERT_EQ(chat.chat.lineRows.size(), 1u);
    EXPECT_EQ(chat.chat.lineRows[0].text.size(), 0x100u);
    ASSERT_EQ(chat.chat.wrappedRows.size(), 10u);
    EXPECT_EQ(chat.chat.wrappedRows.back().name, "Tordo");
    EXPECT_EQ(chat.chat.lines.fixedCountValue, 10);
}

// Dragged (state at 4), the draw (0x493fc0, without the frame) places its children again with the 8 before drawing
// them; resizing (1), also the 9 to the three lists.
TEST_F(LetterWindowsDrawTest, TheDraggedChatPlacesItsChildrenBeforeTheDraw)
{
    OldWindow& chat = OpenAChat({{"Tordo", 1}}, {});
    chat.base.x += 10;
    chat.base.y += 20;
    chat.base.state = Client::Ui::DraggedWindow;

    ASSERT_TRUE(Client::Messenger::DrawTheChatWindow(m_manager, chat, Context(), OldWindowDrawView{.texts = &m_texts})
                    .HasValue());

    const OldChatPart& part = chat.chat;
    EXPECT_EQ(part.lines.base.x, 115);
    EXPECT_EQ(part.lines.base.y, 249);
    EXPECT_EQ(part.users.base.x, 280);
    EXPECT_EQ(part.invite.base.y, 249);
    EXPECT_EQ(part.inviteButton.base.x, 301);
    EXPECT_EQ(part.sendButton.base.y, 251);
    EXPECT_EQ(part.edit.base.x, 117);
    // Not the 9: the rows' height stays.
    EXPECT_EQ(part.lines.base.height, 128);

    chat.base.height += 26;
    chat.base.state = Client::Ui::ResizedWindow;
    m_sprites.Begin(Screen640);
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(part.lines.base.height, 154);
    EXPECT_EQ(part.users.base.height, 154);
    EXPECT_EQ(part.invite.base.height, 154);
    EXPECT_EQ(part.lines.base.y, 275);
}

// The read window with three rows: the body from top to bottom (the first at the top: 0x465450 has them from last to
// first), at x + 10 and in 0xffc8dce6; without blend after them (0x465770 removes it), the line beside the portrait
// (0x78 before the right edge), the buttons' one and its black up to it, the top line and black and the 1014 with the
// last row's colors; the five buttons. At the end of the frame, the vtable + 0x54 (0x49a1d0) stops in the portrait's
// draw without the game.
TEST_F(LetterWindowsDrawTest, TheReadWindowDrawsItsBodyItsBarsAndItsButtonsUntilThePortrait)
{
    OldWindow& read = OpenALetter("linea uno\nlinea dos\nlinea tres");
    const Client::Ui::OldLetterPart& letter = read.letter;
    ASSERT_EQ(letter.portraitMode, 2);
    ASSERT_EQ(read.base.width, 370);
    ASSERT_EQ(letter.lines.base.x, 105);
    ASSERT_EQ(letter.lines.base.y, 237);
    ASSERT_EQ(letter.lines.base.width, 240);
    ASSERT_EQ(letter.lines.base.height, 120);
    ASSERT_EQ(letter.buttons[0].base.x, 107);
    ASSERT_EQ(letter.buttons[0].base.y, 241);

    const auto drawn = Draw();
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x49a1d0"), std::string::npos);
    EXPECT_NE(drawn.Error().find("0x497c10"), std::string::npos);
    EXPECT_NE(drawn.Error().find("sin el juego"), std::string::npos);

    std::vector<ExpectedCommand> expected{
        {std::nullopt, BlendMode::Alpha, 1},    {ScrollbarSlot, BlendMode::Alpha, 2},
        {std::nullopt, BlendMode::Alpha, 2},    {ScrollbarSlot, BlendMode::Alpha, 3},
        {TextMarkerSlot, BlendMode::Alpha, 3}, // the three rows
        {std::nullopt, BlendMode::Opaque, 5},  // the lines and the blacks, without blend
        {TextMarkerSlot, BlendMode::Opaque, 1},
    };
    for (std::size_t button = 0; button < 5; ++button)
    {
        expected.push_back({ButtonSlot, BlendMode::Alpha, 1});
        expected.push_back({TextMarkerSlot, BlendMode::Alpha, 1});
    }
    expected.push_back({TextureSlot{Client::Ui::WindowTitleSlot}, BlendMode::Opaque, 1});
    ExpectCommands(expected);
    // 0x499e44..0x49a0f4: the side line at (105 + 360 − 0x78, 156 + 101 − 19) of 1 × 19, the buttons' one at y + h − 20
    // of 360 − 120 and the black of 18 below; above, the line at y + 0xe and the black of 14.
    ExpectBox(QuadOf(5, 0), 345.0f, 238.0f, 346.0f, 257.0f);
    EXPECT_EQ(VertexOf(5, 0).color, PaletteOf(2));
    ExpectBox(QuadOf(5, 1), 105.0f, 237.0f, 345.0f, 238.0f);
    ExpectBox(QuadOf(5, 2), 105.0f, 238.0f, 345.0f, 256.0f);
    EXPECT_EQ(VertexOf(5, 2).color, PaletteOf(7));
    ExpectBox(QuadOf(5, 3), 105.0f, 115.0f, 465.0f, 116.0f);
    EXPECT_EQ(VertexOf(5, 3).color, PaletteOf(2));
    ExpectBox(QuadOf(5, 4), 105.0f, 101.0f, 465.0f, 115.0f);
    EXPECT_EQ(VertexOf(5, 4).color, PaletteOf(7));
    // The buttons in their order: +0x9e0, +0xa68, +0xaf0, +0xb78 and +0xc00.
    ExpectBox(QuadOf(7), 107.0f, 241.0f, 157.0f, 255.0f);
    ExpectBox(QuadOf(9), 158.0f, 241.0f, 208.0f, 255.0f);
    ExpectBox(QuadOf(11), 291.0f, 241.0f, 341.0f, 255.0f);
    ExpectBox(QuadOf(13), 209.0f, 241.0f, 249.0f, 255.0f);
    ExpectBox(QuadOf(15), 250.0f, 241.0f, 290.0f, 255.0f);

    // 0x465710 (= 0x461c70): (3 − row) · 13 − 120 − 10 + 237.
    const TextColors dim{.text = Dim, .background = 0};
    ExpectTexts(0, {
                       {"linea tres", 115, 146, dim},
                       {"linea dos", 115, 133, dim},
                       {"linea uno", 115, 120, dim},
                       {"De Tordo el 2026/11/13 a las 21:30", 108, 104, dim},
                       {"t1010", 117, 242, dim},
                   });
}

// With +0x774 below 2 (nobody sets it so in the original), the buttons' line and black span the whole width and the
// vtable + 0x54 draws nothing. Without rows, everything continues with the normal blend.
TEST_F(LetterWindowsDrawTest, WithoutThePortraitModeTheBarsAreWholeAndNothingIsCut)
{
    OldWindow& read = OpenALetter("");
    read.letter.portraitMode = 1;

    ASSERT_TRUE(Draw().HasValue());

    ExpectCommands({
        {std::nullopt, BlendMode::Alpha, 1},
        {ScrollbarSlot, BlendMode::Alpha, 2},
        {std::nullopt, BlendMode::Alpha, 2},
        {ScrollbarSlot, BlendMode::Alpha, 3},
        {std::nullopt, BlendMode::Alpha, 4},
        {TextMarkerSlot, BlendMode::Alpha, 1},
    });
    ExpectBox(QuadOf(4, 0), 105.0f, 237.0f, 465.0f, 238.0f);
    ExpectBox(QuadOf(4, 1), 105.0f, 238.0f, 465.0f, 256.0f);
    EXPECT_EQ(Client::Messenger::DrawTheLetterPortrait(read, OldWindowDrawView{}).HasValue(), true);
}

// With the game, both windows' vtable + 0x54 passes their portrait (+0xb0 the read one, +0xb4 the write one) to the
// vtable + 0x14 (0x497c10) at the end of the frame, after the buttons, and its error is the draw's.
TEST_F(LetterWindowsDrawTest, WithTheGameBothLetterWindowsDrawTheirPortraitLast)
{
    OldWindow& read = OpenALetter("hola");
    std::vector<OldPortraitPart*> drawnParts;
    std::size_t commandsBefore = 0;
    m_portrait = [&](OldPortraitPart& part) -> Client::Core::Expected<void, std::string>
    {
        drawnParts.push_back(&part);
        commandsBefore = m_sprites.Commands().size();
        return {};
    };
    ASSERT_TRUE(Draw().HasValue());
    ASSERT_EQ(drawnParts.size(), 1u);
    EXPECT_EQ(drawnParts[0], &read.letter.portrait);
    EXPECT_EQ(commandsBefore, m_sprites.Commands().size());

    OldWindow& write = OpenAWrite();
    drawnParts.clear();
    m_portrait = [&](OldPortraitPart& part) -> Client::Core::Expected<void, std::string>
    {
        drawnParts.push_back(&part);
        return Client::Core::Unexpected(std::string("el modelo"));
    };
    const auto drawn = Draw();
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_EQ(drawn.Error(), "el modelo");
    ASSERT_EQ(drawnParts.size(), 1u);
    EXPECT_TRUE(drawnParts[0] == &read.letter.portrait || drawnParts[0] == &write.letter.portrait);
}

// Dragged and resizing, 0x499d60 places the list, the buttons and the portrait again (the 8) and measures the list and
// the portrait again (the 9).
TEST_F(LetterWindowsDrawTest, TheDraggedReadWindowPlacesItsChildren)
{
    OldWindow& read = OpenALetter("hola");
    read.base.x += 10;
    read.base.height += 13;
    read.base.state = Client::Ui::ResizedWindow;

    ASSERT_TRUE(Client::Messenger::DrawTheReadWindow(m_manager, read, Context(), OldWindowDrawView{.texts = &m_texts})
                    .HasValue());

    const Client::Ui::OldLetterPart& letter = read.letter;
    EXPECT_EQ(letter.lines.base.x, 115);
    EXPECT_EQ(letter.lines.base.y, 250);
    EXPECT_EQ(letter.lines.base.height, 133);
    EXPECT_EQ(letter.buttons[4].base.x, 260);
    EXPECT_EQ(letter.buttons[4].base.y, 254);
    // The portrait: 0x77 from the right edge, 0xf down, and 0xf shorter than the inner area.
    EXPECT_EQ(letter.portrait.base.x, 115 + 360 - 0x77);
    EXPECT_EQ(letter.portrait.base.height, 169 - 0xf);
}

// The write window with the portrait (+0xb0 at 1): with the normal blend, above the black of 29 with the lines at
// y + 0xe and y + 0x1d, below the black of 19 and its line; the 1000 and the 1005 right-aligned in the 1000's width, in
// 0xffc8dce6 with the background there was; the three EDITs (the body's with its bar, which ends without blend) and
// the four buttons. At the end of the frame, the vtable + 0x54 (0x498800) stops in the portrait's draw without the
// game.
TEST_F(LetterWindowsDrawTest, TheWriteWindowDrawsItsBarsItsLabelsItsEditsAndItsButtonsUntilThePortrait)
{
    OldWindow& write = OpenAWrite();
    const Client::Ui::OldLetterPart& letter = write.letter;
    ASSERT_EQ(letter.withPortrait, 1);
    ASSERT_EQ(write.base.width, 370);
    ASSERT_EQ(write.base.height, 216);
    ASSERT_EQ(letter.buttons.size(), 4u);
    m_colors = TextColors{.text = 0xff123456u, .background = 0x00abcdefu};

    const auto drawn = Draw();
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x498800"), std::string::npos);
    EXPECT_NE(drawn.Error().find("0x497c10"), std::string::npos);
    EXPECT_NE(drawn.Error().find("sin el juego"), std::string::npos);

    std::vector<ExpectedCommand> expected{
        {std::nullopt, BlendMode::Alpha, 6},   // the shadow, the blacks and the lines
        {TextMarkerSlot, BlendMode::Alpha, 2}, // the 1000 and the 1005
        {std::nullopt, BlendMode::Alpha, 3},   // the three EDITs' backgrounds
        {ScrollbarSlot, BlendMode::Alpha, 2},  // the body's arrows
        {std::nullopt, BlendMode::Alpha, 2},   // its bar's sides
        {ScrollbarSlot, BlendMode::Opaque, 3}, // the bar, without blend
    };
    for (std::size_t button = 0; button < 4; ++button)
    {
        expected.push_back({ButtonSlot, BlendMode::Alpha, 1});
        expected.push_back({TextMarkerSlot, BlendMode::Alpha, 1});
    }
    expected.push_back({TextureSlot{Client::Ui::WindowTitleSlot}, BlendMode::Opaque, 1});
    ExpectCommands(expected);
    // The inner area: (105, 101), 360 × 190.
    ExpectBox(QuadOf(0, 1), 105.0f, 101.0f, 465.0f, 130.0f);
    EXPECT_EQ(VertexOf(0, 1).color, PaletteOf(7));
    ExpectBox(QuadOf(0, 2), 105.0f, 115.0f, 465.0f, 116.0f);
    EXPECT_EQ(VertexOf(0, 2).color, PaletteOf(2));
    ExpectBox(QuadOf(0, 3), 105.0f, 130.0f, 465.0f, 131.0f);
    EXPECT_EQ(VertexOf(0, 3).color, PaletteOf(2));
    ExpectBox(QuadOf(0, 4), 105.0f, 272.0f, 465.0f, 291.0f);
    EXPECT_EQ(VertexOf(0, 4).color, PaletteOf(7));
    ExpectBox(QuadOf(0, 5), 105.0f, 272.0f, 465.0f, 273.0f);
    EXPECT_EQ(VertexOf(0, 5).color, PaletteOf(2));
    // The buttons in their order: +0xa44, +0xacc, +0xb54 and +0xbdc, at x + 0xc, 0x3f, 0xfa and 0x12d of the inner area
    // and 0x10 above its bottom edge.
    ExpectBox(QuadOf(6), 117.0f, 275.0f, 167.0f, 289.0f);
    ExpectBox(QuadOf(8), 168.0f, 275.0f, 218.0f, 289.0f);
    ExpectBox(QuadOf(10), 355.0f, 275.0f, 405.0f, 289.0f);
    ExpectBox(QuadOf(12), 406.0f, 275.0f, 456.0f, 289.0f);

    // 0x467060 with mode 3: the box is the 1000's (12 letters), and the 1005 starts 8 letters before its column 0.
    const TextColors labels{.text = Dim, .background = 0x00abcdefu};
    ExpectTexts(0, {
                       {"Destinatario", 108, 104, labels},
                       {"Tema", 108, 119, labels},
                   });
    EXPECT_TRUE(m_text.texts[0].windowed);
    EXPECT_EQ(m_text.texts[0].firstColumn, 0);
    EXPECT_EQ(m_text.texts[0].columns, 72);
    EXPECT_TRUE(m_text.texts[1].windowed);
    EXPECT_EQ(m_text.texts[1].firstColumn, 24 - 72);
    EXPECT_EQ(m_text.texts[1].columns, 72);
}

// Without the portrait (+0xb0 at 0), the buttons +0xb54 and +0xbdc are not drawn and the vtable + 0x54 draws nothing.
TEST_F(LetterWindowsDrawTest, WithoutThePortraitTheWriteWindowDrawsTwoButtons)
{
    OldWindow& write = OpenAWrite();
    write.letter.withPortrait = 0;

    ASSERT_TRUE(Draw().HasValue());

    std::vector<ExpectedCommand> expected{
        {std::nullopt, BlendMode::Alpha, 6},
        {TextMarkerSlot, BlendMode::Alpha, 2},
        {std::nullopt, BlendMode::Alpha, 3},
        {ScrollbarSlot, BlendMode::Alpha, 2},
        {std::nullopt, BlendMode::Alpha, 2},
        {ScrollbarSlot, BlendMode::Opaque, 3},
        {ButtonSlot, BlendMode::Alpha, 1},
        {TextMarkerSlot, BlendMode::Alpha, 1},
        {ButtonSlot, BlendMode::Alpha, 1},
        {TextMarkerSlot, BlendMode::Alpha, 1},
        {TextureSlot{Client::Ui::WindowTitleSlot}, BlendMode::Opaque, 1},
    };
    ExpectCommands(expected);
}

TEST_F(LetterWindowsDrawTest, TheWriteWindowEditsShowTheirTextAndTheFocusedCaret)
{
    OldWindow& write = OpenAWrite();
    Client::Ui::OldLetterPart& letter = write.letter;
    letter.to.text.SetText("Tordo");
    letter.subject.text.SetText("Asado");
    letter.text.text.SetText("Hay asado");
    m_focusedEdit = letter.subject.base.id;

    static_cast<void>(Draw());

    // Each EDIT's text, 3 from the edge (the XP EDIT's margin, 06 § El EDIT).
    ASSERT_GE(m_text.texts.size(), 5u);
    EXPECT_EQ(m_text.texts[2].text, "Tordo");
    EXPECT_EQ(m_text.texts[2].x, letter.to.base.x + 3);
    EXPECT_EQ(m_text.texts[2].y, letter.to.base.y);
    EXPECT_EQ(m_text.texts[3].text, "Asado");
    EXPECT_EQ(m_text.texts[3].x, letter.subject.base.x + 3);
    EXPECT_EQ(m_text.texts[3].y, letter.subject.base.y);
    EXPECT_EQ(m_text.texts[4].text, "Hay asado");
    EXPECT_EQ(m_text.texts[4].y, letter.text.base.y);
    // The bitmap of each and its text. With the caret at the end of the subject, the bitmap's clip leaves it out.
    ExpectCommands({
        {std::nullopt, BlendMode::Alpha, 6},
        {TextMarkerSlot, BlendMode::Alpha, 2},
        {std::nullopt, BlendMode::Alpha, 1},
        {TextMarkerSlot, BlendMode::Alpha, 1},
        {std::nullopt, BlendMode::Alpha, 1},
        {TextMarkerSlot, BlendMode::Alpha, 1},
        {std::nullopt, BlendMode::Alpha, 1},
        {TextMarkerSlot, BlendMode::Alpha, 1},
    });
    ExpectBox(QuadOf(4), static_cast<float>(letter.subject.base.x), static_cast<float>(letter.subject.base.y),
              static_cast<float>(letter.subject.base.x + 3 + 30), static_cast<float>(letter.subject.base.y + 14));
    EXPECT_EQ(std::ranges::count_if(m_text.texts, [](const auto& text) { return text.columns == 1; }), 0);
    EXPECT_EQ(letter.subject.draws, 1);
    EXPECT_EQ(letter.to.draws, 1);

    // With the caret in the middle, the column after the width of "As" inverts the text (the EDIT's background over
    // 0xffc8c8c8), as in the add-friend box.
    letter.subject.text.SetCursor(2);
    letter.subject.draws = 0;
    m_text.texts.clear();
    static_cast<void>(Draw());

    const auto caret =
        std::ranges::find_if(m_text.texts, [](const auto& text) { return text.text == "Asado" && text.columns == 1; });
    ASSERT_NE(caret, m_text.texts.end());
    EXPECT_EQ(caret->x, letter.subject.base.x + 3 + 13);
    EXPECT_EQ(caret->firstColumn, 13);
    EXPECT_EQ(caret->style.colors, (TextColors{.text = letter.subject.backgroundColor, .background = 0xffc8c8c8u}));
}

// Dragged and resizing, 0x498480 places the buttons, the EDITs and the portrait again (the 8), but does not measure
// them (does not send the 9): the body EDIT keeps its height.
TEST_F(LetterWindowsDrawTest, TheDraggedWriteWindowPlacesItsChildrenWithoutSizingThem)
{
    OldWindow& write = OpenAWrite();
    const Client::Ui::OldLetterPart& letter = write.letter;
    const std::int32_t toX = letter.to.base.x;
    const std::int32_t bodyHeight = letter.text.base.height;
    const std::int32_t portraitHeight = letter.portrait.base.height;
    write.base.x += 10;
    write.base.y += 20;
    write.base.height += 13;
    write.base.state = Client::Ui::ResizedWindow;

    ASSERT_TRUE(Client::Messenger::DrawTheWriteWindow(m_manager, write, Context(), OldWindowDrawView{.texts = &m_texts})
                    .HasValue());

    EXPECT_EQ(letter.to.base.x, toX + 10);
    EXPECT_EQ(letter.subject.base.y, 80 + 20 + 0x15 + 0x12);
    EXPECT_EQ(letter.text.base.y, 80 + 20 + 0x15 + 0x21);
    EXPECT_EQ(letter.text.base.height, bodyHeight);
    // The buttons, at 0x10 from the inner area's bottom edge, which went 13 lower.
    EXPECT_EQ(letter.buttons[0].base.x, 127);
    EXPECT_EQ(letter.buttons[0].base.y, 275 + 20 + 13);
    EXPECT_EQ(letter.buttons[3].base.x, 416);
    EXPECT_EQ(letter.portrait.base.x, 115 + 360 - 0x77);
    EXPECT_EQ(letter.portrait.base.height, portraitHeight);
}

// F5 with three chats: the first waits for a message, the second (with three members) is the chosen one and the third
// is minimized. 0x4a5490: the background of the 3 and the border of the 15 with the blink, 17 · 2 above the bottom
// edge (0x1b3); 0x4a5750, from bottom to top: the stripe of the 0x11 or the 0x10, the title's friend in black with the
// blink's alpha and, with more than two members, the win_mark.tga mark in red.
TEST_F(LetterWindowsDrawTest, TheWindowListDrawsTheChatsWithTheirBandsAndTheirMarks)
{
    const std::int32_t first = OpenAChat({{"Tordo", 1}}, {}).base.id;
    const std::int32_t second = OpenAChat({{"Zorzal", 1}, {"Mirlo", 2}}, {}).base.id;
    OldWindow& third = OpenAChat({{"Hornero", 1}}, {});
    third.base.state = Client::Ui::MinimizedWindow;
    OldWindow& list = m_manager.windowList;
    ASSERT_EQ(list.list.chats.size(), 3u);
    ASSERT_EQ(list.list.chats[0], first);
    ASSERT_EQ(list.list.chats[1], second);
    list.base.state = Client::Ui::ShownWindow;
    list.list.blink = 1.0f;
    list.list.cursor = 1;
    list.list.waiting.push_back(first);

    ASSERT_TRUE(Client::Ui::DrawTheWindowList(m_manager, Context(), OldWindowDrawView{.texts = &m_texts}).HasValue());

    const TextureSlot mark{0x64c};
    ExpectCommands({
        {std::nullopt, BlendMode::Alpha, 6},   // the background, the four borders and the first one's stripe
        {TextMarkerSlot, BlendMode::Alpha, 1}, // "Tordo"
        {std::nullopt, BlendMode::Alpha, 1},   // the chosen one's stripe
        {TextMarkerSlot, BlendMode::Alpha, 1}, // "Zorzal"
        {mark, BlendMode::Alpha, 1},
    });
    // The row measures 13 ("0"), and with the margin 17: 34 high from 0x1b3 − 34, at x = 0x246, 0x34 wide.
    ExpectBox(QuadOf(0, 0), 582.0f, 401.0f, 634.0f, 435.0f);
    // The 3 is the shadow's: its alpha is the blink · 179.
    EXPECT_EQ(VertexOf(0, 0).color, PaletteOf(3));
    EXPECT_LT(VertexOf(0, 0).color.alpha, 1.0f);
    ExpectBox(QuadOf(0, 1), 582.0f, 401.0f, 634.0f, 402.0f);
    EXPECT_EQ(VertexOf(0, 1).color, PaletteOf(15));
    ExpectBox(QuadOf(0, 2), 582.0f, 401.0f, 583.0f, 435.0f);
    ExpectBox(QuadOf(0, 3), 633.0f, 401.0f, 634.0f, 435.0f);
    ExpectBox(QuadOf(0, 4), 582.0f, 435.0f, 634.0f, 436.0f);
    // The stripe: x + 1, 1 lower than the row, of w − 2 × (13 + 3).
    ExpectBox(QuadOf(0, 5), 583.0f, 419.0f, 633.0f, 435.0f);
    EXPECT_EQ(VertexOf(0, 5).color, PaletteOf(0x11));
    ExpectBox(QuadOf(2), 583.0f, 402.0f, 633.0f, 418.0f);
    EXPECT_EQ(VertexOf(2).color, PaletteOf(0x10));
    // The mark, 4 × 6 at x + w − 7 and 5 lower than the row, in red.
    ExpectBox(QuadOf(4), 627.0f, 406.0f, 631.0f, 412.0f);
    EXPECT_EQ(VertexOf(4).color, (Color{.red = 1.0f, .green = 0.0f, .blue = 0.0f, .alpha = 1.0f}));
    EXPECT_FLOAT_EQ(VertexOf(4, 0, 2).u, 0.5f);
    EXPECT_FLOAT_EQ(VertexOf(4, 0, 2).v, 0.75f);

    // strtok(",") of the title: "t994" and the friends from back to front (0x460e70 adds them at the front: the first
    // one in, Zorzal); what follows the 994, at x + 2 and 3 lower.
    const TextColors black{.text = 0xff000000u, .background = 0};
    ExpectTexts(0, {{"Tordo", 584, 421, black}, {"Zorzal", 584, 404, black}});
    EXPECT_EQ(m_text.texts.size(), 2u);
}

// Without a chosen or waiting one, the text in 0xc8dce6 with the blink's alpha (ftol(0.5 · 255)); a closed chat (the
// 995 in front) shows the friend the same, and one without friends in the title, the 995.
TEST_F(LetterWindowsDrawTest, TheWindowListShowsTheClosedChatsAndTheEmptyOnes)
{
    OldWindow& closed = OpenAChat({{"Tordo", 1}}, {});
    Client::Messenger::CloseTheChat(closed, 1, m_texts);
    ASSERT_EQ(closed.frame.title, std::optional<std::string>("t995t994Tordo"));
    OpenAChat({}, {});
    OldWindow& list = m_manager.windowList;
    list.base.state = Client::Ui::ShownWindow;
    list.list.blink = 0.5f;
    list.list.cursor = list.list.chats.size();

    ASSERT_TRUE(Client::Ui::DrawTheWindowList(m_manager, Context(), OldWindowDrawView{.texts = &m_texts}).HasValue());

    const TextColors faded{.text = 0x7fc8dce6u, .background = 0};
    ExpectTexts(0, {{"Tordo", 584, 421, faded}, {"t995", 584, 404, faded}});
    // The background and the border with the blink's alpha.
    EXPECT_EQ(VertexOf(0, 1).color, *Client::Ui::OldWindowPalette(15, 0.5f));
}

// Hidden, or with every chat minimized, the list draws nothing beyond the frame's.
TEST_F(LetterWindowsDrawTest, TheWindowListWithoutChatsToShowDrawsNothing)
{
    OldWindow& chat = OpenAChat({{"Tordo", 1}}, {});
    OldWindow& list = m_manager.windowList;
    ASSERT_TRUE(Client::Ui::DrawTheWindowList(m_manager, Context(), OldWindowDrawView{.texts = &m_texts}).HasValue());
    EXPECT_TRUE(m_sprites.Commands().empty());

    list.base.state = Client::Ui::ShownWindow;
    chat.base.state = Client::Ui::MinimizedWindow;
    ASSERT_TRUE(Client::Ui::DrawTheWindowList(m_manager, Context(), OldWindowDrawView{.texts = &m_texts}).HasValue());
    EXPECT_TRUE(m_sprites.Commands().empty());
    EXPECT_TRUE(m_text.texts.empty());
}
// 0x4a5120 without the friends window: with a waiting chat, a new letter and an unread one, the man at (0x246 + 4,
// 0x1b3 + 2) of 25 × 14 and the letter at (0x246 + 0x21, 0x1b3 + 5) of 15 × 9, both in the first 12 draws of every 24.
// The new letter blinks at the 11 of every 24: the sixth blink (draw 131) turns it off; then the letter stays steady
// while there is an unread one. No rand().
TEST_F(LetterWindowsDrawTest, TheBarIconsBlinkTheWaitingChatAndTheNewLetterUntilTheSixthBlink)
{
    Client::Ui::OldWindowListPart& list = m_manager.windowList.list;
    list.waiting.push_back(7);
    list.newLetter = 1;
    Client::Messenger::AddALetter(m_manager.letterModel, 3, "Tordo", "Asado", "2026/11/13", "21:30", 0);

    Client::Messenger::DrawTheFriendBarIcons(m_manager, m_sprites);

    ExpectCommands({{TextureSlot{0x64e}, BlendMode::Alpha, 1}, {TextureSlot{0x64d}, BlendMode::Alpha, 1}});
    EXPECT_EQ(m_sprites.Commands().size(), 2u);
    ExpectBox(QuadOf(0), 586.0f, 437.0f, 611.0f, 451.0f);
    EXPECT_FLOAT_EQ(VertexOf(0, 0, 0).v, 0.0f);
    EXPECT_FLOAT_EQ(VertexOf(0, 0, 2).u, 0.78125f);
    EXPECT_FLOAT_EQ(VertexOf(0, 0, 2).v, 0.4375f);
    ExpectBox(QuadOf(1), 615.0f, 440.0f, 630.0f, 449.0f);
    EXPECT_FLOAT_EQ(VertexOf(1, 0, 2).u, 0.9375f);
    EXPECT_FLOAT_EQ(VertexOf(1, 0, 2).v, 0.5625f);
    EXPECT_EQ(list.barBlink, 1);
    EXPECT_EQ(list.letterBlinks, 0);

    // Draws 1..130: five blinks (at 11, 35, 59, 83 and 107), and the new letter stays.
    for (int draw = 1; draw < 131; ++draw)
        Client::Messenger::DrawTheFriendBarIcons(m_manager, m_sprites);
    EXPECT_EQ(list.barBlink, 131);
    EXPECT_EQ(list.letterBlinks, 5);
    EXPECT_EQ(list.newLetter, 1);
    // The 131 (131 % 24 = 11): the sixth turns it off and goes back to 0.
    Client::Messenger::DrawTheFriendBarIcons(m_manager, m_sprites);
    EXPECT_EQ(list.barBlink, 132);
    EXPECT_EQ(list.letterBlinks, 0);
    EXPECT_EQ(list.newLetter, 0);

    // Off (the 12 of every 24): without the man, and the letter steady for the unread one.
    list.barBlink = 12;
    m_sprites.Begin(Screen640);
    Client::Messenger::DrawTheFriendBarIcons(m_manager, m_sprites);
    ExpectCommands({{TextureSlot{0x64d}, BlendMode::Alpha, 1}});
    EXPECT_EQ(m_sprites.Commands().size(), 1u);
    ExpectBox(QuadOf(0), 615.0f, 440.0f, 630.0f, 449.0f);
    EXPECT_EQ(list.barBlink, 13);

    // Read, nothing; with the new letter and off, neither.
    m_manager.letterModel.letters.front().type = 1;
    m_sprites.Begin(Screen640);
    Client::Messenger::DrawTheFriendBarIcons(m_manager, m_sprites);
    list.newLetter = 1;
    Client::Messenger::DrawTheFriendBarIcons(m_manager, m_sprites);
    EXPECT_TRUE(m_sprites.Commands().empty());
    EXPECT_EQ(list.barBlink, 15);
    EXPECT_EQ(list.newLetter, 1);
    EXPECT_EQ(list.letterBlinks, 0);
}

// 0x4a5120 with the friends window open: the pressed button (win_push.jpg) at (0x246, 0x1b3) of 0x34 × 0x12 with
// w / 64 and h / 32, the man at (x + 7, y + 3) from v = 0.4375 and the letter at (x + 0x24, y + 7). With none unread,
// the new letter turns off (0x4a6420(0)) and the letter is drawn anyway in that draw; in the next, only with an unread
// one.
TEST_F(LetterWindowsDrawTest, WithTheFriendsWindowTheBarIconsGoOverThePushedButton)
{
    const auto opened = Client::Messenger::OpenTheFriendsWindow(
        m_manager, Client::Messenger::FriendsWindowX, Client::Messenger::FriendsWindowY, Globals(), StepView());
    ASSERT_TRUE(opened.HasValue()) << opened.Error();
    Client::Ui::OldWindowListPart& list = m_manager.windowList.list;
    list.waiting.push_back(7);
    list.newLetter = 1;
    m_sprites.Begin(Screen640);

    Client::Messenger::DrawTheFriendBarIcons(m_manager, m_sprites);

    ExpectCommands({{TextureSlot{0x64f}, BlendMode::Alpha, 1},
                    {TextureSlot{0x64e}, BlendMode::Alpha, 1},
                    {TextureSlot{0x64d}, BlendMode::Alpha, 1}});
    EXPECT_EQ(m_sprites.Commands().size(), 3u);
    ExpectBox(QuadOf(0), 582.0f, 435.0f, 634.0f, 453.0f);
    EXPECT_FLOAT_EQ(VertexOf(0, 0, 2).u, 0.8125f);
    EXPECT_FLOAT_EQ(VertexOf(0, 0, 2).v, 0.5625f);
    ExpectBox(QuadOf(1), 589.0f, 438.0f, 614.0f, 452.0f);
    EXPECT_FLOAT_EQ(VertexOf(1, 0, 0).v, 0.4375f);
    EXPECT_FLOAT_EQ(VertexOf(1, 0, 2).v, 0.875f);
    ExpectBox(QuadOf(2), 618.0f, 442.0f, 633.0f, 451.0f);
    EXPECT_EQ(list.newLetter, 0);
    EXPECT_EQ(list.barBlink, 1);

    // Without a new letter: with none unread, only the button and the man; with one, also the letter.
    m_sprites.Begin(Screen640);
    Client::Messenger::DrawTheFriendBarIcons(m_manager, m_sprites);
    ExpectCommands({{TextureSlot{0x64f}, BlendMode::Alpha, 1}, {TextureSlot{0x64e}, BlendMode::Alpha, 1}});
    EXPECT_EQ(m_sprites.Commands().size(), 2u);
    Client::Messenger::AddALetter(m_manager.letterModel, 4, "Zorzal", "Mate", "2026/11/13", "22:00", 0);
    list.barBlink = 23;
    m_sprites.Begin(Screen640);
    Client::Messenger::DrawTheFriendBarIcons(m_manager, m_sprites);
    ExpectCommands({{TextureSlot{0x64f}, BlendMode::Alpha, 1}, {TextureSlot{0x64d}, BlendMode::Alpha, 1}});
    EXPECT_EQ(m_sprites.Commands().size(), 2u);
    ExpectBox(QuadOf(1), 618.0f, 442.0f, 633.0f, 451.0f);
    EXPECT_EQ(list.barBlink, 24);
}
} // namespace
