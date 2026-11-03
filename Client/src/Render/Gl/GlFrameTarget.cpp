#include "Render/Gl/GlFrameTarget.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Core/Log.h"
#include "Platform/GlLoader.h"
#include "Render/Gl/GlSpriteRenderer.h"
#include "Render/Gl/TextureTable.h"
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
    const App::PixelSize first = screen();
    // El constructor es privado: make_unique no lo ve.
    std::unique_ptr<GlFrameTarget> target(
        new GlFrameTarget(gl, textures, std::move(renderer).Value(), std::move(screen), std::move(present), log));
    auto text = Scenes::SceneText::Create(textures, target->m_sprites, std::move(fonts), first.width, log);
    if (!text.HasValue())
    {
        return Core::Unexpected(text.Error());
    }
    target->m_text = std::move(text).Value();
    return target;
}

GlFrameTarget::GlFrameTarget(const Platform::GlFunctions& gl, TextureTable& textures, GlSpriteRenderer renderer,
                             std::function<App::PixelSize()> screen, std::function<void()> present, Core::Log& log)
    : m_gl(&gl), m_textures(textures), m_renderer(std::move(renderer)), m_screen(std::move(screen)),
      m_present(std::move(present)), m_log(log)
{
}

Scenes::SceneCanvas& GlFrameTarget::BeginFrame(Core::Duration frameClock)
{
    const App::PixelSize screen = m_screen();
    m_text->FollowScreenWidth(screen.width);
    m_gl->clearColor(0.0f, 0.0f, 0.0f, 1.0f);
    m_gl->clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    m_sprites.Begin(screen);
    m_canvas.emplace(Scenes::SceneCanvas{.sprites = m_sprites,
                                         .text = m_text->Drawer(),
                                         .world = m_world,
                                         .flushSprites = [this] { FlushSprites(); },
                                         .screen = screen,
                                         .frameClock = frameClock});
    return *m_canvas;
}

void GlFrameTarget::Present()
{
    FlushSprites();
    m_present();
}

void GlFrameTarget::FlushSprites()
{
    if (!m_sprites.Vertices().empty())
    {
        m_renderer.Draw(m_sprites, m_textures, m_log);
    }
    m_sprites.Begin(m_sprites.Screen());
}
} // namespace Client::Render::Gl
