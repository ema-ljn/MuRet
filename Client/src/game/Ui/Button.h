#pragma once

#include "Core/PixelSize.h"
#include "Input/InputState.h"
#include "Render/TextureSlot.h"
#include "Render/UiImage.h"
#include "Render/Frame/ITextDrawer.h"
#include "Ui/Widget.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

// A new-UI button (CL5.5): 0x63c330 builds it, 0x63c490 handles it and 0x63c710 draws its label (06 § Las ventanas de
// la UI nueva). Its texture has the frames one below the other, each as tall as the button: frame k starts at row
// k · h (0x63c330 builds the table with (0, k · h)), and 0x647560 shifts the UVs (UiImage::frameTop). The list,
// account and notice buttons use 0 normal, 1 hovered and 2 pressed; the group ones, 3 checked.
//
// Capture: with the left button pressed over it, the button takes it (DAT_07d53534); released over it while captured,
// it is a click (+0xaa), and released anywhere, it lets go. The original has one capture for the whole UI; here each
// button has its own, and "over" means over with the mouse button up: pressed outside everything and dragged over,
// the original shows frame 1 and MuRet frame 0. Handled in the step, not the frame (Q-C81).
namespace Client::Ui
{
// The label colors by state (the +0xd0 table, 0xAABBGGRR): [0] to [3] unchecked (normal, pressed, hover and disabled)
// and [4] to [7] checked.
struct ButtonColors
{
    std::uint32_t normal = 0xffffffff;
    std::uint32_t pressed = 0xffffffff;
    std::uint32_t hover = 0xffffffff;
    std::uint32_t checkedNormal = 0xffffffff;
    std::uint32_t checkedPressed = 0xffffffff;
    std::uint32_t checkedHover = 0xffffffff;
    std::uint32_t disabled = 0xffffffff;
    std::uint32_t checkedDisabled = 0xffffffff;
};

class Button
{
public:
    // `checkedFrame`: the checked frame (+0xbc), or none if the button does not check. `disabledFrame`: the disabled
    // one
    // (+0xb8, and +0xc8 checked), or none for the normal one.
    Button(Render::TextureSlot slot, int width, int height, std::optional<int> checkedFrame = std::nullopt,
           std::optional<int> disabledFrame = std::nullopt);

    // +0xb0 and +0xb4: the pressed and hover frames, 2 and 1 unless 0x63c330 gets others (−1 is 0). The options'
    // checkboxes and a slider's knob have 0 in both.
    void SetStateFrames(int pressed, int hover);

    // +0xa8: disabled, it has no click and never shows hover or pressed (0x63c490). Starts enabled.
    void SetEnabled(bool enabled);
    [[nodiscard]] bool Enabled() const;

    // 0x6472e0(x, y, 3): the top-left corner, in pixels.
    void MoveTo(Point topLeft);
    [[nodiscard]] Rect Area() const;

    // One step of 0x63c490: true on a click. `onClick` runs for that click after +0xaa is set to 1 and before the check
    // flips (0x63c4fc, 0x63c503). No mouse is the +0xa9 at 0 of a window without one (0x669ed0): never over (0x63c470).
    bool Step(const Input::InputState& input, std::optional<Point> mouse, const std::function<void()>& onClick = {});

    void SetChecked(bool checked);
    [[nodiscard]] bool Checked() const;

    // The last step's frame.
    [[nodiscard]] int Frame() const;
    // Over and captured: frame 2, and the label one point lower.
    [[nodiscard]] bool Pressed() const;

    // With its frame, in pixels.
    [[nodiscard]] Render::UiImage Image(const TextureSizes& sizes) const;

    // +0xcc and +0xd0. An empty label is not drawn.
    void SetLabel(std::string label, ButtonColors colors);
    // 0x63c710: with font DAT_05763560, the state's color and background 0, centered in the button's width (0x467060's
    // mode 2). The y in 640 × 480 is ((h − cy) · 0.5 + y) / ry plus 0.5, or 1.5 pressed (+0xd8), truncated. The box, as
    // wide as the button, starts at its x in 640 × 480 (DesignX), and 0x467060 places the text inside, in pixels
    // (ITextDrawer::DrawColumns with CenterInBox): a label wider than the button is cut on both sides (Q-C710), and the
    // background 0 does not show. Without a screen (a side at 0), nothing. Returns the color it drew with, or none if
    // it did not draw: 0x63c710 leaves it as the text color (DAT_006c8e5c), and what is drawn next without setting
    // another comes out with it (Q-C83).
    std::optional<std::uint32_t> DrawLabel(Render::Frame::ITextDrawer& text, Core::PixelSize screen) const;

private:
    [[nodiscard]] std::uint32_t LabelColor() const;

    Render::TextureSlot m_slot;
    Rect m_area;
    std::optional<int> m_checkedFrame;
    std::optional<int> m_disabledFrame;
    int m_pressedFrame = 2;
    int m_hoverFrame = 1;
    bool m_enabled = true;
    bool m_checked = false;
    bool m_captured = false;
    bool m_over = false;
    bool m_hover = false;
    std::string m_label;
    ButtonColors m_colors;
};
} // namespace Client::Ui
