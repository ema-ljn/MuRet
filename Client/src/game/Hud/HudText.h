#pragma once

#include "Core/PixelSize.h"

#include <string_view>

// The loose texts of the game's 2D pass through 0x554180(x, y, text, 0, 0, …) (06 § Texto): with whatever font is in
// the DC (0x57634f4) and the colors of 0x6c8e5c and 0x6c8e64, and the size in 640 × 480 in 0x77f31b8 and 0x77f31bc.
namespace Client::Render::Frame
{
class ITextDrawer;
} // namespace Client::Render::Frame
namespace Client::Kernel
{
struct HudState;
} // namespace Client::Kernel

namespace Client::Ui
{

// 0x554180(x, y, text, 0, 0, …): the text at (x, y) of 640 × 480 if not empty (0x465950 with mode 1), and its size,
// even empty, scaled to 640 × 480 (Render::Text::DesignX and DesignY) into hud.arrowsTextWidth and arrowsTextHeight.
void DrawHudLine(Render::Frame::ITextDrawer& text, Core::PixelSize screen, Kernel::HudState& hud, int x, int y,
                 std::string_view line);

// 0x623690(x, y, text): GetTextExtentPointA with the DC's font and 0x554180 at x − (cx · 640 / W) / 2
// (Render::Text::CenteredAt).
void DrawHudCentered(Render::Frame::ITextDrawer& text, Core::PixelSize screen, Kernel::HudState& hud, int x, int y,
                     std::string_view line);
} // namespace Client::Ui
