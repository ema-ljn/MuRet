#pragma once

#include "Render/Text/IGlyphRasterizer.h"

#include <cstddef>
#include <string_view>

// El rasterizador falso de los tests del texto (CL5.3): cada byte es una letra de `letterWidth`
// columnas y la celda mide `height` filas, en las cuatro fuentes. La tinta de cada letra es el
// bloque de 3 × 4 de arriba a la izquierda de su celda, así que un test sabe qué píxel tiene letra.
// Cuenta las llamadas, para la caché (CL5.3, PR B).
namespace Client::Tests
{
class FakeGlyphRasterizer final : public Render::Text::IGlyphRasterizer
{
public:
    static constexpr int InkWidth = 3;
    static constexpr int InkHeight = 4;

    explicit FakeGlyphRasterizer(int letterWidth = 6, int height = 13) : m_letterWidth(letterWidth), m_height(height) {}

    [[nodiscard]] Render::Text::TextExtent Measure(Render::Text::FontKind /*font*/,
                                                   std::string_view text) const override
    {
        ++measureCalls;
        return {static_cast<int>(text.size()) * m_letterWidth, m_height};
    }

    void DrawInk(Render::Text::FontKind /*font*/, std::string_view text, int firstColumn,
                 Render::Text::InkMask ink) const override
    {
        ++drawCalls;
        for (std::size_t letter = 0; letter < text.size(); ++letter)
        {
            const int left = static_cast<int>(letter) * m_letterWidth - firstColumn;
            for (int row = 0; row < InkHeight && row < m_height; ++row)
            {
                for (int column = left; column < left + InkWidth; ++column)
                {
                    if (column >= 0 && column < Render::Text::TextBoxWidth && row < Render::Text::TextBoxHeight)
                    {
                        ink[static_cast<std::size_t>(row * Render::Text::TextBoxWidth + column)] = 0xff;
                    }
                }
            }
        }
    }

    mutable int measureCalls = 0;
    mutable int drawCalls = 0;

private:
    int m_letterWidth;
    int m_height;
};
} // namespace Client::Tests
