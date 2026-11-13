#include "Render/Gl/GlFrameTarget.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Core/Log.h"
#include "Platform/GlLoader.h"
#include "Render/Gl/GlModelRenderer.h"
#include "Render/Gl/GlSpriteRenderer.h"
#include "Render/Gl/TextureTable.h"
#include "Render/Pass3D.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/SceneText.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace Client::Render::Gl
{
Core::Expected<std::unique_ptr<GlFrameTarget>, std::string>
GlFrameTarget::Create(const Platform::GlFunctions& gl, TextureTable& textures, Scenes::RasterizerFactory fonts,
                      std::function<App::PixelSize()> screen, std::function<void()> present, Core::Log& log)
{
    auto renderer = GlSpriteRenderer::Create(gl);
    if (!renderer.HasValue())
    {
        return Core::Unexpected(renderer.Error());
    }
    auto models = GlModelRenderer::Create(gl);
    if (!models.HasValue())
    {
        return Core::Unexpected(models.Error());
    }
    const App::PixelSize first = screen();
    // El constructor es privado: make_unique no lo ve.
    std::unique_ptr<GlFrameTarget> target(new GlFrameTarget(gl, textures, std::move(renderer).Value(),
                                                            std::move(models).Value(), std::move(screen),
                                                            std::move(present), log));
    auto text = Scenes::SceneText::Create(textures, target->m_sprites, std::move(fonts), first.width, log);
    if (!text.HasValue())
    {
        return Core::Unexpected(text.Error());
    }
    target->m_text = std::move(text).Value();
    return target;
}

GlFrameTarget::GlFrameTarget(const Platform::GlFunctions& gl, TextureTable& textures, GlSpriteRenderer renderer,
                             GlModelRenderer models, std::function<App::PixelSize()> screen,
                             std::function<void()> present, Core::Log& log)
    : m_gl(&gl), m_textures(textures), m_renderer(std::move(renderer)), m_modelRenderer(std::move(models)),
      m_screen(std::move(screen)), m_present(std::move(present)), m_log(log)
{
}

Scenes::SceneCanvas& GlFrameTarget::BeginFrame(Core::Duration frameClock)
{
    const App::PixelSize screen = m_screen();
    m_text->FollowScreenWidth(screen.width);
    m_gl->clearColor(0.0f, 0.0f, 0.0f, 1.0f);
    // El glClear de la profundidad respeta la máscara, y la mezcla aditiva del frame anterior la pudo
    // dejar apagada. En el original la prende el primer pase de 0x637700, 0x620db0(0, 0, 640, 480),
    // que va antes del glClear y no dibuja nada (06 § Cámara, las dos trazas).
    m_gl->depthMask(GL_TRUE);
    m_gl->clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    m_sprites.Begin(screen);
    m_models.Begin(Render::Pass3D{}, false);
    m_canvas.emplace(Scenes::SceneCanvas{.sprites = m_sprites,
                                         .text = m_text->Drawer(),
                                         .world = m_world,
                                         .models = m_models,
                                         .flushSprites = [this] { FlushSprites(); },
                                         .flushModels = [this] { FlushModels(); },
                                         .screen = screen,
                                         .frameClock = frameClock});
    return *m_canvas;
}

// Lo que quedó de un pase 3D sin su fin se dibuja antes del 2D, que va encima.
void GlFrameTarget::Present()
{
    FlushModels();
    FlushSprites();
    m_present();
}

const Render::Text::ITextMeasure& GlFrameTarget::TextMeasure() const
{
    return m_text->Drawer();
}

void GlFrameTarget::FlushSprites()
{
    if (!m_sprites.Vertices().empty())
    {
        m_renderer.Draw(m_sprites, m_textures, m_log);
    }
    m_sprites.Begin(m_sprites.Screen());
}

void GlFrameTarget::FlushModels()
{
    if (m_models.InitializesMaterial() || !m_models.Commands().empty())
    {
        m_modelRenderer.Draw(m_models, m_textures, m_log);
    }
    const auto state = m_models.State();
    m_models.Begin(m_models.Pass(), false);
    m_models.SetState(state);
}
} // namespace Client::Render::Gl
