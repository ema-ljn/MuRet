#include "Ui/NoticeWindow.h"

#include "Core/PixelSize.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/FontSet.h"
#include "Render/TextureSlot.h"
#include "Ui/Notices.h"
#include "Ui/Widget.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"
#include "support/UiInput.h"

#include <gtest/gtest.h>

#include <optional>

// Client::Ui::NoticeWindow: the new UI's type 2 notice, with an OK (0x6429b0, 0x642bf0, 0x642f00 and 0x642e30, in the
// step; CL5.5). It darkens the screen, shows its box with the text by index and closes with OK, Enter or Esc. The
// name is not MessageBox: <windows.h> defines a macro with that name.
namespace
{
using Client::Core::PixelSize;
using Client::Input::InputState;
using Client::Input::Key;
using Client::Tests::ClickAt;
using Client::Tests::TapKey;
using Client::Ui::LayoutNotice;
using Client::Ui::NoticeId;
using Client::Ui::NoticeLayout;
using Client::Ui::NoticeWindow;
using Client::Ui::Rect;

constexpr PixelSize Screen640{640, 480};

Client::Ui::TextureSizes LabSizes()
{
    return Client::Tests::FakeTextureSizes(
        {{0x727, {512, 128}}, {0x724, {64, 128}}, {0x725, {64, 128}}, {0x728, {256, 32}}});
}

std::optional<NoticeId> Some(NoticeId notice)
{
    return notice;
}

std::optional<NoticeId> StepOnce(NoticeWindow& window, InputState& input)
{
    std::optional<NoticeId> closed = window.Step(input, Screen640);
    input.ConsumeEdges();
    return closed;
}

// The 352 × 113 box in the middle, ((W − 352) / 2, (H − 113) / 2), and the 54 × 30 OK at (+149, +72).
TEST(NoticeWindowTest, TheBoxIsCenteredWithItsOk)
{
    const NoticeLayout layout = LayoutNotice(Screen640);

    EXPECT_EQ(layout.box, (Rect{.x = 144, .y = 183, .width = 352, .height = 113}));
    EXPECT_EQ(layout.ok, (Rect{.x = 293, .y = 255, .width = 54, .height = 30}));
}

TEST(NoticeWindowTest, OkClosesIt)
{
    NoticeWindow window(LabSizes(), nullptr);
    window.Open(0x70);
    const NoticeLayout layout = LayoutNotice(Screen640);
    InputState input;
    ClickAt(input, layout.ok.x + 10, layout.ok.y + 10);

    EXPECT_EQ(StepOnce(window, input), Some(0x70));
    EXPECT_FALSE(window.IsOpen());
    EXPECT_EQ(window.Notice(), std::nullopt);
}

// 0x642e30, type 2.
TEST(NoticeWindowTest, EnterOrEscClosesIt)
{
    NoticeWindow window(LabSizes(), nullptr);
    InputState input;
    window.Open(0x64);
    TapKey(input, Key::Enter);
    EXPECT_EQ(StepOnce(window, input), Some(0x64));

    window.Open(0x65);
    TapKey(input, Key::Escape);
    EXPECT_EQ(StepOnce(window, input), Some(0x65));
}

// Closed, it does nothing.
TEST(NoticeWindowTest, OnlyItsOkClosesIt)
{
    NoticeWindow window(LabSizes(), nullptr);
    InputState input;
    TapKey(input, Key::Enter);
    EXPECT_EQ(StepOnce(window, input), std::nullopt);

    window.Open(0x70);
    EXPECT_EQ(window.Notice(), Some(0x70));
    ClickAt(input, 5, 5);
    EXPECT_EQ(StepOnce(window, input), std::nullopt);
    EXPECT_TRUE(window.IsOpen());
}

// 0x6429b0: the base is an untextured image (slot -1) of the whole screen, black with alpha 0x80; then the box
// (message_back, slot 0x727) and the OK (0x724). The batch arrives with the additive blend (Q-C80): the window sets
// the normal one.
TEST(NoticeWindowTest, TheBaseDarkensTheScreen)
{
    NoticeWindow window(LabSizes(), nullptr);
    window.Open(0x70);
    Client::Render::SpriteBatch batch;
    batch.Begin(Screen640);
    batch.SetBlend(Client::Render::BlendMode::Additive);
    Client::Tests::RecordingTextDrawer text(batch);

    window.Draw(batch, text, Screen640);

    const auto commands = batch.Commands();
    ASSERT_GE(commands.size(), 3u);
    EXPECT_EQ(commands[0].texture, std::nullopt);
    EXPECT_EQ(commands[0].blend, Client::Render::BlendMode::Alpha);
    EXPECT_EQ(commands[1].texture, Client::Render::TextureSlot{0x727});
    EXPECT_EQ(commands[2].texture, Client::Render::TextureSlot{0x724});
    const auto base = batch.Vertices().first(6);
    EXPECT_EQ(base[0].x, 0.0f);
    EXPECT_EQ(base[0].y, 480.0f);
    EXPECT_EQ(base[2].x, 640.0f);
    EXPECT_EQ(base[2].y, 0.0f);
    EXPECT_EQ(base[0].color.red, 0.0f);
    EXPECT_FLOAT_EQ(base[0].color.alpha, 128.0f / 255.0f);
}

// 0x642bf0: one line goes centered in the box's width, at y + 38, with font DAT_05763560 in white: 0x642f00 passes it
// in mode 2, in a 352-wide box from the box's x. "Synthetic" measures 9 · 6 = 54: the text goes at column
// (352 − 54) / 2 = 149.
TEST(NoticeWindowTest, OneLineIsCentered)
{
    const auto texts = Client::Tests::MakeTextTable({{402, "Synthetic"}});
    NoticeWindow window(LabSizes(), &texts);
    window.Open(0x70);
    Client::Render::SpriteBatch batch;
    batch.Begin(Screen640);
    Client::Tests::RecordingTextDrawer text(batch);

    window.Draw(batch, text, Screen640);

    ASSERT_EQ(text.texts.size(), 1u);
    EXPECT_EQ(text.texts[0].text, "Synthetic");
    EXPECT_EQ(text.texts[0].x, 144);
    EXPECT_EQ(text.texts[0].y, 183 + 38);
    EXPECT_TRUE(text.texts[0].windowed);
    EXPECT_EQ(text.texts[0].firstColumn, -149);
    EXPECT_EQ(text.texts[0].columns, 352);
    EXPECT_EQ(text.texts[0].style.font, Client::Render::Text::FontKind::OtherFace12);
    EXPECT_EQ(text.texts[0].style.colors.text, 0xffffffffu);
    EXPECT_EQ(text.texts[0].style.colors.background, 0u);
}

// The 0x6f, the version: 405 and 406, at (+25, +32) and (+25, +51).
TEST(NoticeWindowTest, TwoLinesGoOnTheLeft)
{
    const auto texts = Client::Tests::MakeTextTable({{405, "First"}, {406, "Second"}});
    NoticeWindow window(LabSizes(), &texts);
    window.Open(0x6f);
    Client::Render::SpriteBatch batch;
    batch.Begin(Screen640);
    Client::Tests::RecordingTextDrawer text(batch);

    window.Draw(batch, text, Screen640);

    ASSERT_EQ(text.texts.size(), 2u);
    EXPECT_EQ(text.texts[0].text, "First");
    EXPECT_EQ(text.texts[0].x, 169);
    EXPECT_EQ(text.texts[0].y, 215);
    EXPECT_EQ(text.texts[1].text, "Second");
    EXPECT_EQ(text.texts[1].x, 169);
    EXPECT_EQ(text.texts[1].y, 234);
}

// Type 0 (the 0x5d, "Aguarde por favor..."): no buttons; Enter and Esc do not close it.
TEST(NoticeWindowTest, ANoButtonNoticeStaysOpen)
{
    NoticeWindow window(LabSizes(), nullptr);
    window.Open(0x5d);
    InputState input;
    TapKey(input, Key::Enter);
    EXPECT_EQ(StepOnce(window, input), std::nullopt);
    TapKey(input, Key::Escape);
    EXPECT_EQ(StepOnce(window, input), std::nullopt);
    ClickAt(input, 293 + 10, 255 + 10);
    EXPECT_EQ(StepOnce(window, input), std::nullopt);
    EXPECT_TRUE(window.IsOpen());
    EXPECT_EQ(window.Layout().ok, std::nullopt);
    EXPECT_EQ(window.Layout().cancel, std::nullopt);
}

// Type 3 (the 0x5e): OK at (+98, +72) and cancel at (+200, +72). OK or Enter confirm it; cancel or Esc close it
// without confirming (0x6436a0 sets the id to −1).
TEST(NoticeWindowTest, AnOkCancelNoticeCancelsWithEsc)
{
    NoticeWindow window(LabSizes(), nullptr);
    window.Open(0x5e);
    InputState input;
    EXPECT_EQ(StepOnce(window, input), std::nullopt);
    EXPECT_EQ(window.Layout().ok, (std::optional<Rect>{Rect{.x = 242, .y = 255, .width = 54, .height = 30}}));
    EXPECT_EQ(window.Layout().cancel, (std::optional<Rect>{Rect{.x = 344, .y = 255, .width = 54, .height = 30}}));

    TapKey(input, Key::Escape);
    EXPECT_EQ(StepOnce(window, input), std::nullopt);
    EXPECT_FALSE(window.IsOpen());

    window.Open(0x5e);
    ClickAt(input, 344 + 10, 255 + 10);
    EXPECT_EQ(StepOnce(window, input), std::nullopt);
    EXPECT_FALSE(window.IsOpen());

    window.Open(0x5e);
    TapKey(input, Key::Enter);
    EXPECT_EQ(StepOnce(window, input), Some(0x5e));
    window.Open(0x5e);
    ClickAt(input, 242 + 10, 255 + 10);
    EXPECT_EQ(StepOnce(window, input), Some(0x5e));
}

// Type 4 (the 0x5f): the code box at (+32, +76), OK at (+209, +72) and cancel at (+264, +72). The field takes only
// digits, up to 7, is masked and is emptied on opening (0x6436c0).
TEST(NoticeWindowTest, TheCodeNoticeTakesUpToSevenDigits)
{
    NoticeWindow window(LabSizes(), nullptr);
    window.Open(0x5f);
    InputState input;
    EXPECT_EQ(StepOnce(window, input), std::nullopt);
    EXPECT_EQ(window.Layout().codeBox, (std::optional<Rect>{Rect{.x = 176, .y = 259, .width = 171, .height = 23}}));
    EXPECT_EQ(window.Layout().ok, (std::optional<Rect>{Rect{.x = 353, .y = 255, .width = 54, .height = 30}}));
    EXPECT_EQ(window.Layout().cancel, (std::optional<Rect>{Rect{.x = 408, .y = 255, .width = 54, .height = 30}}));
    EXPECT_TRUE(window.WantsTextInput());

    Client::Tests::TypeText(input, "12a3 45-67890");
    EXPECT_EQ(StepOnce(window, input), std::nullopt);
    EXPECT_EQ(window.Code(), "1234567");

    TapKey(input, Key::Enter);
    EXPECT_EQ(StepOnce(window, input), Some(0x5f));
    // What was typed stays until another opens, for the request.
    EXPECT_EQ(window.Code(), "1234567");
    window.Open(0x5f);
    EXPECT_EQ(window.Code(), "");
    window.Open(0x5e);
    EXPECT_FALSE(window.WantsTextInput());
}

// In the box, at (+42, +84).
TEST(NoticeWindowTest, TheCodeIsDrawnMasked)
{
    NoticeWindow window(LabSizes(), nullptr);
    window.Open(0x5f);
    InputState input;
    Client::Tests::TypeText(input, "123");
    StepOnce(window, input);
    Client::Render::SpriteBatch batch;
    batch.Begin(Screen640);
    Client::Tests::RecordingTextDrawer text(batch);

    window.Draw(batch, text, Screen640);

    ASSERT_EQ(text.texts.size(), 1u);
    EXPECT_EQ(text.texts[0].text, "***");
    EXPECT_EQ(text.texts[0].x, 144 + 42);
    EXPECT_EQ(text.texts[0].y, 183 + 84);
}

// 0x643100 splits a lone text into two lines shorter than 52 bytes (0x632c90): the 1654 goes in two, on the left.
TEST(NoticeWindowTest, ALongTextSplitsInTwo)
{
    const auto texts = Client::Tests::MakeTextTable({{1654, "Voce nao pode apagar o personagem que pertence a Guild"}});
    NoticeWindow window(LabSizes(), &texts);
    window.Open(0x92);
    Client::Render::SpriteBatch batch;
    batch.Begin(Screen640);
    Client::Tests::RecordingTextDrawer text(batch);

    window.Draw(batch, text, Screen640);

    ASSERT_EQ(text.texts.size(), 2u);
    EXPECT_EQ(text.texts[0].text, "Voce nao pode apagar o personagem que pertence a ");
    EXPECT_EQ(text.texts[0].x, 169);
    EXPECT_EQ(text.texts[0].y, 215);
    EXPECT_EQ(text.texts[1].text, "Guild");
    EXPECT_EQ(text.texts[1].y, 234);
}

// The 0x5e builds its text with the chosen one's name (sprintf of the 1712).
TEST(NoticeWindowTest, TheConfirmationPlacesTheName)
{
    const auto texts = Client::Tests::MakeTextTable({{1712, "Delete %s?"}});
    NoticeWindow window(LabSizes(), &texts);
    window.Open(0x5e, "char0001");
    Client::Render::SpriteBatch batch;
    batch.Begin(Screen640);
    Client::Tests::RecordingTextDrawer text(batch);

    window.Draw(batch, text, Screen640);

    ASSERT_EQ(text.texts.size(), 1u);
    EXPECT_EQ(text.texts[0].text, "Delete char0001?");
}

// The code does not stay in the window: cancel erases it, and TakeCode returns and erases it (0x643740 empties the
// EDIT after copying it).
TEST(NoticeWindowTest, TheCodeIsForgottenAfterCancelOrTake)
{
    NoticeWindow window(LabSizes(), nullptr);
    InputState input;
    window.Open(0x5f);
    Client::Tests::TypeText(input, "123");
    static_cast<void>(StepOnce(window, input));
    TapKey(input, Key::Escape);
    static_cast<void>(StepOnce(window, input));
    EXPECT_EQ(window.Code(), "");

    window.Open(0x5f);
    Client::Tests::TypeText(input, "456");
    static_cast<void>(StepOnce(window, input));
    TapKey(input, Key::Enter);
    EXPECT_EQ(StepOnce(window, input), Some(0x5f));
    EXPECT_EQ(window.TakeCode(), "456");
    EXPECT_EQ(window.Code(), "");
}

// Only the 0x5e passes its text through sprintf (0x6431b0): in the others, a "%s" comes out as is.
TEST(NoticeWindowTest, OnlyTheConfirmationIsAFormat)
{
    const auto texts = Client::Tests::MakeTextTable({{1654, "Guild %s 100%"}});
    NoticeWindow window(LabSizes(), &texts);
    window.Open(0x92, "char0001");
    Client::Render::SpriteBatch batch;
    batch.Begin(Screen640);
    Client::Tests::RecordingTextDrawer text(batch);

    window.Draw(batch, text, Screen640);

    ASSERT_EQ(text.texts.size(), 1u);
    EXPECT_EQ(text.texts[0].text, "Guild %s 100%");
}
} // namespace
