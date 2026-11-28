#pragma once

#include "Input/InputState.h"
#include "Render/TextureSlot.h"
#include "Render/UiImage.h"
#include "Ui/Button.h"
#include "Ui/Widget.h"

#include <array>
#include <cstdint>
#include <functional>

// A new-UI slider (vtable 0x6ba9f8; CL8, milestone 8; 06 § Las opciones): 0x646640 builds it with three images, the
// thumb (a one-frame 0x63c330 button, +0x28), the bar and its fill (0x640220's bar, +0x108); 0x646a50 handles it and
// 0x646e90 draws it. MuRet has only the horizontal one (+8 at 0), the options one: no login or selection window builds
// the vertical one.
namespace Client::Ui
{
struct SliderImage
{
    Render::TextureSlot slot;
    int width = 0;
    int height = 0;
};

// What 0x646640 takes: the thumb, the bar, the fill and the fill's rectangle inside the bar (0x640220's
// +0xb0..+0xbc, which keeps left, top, right and bottom).
struct SliderParts
{
    SliderImage thumb;
    SliderImage track;
    SliderImage fill;
    Rect fillArea;
};

class Slider
{
public:
    // 0x646640 with the horizontal one: the maximum at 1 (+0x1c), the value at 0 (+0x20), at (0, 0), and the thumb's
    // travel (+0x24) is the bar's width minus the thumb's, or 0.
    explicit Slider(const SliderParts& parts);

    // 0x6468e0: 1 or more.
    void SetMax(int max);
    // 0x646f80: between 0 and the maximum.
    void SetValue(int value);
    [[nodiscard]] int Value() const;
    [[nodiscard]] int Max() const;
    // 0x646890: the bar's top-left corner (+0xc, +0x10), in pixels.
    void MoveTo(Point topLeft);
    [[nodiscard]] Point Position() const;
    // 0x646ef0: opens or closes the thumb (0x63c450) and the bar (0x6407b0).
    void Open(bool open);

    // +9: pressed left of the thumb (1), right of it (2) or dragging it (0x10). The window checks whether it is
    // non-zero.
    [[nodiscard]] bool Moving() const;
    [[nodiscard]] const Button& Thumb() const;

    // 0x646a50 with the step's `elapsedMs` and the UI mouse (0x640850's, which does not check whether the window has
    // it): the window handles it in its logic, with the focus.
    void Step(const Input::InputState& input, Point mouse, double elapsedMs,
              const std::function<void()>& onButtonClick = {});

    // 0x646e90: the bar and the fill (0x6407e0) and the thumb (0x63c710); closed, nothing.
    [[nodiscard]] std::array<Render::UiImage, 3> Images(const TextureSizes& sizes) const;
    [[nodiscard]] bool IsOpen() const;
    // 0x640580: (right − left) · value / maximum in unsigned integers.
    [[nodiscard]] int FillWidth() const;

private:
    // 0x646830: the thumb at x + travel / maximum · value (the 53-bit x87 and _ftol).
    void PlaceTheThumb();
    // +0x1e in 0x646a50: the thumb in its place and nothing moves.
    void Release();
    // The rest of 0x646a50: the thumb's two sides with `pressed` (+0x1c) and the drag.
    void StepThePress(Point mouse, double elapsedMs, bool pressed);
    // 0x646900 and 0x646990
    void StepLeft();
    void StepRight();
    [[nodiscard]] int ThumbX() const;

    SliderParts m_parts;
    Button m_thumb;
    Point m_position;
    int m_max = 1;
    int m_value = 0;
    int m_travel = 0;
    std::uint8_t m_moving = 0;
    // +0x110 and +0x118: how long it has been pressed and the time since the last repeat.
    double m_held = 0.0;
    double m_repeat = 0.0;
    // +0x120 and +0x124: the mouse x of the drag's last step and the thumb's x where it was grabbed.
    int m_lastMouseX = 0;
    int m_grabX = 0;
    // A release that came after the press in the same step (Q-C498).
    bool m_releaseLater = false;
    bool m_open = false;
};
} // namespace Client::Ui
