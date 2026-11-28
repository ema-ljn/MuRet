#include "FrontEnd/EscapeWindow.h"

#include "Core/PixelSize.h"
#include "Tables/TextTable.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "Render/UiImage.h"
#include "Render/Frame/ITextDrawer.h"
#include "Ui/Button.h"
#include "Ui/GameText.h"
#include "Ui/NewUi.h"
#include "Ui/Widget.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Client::FrontEnd
{
namespace
{
// 0x6478c0: the table of 0x66a1c0 (op1_stone, op1_back1 to op1_back4) and the buttons (op1_b_all, four
// frames of 30); 0x61ccb0 uploads them.
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
// 0x66a3d0: the background, 3 inside the frame.
constexpr int FillInset = 3;
constexpr int ButtonWidth = 108;
constexpr int ButtonHeight = 30;
// 0x647c00: the first at 33 from the top, each 4 below the previous one, and the last at 52 from the
// bottom edge.
constexpr int FirstButtonTop = 0x21;
constexpr int ButtonGap = 4;
constexpr int LastButtonFromBottom = 0x34;
// 0x6478c0 with DAT_006d5dc8: 6 lines at login and 10 at selection (the max of 0x66a1c0).
constexpr int LoginLines = 6;
constexpr int SelectionLines = 10;
// +0x408 when the countdown starts, and the milliseconds of each second (0x6baa48).
constexpr std::int16_t CountdownStart = 5;
constexpr std::int16_t NoCountdown = -1;
constexpr double SecondMs = 1000.0;

// The label with 0x63c880 and the colors of 0x6478c0: normal and pressed in 0xffe2e2e2, hovered in
// white, disabled in 0.
constexpr std::array<std::size_t, 4> LabelTexts{381, 382, 385, 388};
constexpr Ui::ButtonColors LabelColors{.normal = 0xffe2e2e2,
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

// The base of 0x669c70 with slot −1: black with alpha 0x80.
constexpr float BaseAlpha = 128.0f / 255.0f;

Ui::Button MakeButton()
{
    return Ui::Button(ButtonSlot, ButtonWidth, ButtonHeight);
}
} // namespace

EscapeWindowLayout LayoutEscapeWindow(Core::PixelSize screen, int lines, int lineHeight)
{
    EscapeWindowLayout layout;
    const int height = lineHeight * lines + TopHeight + BottomHeight;
    layout.frame = Ui::Rect{
        .x = (screen.width - FrameWidth) / 2, .y = (screen.height - height) / 2, .width = FrameWidth, .height = height};
    const int x = (FrameWidth - ButtonWidth) / 2 + layout.frame.x;
    const int step = ButtonHeight + ButtonGap;
    const int top = layout.frame.y + FirstButtonTop;
    const int last = layout.frame.y + height - LastButtonFromBottom;
    layout.buttons[ExitButton] = Ui::Rect{.x = x, .y = top, .width = ButtonWidth, .height = ButtonHeight};
    layout.buttons[ServerButton] = Ui::Rect{.x = x, .y = top + step, .width = ButtonWidth, .height = ButtonHeight};
    layout.buttons[OptionsButton] = Ui::Rect{.x = x, .y = last - step, .width = ButtonWidth, .height = ButtonHeight};
    layout.buttons[CloseButton] = Ui::Rect{.x = x, .y = last, .width = ButtonWidth, .height = ButtonHeight};
    return layout;
}

std::array<Render::UiImage, 5> EscapeFrameImages(Ui::Rect frame, const Ui::TextureSizes& sizes)
{
    return StoneFrameImages(frame, TopSlot, TopHeight, sizes);
}

std::array<Render::UiImage, 5> StoneFrameImages(Ui::Rect frame, Render::TextureSlot topSlot, int topHeight,
                                                const Ui::TextureSizes& sizes)
{
    const int sideHeight = frame.height - topHeight - BottomHeight;
    // The background and the sides follow 0x647370 (0x647260 with 1) and repeat their texture; the edges do not.
    Render::UiImage fill = Ui::PixelImage(StoneSlot,
                                          Ui::Rect{.x = frame.x + FillInset,
                                                   .y = frame.y + FillInset,
                                                   .width = frame.width - 2 * FillInset,
                                                   .height = frame.height - 2 * FillInset},
                                          sizes(StoneSlot));
    fill.resizedWidth = true;
    fill.resizedHeight = true;
    const Render::UiImage top = Ui::PixelImage(
        topSlot, Ui::Rect{.x = frame.x, .y = frame.y, .width = frame.width, .height = topHeight}, sizes(topSlot));
    const Render::UiImage bottom = Ui::PixelImage(
        BottomSlot,
        Ui::Rect{.x = frame.x, .y = frame.y + topHeight + sideHeight, .width = frame.width, .height = BottomHeight},
        sizes(BottomSlot));
    Render::UiImage left = Ui::PixelImage(
        LeftSlot, Ui::Rect{.x = frame.x, .y = frame.y + topHeight, .width = SideWidth, .height = sideHeight},
        sizes(LeftSlot));
    left.resizedHeight = true;
    Render::UiImage right = Ui::PixelImage(
        RightSlot,
        Ui::Rect{
            .x = frame.x + frame.width - SideWidth, .y = frame.y + topHeight, .width = SideWidth, .height = sideHeight},
        sizes(RightSlot));
    right.resizedHeight = true;
    return {fill, top, bottom, left, right};
}

EscapeWindow::EscapeWindow(Ui::TextureSizes sizes, const Tables::TextTable* texts)
    : m_sizes(std::move(sizes)), m_texts(texts), m_buttons{MakeButton(), MakeButton(), MakeButton(), MakeButton()}
{
}

void EscapeWindow::Arm(Ui::NewUiMode mode)
{
    for (std::size_t i = 0; i < m_buttons.size(); ++i)
    {
        m_buttons[i] = MakeButton();
        m_buttons[i].SetLabel(std::string(Ui::TextAt(m_texts, LabelTexts[i]).value_or(std::string_view{})),
                              LabelColors);
    }
    const bool selection = mode == Ui::NewUiMode::Selection;
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

Ui::Rect EscapeWindow::Bounds(Core::PixelSize screen) const
{
    return Ui::Rect{.x = 0, .y = 0, .width = screen.width, .height = screen.height};
}

std::optional<int> EscapeWindow::Countdown() const
{
    return m_countdown;
}

const Ui::Button& EscapeWindow::ButtonAt(std::size_t index) const
{
    return m_buttons.at(index);
}

std::optional<EscapeWindowEvent> EscapeWindow::Step(const Input::InputState& input, Core::PixelSize screen,
                                                    double elapsedMs, Ui::NewUiTurn turn)
{
    if (!m_open)
    {
        return std::nullopt;
    }
    Place(screen);
    const std::optional<Ui::Point> mouse = Ui::MouseOfTheTurn(turn, input, screen);
    std::array<bool, 4> clicked{};
    for (std::size_t i = 0; i < m_buttons.size(); ++i)
    {
        clicked[i] = m_buttons[i].Step(input, mouse, turn.buttonClicked);
    }
    if (!turn.logic)
    {
        return std::nullopt;
    }

    // 0x647d47: with the countdown, nothing but the countdown. The WORD goes through 0 and −1 as in the
    // original: with 0, WM_CLOSE without setting the accumulator to 0.
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
    // The mouse's +0x31 (0x640850) is always 0: Esc also works with a button down.
    const bool escape = input.TakePress(Input::Key::Escape);
    if (escape && turn.buttonClicked)
    {
        turn.buttonClicked();
    }
    if (clicked[CloseButton] || escape)
    {
        Close();
        return EscapeWindowEvent{.kind = EscapeWindowEvent::Kind::Closed};
    }
    return std::nullopt;
}

void EscapeWindow::Draw(Render::SpriteBatch& sprites, Render::Frame::ITextDrawer& text, Core::PixelSize screen)
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
    for (const Ui::Button& button : m_buttons)
    {
        if (!button.Enabled())
        {
            continue;
        }
        sprites.Image(button.Image(m_sizes));
        static_cast<void>(button.DrawLabel(text, screen));
    }
}

void EscapeWindow::Place(Core::PixelSize screen)
{
    m_layout = LayoutEscapeWindow(screen, m_lines, m_sizes(LeftSlot).height);
    for (std::size_t i = 0; i < m_buttons.size(); ++i)
    {
        m_buttons[i].MoveTo(Ui::Point{m_layout.buttons[i].x, m_layout.buttons[i].y});
    }
}
} // namespace Client::FrontEnd
