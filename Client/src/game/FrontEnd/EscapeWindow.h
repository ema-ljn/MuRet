#pragma once

#include "Core/PixelSize.h"
#include "Tables/TextTable.h"
#include "Input/InputState.h"
#include "Render/SpriteBatch.h"
#include "Render/TextureSlot.h"
#include "Render/UiImage.h"
#include "Render/Frame/ITextDrawer.h"
#include "Ui/Button.h"
#include "Ui/NewUi.h"
#include "Ui/Widget.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

// The Esc window of the login and the selection (+0x3d0; CL8, milestone 8; 06 § La ventana de Esc): 0x6478c0
// builds it, 0x647c00 places it, 0x647cd0 opens it, 0x647d40 handles it and 0x647ed0 draws it.
// Full-screen (0x669c70(W, H, −1, 0), black with alpha 0x80, like the notice), with a 213-wide stone
// frame in the center (0x66a1c0) and four 108 × 30 buttons (0x63c330): quit the game (381), choose
// another server (382, only at the selection), the options (385) and close (388).
namespace Client::FrontEnd
{
// 0x647ef0: "...en% d segundos.", with what is left of the countdown.
inline constexpr std::size_t LeavingTheGameText = 380;

// The frame and the buttons, in pixels.
struct EscapeWindowLayout
{
    // 213 × (64 + lines · height of op1_back3 + 43), centered.
    Ui::Rect frame;
    // Quit, another server, options and close.
    std::array<Ui::Rect, 4> buttons{};
};

// 0x6478c0 and 0x647c00 with `lines` frame lines (6 at the login, 10 at the selection) of
// `lineHeight` (the height of op1_back3's buffer, 0x66a4e0).
[[nodiscard]] EscapeWindowLayout LayoutEscapeWindow(Core::PixelSize screen, int lines, int lineHeight);

// 0x66a960: the stone background, the top edge, the bottom one and the two sides, in that order.
[[nodiscard]] std::array<Render::UiImage, 5> EscapeFrameImages(Ui::Rect frame, const Ui::TextureSizes& sizes);
// The same with another top edge (`topSlot`, of `topHeight`): the options' one is op2_back1,
// of 65 (0x644120).
[[nodiscard]] std::array<Render::UiImage, 5> StoneFrameImages(Ui::Rect frame, Render::TextureSlot topSlot,
                                                              int topHeight, const Ui::TextureSizes& sizes);

// What happened in a step of 0x647d40.
struct EscapeWindowEvent
{
    enum class Kind : std::uint8_t
    {
        // 0x647ef0: 380 with what is left, to the messages (0x555190, system and nameless).
        Countdown,
        // The countdown reached 0: "> Menu - Exit game." to the log and WM_CLOSE.
        Quit,
        // Button 1: "> Menu - Join another server.", C3 F1 02 02 (0x647f60), and this window and the
        // selection bar (+0x7f50) close.
        JoinAnotherServer,
        // Button 2: this window closes and the options open (+0x7e8).
        Options,
        // Button 3, or Esc with sound 0x19: it closes.
        Closed,
    };

    Kind kind = Kind::Closed;
    // With Countdown, what is left.
    int seconds = 0;

    friend bool operator==(const EscapeWindowEvent&, const EscapeWindowEvent&) = default;
};

class EscapeWindow final : public Ui::NewUiWindow
{
public:
    EscapeWindow(Ui::TextureSizes sizes, const Tables::TextTable* texts);

    // 0x6478c0 for the mode's scene: at the login (2) button 1 disabled (and not drawn: its
    // disabled frame is −1) and 6 lines; at the selection (4), 10. No countdown (+0x408 at −1
    // and +0x410 at 0). Closed.
    void Arm(Ui::NewUiMode mode);

    // 0x647cd0 with 1. The countdown does not start over.
    void Open();
    void Close() override;
    [[nodiscard]] bool IsOpen() const override;
    // Full-screen: 0x647c00 moves the frame and the buttons, not the rectangle.
    [[nodiscard]] Ui::Rect Bounds(Core::PixelSize screen) const override;

    // +0x408: the seconds left before closing the game, or nothing without a countdown.
    [[nodiscard]] std::optional<int> Countdown() const;
    [[nodiscard]] const Ui::Button& ButtonAt(std::size_t index) const;

    // The buttons (0x63c490) and, with the focus, 0x647d40 with this step's `elapsedMs` (the original
    // passes the frame's, up to 200).
    std::optional<EscapeWindowEvent> Step(const Input::InputState& input, Core::PixelSize screen, double elapsedMs,
                                          Ui::NewUiTurn turn = {});
    // 0x66a050 and 0x647ed0: the black background, the frame and the buttons with their label (0x63c710, which
    // does not draw the disabled one).
    void Draw(Render::SpriteBatch& sprites, Render::Frame::ITextDrawer& text, Core::PixelSize screen);

private:
    void Place(Core::PixelSize screen);

    Ui::TextureSizes m_sizes;
    const Tables::TextTable* m_texts = nullptr;
    std::array<Ui::Button, 4> m_buttons;
    EscapeWindowLayout m_layout;
    int m_lines = 1;
    bool m_open = false;
    // +0x408 (a WORD, −1 without a countdown) and +0x410.
    std::optional<int> m_countdown;
    double m_elapsed = 0.0;
};
} // namespace Client::FrontEnd
