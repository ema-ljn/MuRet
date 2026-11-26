#include "Render/Gl/GlSpriteRenderer.h"

#include "Core/Expected.h"
#include "Core/Log.h"
#include "Platform/Sdl/GlLoader.h"
#include "Render/Gl/GlRenderer.h"
#include "Render/Gl/GlTexture.h"
#include "Render/Gl/TextureTable.h"
#include "Render/SpriteBatch.h"

#include <string>
#include <utility>

namespace Client::Render::Gl
{
namespace
{
// 0x620a00, 0x620a90 and 0x620980: normal, additive and no blend.
void SetBlendFunc(const Platform::Sdl::GlFunctions& gl, BlendMode blend)
{
    switch (blend)
    {
    case BlendMode::Alpha:
        gl.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        return;
    case BlendMode::Additive:
        gl.blendFunc(GL_ONE, GL_ONE);
        return;
    case BlendMode::Opaque:
        // 0x620980 disables GL_BLEND: (GL_ONE, GL_ZERO) outputs the quad's color, the same.
        gl.blendFunc(GL_ONE, GL_ZERO);
        return;
    }
}
} // namespace

GlSpriteRenderer::GlSpriteRenderer(const Platform::Sdl::GlFunctions& gl, GlRenderer renderer)
    : m_gl(&gl), m_renderer(std::move(renderer))
{
}

Core::Expected<GlSpriteRenderer, std::string> GlSpriteRenderer::Create(const Platform::Sdl::GlFunctions& gl)
{
    Core::Expected<GlRenderer, std::string> renderer = GlRenderer::Create(gl);
    if (!renderer.HasValue())
    {
        return Core::Unexpected(renderer.Error());
    }
    return GlSpriteRenderer(gl, std::move(renderer).Value());
}

void GlSpriteRenderer::Draw(const SpriteBatch& batch, const TextureTable& textures, Core::Log& log)
{
    m_renderer.Upload(batch.Vertices(), batch.Screen());
    // 0x6219b0 → 0x620870: the 2D runs without depth or culling, even if the previous 3D pass left them on.
    m_gl->disable(GL_DEPTH_TEST);
    m_gl->disable(GL_CULL_FACE);
    m_gl->enable(GL_BLEND);
    for (const DrawCommand& command : batch.Commands())
    {
        const GlTexture* texture = nullptr;
        if (command.texture.has_value())
        {
            texture = textures.Find(*command.texture);
            if (texture == nullptr)
            {
                // Warn once per slot: insert reports whether it is new.
                if (m_warnedSlots.insert(command.texture->value).second)
                {
                    log.Warning("el slot {:#x} no tiene textura: lo que lo usa no se dibuja", command.texture->value);
                }
                continue;
            }
        }
        SetBlendFunc(*m_gl, command.blend);
        m_renderer.DrawRange(command.firstVertex, command.vertexCount, texture, command.alphaTest);
    }
}
} // namespace Client::Render::Gl
