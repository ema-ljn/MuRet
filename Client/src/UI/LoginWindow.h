#pragma once

#include "App/Credentials.h"
#include "App/WindowState.h"
#include "Data/Text/TextTable.h"
#include "Input/InputState.h"
#include "Render/SpriteBatch.h"
#include "Scenes/ITextDrawer.h"
#include "UI/Button.h"
#include "UI/TextField.h"
#include "UI/Widget.h"

#include <cstdint>
#include <optional>
#include <string>

// La ventana de la cuenta (CL5.5; 06 § El login): la arma 0x6415c0, la ubica 0x641700, la dibuja
// 0x641990 y la atiende 0x6418b0; el emisor es 0x641bf0. La abre el F1 00 del GameServer
// (0x4c1e20). Dos campos de 10 bytes, la cuenta y la clave, OK y cancelar. Un clic en un campo le
// da el foco; Tab lo pasa al otro; OK, o Enter con el mouse arriba, manda; cancelar, o Esc con el
// mouse arriba, cancela. Se atiende en el paso (Q-C81). *Estático*.
//
// Lo que manda nunca va al log: el pedido trae la cuenta y la clave, y su PrintTo de los tests
// (tests/support/UiPrinters.h) escribe los largos.
namespace Client::UI
{
enum class LoginFocus : std::uint8_t
{
    Account,
    Password,
};

// Lo que pidió el jugador en un paso. La ventana ya se cerró: el emisor cierra la de arriba antes
// de mirar los campos (0x641bf0).
struct LoginRequest
{
    enum class Kind : std::uint8_t
    {
        // Los dos campos con algo: el login.
        Submit,
        // La cuenta vacía: el aviso 0x64.
        EmptyAccount,
        // La clave vacía: el aviso 0x65.
        EmptyPassword,
        // Cancelar o Esc: 0x642810 cierra el GameServer y vuelve a la lista.
        Cancel,
    };

    Kind kind = Kind::Cancel;
    // Solo con Submit.
    App::Credentials credentials;
};

// Dónde va cada cosa, en píxeles con la y desde arriba.
struct LoginLayout
{
    Rect window;
    Rect account;
    Rect password;
    Rect ok;
    Rect cancel;
};

// 0x641700: 329 × 245 en ((W − 329) / 2, (H − 245) · 2 / 3); los campos de 156 × 23 en (+109,
// +106) y (+109, +131); OK y cancelar de 54 × 30 en (+150, +178) y (+211, +178).
[[nodiscard]] LoginLayout LayoutLogin(App::PixelSize screen);

class LoginWindow
{
public:
    // `texts` puede ser nullptr (sin rótulos) y tiene que vivir lo que la ventana.
    LoginWindow(TextureSizes sizes, const Data::Text::TextTable* texts);

    // Como 0x643560: abre con el foco en `focus`. Lo que tenían los campos queda.
    void Open(LoginFocus focus);
    void Close();
    [[nodiscard]] bool IsOpen() const;
    [[nodiscard]] LoginFocus Focus() const;
    // El título de arriba (0x641990): el texto 460 o 461 con el grupo y el número, ya armado.
    void SetTitle(std::string title);

    [[nodiscard]] const TextField& Account() const;
    [[nodiscard]] const TextField& Password() const;

    // 0x6418b0 en el paso, con lo tecleado desde el paso anterior. Con Submit, la clave queda
    // vacía en la ventana; con Cancel, también.
    std::optional<LoginRequest> Step(const Input::InputState& input, App::PixelSize screen);
    // Pone la mezcla normal (Q-C80) y dibuja el fondo, los campos, los botones, lo que tiene cada
    // campo (la clave con asteriscos) con el cursor en el del foco, los rótulos y el título.
    void Draw(Render::SpriteBatch& sprites, Scenes::ITextDrawer& text, App::PixelSize screen);

private:
    [[nodiscard]] TextField& Focused();
    void Place(App::PixelSize screen);
    LoginRequest Send();
    LoginRequest Cancel();
    void DrawField(Scenes::ITextDrawer& text, const TextField& field, const Rect& area, bool focused,
                   App::PixelSize screen) const;

    TextureSizes m_sizes;
    const Data::Text::TextTable* m_texts;
    bool m_open = false;
    LoginFocus m_focus = LoginFocus::Account;
    TextField m_account;
    TextField m_password;
    Button m_ok;
    Button m_cancel;
    std::string m_title;
    LoginLayout m_layout;
};
} // namespace Client::UI
