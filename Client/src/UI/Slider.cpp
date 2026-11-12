#include "UI/Slider.h"

#include "Input/InputState.h"
#include "Input/Key.h"
#include "Render/UiImage.h"
#include "UI/Button.h"
#include "UI/Widget.h"

#include <array>
#include <cstdint>

namespace Client::UI
{
namespace
{
// +9: lo que se está moviendo.
constexpr std::uint8_t HoldingLeft = 0x01;
constexpr std::uint8_t HoldingRight = 0x02;
constexpr std::uint8_t Dragging = 0x10;
// 0x6baa00 y 0x6b89d0: apretado al lado de la perilla, la primera repetición a los 500 ms y las otras
// cada 50.
constexpr double FirstRepeatMs = 500.0;
constexpr double RepeatMs = 50.0;
// 0x6b882c: la mitad de un paso, para redondear la perilla al valor más cerca.
constexpr float Half = 0.5f;

Rect FromCorners(int left, int top, int right, int bottom)
{
    return Rect{.x = left, .y = top, .width = right - left, .height = bottom - top};
}
} // namespace

Slider::Slider(const SliderParts& parts)
    : m_parts(parts), m_thumb(parts.thumb.slot, parts.thumb.width, parts.thumb.height)
{
    // 0x63c330(w, h, slot, 1, −1, ...): un cuadro, el 0 en todos los estados.
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

void Slider::Step(const Input::InputState& input, Point mouse, double elapsedMs)
{
    if (!m_open)
    {
        return;
    }
    // La perilla no está en la lista de la ventana (0x66a070): su +0xa9 queda en el 1 de 0x63c330 y
    // ve siempre el mouse.
    static_cast<void>(m_thumb.Step(input, mouse));
    if (!m_thumb.Enabled())
    {
        return;
    }
    // +0x1e: al soltar, la perilla vuelve al lugar de su valor y se deja de mover. 0x640850 no da el
    // apretón y la soltada en el mismo frame; un paso de MuRet sí, y con el botón arriba al final la
    // soltada fue después: queda para el paso siguiente, como el frame siguiente del original, y la
    // ventana ve el deslizador moviéndose en este (Q-C498).
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
    // Los dos lados de la perilla, del alto de la perilla (+0x18): de x a la perilla y de la perilla
    // al final de la barra (+0x14).
    const int top = m_position.y;
    const int bottom = top + m_parts.thumb.height;
    const int thumbX = ThumbX();
    const Rect left = FromCorners(m_position.x, top, thumbX, bottom);
    const Rect right = FromCorners(thumbX + m_parts.thumb.width, top, m_position.x + m_parts.track.width, bottom);
    const auto hold = [&](std::uint8_t side, void (Slider::*move)())
    {
        // +0x110 suma mientras el mouse está de ese lado, apretado o no, y vuelve a 0 al apretar.
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
    // 0x63c470: con la perilla ya movida por el lado, si quedó debajo del mouse al apretar la agarra.
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
    // 0x646e3a: el valor más cerca de la perilla, ((x − x0) + paso · 0,5) / paso con el paso en la x87
    // y _ftol.
    const double step = static_cast<double>(m_travel) / m_max;
    m_value = static_cast<int>((static_cast<double>(ThumbX() - m_position.x) + step * Half) / step);
}

std::array<Render::UiImage, 3> Slider::Images(const TextureSizes& sizes) const
{
    const Render::UiImage track = PixelImage(
        m_parts.track.slot,
        Rect{.x = m_position.x, .y = m_position.y, .width = m_parts.track.width, .height = m_parts.track.height},
        sizes(m_parts.track.slot));
    // 0x6404b0: el relleno en la esquina de su rectángulo; 0x6402d0 le pone el alto (0x647370 con 2) y
    // 0x640580 el ancho (0x647370 con 1), las dos UV sin medio téxel.
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
    // 0x646990: en el máximo, la perilla va al final del recorrido sin la cuenta en la x87.
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
} // namespace Client::UI
