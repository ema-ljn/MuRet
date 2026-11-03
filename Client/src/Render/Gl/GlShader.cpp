#include "Render/Gl/GlShader.h"

#include <cstddef>
#include <string>
#include <utility>

namespace Client::Render::Gl
{
namespace
{
// El largo de GL_INFO_LOG_LENGTH cuenta el NUL del final; el texto es lo que dice `written`.
std::string ShaderLog(const Platform::GlFunctions& gl, GLuint shader)
{
    GLint length = 0;
    gl.getShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    if (length <= 0)
    {
        return {};
    }
    std::string log(static_cast<std::size_t>(length), '\0');
    GLsizei written = 0;
    gl.getShaderInfoLog(shader, length, &written, log.data());
    log.resize(static_cast<std::size_t>(written > 0 ? written : 0));
    return log;
}

std::string ProgramLog(const Platform::GlFunctions& gl, GLuint program)
{
    GLint length = 0;
    gl.getProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    if (length <= 0)
    {
        return {};
    }
    std::string log(static_cast<std::size_t>(length), '\0');
    GLsizei written = 0;
    gl.getProgramInfoLog(program, length, &written, log.data());
    log.resize(static_cast<std::size_t>(written > 0 ? written : 0));
    return log;
}

Core::Expected<GlName, std::string> Compile(const Platform::GlFunctions& gl, GLenum type, std::string_view kind,
                                            std::string_view source)
{
    GlName shader = CreateShader(gl, type);
    if (shader.Get() == 0)
    {
        return Core::Unexpected("glCreateShader dio 0 para el " + std::string(kind));
    }
    const GLchar* text = source.data();
    const auto length = static_cast<GLint>(source.size());
    gl.shaderSource(shader.Get(), 1, &text, &length);
    gl.compileShader(shader.Get());
    GLint compiled = GL_FALSE;
    gl.getShaderiv(shader.Get(), GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE)
    {
        return Core::Unexpected("no compiló el " + std::string(kind) + ": " + ShaderLog(gl, shader.Get()));
    }
    return shader;
}

Core::Expected<GlName, std::string> Link(const Platform::GlFunctions& gl, const GlName& vertex, const GlName& fragment)
{
    GlName program = CreateProgram(gl);
    if (program.Get() == 0)
    {
        return Core::Unexpected(std::string("glCreateProgram dio 0"));
    }
    gl.attachShader(program.Get(), vertex.Get());
    gl.attachShader(program.Get(), fragment.Get());
    gl.linkProgram(program.Get());
    GLint linked = GL_FALSE;
    gl.getProgramiv(program.Get(), GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE)
    {
        return Core::Unexpected("no enlazó el programa: " + ProgramLog(gl, program.Get()));
    }
    return program;
}
} // namespace

GlShader::GlShader(const Platform::GlFunctions& gl, GlName program) : m_gl(&gl), m_program(std::move(program)) {}

Core::Expected<GlShader, std::string> GlShader::Create(const Platform::GlFunctions& gl, std::string_view vertexSource,
                                                       std::string_view fragmentSource)
{
    const Core::Expected<GlName, std::string> vertex = Compile(gl, GL_VERTEX_SHADER, "vertex shader", vertexSource);
    if (!vertex.HasValue())
    {
        return Core::Unexpected(vertex.Error());
    }
    const Core::Expected<GlName, std::string> fragment =
        Compile(gl, GL_FRAGMENT_SHADER, "fragment shader", fragmentSource);
    if (!fragment.HasValue())
    {
        return Core::Unexpected(fragment.Error());
    }
    Core::Expected<GlName, std::string> program = Link(gl, vertex.Value(), fragment.Value());
    if (!program.HasValue())
    {
        return Core::Unexpected(program.Error());
    }
    return GlShader(gl, std::move(program).Value());
}

GLuint GlShader::Program() const
{
    return m_program.Get();
}

GLint GlShader::UniformLocation(const char* name) const
{
    return m_gl->getUniformLocation(m_program.Get(), name);
}
} // namespace Client::Render::Gl
