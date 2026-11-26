#include "Render/Gl/GlModelRenderer.h"

#include "Core/Expected.h"
#include "Core/Log.h"
#include "Platform/Sdl/GlLoader.h"
#include "Render/Gl/GlName.h"
#include "Render/Gl/GlShader.h"
#include "Render/Gl/GlTexture.h"
#include "Render/Gl/Shaders.h"
#include "Render/Gl/TextureTable.h"
#include "Render/ModelBatch.h"
#include "Render/ModelFog.h"
#include "Render/ModelVertex.h"
#include "Render/Pass3D.h"
#include "Render/TextureEnvMode.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstddef>
#include <initializer_list>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace Client::Render::Gl
{
namespace
{
// The VBO is a raw copy of the ModelVertex array: nine packed floats.
static_assert(std::is_standard_layout_v<Render::ModelVertex>);
static_assert(sizeof(Render::ModelVertex) == 9 * sizeof(float));

// glVertexAttribPointer takes the offset inside the VBO as a pointer.
const void* BufferOffset(std::size_t bytes)
{
    return reinterpret_cast<const void*>(bytes);
}

void DescribeVertices(const Platform::Sdl::GlFunctions& gl)
{
    constexpr auto stride = static_cast<GLsizei>(sizeof(Render::ModelVertex));
    gl.vertexAttribPointer(ModelPositionLocation, 3, GL_FLOAT, GL_FALSE, stride,
                           BufferOffset(offsetof(Render::ModelVertex, position)));
    gl.enableVertexAttribArray(ModelPositionLocation);
    gl.vertexAttribPointer(ModelUvLocation, 2, GL_FLOAT, GL_FALSE, stride,
                           BufferOffset(offsetof(Render::ModelVertex, uv)));
    gl.enableVertexAttribArray(ModelUvLocation);
    gl.vertexAttribPointer(ModelColorLocation, 4, GL_FLOAT, GL_FALSE, stride,
                           BufferOffset(offsetof(Render::ModelVertex, color)));
    gl.enableVertexAttribArray(ModelColorLocation);
}
} // namespace

GlModelRenderer::GlModelRenderer(const Platform::Sdl::GlFunctions& gl, GlShader shader, GlName vertexArray,
                                 GlName vertexBuffer, Uniforms uniforms)
    : m_gl(&gl), m_shader(std::move(shader)), m_vertexArray(std::move(vertexArray)),
      m_vertexBuffer(std::move(vertexBuffer)), m_uniforms(uniforms)
{
}

Core::Expected<GlModelRenderer, std::string> GlModelRenderer::Create(const Platform::Sdl::GlFunctions& gl)
{
    Core::Expected<GlShader, std::string> shader = GlShader::Create(gl, ModelVertexShader, ModelFragmentShader);
    if (!shader.HasValue())
    {
        return Core::Unexpected(shader.Error());
    }
    const GlShader& program = shader.Value();
    const Uniforms uniforms{.viewProjection = program.UniformLocation(ModelViewProjectionUniform),
                            .eyeDepthAxis = program.UniformLocation(ModelEyeDepthAxisUniform),
                            .eyeDepthOffset = program.UniformLocation(ModelEyeDepthOffsetUniform),
                            .textured = program.UniformLocation(ModelTexturedUniform),
                            .alphaTest = program.UniformLocation(ModelAlphaTestUniform),
                            .textureAdd = program.UniformLocation(ModelTextureAddUniform),
                            .fog = program.UniformLocation(ModelFogUniform),
                            .fogColor = program.UniformLocation(ModelFogColorUniform),
                            .fogStart = program.UniformLocation(ModelFogStartUniform),
                            .fogEnd = program.UniformLocation(ModelFogEndUniform)};
    for (const auto& [location, name] :
         {std::pair{uniforms.viewProjection, ModelViewProjectionUniform},
          std::pair{uniforms.eyeDepthAxis, ModelEyeDepthAxisUniform},
          std::pair{uniforms.eyeDepthOffset, ModelEyeDepthOffsetUniform},
          std::pair{uniforms.textured, ModelTexturedUniform}, std::pair{uniforms.alphaTest, ModelAlphaTestUniform},
          std::pair{uniforms.textureAdd, ModelTextureAddUniform}, std::pair{uniforms.fog, ModelFogUniform},
          std::pair{uniforms.fogColor, ModelFogColorUniform}, std::pair{uniforms.fogStart, ModelFogStartUniform},
          std::pair{uniforms.fogEnd, ModelFogEndUniform}})
    {
        if (location < 0)
        {
            return Core::Unexpected(std::string("el shader del 3D no tiene el uniform ") + name);
        }
    }

    GlName vertexArray = GenVertexArray(gl);
    GlName vertexBuffer = GenBuffer(gl);
    gl.bindVertexArray(vertexArray.Get());
    gl.bindBuffer(GL_ARRAY_BUFFER, vertexBuffer.Get());
    DescribeVertices(gl);
    gl.bindVertexArray(0);
    return GlModelRenderer(gl, std::move(shader).Value(), std::move(vertexArray), std::move(vertexBuffer), uniforms);
}

void GlModelRenderer::Draw(const ModelBatch& batch, const TextureTable& textures, Core::Log& log)
{
    BeginPass(batch);
    auto space = ModelCoordinateSpace::World;
    for (const ModelDrawCommand& command : batch.Commands())
    {
        if (command.operation == ModelStateOperation::Fog)
        {
            SetFog(command.fog);
            continue;
        }
        if (command.operation == ModelStateOperation::TextureEnv)
        {
            m_gl->uniform1i(m_uniforms.textureAdd, command.textureEnv == TextureEnvMode::Add ? 1 : 0);
            continue;
        }
        if (!command.state.HasValidTextureBinding())
        {
            log.Error("un comando 3D pide slot y nombre físico a la vez; se rechaza el comando ambiguo");
            continue;
        }
        if (command.operation != ModelStateOperation::Draw && command.operation != ModelStateOperation::Binding)
        {
            SetOperation(command.state, command.operation);
            continue;
        }
        const GlTexture* texture = nullptr;
        if (command.state.texture.has_value())
        {
            texture = textures.Find(*command.state.texture);
            // Warn once per slot: insert reports whether it is new.
            if (texture == nullptr && m_warnedSlots.insert(command.state.texture->value).second)
            {
                log.Warning("el slot {:#x} no tiene textura: lo que lo usa sale sin textura (Q-C29)",
                            command.state.texture->value);
            }
        }
        if (command.operation == ModelStateOperation::Draw)
            SetDrawState(command.state);
        if (texture != nullptr)
        {
            m_gl->bindTexture(GL_TEXTURE_2D, texture->Name());
        }
        else if (command.state.rawTextureName.has_value())
        {
            m_gl->bindTexture(GL_TEXTURE_2D, *command.state.rawTextureName);
        }
        const bool rawTextured = command.state.rawTextureName.has_value() && *command.state.rawTextureName != 0;
        m_gl->uniform1i(m_uniforms.textured,
                        (texture != nullptr || rawTextured) && command.state.texture2d.value_or(true) ? 1 : 0);
        if (command.operation == ModelStateOperation::Binding)
            continue;
        m_gl->uniform1i(m_uniforms.alphaTest,
                        command.state.alphaTest.value_or(command.state.blend == ModelBlend::Alpha) ? 1 : 0);
        SetTextureEnvAndFog(command);
        if (space != command.state.space)
        {
            space = command.state.space;
            SetViewProjection(batch, space);
        }
        m_gl->drawArrays(GL_TRIANGLES, static_cast<GLint>(command.firstVertex),
                         static_cast<GLsizei>(command.vertexCount));
    }
}

// Matrices/VBO of each batch. Only the 620db0 pass resets the material; 6213e0 and the flushes do not.
void GlModelRenderer::BeginPass(const ModelBatch& batch) const
{
    const Pass3D& pass = batch.Pass();
    m_gl->viewport(pass.viewport.x, pass.viewport.y, pass.viewport.z, pass.viewport.w);
    m_gl->useProgram(m_shader.Program());
    m_gl->bindVertexArray(m_vertexArray.Get());
    m_gl->bindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer.Get());
    m_gl->bufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(batch.Vertices().size_bytes()), batch.Vertices().data(),
                     GL_STREAM_DRAW);
    SetViewProjection(batch, ModelCoordinateSpace::World);
    if (batch.InitializesMaterial())
    {
        // 620eea..620f4c: alpha off; texture/depth/cull on; depth write and LEQUAL.
        m_gl->uniform1i(m_uniforms.alphaTest, 0);
        m_gl->uniform1i(m_uniforms.textured, 1);
        // 620f52..620fa5: the zero policy disables fog even in an empty pass.
        m_gl->uniform1i(m_uniforms.fog, 0);
        m_gl->enable(GL_DEPTH_TEST);
        m_gl->depthFunc(GL_LEQUAL);
        m_gl->enable(GL_CULL_FACE);
        m_gl->depthMask(GL_TRUE);
        // 0x495949: the card portrait clears depth with the mask already on (0x620890).
        if (pass.clearDepth)
            m_gl->clear(GL_DEPTH_BUFFER_BIT);
    }
}

void GlModelRenderer::SetViewProjection(const ModelBatch& batch, ModelCoordinateSpace space) const
{
    const Pass3D& pass = batch.Pass();
    const glm::mat4 matrix = space == ModelCoordinateSpace::Eye ? pass.projection : pass.projection * pass.view;
    // GL_FALSE: GLM is column-major. The space belongs to the command, even in World/Eye/World.
    m_gl->uniformMatrix4fv(m_uniforms.viewProjection, 1, GL_FALSE, glm::value_ptr(matrix));
    // Q-C391: eye |z| also in orthographic, where clip.w is always 1.
    // GLM indexes column/row; the Eye pass already has the view applied to its vertices.
    if (space == ModelCoordinateSpace::Eye)
    {
        m_gl->uniform3f(m_uniforms.eyeDepthAxis, 0.0f, 0.0f, 1.0f);
        m_gl->uniform1f(m_uniforms.eyeDepthOffset, 0.0f);
        return;
    }
    m_gl->uniform3f(m_uniforms.eyeDepthAxis, pass.view[0][2], pass.view[1][2], pass.view[2][2]);
    m_gl->uniform1f(m_uniforms.eyeDepthOffset, pass.view[3][2]);
}

// glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE) and glFog (0x42b0a0) or glDisable(GL_FOG): the command carries both,
// and without fog the color, start and end are unused.
void GlModelRenderer::SetTextureEnvAndFog(const ModelDrawCommand& command) const
{
    m_gl->uniform1i(m_uniforms.textureAdd, command.textureEnv == TextureEnvMode::Add ? 1 : 0);
    SetFog(command.fog);
}

void GlModelRenderer::SetFog(const std::optional<ModelFog>& fog) const
{
    if (!fog.has_value())
    {
        m_gl->uniform1i(m_uniforms.fog, 0);
        return;
    }
    const ModelFog& enabled = *fog;
    m_gl->uniform1i(m_uniforms.fog, 1);
    m_gl->uniform3f(m_uniforms.fogColor, enabled.color.x, enabled.color.y, enabled.color.z);
    m_gl->uniform1f(m_uniforms.fogStart, enabled.start);
    m_gl->uniform1f(m_uniforms.fogEnd, enabled.end);
}

// 0x620980, 0x620a00(1), 0x620a90, the two shadows of 0x4eff40, 0x620b10 and 0x620870 with flag 0x2000.
void GlModelRenderer::SetDrawState(const ModelDrawState& state) const
{
    SetBlend(state.blend);
    const bool legacyCull = state.blend == ModelBlend::Opaque || state.blend == ModelBlend::Shadow;
    const bool legacyWrite = state.blend == ModelBlend::Opaque || state.blend == ModelBlend::Alpha;
    (state.cullEnabled.value_or(legacyCull) ? m_gl->enable : m_gl->disable)(GL_CULL_FACE);
    m_gl->depthMask(state.depthWrite.value_or(legacyWrite) ? GL_TRUE : GL_FALSE);

    if (state.depthTest)
    {
        m_gl->enable(GL_DEPTH_TEST);
    }
    else
    {
        m_gl->disable(GL_DEPTH_TEST);
    }
}
// Same factors as the legacy path; the partial operations do not touch cull/write.
void GlModelRenderer::SetBlend(ModelBlend blend) const
{
    switch (blend)
    {
    case ModelBlend::Opaque:
    case ModelBlend::Shadow:
        m_gl->disable(GL_BLEND);
        break;
    case ModelBlend::Subtractive:
        m_gl->enable(GL_BLEND);
        m_gl->blendFunc(GL_ZERO, GL_ONE_MINUS_SRC_COLOR);
        break;
    case ModelBlend::Multiplicative:
        m_gl->enable(GL_BLEND);
        m_gl->blendFunc(GL_ZERO, GL_SRC_COLOR);
        break;
    case ModelBlend::Additive:
        m_gl->enable(GL_BLEND);
        m_gl->blendFunc(GL_ONE, GL_ONE);
        break;
    case ModelBlend::InverseColorAdditive:
        m_gl->enable(GL_BLEND);
        m_gl->blendFunc(GL_ONE_MINUS_SRC_COLOR, GL_ONE);
        break;
    case ModelBlend::Alpha:
    case ModelBlend::BlendedShadow:
        m_gl->enable(GL_BLEND);
        m_gl->blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        break;
    }
}

void GlModelRenderer::SetOperation(const ModelDrawState& state, ModelStateOperation operation) const
{
    switch (operation)
    {
    case ModelStateOperation::Blend:
        SetBlend(state.blend);
        break;
    case ModelStateOperation::Cull:
        if (state.cullEnabled.has_value())
            (*state.cullEnabled ? m_gl->enable : m_gl->disable)(GL_CULL_FACE);
        break;
    case ModelStateOperation::DepthTest:
        (state.depthTest ? m_gl->enable : m_gl->disable)(GL_DEPTH_TEST);
        break;
    case ModelStateOperation::DepthWrite:
        if (state.depthWrite.has_value())
            m_gl->depthMask(*state.depthWrite ? GL_TRUE : GL_FALSE);
        break;
    case ModelStateOperation::AlphaTest:
        if (state.alphaTest.has_value())
            m_gl->uniform1i(m_uniforms.alphaTest, *state.alphaTest ? 1 : 0);
        break;
    case ModelStateOperation::Texture2d:
        if (state.texture2d.has_value())
            m_gl->uniform1i(m_uniforms.textured, *state.texture2d ? 1 : 0);
        break;
    case ModelStateOperation::Draw:
    case ModelStateOperation::Binding:
    case ModelStateOperation::TextureEnv:
    case ModelStateOperation::Fog:
        break;
    }
}
} // namespace Client::Render::Gl
