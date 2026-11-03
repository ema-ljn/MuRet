#include "Render/Gl/GlName.h"

#include "Platform/GlLoader.h"

#include <utility>

namespace Client::Render::Gl
{
namespace
{
void DeleteTexture(const Platform::GlFunctions& gl, GLuint name)
{
    gl.deleteTextures(1, &name);
}

void DeleteBuffer(const Platform::GlFunctions& gl, GLuint name)
{
    gl.deleteBuffers(1, &name);
}

void DeleteVertexArray(const Platform::GlFunctions& gl, GLuint name)
{
    gl.deleteVertexArrays(1, &name);
}

void DeleteFramebuffer(const Platform::GlFunctions& gl, GLuint name)
{
    gl.deleteFramebuffers(1, &name);
}

void DeleteShader(const Platform::GlFunctions& gl, GLuint name)
{
    gl.deleteShader(name);
}

void DeleteProgram(const Platform::GlFunctions& gl, GLuint name)
{
    gl.deleteProgram(name);
}
} // namespace

GlName::GlName(const Platform::GlFunctions& gl, Deleter deleter, GLuint name)
    : m_gl(&gl), m_deleter(deleter), m_name(name)
{
}

GlName::~GlName()
{
    Release();
}

// std::exchange deja el de origen en 0: su destructor no borra nada.
GlName::GlName(GlName&& other) noexcept
    : m_gl(other.m_gl), m_deleter(other.m_deleter), m_name(std::exchange(other.m_name, 0))
{
}

GlName& GlName::operator=(GlName&& other) noexcept
{
    if (this != &other)
    {
        Release();
        m_gl = other.m_gl;
        m_deleter = other.m_deleter;
        m_name = std::exchange(other.m_name, 0);
    }
    return *this;
}

GLuint GlName::Get() const
{
    return m_name;
}

void GlName::Release()
{
    if (m_name == 0 || m_deleter == nullptr)
    {
        return;
    }
    m_deleter(*m_gl, m_name);
    m_name = 0;
}

GlName GenTexture(const Platform::GlFunctions& gl)
{
    GLuint name = 0;
    gl.genTextures(1, &name);
    return GlName(gl, DeleteTexture, name);
}

GlName GenBuffer(const Platform::GlFunctions& gl)
{
    GLuint name = 0;
    gl.genBuffers(1, &name);
    return GlName(gl, DeleteBuffer, name);
}

GlName GenVertexArray(const Platform::GlFunctions& gl)
{
    GLuint name = 0;
    gl.genVertexArrays(1, &name);
    return GlName(gl, DeleteVertexArray, name);
}

GlName GenFramebuffer(const Platform::GlFunctions& gl)
{
    GLuint name = 0;
    gl.genFramebuffers(1, &name);
    return GlName(gl, DeleteFramebuffer, name);
}

GlName CreateShader(const Platform::GlFunctions& gl, GLenum type)
{
    return GlName(gl, DeleteShader, gl.createShader(type));
}

GlName CreateProgram(const Platform::GlFunctions& gl)
{
    return GlName(gl, DeleteProgram, gl.createProgram());
}
} // namespace Client::Render::Gl
