#include "UI/SelectionBar.h"

#include "App/WindowState.h"
#include "Data/Text/TextTable.h"
#include "Input/InputState.h"
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
#include "UI/GameText.h"
#include "UI/Widget.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

namespace Client::UI
{
namespace
{
// 0x63e820: el adorno (deco.tga, 0xe) y los botones de b_create (0xf), server_menu_b_all (0x10),
// b_connect (0x11) y b_delete (0x12), de 54 × 30; los de crear, conectar y borrar con el cuadro 3
// de deshabilitado.
constexpr Render::TextureSlot DecoSlot{0xe};
constexpr Render::TextureSlot CreateSlot{0xf};
constexpr Render::TextureSlot MenuSlot{0x10};
constexpr Render::TextureSlot ConnectSlot{0x11};
constexpr Render::TextureSlot DeleteSlot{0x12};
constexpr int DisabledFrame = 3;
constexpr int ButtonWidth = 54;
constexpr int ButtonHeight = 30;
constexpr int BarLeft = 22;
constexpr int BarHeight = 54;
constexpr int DecoWidth = 189;
constexpr int DecoHeight = 103;
constexpr int DecoAbove = 46;
constexpr int StripTop = 33;
constexpr int StripHeight = 21;
// La franja de 0x669c70 sin slot y con el alfa de +0x148: 0x8f.
constexpr float StripAlpha = 143.0f / 255.0f;
// El texto 1710 de 0x63ec00, en (W − 464, y + 40), con 0xff27d9ff.
constexpr std::size_t RightText = 1710;
constexpr int RightTextFromRight = 464;
constexpr int TextTop = 40;
constexpr Render::Text::TextStyle RightStyle{Render::Text::FontKind::OtherFace12,
                                             Render::Text::TextColors{0xff27d9ff, 0}};
// Con el +0x543, los textos 436 y 437 centrados en (320, 330) y (320, 348) de 640 × 480
// (0x623690), en negro sobre 0x8000ffff.
constexpr std::size_t BlockedText = 436;
constexpr std::size_t BlockedSiteText = 437;
constexpr int BlockedCenter = 320;
constexpr int BlockedTop = 330;
constexpr int BlockedSiteTop = 348;
constexpr Render::Text::TextStyle BlockedStyle{Render::Text::FontKind::OtherFace12,
                                               Render::Text::TextColors{0xff000000, 0x8000ffff}};
// La y de 0x65f4b0, con el float 0xbf71eb85 (0x6baae0), como la barra del login (MenuBar.cpp).
constexpr float HeightFactor = -0.945f;
static_assert(std::bit_cast<std::uint32_t>(HeightFactor) == 0xbf71eb85U, "el float de 0x6baae0");

Rect ButtonAt(int x, int y)
{
    return Rect{.x = x, .y = y, .width = ButtonWidth, .height = ButtonHeight};
}
} // namespace

SelectionBarLayout LayoutSelectionBar(App::PixelSize screen)
{
    // fild y fmul con el float: la x87 no redondea el producto, y un double lo guarda exacto.
    const int y = -static_cast<int>(static_cast<double>(screen.height) * static_cast<double>(HeightFactor)) - BarHeight;
    const int width = screen.width - 2 * BarLeft;
    SelectionBarLayout layout{};
    layout.bar = Rect{.x = BarLeft, .y = y, .width = width, .height = BarHeight};
    layout.deco = Rect{.x = screen.width - DecoWidth, .y = y - DecoAbove, .width = DecoWidth, .height = DecoHeight};
    layout.strip = Rect{.x = BarLeft, .y = y + StripTop, .width = width, .height = StripHeight};
    layout.create = ButtonAt(BarLeft, y);
    layout.menu = ButtonAt(BarLeft + ButtonWidth + 1, y);
    layout.connect = ButtonAt(BarLeft + width - 2 * ButtonWidth - 1, y);
    layout.remove = ButtonAt(BarLeft + width - ButtonWidth, y);
    return layout;
}

SelectionBar::SelectionBar(TextureSizes sizes, const Data::Text::TextTable* texts)
    : m_sizes(std::move(sizes)), m_texts(texts),
      m_create(CreateSlot, ButtonWidth, ButtonHeight, std::nullopt, DisabledFrame),
      m_menu(MenuSlot, ButtonWidth, ButtonHeight),
      m_connect(ConnectSlot, ButtonWidth, ButtonHeight, std::nullopt, DisabledFrame),
      m_delete(DeleteSlot, ButtonWidth, ButtonHeight, std::nullopt, DisabledFrame)
{
}

void SelectionBar::SetBlockedAccount(bool blocked)
{
    m_blockedAccount = blocked;
}

void SelectionBar::Enable(bool aFreeSlot, bool aChosenOne)
{
    m_create.SetEnabled(aFreeSlot);
    m_connect.SetEnabled(aChosenOne);
    m_delete.SetEnabled(aChosenOne);
}

std::optional<SelectionBarClick> SelectionBar::Step(const Input::InputState& input, App::PixelSize screen)
{
    Place(screen);
    const Point mouse = MouseInPixels(input, screen);
    const bool create = m_create.Step(input, mouse);
    const bool menu = m_menu.Step(input, mouse);
    const bool connect = m_connect.Step(input, mouse);
    const bool remove = m_delete.Step(input, mouse);
    if (connect)
    {
        return SelectionBarClick::Connect;
    }
    if (menu)
    {
        return SelectionBarClick::Menu;
    }
    if (create)
    {
        return SelectionBarClick::Create;
    }
    if (remove)
    {
        return SelectionBarClick::Delete;
    }
    return std::nullopt;
}

void SelectionBar::StepWithoutTheMouse(const Input::InputState& input)
{
    // Un punto que no está en ningún botón: sin encima ni clic, y la captura se suelta con el botón.
    constexpr Point Nowhere{-1, -1};
    for (Button* button : {&m_create, &m_menu, &m_connect, &m_delete})
    {
        static_cast<void>(button->Step(input, Nowhere));
    }
}

bool SelectionBar::Covers(Point mouse, App::PixelSize screen) const
{
    return LayoutSelectionBar(screen).bar.Contains(mouse);
}

void SelectionBar::Draw(Render::SpriteBatch& sprites, Scenes::ITextDrawer& text, App::PixelSize screen)
{
    Place(screen);
    sprites.SetBlend(Render::BlendMode::Alpha);
    sprites.Image(PixelImage(DecoSlot, m_layout.deco, m_sizes(DecoSlot)));
    sprites.Image(
        Render::UiImage{.slot = std::nullopt,
                        .x = m_layout.strip.x,
                        .y = m_layout.strip.y,
                        .width = m_layout.strip.width,
                        .height = m_layout.strip.height,
                        .color = Render::Color{.red = 0.0f, .green = 0.0f, .blue = 0.0f, .alpha = StripAlpha}});
    // 0x620a00(1) y glColor4f(1, 1, 1, 1): la franja dejó la textura apagada y su color (Q-C75).
    sprites.SetBlend(Render::BlendMode::Alpha);
    sprites.SetColor(Render::Color{});
    if (const std::optional<std::string_view> right = TextAt(m_texts, RightText); right && !right->empty())
    {
        text.Draw(Render::Text::DesignX(screen.width - RightTextFromRight, screen),
                  Render::Text::DesignY(m_layout.bar.y + TextTop, screen), *right, RightStyle);
    }
    if (m_blockedAccount)
    {
        for (const auto& [number, top] :
             {std::pair{BlockedText, BlockedTop}, std::pair{BlockedSiteText, BlockedSiteTop}})
        {
            if (const std::optional<std::string_view> line = TextAt(m_texts, number); line && !line->empty())
            {
                const Render::Text::TextExtent extent = text.Measure(BlockedStyle.font, *line);
                text.Draw(Render::Text::CenteredAt(BlockedCenter, extent, screen), top, *line, BlockedStyle);
            }
        }
    }
    sprites.Image(m_create.Image(m_sizes));
    sprites.Image(m_menu.Image(m_sizes));
    sprites.Image(m_connect.Image(m_sizes));
    sprites.Image(m_delete.Image(m_sizes));
}

const Button& SelectionBar::CreateButton() const
{
    return m_create;
}

const Button& SelectionBar::ConnectButton() const
{
    return m_connect;
}

const Button& SelectionBar::DeleteButton() const
{
    return m_delete;
}

void SelectionBar::Place(App::PixelSize screen)
{
    m_layout = LayoutSelectionBar(screen);
    m_create.MoveTo(Point{m_layout.create.x, m_layout.create.y});
    m_menu.MoveTo(Point{m_layout.menu.x, m_layout.menu.y});
    m_connect.MoveTo(Point{m_layout.connect.x, m_layout.connect.y});
    m_delete.MoveTo(Point{m_layout.remove.x, m_layout.remove.y});
}
} // namespace Client::UI
