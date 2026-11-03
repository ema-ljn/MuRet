#include "Scenes/SceneText.h"

#include <utility>

namespace Client::Scenes
{
Core::Expected<std::unique_ptr<SceneText>, std::string> SceneText::Create(Render::ITextureStore& store,
                                                                          Render::SpriteBatch& batch,
                                                                          RasterizerFactory fonts, int screenWidth,
                                                                          Core::Log& log)
{
    auto rasterizer = fonts(screenWidth);
    if (!rasterizer.HasValue())
    {
        return Core::Unexpected(rasterizer.Error());
    }
    // El constructor es privado: make_unique no lo ve.
    return std::unique_ptr<SceneText>(
        new SceneText(store, batch, std::move(fonts), std::move(rasterizer).Value(), screenWidth, log));
}

SceneText::SceneText(Render::ITextureStore& store, Render::SpriteBatch& batch, RasterizerFactory fonts,
                     std::unique_ptr<Render::Text::IGlyphRasterizer> rasterizer, int screenWidth, Core::Log& log)
    : m_fonts(std::move(fonts)), m_log(log), m_rasterizer(std::move(rasterizer)), m_screenWidth(screenWidth),
      m_cache(*m_rasterizer, store), m_drawer(m_cache, batch, log)
{
}

void SceneText::FollowScreenWidth(int screenWidth)
{
    if (screenWidth == m_screenWidth || m_failedWidth == screenWidth)
    {
        return;
    }
    auto rasterizer = m_fonts(screenWidth);
    if (!rasterizer.HasValue())
    {
        m_failedWidth = screenWidth;
        m_log.Error("las fuentes para {} de ancho no abren, siguen las de {}: {}", screenWidth, m_screenWidth,
                    rasterizer.Error());
        return;
    }
    // La caché deja el rasterizador viejo antes de que se destruya.
    std::unique_ptr<Render::Text::IGlyphRasterizer> next = std::move(rasterizer).Value();
    m_cache.ChangeRasterizer(*next);
    m_rasterizer = std::move(next);
    m_screenWidth = screenWidth;
    m_failedWidth.reset();
}

ITextDrawer& SceneText::Drawer()
{
    return m_drawer;
}
} // namespace Client::Scenes
