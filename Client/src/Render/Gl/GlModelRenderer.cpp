#include "Render/Gl/GlModelRenderer.h"

#include "Core/Expected.h"
#include "Core/Log.h"
#include "Platform/GlLoader.h"
#include "Render/Gl/GlName.h"
#include "Render/Gl/GlShader.h"
#include "Render/Gl/GlTexture.h"
#include "Render/Gl/Shaders.h"
#include "Render/Gl/TextureTable.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/Pass3D.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstddef>
#include <initializer_list>
#include <string>
#include <type_traits>
#include <utility>

namespace Client::Render::Gl
{
namespace
{
// El VBO es una copia de los ModelVertex tal cual: nueve floats seguidos, sin relleno.
static_assert(std::is_standard_layout_v<Render::ModelVertex>);
static_assert(sizeof(Render::ModelVertex) == 9 * sizeof(float));

// glVertexAttribPointer recibe el desplazamiento dentro del VBO como si fuera un puntero.
const void* BufferOffset(std::size_t bytes)
{
    return reinterpret_cast<const void*>(bytes);
}

// El formato de ModelVertex para el VAO: tres floats de posición, dos de UV y cuatro de color.
void DescribeVertices(const Platform::GlFunctions& gl)
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

GlModelRenderer::GlModelRenderer(const Platform::GlFunctions& gl, GlShader shader, GlName vertexArray,
                                 GlName vertexBuffer, Uniforms uniforms)
    : m_gl(&gl), m_shader(std::move(shader)), m_vertexArray(std::move(vertexArray)),
      m_vertexBuffer(std::move(vertexBuffer)), m_uniforms(uniforms)
{
}

Core::Expected<GlModelRenderer, std::string> GlModelRenderer::Create(const Platform::GlFunctions& gl)
{
    Core::Expected<GlShader, std::string> shader = GlShader::Create(gl, ModelVertexShader, ModelFragmentShader);
    if (!shader.HasValue())
    {
        return Core::Unexpected(shader.Error());
    }
    const Uniforms uniforms{.viewProjection = shader.Value().UniformLocation(ModelViewProjectionUniform),
                            .textured = shader.Value().UniformLocation(ModelTexturedUniform),
                            .alphaTest = shader.Value().UniformLocation(ModelAlphaTestUniform)};
    for (const auto& [location, name] :
         {std::pair{uniforms.viewProjection, ModelViewProjectionUniform},
          std::pair{uniforms.textured, ModelTexturedUniform}, std::pair{uniforms.alphaTest, ModelAlphaTestUniform}})
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
    for (const ModelDrawCommand& command : batch.Commands())
    {
        const GlTexture* texture = nullptr;
        if (command.state.texture.has_value())
        {
            texture = textures.Find(*command.state.texture);
            // insert dice si el slot es nuevo: solo entonces se avisa.
            if (texture == nullptr && m_warnedSlots.insert(command.state.texture->value).second)
            {
                log.Warning("el slot {:#x} no tiene textura: lo que lo usa sale sin textura (Q-C29)",
                            command.state.texture->value);
            }
        }
        SetDrawState(command.state);
        if (texture != nullptr)
        {
            m_gl->bindTexture(GL_TEXTURE_2D, texture->Name());
        }
        m_gl->uniform1i(m_uniforms.textured, texture != nullptr ? 1 : 0);
        const bool alphaTest =
            command.state.blend == ModelBlend::Alpha || command.state.blend == ModelBlend::BlendedShadow;
        m_gl->uniform1i(m_uniforms.alphaTest, alphaTest ? 1 : 0);
        m_gl->drawArrays(GL_TRIANGLES, static_cast<GLint>(command.firstVertex),
                         static_cast<GLsizei>(command.vertexCount));
    }
}

// 0x620db0: el viewport, las matrices, el depth test con GL_LEQUAL, el culling y la máscara.
void GlModelRenderer::BeginPass(const ModelBatch& batch) const
{
    const Pass3D& pass = batch.Pass();
    m_gl->viewport(pass.viewport.x, pass.viewport.y, pass.viewport.z, pass.viewport.w);
    m_gl->useProgram(m_shader.Program());
    m_gl->bindVertexArray(m_vertexArray.Get());
    m_gl->bindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer.Get());
    // El VBO se llena de nuevo en cada pase, como el lote en cada frame.
    m_gl->bufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(batch.Vertices().size_bytes()), batch.Vertices().data(),
                     GL_STREAM_DRAW);
    const glm::mat4 viewProjection = pass.projection * pass.view;
    // GL_FALSE: la matriz de glm ya está en column-major, como la espera GL.
    m_gl->uniformMatrix4fv(m_uniforms.viewProjection, 1, GL_FALSE, glm::value_ptr(viewProjection));
    m_gl->enable(GL_DEPTH_TEST);
    m_gl->depthFunc(GL_LEQUAL);
    m_gl->enable(GL_CULL_FACE);
    m_gl->depthMask(GL_TRUE);
}

// 0x620980, 0x620a00(1) y 0x620a90, las dos sombras de 0x4eff40, y 0x620870 con la bandera 0x2000.
void GlModelRenderer::SetDrawState(const ModelDrawState& state) const
{
    switch (state.blend)
    {
    case ModelBlend::Opaque:
        m_gl->disable(GL_BLEND);
        m_gl->enable(GL_CULL_FACE);
        m_gl->depthMask(GL_TRUE);
        break;
    case ModelBlend::Alpha:
        m_gl->enable(GL_BLEND);
        m_gl->blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        m_gl->disable(GL_CULL_FACE);
        m_gl->depthMask(GL_TRUE);
        break;
    case ModelBlend::Additive:
        m_gl->enable(GL_BLEND);
        m_gl->blendFunc(GL_ONE, GL_ONE);
        m_gl->disable(GL_CULL_FACE);
        m_gl->depthMask(GL_FALSE);
        break;
    case ModelBlend::Shadow:
        m_gl->disable(GL_BLEND);
        m_gl->enable(GL_CULL_FACE);
        m_gl->depthMask(GL_FALSE);
        break;
    case ModelBlend::BlendedShadow:
        m_gl->enable(GL_BLEND);
        m_gl->blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        m_gl->disable(GL_CULL_FACE);
        m_gl->depthMask(GL_FALSE);
        break;
    }
    if (state.depthTest)
    {
        m_gl->enable(GL_DEPTH_TEST);
    }
    else
    {
        m_gl->disable(GL_DEPTH_TEST);
    }
}
} // namespace Client::Render::Gl
