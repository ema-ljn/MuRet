#include "Ui/Slider.h"

#include "Input/InputState.h"
#include "Input/Key.h"
#include "Math/FloatConstants.h"
#include "Render/UiImage.h"
#include "Ui/Button.h"
#include "Ui/Widget.h"

#include <array>
#include <cstdint>
#include <functional>

namespace Client::Ui
{
namespace
{
// +9: what is moving.
constexpr std::uint8_t HoldingLeft = 0x01;
constexpr std::uint8_t HoldingRight = 0x02;
constexpr std::uint8_t Dragging = 0x10;
// 0x6baa00 and 0x6b89d0: pressed beside the thumb, the first repeat at 500 ms and the others every 50.
constexpr double FirstRepeatMs = 500.0;
constexpr double RepeatMs = 50.0;
// 0x6b882c: half a step, to round the thumb to the nearest value.
using Math::Half;

Rect FromCorners(int left, int top, int right, int bottom)
{
    return Rect{.x = left, .y = top, .width = right - left, .height = bottom - top};
}
} // namespace

Slider::Slider(const SliderParts& parts)
    : m_parts(parts), m_thumb(parts.thumb.slot, parts.thumb.width, parts.thumb.height)
{
    // 0x63c330(w, h, slot, 1, −1, ...): one frame, 0 in every state.
    m_thumb.SetStateFrames(0, 0);
    const int travel = parts.track.width - parts.thumb.width;
    m_travel = travel < 0 ? 0 : travel;
    PlaceTheThumb();
}

void Slider::SetMax(int max)
{
    m_max = max > 1 ? max : 1;
    PlaceTheThumb();
}

void Slider::SetValue(int value)
{
    m_value = value < 0 ? 0 : (value > m_max ? m_max : value);
    PlaceTheThumb();
}

int Slider::Value() const
{
    return m_value;
}

int Slider::Max() const
{
    return m_max;
}

void Slider::MoveTo(Point topLeft)
{
    m_position = topLeft;
    PlaceTheThumb();
}

Point Slider::Position() const
{
    return m_position;
}

void Slider::Open(bool open)
{
    m_open = open;
}

bool Slider::IsOpen() const
{
    return m_open;
}

bool Slider::Moving() const
{
    return m_moving != 0;
}

const Button& Slider::Thumb() const
{
    return m_thumb;
}

void Slider::Step(const Input::InputState& input, Point mouse, double elapsedMs,
                  const std::function<void()>& onButtonClick)
{
    if (!m_open)
    {
        return;
    }
    // The thumb is not in the window's list (0x66a070): its +0xa9 stays at 0x63c330's 1 and it always sees the mouse.
    static_cast<void>(m_thumb.Step(input, mouse, onButtonClick));
    if (!m_thumb.Enabled())
    {
        return;
    }
    // +0x1e: on release, the thumb goes back to its value's place and stops moving. 0x640850 does not give the press
    // and the release in the same frame; a MuRet step does, and with the button up at the end the release came after:
    // it is left for the next step, like the original's next frame, and the window sees the slider moving in this one
    // (Q-C498).
    const bool pressed = input.WasPressed(Input::MouseButton::Left);
    const bool released = input.WasReleased(Input::MouseButton::Left);
    const bool releasedLast = pressed && released && !input.IsDown(Input::MouseButton::Left);
    if (m_releaseLater || (released && !releasedLast))
    {
        Release();
    }
    m_releaseLater = releasedLast;
    StepThePress(mouse, elapsedMs, pressed);
}

void Slider::Release()
{
    PlaceTheThumb();
    m_moving = 0;
}

void Slider::StepThePress(Point mouse, double elapsedMs, bool pressed)
{
    // The two sides of the thumb, as tall as the thumb (+0x18): from x to the thumb and from the thumb to the end of
    // the bar (+0x14).
    const int top = m_position.y;
    const int bottom = top + m_parts.thumb.height;
    const int thumbX = ThumbX();
    const Rect left = FromCorners(m_position.x, top, thumbX, bottom);
    const Rect right = FromCorners(thumbX + m_parts.thumb.width, top, m_position.x + m_parts.track.width, bottom);
    const auto hold = [&](std::uint8_t side, void (Slider::*move)())
    {
        // +0x110 adds up while the mouse is on that side, pressed or not, and goes back to 0 on a press.
        m_held += elapsedMs;
        if (pressed)
        {
            (this->*move)();
            m_moving = static_cast<std::uint8_t>(m_moving | side);
            m_held = 0.0;
        }
        if (m_held < FirstRepeatMs || (m_moving & side) == 0)
        {
            return;
        }
        m_repeat += elapsedMs;
        if (m_repeat < RepeatMs)
        {
            return;
        }
        (this->*move)();
        m_repeat = 0.0;
    };
    if (left.Contains(mouse))
    {
        hold(HoldingLeft, &Slider::StepLeft);
    }
    else if (right.Contains(mouse))
    {
        hold(HoldingRight, &Slider::StepRight);
    }
    // 0x63c470: with the thumb already moved by the side, if it ended up under the mouse on the press it grabs it.
    if (m_thumb.Area().Contains(mouse) && pressed)
    {
        m_lastMouseX = mouse.x;
        m_grabX = mouse.x - ThumbX() + m_position.x;
        m_moving = static_cast<std::uint8_t>(m_moving | Dragging);
    }
    if ((m_moving & Dragging) == 0)
    {
        return;
    }
    int x = m_position.x;
    if (m_grabX < mouse.x)
    {
        if (m_grabX + m_travel > mouse.x)
        {
            x = ThumbX() - m_lastMouseX + mouse.x;
            m_lastMouseX = mouse.x;
        }
        else
        {
            x = m_travel + m_position.x;
            m_lastMouseX = m_travel + m_grabX;
        }
    }
    else
    {
        m_lastMouseX = m_grabX;
    }
    m_thumb.MoveTo(Point{x, m_thumb.Area().y});
    // 0x646e3a: the value nearest the thumb, ((x − x0) + step · 0.5) / step with the step on the x87 and _ftol.
    const double step = static_cast<double>(m_travel) / m_max;
    m_value = static_cast<int>((static_cast<double>(ThumbX() - m_position.x) + step * Half) / step);
}

std::array<Render::UiImage, 3> Slider::Images(const TextureSizes& sizes) const
{
    const Render::UiImage track = PixelImage(
        m_parts.track.slot,
        Rect{.x = m_position.x, .y = m_position.y, .width = m_parts.track.width, .height = m_parts.track.height},
        sizes(m_parts.track.slot));
    // 0x6404b0: the fill at its rectangle's corner; 0x6402d0 sets its height (0x647370 with 2) and 0x640580 its width
    // (0x647370 with 1), both UVs without the half texel.
    Render::UiImage fill = PixelImage(m_parts.fill.slot,
                                      Rect{.x = m_position.x + m_parts.fillArea.x,
                                           .y = m_position.y + m_parts.fillArea.y,
                                           .width = FillWidth(),
                                           .height = m_parts.fillArea.height},
                                      sizes(m_parts.fill.slot));
    fill.resizedWidth = true;
    fill.resizedHeight = true;
    return {track, fill, m_thumb.Image(sizes)};
}

int Slider::FillWidth() const
{
    const std::uint32_t max = m_max >= 1 ? static_cast<std::uint32_t>(m_max) : 1U;
    const auto raw = static_cast<std::uint32_t>(m_value);
    const std::uint32_t value = raw <= max ? raw : max;
    const auto width = static_cast<std::uint32_t>(m_parts.fillArea.width);
    return static_cast<int>(width * value / max);
}

void Slider::PlaceTheThumb()
{
    const int x = static_cast<int>(static_cast<double>(m_travel) / m_max * m_value + m_position.x);
    m_thumb.MoveTo(Point{x, m_position.y});
}

void Slider::StepLeft()
{
    --m_value;
    PlaceTheThumb();
}

void Slider::StepRight()
{
    // 0x646990: at the maximum, the thumb goes to the end of the travel without the x87 computation.
    ++m_value;
    if (m_value != m_max)
    {
        PlaceTheThumb();
        return;
    }
    m_thumb.MoveTo(Point{m_travel + m_position.x, m_position.y});
}

int Slider::ThumbX() const
{
    return m_thumb.Area().x;
}
} // namespace Client::Ui
