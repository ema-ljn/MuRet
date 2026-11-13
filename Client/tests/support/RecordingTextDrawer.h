#pragma once

#include "Render/OriginalQuad.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextBox.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Scenes/ITextDrawer.h"

#include <algorithm>
#include <cstdint>
#include <optional>
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
inline constexpr Render::TextureSlot TextMarkerSlot{
    static_cast<std::uint16_t>(Render::FirstTextTextureSlot.value + Render::TextTextureSlotCount - 1)};

struct RecordedText
{
    int x = 0;
    int y = 0;
    std::string text;
    Render::Text::TextStyle style;
    // Si vino por DrawClippedAtLeft (el modo 6 del cartel).
    bool clippedAtLeft = false;
    // La caja, si vino por DrawBoxed (un renglón de un globo), con la x y la y en píxeles antes de 0x554000.
    std::optional<Render::Text::BoxShape> box{};
    bool clampY = false;
    // Si vino por DrawColumns (0x467060 con un ancho): la primera columna del texto y las de la caja.
    bool windowed = false;
    int firstColumn = 0;
    int columns = 0;
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

    // Con x negativa, la marca va desde 0 con cx + x de ancho, y sin ancho no va.
    void DrawClippedAtLeft(int x, int y, std::string_view text, const Render::Text::TextStyle& style) override
    {
        texts.push_back(RecordedText{.x = x, .y = y, .text = std::string(text), .style = style, .clippedAtLeft = true});
        const int width = Measure(style.font, text).width + std::min(x, 0);
        if (width <= 0)
            return;
        m_batch.Quad(Render::OriginalQuad{.slot = TextMarkerSlot,
                                          .x = static_cast<float>(std::max(x, 0)),
                                          .y = static_cast<float>(y),
                                          .width = static_cast<float>(width),
                                          .height = static_cast<float>(Height),
                                          .scalePosition = true});
    }

    // La marca va desde x con el ancho de la caja, aunque el texto esté vacío; sin columnas no va.
    void DrawColumns(int x, int y, std::string_view text, const Render::Text::TextStyle& style, int firstColumn,
                     int columns) override
    {
        texts.push_back(RecordedText{.x = x,
                                     .y = y,
                                     .text = std::string(text),
                                     .style = style,
                                     .windowed = true,
                                     .firstColumn = firstColumn,
                                     .columns = columns});
        if (columns <= 0)
            return;
        m_batch.Quad(Render::OriginalQuad{.slot = TextMarkerSlot,
                                          .x = static_cast<float>(x),
                                          .y = static_cast<float>(y),
                                          .width = static_cast<float>(columns),
                                          .height = static_cast<float>(Height),
                                          .scalePosition = true});
    }

    // La marca va donde la deja 0x554000 (BoxedQuad), en píxeles, aunque el texto esté vacío.
    void DrawBoxed(int x, int y, std::string_view text, const Render::Text::TextStyle& style,
                   const Render::Text::BoxShape& box, bool clampY) override
    {
        texts.push_back(
            RecordedText{.x = x, .y = y, .text = std::string(text), .style = style, .box = box, .clampY = clampY});
        Render::OriginalQuad quad = Render::Text::BoxedQuad(x, y, box.width, box.rows, clampY, m_batch.Screen());
        quad.slot = TextMarkerSlot;
        m_batch.Quad(quad);
    }

    std::vector<RecordedText> texts;

private:
    Render::SpriteBatch& m_batch;
};
} // namespace Client::Tests
