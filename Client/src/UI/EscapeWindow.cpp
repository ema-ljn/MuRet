#include "UI/EscapeWindow.h"

#include "App/WindowState.h"
#include "Data/Text/TextTable.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "Render/UiImage.h"
#include "Scenes/ITextDrawer.h"
#include "UI/Button.h"
#include "UI/GameText.h"
#include "UI/NewUi.h"
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
// 0x6478c0: la tabla de 0x66a1c0 (op1_stone, op1_back1 a op1_back4) y los botones (op1_b_all, de
// cuatro cuadros de 30); los sube 0x61ccb0.
constexpr Render::TextureSlot StoneSlot{0x72a};
constexpr Render::TextureSlot TopSlot{0x72b};
constexpr Render::TextureSlot BottomSlot{0x72c};
constexpr Render::TextureSlot LeftSlot{0x72d};
constexpr Render::TextureSlot RightSlot{0x72e};
constexpr Render::TextureSlot ButtonSlot{0x72f};

constexpr int FrameWidth = 213;
constexpr int TopHeight = 64;
constexpr int BottomHeight = 43;
constexpr int SideWidth = 5;
// 0x66a3d0: el fondo, 3 adentro del marco.
constexpr int FillInset = 3;
constexpr int ButtonWidth = 108;
constexpr int ButtonHeight = 30;
// 0x647c00: el primero a 33 de arriba, cada uno 4 abajo del anterior, y el último a 52 del borde de
// abajo.
constexpr int FirstButtonTop = 0x21;
constexpr int ButtonGap = 4;
constexpr int LastButtonFromBottom = 0x34;
// 0x6478c0 con DAT_006d5dc8: 6 líneas en el login y 10 en la selección (el máximo de 0x66a1c0).
constexpr int LoginLines = 6;
constexpr int SelectionLines = 10;
// +0x408 cuando arranca la cuenta, y los milisegundos de cada segundo (0x6baa48).
constexpr std::int16_t CountdownStart = 5;
constexpr std::int16_t NoCountdown = -1;
constexpr double SecondMs = 1000.0;

// 381, 382, 385 y 388; el rótulo con 0x63c880 y los colores de 0x6478c0: normal y apretado en
// 0xffe2e2e2, encima en blanco, deshabilitado en 0.
constexpr std::array<std::size_t, 4> LabelTexts{381, 382, 385, 388};
constexpr ButtonColors LabelColors{.normal = 0xffe2e2e2,
                                   .pressed = 0xffe2e2e2,
                                   .hover = 0xffffffff,
                                   .checkedNormal = 0xffe2e2e2,
                                   .checkedPressed = 0xffe2e2e2,
                                   .checkedHover = 0xffffffff,
                                   .disabled = 0,
                                   .checkedDisabled = 0};
constexpr std::size_t ExitButton = 0;
constexpr std::size_t ServerButton = 1;
constexpr std::size_t OptionsButton = 2;
constexpr std::size_t CloseButton = 3;

// La base de 0x669c70 con el slot −1: negra con alfa 0x80.
constexpr float BaseAlpha = 128.0f / 255.0f;

Button MakeButton()
{
    return Button(ButtonSlot, ButtonWidth, ButtonHeight);
}
} // namespace

EscapeWindowLayout LayoutEscapeWindow(App::PixelSize screen, int lines, int lineHeight)
{
    EscapeWindowLayout layout;
    const int height = lineHeight * lines + TopHeight + BottomHeight;
    layout.frame = Rect{
        .x = (screen.width - FrameWidth) / 2, .y = (screen.height - height) / 2, .width = FrameWidth, .height = height};
    const int x = (FrameWidth - ButtonWidth) / 2 + layout.frame.x;
    const int step = ButtonHeight + ButtonGap;
    const int top = layout.frame.y + FirstButtonTop;
    const int last = layout.frame.y + height - LastButtonFromBottom;
    layout.buttons[ExitButton] = Rect{.x = x, .y = top, .width = ButtonWidth, .height = ButtonHeight};
    layout.buttons[ServerButton] = Rect{.x = x, .y = top + step, .width = ButtonWidth, .height = ButtonHeight};
    layout.buttons[OptionsButton] = Rect{.x = x, .y = last - step, .width = ButtonWidth, .height = ButtonHeight};
    layout.buttons[CloseButton] = Rect{.x = x, .y = last, .width = ButtonWidth, .height = ButtonHeight};
    return layout;
}

std::array<Render::UiImage, 5> EscapeFrameImages(Rect frame, const TextureSizes& sizes)
{
    const int sideHeight = frame.height - TopHeight - BottomHeight;
    // El fondo y los lados siguen a 0x647370 (0x647260 con 1) y repiten su textura; los bordes, no.
    Render::UiImage fill = PixelImage(StoneSlot,
                                      Rect{.x = frame.x + FillInset,
                                           .y = frame.y + FillInset,
                                           .width = frame.width - 2 * FillInset,
                                           .height = frame.height - 2 * FillInset},
                                      sizes(StoneSlot));
    fill.resizedWidth = true;
    fill.resizedHeight = true;
    const Render::UiImage top = PixelImage(
        TopSlot, Rect{.x = frame.x, .y = frame.y, .width = frame.width, .height = TopHeight}, sizes(TopSlot));
    const Render::UiImage bottom = PixelImage(
        BottomSlot,
        Rect{.x = frame.x, .y = frame.y + TopHeight + sideHeight, .width = frame.width, .height = BottomHeight},
        sizes(BottomSlot));
    Render::UiImage left =
        PixelImage(LeftSlot, Rect{.x = frame.x, .y = frame.y + TopHeight, .width = SideWidth, .height = sideHeight},
                   sizes(LeftSlot));
    left.resizedHeight = true;
    Render::UiImage right = PixelImage(
        RightSlot,
        Rect{
            .x = frame.x + frame.width - SideWidth, .y = frame.y + TopHeight, .width = SideWidth, .height = sideHeight},
        sizes(RightSlot));
    right.resizedHeight = true;
    return {fill, top, bottom, left, right};
}

EscapeWindow::EscapeWindow(TextureSizes sizes, const Data::Text::TextTable* texts)
    : m_sizes(std::move(sizes)), m_texts(texts), m_buttons{MakeButton(), MakeButton(), MakeButton(), MakeButton()}
{
}

void EscapeWindow::Arm(NewUiMode mode)
{
    for (std::size_t i = 0; i < m_buttons.size(); ++i)
    {
        m_buttons[i] = MakeButton();
        m_buttons[i].SetLabel(std::string(TextAt(m_texts, LabelTexts[i]).value_or(std::string_view{})), LabelColors);
    }
    const bool selection = mode == NewUiMode::Selection;
    m_buttons[ServerButton].SetEnabled(selection);
    m_lines = selection ? SelectionLines : LoginLines;
    m_countdown = std::nullopt;
    m_elapsed = 0.0;
    m_open = false;
}

void EscapeWindow::Open()
{
    m_open = true;
}

void EscapeWindow::Close()
{
    m_open = false;
}

bool EscapeWindow::IsOpen() const
{
    return m_open;
}

Rect EscapeWindow::Bounds(App::PixelSize screen) const
{
    return Rect{.x = 0, .y = 0, .width = screen.width, .height = screen.height};
}

std::optional<int> EscapeWindow::Countdown() const
{
    return m_countdown;
}

const Button& EscapeWindow::ButtonAt(std::size_t index) const
{
    return m_buttons.at(index);
}

std::optional<EscapeWindowEvent> EscapeWindow::Step(const Input::InputState& input, App::PixelSize screen,
                                                    double elapsedMs, NewUiTurn turn)
{
    if (!m_open)
    {
        return std::nullopt;
    }
    Place(screen);
    const std::optional<Point> mouse = MouseOfTheTurn(turn, input, screen);
    std::array<bool, 4> clicked{};
    for (std::size_t i = 0; i < m_buttons.size(); ++i)
    {
        clicked[i] = m_buttons[i].Step(input, mouse);
    }
    if (!turn.logic)
    {
        return std::nullopt;
    }

    // 0x647d47: con la cuenta, nada más que la cuenta. El WORD pasa por 0 y por −1 como en el
    // original: con 0, WM_CLOSE sin poner el acumulador en 0.
    if (m_countdown)
    {
        m_elapsed += elapsedMs;
        if (!(m_elapsed > SecondMs))
        {
            return std::nullopt;
        }
        const auto left = static_cast<std::int16_t>(*m_countdown - 1);
        m_countdown = left == NoCountdown ? std::nullopt : std::optional<int>{left};
        if (left == 0)
        {
            return EscapeWindowEvent{.kind = EscapeWindowEvent::Kind::Quit};
        }
        m_elapsed = 0.0;
        return EscapeWindowEvent{.kind = EscapeWindowEvent::Kind::Countdown, .seconds = left};
    }
    if (clicked[ExitButton])
    {
        m_countdown = CountdownStart;
        return EscapeWindowEvent{.kind = EscapeWindowEvent::Kind::Countdown, .seconds = CountdownStart};
    }
    if (clicked[ServerButton])
    {
        Close();
        return EscapeWindowEvent{.kind = EscapeWindowEvent::Kind::JoinAnotherServer};
    }
    if (clicked[OptionsButton])
    {
        Close();
        return EscapeWindowEvent{.kind = EscapeWindowEvent::Kind::Options};
    }
    // El +0x31 del mouse (0x640850) es siempre 0: Esc vale también con un botón abajo.
    if (clicked[CloseButton] || input.TakePress(Input::Key::Escape))
    {
        Close();
        return EscapeWindowEvent{.kind = EscapeWindowEvent::Kind::Closed};
    }
    return std::nullopt;
}

void EscapeWindow::Draw(Render::SpriteBatch& sprites, Scenes::ITextDrawer& text, App::PixelSize screen)
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
    for (const Render::UiImage& image : EscapeFrameImages(m_layout.frame, m_sizes))
    {
        sprites.Image(image);
    }
    for (const Button& button : m_buttons)
    {
        if (!button.Enabled())
        {
            continue;
        }
        sprites.Image(button.Image(m_sizes));
        static_cast<void>(button.DrawLabel(text, screen));
    }
}

void EscapeWindow::Place(App::PixelSize screen)
{
    m_layout = LayoutEscapeWindow(screen, m_lines, m_sizes(LeftSlot).height);
    for (std::size_t i = 0; i < m_buttons.size(); ++i)
    {
        m_buttons[i].MoveTo(Point{m_layout.buttons[i].x, m_layout.buttons[i].y});
    }
}
} // namespace Client::UI
