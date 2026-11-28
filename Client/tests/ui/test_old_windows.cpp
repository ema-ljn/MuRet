#include "Ui/OldWindows.h"

#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Chat/ChatInput.h"
#include "Chat/ChatLog.h"
#include "Ui/OldWindowBase.h"
#include "Ui/OldWindowDraw.h"
#include "Audio/SoundId.h"
#include "support/RecordingTextDrawer.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <list>
#include <limits>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The old windows (CL8 milestone 7; 06 § HUD y ventanas, "Las ventanas viejas"): the factory 0x48ee60, the step
// 0x490490 with the buttons (0x45b200) and the frame (0x492ac0), the messages of 0x490b70 and the drawing of 0x492500.
// Nothing in CL8 opens one: the tests open the message box by hand, with invented texts.
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
using Client::Render::Text::TextColors;
using Client::Tests::RecordingTextDrawer;
using Client::Tests::TextMarkerSlot;
using Client::Ui::OldMessage;
using Client::Ui::OldWindow;
using Client::Ui::OldWindowDrawContext;
using Client::Ui::OldWindowDrawView;
using Client::Ui::OldWindowGlobals;
using Client::Ui::OldWindowManager;
using Client::Ui::OldWindowMeasure;
using Client::Ui::OldWindowRequest;
using Client::Ui::OldWindowStepView;
using Client::Ui::OldWindowTexts;

constexpr PixelSize Screen640{640, 480};
constexpr std::uint8_t EscapeKey = 0x1b;
constexpr std::uint8_t F6Key = 0x75;
constexpr std::uint8_t EnterKey = 0x0d;
// The box with its defaults (0x48ee60 with −1 and −1): at (0, 0x14c), 0x96 × 100.
constexpr std::int32_t BoxY = 0x14c;
// 0x41ed10(0x19)
constexpr std::int32_t ButtonSound = 0x19;

const OldWindowTexts Texts{.notice = "Aviso", .question = "Pregunta", .yes = "Si", .no = "No"};

class OldWindowsTest : public ::testing::Test
{
protected:
    OldWindowsTest() : m_text(m_sprites)
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

    std::int32_t Open(std::int32_t kind, std::string_view text, std::int32_t x = -1, std::int32_t y = -1,
                      std::int32_t aux = 0, std::int32_t flags = 0)
    {
        const auto opened = Client::Ui::CreateTheOldWindow(
            m_manager, OldWindowRequest{.kind = kind, .x = x, .y = y, .data = text, .aux = aux, .flags = flags}, Texts,
            OldWindowMeasure{.measure = m_text, .dcFont = FontKind::Normal, .screen = Screen640});
        EXPECT_TRUE(opened.HasValue()) << opened.Error();
        return opened.HasValue() ? opened.Value() : -1;
    }

    // A step like the game's: 0x636e2f (0x5762eb8 at 0), the window list (0x636e3c) and 0x490490.
    Client::Core::Expected<void, std::string> Step(std::int32_t x, std::int32_t y)
    {
        m_hovered = 0;
        m_mouseOnUi = 0;
        const OldWindowStepView view{
            .mouseX = x, .mouseY = y, .keyEdge = [this](std::uint8_t key) { return m_keys.erase(key) > 0; }};
        if (auto list = Client::Ui::StepTheWindowList(m_manager, Globals(), view); !list.HasValue())
            return list;
        return Client::Ui::StepTheOldWindows(m_manager, Globals(), view);
    }

    // The WndProc's WM_LBUTTONDOWN and WM_LBUTTONUP.
    void Press()
    {
        m_leftHeld = true;
        m_leftClick = true;
        m_released = false;
    }
    void Release()
    {
        m_leftHeld = false;
        m_leftClick = false;
        m_released = true;
    }

    OldWindow& Window(std::int32_t id)
    {
        return m_manager.windows.at(id);
    }

    void Draw(bool friendsOpen = false, std::int32_t x = 639, std::int32_t y = 0)
    {
        const OldWindowDrawContext context{
            .sprites = m_sprites, .text = m_text, .dcFont = m_dcFont, .textColors = m_colors};
        const OldWindowDrawView view{.mouseX = x, .mouseY = y, .leftClick = m_leftClick, .friendsOpen = friendsOpen};
        const auto windows = Client::Ui::DrawTheOldWindows(m_manager, context, view);
        ASSERT_TRUE(windows.HasValue()) << windows.Error();
        const auto list = Client::Ui::DrawTheWindowList(m_manager, context, view);
        ASSERT_TRUE(list.HasValue()) << list.Error();
    }

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
    std::set<std::uint8_t> m_keys;
    FontKind m_dcFont = FontKind::Bold;
    TextColors m_colors{};
};

// 0x48f78f..0x48f7c6: the window registry is a unique insert, even if the counter comes back to a live ID.
TEST_F(OldWindowsTest, ARepeatedIdKeepsTheFirstRegisteredWindow)
{
    const std::int32_t original = Open(Client::Ui::FrameCase, "primera", 20, 30);
    ASSERT_NE(original, 0);
    m_manager.lastId = original - 1;

    EXPECT_EQ(Open(Client::Ui::FrameCase, "segunda", 100, 120), original);

    ASSERT_EQ(m_manager.windows.size(), 1u);
    EXPECT_EQ(Window(original).frame.title, std::optional<std::string>("primera"));
    EXPECT_EQ(Window(original).base.x, 20);
    EXPECT_EQ(Window(original).base.y, 30);
    EXPECT_EQ(m_manager.order, (std::list<std::int32_t>{original, original}));
}

// The failed insert does not reseat the previous object: 0x48f82d uses the new object left unregistered.
TEST_F(OldWindowsTest, ARepeatedIdDoesNotRepositionTheFirstWindowsButtons)
{
    const std::int32_t original = Open(Client::Ui::QuestionCase, "primera", 20, 30);
    const std::int32_t buttonId = Window(original).box.buttons.front().base.id;
    const std::int32_t buttonX = Window(original).box.buttons.front().base.x;
    Window(original).base.x = 200;
    m_manager.lastId = original - 1;

    EXPECT_EQ(Open(Client::Ui::NoticeCase, "segunda", 300, 120), original);

    EXPECT_EQ(Window(original).box.buttons.front().base.id, buttonId);
    EXPECT_EQ(Window(original).box.buttons.front().base.x, buttonX);
    EXPECT_EQ(Window(original).box.oneButton, 0);
    EXPECT_EQ(m_manager.order, (std::list<std::int32_t>{original, original}));
}

TEST_F(OldWindowsTest, DestroyingAQuestionReleasesItsSecondButton)
{
    const std::int32_t box = Open(Client::Ui::QuestionCase, "pregunta");
    m_pressedButton = Window(box).box.buttons[1].base.id;

    ASSERT_TRUE(Client::Ui::DestroyTheOldWindow(m_manager, box, m_pressedButton).HasValue());

    EXPECT_EQ(m_pressedButton, 0);
}

// 0x459ef0 increments a DWORD; 0x48f6f2 uses SETC to order its keys as uint32, also past the sign bit.
TEST_F(OldWindowsTest, WindowIdsKeepTheirUnsignedOrderAcrossTheSignBit)
{
    const std::int32_t first = Open(Client::Ui::FrameCase, "primera");
    m_manager.lastId = std::numeric_limits<std::int32_t>::max();
    const std::int32_t wrapped = Open(Client::Ui::FrameCase, "segunda", 20, 30);

    EXPECT_EQ(wrapped, std::numeric_limits<std::int32_t>::min());
    ASSERT_EQ(m_manager.windows.size(), 2u);
    EXPECT_EQ(m_manager.windows.begin()->first, first);
    EXPECT_EQ(m_manager.windows.rbegin()->first, wrapped);
}

// 0x459ef0: the counter keeps the 32-bit wrap; 0 stays the sentinel value for the consumers.
TEST_F(OldWindowsTest, WindowIdsWrapFromAllBitsSetToZero)
{
    m_manager.lastId = -1;

    EXPECT_EQ(Open(Client::Ui::FrameCase, "cero"), 0);

    EXPECT_EQ(m_manager.lastId, 0);
    ASSERT_EQ(m_manager.windows.size(), 1u);
    EXPECT_TRUE(m_manager.windows.contains(0));
}

// 0x48ee60 case 9: the frame (0x491ca0) and the two buttons (0x45ad80) take the ids after the window list's;
// 0x4a38f0 gives it title 991, the text wrapped in 2 lines of 0x7d and the buttons at (0x12, 0x28) and (0x49, 0x28)
// of its inner area (x + 5, y + 0x15), which 0x4a3a50 places at the end.
TEST_F(OldWindowsTest, AQuestionBoxTakesTheNextIdsAndPlacesItsButtonsInsideIt)
{
    const std::int32_t id = Open(Client::Ui::QuestionCase, "Hola");

    EXPECT_EQ(id, Client::Ui::WindowListId + 1);
    EXPECT_EQ(m_manager.lastId, Client::Ui::WindowListId + 3);
    const OldWindow& box = Window(id);
    EXPECT_EQ(box.kind, Client::Ui::OldWindowKind::MessageBox);
    EXPECT_EQ(box.base.x, 0);
    EXPECT_EQ(box.base.y, BoxY);
    EXPECT_EQ(box.base.width, 0x96);
    EXPECT_EQ(box.base.height, 100);
    EXPECT_EQ(box.base.flags, 0xb);
    EXPECT_EQ(box.base.state, Client::Ui::ShownWindow);
    EXPECT_EQ(box.frame.title, "Pregunta");
    EXPECT_EQ(box.box.lines[0], "Hola");
    EXPECT_EQ(box.box.lines[1], "");
    const auto& yes = box.box.buttons[0];
    EXPECT_EQ(yes.base.id, id + 1);
    EXPECT_EQ(yes.base.parent, id);
    EXPECT_EQ(yes.base.x, 5 + 0x12);
    EXPECT_EQ(yes.base.y, BoxY + 0x15 + 0x28);
    EXPECT_EQ(yes.base.width, 0x32);
    EXPECT_EQ(yes.base.height, 0x14);
    EXPECT_EQ(yes.command, 1);
    EXPECT_EQ(yes.label, "Si");
    const auto& no = box.box.buttons[1];
    EXPECT_EQ(no.base.id, id + 2);
    EXPECT_EQ(no.base.x, 5 + 0x49);
    EXPECT_EQ(no.base.y, BoxY + 0x15 + 0x28);
    EXPECT_EQ(no.command, 2);
    EXPECT_EQ(no.label, "No");
    EXPECT_EQ(m_manager.order, (std::list<std::int32_t>{id}));
    EXPECT_EQ(m_manager.boxes, (std::list<std::int32_t>{id}));
    EXPECT_EQ(m_manager.modal, 0);
}

// 0x459c70 wraps the text at 0x7d in the DC's font, like the chat log.
TEST_F(OldWindowsTest, TheBoxTextIsWrappedInTwoLines)
{
    constexpr std::string_view Text = "una pregunta inventada que no entra en un solo renglon de la caja";
    const auto expected = Client::Chat::WrapByWidth(Text, m_text, FontKind::Normal, Screen640, 0x7d, 2, 0);
    ASSERT_TRUE(expected.HasValue()) << expected.Error();
    ASSERT_EQ(expected.Value().lines.size(), 2u);

    const std::int32_t id = Open(Client::Ui::NoticeCase, Text);

    EXPECT_EQ(Window(id).box.lines[0], expected.Value().lines[0]);
    EXPECT_EQ(Window(id).box.lines[1], expected.Value().lines[1]);
}

// With one button (0x48ee60 case 10), the first has title 228 at (0x2d, 0x28); 0x4a38f0 does not set up the second,
// which stays as 0x45ad80 left it: no parent, at (0, 0), 0x46 × 0x14 (Q-C570).
TEST_F(OldWindowsTest, ANoticeBoxHasOneButtonAndAGhostAtTheOrigin)
{
    const std::int32_t id = Open(Client::Ui::NoticeCase, "Listo", 100, 50);

    const OldWindow& box = Window(id);
    EXPECT_EQ(box.base.x, 100);
    EXPECT_EQ(box.base.y, 50);
    EXPECT_EQ(box.frame.title, "Aviso");
    EXPECT_EQ(box.box.oneButton, 1);
    EXPECT_EQ(box.box.buttons[0].base.x, 100 + 5 + 0x2d);
    EXPECT_EQ(box.box.buttons[0].base.y, 50 + 0x15 + 0x28);
    EXPECT_EQ(box.box.buttons[0].label, "Aviso");
    const auto& ghost = box.box.buttons[1];
    EXPECT_EQ(ghost.base.parent, 0);
    EXPECT_EQ(ghost.base.x, 0);
    EXPECT_EQ(ghost.base.y, 0);
    EXPECT_EQ(ghost.base.width, 0x46);
    EXPECT_EQ(ghost.base.height, 0x14);
    EXPECT_FALSE(ghost.label.has_value());
}

// 0x4a43c0 runs the step of both buttons: the unused one takes the mouse at the top left, is pressed and sounds on
// release, with its command to nobody (Q-C570).
TEST_F(OldWindowsTest, TheGhostButtonTakesTheMouseAtTheTopLeft)
{
    const std::int32_t id = Open(Client::Ui::NoticeCase, "Listo", 100, 50);
    const std::int32_t ghost = id + 2;

    ASSERT_TRUE(Step(10, 10).HasValue());
    EXPECT_EQ(m_hovered, ghost);
    EXPECT_EQ(m_mouseOnUi, 1);

    Press();
    ASSERT_TRUE(Step(10, 10).HasValue());
    EXPECT_EQ(m_pressedButton, ghost);
    EXPECT_EQ(Window(id).box.buttons[1].pressed, 1);

    Release();
    ASSERT_TRUE(Step(10, 10).HasValue());
    EXPECT_EQ(m_sound, Client::Audio::SoundId{ButtonSound});
    EXPECT_EQ(m_pressedButton, 0);
    EXPECT_FALSE(m_released);
    ASSERT_TRUE(Step(10, 10).HasValue());
    EXPECT_TRUE(m_manager.windows.contains(id));
    EXPECT_TRUE(Window(id).base.queue.empty());
}

// The click on "Si": the press marks it (0x5762ec4); the release sends 10 to the box and sounds; on the next step the
// box tells the owner (12 with its id and 1) and asks the manager to close it, which frees it at the end of the same
// step, sounds again and gives the focus to the one below.
TEST_F(OldWindowsTest, ClickingYesAnswersTheOwnerAndClosesTheBoxOneStepLater)
{
    const std::int32_t owner = Open(Client::Ui::FrameCase, "Dueño", 300, 20);
    const std::int32_t id = Open(Client::Ui::QuestionCase, "Seguro?", -1, -1, owner);
    const auto& yes = Window(id).box.buttons[0].base;
    const std::int32_t x = yes.x + 4;
    const std::int32_t y = yes.y + 4;

    Press();
    ASSERT_TRUE(Step(x, y).HasValue());
    EXPECT_EQ(m_hovered, id + 1);
    EXPECT_EQ(m_pressedButton, id + 1);
    // The press on the button goes up to its parent (0x45aa80 → 0x490b70 with 1).
    EXPECT_EQ(m_manager.focus, id);

    Release();
    ASSERT_TRUE(Step(x, y).HasValue());
    EXPECT_EQ(m_sound, Client::Audio::SoundId{ButtonSound});
    EXPECT_EQ(Window(id).base.queue.front(), (OldMessage{.type = 10, .a = 1, .b = 0, .text = {}}));

    m_sound.reset();
    ASSERT_TRUE(Step(x, y).HasValue());
    EXPECT_FALSE(m_manager.windows.contains(id));
    EXPECT_EQ(m_manager.order, (std::list<std::int32_t>{owner}));
    EXPECT_TRUE(m_manager.boxes.empty());
    EXPECT_EQ(m_sound, Client::Audio::SoundId{ButtonSound});
    // The owner, lower in the list, runs its step after the box: it handles the 12 in the same step.
    EXPECT_EQ(Window(owner).base.current, (OldMessage{.type = 12, .a = id, .b = 1, .text = {}}));
    // 0x490b70 with 4: the new last one of the list receives the 6.
    EXPECT_EQ(Window(owner).base.queue.front(), (OldMessage{.type = 6, .a = 0, .b = 0, .text = {}}));
}

TEST_F(OldWindowsTest, ClickingNoAnswersZero)
{
    const std::int32_t owner = Open(Client::Ui::FrameCase, "Dueño", 300, 20);
    const std::int32_t id = Open(Client::Ui::QuestionCase, "Seguro?", -1, -1, owner);
    const auto& no = Window(id).box.buttons[1].base;

    Press();
    ASSERT_TRUE(Step(no.x + 1, no.y + 1).HasValue());
    Release();
    ASSERT_TRUE(Step(no.x + 1, no.y + 1).HasValue());
    ASSERT_TRUE(Step(no.x + 1, no.y + 1).HasValue());

    EXPECT_FALSE(m_manager.windows.contains(id));
    EXPECT_EQ(Window(owner).base.current, (OldMessage{.type = 12, .a = id, .b = 0, .text = {}}));
}

// Released off the button: 0x45b200 drops everything, the left button and both edges.
TEST_F(OldWindowsTest, ReleasingAwayFromTheButtonDropsThePress)
{
    const std::int32_t id = Open(Client::Ui::QuestionCase, "Seguro?");
    const auto& yes = Window(id).box.buttons[0].base;

    Press();
    ASSERT_TRUE(Step(yes.x + 1, yes.y + 1).HasValue());
    ASSERT_EQ(m_pressedButton, id + 1);
    ASSERT_TRUE(Step(yes.x + 1 + yes.width, yes.y + 1).HasValue());

    EXPECT_EQ(m_pressedButton, 0);
    EXPECT_EQ(Window(id).box.buttons[0].pressed, 0);
    EXPECT_FALSE(m_leftHeld);
    EXPECT_FALSE(m_leftClick);
    EXPECT_TRUE(Window(id).base.queue.empty());
}

// Enter (0x553520(0xd)) is read only by the box at the end of the list: it sends 10 with 1 and closes on the next step.
TEST_F(OldWindowsTest, EnterAnswersOnlyTheBoxOnTop)
{
    const std::int32_t below = Open(Client::Ui::QuestionCase, "Abajo", 0, 0, 0, Client::Ui::KeepPlaceFlag);
    const std::int32_t top = Open(Client::Ui::QuestionCase, "Arriba", 300, 0, 0, Client::Ui::KeepPlaceFlag);
    m_keys = {EnterKey};

    ASSERT_TRUE(Step(639, 479).HasValue());
    EXPECT_EQ(Window(top).base.queue.front(), (OldMessage{.type = 10, .a = 1, .b = 0, .text = {}}));
    EXPECT_TRUE(Window(below).base.queue.empty());
    EXPECT_TRUE(m_keys.empty());

    ASSERT_TRUE(Step(639, 479).HasValue());
    EXPECT_FALSE(m_manager.windows.contains(top));
    EXPECT_TRUE(m_manager.windows.contains(below));
}

// 0x48f606..0x48f6b8: after moving, the loop goes on from the map's first, which the ++ skips; a new window can land
// right over the one with the smallest id (Q-C571).
TEST_F(OldWindowsTest, TheCascadeSkipsTheFirstWindowAfterMoving)
{
    const std::int32_t first = Open(Client::Ui::FrameCase, "Uno", 20, 20, 0, Client::Ui::KeepPlaceFlag);
    const std::int32_t second = Open(Client::Ui::FrameCase, "Dos", 0, 0, 0, Client::Ui::KeepPlaceFlag);

    const std::int32_t third = Open(Client::Ui::FrameCase, "Tres", 0, 0);

    EXPECT_EQ(Window(second).base.x, 0);
    EXPECT_EQ(Window(third).base.x, Window(first).base.x);
    EXPECT_EQ(Window(third).base.y, Window(first).base.y);
}

// Without the skip, the cascade moves 20 each time it finds one in its place.
TEST_F(OldWindowsTest, TheCascadeMovesTwentyPerWindowInItsPlace)
{
    static_cast<void>(Open(Client::Ui::FrameCase, "Uno", 0, 0, 0, Client::Ui::KeepPlaceFlag));
    static_cast<void>(Open(Client::Ui::FrameCase, "Dos", 20, 20, 0, Client::Ui::KeepPlaceFlag));

    const std::int32_t third = Open(Client::Ui::FrameCase, "Tres", 0, 0);

    EXPECT_EQ(Window(third).base.x, 40);
    EXPECT_EQ(Window(third).base.y, 40);
}

// 0x5762ebc: with a modal box, the factory builds no other (and spends no ids); it is freed with the box.
TEST_F(OldWindowsTest, AModalBoxBlocksAnotherModalUntilItGoes)
{
    const std::int32_t modal = Open(Client::Ui::ModalNoticeCase, "Espera");
    ASSERT_EQ(m_manager.modal, modal);
    EXPECT_TRUE(m_manager.boxes.empty());
    const std::int32_t lastId = m_manager.lastId;

    EXPECT_EQ(Open(Client::Ui::ModalQuestionCase, "Otra"), 0);
    EXPECT_EQ(m_manager.lastId, lastId);
    EXPECT_EQ(m_manager.order.size(), 1u);

    ASSERT_TRUE(Client::Ui::DestroyTheOldWindow(m_manager, modal, m_pressedButton).HasValue());
    EXPECT_EQ(m_manager.modal, 0);
    EXPECT_NE(Open(Client::Ui::ModalQuestionCase, "Otra"), 0);
}

// The CL9.4 cases stop with the factory's address.
TEST_F(OldWindowsTest, TheFriendsCasesOfTheFactoryAreACut)
{
    const auto opened = Client::Ui::CreateTheOldWindow(
        m_manager, OldWindowRequest{.kind = 3, .x = -1, .y = -1, .data = "", .aux = 0, .flags = 0}, Texts,
        OldWindowMeasure{.measure = m_text, .dcFont = FontKind::Normal, .screen = Screen640});
    ASSERT_FALSE(opened.HasValue());
    EXPECT_NE(opened.Error().find("0x48ee60"), std::string::npos);
}

// 0x48ee98..0x48ee9b compares unsigned with 10: both 11 and −1 return 0 before building or spending IDs.
TEST_F(OldWindowsTest, UnknownFactoryKindsReturnZeroWithoutChangingTheManager)
{
    const std::int32_t existing = Open(Client::Ui::NoticeCase, "ya abierta", 0, 0, 0, Client::Ui::KeepPlaceFlag);
    const std::int32_t lastId = m_manager.lastId;
    const std::int32_t pressedButton = Window(existing).box.buttons.front().base.id;
    m_manager.focus = existing;
    m_pressedButton = pressedButton;
    Client::Ui::PostToTheManager(m_manager, Client::Ui::RaiseMessage, existing, 0);

    for (const std::int32_t kind : {11, -1})
    {
        SCOPED_TRACE(kind);
        const auto opened = Client::Ui::CreateTheOldWindow(
            m_manager, OldWindowRequest{.kind = kind, .x = -1, .y = -1, .data = "desconocido"}, Texts,
            OldWindowMeasure{.measure = m_text, .dcFont = FontKind::Normal, .screen = Screen640});

        EXPECT_TRUE(opened.HasValue()) << opened.Error();
        if (opened.HasValue())
        {
            EXPECT_EQ(opened.Value(), 0);
        }
        EXPECT_EQ(m_manager.lastId, lastId);
        EXPECT_EQ(m_manager.windows.size(), 1u);
        EXPECT_TRUE(m_manager.windows.contains(existing));
        EXPECT_EQ(m_manager.order, (std::list<std::int32_t>{existing}));
        EXPECT_EQ(m_manager.boxes, (std::list<std::int32_t>{existing}));
        EXPECT_EQ(m_manager.focus, existing);
        EXPECT_EQ(m_pressedButton, pressedButton);
        ASSERT_EQ(m_manager.queue.size(), 1u);
        EXPECT_EQ(m_manager.queue.front(),
                  (OldMessage{.type = Client::Ui::RaiseMessage, .a = existing, .b = 0, .text = {}}));
    }
}

// 0x4904bc: Esc closes the last one of the list, in view; it sounds and the focus goes to the one left.
TEST_F(OldWindowsTest, EscapeClosesTheLastWindow)
{
    const std::int32_t below = Open(Client::Ui::QuestionCase, "Abajo", 0, 0, 0, Client::Ui::KeepPlaceFlag);
    const std::int32_t top = Open(Client::Ui::QuestionCase, "Arriba", 300, 0, 0, Client::Ui::KeepPlaceFlag);
    m_keys = {EscapeKey};

    ASSERT_TRUE(Step(639, 479).HasValue());

    EXPECT_FALSE(m_manager.windows.contains(top));
    EXPECT_EQ(m_manager.order, (std::list<std::int32_t>{below}));
    EXPECT_EQ(m_sound, Client::Audio::SoundId{ButtonSound});
    EXPECT_EQ(Window(below).base.queue.front(), (OldMessage{.type = 6, .a = 0, .b = 0, .text = {}}));
}

// F6 (0x490561): the first hides the shown ones and notes them; the second shows them, asks the manager to raise the
// top one and empties the list.
TEST_F(OldWindowsTest, F6HidesTheWindowsAndShowsThemAgain)
{
    const std::int32_t id = Open(Client::Ui::QuestionCase, "Hola");
    m_focus = ChatFocus::Message;
    m_manager.focus = id;

    m_keys = {F6Key};
    ASSERT_TRUE(Step(639, 479).HasValue());
    EXPECT_EQ(Window(id).base.state, Client::Ui::HiddenWindow);
    EXPECT_EQ(m_manager.hidden, (std::list<std::int32_t>{id}));
    EXPECT_EQ(m_manager.hiddenByF6, 1);
    EXPECT_EQ(m_manager.focus, 0);
    EXPECT_EQ(m_focus, ChatFocus::Game);

    m_keys = {F6Key};
    ASSERT_TRUE(Step(639, 479).HasValue());
    EXPECT_EQ(Window(id).base.state, Client::Ui::ShownWindow);
    EXPECT_TRUE(m_manager.hidden.empty());
    EXPECT_EQ(m_manager.hiddenByF6, 0);
    EXPECT_EQ(m_manager.focus, id);
}

// F6 with no window shown sets +0xa8 to 1 with the list empty: from then on F6 does nothing more, and a box opened
// later is not hidden (Q-C573).
TEST_F(OldWindowsTest, AFirstF6WithNothingShownLeavesF6Stuck)
{
    m_keys = {F6Key};
    ASSERT_TRUE(Step(639, 479).HasValue());
    ASSERT_EQ(m_manager.hiddenByF6, 1);

    const std::int32_t id = Open(Client::Ui::QuestionCase, "Hola");
    m_keys = {F6Key};
    ASSERT_TRUE(Step(639, 479).HasValue());
    EXPECT_TRUE(m_keys.empty());
    EXPECT_EQ(Window(id).base.state, Client::Ui::ShownWindow);
    EXPECT_EQ(m_manager.hiddenByF6, 1);
}

// One hidden by F6 and freed before the second F6 leaves the count short: 0x491080 opens the friends window (Q-C448).
TEST_F(OldWindowsTest, ASecondF6WithAWindowGoneOpensTheFriendsWindow)
{
    const std::int32_t id = Open(Client::Ui::QuestionCase, "Hola");
    m_keys = {F6Key};
    ASSERT_TRUE(Step(639, 479).HasValue());
    ASSERT_TRUE(Client::Ui::DestroyTheOldWindow(m_manager, id, m_pressedButton).HasValue());

    m_keys = {F6Key};
    const auto stepped = Step(639, 479);

    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x491080"), std::string::npos);
}

// With the game (CL9 milestone 9), the second F6 opens the friends window with 0x491080 instead of showing the one
// that is gone (Q-C448).
TEST_F(OldWindowsTest, ShowingAgainWithAWindowGoneRunsTheFriendsOpening)
{
    int opened = 0;
    OldWindowStepView view{.mouseX = 639, .mouseY = 479};
    view.openFriends = [&opened]() -> Client::Core::Expected<void, std::string>
    {
        ++opened;
        return {};
    };
    const std::int32_t id = Open(Client::Ui::QuestionCase, "Hola");
    ASSERT_TRUE(Client::Ui::HideTheOldWindows(m_manager, 1, 1, Globals(), &view).HasValue());
    ASSERT_TRUE(Client::Ui::DestroyTheOldWindow(m_manager, id, m_pressedButton).HasValue());
    ASSERT_TRUE(Client::Ui::HideTheOldWindows(m_manager, 0, 1, Globals(), &view).HasValue());
    EXPECT_EQ(opened, 1);
    EXPECT_TRUE(m_manager.hidden.empty());
}

// 0x4906a0(1, 0), the Chaos Castle one: it hides them without noting them, and F6 no longer shows them again
// (Q-C572); the modal one stays.
TEST_F(OldWindowsTest, HidingWithoutFriendsDoesNotNoteTheWindows)
{
    const std::int32_t box = Open(Client::Ui::QuestionCase, "Hola");
    const std::int32_t modal = Open(Client::Ui::ModalNoticeCase, "Espera", 300, 0);

    ASSERT_TRUE(Client::Ui::HideTheOldWindows(m_manager, 1, 0, Globals()).HasValue());
    EXPECT_EQ(Window(box).base.state, Client::Ui::HiddenWindow);
    EXPECT_EQ(Window(modal).base.state, Client::Ui::ShownWindow);
    EXPECT_TRUE(m_manager.hidden.empty());

    m_keys = {F6Key};
    ASSERT_TRUE(Step(639, 479).HasValue());
    m_keys = {F6Key};
    ASSERT_TRUE(Step(639, 479).HasValue());
    EXPECT_EQ(Window(box).base.state, Client::Ui::HiddenWindow);
}

// Top to bottom: with two boxes one over the other, the mouse belongs to the top one; a press on the lower one, where
// the other does not cover it, raises it.
TEST_F(OldWindowsTest, TheMouseGoesToTheTopWindowAndAPressRaisesTheOneBelow)
{
    const std::int32_t below = Open(Client::Ui::QuestionCase, "Abajo", 0, 0, 0, Client::Ui::KeepPlaceFlag);
    const std::int32_t top = Open(Client::Ui::QuestionCase, "Arriba", 100, 0, 0, Client::Ui::KeepPlaceFlag);

    ASSERT_TRUE(Step(120, 2).HasValue());
    EXPECT_EQ(m_hovered, top);

    Press();
    ASSERT_TRUE(Step(50, 30).HasValue());
    EXPECT_EQ(m_hovered, below);
    EXPECT_EQ(m_manager.order, (std::list<std::int32_t>{top, below}));
    EXPECT_EQ(m_manager.focus, below);
}

// 0x492ac0: a press on the title bar captures the mouse (0x5762eb4) and drags the box, clamped to the screen; on
// release, it leaves it.
TEST_F(OldWindowsTest, TheTitleBarDragsTheBox)
{
    const std::int32_t id = Open(Client::Ui::QuestionCase, "Hola", 100, 100);

    Press();
    ASSERT_TRUE(Step(110, 105).HasValue());
    EXPECT_EQ(m_capture, id);
    EXPECT_EQ(Window(id).base.state, Client::Ui::DraggedWindow);

    m_leftClick = false;
    ASSERT_TRUE(Step(130, 95).HasValue());
    EXPECT_EQ(Window(id).base.x, 120);
    EXPECT_EQ(Window(id).base.y, 90);
    ASSERT_TRUE(Step(0, 0).HasValue());
    EXPECT_EQ(Window(id).base.x, 0);
    EXPECT_EQ(Window(id).base.y, 0);

    Release();
    ASSERT_TRUE(Step(0, 0).HasValue());
    EXPECT_EQ(m_capture, 0);
    EXPECT_EQ(Window(id).base.state, Client::Ui::ShownWindow);
}

// 0x491960: the colors with ftol(alpha · 255), and 3 with ftol(alpha · 179).
TEST(OldWindowPaletteTest, TheShadowTakesLessAlpha)
{
    EXPECT_EQ(Client::Ui::OldWindowPalette(0, 1.0f),
              (Color{.red = 146.0f / 255.0f, .green = 134.0f / 255.0f, .blue = 121.0f / 255.0f, .alpha = 1.0f}));
    EXPECT_EQ(Client::Ui::OldWindowPalette(3, 1.0f),
              (Color{.red = 0.0f, .green = 0.0f, .blue = 0.0f, .alpha = 179.0f / 255.0f}));
    EXPECT_FLOAT_EQ(Client::Ui::OldWindowPalette(2, 0.5f)->alpha, 127.0f / 255.0f);
    EXPECT_FALSE(Client::Ui::OldWindowPalette(19, 1.0f).has_value());
    EXPECT_FALSE(Client::Ui::OldWindowPalette(-1, 1.0f).has_value());
}

struct ExpectedCommand
{
    std::optional<TextureSlot> texture;
    BlendMode blend;
    bool alphaTest;
    std::size_t quads;
};

// 0x48ee60 case 9's box at (0, 0x14c), drawn by 0x492500 with no mouse over it and no friends window: shadow, text,
// buttons (0x4a3a90 and 0x45b2e0), the whole frame in gray (0.5; the title bar unblended because the button left
// 0x620980), edges 10..13, border (0x492180), line, title and cross. The hidden window list adds nothing.
TEST_F(OldWindowsTest, TheBoxIsDrawnLikeTheOriginal)
{
    static_cast<void>(Open(Client::Ui::QuestionCase, "Hola"));

    Draw();

    const std::span<const DrawCommand> commands = m_sprites.Commands();
    const std::vector<ExpectedCommand> expected{
        {std::nullopt, BlendMode::Alpha, false, 1},   // the shadow
        {TextMarkerSlot, BlendMode::Alpha, false, 1}, // "Hola"
        {TextureSlot{Client::Ui::WindowButtonSlot}, BlendMode::Alpha, true, 1},
        {TextMarkerSlot, BlendMode::Alpha, true, 1}, // "Si"
        {TextureSlot{Client::Ui::WindowButtonSlot}, BlendMode::Alpha, true, 1},
        {TextMarkerSlot, BlendMode::Alpha, true, 1}, // "No"
        {TextureSlot{Client::Ui::WindowTitleSlot}, BlendMode::Opaque, false, 1},
        {std::nullopt, BlendMode::Opaque, false, 6 + 16}, // the edges and the border
        {std::nullopt, BlendMode::Alpha, false, 1},       // the line
        {TextMarkerSlot, BlendMode::Alpha, false, 1},     // "Pregunta"
        {TextureSlot{Client::Ui::WindowTitleButtonsSlot}, BlendMode::Alpha, false, 1},
    };
    ASSERT_EQ(commands.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        SCOPED_TRACE(i);
        EXPECT_EQ(commands[i].texture, expected[i].texture);
        EXPECT_EQ(commands[i].blend, expected[i].blend);
        EXPECT_EQ(commands[i].alphaTest, expected[i].alphaTest);
        EXPECT_EQ(commands[i].vertexCount, expected[i].quads * 6);
    }

    const std::span<const SpriteVertex> vertices = m_sprites.Vertices();
    // The shadow: (x, y + 5), w × (h − 10), black with 179.
    EXPECT_NEAR(vertices[0].x, 0.0f, 1e-4f);
    EXPECT_NEAR(vertices[0].y, 480.0f - 337.0f, 1e-4f);
    EXPECT_NEAR(vertices[2].x, 150.0f, 1e-4f);
    EXPECT_NEAR(vertices[2].y, 480.0f - 427.0f, 1e-4f);
    EXPECT_FLOAT_EQ(vertices[0].color.alpha, 179.0f / 255.0f);
    // The "Si" button at (0x17, 0x189), 0x32 × 0x14, at 0.8.
    const SpriteVertex& yes = vertices[commands[2].firstVertex];
    EXPECT_NEAR(yes.x, 23.0f, 1e-4f);
    EXPECT_NEAR(yes.y, 480.0f - 393.0f, 1e-4f);
    EXPECT_NEAR(vertices[commands[2].firstVertex + 2].x, 73.0f, 1e-4f);
    EXPECT_FLOAT_EQ(yes.color.red, 0.8f);
    // The title bar at (x + 6, y + 5), w − 12 × 15, at 0.5.
    const SpriteVertex& bar = vertices[commands[6].firstVertex];
    EXPECT_NEAR(bar.x, 6.0f, 1e-4f);
    EXPECT_NEAR(bar.y, 480.0f - 337.0f, 1e-4f);
    EXPECT_NEAR(vertices[commands[6].firstVertex + 2].x, 144.0f, 1e-4f);
    EXPECT_NEAR(vertices[commands[6].firstVertex + 2].y, 480.0f - 352.0f, 1e-4f);
    EXPECT_FLOAT_EQ(bar.color.red, 0.5f);
    // The first edge, 10 instead of 8 (it is not the top one with the friends window).
    const SpriteVertex& edge = vertices[commands[7].firstVertex];
    EXPECT_NEAR(edge.x, 5.0f, 1e-4f);
    EXPECT_NEAR(edge.y, 480.0f - 337.0f, 1e-4f);
    EXPECT_FLOAT_EQ(edge.color.red, 83.0f / 255.0f);
    // The cross, 9 × 9 at (x + w − 16, y + 8), at 0.8.
    const SpriteVertex& close = vertices[commands[10].firstVertex];
    EXPECT_NEAR(close.x, 134.0f, 1e-4f);
    EXPECT_NEAR(close.y, 480.0f - 340.0f, 1e-4f);
    EXPECT_FLOAT_EQ(close.color.red, 0.8f);

    ASSERT_EQ(m_text.texts.size(), 4u);
    EXPECT_EQ(m_text.texts[0].text, "Hola");
    EXPECT_EQ(m_text.texts[0].x, 10);
    EXPECT_EQ(m_text.texts[0].y, BoxY + 0x1d);
    EXPECT_EQ(m_text.texts[0].style.font, FontKind::Normal);
    EXPECT_EQ(m_text.texts[0].style.colors, (TextColors{.text = 0xffffffffu, .background = 0}));
    // ((0x32 − 12) + 0.5) · 0.5 + x and ((0x14 − 13) + 0.5) · 0.5 + y + 1.
    EXPECT_EQ(m_text.texts[1].text, "Si");
    EXPECT_EQ(m_text.texts[1].x, 42);
    EXPECT_EQ(m_text.texts[1].y, 397);
    EXPECT_EQ(m_text.texts[1].style.colors, (TextColors{.text = 0xffc8dce6u, .background = 0}));
    EXPECT_EQ(m_text.texts[2].text, "No");
    EXPECT_EQ(m_text.texts[2].x, 97);
    EXPECT_EQ(m_text.texts[3].text, "Pregunta");
    EXPECT_EQ(m_text.texts[3].x, 9);
    EXPECT_EQ(m_text.texts[3].y, BoxY + 8);
    EXPECT_EQ(m_text.texts[3].style.font, FontKind::Bold);
    EXPECT_EQ(m_text.texts[3].style.colors, (TextColors{.text = 0xff646e73u, .background = 0}));
    EXPECT_EQ(m_dcFont, FontKind::Normal);
    EXPECT_EQ(m_sprites.Blend(), BlendMode::Opaque);
    EXPECT_EQ(m_manager.drawn, 1);
    // 0x4a5490: the window list measures its row with "0" the first time.
    EXPECT_FLOAT_EQ(m_manager.windowList.list.rowHeight, 13.0f);
}

// With the mouse and the click on the pressed button: 0.6, one pixel inside and the text one lower and to the right.
TEST_F(OldWindowsTest, ThePressedButtonSinksOnePixel)
{
    const std::int32_t id = Open(Client::Ui::QuestionCase, "Hola");
    Window(id).box.buttons[0].pressed = 1;
    m_leftClick = true;

    Draw(false, 30, 400);

    const std::span<const DrawCommand> commands = m_sprites.Commands();
    ASSERT_GE(commands.size(), 3u);
    const SpriteVertex& yes = m_sprites.Vertices()[commands[2].firstVertex];
    EXPECT_NEAR(yes.x, 24.0f, 1e-4f);
    EXPECT_NEAR(yes.y, 480.0f - 394.0f, 1e-4f);
    EXPECT_NEAR(m_sprites.Vertices()[commands[2].firstVertex + 2].x, 73.0f, 1e-4f);
    EXPECT_FLOAT_EQ(yes.color.red, 0.6f);
    EXPECT_EQ(m_text.texts[1].x, 43);
    EXPECT_EQ(m_text.texts[1].y, 398);
}

// The top one with the friends window open (0x659290(1)) is not dimmed: edges 8 and 9, the light title and no final
// opaque blend.
TEST_F(OldWindowsTest, TheTopWindowWithTheFriendsWindowOpenIsNotDimmed)
{
    static_cast<void>(Open(Client::Ui::QuestionCase, "Hola"));

    Draw(true);

    const std::span<const DrawCommand> commands = m_sprites.Commands();
    ASSERT_EQ(commands.size(), 11u);
    EXPECT_FLOAT_EQ(m_sprites.Vertices()[commands[6].firstVertex].color.red, 1.0f);
    EXPECT_FLOAT_EQ(m_sprites.Vertices()[commands[7].firstVertex].color.red, 153.0f / 255.0f);
    EXPECT_EQ(m_text.texts[3].style.colors.text, 0xffc8dce6u);
}
// P-C215: F5 with two chats (two boxes as chats): 0x4a5be0 shows the list and chooses the one not on top, then the
// next with wraparound; Enter raises the chosen one (0x4a4e20); the mouse over the rows (0x4a4eb0) chooses them bottom
// up and the release minimizes the top one or raises another. At the top edge, none (Q-C1008).
TEST_F(OldWindowsTest, F5ShowsTheChatsAndChoosesOne)
{
    constexpr std::uint8_t F5Key = 0x74;
    const std::int32_t first = Open(Client::Ui::QuestionCase, "Uno");
    const std::int32_t second = Open(Client::Ui::QuestionCase, "Dos");
    OldWindow& list = m_manager.windowList;
    list.list.chats = {first, second};
    list.list.cursor = 2;
    list.list.rowHeight = 14.0f;
    m_keys.insert(F5Key);
    ASSERT_TRUE(Step(0, 0).HasValue());
    EXPECT_EQ(list.base.state, Client::Ui::ShownWindow);
    EXPECT_EQ(list.list.armed, 1);
    EXPECT_EQ(list.base.height, 36);
    EXPECT_EQ(list.base.y, 0x1b3 - 36);
    EXPECT_FLOAT_EQ(list.list.blinkStep, 0.25f);
    EXPECT_EQ(list.list.cursor, 0u);
    m_keys.insert(F5Key);
    ASSERT_TRUE(Step(0, 0).HasValue());
    EXPECT_EQ(list.list.cursor, 1u);
    m_keys.insert(F5Key);
    ASSERT_TRUE(Step(0, 0).HasValue());
    EXPECT_EQ(list.list.cursor, 0u);
    m_keys.insert(EnterKey);
    ASSERT_TRUE(Step(0, 0).HasValue());
    EXPECT_EQ(list.base.state, Client::Ui::HiddenWindow);
    EXPECT_EQ(list.list.armed, 0);
    EXPECT_EQ(m_manager.order.back(), first);

    // Hidden, F5 skips the top one (now the first). The mouse on the bottom row is the first: it is the top one and is
    // minimized.
    m_keys.insert(F5Key);
    ASSERT_TRUE(Step(0, 0).HasValue());
    EXPECT_EQ(list.list.cursor, 1u);
    Release();
    ASSERT_TRUE(Step(0x250, 0x1b3 - 5).HasValue());
    EXPECT_EQ(m_sound, Client::Audio::SoundId{ButtonSound});
    EXPECT_EQ(m_mouseOnUi, 1);
    EXPECT_EQ(list.base.state, Client::Ui::HiddenWindow);
    EXPECT_EQ(Window(first).base.state, Client::Ui::HiddenWindow);
    EXPECT_EQ(m_manager.order.front(), first);

    // The first, hidden, is raised.
    m_keys.insert(F5Key);
    ASSERT_TRUE(Step(0, 0).HasValue());
    Release();
    m_leftHeld = true;
    ASSERT_TRUE(Step(0x250, 0x1b3 - 5).HasValue());
    EXPECT_EQ(m_manager.order.back(), first);
    EXPECT_EQ(Window(first).base.state, Client::Ui::ShownWindow);
    EXPECT_FALSE(m_leftHeld);

    // The top edge gives the end: none, and the list hides anyway.
    m_keys.insert(F5Key);
    ASSERT_TRUE(Step(0, 0).HasValue());
    Release();
    ASSERT_TRUE(Step(0x250, 0x1b3 - 36).HasValue());
    EXPECT_EQ(list.list.cursor, 2u);
    EXPECT_EQ(list.base.state, Client::Ui::HiddenWindow);
    EXPECT_EQ(m_manager.order.back(), first);

    // Off the rows and without F5, the chosen one goes to the end; on freeing a chat (0x4a4b30), too.
    m_keys.insert(F5Key);
    ASSERT_TRUE(Step(0, 0).HasValue());
    list.list.armed = 0;
    ASSERT_TRUE(Step(0, 0).HasValue());
    EXPECT_EQ(list.list.cursor, 2u);
    list.list.cursor = 0;
    ASSERT_TRUE(Client::Ui::DestroyTheOldWindow(m_manager, first, m_pressedButton).HasValue());
    EXPECT_EQ(list.list.chats.size(), 1u);
    EXPECT_EQ(list.list.cursor, 1u);

    // With the focus on the chat input, F5 is not read; in an old window's EDIT, it is (0x467d3b).
    m_focus = ChatFocus::Message;
    m_keys.insert(F5Key);
    ASSERT_TRUE(Step(0, 0).HasValue());
    EXPECT_EQ(list.list.armed, 0);
    Release();
    m_focus = ChatFocus::OldWindow;
    m_keys.insert(F5Key);
    ASSERT_TRUE(Step(0, 0).HasValue());
    EXPECT_EQ(list.list.armed, 1);
}
} // namespace
