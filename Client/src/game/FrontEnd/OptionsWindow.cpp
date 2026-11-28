#include "FrontEnd/OptionsWindow.h"

#include "Core/PixelSize.h"
#include "Tables/TextTable.h"
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
#include "Render/Frame/ITextDrawer.h"
#include "Ui/Button.h"
#include "FrontEnd/EscapeWindow.h"
#include "Ui/GameText.h"
#include "Ui/NewUi.h"
#include "Ui/Slider.h"
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
// 0x644120: the table of 0x66a1c0 with op2_back1 on top (0x730, 213 × 65), the checkboxes of op2_ch
// (0x731, two frames of 16), the button of op1_b_all and the sliders: the knob of op2_volume3
// (0x732, 13 × 13), the bar of op2_volume1 (0x734, 98 × 13) and the fill of op2_volume2 (0x733, 4 × 7)
// at (3, 3, 95, 10). 0x61e8xx uploads them.
constexpr Render::TextureSlot TopSlot{0x730};
constexpr Render::TextureSlot LineSlot{0x72d};
constexpr Render::TextureSlot CheckSlot{0x731};
constexpr Render::TextureSlot CloseSlot{0x72f};
constexpr Ui::SliderParts SliderLook{.thumb = {.slot = Render::TextureSlot{0x732}, .width = 13, .height = 13},
                                     .track = {.slot = Render::TextureSlot{0x734}, .width = 98, .height = 13},
                                     .fill = {.slot = Render::TextureSlot{0x733}, .width = 4, .height = 7},
                                     .fillArea = Ui::Rect{.x = 3, .y = 3, .width = 92, .height = 7}};

constexpr int FrameWidth = 213;
constexpr int TopHeight = 65;
constexpr int BottomHeight = 43;
// 0x644302 and vtable + 0x1c: 30 lines, the max 0x66a1c0 gives it.
constexpr int Lines = 30;
constexpr int CheckSize = 16;
constexpr int CheckedFrame = 1;
constexpr int CloseWidth = 108;
constexpr int CloseHeight = 30;
// 0x644520: the checkboxes at 52 from the left and top edges, one every 16 + 25; the first slider,
// 48 below the last checkbox plus the height of one, and the other, 48 plus its height (13) below; the
// button, at 301 from the top.
constexpr int Inset = 0x34;
constexpr int CheckGap = 0x19;
constexpr int SliderGap = 0x30;
constexpr int CloseTop = 0x12d;
// 0x644860: the title at 10 from the top, the checkbox labels 24 to the right and 4 down, and the
// slider labels 18 up, with the value 85 to the right.
constexpr int TitleTop = 10;
constexpr int CheckLabelLeft = 0x18;
constexpr int CheckLabelTop = 4;
constexpr int SliderLabelAbove = 0x12;
constexpr int SliderValueLeft = 0x55;
// 0x6468e0(9) and 0x6468e0(4).
constexpr int VolumeMax = 9;
constexpr int GlowMax = 4;

constexpr std::size_t TitleText = 385;
constexpr std::array<std::size_t, 3> CheckTexts{386, 387, 919};
constexpr std::array<std::size_t, 2> SliderTexts{389, 1840};
constexpr std::size_t CloseText = 388;
// 0x644860 with the font of DAT_05763560, white on 0.
constexpr Render::Text::TextStyle TextLook{Render::Text::FontKind::OtherFace12,
                                           Render::Text::TextColors{0xffffffff, 0}};
// The button's label, with the colors of 0x644378 (those of the Esc window).
constexpr Ui::ButtonColors CloseColors{.normal = 0xffe2e2e2,
                                       .pressed = 0xffe2e2e2,
                                       .hover = 0xffffffff,
                                       .checkedNormal = 0xffe2e2e2,
                                       .checkedPressed = 0xffe2e2e2,
                                       .checkedHover = 0xffffffff,
                                       .disabled = 0,
                                       .checkedDisabled = 0};
// The base of 0x669c70 with slot −1: black with alpha 0x80.
constexpr float BaseAlpha = 128.0f / 255.0f;

Ui::Button MakeCheck()
{
    // 0x63c330(16, 16, 0x731, 2, 0, 0, −1, 1, 1, 1, −1): frame 0 unchecked, pressed or not, and 1
    // checked.
    Ui::Button check(CheckSlot, CheckSize, CheckSize, CheckedFrame);
    check.SetStateFrames(0, 0);
    return check;
}

std::string_view TextOf(const Tables::TextTable* texts, std::size_t index)
{
    return Ui::TextAt(texts, index).value_or(std::string_view{});
}

void DrawAt(Render::Frame::ITextDrawer& text, Ui::Point pixel, std::string_view line, Core::PixelSize screen)
{
    // 0x5542f0 with mode 1 and width 0; it does not draw an empty text.
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
        // 0x64480a: the value's low byte.
        settings.glowDetail = static_cast<std::uint8_t>(event.value & 0xff);
        return;
    case OptionsEvent::Kind::Closed:
        return;
    }
}

OptionsWindowLayout LayoutOptionsWindow(Core::PixelSize screen, int lineHeight)
{
    OptionsWindowLayout layout;
    const int height = lineHeight * Lines + TopHeight + BottomHeight;
    layout.frame = Ui::Rect{
        .x = (screen.width - FrameWidth) / 2, .y = (screen.height - height) / 2, .width = FrameWidth, .height = height};
    const int left = layout.frame.x + Inset;
    int top = layout.frame.y + Inset;
    for (Ui::Rect& check : layout.checks)
    {
        check = Ui::Rect{.x = left, .y = top, .width = CheckSize, .height = CheckSize};
        top += CheckSize + CheckGap;
    }
    const int first = CheckSize + layout.checks[2].y + SliderGap;
    layout.sliders[0] = Ui::Point{left, first};
    layout.sliders[1] = Ui::Point{left, first + SliderLook.thumb.height + SliderGap};
    layout.close = Ui::Rect{.x = (FrameWidth - CloseWidth) / 2 + layout.frame.x,
                            .y = layout.frame.y + CloseTop,
                            .width = CloseWidth,
                            .height = CloseHeight};
    return layout;
}

OptionsWindow::OptionsWindow(Ui::TextureSizes sizes, const Tables::TextTable* texts)
    : m_sizes(std::move(sizes)), m_texts(texts), m_checks{MakeCheck(), MakeCheck(), MakeCheck()},
      m_close(CloseSlot, CloseWidth, CloseHeight), m_sliders{Ui::Slider(SliderLook), Ui::Slider(SliderLook)}
{
}

void OptionsWindow::Arm(const OptionsSettings& settings)
{
    for (Ui::Button& check : m_checks)
    {
        check = MakeCheck();
    }
    m_close = Ui::Button(CloseSlot, CloseWidth, CloseHeight);
    m_close.SetLabel(std::string(TextOf(m_texts, CloseText)), CloseColors);
    m_sliders = {Ui::Slider(SliderLook), Ui::Slider(SliderLook)};
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
    for (Ui::Slider& slider : m_sliders)
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

Ui::Rect OptionsWindow::Bounds(Core::PixelSize screen) const
{
    return Ui::Rect{.x = 0, .y = 0, .width = screen.width, .height = screen.height};
}

const Ui::Button& OptionsWindow::Check(std::size_t index) const
{
    return m_checks.at(index);
}

const Ui::Button& OptionsWindow::CloseButton() const
{
    return m_close;
}

const Ui::Slider& OptionsWindow::SliderAt(std::size_t index) const
{
    return m_sliders.at(index);
}

std::optional<OptionsEvent> OptionsWindow::Step(const Input::InputState& input, Core::PixelSize screen,
                                                double elapsedMs, const OptionsSettings& current, Ui::NewUiTurn turn)
{
    if (!m_open)
    {
        return std::nullopt;
    }
    Place(screen);
    const std::optional<Ui::Point> mouse = Ui::MouseOfTheTurn(turn, input, screen);
    std::array<bool, 3> checked{};
    for (std::size_t i = 0; i < m_checks.size(); ++i)
    {
        checked[i] = m_checks[i].Step(input, mouse, turn.buttonClicked);
    }
    const bool closed = m_close.Step(input, mouse, turn.buttonClicked);
    if (!turn.logic)
    {
        return std::nullopt;
    }
    // 0x644730: the sliders first, with the mouse of 0x640850.
    const Ui::Point raw = Ui::MouseInPixels(input, screen);
    for (Ui::Slider& slider : m_sliders)
    {
        slider.Step(input, raw, elapsedMs, turn.buttonClicked);
    }
    // A checkbox that changed writes its global and ends the step.
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
    // A slider that moves (+9 non-zero) writes its value if it is different, and ends the step
    // even if it is the same.
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
        // 0x6447fe: compares the value's low byte with the global's byte.
        if ((m_sliders[1].Value() & 0xff) == current.glowDetail)
        {
            return std::nullopt;
        }
        return OptionsEvent{.kind = OptionsEvent::Kind::GlowDetail, .value = m_sliders[1].Value()};
    }
    // The mouse's +0x31 (0x640850) is always 0.
    if (input.TakePress(Input::Key::Escape))
    {
        if (turn.buttonClicked)
        {
            turn.buttonClicked();
        }
        Close();
        return OptionsEvent{.kind = OptionsEvent::Kind::Closed};
    }
    return std::nullopt;
}

void OptionsWindow::Draw(Render::SpriteBatch& sprites, Render::Frame::ITextDrawer& text, Core::PixelSize screen,
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
    // The title, centered in the frame's width (0x5542f0 with mode 2), like a button's label.
    if (const std::string_view title = TextOf(m_texts, TitleText); !title.empty())
    {
        const Render::Text::TextExtent extent = text.Measure(TextLook.font, title);
        const int centered = m_layout.frame.x + (m_layout.frame.width - extent.width) / 2;
        text.Draw(Render::Text::DesignX(centered, screen), Render::Text::DesignY(m_layout.frame.y + TitleTop, screen),
                  title, TextLook);
    }
    for (std::size_t i = 0; i < m_checks.size(); ++i)
    {
        const Ui::Rect area = m_checks[i].Area();
        DrawAt(text, Ui::Point{area.x + CheckLabelLeft, area.y + CheckLabelTop}, TextOf(m_texts, CheckTexts[i]),
               screen);
    }
    // The values are the globals': the glow's, 2 · d + 5.
    const std::array<int, 2> values{current.volume, current.glowDetail * 2 + 5};
    for (std::size_t i = 0; i < m_sliders.size(); ++i)
    {
        const Ui::Point at = m_sliders[i].Position();
        DrawAt(text, Ui::Point{at.x, at.y - SliderLabelAbove}, TextOf(m_texts, SliderTexts[i]), screen);
        DrawAt(text, Ui::Point{at.x + SliderValueLeft, at.y - SliderLabelAbove}, std::to_string(values[i]), screen);
        if (m_sliders[i].IsOpen())
        {
            for (const Render::UiImage& image : m_sliders[i].Images(m_sizes))
            {
                sprites.Image(image);
            }
        }
    }
    // 0x66a090: the checkboxes and the button, in the order 0x66a070 added them.
    for (const Ui::Button& check : m_checks)
    {
        sprites.Image(check.Image(m_sizes));
    }
    sprites.Image(m_close.Image(m_sizes));
    static_cast<void>(m_close.DrawLabel(text, screen));
}

void OptionsWindow::Place(Core::PixelSize screen)
{
    m_layout = LayoutOptionsWindow(screen, m_sizes(LineSlot).height);
    for (std::size_t i = 0; i < m_checks.size(); ++i)
    {
        m_checks[i].MoveTo(Ui::Point{m_layout.checks[i].x, m_layout.checks[i].y});
    }
    m_close.MoveTo(Ui::Point{m_layout.close.x, m_layout.close.y});
    for (std::size_t i = 0; i < m_sliders.size(); ++i)
    {
        if (m_sliders[i].Position() != m_layout.sliders[i])
        {
            m_sliders[i].MoveTo(m_layout.sliders[i]);
        }
    }
}
} // namespace Client::FrontEnd
