#pragma once

#include "Render/OriginalQuad.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextBox.h"
#include "Render/Text/TextLayout.h"
#include "Render/Text/TextStyle.h"
#include "Render/TextureSlot.h"
#include "Render/Frame/ITextDrawer.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The scene tests' ITextDrawer: records each text the scene asks for and puts one marker quad per text in the batch,
// with slot TextMarkerSlot and no alpha, like TextCache's quads: it goes out with the color, blending and alpha test
// the batch has, so a test sees in what order and with what state each text goes out. It measures like the fake
// rasterizer: 6 columns per letter and 13 rows.
namespace Client::Tests
{
// The last text slot, which no scene uses.
inline constexpr Render::TextureSlot TextMarkerSlot{
    static_cast<std::uint16_t>(Render::FirstTextTextureSlot.value + Render::TextTextureSlotCount - 1)};

struct RecordedText
{
    int x = 0;
    int y = 0;
    std::string text;
    Render::Text::TextStyle style;
    // Whether it came through DrawClippedAtLeft (the sign's mode 6).
    bool clippedAtLeft = false;
    // The box, if it came through DrawBoxed (a line of a bubble), with x and y in pixels before 0x554000.
    std::optional<Render::Text::BoxShape> box{};
    bool clampY = false;
    // Whether it came through DrawColumns (0x467060 with a width): the text's first column and the box's columns.
    bool windowed = false;
    bool pixelPosition = false;
    int firstColumn = 0;
    int columns = 0;
};

class RecordingTextDrawer final : public Render::Frame::ITextDrawer
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

    // With a negative x, the marker goes from 0 with cx + x of width, and without width there is none.
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

    // The marker goes from x with the box's width, even if the text is empty; without columns there is none.
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

    void DrawColumnsAtPixels(int pixelX, int pixelY, std::string_view text, const Render::Text::TextStyle& style,
                             int firstColumn, int columns) override
    {
        texts.push_back(RecordedText{.x = pixelX,
                                     .y = pixelY,
                                     .text = std::string(text),
                                     .style = style,
                                     .windowed = true,
                                     .pixelPosition = true,
                                     .firstColumn = firstColumn,
                                     .columns = columns});
        if (columns <= 0)
            return;
        m_batch.Quad(Render::OriginalQuad{.slot = TextMarkerSlot,
                                          .x = static_cast<float>(pixelX),
                                          .y = static_cast<float>(pixelY),
                                          .width = static_cast<float>(columns),
                                          .height = static_cast<float>(Height)});
    }

    // The marker goes where 0x554000 (BoxedQuad) puts it, in pixels, even if the text is empty.
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
