#pragma once

#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"

#include <string_view>

namespace Client::Tests
{
// A text measurer without fonts: 6 × 13 pixels per letter. For the tests that go through 0x555190 (a game message)
// without looking at where it lands.
class LetterMeasure final : public Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Render::Text::TextExtent Measure(Render::Text::FontKind /*font*/,
                                                   std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};
} // namespace Client::Tests
