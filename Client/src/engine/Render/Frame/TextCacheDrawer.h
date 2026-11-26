#pragma once

#include "Core/Expected.h"
#include "Core/Log.h"
#include "EngineApi.h"
#include "Render/SpriteBatch.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextBox.h"
#include "Render/Text/TextCache.h"
#include "Render/Text/TextStyle.h"
#include "Render/Frame/ITextDrawer.h"

#include <set>
#include <string>
#include <string_view>

// A text that cannot be drawn (the store did not upload a box, it does not fit the slots, or making room would evict
// one already in the batch) is skipped and logged once: the same text comes every frame. No message carries the text.
namespace Client::Render::Frame
{
class ENGINE_API TextCacheDrawer final : public ITextDrawer
{
public:
    // The cache, the batch and the log must outlive it.
    TextCacheDrawer(Render::Text::TextCache& cache, Render::SpriteBatch& batch, Core::Log& log);

    [[nodiscard]] Render::Text::TextExtent Measure(Render::Text::FontKind font, std::string_view text) const override;
    void Draw(int x, int y, std::string_view text, const Render::Text::TextStyle& style) override;
    void DrawClippedAtLeft(int x, int y, std::string_view text, const Render::Text::TextStyle& style) override;
    void DrawColumns(int x, int y, std::string_view text, const Render::Text::TextStyle& style, int firstColumn,
                     int columns) override;
    void DrawColumnsAtPixels(int pixelX, int pixelY, std::string_view text, const Render::Text::TextStyle& style,
                             int firstColumn, int columns) override;
    void DrawBoxed(int x, int y, std::string_view text, const Render::Text::TextStyle& style,
                   const Render::Text::BoxShape& box, bool clampY) override;

private:
    void LogOnce(const Core::Expected<void, std::string>& drawn);

    Render::Text::TextCache& m_cache;
    Render::SpriteBatch& m_batch;
    Core::Log& m_log;
    std::set<std::string> m_logged;
};
} // namespace Client::Render::Frame
