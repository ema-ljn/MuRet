#pragma once

#include "Render/Text/FontSet.h"
#include "Render/Text/TextBox.h"

#include <string_view>

// What the original's text asks of GDI (06 § Texto): measuring, like GetTextExtentPointA, and drawing, like TextOutA
// on the DIB. A text is a byte string, as in the original, and each byte is a character: Text.bmd is one byte per
// letter (Q-C67).
namespace Client::Render::Text
{
// GetTextExtentPointA's SIZE: the letters' summed advance and the cell height.
struct TextExtent
{
    int width = 0;
    int height = 0;

    friend bool operator==(const TextExtent&, const TextExtent&) = default;
};

// GetTextExtentPointA on the text DC (0x57634f4): also needed by the step and the packets, like the message area when
// adding a line (0x555190).
class ITextMeasure
{
public:
    virtual ~ITextMeasure() = default;

    // An empty text measures 0 wide and the cell height.
    [[nodiscard]] virtual TextExtent Measure(FontKind font, std::string_view text) const = 0;
};

class IGlyphRasterizer : public ITextMeasure
{
public:
    // TextOutA at (−firstColumn, 0), with the cell at the top (TA_TOP) and no antialiasing: sets 0xff in each `ink`
    // pixel a letter covers. Does not clear the rest: the caller passes it at 0, as 0x467060 clears the DIB.
    virtual void DrawInk(FontKind font, std::string_view text, int firstColumn, InkMask ink) const = 0;
};
} // namespace Client::Render::Text
