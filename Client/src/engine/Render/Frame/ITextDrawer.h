#pragma once

#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextBox.h"
#include "Render/Text/TextStyle.h"

#include <string_view>

// Text at (x, y) in 640 × 480, like 0x554180 with TextOut other than 1 (the login's). Draws into the frame batch with
// the current color and blend.
namespace Client::Render::Frame
{
// Measure gives the text's width and height in pixels.
class ITextDrawer : public Render::Text::ITextMeasure
{
public:
    virtual void Draw(int x, int y, std::string_view text, const Render::Text::TextStyle& style) = 0;
    // 0x467060 in mode 6, the Slide.bmd banner's (0x46a816): with negative x, the text shifts −x DIB columns (pixels,
    // although x is in 640 × 480: Q-C519) and is drawn from x 0 with cx + x columns; with x ≥ 0, like Draw.
    virtual void DrawClippedAtLeft(int x, int y, std::string_view text, const Render::Text::TextStyle& style) = 0;
    // 0x467060 with a width (modes 1 and 2 of 0x554180): a box of `columns` columns from x, with the whole background
    // and text column `firstColumn` at its column 0 (Render::Text::TextCache::DrawColumns).
    virtual void DrawColumns(int x, int y, std::string_view text, const Render::Text::TextStyle& style, int firstColumn,
                             int columns) = 0;
    virtual void DrawColumnsAtPixels(int pixelX, int pixelY, std::string_view text,
                                     const Render::Text::TextStyle& style, int firstColumn, int columns) = 0;
    // 0x553e50 and 0x554000, one line of a bubble (0x555b00): the `box` box at screen pixel (x, y), shifted by 0x554000
    // (with 0x77f3b66, `clampY`, also y). With empty text, only the background.
    virtual void DrawBoxed(int x, int y, std::string_view text, const Render::Text::TextStyle& style,
                           const Render::Text::BoxShape& box, bool clampY) = 0;
};
} // namespace Client::Render::Frame
