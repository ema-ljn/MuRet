#pragma once

#include "Render/OriginalQuad.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Scenes/ITextDrawer.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// El ITextDrawer de los tests de las escenas (CL5.4): anota cada texto que pide la escena y pone
// en el lote un quad de marca por texto, con el slot TextMarkerSlot y sin alfa, como los quads del
// TextCache: sale con el color, la mezcla y el alpha test que haya en el lote. Así un test ve en
// qué orden y con qué estado sale cada texto. Mide como el rasterizador falso: 6 columnas por
// letra y 13 filas.
namespace Client::Tests
{
// El último slot del texto, que ninguna escena usa.
inline constexpr Render::TextureSlot TextMarkerSlot{static_cast<std::uint16_t>(Render::TextureSlotCount - 1)};

struct RecordedText
{
    int x = 0;
    int y = 0;
    std::string text;
    Render::Text::TextStyle style;
};

class RecordingTextDrawer final : public Scenes::ITextDrawer
{
public:
    static constexpr int LetterWidth = 6;
    static constexpr int Height = 13;

    explicit RecordingTextDrawer(Render::SpriteBatch& batch) : m_batch(batch) {}

    [[nodiscard]] Render::Text::TextExtent Measure(Render::Text::FontKind /*font*/,
                                                   std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * LetterWidth, Height};
    }

    void Draw(int x, int y, std::string_view text, const Render::Text::TextStyle& style) override
    {
        texts.push_back(RecordedText{.x = x, .y = y, .text = std::string(text), .style = style});
        const Render::Text::TextExtent extent = Measure(style.font, text);
        m_batch.Quad(Render::OriginalQuad{.slot = TextMarkerSlot,
                                          .x = static_cast<float>(x),
                                          .y = static_cast<float>(y),
                                          .width = static_cast<float>(extent.width),
                                          .height = static_cast<float>(extent.height),
                                          .scalePosition = true});
    }

    std::vector<RecordedText> texts;

private:
    Render::SpriteBatch& m_batch;
};
} // namespace Client::Tests
