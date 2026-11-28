#pragma once

#include "Core/PixelSize.h"
#include "Tables/TextTable.h"
#include "Input/InputState.h"
#include "Render/SpriteBatch.h"
#include "Render/Frame/ITextDrawer.h"
#include "Ui/Button.h"
#include "Ui/NewUi.h"
#include "Ui/TextField.h"
#include "Ui/Widget.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

// The selection's create-character window (+0x8498: 0x63d1a0 builds it, 0x63d4e0 places it,
// 0x63d7c0 changes the class, 0x63d880 handles it and 0x63e0a0 draws it; 06 § La ventana de crear).
// Modal, with the full-screen base: the five classes, the name, the class's attributes and
// description, OK and cancel. The name checks belong to the sender (Ui::NoticeOfNewName). It is
// handled in the step (Q-C81). *Static*, and the place *emulated* on four screens.
namespace Client::FrontEnd
{
inline constexpr std::size_t CreatableClassCount = 5;
// What GetWindowTextA copies into 0x7798bf8 (limit 0xff, with its zero).
inline constexpr std::size_t NameBufferSize = 0x100;

struct CreateWindowLayout
{
    Ui::Rect box;        // 454 × 406, centered (not drawn)
    Ui::Rect nameBack;   // cha_id.tga, 346 × 38, at (x, y + 317)
    Ui::Rect attributes; // black with alpha 0x8f, 108 × 80 (96 with the Dark Lord), at (x + 346, y + 50)
    Ui::Rect strip;      // black with alpha 0x8f, 454 × 51, at (x, y + 355)
    std::array<Ui::Rect, CreatableClassCount> classes; // cha_bt.tga, 108 × 26
    Ui::Rect ok;                                       // 54 × 30, at (x + 346, y + 325)
    Ui::Rect cancel;                                   // 54 × 30, at (x + 400, y + 325)
    // Where the name field's text goes, in 640 × 480: (ftol((x + 78) / rx), ftol((y + 338) / ry)).
    Ui::Point nameField;
};

// 0x63d4e0 with ((W − 454) / 2, (H − 406) / 2), C's division; the attributes with the height of
// class `characterClass`.
[[nodiscard]] CreateWindowLayout LayoutCreateWindow(Core::PixelSize screen, std::uint8_t characterClass);

enum class CreateWindowEvent : std::uint8_t
{
    Submitted, // OK or Enter: the sender checks the name and, if it passes, closes the window (0x63d990)
    Cancelled, // cancel or Esc: the window is already closed
};

class CreateCharacterWindow final : public Ui::NewUiWindow
{
public:
    // `texts` can be nullptr and has to outlive the window. The build (0x63d1a0) picks the
    // Dark Knight (+0x890 at 1) and enables all classes until the list arrives.
    CreateCharacterWindow(Ui::TextureSizes sizes, const Tables::TextTable* texts);

    // 0x63d7c0: enables classes 0 to `creatable` (pkt[4] of the list); with 5 or more, the five
    // (the original writes outside the buttons: Q-C119).
    void EnableClasses(std::uint8_t creatable);

    // 0x63d670(1): the field empty. The class stays as it was.
    void Open();
    // 0x63d670(0): the field empty.
    void Close() override;
    [[nodiscard]] bool IsOpen() const override;
    // Full-screen, like the base (0x669c70(W, H, −1, 0)): it has the mouse on all of it.
    [[nodiscard]] Ui::Rect Bounds(Core::PixelSize screen) const override;

    // 0 to 4 (+0x890).
    [[nodiscard]] std::uint8_t ChosenClass() const;
    // The field's name. Valid until the next change; it does not go to the log.
    [[nodiscard]] std::string_view Name() const;
    // 0x7798bf8, the 256 bytes where OK copies the field with its zero (GetWindowTextA): behind it may
    // remain those of a previous longer attempt, because it is only emptied on open (0x553570). It is
    // what 0x447ca0 reads (Q-C118).
    [[nodiscard]] std::string_view Buffer() const;
    [[nodiscard]] const Ui::Button& ClassButton(std::size_t index) const;
    [[nodiscard]] const Ui::Button& OkButton() const;
    // With the window open, the field has the keyboard.
    [[nodiscard]] bool WantsTextInput() const;

    // 0x63d880: what is typed goes to the field; a click on an enabled class picks it; then OK, cancel,
    // Enter or Esc, in that order.
    // Without the focus (a notice on top, 0x65fc30): the buttons keep their state, without the mouse
    // over them and without a click; the EDIT keeps the Win32 focus (nobody takes it away), so what is
    // typed goes into the name.
    std::optional<CreateWindowEvent> Step(const Input::InputState& input, Core::PixelSize screen,
                                          Ui::NewUiTurn turn = {});

    // 0x66a050: the full-screen black base with alpha 0x80. The preview goes between this and
    // Draw, which is not darkened.
    void DrawBase(Render::SpriteBatch& sprites, Core::PixelSize screen) const;
    // 0x63e0a0 after the preview: the name background, the attributes box and the strip,
    // the seven buttons, the attributes, the description and the field.
    void Draw(Render::SpriteBatch& sprites, Render::Frame::ITextDrawer& text, Core::PixelSize screen);

private:
    void Place(Core::PixelSize screen);
    void Choose(std::uint8_t characterClass);
    void TypeInto(const Input::InputState& input);
    void CopyTheName();
    void DrawAttributes(Render::Frame::ITextDrawer& text, Core::PixelSize screen) const;
    void DrawDescription(Render::Frame::ITextDrawer& text, Core::PixelSize screen) const;
    void DrawTheName(Render::Frame::ITextDrawer& text, Core::PixelSize screen) const;

    Ui::TextureSizes m_sizes;
    const Tables::TextTable* m_texts;
    bool m_open = false;
    std::uint8_t m_class = 1;
    std::array<Ui::Button, CreatableClassCount> m_classes;
    Ui::Button m_ok;
    Ui::Button m_cancel;
    Ui::TextField m_name;
    std::array<char, NameBufferSize> m_buffer{};
    CreateWindowLayout m_layout{};
};
} // namespace Client::FrontEnd
