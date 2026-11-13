#include "UI/CreditsWindow.h"

#include "App/WindowState.h"
#include "Data/Text/Credits.h"
#include "Input/InputState.h"
#include "Input/Key.h"
#include "Render/ScreenScale.h"
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
#include "UI/NewUi.h"
#include "UI/Widget.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace Client::UI
{
namespace
{
// 0x63f020 y 0x63fc50: las dos mitades de la ilustración (0x6f8 y 0x6f9, de 400 × 400 en (0, 126) y
// (400, 126)), el adorno (deco.tga, 189 × 103, sin escalar), el logo (cr_mu_lo.tga, 290 × 41, en
// (241, 549)), las barras sin textura (800 × 42 en (0, 42 · i)) y el botón de message_close_b_all, de
// 54 × 30 con tres cuadros.
constexpr Render::TextureSlot FirstIllustrationSlot{0x6f8};
constexpr Render::TextureSlot DecoSlot{0x12};
constexpr Render::TextureSlot LogoSlot{0x1a};
constexpr Render::TextureSlot CloseSlot{0x726};
constexpr int IllustrationSize = 400;
constexpr int IllustrationTop = 0x7e;
constexpr int DecoWidth = 0xbd;
constexpr int DecoHeight = 0x67;
constexpr int LogoLeft = 0xf1;
constexpr int LogoTop = 0x225;
constexpr int LogoWidth = 0x122;
constexpr int LogoHeight = 0x29;
constexpr int BarWidth = 0x320;
constexpr int BarHeight = 0x2a;
constexpr int CloseWidth = 0x36;
constexpr int CloseHeight = 0x1e;
constexpr int CloseLeft = 0x7a;
constexpr int CloseTop = 0x3f;
// 0x6ba884, 0x6ba880 y 0x6ba888.
constexpr float PerWidth = 1.0f / 800.0f;
constexpr float PerHeight = 1.0f / 600.0f;
constexpr float DecoHeightFactor = 0.878333330154419f;
// 0x63fe20: la ilustración se funde a 0,1275 de alfa por ms (0x6ba890 y 0x6ba8a0) y se queda 22 s
// (0x6ba898); 8 pares, de im1 a im8.
constexpr double IllustrationFade = 0.1275;
constexpr double IllustrationHoldMs = 22000.0;
constexpr std::uint8_t IllustrationCount = 8;
// 0x6400b0: las barras, a 0,85 por ms (0x6ba8a8 y 0x6ba8b8), y el renglón de los nombres se queda 2,3 s
// (0x6ba8b0).
constexpr double BarFade = 0.85;
constexpr double RowHoldMs = 2300.0;
constexpr std::size_t NameRow = 2;
// Los estados de las dos máquinas: 1 aparece (la barra se va), 2 se queda, 3 se va (la barra vuelve).
constexpr int Appearing = 1;
constexpr int Staying = 2;
constexpr int Leaving = 3;
constexpr int Opaque = 0xff;

// 0x63f6b0: el título y el rol en 0xffe2e2e2, a 20 y 46, centrados en el ancho; los nombres en
// 0xffc1eeff, a 72, en columnas (0x5542f0 con el modo 2).
constexpr int TitleTop = 0x14;
constexpr int RoleTop = 0x2e;
constexpr int NamesTop = 0x48;
constexpr Render::Text::TextStyle TitleStyle{Render::Text::FontKind::Credits, Render::Text::TextColors{0xffe2e2e2, 0}};
constexpr Render::Text::TextStyle NameStyle{Render::Text::FontKind::Credits, Render::Text::TextColors{0xffc1eeff, 0}};

float ScaleOf(int side, float per)
{
    // fild, fmul por el float y fstp a float: el producto en double y redondeado una vez.
    return static_cast<float>(static_cast<double>(side) * static_cast<double>(per));
}

float AlphaOf(std::uint8_t alpha)
{
    return static_cast<float>(alpha) / 255.0f;
}

// 0x5542f0 con el modo 2: `line` centrado en `width` píxeles desde la x de 640 × 480 `designX`, como el
// rótulo de un botón (Button::DrawLabel). No dibuja un texto vacío.
void DrawCentered(Scenes::ITextDrawer& text, int designX, int designY, int width, std::string_view line,
                  const Render::Text::TextStyle& style, App::PixelSize screen)
{
    if (line.empty())
    {
        return;
    }
    const Render::Text::TextExtent extent = text.Measure(style.font, line);
    const auto left = static_cast<int>(Render::ScaleX(static_cast<float>(designX), screen));
    text.Draw(Render::Text::DesignX(left + (width - extent.width) / 2, screen), designY, line, style);
}
} // namespace

CreditsLayout LayoutCredits(App::PixelSize screen)
{
    CreditsLayout layout;
    layout.scaleX = ScaleOf(screen.width, PerWidth);
    layout.scaleY = ScaleOf(screen.height, PerHeight);
    const auto decoBottom =
        static_cast<int>(static_cast<double>(screen.height) * static_cast<double>(DecoHeightFactor));
    layout.deco =
        Rect{.x = screen.width - DecoWidth, .y = decoBottom - DecoHeight, .width = DecoWidth, .height = DecoHeight};
    layout.close =
        Rect{.x = layout.deco.x + CloseLeft, .y = layout.deco.y + CloseTop, .width = CloseWidth, .height = CloseHeight};
    return layout;
}

CreditsWindow::CreditsWindow(TextureSizes sizes, std::span<const Data::Text::CreditEntry> credits)
    : m_sizes(std::move(sizes)), m_credits(credits), m_close(CloseSlot, CloseWidth, CloseHeight)
{
}

void CreditsWindow::Arm()
{
    m_close = Button(CloseSlot, CloseWidth, CloseHeight);
    m_barAlphas.fill(Opaque);
    m_illustrationState = 0;
    m_open = false;
}

CreditsStep CreditsWindow::Open()
{
    m_open = true;
    CreditsStep step;
    // 0x63fbf0.
    m_illustrationHold = 0.0;
    m_illustrationState = Appearing;
    m_illustration = 0;
    LoadTheIllustration(step);
    m_rowStates.fill(Appearing);
    m_rowHold = 0.0;
    m_next = 0;
    m_nameCount = 0;
    NextGroup(step);
    return step;
}

void CreditsWindow::Close()
{
    m_open = false;
    m_illustrationState = 0;
}

bool CreditsWindow::IsOpen() const
{
    return m_open;
}

Rect CreditsWindow::Bounds(App::PixelSize screen) const
{
    return Rect{.x = 0, .y = 0, .width = screen.width, .height = screen.height};
}

CreditsStep CreditsWindow::Step(const Input::InputState& input, App::PixelSize screen, double elapsedMs, NewUiTurn turn)
{
    CreditsStep step;
    if (!m_open)
    {
        return step;
    }
    m_layout = LayoutCredits(screen);
    m_close.MoveTo(Point{m_layout.close.x, m_layout.close.y});
    const bool clicked = m_close.Step(input, MouseOfTheTurn(turn, input, screen));
    if (!turn.logic)
    {
        return step;
    }
    // 0x63f630: el botón cierra sin sonido; Esc (con el +0x31 del mouse, siempre 0) con el 0x19. Y sigue:
    // los renglones se mueven igual en el paso que los cerró.
    if (clicked || input.TakePress(Input::Key::Escape))
    {
        Shut(step);
    }
    for (std::size_t row = 0; row < m_rowStates.size(); ++row)
    {
        StepRow(row, elapsedMs, step);
    }
    StepIllustration(elapsedMs, step);
    return step;
}

void CreditsWindow::Draw(Render::SpriteBatch& sprites, Scenes::ITextDrawer& text, App::PixelSize screen)
{
    if (!m_open)
    {
        return;
    }
    m_layout = LayoutCredits(screen);
    m_close.MoveTo(Point{m_layout.close.x, m_layout.close.y});
    sprites.SetBlend(Render::BlendMode::Alpha);
    // 0x66a050: la base de 0x669c70 con el alfa en 0xff (0x63f13b).
    sprites.Image(Render::UiImage{.slot = std::nullopt,
                                  .x = 0,
                                  .y = 0,
                                  .width = screen.width,
                                  .height = screen.height,
                                  .color = Render::Color{.red = 0.0f, .green = 0.0f, .blue = 0.0f, .alpha = 1.0f}});
    sprites.SetColor(Render::Color{});
    // 0x63f6c8: +0x40, +0xe8, +0x190 y +0x238 (0x647650).
    for (std::uint16_t half = 0; half < 2; ++half)
    {
        const Render::TextureSlot slot{static_cast<std::uint16_t>(FirstIllustrationSlot.value + half)};
        Render::UiImage image = PixelImage(slot,
                                           Rect{.x = IllustrationSize * half,
                                                .y = IllustrationTop,
                                                .width = IllustrationSize,
                                                .height = IllustrationSize},
                                           m_sizes(slot));
        image.scaleX = m_layout.scaleX;
        image.scaleY = m_layout.scaleY;
        image.color.alpha = AlphaOf(m_illustrationAlpha);
        sprites.Image(image);
    }
    sprites.Image(PixelImage(DecoSlot, m_layout.deco, m_sizes(DecoSlot)));
    Render::UiImage logo = PixelImage(
        LogoSlot, Rect{.x = LogoLeft, .y = LogoTop, .width = LogoWidth, .height = LogoHeight}, m_sizes(LogoSlot));
    logo.scaleX = m_layout.scaleX;
    logo.scaleY = m_layout.scaleY;
    sprites.Image(logo);
    DrawTexts(text, screen);
    // 0x63f953: las tres barras, negras (+0x61..+0x63 en 0), con su alfa.
    for (std::size_t row = 0; row < m_barAlphas.size(); ++row)
    {
        sprites.Image(Render::UiImage{
            .slot = std::nullopt,
            .x = 0,
            .y = BarHeight * static_cast<int>(row),
            .width = BarWidth,
            .height = BarHeight,
            .scaleX = m_layout.scaleX,
            .scaleY = m_layout.scaleY,
            .color = Render::Color{.red = 0.0f, .green = 0.0f, .blue = 0.0f, .alpha = AlphaOf(m_barAlphas[row])}});
    }
    sprites.SetColor(Render::Color{});
    // 0x66a090: el botón.
    sprites.Image(m_close.Image(m_sizes));
}

const Button& CreditsWindow::CloseButton() const
{
    return m_close;
}

int CreditsWindow::IllustrationState() const
{
    return m_illustrationState;
}

std::uint8_t CreditsWindow::Illustration() const
{
    return m_illustration;
}

std::uint8_t CreditsWindow::IllustrationAlpha() const
{
    return m_illustrationAlpha;
}

int CreditsWindow::RowState(std::size_t row) const
{
    return m_rowStates.at(row);
}

std::uint8_t CreditsWindow::BarAlpha(std::size_t row) const
{
    return m_barAlphas.at(row);
}

std::size_t CreditsWindow::TitleIndex() const
{
    return m_title;
}

std::size_t CreditsWindow::RoleIndex() const
{
    return m_role;
}

std::span<const std::size_t> CreditsWindow::NameIndices() const
{
    return std::span(m_names).first(m_nameCount);
}

std::size_t CreditsWindow::NextIndex() const
{
    return m_next;
}

void CreditsWindow::Shut(CreditsStep& step)
{
    // 0x63f9a0: 0x65f6c0 la cierra (0x63f5a0 con 0) antes de mandar nada.
    Close();
    ++step.closes;
}

void CreditsWindow::LoadTheIllustration(CreditsStep& step)
{
    // 0x63fc50: las dos mitades del par +0x5c8, transparentes (+0x60 en 0) y visibles.
    step.illustration = m_illustration;
    m_illustrationAlpha = 0;
}

void CreditsWindow::NextGroup(CreditsStep& step)
{
    using Data::Text::CreditKind;
    // 0x63ffe0: con el final, el sonido 0x19 y 0x63f9a0, y sigue leyendo.
    if (KindAt(m_next) == static_cast<std::uint8_t>(CreditKind::End))
    {
        Shut(step);
    }
    if (KindAt(m_next) == static_cast<std::uint8_t>(CreditKind::Title))
    {
        m_title = m_next++;
    }
    if (KindAt(m_next) == static_cast<std::uint8_t>(CreditKind::Role))
    {
        m_role = m_next++;
    }
    std::size_t count = 0;
    while (count < m_names.size() && KindAt(m_next) == static_cast<std::uint8_t>(CreditKind::Name))
    {
        m_names[count++] = m_next++;
    }
    m_nameCount = count;
}

void CreditsWindow::StepRow(std::size_t row, double elapsedMs, CreditsStep& step)
{
    using Data::Text::CreditKind;
    std::uint8_t& alpha = m_barAlphas[row];
    switch (m_rowStates[row])
    {
    case Appearing:
    {
        // _ftol del producto y la resta en 16 bits con signo (0x6401bf..0x6401c9).
        auto left = static_cast<std::int16_t>(alpha - static_cast<int>(elapsedMs * BarFade));
        if (left <= 0)
        {
            m_rowStates[row] = Staying;
            left = 0;
        }
        alpha = static_cast<std::uint8_t>(left & 0xff);
        return;
    }
    case Staying:
    {
        // Solo el de los nombres cuenta: a los 2,3 s se van los nombres, el rol si lo que sigue no es un
        // nombre y el título si tampoco es un rol.
        if (row != NameRow)
        {
            return;
        }
        m_rowHold += elapsedMs;
        if (!(m_rowHold > RowHoldMs))
        {
            return;
        }
        m_rowHold = 0.0;
        m_rowStates[NameRow] = Leaving;
        const std::uint8_t next = KindAt(m_next);
        if (next == static_cast<std::uint8_t>(CreditKind::Name))
        {
            return;
        }
        m_rowStates[1] = Leaving;
        if (next == static_cast<std::uint8_t>(CreditKind::Role))
        {
            return;
        }
        m_rowStates[0] = Leaving;
        return;
    }
    case Leaving:
    {
        auto left = static_cast<std::int16_t>(alpha - static_cast<int>(elapsedMs * -BarFade));
        if (left >= Opaque)
        {
            m_rowStates[row] = Appearing;
            left = Opaque;
            if (row == NameRow)
            {
                NextGroup(step);
            }
        }
        alpha = static_cast<std::uint8_t>(left & 0xff);
        return;
    }
    default:
        return;
    }
}

void CreditsWindow::StepIllustration(double elapsedMs, CreditsStep& step)
{
    switch (m_illustrationState)
    {
    case Appearing:
    {
        auto left = static_cast<std::int16_t>(m_illustrationAlpha - static_cast<int>(elapsedMs * -IllustrationFade));
        if (left >= Opaque)
        {
            m_illustrationState = Staying;
            left = Opaque;
        }
        m_illustrationAlpha = static_cast<std::uint8_t>(left & 0xff);
        return;
    }
    case Staying:
        m_illustrationHold += elapsedMs;
        if (!(m_illustrationHold > IllustrationHoldMs))
        {
            return;
        }
        m_illustrationHold = 0.0;
        m_illustrationState = Leaving;
        return;
    case Leaving:
    {
        auto left = static_cast<std::int16_t>(m_illustrationAlpha - static_cast<int>(elapsedMs * IllustrationFade));
        if (left <= 0)
        {
            // +0x5c8 da la vuelta en 8.
            const auto next = static_cast<std::uint8_t>(m_illustration + 1);
            m_illustration = next == IllustrationCount ? std::uint8_t{0} : next;
            m_illustrationState = Appearing;
            LoadTheIllustration(step);
            left = 0;
        }
        m_illustrationAlpha = static_cast<std::uint8_t>(left & 0xff);
        return;
    }
    default:
        return;
    }
}

std::uint8_t CreditsWindow::KindAt(std::size_t index) const
{
    return Data::Text::CreditKindAt(m_credits, index);
}

void CreditsWindow::DrawTexts(Scenes::ITextDrawer& text, App::PixelSize screen) const
{
    const auto textAt = [this](std::size_t index)
    { return index < m_credits.size() ? std::string_view(m_credits[index].text) : std::string_view{}; };
    const int width = screen.width;
    DrawCentered(text, 0, TitleTop, width, textAt(m_title), TitleStyle, screen);
    DrawCentered(text, 0, RoleTop, width, textAt(m_role), TitleStyle, screen);
    // Las columnas de 640 × 480 de cada cuenta, con el ancho en píxeles: 1 en todo el ancho; 2 en el
    // segundo y el tercer cuarto; 3 en tercios (0, 213 y 426); 4 en cuartos.
    struct Columns
    {
        std::array<int, 4> lefts{};
        int width = 0;
    };
    Columns columns;
    switch (m_nameCount)
    {
    case 1:
        columns = Columns{.lefts = {0, 0, 0, 0}, .width = width};
        break;
    case 2:
        columns = Columns{.lefts = {0xa0, 0x140, 0, 0}, .width = width / 4};
        break;
    case 3:
        columns = Columns{.lefts = {0, 0xd5, 0x1aa, 0}, .width = width / 3};
        break;
    case 4:
        columns = Columns{.lefts = {0, 0xa0, 0x140, 0x1e0}, .width = width / 4};
        break;
    default:
        return;
    }
    for (std::size_t i = 0; i < m_nameCount; ++i)
    {
        DrawCentered(text, columns.lefts[i], NamesTop, columns.width, textAt(m_names[i]), NameStyle, screen);
    }
}
} // namespace Client::UI
