#pragma once

#include "Session/Credentials.h"
#include "Core/PixelSize.h"
#include "Tables/TextTable.h"
#include "Input/InputState.h"
#include "Render/SpriteBatch.h"
#include "Render/Frame/ITextDrawer.h"
#include "Ui/Button.h"
#include "Ui/NewUi.h"
#include "Ui/TextField.h"
#include "Ui/Widget.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// The account window (CL5.5; 06 § El login): 0x6415c0 builds it, 0x641700 places it, 0x641990 draws it
// and 0x6418b0 handles it; the sender is 0x641bf0. The GameServer's F1 00 opens it (0x4c1e20). Two
// 10-byte fields, the account and the password, OK and cancel. A click on a field gives it the focus;
// Tab passes it to the other; OK or Enter sends; cancel or Esc cancels. What is typed, Tab and
// Backspace go in the order they arrived (InputState::Text), like the WM_CHAR to the focused EDIT.
// It is handled in the step (Q-C81). *Static*.
//
// What it sends never goes to the log: the request carries the account and the password, and its PrintTo
// in the tests (tests/support/UiPrinters.h) writes the lengths.
namespace Client::FrontEnd
{
enum class LoginFocus : std::uint8_t
{
    Account,
    Password,
};

// What the player asked for in a step. The window is already closed: the sender closes the top one before
// checking the fields (0x641bf0).
struct LoginRequest
{
    enum class Kind : std::uint8_t
    {
        // Both fields filled: the login.
        Submit,
        // Notice 0x64.
        EmptyAccount,
        // Notice 0x65.
        EmptyPassword,
        // Cancel or Esc: 0x642810 closes the GameServer and goes back to the list.
        Cancel,
    };

    Kind kind = Kind::Cancel;
    // Only with Submit.
    App::Credentials credentials;
};

// Where each thing goes, in pixels with y from the top.
struct LoginLayout
{
    Ui::Rect window;
    Ui::Rect account;
    Ui::Rect password;
    Ui::Rect ok;
    Ui::Rect cancel;
};

// 0x641700: 329 × 245 at ((W − 329) / 2, (H − 245) · 2 / 3); the 156 × 23 fields at (+109,
// +106) and (+109, +131); OK and cancel of 54 × 30 at (+150, +178) and (+211, +178).
[[nodiscard]] LoginLayout LayoutLogin(Core::PixelSize screen);

class LoginWindow final : public Ui::NewUiWindow
{
public:
    // `texts` can be nullptr (no labels) and has to outlive the window.
    LoginWindow(Ui::TextureSizes sizes, const Tables::TextTable* texts);

    // Like 0x643560: opens with the focus on `focus`. What the fields had stays.
    void Open(LoginFocus focus);
    // Closes from outside (the scene, when the session stops waiting for the account or leaves) and empties
    // the password: what was typed does not stay in the window. The account stays, like the original's EDIT.
    void Close() override;
    [[nodiscard]] bool IsOpen() const override;
    // The window (`window` of LayoutLogin).
    [[nodiscard]] Ui::Rect Bounds(Core::PixelSize screen) const override;
    [[nodiscard]] LoginFocus Focus() const;
    // The top title (0x641990): text 460 or 461 with the group and the number, already built.
    void SetTitle(std::string title);

    [[nodiscard]] const Ui::TextField& Account() const;
    [[nodiscard]] const Ui::TextField& Password() const;

    // 0x6418b0 in the step, with what was typed since the previous step. With Submit, the password is left
    // empty in the window; with Cancel, too; with EmptyAccount it stays, as the sender leaves it. Typing
    // goes in with or without the UI focus; clicks, Enter and Esc, only with it.
    std::optional<LoginRequest> Step(const Input::InputState& input, Core::PixelSize screen, Ui::NewUiTurn turn = {});
    // Sets the normal blend (Q-C80) and draws the background, the fields, the buttons, each field's content
    // (the password with asterisks), the labels and the title. The original's active control
    // (0x468320) composes the caret as pixels; it does not add the "_" glyph of the legacy path.
    // MuRet does not reproduce that rectangular caret yet: the focus keeps editing, without a visual cue.
    void Draw(Render::SpriteBatch& sprites, Render::Frame::ITextDrawer& text, Core::PixelSize screen);

private:
    [[nodiscard]] Ui::TextField& Focused();
    // What is typed, byte by byte: TabChar passes the focus, BackspaceChar deletes in the focused field and
    // the rest goes to that field (TextField drops what is not printable ASCII).
    void TypeInOrder(std::string_view typed);
    void Place(Core::PixelSize screen);
    LoginRequest Send();
    LoginRequest Cancel();
    void DrawField(Render::Frame::ITextDrawer& text, const Ui::TextField& field, const Ui::Rect& area,
                   Core::PixelSize screen) const;

    Ui::TextureSizes m_sizes;
    const Tables::TextTable* m_texts;
    bool m_open = false;
    LoginFocus m_focus = LoginFocus::Account;
    Ui::TextField m_account;
    Ui::TextField m_password;
    Ui::Button m_ok;
    Ui::Button m_cancel;
    std::string m_title;
    LoginLayout m_layout;
};
} // namespace Client::FrontEnd
