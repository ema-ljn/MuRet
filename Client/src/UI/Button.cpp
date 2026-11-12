#include "UI/Button.h"

#include "App/WindowState.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Render/ScreenScale.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Render/UiImage.h"
#include "Scenes/ITextDrawer.h"
#include "UI/Widget.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace Client::UI
{
namespace
{
constexpr int NormalFrame = 0;
constexpr int HoverFrame = 1;
constexpr int PressedFrame = 2;

// +0xd8: lo que 0x63c490 le suma a la y del rótulo en 640 × 480 (0x3f000000 y 0x3fc00000).
constexpr double LabelOffset = 0.5;
constexpr double PressedLabelOffset = 1.5;
} // namespace

Button::Button(Render::TextureSlot slot, int width, int height, std::optional<int> checkedFrame,
               std::optional<int> disabledFrame)
    : m_slot(slot), m_area{.x = 0, .y = 0, .width = width, .height = height}, m_checkedFrame(checkedFrame),
      m_disabledFrame(disabledFrame)
{
}

void Button::SetEnabled(bool enabled)
{
    m_enabled = enabled;
    if (!enabled)
    {
        m_captured = false;
        m_hover = false;
    }
}

bool Button::Enabled() const
{
    return m_enabled;
}

void Button::MoveTo(Point topLeft)
{
    m_area.x = topLeft.x;
    m_area.y = topLeft.y;
}

Rect Button::Area() const
{
    return m_area;
}

bool Button::Step(const Input::InputState& input, std::optional<Point> mouse)
{
    m_over = mouse.has_value() && m_area.Contains(*mouse);
    if (!m_enabled)
    {
        m_captured = false;
        m_hover = false;
        return false;
    }
    if (m_over && input.WasPressed(Input::MouseButton::Left))
    {
        m_captured = true;
    }
    bool clicked = false;
    if (input.WasReleased(Input::MouseButton::Left))
    {
        clicked = m_over && m_captured;
        m_captured = false;
    }
    // Un clic en uno que se marca lo da vuelta (+0xab), como 0x63c490; la ventana después marca
    // el que quiera.
    if (clicked && m_checkedFrame.has_value())
    {
        m_checked = !m_checked;
    }
    m_hover = m_over && !m_captured && !input.IsDown(Input::MouseButton::Left);
    return clicked;
}

void Button::SetChecked(bool checked)
{
    m_checked = checked;
}

bool Button::Checked() const
{
    return m_checked;
}

int Button::Frame() const
{
    if (!m_enabled)
    {
        return m_disabledFrame.value_or(NormalFrame);
    }
    if (m_checked && m_checkedFrame.has_value())
    {
        return *m_checkedFrame;
    }
    if (Pressed())
    {
        return PressedFrame;
    }
    return m_hover ? HoverFrame : NormalFrame;
}

bool Button::Pressed() const
{
    return m_enabled && m_over && m_captured;
}

Render::UiImage Button::Image(const TextureSizes& sizes) const
{
    Render::UiImage image = PixelImage(m_slot, m_area, sizes(m_slot));
    image.frameTop = Frame() * m_area.height;
    return image;
}

void Button::SetLabel(std::string label, ButtonColors colors)
{
    m_label = std::move(label);
    m_colors = colors;
}

std::optional<std::uint32_t> Button::DrawLabel(Scenes::ITextDrawer& text, App::PixelSize screen) const
{
    // Sin pantalla no hay proporción por la que dividir la y.
    if (m_label.empty() || screen.width <= 0 || screen.height <= 0)
    {
        return std::nullopt;
    }
    constexpr Render::Text::FontKind Font = Render::Text::FontKind::OtherFace12;
    const Render::Text::TextExtent extent = text.Measure(Font, m_label);
    // 0x467060, el modo 2: (ancho − cx) / 2 con la división de C.
    const int centered = m_area.x + (m_area.width - extent.width) / 2;
    // 0x63c7e1..0x63c80d: fild de h − cy, fmul por 0,5, fiadd de la y, fdiv por ry y fadd de +0xd8,
    // en la x87 de 53 bits; _ftol trunca.
    const double middle = static_cast<double>(m_area.height - extent.height) * 0.5 + static_cast<double>(m_area.y);
    const double ratioY = Render::ScaleY(1.0f, screen);
    const double offset = Pressed() ? PressedLabelOffset : LabelOffset;
    const int y = static_cast<int>(middle / ratioY + offset);
    const std::uint32_t color = LabelColor();
    text.Draw(Render::Text::DesignX(centered, screen), y, m_label,
              Render::Text::TextStyle{Font, Render::Text::TextColors{color, 0}});
    return color;
}

std::uint32_t Button::LabelColor() const
{
    // 0x63c490 elige el color con el mismo estado que el cuadro.
    if (!m_enabled)
    {
        return m_checked ? m_colors.checkedDisabled : m_colors.disabled;
    }
    if (m_checked)
    {
        return Pressed() ? m_colors.checkedPressed : (m_hover ? m_colors.checkedHover : m_colors.checkedNormal);
    }
    return Pressed() ? m_colors.pressed : (m_hover ? m_colors.hover : m_colors.normal);
}
} // namespace Client::UI
