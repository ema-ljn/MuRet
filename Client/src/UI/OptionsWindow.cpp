#include "UI/OptionsWindow.h"

#include "App/WindowState.h"
#include "Data/Text/TextTable.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Render/UiImage.h"
#include "Scenes/ITextDrawer.h"
#include "UI/Button.h"
#include "UI/EscapeWindow.h"
#include "UI/GameText.h"
#include "UI/NewUi.h"
#include "UI/Slider.h"
#include "UI/Widget.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Client::UI
{
namespace
{
// 0x644120: la tabla de 0x66a1c0 con op2_back1 arriba (0x730, de 213 × 65), las casillas de op2_ch
// (0x731, dos cuadros de 16), el botón de op1_b_all y los deslizadores: la perilla de op2_volume3
// (0x732, 13 × 13), la barra de op2_volume1 (0x734, 98 × 13) y el relleno de op2_volume2 (0x733, 4 × 7)
// en (3, 3, 95, 10). Los sube 0x61e8xx.
constexpr Render::TextureSlot TopSlot{0x730};
constexpr Render::TextureSlot LineSlot{0x72d};
constexpr Render::TextureSlot CheckSlot{0x731};
constexpr Render::TextureSlot CloseSlot{0x72f};
constexpr SliderParts SliderLook{.thumb = {.slot = Render::TextureSlot{0x732}, .width = 13, .height = 13},
                                 .track = {.slot = Render::TextureSlot{0x734}, .width = 98, .height = 13},
                                 .fill = {.slot = Render::TextureSlot{0x733}, .width = 4, .height = 7},
                                 .fillArea = Rect{.x = 3, .y = 3, .width = 92, .height = 7}};

constexpr int FrameWidth = 213;
constexpr int TopHeight = 65;
constexpr int BottomHeight = 43;
// 0x644302 y vtable + 0x1c: 30 líneas, el máximo que le da 0x66a1c0.
constexpr int Lines = 30;
constexpr int CheckSize = 16;
constexpr int CheckedFrame = 1;
constexpr int CloseWidth = 108;
constexpr int CloseHeight = 30;
// 0x644520: las casillas a 52 del borde izquierdo y de arriba, una cada 16 + 25; el primer deslizador,
// 48 abajo de la última casilla más el alto de una, y el otro, 48 más su alto (13) abajo; el botón, a
// 301 de arriba.
constexpr int Inset = 0x34;
constexpr int CheckGap = 0x19;
constexpr int SliderGap = 0x30;
constexpr int CloseTop = 0x12d;
// 0x644860: el título a 10 de arriba, los rótulos de las casillas 24 a la derecha y 4 abajo, y los de
// los deslizadores 18 arriba, con el valor 85 a la derecha.
constexpr int TitleTop = 10;
constexpr int CheckLabelLeft = 0x18;
constexpr int CheckLabelTop = 4;
constexpr int SliderLabelAbove = 0x12;
constexpr int SliderValueLeft = 0x55;
// 0x6468e0(9) y 0x6468e0(4).
constexpr int VolumeMax = 9;
constexpr int GlowMax = 4;

constexpr std::size_t TitleText = 385;
constexpr std::array<std::size_t, 3> CheckTexts{386, 387, 919};
constexpr std::array<std::size_t, 2> SliderTexts{389, 1840};
constexpr std::size_t CloseText = 388;
// 0x644860 con la fuente de DAT_05763560, blanco sobre 0.
constexpr Render::Text::TextStyle TextLook{Render::Text::FontKind::OtherFace12,
                                           Render::Text::TextColors{0xffffffff, 0}};
// El rótulo del botón, con los colores de 0x644378 (los de la ventana de Esc).
constexpr ButtonColors CloseColors{.normal = 0xffe2e2e2,
                                   .pressed = 0xffe2e2e2,
                                   .hover = 0xffffffff,
                                   .checkedNormal = 0xffe2e2e2,
                                   .checkedPressed = 0xffe2e2e2,
                                   .checkedHover = 0xffffffff,
                                   .disabled = 0,
                                   .checkedDisabled = 0};
// La base de 0x669c70 con el slot −1: negra con alfa 0x80.
constexpr float BaseAlpha = 128.0f / 255.0f;

Button MakeCheck()
{
    // 0x63c330(16, 16, 0x731, 2, 0, 0, −1, 1, 1, 1, −1): el cuadro 0 sin marcar, apretada o no, y el 1
    // marcada.
    Button check(CheckSlot, CheckSize, CheckSize, CheckedFrame);
    check.SetStateFrames(0, 0);
    return check;
}

std::string_view TextOf(const Data::Text::TextTable* texts, std::size_t index)
{
    return TextAt(texts, index).value_or(std::string_view{});
}

void DrawAt(Scenes::ITextDrawer& text, Point pixel, std::string_view line, App::PixelSize screen)
{
    // 0x5542f0 con el modo 1 y ancho 0; no dibuja un texto vacío.
    if (line.empty())
    {
        return;
    }
    text.Draw(Render::Text::DesignX(pixel.x, screen), Render::Text::DesignY(pixel.y, screen), line, TextLook);
}
} // namespace

void ApplyOption(OptionsSettings& settings, const OptionsEvent& event)
{
    switch (event.kind)
    {
    case OptionsEvent::Kind::AutoAttack:
        settings.autoAttack = event.value != 0;
        return;
    case OptionsEvent::Kind::NoticeSound:
        settings.noticeSound = event.value != 0;
        return;
    case OptionsEvent::Kind::SlidesOn:
        settings.slidesOn = event.value != 0;
        return;
    case OptionsEvent::Kind::Volume:
        settings.volume = event.value;
        return;
    case OptionsEvent::Kind::GlowDetail:
        // 0x64480a: el byte bajo del valor.
        settings.glowDetail = static_cast<std::uint8_t>(event.value & 0xff);
        return;
    case OptionsEvent::Kind::Closed:
        return;
    }
}

OptionsWindowLayout LayoutOptionsWindow(App::PixelSize screen, int lineHeight)
{
    OptionsWindowLayout layout;
    const int height = lineHeight * Lines + TopHeight + BottomHeight;
    layout.frame = Rect{
        .x = (screen.width - FrameWidth) / 2, .y = (screen.height - height) / 2, .width = FrameWidth, .height = height};
    const int left = layout.frame.x + Inset;
    int top = layout.frame.y + Inset;
    for (Rect& check : layout.checks)
    {
        check = Rect{.x = left, .y = top, .width = CheckSize, .height = CheckSize};
        top += CheckSize + CheckGap;
    }
    const int first = CheckSize + layout.checks[2].y + SliderGap;
    layout.sliders[0] = Point{left, first};
    layout.sliders[1] = Point{left, first + SliderLook.thumb.height + SliderGap};
    layout.close = Rect{.x = (FrameWidth - CloseWidth) / 2 + layout.frame.x,
                        .y = layout.frame.y + CloseTop,
                        .width = CloseWidth,
                        .height = CloseHeight};
    return layout;
}

OptionsWindow::OptionsWindow(TextureSizes sizes, const Data::Text::TextTable* texts)
    : m_sizes(std::move(sizes)), m_texts(texts), m_checks{MakeCheck(), MakeCheck(), MakeCheck()},
      m_close(CloseSlot, CloseWidth, CloseHeight), m_sliders{Slider(SliderLook), Slider(SliderLook)}
{
}

void OptionsWindow::Arm(const OptionsSettings& settings)
{
    for (Button& check : m_checks)
    {
        check = MakeCheck();
    }
    m_close = Button(CloseSlot, CloseWidth, CloseHeight);
    m_close.SetLabel(std::string(TextOf(m_texts, CloseText)), CloseColors);
    m_sliders = {Slider(SliderLook), Slider(SliderLook)};
    m_sliders[0].SetMax(VolumeMax);
    m_sliders[1].SetMax(GlowMax);
    m_layout = OptionsWindowLayout{};
    // 0x6446e0.
    m_checks[0].SetChecked(settings.autoAttack);
    m_checks[1].SetChecked(settings.noticeSound);
    m_checks[2].SetChecked(settings.slidesOn);
    m_sliders[0].SetValue(settings.volume);
    m_sliders[1].SetValue(settings.glowDetail);
    m_open = false;
}

void OptionsWindow::Open()
{
    m_open = true;
    for (Slider& slider : m_sliders)
    {
        slider.Open(true);
    }
}

void OptionsWindow::Close()
{
    m_open = false;
}

bool OptionsWindow::IsOpen() const
{
    return m_open;
}

Rect OptionsWindow::Bounds(App::PixelSize screen) const
{
    return Rect{.x = 0, .y = 0, .width = screen.width, .height = screen.height};
}

const Button& OptionsWindow::Check(std::size_t index) const
{
    return m_checks.at(index);
}

const Button& OptionsWindow::CloseButton() const
{
    return m_close;
}

const Slider& OptionsWindow::SliderAt(std::size_t index) const
{
    return m_sliders.at(index);
}

std::optional<OptionsEvent> OptionsWindow::Step(const Input::InputState& input, App::PixelSize screen, double elapsedMs,
                                                const OptionsSettings& current, NewUiTurn turn)
{
    if (!m_open)
    {
        return std::nullopt;
    }
    Place(screen);
    const std::optional<Point> mouse = MouseOfTheTurn(turn, input, screen);
    std::array<bool, 3> checked{};
    for (std::size_t i = 0; i < m_checks.size(); ++i)
    {
        checked[i] = m_checks[i].Step(input, mouse);
    }
    const bool closed = m_close.Step(input, mouse);
    if (!turn.logic)
    {
        return std::nullopt;
    }
    // 0x644730: los deslizadores primero, con el mouse de 0x640850.
    const Point raw = MouseInPixels(input, screen);
    for (Slider& slider : m_sliders)
    {
        slider.Step(input, raw, elapsedMs);
    }
    // Una casilla que cambió escribe su global y termina el paso.
    constexpr std::array<OptionsEvent::Kind, 3> CheckKinds{
        OptionsEvent::Kind::AutoAttack, OptionsEvent::Kind::NoticeSound, OptionsEvent::Kind::SlidesOn};
    for (std::size_t i = 0; i < m_checks.size(); ++i)
    {
        if (checked[i])
        {
            return OptionsEvent{.kind = CheckKinds[i], .value = m_checks[i].Checked() ? 1 : 0};
        }
    }
    if (closed)
    {
        Close();
        return OptionsEvent{.kind = OptionsEvent::Kind::Closed};
    }
    // Un deslizador que se mueve (+9 distinto de 0) escribe su valor si es otro, y termina el paso
    // aunque sea el mismo.
    if (m_sliders[0].Moving())
    {
        if (m_sliders[0].Value() == current.volume)
        {
            return std::nullopt;
        }
        return OptionsEvent{.kind = OptionsEvent::Kind::Volume, .value = m_sliders[0].Value()};
    }
    if (m_sliders[1].Moving())
    {
        // 0x6447fe: compara el byte bajo del valor con el byte del global.
        if ((m_sliders[1].Value() & 0xff) == current.glowDetail)
        {
            return std::nullopt;
        }
        return OptionsEvent{.kind = OptionsEvent::Kind::GlowDetail, .value = m_sliders[1].Value()};
    }
    // El +0x31 del mouse (0x640850) es siempre 0.
    if (input.TakePress(Input::Key::Escape))
    {
        Close();
        return OptionsEvent{.kind = OptionsEvent::Kind::Closed};
    }
    return std::nullopt;
}

void OptionsWindow::Draw(Render::SpriteBatch& sprites, Scenes::ITextDrawer& text, App::PixelSize screen,
                         const OptionsSettings& current)
{
    if (!m_open)
    {
        return;
    }
    Place(screen);
    sprites.SetBlend(Render::BlendMode::Alpha);
    sprites.Image(
        Render::UiImage{.slot = std::nullopt,
                        .x = 0,
                        .y = 0,
                        .width = screen.width,
                        .height = screen.height,
                        .color = Render::Color{.red = 0.0f, .green = 0.0f, .blue = 0.0f, .alpha = BaseAlpha}});
    sprites.SetColor(Render::Color{});
    for (const Render::UiImage& image : StoneFrameImages(m_layout.frame, TopSlot, TopHeight, m_sizes))
    {
        sprites.Image(image);
    }
    // El título, centrado en el ancho del marco (0x5542f0 con el modo 2), como el rótulo de un botón.
    if (const std::string_view title = TextOf(m_texts, TitleText); !title.empty())
    {
        const Render::Text::TextExtent extent = text.Measure(TextLook.font, title);
        const int centered = m_layout.frame.x + (m_layout.frame.width - extent.width) / 2;
        text.Draw(Render::Text::DesignX(centered, screen), Render::Text::DesignY(m_layout.frame.y + TitleTop, screen),
                  title, TextLook);
    }
    for (std::size_t i = 0; i < m_checks.size(); ++i)
    {
        const Rect area = m_checks[i].Area();
        DrawAt(text, Point{area.x + CheckLabelLeft, area.y + CheckLabelTop}, TextOf(m_texts, CheckTexts[i]), screen);
    }
    // Los valores son los de los globales: el del brillo, 2 · d + 5.
    const std::array<int, 2> values{current.volume, current.glowDetail * 2 + 5};
    for (std::size_t i = 0; i < m_sliders.size(); ++i)
    {
        const Point at = m_sliders[i].Position();
        DrawAt(text, Point{at.x, at.y - SliderLabelAbove}, TextOf(m_texts, SliderTexts[i]), screen);
        DrawAt(text, Point{at.x + SliderValueLeft, at.y - SliderLabelAbove}, std::to_string(values[i]), screen);
        if (m_sliders[i].IsOpen())
        {
            for (const Render::UiImage& image : m_sliders[i].Images(m_sizes))
            {
                sprites.Image(image);
            }
        }
    }
    // 0x66a090: las casillas y el botón, en el orden en que 0x66a070 los sumó.
    for (const Button& check : m_checks)
    {
        sprites.Image(check.Image(m_sizes));
    }
    sprites.Image(m_close.Image(m_sizes));
    static_cast<void>(m_close.DrawLabel(text, screen));
}

void OptionsWindow::Place(App::PixelSize screen)
{
    m_layout = LayoutOptionsWindow(screen, m_sizes(LineSlot).height);
    for (std::size_t i = 0; i < m_checks.size(); ++i)
    {
        m_checks[i].MoveTo(Point{m_layout.checks[i].x, m_layout.checks[i].y});
    }
    m_close.MoveTo(Point{m_layout.close.x, m_layout.close.y});
    for (std::size_t i = 0; i < m_sliders.size(); ++i)
    {
        if (m_sliders[i].Position() != m_layout.sliders[i])
        {
            m_sliders[i].MoveTo(m_layout.sliders[i]);
        }
    }
}
} // namespace Client::UI
