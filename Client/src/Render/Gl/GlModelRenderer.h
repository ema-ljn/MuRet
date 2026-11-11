#pragma once

#include "Core/Expected.h"
#include "Core/Log.h"
#include "Platform/GlLoader.h"
#include "Render/Gl/GlName.h"
#include "Render/Gl/GlShader.h"
#include "Render/Gl/TextureTable.h"
#include "Render/ModelBatch.h"

#include <cstdint>
#include <set>
#include <string>

// El ModelBatch de un pase en GL (CL6.3): el shader del 3D de Shaders.h, un VAO con el formato de
// ModelVertex y el VBO que se llena en cada pase. Pone el estado de 0x620db0 (el viewport, la
// perspectiva y la vista, el depth test con GL_LEQUAL, el culling y la profundidad escrita) y corre
// los tramos en orden, cada uno con la mezcla de 0x4ecc30 (06 § El modelo en el binario) y un
// glDrawArrays. Deja el estado como lo deja el último tramo, como el original: el 2D que sigue
// (GlSpriteRenderer) apaga la profundidad y el culling. GL arranca con GL_BACK y GL_CCW, y el
// original no llama a glCullFace ni a glFrontFace: quedan como vienen. Se mueve y no se copia, y
// necesita el contexto vivo.
namespace Client::Render::Gl
{
class GlModelRenderer
{
public:
    // Compila y enlaza el shader y arma el VAO. El error trae el log del compilador de GLSL o el
    // uniform que falta.
    [[nodiscard]] static Core::Expected<GlModelRenderer, std::string> Create(const Platform::GlFunctions& gl);

    // Dibuja `batch` en el framebuffer que esté atado. Un tramo con un slot que `textures` no tiene
    // sale sin textura, con el color de sus vértices (Q-C29: el original ata el nombre 0 de un slot
    // que nunca tuvo textura), y el slot sale una vez en `log` como aviso.
    void Draw(const ModelBatch& batch, const TextureTable& textures, Core::Log& log);

private:
    // Dónde están los uniforms del shader.
    struct Uniforms
    {
        GLint viewProjection = -1;
        GLint textured = -1;
        GLint alphaTest = -1;
        GLint textureAdd = -1;
        GLint fog = -1;
        GLint fogColor = -1;
        GLint fogStart = -1;
        GLint fogEnd = -1;
    };

    GlModelRenderer(const Platform::GlFunctions& gl, GlShader shader, GlName vertexArray, GlName vertexBuffer,
                    Uniforms uniforms);

    // El estado de 0x620db0 para el pase de `batch`, con sus vértices subidos.
    void BeginPass(const ModelBatch& batch) const;
    // La mezcla, el culling, la máscara y el depth test de un tramo.
    void SetDrawState(const ModelDrawState& state) const;
    void SetBlend(ModelBlend blend) const;
    void SetOperation(const ModelDrawState& state, ModelStateOperation operation) const;
    void SetViewProjection(const ModelBatch& batch, ModelCoordinateSpace space) const;
    // El modo de la textura y la niebla de un tramo que dibuja: no son del material, van en cada comando.
    void SetTextureEnvAndFog(const ModelDrawCommand& command) const;

    const Platform::GlFunctions* m_gl;
    GlShader m_shader;
    GlName m_vertexArray;
    GlName m_vertexBuffer;
    Uniforms m_uniforms;
    // Los slots que ya se avisaron.
    std::set<std::uint16_t> m_warnedSlots;
};
} // namespace Client::Render::Gl
