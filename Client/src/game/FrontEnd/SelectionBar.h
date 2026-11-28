#pragma once

#include "Core/PixelSize.h"
#include "Tables/TextTable.h"
#include "Input/InputState.h"
#include "Render/SpriteBatch.h"
#include "Render/Frame/ITextDrawer.h"
#include "Ui/Button.h"
#include "Ui/NewUi.h"
#include "Ui/Widget.h"

#include <cstdint>
#include <optional>
#include <string_view>

// The selection bar (window +0x7f50 of mode 4: 0x63e820 builds it, 0x63ec00 draws it and
// 0x63eb90 handles it; 06 § La selección de personaje, "La barra"). Not modal: it covers only its
// rectangle, and the characters are not picked under it. The text of the first 0D with 1 that arrives at the
// selection (+0x511) is kept by the game (WorldRuntime): the original's bar is built once. It is
// handled in the step (Q-C81). *Static*, and the place *emulated* on four screens.
namespace Client::FrontEnd
{
struct SelectionBarLayout
{
    Ui::Rect bar;   // (22, y), (W − 44) × 54
    Ui::Rect deco;  // deco.tga, 189 × 103, at (W − 189, y − 46)
    Ui::Rect strip; // black with alpha 0x8f, (W − 44) × 21, at (22, y + 33)
    Ui::Rect create;
    Ui::Rect menu;
    Ui::Rect connect;
    Ui::Rect remove;
};

// 0x65f4b0 with 0x63e9e0: x = 22 and y = −ftol(H · −0.945f) − 54, with the product on the x87; the
// 54 × 30 buttons at (22, y), (77, y), (W − 131, y) and (W − 76, y).
[[nodiscard]] SelectionBarLayout LayoutSelectionBar(Core::PixelSize screen);

// The click 0x63eb90 handles, one per step, in this order.
enum class SelectionBarClick : std::uint8_t
{
    Connect, // 0x633c10, the same as Enter
    Menu,    // opens the menu (+0x3d0)
    Create,  // opens the create window (+0x8498)
    Delete,  // 0x63edf0, the delete checks
};

class SelectionBar final : public Ui::NewUiWindow
{
public:
    // `texts` can be nullptr and has to outlive the bar.
    SelectionBar(Ui::TextureSizes sizes, const Tables::TextTable* texts);

    // The +0x543 that the build (0x63e820) computes once, with the list: some character of slots
    // 0 to 4 has bit 0x10 of its flags. With it texts 436 and 437 are drawn.
    void SetBlockedAccount(bool blocked);

    // 0x63eb40: create, with a free slot of the five; connect and delete, with one chosen. The menu,
    // always.
    void Enable(bool aFreeSlot, bool aChosenOne);

    // 0x65f690 in 0x65f4b0, when building the selection; button 1 of the Esc window closes it (0x647e48).
    void Open();
    void Close() override;
    [[nodiscard]] bool IsOpen() const override;
    // The bar (`bar` of LayoutSelectionBar).
    [[nodiscard]] Ui::Rect Bounds(Core::PixelSize screen) const override;

    // 0x63c490 on the four buttons and, with the focus, 0x63eb90: the one that had a click. Without the mouse
    // (a window on top, 0x65fc30), the buttons keep their state, without hover or click.
    std::optional<SelectionBarClick> Step(const Input::InputState& input, Core::PixelSize screen,
                                          Ui::NewUiTurn turn = {});

    // The ornament and the strip, the server notice (+0x511, `serverNotice`), text 1710, 436
    // and 437 with the account blocked and the four buttons, with the normal blend.
    void Draw(Render::SpriteBatch& sprites, Render::Frame::ITextDrawer& text, Core::PixelSize screen,
              std::string_view serverNotice = {});

    [[nodiscard]] const Ui::Button& CreateButton() const;
    [[nodiscard]] const Ui::Button& ConnectButton() const;
    [[nodiscard]] const Ui::Button& DeleteButton() const;

private:
    void Place(Core::PixelSize screen);

    Ui::TextureSizes m_sizes;
    const Tables::TextTable* m_texts;
    bool m_blockedAccount = false;
    bool m_open = false;
    Ui::Button m_create;
    Ui::Button m_menu;
    Ui::Button m_connect;
    Ui::Button m_delete;
    SelectionBarLayout m_layout;
};
} // namespace Client::FrontEnd
