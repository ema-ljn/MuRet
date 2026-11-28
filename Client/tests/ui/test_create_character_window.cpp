#include "FrontEnd/CreateCharacterWindow.h"

#include "Core/PixelSize.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Render/SpriteBatch.h"
#include "Render/TextureSlot.h"
#include "Ui/NewUi.h"
#include "FrontEnd/NameChecks.h"
#include "Ui/Widget.h"

#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"
#include "support/UiInput.h"
#include "support/UiPrinters.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Client::FrontEnd::CreateCharacterWindow: the selection's create window (+0x8498: 0x63d1a0, 0x63d4e0, 0x63d7c0,
// 0x63d880 and 0x63e0a0), without the preview. The names are synthetic.
namespace
{
using Client::Core::PixelSize;
using Client::FrontEnd::CreateCharacterWindow;
using Client::FrontEnd::CreateWindowEvent;
using Client::FrontEnd::CreateWindowLayout;
using Client::FrontEnd::LayoutCreateWindow;
using Client::Input::InputState;
using Client::Input::Key;
using Client::Tests::ClickAt;
using Client::Tests::TapKey;
using Client::Tests::TypeText;
using Client::Ui::NewUiTurn;
using Client::Ui::Point;
using Client::Ui::Rect;

constexpr PixelSize Screen640{640, 480};

Client::Ui::TextureSizes LabSizes()
{
    return Client::Tests::FakeTextureSizes(
        {{0xc, {512, 64}}, {0xd, {128, 128}}, {0x724, {64, 128}}, {0x725, {64, 128}}});
}

std::optional<CreateWindowEvent> StepOnce(CreateCharacterWindow& window, InputState& input)
{
    std::optional<CreateWindowEvent> event = window.Step(input, Screen640);
    input.ConsumeEdges();
    return event;
}

void ClickIn(CreateCharacterWindow& window, InputState& input, const Rect& area)
{
    ClickAt(input, area.x + 5, area.y + 5);
    static_cast<void>(StepOnce(window, input));
}

// 0x63d4e0 with ((W − 454) / 2, (H − 406) / 2): at 640 × 480, (93, 37), the contract's emulated table.
TEST(CreateCharacterWindowTest, TheLayoutFollowsTheScreen)
{
    const CreateWindowLayout layout = LayoutCreateWindow(Screen640, 1);
    EXPECT_EQ(layout.box, (Rect{.x = 93, .y = 37, .width = 454, .height = 406}));
    EXPECT_EQ(layout.nameBack, (Rect{.x = 93, .y = 354, .width = 346, .height = 38}));
    EXPECT_EQ(layout.attributes, (Rect{.x = 439, .y = 87, .width = 108, .height = 80}));
    EXPECT_EQ(layout.strip, (Rect{.x = 93, .y = 392, .width = 454, .height = 51}));
    const std::vector<int> classTops{194, 220, 246, 283, 309};
    for (std::size_t i = 0; i < classTops.size(); ++i)
    {
        EXPECT_EQ(layout.classes[i], (Rect{.x = 439, .y = classTops[i], .width = 108, .height = 26})) << i;
    }
    EXPECT_EQ(layout.ok, (Rect{.x = 439, .y = 362, .width = 54, .height = 30}));
    EXPECT_EQ(layout.cancel, (Rect{.x = 493, .y = 362, .width = 54, .height = 30}));
    EXPECT_EQ(layout.nameField, (Point{171, 375}));
    // The attributes box grows with the Dark Lord (0x647370(0, 96, 2)).
    EXPECT_EQ(LayoutCreateWindow(Screen640, 4).attributes.height, 96);
    const CreateWindowLayout at1024 = LayoutCreateWindow(PixelSize{1024, 768}, 0);
    EXPECT_EQ(at1024.box.x, 285);
    EXPECT_EQ(at1024.box.y, 181);
    // The field at 640 × 480: ftol((285 + 78) / 1.6) = 226 and ftol((181 + 338) / 1.6) = 324.
    EXPECT_EQ(at1024.nameField, (Point{226, 324}));
}

// 0x63d7c0: pkt[4] is the highest class that can be created: it enables from 0 to it. MuRet bounds it to 4 (the
// original writes outside the five).
TEST(CreateCharacterWindowTest, TheClassesUpToPkt4AreEnabled)
{
    CreateCharacterWindow window(LabSizes(), nullptr);
    for (const auto& [creatable, enabled] :
         {std::pair{std::uint8_t{0}, std::size_t{1}}, std::pair{std::uint8_t{2}, std::size_t{3}},
          std::pair{std::uint8_t{4}, std::size_t{5}}, std::pair{std::uint8_t{7}, std::size_t{5}}})
    {
        window.EnableClasses(creatable);
        for (std::size_t i = 0; i < 5; ++i)
        {
            EXPECT_EQ(window.ClassButton(i).Enabled(), i < enabled) << static_cast<int>(creatable) << " " << i;
        }
    }
}

// The build checks the Dark Knight (+0x890 at 1). With pkt[4] at 0 it stays checked and disabled, and OK creates it
// anyway: 0x63d990 reads the class, not the button.
TEST(CreateCharacterWindowTest, TheKnightStartsChecked)
{
    CreateCharacterWindow window(LabSizes(), nullptr);
    window.EnableClasses(0);
    window.Open();
    EXPECT_EQ(window.ChosenClass(), 1);
    EXPECT_TRUE(window.ClassButton(1).Checked());
    EXPECT_FALSE(window.ClassButton(1).Enabled());
    InputState input;
    TypeText(input, "char0001");
    static_cast<void>(StepOnce(window, input));
    TapKey(input, Key::Enter);
    EXPECT_EQ(StepOnce(window, input), std::optional{CreateWindowEvent::Submitted});
    EXPECT_EQ(window.ChosenClass(), 1);
}

// 0x63d880: the click on an enabled class checks only it and chooses it; on a disabled one, nothing. The class does
// not return to 1 on closing and opening.
TEST(CreateCharacterWindowTest, AClassClickChoosesIt)
{
    CreateCharacterWindow window(LabSizes(), nullptr);
    window.EnableClasses(2);
    window.Open();
    InputState input;
    const CreateWindowLayout layout = LayoutCreateWindow(Screen640, 1);

    ClickIn(window, input, layout.classes[2]);
    EXPECT_EQ(window.ChosenClass(), 2);
    for (std::size_t i = 0; i < 5; ++i)
    {
        EXPECT_EQ(window.ClassButton(i).Checked(), i == 2) << i;
    }
    ClickIn(window, input, layout.classes[4]);
    EXPECT_EQ(window.ChosenClass(), 2);

    window.Close();
    window.Open();
    EXPECT_EQ(window.ChosenClass(), 2);
}

// 0x63d670: the field empties on open and on close, and takes up to 10 bytes (EM_LIMITTEXT(10)).
TEST(CreateCharacterWindowTest, TheNameIsClearedOnOpenAndClose)
{
    CreateCharacterWindow window(LabSizes(), nullptr);
    window.Open();
    InputState input;
    TypeText(input, "abcdefghijkl");
    static_cast<void>(StepOnce(window, input));
    EXPECT_EQ(window.Name(), "abcdefghij");
    TypeText(input, "\b");
    static_cast<void>(StepOnce(window, input));
    EXPECT_EQ(window.Name(), "abcdefghi");

    window.Open();
    EXPECT_EQ(window.Name(), "");
    TypeText(input, "abc");
    static_cast<void>(StepOnce(window, input));
    window.Close();
    EXPECT_EQ(window.Name(), "");
    EXPECT_FALSE(window.IsOpen());
}

// OK or Enter asks to create (the window stays open: whoever sends closes it); cancel or Esc close it. Closed, it
// handles nothing.
TEST(CreateCharacterWindowTest, EnterSubmitsAndEscCloses)
{
    const CreateWindowLayout layout = LayoutCreateWindow(Screen640, 1);
    CreateCharacterWindow window(LabSizes(), nullptr);
    window.Open();
    InputState input;

    ClickAt(input, layout.ok.x + 5, layout.ok.y + 5);
    EXPECT_EQ(StepOnce(window, input), std::optional{CreateWindowEvent::Submitted});
    EXPECT_TRUE(window.IsOpen());
    TapKey(input, Key::Enter);
    EXPECT_EQ(StepOnce(window, input), std::optional{CreateWindowEvent::Submitted});
    TapKey(input, Key::Escape);
    EXPECT_EQ(StepOnce(window, input), std::optional{CreateWindowEvent::Cancelled});
    EXPECT_FALSE(window.IsOpen());
    TapKey(input, Key::Enter);
    EXPECT_EQ(StepOnce(window, input), std::nullopt);

    window.Open();
    ClickAt(input, layout.cancel.x + 5, layout.cancel.y + 5);
    EXPECT_EQ(StepOnce(window, input), std::optional{CreateWindowEvent::Cancelled});
    EXPECT_FALSE(window.IsOpen());
}

// With a notice on top (the name's, or the reply's), the window does not have the mouse: its buttons stay without
// hover and without click (0x65fc30 and 0x63c490).
TEST(CreateCharacterWindowTest, UnderANoticeTheButtonsLetGoOfTheMouse)
{
    const CreateWindowLayout layout = LayoutCreateWindow(Screen640, 1);
    CreateCharacterWindow window(LabSizes(), nullptr);
    window.Open();
    InputState input;
    Client::Tests::MoveMouse(input, layout.ok.x + 5, layout.ok.y + 5);
    static_cast<void>(StepOnce(window, input));
    ASSERT_EQ(window.OkButton().Frame(), 1);

    static_cast<void>(window.Step(input, Screen640, NewUiTurn{.mouse = false, .logic = false}));

    EXPECT_EQ(window.OkButton().Frame(), 0);
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

// 0x66a050 and 0x63e0a0 without the preview: the black base of the whole screen, the name background, the attributes
// box and the strip, the seven buttons with labels 20 to 24 (the classes), the attributes in 0xff00b4ff with their
// labels, the description and the name with the cursor.
TEST(CreateCharacterWindowTest, TheDrawGoesBaseBacksButtonsAndTexts)
{
    const auto texts = Client::Tests::MakeTextTable({{20, "Wiz"},
                                                     {21, "Kni"},
                                                     {22, "Elf"},
                                                     {23, "Mag"},
                                                     {24, "Lor"},
                                                     {1701, "Str"},
                                                     {1702, "Agi"},
                                                     {1703, "Vit"},
                                                     {1704, "Ene"},
                                                     {1706, "Knight line"}});
    CreateCharacterWindow window(LabSizes(), &texts);
    window.Open();
    InputState input;
    TypeText(input, "char0001");
    static_cast<void>(StepOnce(window, input));
    Client::Render::SpriteBatch batch;
    batch.Begin(Screen640);
    Client::Tests::RecordingTextDrawer text(batch);

    window.DrawBase(batch, Screen640);
    window.Draw(batch, text, Screen640);

    const std::uint16_t marker = Client::Tests::TextMarkerSlot.value;
    const std::vector<std::uint16_t> slots = QuadSlots(batch);
    ASSERT_GE(slots.size(), 4u + 7u);
    EXPECT_EQ((std::vector<std::uint16_t>(slots.begin(), slots.begin() + 4)),
              (std::vector<std::uint16_t>{0xffff, 0xc, 0xffff, 0xffff}));
    // Each button, with its label behind if it has one.
    EXPECT_EQ(
        (std::vector<std::uint16_t>(slots.begin() + 4, slots.begin() + 16)),
        (std::vector<std::uint16_t>{0xd, marker, 0xd, marker, 0xd, marker, 0xd, marker, 0xd, marker, 0x724, 0x725}));
    std::vector<std::string> drawn;
    for (const auto& recorded : text.texts)
    {
        drawn.push_back(recorded.text);
    }
    EXPECT_EQ(drawn, (std::vector<std::string>{"Wiz", "Kni", "Elf", "Mag", "Lor", "28", "Str", "20", "Agi", "25", "Vit",
                                               "10", "Ene", "Knight line", "char0001", "_"}));
    // The values at (ax + 76, ay + 10 + 17 i) of 640 × 480, in 0xff00b4ff; the labels at ax + 22.
    EXPECT_EQ(text.texts[5].x, 439 + 76);
    EXPECT_EQ(text.texts[5].y, 87 + 10);
    EXPECT_EQ(text.texts[5].style.colors.text, 0xff00b4ffu);
    EXPECT_EQ(text.texts[6].x, 439 + 22);
    EXPECT_EQ(text.texts[6].style.colors.text, 0xffffffffu);
    EXPECT_EQ(text.texts[11].y, 87 + 10 + 17 * 3);
    // The description at (x + 10, y + 355 + 12), and the name in the field.
    EXPECT_EQ(text.texts[13].x, 93 + 10);
    EXPECT_EQ(text.texts[13].y, 37 + 355 + 12);
    EXPECT_EQ(text.texts[14].x, 171);
    EXPECT_EQ(text.texts[14].y, 375);
    // The chosen class (1) comes out checked: frame 3, row 78 of cha_bt.
    EXPECT_EQ(window.ClassButton(1).Frame(), 3);
}

// With the Dark Lord, the fifth attribute: "25" and text 1738, at ay + 78; and the description split into two lines
// of under 75 bytes (SplitIntoLines), the second 19 lower.
TEST(CreateCharacterWindowTest, TheDarkLordHasCommandAndTwoLines)
{
    const std::string longLine(60, 'a');
    const auto texts = Client::Tests::MakeTextTable({{1738, "Com"}, {1709, longLine + " " + std::string(30, 'b')}});
    CreateCharacterWindow window(LabSizes(), &texts);
    window.EnableClasses(4);
    window.Open();
    InputState input;
    ClickIn(window, input, LayoutCreateWindow(Screen640, 4).classes[4]);
    Client::Render::SpriteBatch batch;
    batch.Begin(Screen640);
    Client::Tests::RecordingTextDrawer text(batch);

    window.Draw(batch, text, Screen640);

    std::vector<std::string> drawn;
    for (const auto& recorded : text.texts)
    {
        drawn.push_back(recorded.text);
    }
    // 0x632c90 cuts by bytes, under 75: 74 in the first and the rest in the second.
    EXPECT_EQ(drawn, (std::vector<std::string>{"26", "20", "20", "15", "25", "Com",
                                               longLine + " " + std::string(13, 'b'), std::string(17, 'b'), "_"}));
    EXPECT_EQ(text.texts[4].y, 87 + 78);
    EXPECT_EQ(text.texts[7].y, 37 + 355 + 12 + 19);
}

// U+00f1 keeps the same byte with the EDIT's XP profile.
TEST(CreateCharacterWindowTest, TheNameKeepsTheNWithTildeByte)
{
    CreateCharacterWindow window(LabSizes(), nullptr);
    window.Open();
    InputState input;
    TypeText(input, "Pe\xc3\xb1"
                    "a");
    static_cast<void>(StepOnce(window, input));
    EXPECT_EQ(window.Name(), "Pe\xf1"
                             "a");
}

TEST(CreateCharacterWindowTest, TheNameUsesXpWindows1252BeforeTheExistingByteCheck)
{
    CreateCharacterWindow window(LabSizes(), nullptr);
    window.Open();
    InputState oddByte;
    TypeText(oddByte, "a€bc");
    static_cast<void>(StepOnce(window, oddByte));

    EXPECT_TRUE(window.Name() == std::string("a\x80"
                                             "bc",
                                             4));
    EXPECT_FALSE(Client::FrontEnd::HasForbiddenSymbol(window.Name()));

    window.Open();
    InputState evenByte;
    TypeText(evenByte, "€abc");
    static_cast<void>(StepOnce(window, evenByte));

    EXPECT_TRUE(window.Name() == std::string("\x80"
                                             "abc",
                                             4));
    EXPECT_TRUE(Client::FrontEnd::HasForbiddenSymbol(window.Name()));

    window.Open();
    InputState unsupported;
    TypeText(unsupported, "a漢b");
    static_cast<void>(StepOnce(window, unsupported));

    EXPECT_TRUE(window.Name() == "a?b");
    EXPECT_FALSE(Client::FrontEnd::HasForbiddenSymbol(window.Name()));
}

TEST(CreateCharacterWindowTest, EnterIsNotInsertedIntoTheNameBeforeSubmission)
{
    CreateCharacterWindow window(LabSizes(), nullptr);
    const std::array cases{
        std::pair{std::string("abcd"), std::string("abcd")},
        std::pair{std::string("abcde"), std::string("abcde")},
        std::pair{std::string("a€b"), std::string("a\x80"
                                                  "b",
                                                  3)},
    };

    for (const auto& [typed, expectedName] : cases)
    {
        window.Open();
        InputState input;
        TypeText(input, typed);
        static_cast<void>(StepOnce(window, input));
        TapKey(input, Key::Enter);

        ASSERT_EQ(StepOnce(window, input), std::optional{CreateWindowEvent::Submitted});
        EXPECT_TRUE(window.Name() == expectedName);
        EXPECT_FALSE(Client::FrontEnd::HasForbiddenSymbol(window.Name()));

        const std::string expectedBuffer = expectedName + '\0';
        EXPECT_TRUE(window.Buffer().substr(0, expectedBuffer.size()) == expectedBuffer);
    }
}

// The buffer at 0x7798bf8: OK copies the field with its zero (GetWindowTextA) and what is behind stays; it is emptied
// only on open (0x553570).
TEST(CreateCharacterWindowTest, TheBufferKeepsWhatWasBehindTheName)
{
    CreateCharacterWindow window(LabSizes(), nullptr);
    window.Open();
    InputState input;
    TypeText(input, "abcdef g");
    static_cast<void>(StepOnce(window, input));
    TapKey(input, Key::Enter);
    ASSERT_EQ(StepOnce(window, input), std::optional{CreateWindowEvent::Submitted});
    TypeText(input, "\b\b\b");
    static_cast<void>(StepOnce(window, input));
    TapKey(input, Key::Enter);
    ASSERT_EQ(StepOnce(window, input), std::optional{CreateWindowEvent::Submitted});

    EXPECT_EQ(window.Buffer().substr(0, 9), std::string_view("abcde\0 g\0", 9));
    window.Open();
    EXPECT_EQ(window.Buffer().substr(0, 9), std::string_view("\0\0\0\0\0\0\0\0\0", 9));
}

// With a notice on top, the EDIT keeps the Win32 focus: what is typed goes into the name, without a click on the
// buttons.
TEST(CreateCharacterWindowTest, UnderANoticeTheNameStillTakesTheTyping)
{
    CreateCharacterWindow window(LabSizes(), nullptr);
    window.Open();
    InputState input;
    TypeText(input, "abc");
    static_cast<void>(window.Step(input, Screen640, NewUiTurn{.mouse = false, .logic = false}));
    input.ConsumeEdges();
    TypeText(input, "\bx");
    static_cast<void>(window.Step(input, Screen640, NewUiTurn{.mouse = false, .logic = false}));
    EXPECT_EQ(window.Name(), "abx");
}
} // namespace
