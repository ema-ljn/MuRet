#pragma once

#include "Core/Expected.h"
#include "Core/Log.h"
#include "OpenGLApi.h"
#include "Platform/Sdl/GlLoader.h"
#include "Render/Gl/GlName.h"
#include "Render/Gl/GlShader.h"
#include "Render/Gl/TextureTable.h"
#include "Render/ModelBatch.h"
#include "Render/ModelFog.h"

#include <cstdint>
#include <optional>
#include <set>
#include <string>

// Sets 0x620db0's state (viewport, perspective and view, GL_LEQUAL depth test, culling, depth write) and draws each
// range in order with 0x4ecc30's blend (06 § El modelo en el binario). Like the original, it leaves the last range's
// state (the 2D turns depth and culling off) and never calls glCullFace or glFrontFace: GL's GL_BACK and GL_CCW stay.
namespace Client::Render::Gl
{
class OPENGL_API GlModelRenderer
{
public:
    // The error carries the GLSL compiler log or the missing uniform.
    [[nodiscard]] static Core::Expected<GlModelRenderer, std::string> Create(const Platform::Sdl::GlFunctions& gl);

    // Into the bound framebuffer. A range whose slot `textures` lacks draws untextured with its vertex colors (Q-C29:
    // the original binds name 0 for a slot that never had a texture), and the slot is warned once in `log`.
    void Draw(const ModelBatch& batch, const TextureTable& textures, Core::Log& log);

private:
    struct Uniforms
    {
        GLint viewProjection = -1;
        GLint eyeDepthAxis = -1;
        GLint eyeDepthOffset = -1;
        GLint textured = -1;
        GLint alphaTest = -1;
        GLint textureAdd = -1;
        GLint fog = -1;
        GLint fogColor = -1;
        GLint fogStart = -1;
        GLint fogEnd = -1;
    };

    GlModelRenderer(const Platform::Sdl::GlFunctions& gl, GlShader shader, GlName vertexArray, GlName vertexBuffer,
                    Uniforms uniforms);

    // 0x620db0's state for `batch`'s pass, with its vertices uploaded.
    void BeginPass(const ModelBatch& batch) const;
    void SetDrawState(const ModelDrawState& state) const;
    void SetBlend(ModelBlend blend) const;
    void SetOperation(const ModelDrawState& state, ModelStateOperation operation) const;
    void SetViewProjection(const ModelBatch& batch, ModelCoordinateSpace space) const;
    // Not part of the material: they come with each command.
    void SetTextureEnvAndFog(const ModelDrawCommand& command) const;
    void SetFog(const std::optional<ModelFog>& fog) const;

    const Platform::Sdl::GlFunctions* m_gl;
    GlShader m_shader;
    GlName m_vertexArray;
    GlName m_vertexBuffer;
    Uniforms m_uniforms;
    std::set<std::uint16_t> m_warnedSlots;
};
} // namespace Client::Render::Gl
