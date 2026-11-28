#pragma once

#include "Core/PixelSize.h"
#include "Tables/TextTable.h"
#include "Input/InputState.h"
#include "Render/SpriteBatch.h"
#include "Render/Frame/ITextDrawer.h"
#include "Ui/Button.h"
#include "Ui/NewUi.h"
#include "Ui/Notices.h"
#include "Ui/TextField.h"
#include "Ui/Widget.h"

#include <optional>
#include <string>
#include <string_view>

// The new UI's notice (CL5.5 and CL6.5; 06 § Los avisos and "Los avisos de la selección"): 0x6429b0 and 0x642bf0
// build it, 0x642f00 draws it and 0x642e30 handles it. It darkens the screen with a black base of alpha 0x80 and shows
// its box with the notice's text (Notices.h), split into two rows under 52 bytes (0x643100). The buttons go by its
// kind (KindOf): none, the OK, the OK and the cancel, or those two with the deletion code field, which takes up to 7
// masked digits and empties on opening (0x6436c0). Handled in the step (Q-C81). *Static*.
//
// Not called MessageBox: <windows.h> defines a macro with that name.
namespace Client::Ui
{
struct NoticeLayout
{
    Rect box;
    Rect ok;
};

// 0x6429b0: the 352 × 113 box at ((W − 352) / 2, (H − 113) / 2), and the 54 × 30 OK at (+149, +72), kind 2's.
[[nodiscard]] NoticeLayout LayoutNotice(Core::PixelSize screen);

// What the open notice has by its kind (0x642bf0), in pixels: the box, the 54 × 30 OK and cancel, and the 171 × 23
// code box (delete_secret_number, slot 0x728).
struct NoticeControls
{
    Rect box;
    std::optional<Rect> ok;
    std::optional<Rect> cancel;
    std::optional<Rect> codeBox;
};

class NoticeWindow final : public NewUiWindow
{
public:
    // `texts` may be nullptr (the box without text) and must live as long as the window.
    NoticeWindow(TextureSizes sizes, const Tables::TextTable* texts);

    // 0x65ee60. An open one is replaced. `argument` goes in the "%s" of the 0x5e's text, the only one 0x6431b0 builds
    // with sprintf (the chosen one's name); the others do not use it. The code empties.
    void Open(NoticeId notice, std::string_view argument = {});
    void Close() override;
    [[nodiscard]] bool IsOpen() const override;
    // The whole screen: 0x6429b0 builds it W × H, and its vtable + 8 moves only the box.
    [[nodiscard]] Rect Bounds(Core::PixelSize screen) const override;
    [[nodiscard]] std::optional<NoticeId> Notice() const;

    // 0x642e30 in the step: with OK or Enter (and Esc in kind 2) it closes and returns the notice that closed, for what
    // follows (FollowUpOf); with cancel or Esc in kinds 3 and 4 it closes without returning it; kind 0 does not close.
    // In kind 4, what is typed goes to the code field: digits only. The buttons, Enter and Esc, with the focus.
    std::optional<NoticeId> Step(const Input::InputState& input, Core::PixelSize screen, NewUiTurn turn = {});

    // The open notice's, on the screen of the last step or draw.
    [[nodiscard]] const NoticeControls& Layout() const;

    // Until it is cancelled or another notice opens.
    [[nodiscard]] std::string_view Code() const;
    // The digits, leaving the field empty: 0x643740 copies the EDIT after the OK and empties it.
    [[nodiscard]] std::string TakeCode();

    // With the code field open.
    [[nodiscard]] bool WantsTextInput() const;
    // The base, the box, the OK and the text, with the normal blend (Q-C80).
    void Draw(Render::SpriteBatch& sprites, Render::Frame::ITextDrawer& text, Core::PixelSize screen);

private:
    void Place(Core::PixelSize screen);

    TextureSizes m_sizes;
    const Tables::TextTable* m_texts;
    std::optional<NoticeId> m_notice;
    std::string m_argument;
    Button m_ok;
    Button m_cancel;
    TextField m_code;
    NoticeControls m_layout;
};
} // namespace Client::Ui
