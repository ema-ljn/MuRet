#include "Render/Gl/GlTexture.h"

#include "Core/Expected.h"
#include "Data/Textures/Sampling.h"
#include "Data/Textures/Texture.h"
#include "Platform/Sdl/GlLoader.h"
#include "Render/Gl/GlName.h"

#include <cstddef>
#include <format>
#include <string>
#include <utility>

namespace Client::Render::Gl
{
namespace
{
using Data::Textures::Filter;
using Data::Textures::PixelFormat;
using Data::Textures::Texture;
using Data::Textures::Wrap;

// Without mipmaps the min filter can only be GL_NEAREST or GL_LINEAR: with GL's default GL_NEAREST_MIPMAP_LINEAR the
// texture would be incomplete and read as black.
GLint GlFilter(Filter filter)
{
    return static_cast<GLint>(filter == Filter::Linear ? GL_LINEAR : GL_NEAREST);
}

// The original asks for GL_CLAMP (0x2900), which the core profile lacks (glTexParameteri would give GL_INVALID_ENUM):
// GL_CLAMP_TO_EDGE instead; the edge difference with GL_LINEAR is in Q-C73.
GLint GlWrap(Wrap wrap)
{
    return static_cast<GLint>(wrap == Wrap::Repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
}

// The format the original uploads each one with, with a fixed size of 8 bits per channel.
GLint InternalFormat(PixelFormat format)
{
    return static_cast<GLint>(format == PixelFormat::Rgb ? GL_RGB8 : GL_RGBA8);
}

GLenum PixelLayout(PixelFormat format)
{
    return format == PixelFormat::Rgb ? GL_RGB : GL_RGBA;
}

// GL can hold several errors (one per flag, and glGetError returns one per call); the cap avoids an endless loop if a
// driver never stops returning errors.
constexpr int MaxPendingErrors = 16;

// Drains GL's errors and returns the first, or GL_NO_ERROR if there were none.
GLenum TakeErrors(const Platform::Sdl::GlFunctions& gl)
{
    GLenum first = GL_NO_ERROR;
    for (int taken = 0; taken < MaxPendingErrors; ++taken)
    {
        const GLenum error = gl.getError();
        if (error == GL_NO_ERROR)
        {
            break;
        }
        if (first == GL_NO_ERROR)
        {
            first = error;
        }
    }
    return first;
}

// For the message: "GL_INVALID_VALUE (0x501)".
std::string ErrorText(GLenum error)
{
    const char* name = nullptr;
    switch (error)
    {
    case GL_INVALID_ENUM:
        name = "GL_INVALID_ENUM";
        break;
    case GL_INVALID_VALUE:
        name = "GL_INVALID_VALUE";
        break;
    case GL_INVALID_OPERATION:
        name = "GL_INVALID_OPERATION";
        break;
    case GL_OUT_OF_MEMORY:
        name = "GL_OUT_OF_MEMORY";
        break;
    case GL_INVALID_FRAMEBUFFER_OPERATION:
        name = "GL_INVALID_FRAMEBUFFER_OPERATION";
        break;
    default:
        return std::format("el error {:#x} de GL", error);
    }
    return std::format("{} ({:#x})", name, error);
}

std::string SizeText(const Texture& texture)
{
    return std::to_string(texture.textureWidth) + " × " + std::to_string(texture.textureHeight);
}

// Empty if `texture` can be uploaded: sides of 1 or more and the whole buffer's pixels.
std::string CheckBuffer(const Texture& texture)
{
    if (texture.textureWidth <= 0 || texture.textureHeight <= 0)
    {
        return "una textura de " + SizeText(texture) + " no se puede subir";
    }
    const std::size_t expected = static_cast<std::size_t>(texture.textureWidth) *
                                 static_cast<std::size_t>(texture.textureHeight) *
                                 Data::Textures::BytesPerPixel(texture.format);
    if (texture.pixels.size() != expected)
    {
        return "el buffer de " + SizeText(texture) + " mide " + std::to_string(expected) + " bytes y la textura trae " +
               std::to_string(texture.pixels.size());
    }
    return {};
}
} // namespace

GlTexture::GlTexture(GlName name, int width, int height) : m_name(std::move(name)), m_width(width), m_height(height) {}

Core::Expected<GlTexture, std::string> GlTexture::Upload(const Platform::Sdl::GlFunctions& gl, const Texture& texture,
                                                         Data::Textures::Sampling sampling)
{
    std::string bufferError = CheckBuffer(texture);
    if (!bufferError.empty())
    {
        return Core::Unexpected(std::move(bufferError));
    }

    // glGetError does not say which call left an error: one already pending belongs to another call and is not blamed
    // on this upload (AUD-C03).
    if (const GLenum previous = TakeErrors(gl); previous != GL_NO_ERROR)
    {
        return Core::Unexpected("antes de subir la textura de " + SizeText(texture) +
                                ", GL ya tenía un error pendiente de otra llamada: " + ErrorText(previous));
    }

    GlName name = GenTexture(gl);
    gl.bindTexture(GL_TEXTURE_2D, name.Get());
    GLint previousAlignment = 0;
    gl.getIntegerv(GL_UNPACK_ALIGNMENT, &previousAlignment);
    gl.pixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl.texImage2D(GL_TEXTURE_2D, 0, InternalFormat(texture.format), texture.textureWidth, texture.textureHeight, 0,
                  PixelLayout(texture.format), GL_UNSIGNED_BYTE, texture.pixels.data());
    gl.pixelStorei(GL_UNPACK_ALIGNMENT, previousAlignment);
    // Like the original: the filter for both mag and min, the wrap on both axes.
    gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GlFilter(sampling.filter));
    gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GlFilter(sampling.filter));
    gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GlWrap(sampling.wrap));
    gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GlWrap(sampling.wrap));

    // Errors since the check above belong to this upload: the glTexImage2D or, with GL_OUT_OF_MEMORY, any of the calls.
    // Read after the last call so nothing is left for the next one. The GlName deletes the name on exit: GL unbinds
    // what is deleted, and GL_TEXTURE_2D is left at 0.
    if (const GLenum uploadError = TakeErrors(gl); uploadError != GL_NO_ERROR)
    {
        return Core::Unexpected("GL no aceptó la textura de " + SizeText(texture) + ": " + ErrorText(uploadError));
    }
    return GlTexture(std::move(name), texture.textureWidth, texture.textureHeight);
}

GLuint GlTexture::Name() const
{
    return m_name.Get();
}

int GlTexture::Width() const
{
    return m_width;
}

int GlTexture::Height() const
{
    return m_height;
}
} // namespace Client::Render::Gl
