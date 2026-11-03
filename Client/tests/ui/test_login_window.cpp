#include "UI/LoginWindow.h"

#include "App/WindowState.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/FontSet.h"
#include "UI/Widget.h"
#include "support/AppPrinters.h"
#include "support/RecordingTextDrawer.h"
#include "support/SceneDataFiles.h"
#include "support/UiInput.h"
#include "support/UiPrinters.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

// Client::UI::LoginWindow: la ventana de la cuenta (0x6415c0, 0x641700, 0x641990 y 0x6418b0, en el
// paso; el emisor 0x641bf0; CL5.5). Dos campos de 10 bytes, la clave con asteriscos, OK y cancelar;
// Enter u OK manda, Esc o cancelar cancela, Tab cambia de campo. Solo la cuenta y la clave
// sintéticas de los fixtures.
namespace
{
using Client::App::PixelSize;
using Client::Input::InputState;
using Client::Input::Key;
using Client::Tests::ClickAt;
using Client::Tests::PressLeft;
using Client::Tests::TapKey;
using Client::Tests::TypeText;
using Client::UI::LayoutLogin;
using Client::UI::LoginFocus;
using Client::UI::LoginLayout;
using Client::UI::LoginRequest;
using Client::UI::LoginWindow;
using Client::UI::Rect;

constexpr PixelSize Screen640{640, 480};
constexpr PixelSize Screen1024{1024, 768};

Client::UI::TextureSizes LabSizes()
{
    return Client::Tests::FakeTextureSizes(
        {{0x13, {512, 256}}, {0x14, {256, 32}}, {0x724, {64, 128}}, {0x725, {64, 128}}});
}

// Un paso con lo que haya en `input`; los flancos y el texto se borran después.
std::optional<LoginRequest> StepOnce(LoginWindow& window, InputState& input, PixelSize screen = Screen640)
{
    std::optional<LoginRequest> request = window.Step(input, screen);
    input.ConsumeEdges();
    return request;
}

// La cuenta y la clave sintéticas, tecleadas con Tab en el medio.
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

// 0x6415c0: 329 × 245 en ((W − 329) / 2, (H − 245) · 2 / 3); los campos de 156 × 23 en (+109, +106)
// y (+109, +131); OK y cancelar de 54 × 30 en (+150, +178) y (+211, +178).
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

// 0x643560 abre la ventana con el foco en la clave después de una clave mala.
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

// 0x6418b0: un clic en un campo le da el foco (DAT_077f3b70).
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

// Lo tecleado en un paso va en el orden en que llegó, como los WM_CHAR al EDIT del foco: lo de
// antes de un Tab queda en la cuenta y lo de después en la clave, aunque lleguen juntos (una
// ráfaga, o el auto-type de un gestor de claves). Y el Enter del mismo paso manda los dos.
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

// Un Backspace antes de lo tecleado, en el mismo paso, borra antes de sumar.
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

// Backspace sostenido borra uno por cada repetición del teclado, que Platform manda como texto
// (InputEvents.h), como el EDIT de Windows con los WM_CHAR 8 que se repiten.
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
    // El emisor cierra la ventana de arriba antes de mandar (0x641bf0).
    EXPECT_FALSE(window.IsOpen());
}

// 0x641bf0: con la cuenta vacía, el aviso 0x64; con la clave vacía, el 0x65. No sale nada.
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

// Lo que se mandó no queda en la ventana: la clave se vacía, la cuenta queda para reintentar.
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

// 0x6418b0 lee Enter y Esc si el byte +0x31 del mouse (0x640850) está en 0, y nada lo prende: solo
// lo escribe, en 0, su init (0x640926). Con el botón del mouse abajo, Enter manda igual, y Esc
// cancela.
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

// Cancelar, con el botón o con Esc, vacía la clave; la cuenta queda.
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

// Cerrada desde afuera (la escena, cuando la sesión deja de esperar la cuenta o se va), la clave
// tecleada no queda en la ventana.
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

// Con la cuenta vacía, el emisor no toca los campos (0x641bf0): la clave queda para cuando el OK
// del aviso 0x64 abra otra vez la cuenta.
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

// Lo apretado antes de cerrar no cuenta al abrir otra vez: soltar encima del OK no es un clic.
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

// Cerrada no lee nada: lo tecleado no entra.
TEST(LoginWindowTest, AClosedWindowTakesNothing)
{
    LoginWindow window(LabSizes(), nullptr);
    InputState input;
    TypeText(input, "user0001");
    TapKey(input, Key::Enter);

    EXPECT_EQ(StepOnce(window, input), std::nullopt);
    EXPECT_EQ(window.Account().Value(), "");
}

// 0x641990 y 0x553ae0: la clave se dibuja con asteriscos, nunca con lo tecleado; los rótulos 450 y
// 451 en (+30, +113) y (+30, +139) y el título en (+111, +80), con la fuente DAT_05763560 en
// blanco; los campos en (+6, +6) con el color 0xffd2e6ff. Todo pasa a 640 × 480 con DesignX y
// DesignY: en 640 × 480, lo mismo.
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
    EXPECT_EQ(drawn, (std::vector<std::string>{"user0001", "********", "_", "Account", "Password", "Title"}));
    EXPECT_EQ(text.texts[0].x, 270);
    EXPECT_EQ(text.texts[0].y, 268);
    EXPECT_EQ(text.texts[0].style.colors.text, 0xffd2e6ffu);
    EXPECT_EQ(text.texts[1].y, 293);
    // El cursor, en el campo con el foco, detrás de lo que tiene: 8 asteriscos de 6 columnas.
    EXPECT_EQ(text.texts[2].x, 270 + 48);
    EXPECT_EQ(text.texts[2].y, 293);
    EXPECT_EQ(text.texts[3].x, 185);
    EXPECT_EQ(text.texts[3].y, 269);
    EXPECT_EQ(text.texts[3].style.font, Client::Render::Text::FontKind::OtherFace12);
    EXPECT_EQ(text.texts[3].style.colors.text, 0xffffffffu);
    EXPECT_EQ(text.texts[4].y, 295);
    EXPECT_EQ(text.texts[5].x, 266);
    EXPECT_EQ(text.texts[5].y, 236);
}

// El lote llega con la aditiva (Q-C80): la ventana pone la normal. El fondo, los dos campos y los
// dos botones son imágenes, antes de los textos.
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

// Lo que imprime GoogleTest de un pedido no trae la cuenta ni la clave (UiPrinters.h).
TEST(LoginWindowTest, ARequestPrintsNoCredentials)
{
    const LoginRequest request{.kind = LoginRequest::Kind::Submit, .credentials = {"user0001", "pass0002"}};

    const std::string printed = ::testing::PrintToString(request);

    EXPECT_EQ(printed.find("user0001"), std::string::npos);
    EXPECT_EQ(printed.find("pass0002"), std::string::npos);
}
} // namespace
