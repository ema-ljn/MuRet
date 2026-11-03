#include "UI/LoginWindow.h"

#include "Input/InputEvent.h"
#include "Input/Key.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "UI/GameText.h"

#include <cstddef>
#include <string_view>
#include <utility>

namespace Client::UI
{
namespace
{
// 0x6415c0: el fondo (login_back, slot 0x13), los campos (login_me, 0x14) y los botones de 0x61ccb0
// (message_ok_b_all, 0x724, y loding_cancel_b_all, 0x725), de tres cuadros.
constexpr Render::TextureSlot BackgroundSlot{0x13};
constexpr Render::TextureSlot FieldSlot{0x14};
constexpr Render::TextureSlot OkSlot{0x724};
constexpr Render::TextureSlot CancelSlot{0x725};
constexpr int Width = 329;
constexpr int Height = 245;
constexpr int FieldLeft = 109;
constexpr int AccountTop = 106;
constexpr int PasswordTop = 131;
constexpr int FieldWidth = 156;
constexpr int FieldHeight = 23;
constexpr int ButtonsTop = 178;
constexpr int OkLeft = 150;
constexpr int CancelLeft = 211;
constexpr int ButtonWidth = 54;
constexpr int ButtonHeight = 30;

// 0x641990: lo de cada campo en (+6, +6); los rótulos 450 y 451 en (+30, +113) y (+30, +139); el
// título en (+111, +80).
constexpr int FieldTextInset = 6;
constexpr std::size_t AccountLabelText = 450;
constexpr std::size_t PasswordLabelText = 451;
constexpr int LabelLeft = 30;
constexpr int AccountLabelTop = 113;
constexpr int PasswordLabelTop = 139;
constexpr int TitleLeft = 111;
constexpr int TitleTop = 80;

// 0x641a7a..0x641a91: los rótulos con DAT_05763560, en blanco y sin fondo. 0x553ae0: el campo en
// 0xffd2e6ff, sin fondo, con la fuente que haya: la de los textos de la escena, *inferido*.
constexpr Render::Text::TextStyle LabelStyle{Render::Text::FontKind::OtherFace12,
                                             Render::Text::TextColors{0xffffffff, 0}};
constexpr Render::Text::TextStyle FieldStyle{Render::Text::FontKind::Normal, Render::Text::TextColors{0xffd2e6ff, 0}};
// El cursor de 0x553ae0 (0x6c8f14), detrás de lo que tiene el campo del foco. El original lo
// prende un frame sí y uno no; MuRet lo deja prendido.
constexpr std::string_view Caret = "_";
} // namespace

LoginLayout LayoutLogin(App::PixelSize screen)
{
    const int x = (screen.width - Width) / 2;
    const int y = (screen.height - Height) * 2 / 3;
    return LoginLayout{
        .window = Rect{.x = x, .y = y, .width = Width, .height = Height},
        .account = Rect{.x = x + FieldLeft, .y = y + AccountTop, .width = FieldWidth, .height = FieldHeight},
        .password = Rect{.x = x + FieldLeft, .y = y + PasswordTop, .width = FieldWidth, .height = FieldHeight},
        .ok = Rect{.x = x + OkLeft, .y = y + ButtonsTop, .width = ButtonWidth, .height = ButtonHeight},
        .cancel = Rect{.x = x + CancelLeft, .y = y + ButtonsTop, .width = ButtonWidth, .height = ButtonHeight}};
}

LoginWindow::LoginWindow(TextureSizes sizes, const Data::Text::TextTable* texts)
    : m_sizes(std::move(sizes)), m_texts(texts), m_account(App::MaxCredentialBytes, false),
      m_password(App::MaxCredentialBytes, true), m_ok(OkSlot, ButtonWidth, ButtonHeight),
      m_cancel(CancelSlot, ButtonWidth, ButtonHeight)
{
}

void LoginWindow::Open(LoginFocus focus)
{
    m_open = true;
    m_focus = focus;
    m_ok = Button(OkSlot, ButtonWidth, ButtonHeight);
    m_cancel = Button(CancelSlot, ButtonWidth, ButtonHeight);
}

void LoginWindow::Close()
{
    m_open = false;
    m_password.Clear();
}

bool LoginWindow::IsOpen() const
{
    return m_open;
}

LoginFocus LoginWindow::Focus() const
{
    return m_focus;
}

void LoginWindow::SetTitle(std::string title)
{
    m_title = std::move(title);
}

const TextField& LoginWindow::Account() const
{
    return m_account;
}

const TextField& LoginWindow::Password() const
{
    return m_password;
}

std::optional<LoginRequest> LoginWindow::Step(const Input::InputState& input, App::PixelSize screen)
{
    if (!m_open)
    {
        return std::nullopt;
    }
    Place(screen);
    const Point mouse = MouseInPixels(input, screen);
    if (input.WasPressed(Input::MouseButton::Left))
    {
        if (m_layout.account.Contains(mouse))
        {
            m_focus = LoginFocus::Account;
        }
        else if (m_layout.password.Contains(mouse))
        {
            m_focus = LoginFocus::Password;
        }
    }
    TypeInOrder(input.Text());

    const bool ok = m_ok.Step(input, mouse);
    const bool cancel = m_cancel.Step(input, mouse);
    if (ok)
    {
        return Send();
    }
    if (cancel)
    {
        return Cancel();
    }
    // 0x6418b0: Enter y Esc, si el byte +0x31 del mouse (0x640850) está en 0. Solo lo escribe su
    // init, en 0 (0x640926): valen también con un botón del mouse abajo.
    if (input.WasPressed(Input::Key::Enter))
    {
        return Send();
    }
    if (input.WasPressed(Input::Key::Escape))
    {
        return Cancel();
    }
    return std::nullopt;
}

void LoginWindow::Draw(Render::SpriteBatch& sprites, Scenes::ITextDrawer& text, App::PixelSize screen)
{
    if (!m_open)
    {
        return;
    }
    Place(screen);
    sprites.SetBlend(Render::BlendMode::Alpha);
    sprites.Image(PixelImage(BackgroundSlot, m_layout.window, m_sizes(BackgroundSlot)));
    sprites.Image(PixelImage(FieldSlot, m_layout.account, m_sizes(FieldSlot)));
    sprites.Image(PixelImage(FieldSlot, m_layout.password, m_sizes(FieldSlot)));
    sprites.Image(m_ok.Image(m_sizes));
    sprites.Image(m_cancel.Image(m_sizes));

    DrawField(text, m_account, m_layout.account, m_focus == LoginFocus::Account, screen);
    DrawField(text, m_password, m_layout.password, m_focus == LoginFocus::Password, screen);

    const Rect& window = m_layout.window;
    const int labelX = Render::Text::DesignX(window.x + LabelLeft, screen);
    if (const auto account = TextAt(m_texts, AccountLabelText); account && !account->empty())
    {
        text.Draw(labelX, Render::Text::DesignY(window.y + AccountLabelTop, screen), *account, LabelStyle);
    }
    if (const auto password = TextAt(m_texts, PasswordLabelText); password && !password->empty())
    {
        text.Draw(labelX, Render::Text::DesignY(window.y + PasswordLabelTop, screen), *password, LabelStyle);
    }
    if (!m_title.empty())
    {
        text.Draw(Render::Text::DesignX(window.x + TitleLeft, screen),
                  Render::Text::DesignY(window.y + TitleTop, screen), m_title, LabelStyle);
    }
}

TextField& LoginWindow::Focused()
{
    return m_focus == LoginFocus::Account ? m_account : m_password;
}

void LoginWindow::TypeInOrder(std::string_view typed)
{
    for (const char byte : typed)
    {
        if (byte == Input::TabChar)
        {
            m_focus = m_focus == LoginFocus::Account ? LoginFocus::Password : LoginFocus::Account;
        }
        else if (byte == Input::BackspaceChar)
        {
            Focused().Backspace();
        }
        else
        {
            Focused().Type(std::string_view(&byte, 1));
        }
    }
}

void LoginWindow::Place(App::PixelSize screen)
{
    m_layout = LayoutLogin(screen);
    m_ok.MoveTo(Point{m_layout.ok.x, m_layout.ok.y});
    m_cancel.MoveTo(Point{m_layout.cancel.x, m_layout.cancel.y});
}

LoginRequest LoginWindow::Send()
{
    // 0x641bf0: cierra la ventana de arriba y mira los campos, sin tocarlos: con la cuenta vacía,
    // la clave queda para cuando el aviso abra otra vez la cuenta.
    m_open = false;
    if (m_account.Size() == 0)
    {
        return LoginRequest{.kind = LoginRequest::Kind::EmptyAccount, .credentials = {}};
    }
    if (m_password.Size() == 0)
    {
        return LoginRequest{.kind = LoginRequest::Kind::EmptyPassword, .credentials = {}};
    }
    LoginRequest request{.kind = LoginRequest::Kind::Submit,
                         .credentials =
                             App::Credentials{std::string(m_account.Value()), std::string(m_password.Value())}};
    m_password.Clear();
    return request;
}

LoginRequest LoginWindow::Cancel()
{
    // Close vacía la clave.
    Close();
    return LoginRequest{.kind = LoginRequest::Kind::Cancel, .credentials = {}};
}

void LoginWindow::DrawField(Scenes::ITextDrawer& text, const TextField& field, const Rect& area, bool focused,
                            App::PixelSize screen) const
{
    const int x = Render::Text::DesignX(area.x + FieldTextInset, screen);
    const int y = Render::Text::DesignY(area.y + FieldTextInset, screen);
    const std::string shown = field.Shown();
    if (!shown.empty())
    {
        text.Draw(x, y, shown, FieldStyle);
    }
    if (focused)
    {
        // 0x553ae0 lo pone en la x del campo más el ancho de lo que tiene, en 640 × 480.
        const int width = shown.empty() ? 0 : text.Measure(FieldStyle.font, shown).width;
        text.Draw(x + Render::Text::DesignX(width, screen), y, Caret, FieldStyle);
    }
}
} // namespace Client::UI
