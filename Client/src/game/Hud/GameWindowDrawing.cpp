#include "Hud/GameWindowDrawing.h"

#include "Core/PixelSize.h"
#include "Math/X87.h"
#include "Render/OriginalQuad.h"
#include "Render/SpriteBatch.h"
#include "Render/SpriteVertex.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Ui/GameHudContext.h"
#include "Render/Frame/ITextDrawer.h"
#include "Kernel/WorldRuntime.h"
#include "Ui/HudPrimitives.h"
#include "Ui/Widget.h"

#include <cstdint>
#include <string_view>

namespace Client::Ui::WindowDrawing
{
namespace
{
constexpr Render::Color White{1.0f, 1.0f, 1.0f, 1.0f};

// Interface\Item_Back01.jpg, Item_Back02.jpg and exit_01.jpg (0x61ae60).
constexpr std::uint16_t WindowTopSlot = 0x104;
constexpr std::uint16_t WindowBottomSlot = 0x105;
constexpr std::uint16_t CloseSlot = 0x118;
// 0x5ee220: 190 × 256 on top (0x6b8840 the sum) and 190 × 177 below.
constexpr float WindowWidth = 190.0f;
constexpr float WindowTopHeight = 256.0f;
constexpr float WindowBottomHeight = 177.0f;
constexpr float WindowDu = 0.7421875f;
constexpr float WindowBottomDv = 0.69140625f;
// The close button: 25 (0x6b8808) and 395 (0x6b8804) from the corner, 24 (0x6b8800) a side, and the help 13 higher.
constexpr double CloseLeft = 25.0;
constexpr double CloseTop = 395.0;
constexpr float CloseSize = 24.0f;
constexpr float CloseUv = 0.75f;
constexpr std::int32_t HelpAbove = 0xd;
// 0x6c8e5c and 0x6c8e64 of the close button's help (0x5f1296, 0x40a861).
constexpr Render::Text::TextColors CloseHelpColors{.text = 0xffffffffu, .background = 0xff000000u};
// 0x5f0cd6..0x5f0cef: the title box's width, 120 · W / 640 unsigned.
constexpr std::uint32_t TitleBoxDesignWidth = 120;
constexpr std::uint32_t DesignWidth = 640;

using Math::F32;
using Math::Truncate;

// What 0x554180 and 0x5542f0 measure after drawing: GetTextExtentPoint32 with the DC's font, the width clipped to
// `limit` if positive and smaller, and both scaled to 640 × 480 (fdiv by rx and ry and _ftol).
void KeepTheMeasure(Ui::GameHudContext& context, std::string_view text, std::int32_t limit)
{
    Kernel::HudState& hud = context.hud;
    const Render::Text::TextExtent extent = context.text.Measure(hud.dcFont, text);
    std::int32_t width = extent.width;
    if (limit > 0 && width > limit)
        width = limit;
    const Core::PixelSize screen = context.sprites.Screen();
    hud.arrowsTextWidth = Render::Text::DesignX(width, screen);
    hud.arrowsTextHeight = Render::Text::DesignY(extent.height, screen);
}

// fild of the mouse against the float box: left ≤ x < left + side, with the sum on the x87.
bool MouseInSquare(const Ui::Point mouse, float left, float top, float size)
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

void DrawTheFrame(Render::SpriteBatch& sprites, float x, float y, float width, float height, bool filled)
{
    // 0x471532..0x47155e: the glColor4ub(0x92, 0x90, 0x8d, 0xc8) box and 0x459ec0.
    constexpr Render::Color FrameFill{
        .red = 146.0f / 255.0f, .green = 144.0f / 255.0f, .blue = 141.0f / 255.0f, .alpha = 200.0f / 255.0f};
    if (filled)
    {
        sprites.SetColor(FrameFill);
        sprites.ColorQuad(x, y, width, height);
        sprites.SetColor(White);
        sprites.EnableTexture();
    }
    constexpr std::uint16_t FrameRowSlot = 0x117;
    constexpr std::uint16_t FrameSideSlot = 0x104;
    constexpr float FrameLine = 2.0f;
    constexpr float FrameRowU = 0.0390625f;
    constexpr float FrameRowV = 0.3125f;
    constexpr float FrameRowDu = 0.6640625f;
    constexpr float FrameRowDv = 0.125f;
    constexpr float FrameSideU = 0.00390625f;
    constexpr float FrameSideV = 0.3125f;
    constexpr float FrameSideDu = 0.0078125f;
    constexpr float FrameSideDv = 0.48828125f;
    const auto quad =
        [&sprites](std::uint16_t slot, float left, float top, float w, float h, float u, float v, float du, float dv)
    {
        sprites.Quad(Render::OriginalQuad{.slot = Render::TextureSlot{slot},
                                          .x = left,
                                          .y = top,
                                          .width = w,
                                          .height = h,
                                          .u = u,
                                          .v = v,
                                          .du = du,
                                          .dv = dv,
                                          .scaleSize = true,
                                          .scalePosition = true,
                                          .alpha = 0.0f});
    };
    const auto f32 = [](double value) { return static_cast<float>(value); };
    quad(FrameRowSlot, x, y, width, FrameLine, FrameRowU, FrameRowV, FrameRowDu, FrameRowDv);
    quad(FrameRowSlot, x, f32(static_cast<double>(y) + static_cast<double>(height)),
         f32(static_cast<double>(width) + 1.0), FrameLine, FrameRowU, FrameRowV, FrameRowDu, FrameRowDv);
    quad(FrameSideSlot, x, y, FrameLine, height, FrameSideU, FrameSideV, FrameSideDu, FrameSideDv);
    quad(FrameSideSlot, f32(static_cast<double>(x) + static_cast<double>(width)), y, FrameLine, height, FrameSideU,
         FrameSideV, FrameSideDu, FrameSideDv);
}

void DrawTheWindowBack(Render::SpriteBatch& sprites, std::int32_t x, std::int32_t y)
{
    const auto left = static_cast<float>(x);
    const auto top = static_cast<float>(y);
    Quad(sprites, WindowTopSlot, left, top, WindowWidth, WindowTopHeight, WindowDu, 1.0f);
    Quad(sprites, WindowBottomSlot, left, F32(static_cast<double>(top) + static_cast<double>(WindowTopHeight)),
         WindowWidth, WindowBottomHeight, WindowDu, WindowBottomDv);
}

void SelectFont(Ui::GameHudContext& context, Render::Text::FontKind font)
{
    context.hud.dcFont = font;
}

void DrawText(Ui::GameHudContext& context, std::int32_t x, std::int32_t y, std::string_view text)
{
    Kernel::HudState& hud = context.hud;
    if (!text.empty())
        context.text.Draw(x, y, text, Render::Text::TextStyle{.font = hud.dcFont, .colors = hud.textColors});
    KeepTheMeasure(context, text, 0);
}

std::int32_t BoxWidth(const Ui::GameHudContext& context, std::uint32_t designWidth)
{
    const auto screenWidth = static_cast<std::uint32_t>(context.sprites.Screen().width);
    return static_cast<std::int32_t>(screenWidth * designWidth / DesignWidth);
}

void DrawInBox(Ui::GameHudContext& context, std::int32_t x, std::int32_t y, std::string_view text, std::int32_t box)
{
    Kernel::HudState& hud = context.hud;
    // 0x554180: 0x465950 with a text or with a box. 0x467060 in mode 2 (0x46711b..0x467132): the box of
    // cx + 2 · ((box − cx) / 2) columns from _ftol(x · rx), with the whole background, and the text shifted (box − cx)
    // / 2.
    if (!text.empty() || box != 0)
    {
        const Render::Text::CenteredInBox placed = Render::Text::CenterInBox(context.text, hud.dcFont, text, box);
        context.text.DrawColumns(x, y, text, Render::Text::TextStyle{.font = hud.dcFont, .colors = hud.textColors},
                                 -placed.textColumn, placed.width);
    }
    KeepTheMeasure(context, text, box);
}

void DrawInBox(Ui::GameHudContext& context, std::int32_t x, std::int32_t y, std::string_view text,
               std::uint32_t designWidth)
{
    DrawInBox(context, x, y, text, BoxWidth(context, designWidth));
}

void DrawClipped(Ui::GameHudContext& context, std::int32_t x, std::int32_t y, std::string_view text, std::int32_t width)
{
    Kernel::HudState& hud = context.hud;
    const Render::Text::TextStyle style{.font = hud.dcFont, .colors = hud.textColors};
    if (width <= 0)
    {
        if (!text.empty())
            context.text.Draw(x, y, text, style);
    }
    else
    {
        // The cx of GetTextExtentPoint32A in 0x467060: an empty text measures "0" (0x4670ba..0x4670d0).
        const std::int32_t measured =
            context.text.Measure(hud.dcFont, text.empty() ? std::string_view("0") : text).width;
        context.text.DrawColumns(x, y, text, style, width < measured ? measured - width : 0, width);
    }
    KeepTheMeasure(context, text, width);
}

void DrawTitle(Ui::GameHudContext& context, std::int32_t x, std::int32_t y, std::string_view text)
{
    DrawInBox(context, x, y, text, BoxWidth(context, TitleBoxDesignWidth));
}

void DrawCenteredText(Ui::GameHudContext& context, std::int32_t x, std::int32_t y, std::string_view text)
{
    const Render::Text::TextExtent extent = context.text.Measure(context.hud.dcFont, text);
    DrawText(context, Render::Text::CenteredAt(x, extent, context.sprites.Screen()), y, text);
}

bool DrawTheCloseButton(Ui::GameHudContext& context, std::int32_t x, std::int32_t y)
{
    Render::SpriteBatch& sprites = context.sprites;
    sprites.SetColor(White);
    const float left = F32(static_cast<double>(x) + CloseLeft);
    const float top = F32(static_cast<double>(y) + CloseTop);
    Quad(sprites, CloseSlot, left, top, CloseSize, CloseSize, CloseUv, CloseUv);
    return MouseInSquare(context.mouse, left, top, CloseSize);
}

void DrawTheCloseHelp(Ui::GameHudContext& context, std::int32_t x, std::int32_t y, std::string_view help)
{
    const float left = F32(static_cast<double>(x) + CloseLeft);
    const float top = F32(static_cast<double>(y) + CloseTop);
    SelectFont(context, Render::Text::FontKind::Normal);
    context.hud.textColors = CloseHelpColors;
    Ui::DrawHelp(context.sprites, context.text, Truncate(static_cast<double>(left)),
                 Truncate(static_cast<double>(top)) - HelpAbove, help);
    // 0x554340 selects the normal font, which stays in the DC, and leaves the text white on a transparent background.
    SelectFont(context, Render::Text::FontKind::Normal);
    context.hud.textColors = Render::Text::TextColors{.text = 0xffffffffu, .background = 0};
}
} // namespace Client::Ui::WindowDrawing
