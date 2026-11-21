#pragma once

#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"

#include <string_view>

namespace Client::Tests
{
// Un medidor de texto sin fuentes: 6 × 13 píxeles por letra. Para los tests que pasan por 0x555190 (un mensaje del
// juego) sin mirar dónde cae.
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
