#include "Scenes/HudText.h"

#include "Core/PixelSize.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Render/Frame/ITextDrawer.h"
#include "Kernel/WorldRuntime.h"

#include <string_view>

namespace Client::Scenes
{
void DrawHudLine(Render::Frame::ITextDrawer& text, Core::PixelSize screen, Kernel::HudState& hud, int x, int y,
                 std::string_view line)
{
    if (!line.empty())
        text.Draw(x, y, line, Render::Text::TextStyle{.font = hud.dcFont, .colors = hud.textColors});
    const Render::Text::TextExtent extent = text.Measure(hud.dcFont, line);
    hud.arrowsTextWidth = Render::Text::DesignX(extent.width, screen);
    hud.arrowsTextHeight = Render::Text::DesignY(extent.height, screen);
}

void DrawHudCentered(Render::Frame::ITextDrawer& text, Core::PixelSize screen, Kernel::HudState& hud, int x, int y,
                     std::string_view line)
{
    const Render::Text::TextExtent extent = text.Measure(hud.dcFont, line);
    DrawHudLine(text, screen, hud, Render::Text::CenteredAt(x, extent, screen), y, line);
}
} // namespace Client::Scenes
