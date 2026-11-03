#include "Scenes/TextCacheDrawer.h"

namespace Client::Scenes
{
TextCacheDrawer::TextCacheDrawer(Render::Text::TextCache& cache, Render::SpriteBatch& batch, Core::Log& log)
    : m_cache(cache), m_batch(batch), m_log(log)
{
}

Render::Text::TextExtent TextCacheDrawer::Measure(Render::Text::FontKind font, std::string_view text) const
{
    return m_cache.Measure(font, text);
}

void TextCacheDrawer::Draw(int x, int y, std::string_view text, const Render::Text::TextStyle& style)
{
    const auto drawn = m_cache.Draw(m_batch, x, y, text, style);
    if (!drawn.HasValue() && m_logged.insert(drawn.Error()).second)
    {
        m_log.Error("un texto no se dibuja: {}", drawn.Error());
    }
}
} // namespace Client::Scenes
