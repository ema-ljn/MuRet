#include "Render/Gl/GlSpriteRenderer.h"

#include "Core/Expected.h"
#include "Core/Log.h"
#include "Platform/GlLoader.h"
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
// 0x620a00 y 0x620a90: la mezcla normal y la aditiva.
void SetBlendFunc(const Platform::GlFunctions& gl, BlendMode blend)
{
    switch (blend)
    {
    case BlendMode::Alpha:
        gl.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        return;
    case BlendMode::Additive:
        gl.blendFunc(GL_ONE, GL_ONE);
        return;
    }
}
} // namespace

GlSpriteRenderer::GlSpriteRenderer(const Platform::GlFunctions& gl, GlRenderer renderer)
    : m_gl(&gl), m_renderer(std::move(renderer))
{
}

Core::Expected<GlSpriteRenderer, std::string> GlSpriteRenderer::Create(const Platform::GlFunctions& gl)
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
    // 0x6219b0 → 0x620870: el 2D va sin profundidad ni culling, aunque el pase 3D de antes los dejó
    // prendidos (CL6.3).
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
                // insert dice si el slot es nuevo: solo entonces se avisa.
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
