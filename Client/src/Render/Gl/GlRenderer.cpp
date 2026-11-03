#include "Render/Gl/GlRenderer.h"

#include "Render/Gl/Shaders.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat4x4.hpp>

#include <cstddef>
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
                       GLint projectionLocation)
    : m_gl(&gl), m_shader(std::move(shader)), m_vertexArray(std::move(vertexArray)),
      m_vertexBuffer(std::move(vertexBuffer)), m_projectionLocation(projectionLocation)
{
}

Core::Expected<GlRenderer, std::string> GlRenderer::Create(const Platform::GlFunctions& gl)
{
    Core::Expected<GlShader, std::string> shader = GlShader::Create(gl, SpriteVertexShader, SpriteFragmentShader);
    if (!shader.HasValue())
    {
        return Core::Unexpected(shader.Error());
    }
    const GLint projectionLocation = shader.Value().UniformLocation(SpriteProjectionUniform);
    if (projectionLocation < 0)
    {
        return Core::Unexpected(std::string("el shader del 2D no tiene el uniform ") + SpriteProjectionUniform);
    }

    GlName vertexArray = GenVertexArray(gl);
    GlName vertexBuffer = GenBuffer(gl);
    gl.bindVertexArray(vertexArray.Get());
    gl.bindBuffer(GL_ARRAY_BUFFER, vertexBuffer.Get());
    DescribeVertices(gl);
    gl.bindVertexArray(0);
    return GlRenderer(gl, std::move(shader).Value(), std::move(vertexArray), std::move(vertexBuffer),
                      projectionLocation);
}

void GlRenderer::Draw(std::span<const Render::SpriteVertex> vertices, const GlTexture& texture,
                      App::PixelSize screen) const
{
    // gluOrtho2D(0, W, 0, H) es glm::ortho con near -1 y far 1 (la CL5.0 los comparó bit a bit).
    const glm::mat4 projection =
        glm::ortho(0.0f, static_cast<float>(screen.width), 0.0f, static_cast<float>(screen.height));
    m_gl->viewport(0, 0, screen.width, screen.height);
    m_gl->useProgram(m_shader.Program());
    m_gl->bindVertexArray(m_vertexArray.Get());
    m_gl->bindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer.Get());
    // El VBO se llena de nuevo en cada dibujo, como el lote de la CL5.2 en cada frame.
    m_gl->bufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size_bytes()), vertices.data(), GL_STREAM_DRAW);
    // GL_FALSE: la matriz de glm ya está en column-major, como la espera GL.
    m_gl->uniformMatrix4fv(m_projectionLocation, 1, GL_FALSE, glm::value_ptr(projection));
    m_gl->bindTexture(GL_TEXTURE_2D, texture.Name());
    m_gl->drawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    m_gl->bindVertexArray(0);
}
} // namespace Client::Render::Gl
