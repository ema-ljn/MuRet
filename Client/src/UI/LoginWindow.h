#pragma once

#include "App/Credentials.h"
#include "App/WindowState.h"
#include "Data/Text/TextTable.h"
#include "Input/InputState.h"
#include "Render/SpriteBatch.h"
#include "Scenes/ITextDrawer.h"
#include "UI/Button.h"
#include "UI/NewUi.h"
#include "UI/TextField.h"
#include "UI/Widget.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// La ventana de la cuenta (CL5.5; 06 § El login): la arma 0x6415c0, la ubica 0x641700, la dibuja
// 0x641990 y la atiende 0x6418b0; el emisor es 0x641bf0. La abre el F1 00 del GameServer
// (0x4c1e20). Dos campos de 10 bytes, la cuenta y la clave, OK y cancelar. Un clic en un campo le
// da el foco; Tab lo pasa al otro; OK o Enter manda; cancelar o Esc cancela. Lo tecleado, Tab y
// Backspace van en el orden en que llegaron (InputState::Text), como los WM_CHAR al EDIT del foco.
// Se atiende en el paso (Q-C81). *Estático*.
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

class LoginWindow final : public NewUiWindow
{
public:
    // `texts` puede ser nullptr (sin rótulos) y tiene que vivir lo que la ventana.
    LoginWindow(TextureSizes sizes, const Data::Text::TextTable* texts);

    // Como 0x643560: abre con el foco en `focus`. Lo que tenían los campos queda.
    void Open(LoginFocus focus);
    // Cierra desde afuera (la escena, cuando la sesión deja de esperar la cuenta o se va) y vacía
    // la clave: lo tecleado no queda en la ventana. La cuenta queda, como el EDIT del original.
    void Close() override;
    [[nodiscard]] bool IsOpen() const override;
    // La ventana (`window` de LayoutLogin).
    [[nodiscard]] Rect Bounds(App::PixelSize screen) const override;
    [[nodiscard]] LoginFocus Focus() const;
    // El título de arriba (0x641990): el texto 460 o 461 con el grupo y el número, ya armado.
    void SetTitle(std::string title);

    [[nodiscard]] const TextField& Account() const;
    [[nodiscard]] const TextField& Password() const;

    // 0x6418b0 en el paso, con lo tecleado desde el paso anterior. Con Submit, la clave queda
    // vacía en la ventana; con Cancel, también; con EmptyAccount queda, como el emisor la deja. Lo
    // tecleado entra con o sin el foco de la UI; los clics, Enter y Esc, solo con él.
    std::optional<LoginRequest> Step(const Input::InputState& input, App::PixelSize screen, NewUiTurn turn = {});
    // Pone la mezcla normal (Q-C80) y dibuja el fondo, los campos, los botones, lo que tiene cada
    // campo (la clave con asteriscos), los rótulos y el título. El control activo del original
    // (0x468320) compone el caret como píxeles; no agrega el glifo "_" del camino legacy.
    // MuRet todavía no reproduce ese caret rectangular: el foco conserva la edición, sin señal visual.
    void Draw(Render::SpriteBatch& sprites, Scenes::ITextDrawer& text, App::PixelSize screen);

private:
    [[nodiscard]] TextField& Focused();
    // Lo tecleado, byte por byte: TabChar pasa el foco, BackspaceChar borra en el campo del foco y
    // lo demás va a ese campo (TextField descarta lo que no es ASCII imprimible).
    void TypeInOrder(std::string_view typed);
    void Place(App::PixelSize screen);
    LoginRequest Send();
    LoginRequest Cancel();
    void DrawField(Scenes::ITextDrawer& text, const TextField& field, const Rect& area, App::PixelSize screen) const;

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
