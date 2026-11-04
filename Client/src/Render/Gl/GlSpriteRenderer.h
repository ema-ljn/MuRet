#pragma once

#include "Core/Expected.h"
#include "Core/Log.h"
#include "Platform/GlLoader.h"
#include "Render/Gl/GlRenderer.h"
#include "Render/Gl/TextureTable.h"
#include "Render/SpriteBatch.h"

#include <cstdint>
#include <set>
#include <string>

// El SpriteBatch de un frame en GL (CL5.2): sube sus vértices una vez y corre sus tramos en orden,
// cada uno con un glDrawArrays, la mezcla de 0x620a00 o 0x620a90, la textura de su slot (o ninguna)
// y el alpha test del original, el glAlphaFunc(GL_GREATER, 0,25) que el shader hace con un discard
// (06 § Primitivas 2D). Prende la mezcla y la deja prendida, como esas dos funciones, y apaga la
// profundidad y el culling que deja un pase 3D (0x6219b0 → 0x620870). Se mueve y no se copia, y
// necesita el contexto vivo.
namespace Client::Render::Gl
{
class GlSpriteRenderer
{
public:
    // El pipeline de GlRenderer: el error trae el log del compilador de GLSL o el uniform que
    // falta.
    [[nodiscard]] static Core::Expected<GlSpriteRenderer, std::string> Create(const Platform::GlFunctions& gl);

    // Dibuja `batch` en el framebuffer que esté atado, con el viewport y la matriz de su pantalla.
    // Un tramo con un slot que `textures` no tiene no se dibuja, y el slot sale una vez en `log`
    // como aviso: el lote se dibuja en cada frame, y avisar en cada uno llenaría el log.
    void Draw(const SpriteBatch& batch, const TextureTable& textures, Core::Log& log);

private:
    GlSpriteRenderer(const Platform::GlFunctions& gl, GlRenderer renderer);

    const Platform::GlFunctions* m_gl;
    GlRenderer m_renderer;
    // Los slots que ya se avisaron.
    std::set<std::uint16_t> m_warnedSlots;
};
} // namespace Client::Render::Gl
