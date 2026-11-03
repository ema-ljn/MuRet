#include "LabQuadRenderer.h"

#include "Fan.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Labs
{
namespace
{
// GLSL 330 core. El vertex shader hace lo que en la función fija hacía la matriz de gluOrtho2D:
// lleva cada vértice, en píxeles de la ventana, al clip. El fragment shader es el GL_MODULATE de
// glTexEnv con el color en blanco: el color de la textura tal cual. Sin alpha test: es el `discard`
// de la CL5.2.
constexpr std::string_view VertexShaderSource = R"(#version 330 core
layout(location = 0) in vec2 position;
layout(location = 1) in vec2 uv;
uniform mat4 projection;
out vec2 fragmentUv;
void main()
{
    fragmentUv = uv;
    gl_Position = projection * vec4(position, 0.0, 1.0);
}
)";

constexpr std::string_view FragmentShaderSource = R"(#version 330 core
in vec2 fragmentUv;
uniform sampler2D image;
out vec4 color;
void main()
{
    color = texture(image, fragmentUv);
}
)";

// Las location de los atributos en el vertex shader.
constexpr GLuint PositionAttribute = 0;
constexpr GLuint UvAttribute = 1;

// Los cuatro vértices de un quad, pasados a dos triángulos: seis índices.
constexpr std::uint32_t QuadVertexCount = 4;
constexpr GLsizei QuadIndexCount = 6;

// glVertexAttribPointer recibe el desplazamiento dentro del VBO como si fuera un puntero.
const void* BufferOffset(std::size_t bytes)
{
    return reinterpret_cast<const void*>(bytes);
}

std::string ShaderLog(const LabGl& gl, GLuint shader)
{
    GLint length = 0;
    gl.getShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<std::size_t>(length > 0 ? length : 0), '\0');
    if (length > 0)
    {
        gl.getShaderInfoLog(shader, length, nullptr, log.data());
    }
    return log;
}

std::string ProgramLog(const LabGl& gl, GLuint program)
{
    GLint length = 0;
    gl.getProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<std::size_t>(length > 0 ? length : 0), '\0');
    if (length > 0)
    {
        gl.getProgramInfoLog(program, length, nullptr, log.data());
    }
    return log;
}

Client::Core::Expected<GlName, std::string> Compile(const LabGl& gl, GLenum type, std::string_view source)
{
    GlName shader = CreateShader(gl, type);
    if (shader.Get() == 0)
    {
        return Client::Core::Unexpected(std::string("glCreateShader dio 0"));
    }
    const GLchar* text = source.data();
    const auto length = static_cast<GLint>(source.size());
    gl.shaderSource(shader.Get(), 1, &text, &length);
    gl.compileShader(shader.Get());
    GLint compiled = GL_FALSE;
    gl.getShaderiv(shader.Get(), GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE)
    {
        return Client::Core::Unexpected("no compiló el shader: " + ShaderLog(gl, shader.Get()));
    }
    return shader;
}

// Los shaders se pueden borrar después de enlazar: el programa ya tiene lo suyo.
Client::Core::Expected<GlName, std::string> Link(const LabGl& gl, const GlName& vertex, const GlName& fragment)
{
    GlName program = CreateProgram(gl);
    if (program.Get() == 0)
    {
        return Client::Core::Unexpected(std::string("glCreateProgram dio 0"));
    }
    gl.attachShader(program.Get(), vertex.Get());
    gl.attachShader(program.Get(), fragment.Get());
    gl.linkProgram(program.Get());
    GLint linked = GL_FALSE;
    gl.getProgramiv(program.Get(), GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE)
    {
        return Client::Core::Unexpected("no enlazó el programa: " + ProgramLog(gl, program.Get()));
    }
    return program;
}

// El formato de QuadVertex para el VAO: dos floats de posición y dos de UV, uno detrás del otro.
void DescribeVertices(const LabGl& gl)
{
    constexpr auto stride = static_cast<GLsizei>(sizeof(QuadVertex));
    gl.vertexAttribPointer(PositionAttribute, 2, GL_FLOAT, GL_FALSE, stride, BufferOffset(offsetof(QuadVertex, x)));
    gl.enableVertexAttribArray(PositionAttribute);
    gl.vertexAttribPointer(UvAttribute, 2, GL_FLOAT, GL_FALSE, stride, BufferOffset(offsetof(QuadVertex, u)));
    gl.enableVertexAttribArray(UvAttribute);
}
} // namespace

LabQuadRenderer::LabQuadRenderer(const LabGl& gl, GlName program, GlName vertexArray, GlName vertexBuffer,
                                 GlName indexBuffer, GLint projectionLocation)
    : m_gl(&gl), m_program(std::move(program)), m_vertexArray(std::move(vertexArray)),
      m_vertexBuffer(std::move(vertexBuffer)), m_indexBuffer(std::move(indexBuffer)),
      m_projectionLocation(projectionLocation)
{
}

Client::Core::Expected<LabQuadRenderer, std::string> LabQuadRenderer::Create(const LabGl& gl)
{
    const Client::Core::Expected<GlName, std::string> vertex = Compile(gl, GL_VERTEX_SHADER, VertexShaderSource);
    if (!vertex.HasValue())
    {
        return Client::Core::Unexpected(vertex.Error());
    }
    const Client::Core::Expected<GlName, std::string> fragment = Compile(gl, GL_FRAGMENT_SHADER, FragmentShaderSource);
    if (!fragment.HasValue())
    {
        return Client::Core::Unexpected(fragment.Error());
    }
    Client::Core::Expected<GlName, std::string> program = Link(gl, vertex.Value(), fragment.Value());
    if (!program.HasValue())
    {
        return Client::Core::Unexpected(program.Error());
    }
    const GLint projectionLocation = gl.getUniformLocation(program.Value().Get(), "projection");
    if (projectionLocation < 0)
    {
        return Client::Core::Unexpected(std::string("el shader no tiene el uniform projection"));
    }

    GlName vertexArray = GenVertexArray(gl);
    GlName vertexBuffer = GenBuffer(gl);
    GlName indexBuffer = GenBuffer(gl);
    gl.bindVertexArray(vertexArray.Get());
    gl.bindBuffer(GL_ARRAY_BUFFER, vertexBuffer.Get());
    DescribeVertices(gl);
    // El EBO atado con el VAO queda en el VAO: cada Draw lo usa sin volver a atarlo.
    const std::vector<Triangle> triangles = FanToTriangles(QuadVertexCount);
    gl.bindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer.Get());
    gl.bufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(triangles.size() * sizeof(Triangle)),
                  triangles.data(), GL_STATIC_DRAW);
    gl.bindVertexArray(0);
    return LabQuadRenderer(gl, std::move(program).Value(), std::move(vertexArray), std::move(vertexBuffer),
                           std::move(indexBuffer), projectionLocation);
}

void LabQuadRenderer::Draw(const std::array<QuadVertex, 4>& vertices, GLuint texture, const Matrix4& projection) const
{
    m_gl->useProgram(m_program.Get());
    m_gl->bindVertexArray(m_vertexArray.Get());
    m_gl->bindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer.Get());
    // El VBO se llena de nuevo en cada dibujo, como el lote de la CL5.2 en cada frame.
    m_gl->bufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(sizeof(vertices)), vertices.data(), GL_STREAM_DRAW);
    // GL_FALSE: la matriz ya está en column-major, como la espera GL.
    m_gl->uniformMatrix4fv(m_projectionLocation, 1, GL_FALSE, projection.data());
    m_gl->bindTexture(GL_TEXTURE_2D, texture);
    m_gl->drawElements(GL_TRIANGLES, QuadIndexCount, GL_UNSIGNED_INT, nullptr);
    m_gl->bindVertexArray(0);
}
} // namespace Labs
