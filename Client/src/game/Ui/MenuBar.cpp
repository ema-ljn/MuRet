#include "Ui/MenuBar.h"

#include "Core/PixelSize.h"
#include "Input/InputState.h"
#include "Render/SpriteBatch.h"
#include "Render/TextureSlot.h"
#include "Ui/Button.h"
#include "Ui/NewUi.h"
#include "Ui/Widget.h"

#include <bit>
#include <cstdint>
#include <optional>
#include <utility>

namespace Client::Ui
{
namespace
{
// 0x641000: the buttons, from server_menu_b_all (slot 0x10) and server_credit_b_all (0x11), and the deco.tga ornament
// (0x12), 189 × 103 with its point at (105, 59).
constexpr Render::TextureSlot MenuSlot{0x10};
constexpr Render::TextureSlot CreditSlot{0x11};
constexpr Render::TextureSlot OrnamentSlot{0x12};
constexpr int ButtonWidth = 54;
constexpr int ButtonHeight = 30;
constexpr int OrnamentWidth = 189;
constexpr int OrnamentHeight = 103;
constexpr Point OrnamentPoint{105, 59};

// 0x65f300: x = 30, the width is W − 2 · 30, and the y is −11 minus the height times the float 0xbf71eb85 (0x6baae0),
// slightly smaller than 0.945 in absolute value, minus the bar's height.
constexpr int BarLeft = 30;
constexpr int BarBottomOffset = -11;
constexpr float HeightFactor = -0.945f;
static_assert(std::bit_cast<std::uint32_t>(HeightFactor) == 0xbf71eb85U, "el float de 0x6baae0");

// fild and fmul with the float: the x87 does not round the product to float, and a double holds it exactly (an int
// below 2^29 times the float's 24 bits). _ftol truncates toward 0, like the cast.
int HeightTerm(int height)
{
    return static_cast<int>(static_cast<double>(height) * static_cast<double>(HeightFactor));
}
} // namespace

MenuBarLayout LayoutMenuBar(Core::PixelSize screen)
{
    MenuBarLayout layout;
    const int y = BarBottomOffset - HeightTerm(screen.height) - ButtonHeight;
    layout.bar = Rect{.x = BarLeft, .y = y, .width = screen.width - 2 * BarLeft, .height = ButtonHeight};
    layout.menu = Rect{.x = BarLeft, .y = y, .width = ButtonWidth, .height = ButtonHeight};
    layout.credit =
        Rect{.x = layout.bar.x + layout.bar.width - ButtonWidth, .y = y, .width = ButtonWidth, .height = ButtonHeight};
    layout.ornament = Rect{.x = layout.credit.x - OrnamentPoint.x,
                           .y = layout.credit.y - OrnamentPoint.y,
                           .width = OrnamentWidth,
                           .height = OrnamentHeight};
    return layout;
}

MenuBar::MenuBar(TextureSizes sizes)
    : m_sizes(std::move(sizes)), m_menu(MenuSlot, ButtonWidth, ButtonHeight),
      m_credit(CreditSlot, ButtonWidth, ButtonHeight)
{
}

void MenuBar::Open()
{
    if (m_open)
    {
        return;
    }
    m_open = true;
    m_menu = Button(MenuSlot, ButtonWidth, ButtonHeight);
    m_credit = Button(CreditSlot, ButtonWidth, ButtonHeight);
}

void MenuBar::Close()
{
    m_open = false;
}

bool MenuBar::IsOpen() const
{
    return m_open;
}

Rect MenuBar::Bounds(Core::PixelSize screen) const
{
    return LayoutMenuBar(screen).bar;
}

std::optional<MenuBarClick> MenuBar::Step(const Input::InputState& input, Core::PixelSize screen, NewUiTurn turn)
{
    if (!m_open)
    {
        return std::nullopt;
    }
    Place(screen);
    const std::optional<Point> mouse = MouseOfTheTurn(turn, input, screen);
    const bool menu = m_menu.Step(input, mouse, turn.buttonClicked);
    const bool credit = m_credit.Step(input, mouse, turn.buttonClicked);
    if (!turn.logic)
    {
        return std::nullopt;
    }
    if (menu)
    {
        return MenuBarClick::Menu;
    }
    if (credit)
    {
        return MenuBarClick::Credit;
    }
    return std::nullopt;
}

void MenuBar::Draw(Render::SpriteBatch& sprites, Core::PixelSize screen)
{
    if (!m_open)
    {
        return;
    }
    Place(screen);
    sprites.SetBlend(Render::BlendMode::Alpha);
    sprites.Image(PixelImage(OrnamentSlot, m_layout.ornament, m_sizes(OrnamentSlot)));
    sprites.Image(m_menu.Image(m_sizes));
    sprites.Image(m_credit.Image(m_sizes));
}

void MenuBar::Place(Core::PixelSize screen)
{
    m_layout = LayoutMenuBar(screen);
    m_menu.MoveTo(Point{m_layout.menu.x, m_layout.menu.y});
    m_credit.MoveTo(Point{m_layout.credit.x, m_layout.credit.y});
}
} // namespace Client::Ui
