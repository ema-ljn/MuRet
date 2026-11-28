#include "FrontEnd/LoginWindow.h"

#include "Core/PixelSize.h"
#include "Input/InputEvent.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/FontSet.h"
#include "Render/TextureSlot.h"
#include "Ui/Widget.h"
#include "support/AppPrinters.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"
#include "support/UiInput.h"
#include "support/UiPrinters.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Client::FrontEnd::LoginWindow: the account window (0x6415c0, 0x641700, 0x641990 and 0x6418b0, in the step; the
// sender 0x641bf0; CL5.5). Two 10-byte fields, the password with asterisks, OK and cancel; Enter or OK sends, Esc or
// cancel cancels, Tab switches field. Only the fixtures' synthetic account and password.
namespace
{
using Client::Core::PixelSize;
using Client::FrontEnd::LayoutLogin;
using Client::FrontEnd::LoginFocus;
using Client::FrontEnd::LoginLayout;
using Client::FrontEnd::LoginRequest;
using Client::FrontEnd::LoginWindow;
using Client::Input::InputState;
using Client::Input::Key;
using Client::Tests::ClickAt;
using Client::Tests::PressLeft;
using Client::Tests::TapKey;
using Client::Tests::TypeText;
using Client::Ui::Rect;

constexpr PixelSize Screen640{640, 480};
constexpr PixelSize Screen1024{1024, 768};
// Two cycles of the original control's 24 calls, to also reject an intermittent glyph. It does not check the
// rectangular caret or a time-based blink.
constexpr int RepeatedDrawCount = 48;

Client::Ui::TextureSizes LabSizes()
{
    return Client::Tests::FakeTextureSizes(
        {{0x13, {512, 256}}, {0x14, {256, 32}}, {0x724, {64, 128}}, {0x725, {64, 128}}});
}

// A step with whatever is in `input`; the edges and the text are cleared afterwards.
std::optional<LoginRequest> StepOnce(LoginWindow& window, InputState& input, PixelSize screen = Screen640)
{
    std::optional<LoginRequest> request = window.Step(input, screen);
    input.ConsumeEdges();
    return request;
}

// The synthetic account and password, typed with Tab in between.
void TypeBoth(LoginWindow& window, InputState& input)
{
    TypeText(input, "user0001");
    StepOnce(window, input);
    TapKey(input, Key::Tab);
    StepOnce(window, input);
    TypeText(input, "pass0002");
    StepOnce(window, input);
}

int MiddleX(const Rect& rect)
{
    return rect.x + rect.width / 2;
}

int MiddleY(const Rect& rect)
{
    return rect.y + rect.height / 2;
}

// 0x6415c0: 329 × 245 at ((W − 329) / 2, (H − 245) · 2 / 3); the 156 × 23 fields at (+109, +106) and (+109, +131);
// OK and cancel, 54 × 30, at (+150, +178) and (+211, +178).
TEST(LoginWindowTest, ItIsPlacedInPixels)
{
    const LoginLayout small = LayoutLogin(Screen640);
    EXPECT_EQ(small.window, (Rect{.x = 155, .y = 156, .width = 329, .height = 245}));
    EXPECT_EQ(small.account, (Rect{.x = 264, .y = 262, .width = 156, .height = 23}));
    EXPECT_EQ(small.password, (Rect{.x = 264, .y = 287, .width = 156, .height = 23}));
    EXPECT_EQ(small.ok, (Rect{.x = 305, .y = 334, .width = 54, .height = 30}));
    EXPECT_EQ(small.cancel, (Rect{.x = 366, .y = 334, .width = 54, .height = 30}));

    const LoginLayout large = LayoutLogin(Screen1024);
    EXPECT_EQ(large.window, (Rect{.x = 347, .y = 348, .width = 329, .height = 245}));
}

TEST(LoginWindowTest, TabMovesTheFocus)
{
    LoginWindow window(LabSizes(), nullptr);
    window.Open(LoginFocus::Account);
    InputState input;

    TypeBoth(window, input);

    EXPECT_EQ(window.Account().Value(), "user0001");
    EXPECT_EQ(window.Password().Value(), "pass0002");
    EXPECT_EQ(window.Focus(), LoginFocus::Password);
    TapKey(input, Key::Tab);
    StepOnce(window, input);
    EXPECT_EQ(window.Focus(), LoginFocus::Account);
}

// 0x643560 opens the window with the focus on the password after a bad password.
TEST(LoginWindowTest, OpenSetsTheFocus)
{
    LoginWindow window(LabSizes(), nullptr);
    window.Open(LoginFocus::Password);
    InputState input;
    TypeText(input, "pass0002");
    StepOnce(window, input);

    EXPECT_EQ(window.Account().Value(), "");
    EXPECT_EQ(window.Password().Value(), "pass0002");
}

// 0x6418b0: a click on a field gives it the focus (DAT_077f3b70).
TEST(LoginWindowTest, AClickOnAFieldFocusesIt)
{
    LoginWindow window(LabSizes(), nullptr);
    window.Open(LoginFocus::Account);
    const LoginLayout layout = LayoutLogin(Screen640);
    InputState input;
    ClickAt(input, MiddleX(layout.password), MiddleY(layout.password));
    StepOnce(window, input);

    EXPECT_EQ(window.Focus(), LoginFocus::Password);
}

TEST(LoginWindowTest, BackspaceErasesInTheFocusedField)
{
    LoginWindow window(LabSizes(), nullptr);
    window.Open(LoginFocus::Account);
    InputState input;
    TypeBoth(window, input);

    TapKey(input, Key::Backspace);
    StepOnce(window, input);

    EXPECT_EQ(window.Account().Value(), "user0001");
    EXPECT_EQ(window.Password().Value(), "pass000");
}

// What is typed in a step goes in the order it arrived, like the WM_CHAR to the focused EDIT: what comes before a Tab
// stays in the account and what comes after in the password, even if they arrive together (a burst, or a password
// manager's auto-type). And the Enter of the same step sends both.
TEST(LoginWindowTest, WhatIsTypedAroundATabKeepsItsField)
{
    LoginWindow window(LabSizes(), nullptr);
    window.Open(LoginFocus::Account);
    InputState input;
    TypeText(input, "user0001");
    TapKey(input, Key::Tab);
    TypeText(input, "pass0002");
    TapKey(input, Key::Enter);

    const std::optional<LoginRequest> request = StepOnce(window, input);

    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->kind, LoginRequest::Kind::Submit);
    EXPECT_TRUE(request->credentials.account == "user0001");
    EXPECT_TRUE(request->credentials.password == "pass0002");
}

// A Backspace before the typed text, in the same step, erases before adding.
TEST(LoginWindowTest, ABackspaceBeforeTheTextErasesFirst)
{
    LoginWindow window(LabSizes(), nullptr);
    window.Open(LoginFocus::Account);
    InputState input;
    TypeText(input, "user0002");
    StepOnce(window, input);

    TapKey(input, Key::Backspace);
    TypeText(input, "1");
    StepOnce(window, input);

    EXPECT_EQ(window.Account().Value(), "user0001");
}

// A held Backspace erases one per keyboard repeat, which Platform sends as text (InputEvents.h), like the Windows
// EDIT with repeated WM_CHAR 8.
TEST(LoginWindowTest, AHeldBackspaceRepeats)
{
    LoginWindow window(LabSizes(), nullptr);
    window.Open(LoginFocus::Account);
    InputState input;
    TypeText(input, "user0001");
    StepOnce(window, input);

    Client::Tests::PressKey(input, Key::Backspace);
    TypeText(input, std::string(2, Client::Input::BackspaceChar));
    StepOnce(window, input);

    EXPECT_EQ(window.Account().Value(), "user0");
}

TEST(LoginWindowTest, EnterWithBothFieldsSubmits)
{
    LoginWindow window(LabSizes(), nullptr);
    window.Open(LoginFocus::Account);
    InputState input;
    TypeBoth(window, input);

    TapKey(input, Key::Enter);
    const std::optional<LoginRequest> request = StepOnce(window, input);

    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->kind, LoginRequest::Kind::Submit);
    EXPECT_EQ(request->credentials.account, "user0001");
    EXPECT_EQ(request->credentials.password, "pass0002");
    // The sender closes the top window before sending (0x641bf0).
    EXPECT_FALSE(window.IsOpen());
}

// 0x641bf0: with an empty account, notice 0x64; with an empty password, 0x65. Nothing goes out.
TEST(LoginWindowTest, EnterWithAnEmptyFieldDoesNotSubmit)
{
    LoginWindow window(LabSizes(), nullptr);
    window.Open(LoginFocus::Account);
    InputState input;
    TapKey(input, Key::Enter);
    const std::optional<LoginRequest> noAccount = StepOnce(window, input);
    ASSERT_TRUE(noAccount.has_value());
    EXPECT_EQ(noAccount->kind, LoginRequest::Kind::EmptyAccount);
    EXPECT_FALSE(window.IsOpen());

    window.Open(LoginFocus::Account);
    TypeText(input, "user0001");
    StepOnce(window, input);
    TapKey(input, Key::Enter);
    const std::optional<LoginRequest> noPassword = StepOnce(window, input);
    ASSERT_TRUE(noPassword.has_value());
    EXPECT_EQ(noPassword->kind, LoginRequest::Kind::EmptyPassword);
    EXPECT_TRUE(noPassword->credentials.password.empty());
}

// What was sent does not stay in the window: the password empties, the account stays for retrying.
TEST(LoginWindowTest, SubmittingEmptiesThePassword)
{
    LoginWindow window(LabSizes(), nullptr);
    window.Open(LoginFocus::Account);
    InputState input;
    TypeBoth(window, input);
    TapKey(input, Key::Enter);
    ASSERT_TRUE(StepOnce(window, input).has_value());

    EXPECT_EQ(window.Password().Value(), "");
    EXPECT_EQ(window.Account().Value(), "user0001");
}

// 0x6418b0 reads Enter and Esc if the mouse's byte +0x31 (0x640850) is 0, and nothing turns it on: only its init
// (0x640926) writes it, with 0. With the mouse button down, Enter sends anyway, and Esc cancels.
TEST(LoginWindowTest, EnterAndEscWorkWithTheMouseHeld)
{
    LoginWindow window(LabSizes(), nullptr);
    window.Open(LoginFocus::Account);
    InputState input;
    TypeBoth(window, input);
    Client::Tests::MoveMouse(input, 5, 5);
    PressLeft(input);
    StepOnce(window, input);

    TapKey(input, Key::Enter);
    const std::optional<LoginRequest> sent = StepOnce(window, input);
    ASSERT_TRUE(sent.has_value());
    EXPECT_EQ(sent->kind, LoginRequest::Kind::Submit);

    window.Open(LoginFocus::Account);
    TapKey(input, Key::Escape);
    const std::optional<LoginRequest> cancelled = StepOnce(window, input);
    ASSERT_TRUE(cancelled.has_value());
    EXPECT_EQ(cancelled->kind, LoginRequest::Kind::Cancel);
}

// Cancelling, with the button or with Esc, empties the password; the account stays.
TEST(LoginWindowTest, CancellingEmptiesThePassword)
{
    const LoginLayout layout = LayoutLogin(Screen640);
    LoginWindow window(LabSizes(), nullptr);
    window.Open(LoginFocus::Account);
    InputState input;
    TypeBoth(window, input);
    ClickAt(input, MiddleX(layout.cancel), MiddleY(layout.cancel));
    ASSERT_TRUE(StepOnce(window, input).has_value());
    EXPECT_EQ(window.Password().Size(), 0u);
    EXPECT_EQ(window.Account().Value(), "user0001");

    window.Open(LoginFocus::Password);
    TypeText(input, "pass0002");
    StepOnce(window, input);
    TapKey(input, Key::Escape);
    ASSERT_TRUE(StepOnce(window, input).has_value());
    EXPECT_EQ(window.Password().Size(), 0u);
}

// Closed from outside (by the scene, when the session stops waiting for the account or leaves), the typed password
// does not stay in the window.
TEST(LoginWindowTest, ClosingItEmptiesThePassword)
{
    LoginWindow window(LabSizes(), nullptr);
    window.Open(LoginFocus::Account);
    InputState input;
    TypeBoth(window, input);

    window.Close();

    EXPECT_FALSE(window.IsOpen());
    EXPECT_EQ(window.Password().Size(), 0u);
    EXPECT_EQ(window.Account().Value(), "user0001");
}

// With an empty account, the sender does not touch the fields (0x641bf0): the password stays for when the OK of
// notice 0x64 opens the account again.
TEST(LoginWindowTest, AnEmptyAccountKeepsThePassword)
{
    LoginWindow window(LabSizes(), nullptr);
    window.Open(LoginFocus::Password);
    InputState input;
    TypeText(input, "pass0002");
    TapKey(input, Key::Enter);

    const std::optional<LoginRequest> request = StepOnce(window, input);

    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->kind, LoginRequest::Kind::EmptyAccount);
    EXPECT_EQ(window.Password().Size(), 8u);
}

// What was pressed before closing does not count on reopening: releasing over OK is not a click.
TEST(LoginWindowTest, ReopeningForgetsAPress)
{
    const LoginLayout layout = LayoutLogin(Screen640);
    LoginWindow window(LabSizes(), nullptr);
    window.Open(LoginFocus::Account);
    InputState input;
    TypeBoth(window, input);
    Client::Tests::MoveMouse(input, MiddleX(layout.ok), MiddleY(layout.ok));
    PressLeft(input);
    StepOnce(window, input);
    window.Close();
    window.Open(LoginFocus::Account);

    Client::Tests::ReleaseLeft(input);

    EXPECT_EQ(StepOnce(window, input), std::nullopt);
    EXPECT_TRUE(window.IsOpen());
}

TEST(LoginWindowTest, OkSubmitsAndCancelCancels)
{
    const LoginLayout layout = LayoutLogin(Screen640);
    LoginWindow window(LabSizes(), nullptr);
    window.Open(LoginFocus::Account);
    InputState input;
    TypeBoth(window, input);
    ClickAt(input, MiddleX(layout.ok), MiddleY(layout.ok));
    const std::optional<LoginRequest> ok = StepOnce(window, input);
    ASSERT_TRUE(ok.has_value());
    EXPECT_EQ(ok->kind, LoginRequest::Kind::Submit);

    window.Open(LoginFocus::Account);
    ClickAt(input, MiddleX(layout.cancel), MiddleY(layout.cancel));
    const std::optional<LoginRequest> cancel = StepOnce(window, input);
    ASSERT_TRUE(cancel.has_value());
    EXPECT_EQ(cancel->kind, LoginRequest::Kind::Cancel);
    EXPECT_FALSE(window.IsOpen());
}

TEST(LoginWindowTest, EscCancels)
{
    LoginWindow window(LabSizes(), nullptr);
    window.Open(LoginFocus::Account);
    InputState input;
    TapKey(input, Key::Escape);

    const std::optional<LoginRequest> cancel = StepOnce(window, input);

    ASSERT_TRUE(cancel.has_value());
    EXPECT_EQ(cancel->kind, LoginRequest::Kind::Cancel);
}

TEST(LoginWindowTest, AClosedWindowTakesNothing)
{
    LoginWindow window(LabSizes(), nullptr);
    InputState input;
    TypeText(input, "user0001");
    TapKey(input, Key::Enter);

    EXPECT_EQ(StepOnce(window, input), std::nullopt);
    EXPECT_EQ(window.Account().Value(), "");
}

// 0x641990: the password is drawn with asterisks, never with what was typed; labels 450 and 451 at (+30, +113) and
// (+30, +139) and the title at (+111, +80), with the font DAT_05763560 in white. The inset (+6, +6) and the color
// 0xffd2e6ff keep MuRet's previous presentation, pending a check against the active EDIT's configuration; this test
// does not test visual equality. Everything goes to 640 × 480 with DesignX and DesignY: at 640 × 480, the same.
TEST(LoginWindowTest, ThePasswordIsDrawnAsStars)
{
    const auto texts = Client::Tests::MakeTextTable({{450, "Account"}, {451, "Password"}});
    LoginWindow window(LabSizes(), &texts);
    window.Open(LoginFocus::Account);
    window.SetTitle("Title");
    InputState input;
    TypeBoth(window, input);
    Client::Render::SpriteBatch batch;
    batch.Begin(Screen640);
    Client::Tests::RecordingTextDrawer text(batch);

    window.Draw(batch, text, Screen640);

    std::vector<std::string> drawn;
    for (const auto& recorded : text.texts)
    {
        drawn.push_back(recorded.text);
        EXPECT_EQ(recorded.text.find("pass0002"), std::string::npos);
    }
    ASSERT_EQ(drawn, (std::vector<std::string>{"user0001", "********", "Account", "Password", "Title"}));
    EXPECT_EQ(text.texts[0].x, 270);
    EXPECT_EQ(text.texts[0].y, 268);
    EXPECT_EQ(text.texts[0].style.colors.text, 0xffd2e6ffu);
    EXPECT_EQ(text.texts[1].y, 293);
    EXPECT_EQ(text.texts[2].x, 185);
    EXPECT_EQ(text.texts[2].y, 269);
    EXPECT_EQ(text.texts[2].style.font, Client::Render::Text::FontKind::OtherFace12);
    EXPECT_EQ(text.texts[2].style.colors.text, 0xffffffffu);
    EXPECT_EQ(text.texts[3].y, 295);
    EXPECT_EQ(text.texts[4].x, 266);
    EXPECT_EQ(text.texts[4].y, 236);
}

// 0x641990's active selector calls 0x469350 → 0x468320: its caret is a rectangular region of the bitmap, never the
// "_" glyph of the legacy path 0x553ae0.
TEST(LoginWindowTest, EmptyFieldsDoNotEmitACaretGlyph)
{
    for (const auto focus : {LoginFocus::Account, LoginFocus::Password})
    {
        LoginWindow window(LabSizes(), nullptr);
        window.Open(focus);
        for (int draw = 0; draw < RepeatedDrawCount; ++draw)
        {
            Client::Render::SpriteBatch batch;
            batch.Begin(Screen640);
            Client::Tests::RecordingTextDrawer text(batch);

            window.Draw(batch, text, Screen640);

            ASSERT_TRUE(text.texts.empty());
        }
    }
}

TEST(LoginWindowTest, EditingPreservesLiteralUnderscoresWithoutAddingACaretGlyph)
{
    for (const std::string_view account : {"user_01", "user_", "_"})
    {
        LoginWindow window(LabSizes(), nullptr);
        window.Open(LoginFocus::Account);
        InputState input;
        TypeText(input, std::string(account));
        StepOnce(window, input);
        TapKey(input, Key::Tab);
        StepOnce(window, input);
        TypeText(input, "_");
        StepOnce(window, input);

        for (const auto focus : {LoginFocus::Password, LoginFocus::Account})
        {
            ASSERT_EQ(window.Focus(), focus);
            for (int draw = 0; draw < RepeatedDrawCount; ++draw)
            {
                Client::Render::SpriteBatch batch;
                batch.Begin(Screen640);
                Client::Tests::RecordingTextDrawer text(batch);

                window.Draw(batch, text, Screen640);

                ASSERT_EQ(text.texts.size(), 2u);
                EXPECT_EQ(text.texts[0].text, account);
                EXPECT_EQ(text.texts[1].text, "*");
            }
            TapKey(input, Key::Tab);
            StepOnce(window, input);
        }
    }
}

// The batch arrives with the additive blend (Q-C80): the window sets the normal one. The background, the two fields
// and the two buttons are images, before the texts.
TEST(LoginWindowTest, ItDrawsWithTheNormalBlend)
{
    LoginWindow window(LabSizes(), nullptr);
    window.Open(LoginFocus::Account);
    Client::Render::SpriteBatch batch;
    batch.Begin(Screen640);
    batch.SetBlend(Client::Render::BlendMode::Additive);
    Client::Tests::RecordingTextDrawer text(batch);

    window.Draw(batch, text, Screen640);

    ASSERT_FALSE(batch.Commands().empty());
    EXPECT_EQ(batch.Commands().front().blend, Client::Render::BlendMode::Alpha);
    ASSERT_GE(batch.Commands().size(), 4u);
    EXPECT_EQ(batch.Commands()[0].texture, Client::Render::TextureSlot{0x13});
    EXPECT_EQ(batch.Commands()[1].texture, Client::Render::TextureSlot{0x14});
    EXPECT_EQ(batch.Commands()[2].texture, Client::Render::TextureSlot{0x724});
    EXPECT_EQ(batch.Commands()[3].texture, Client::Render::TextureSlot{0x725});
}

// Through UiPrinters.h.
TEST(LoginWindowTest, ARequestPrintsNoCredentials)
{
    const LoginRequest request{.kind = LoginRequest::Kind::Submit, .credentials = {"user0001", "pass0002"}};

    const std::string printed = ::testing::PrintToString(request);

    EXPECT_EQ(printed.find("user0001"), std::string::npos);
    EXPECT_EQ(printed.find("pass0002"), std::string::npos);
}
} // namespace
