#include "Scenes/GameWindowDrawing.h"

#include "App/WindowState.h"
#include "Render/OriginalQuad.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Scenes/GameHud.h"
#include "Scenes/ITextDrawer.h"
#include "Scenes/WorldRuntime.h"
#include "UI/HudPrimitives.h"
#include "UI/Widget.h"
#include "World/OriginalFtol.h"

#include <cstdint>
#include <string_view>

namespace Client::Scenes::WindowDrawing
{
namespace
{
constexpr Render::Color White{1.0f, 1.0f, 1.0f, 1.0f};

// Interface\Item_Back01.jpg, Item_Back02.jpg y exit_01.jpg (0x61ae60).
constexpr std::uint16_t WindowTopSlot = 0x104;
constexpr std::uint16_t WindowBottomSlot = 0x105;
constexpr std::uint16_t CloseSlot = 0x118;
// 0x5ee220: 190 × 256 arriba (0x6b8840 la suma) y 190 × 177 abajo.
constexpr float WindowWidth = 190.0f;
constexpr float WindowTopHeight = 256.0f;
constexpr float WindowBottomHeight = 177.0f;
constexpr float WindowDu = 0.7421875f;
constexpr float WindowBottomDv = 0.69140625f;
// El botón de cerrar: 25 (0x6b8808) y 395 (0x6b8804) de la esquina, de 24 (0x6b8800) de lado, y la ayuda 13 más
// arriba.
constexpr double CloseLeft = 25.0;
constexpr double CloseTop = 395.0;
constexpr float CloseSize = 24.0f;
constexpr float CloseUv = 0.75f;
constexpr std::int32_t HelpAbove = 0xd;
// 0x6c8e5c y 0x6c8e64 de la ayuda del botón de cerrar (0x5f1296, 0x40a861).
constexpr Render::Text::TextColors CloseHelpColors{.text = 0xffffffffu, .background = 0xff000000u};
// 0x5f0cd6..0x5f0cef: el ancho de la caja del título, 120 · W / 640 sin signo (k · W / 640 en las demás cajas).
constexpr std::uint32_t TitleBoxDesignWidth = 120;
constexpr std::uint32_t DesignWidth = 640;

// La x87 de 53 bits y el fstp en float.
float F32(double value)
{
    return static_cast<float>(value);
}

std::int32_t Truncate(double value)
{
    return World::OriginalFtolLow(value).value_or(0);
}

// Lo que miden 0x554180 y 0x5542f0 después de dibujar: GetTextExtentPoint32 con la fuente del DC, el ancho
// recortado a `limit` si es positivo y menor, y los dos pasados a 640 × 480 (fdiv por rx y ry y _ftol).
void KeepTheMeasure(GameHudContext& context, std::string_view text, std::int32_t limit)
{
    HudState& hud = context.hud;
    const Render::Text::TextExtent extent = context.text.Measure(hud.dcFont, text);
    std::int32_t width = extent.width;
    if (limit > 0 && width > limit)
        width = limit;
    const App::PixelSize screen = context.sprites.Screen();
    hud.arrowsTextWidth = Render::Text::DesignX(width, screen);
    hud.arrowsTextHeight = Render::Text::DesignY(extent.height, screen);
}

// fild del mouse contra la caja en float: izquierda ≤ x < izquierda + lado, con la suma en la x87.
bool MouseInSquare(const UI::Point mouse, float left, float top, float size)
{
    const auto x = static_cast<double>(mouse.x);
    const auto y = static_cast<double>(mouse.y);
    return static_cast<double>(left) <= x && x < static_cast<double>(left) + static_cast<double>(size) &&
           static_cast<double>(top) <= y && y < static_cast<double>(top) + static_cast<double>(size);
}
} // namespace

void Quad(Render::SpriteBatch& sprites, std::uint16_t slot, float x, float y, float width, float height, float du,
          float dv)
{
    sprites.Quad(Render::OriginalQuad{.slot = Render::TextureSlot{slot},
                                      .x = x,
                                      .y = y,
                                      .width = width,
                                      .height = height,
                                      .u = 0.0f,
                                      .v = 0.0f,
                                      .du = du,
                                      .dv = dv,
                                      .scaleSize = true,
                                      .scalePosition = true,
                                      .alpha = 0.0f});
}

void DrawTheWindowBack(Render::SpriteBatch& sprites, std::int32_t x, std::int32_t y)
{
    const auto left = static_cast<float>(x);
    const auto top = static_cast<float>(y);
    Quad(sprites, WindowTopSlot, left, top, WindowWidth, WindowTopHeight, WindowDu, 1.0f);
    Quad(sprites, WindowBottomSlot, left, F32(static_cast<double>(top) + static_cast<double>(WindowTopHeight)),
         WindowWidth, WindowBottomHeight, WindowDu, WindowBottomDv);
}

void SelectFont(GameHudContext& context, Render::Text::FontKind font)
{
    context.hud.dcFont = font;
}

void DrawText(GameHudContext& context, std::int32_t x, std::int32_t y, std::string_view text)
{
    HudState& hud = context.hud;
    if (!text.empty())
        context.text.Draw(x, y, text, Render::Text::TextStyle{.font = hud.dcFont, .colors = hud.textColors});
    KeepTheMeasure(context, text, 0);
}

void DrawTitle(GameHudContext& context, std::int32_t x, std::int32_t y, std::string_view text)
{
    DrawCenteredInBox(context, x, y, text, TitleBoxDesignWidth);
}

void DrawCenteredInBox(GameHudContext& context, std::int32_t x, std::int32_t y, std::string_view text,
                       std::uint32_t designWidth)
{
    HudState& hud = context.hud;
    const App::PixelSize screen = context.sprites.Screen();
    const auto box = static_cast<std::int32_t>(static_cast<std::uint32_t>(screen.width) * designWidth / DesignWidth);
    // 0x554180: 0x465950 con un texto o con caja. 0x467060 en el modo 2 (0x46711b..0x467132): la caja de
    // cx + 2 · ((caja − cx) / 2) columnas desde _ftol(x · rx), con el fondo entero, y el texto corrido (caja − cx) / 2.
    if (!text.empty() || box != 0)
    {
        const Render::Text::CenteredInBox placed = Render::Text::CenterInBox(context.text, hud.dcFont, text, box);
        context.text.DrawColumns(x, y, text, Render::Text::TextStyle{.font = hud.dcFont, .colors = hud.textColors},
                                 -placed.textColumn, placed.width);
    }
    KeepTheMeasure(context, text, box);
}

void DrawCenteredText(GameHudContext& context, std::int32_t x, std::int32_t y, std::string_view text)
{
    const Render::Text::TextExtent extent = context.text.Measure(context.hud.dcFont, text);
    DrawText(context, Render::Text::CenteredAt(x, extent, context.sprites.Screen()), y, text);
}

bool DrawTheCloseButton(GameHudContext& context, std::int32_t x, std::int32_t y)
{
    Render::SpriteBatch& sprites = context.sprites;
    sprites.SetColor(White);
    const float left = F32(static_cast<double>(x) + CloseLeft);
    const float top = F32(static_cast<double>(y) + CloseTop);
    Quad(sprites, CloseSlot, left, top, CloseSize, CloseSize, CloseUv, CloseUv);
    return MouseInSquare(context.mouse, left, top, CloseSize);
}

void DrawTheCloseHelp(GameHudContext& context, std::int32_t x, std::int32_t y, std::string_view help)
{
    const float left = F32(static_cast<double>(x) + CloseLeft);
    const float top = F32(static_cast<double>(y) + CloseTop);
    SelectFont(context, Render::Text::FontKind::Normal);
    context.hud.textColors = CloseHelpColors;
    UI::DrawHelp(context.sprites, context.text, Truncate(static_cast<double>(left)),
                 Truncate(static_cast<double>(top)) - HelpAbove, help);
    // 0x554340 selecciona la normal, que queda en el DC, y deja el texto en blanco sobre un fondo transparente.
    SelectFont(context, Render::Text::FontKind::Normal);
    context.hud.textColors = Render::Text::TextColors{.text = 0xffffffffu, .background = 0};
}
} // namespace Client::Scenes::WindowDrawing
