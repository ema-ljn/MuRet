#include "Render/Gl/GlRenderer.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Platform/GlLoader.h"
#include "Render/Gl/GlName.h"
#include "Render/Gl/GlShader.h"
#include "Render/Gl/GlTexture.h"
#include "Render/Gl/Shaders.h"
#include "Render/SpriteVertex.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstddef>
#include <initializer_list>
#include <span>
#include <string>
#include <type_traits>
#include <utility>

namespace Client::Render::Gl
{
namespace
{
// El VBO es una copia de los SpriteVertex tal cual: ocho floats seguidos, sin relleno.
static_assert(std::is_standard_layout_v<Render::SpriteVertex>);
static_assert(sizeof(Render::SpriteVertex) == 8 * sizeof(float));

// glVertexAttribPointer recibe el desplazamiento dentro del VBO como si fuera un puntero.
const void* BufferOffset(std::size_t bytes)
{
    return reinterpret_cast<const void*>(bytes);
}

// El formato de SpriteVertex para el VAO: dos floats de posición, dos de UV y cuatro de color.
void DescribeVertices(const Platform::GlFunctions& gl)
{
    constexpr auto stride = static_cast<GLsizei>(sizeof(Render::SpriteVertex));
    gl.vertexAttribPointer(SpritePositionLocation, 2, GL_FLOAT, GL_FALSE, stride,
                           BufferOffset(offsetof(Render::SpriteVertex, x)));
    gl.enableVertexAttribArray(SpritePositionLocation);
    gl.vertexAttribPointer(SpriteUvLocation, 2, GL_FLOAT, GL_FALSE, stride,
                           BufferOffset(offsetof(Render::SpriteVertex, u)));
    gl.enableVertexAttribArray(SpriteUvLocation);
    gl.vertexAttribPointer(SpriteColorLocation, 4, GL_FLOAT, GL_FALSE, stride,
                           BufferOffset(offsetof(Render::SpriteVertex, color)));
    gl.enableVertexAttribArray(SpriteColorLocation);
}
} // namespace

GlRenderer::GlRenderer(const Platform::GlFunctions& gl, GlShader shader, GlName vertexArray, GlName vertexBuffer,
                       Uniforms uniforms)
    : m_gl(&gl), m_shader(std::move(shader)), m_vertexArray(std::move(vertexArray)),
      m_vertexBuffer(std::move(vertexBuffer)), m_uniforms(uniforms)
{
}

Core::Expected<GlRenderer, std::string> GlRenderer::Create(const Platform::GlFunctions& gl)
{
    Core::Expected<GlShader, std::string> shader = GlShader::Create(gl, SpriteVertexShader, SpriteFragmentShader);
    if (!shader.HasValue())
    {
        return Core::Unexpected(shader.Error());
    }
    const Uniforms uniforms{.projection = shader.Value().UniformLocation(SpriteProjectionUniform),
                            .textured = shader.Value().UniformLocation(SpriteTexturedUniform),
                            .alphaTest = shader.Value().UniformLocation(SpriteAlphaTestUniform)};
    for (const auto& [location, name] :
         {std::pair{uniforms.projection, SpriteProjectionUniform}, std::pair{uniforms.textured, SpriteTexturedUniform},
          std::pair{uniforms.alphaTest, SpriteAlphaTestUniform}})
    {
        if (location < 0)
        {
            return Core::Unexpected(std::string("el shader del 2D no tiene el uniform ") + name);
        }
    }

    GlName vertexArray = GenVertexArray(gl);
    GlName vertexBuffer = GenBuffer(gl);
    gl.bindVertexArray(vertexArray.Get());
    gl.bindBuffer(GL_ARRAY_BUFFER, vertexBuffer.Get());
    DescribeVertices(gl);
    gl.bindVertexArray(0);
    return GlRenderer(gl, std::move(shader).Value(), std::move(vertexArray), std::move(vertexBuffer), uniforms);
}

void GlRenderer::Draw(std::span<const Render::SpriteVertex> vertices, const GlTexture& texture,
                      App::PixelSize screen) const
{
    Upload(vertices, screen);
    DrawRange(0, vertices.size(), &texture, false);
}

void GlRenderer::Upload(std::span<const Render::SpriteVertex> vertices, App::PixelSize screen) const
{
    // gluOrtho2D(0, W, 0, H) es glm::ortho con near -1 y far 1 (la CL5.0 los comparó bit a bit).
    const glm::mat4 projection =
        glm::ortho(0.0f, static_cast<float>(screen.width), 0.0f, static_cast<float>(screen.height));
    m_gl->viewport(0, 0, screen.width, screen.height);
    m_gl->useProgram(m_shader.Program());
    m_gl->bindVertexArray(m_vertexArray.Get());
    m_gl->bindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer.Get());
    // El VBO se llena de nuevo en cada llamada, como el lote en cada frame.
    m_gl->bufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size_bytes()), vertices.data(), GL_STREAM_DRAW);
    // GL_FALSE: la matriz de glm ya está en column-major, como la espera GL.
    m_gl->uniformMatrix4fv(m_uniforms.projection, 1, GL_FALSE, glm::value_ptr(projection));
}

void GlRenderer::DrawRange(std::size_t firstVertex, std::size_t vertexCount, const GlTexture* texture,
                           bool alphaTest) const
{
    if (texture != nullptr)
    {
        m_gl->bindTexture(GL_TEXTURE_2D, texture->Name());
    }
    m_gl->uniform1i(m_uniforms.textured, texture != nullptr ? 1 : 0);
    m_gl->uniform1i(m_uniforms.alphaTest, alphaTest ? 1 : 0);
    m_gl->drawArrays(GL_TRIANGLES, static_cast<GLint>(firstVertex), static_cast<GLsizei>(vertexCount));
}
} // namespace Client::Render::Gl
