#pragma once

#include "Core/PixelSize.h"
#include "Input/InputState.h"
#include "Render/SpriteBatch.h"
#include "Ui/Button.h"
#include "Ui/NewUi.h"
#include "Ui/Widget.h"

#include <cstdint>
#include <optional>

// The login's bottom bar (+0xe40; 06 § Las ventanas de la UI nueva): 0x641000 builds it, 0x65f300 and 0x6410c0 place
// it, 0x641440 draws it and 0x6411b0 handles the clicks. A backgroundless W − 60 × 30 window with two 54 × 30 buttons
// of three frames: the menu one on the left and the credits one on the right, inside the deco.tga ornament. 0x4c1860
// opens it with the list and it stays open until the login is left. *Static*.
//
// The clicks show, but the scene does nothing with them yet: in the original the menu one opens the Esc window
// (+0x3d0) and the credits one sends C1 04 F4 06 to the CS and opens the +0x4578 one, both unread (P-C13). Handled in
// the step (Q-C81).
namespace Client::Ui
{
// In pixels, with the y from the top.
struct MenuBarLayout
{
    Rect bar;
    Rect menu;
    Rect credit;
    // Already shifted by its point (105, 59).
    Rect ornament;
};

// 0x65f300: the bar at (30, −11 − (int) (H · −0.945f) − 30), with the product at x87 precision and truncated toward
// 0, and W − 60 wide. 0x6410c0: the menu at its corner, the credits at (x + width − 54, y) and the ornament with its
// point at the credits' corner.
[[nodiscard]] MenuBarLayout LayoutMenuBar(Core::PixelSize screen);

// +0xea or +0x1ca, 0x6411b0.
enum class MenuBarClick : std::uint8_t
{
    Menu,
    Credit,
};

class MenuBar final : public NewUiWindow
{
public:
    explicit MenuBar(TextureSizes sizes);

    // 0x65f690: one already open stays as it was.
    void Open();
    void Close() override;
    [[nodiscard]] bool IsOpen() const override;
    // W − 60 × 30 (LayoutMenuBar's `bar`).
    [[nodiscard]] Rect Bounds(Core::PixelSize screen) const override;

    // 0x63c490 on both buttons and, with the focus, 0x6411b0: the button that got a click, the menu first.
    std::optional<MenuBarClick> Step(const Input::InputState& input, Core::PixelSize screen, NewUiTurn turn = {});
    // 0x641440 with the normal blend (Q-C80): the ornament and then both buttons, without labels.
    void Draw(Render::SpriteBatch& sprites, Core::PixelSize screen);

private:
    void Place(Core::PixelSize screen);

    TextureSizes m_sizes;
    bool m_open = false;
    Button m_menu;
    Button m_credit;
    MenuBarLayout m_layout;
};
} // namespace Client::Ui
