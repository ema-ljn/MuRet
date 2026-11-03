#include "Render/Gl/GlTexture.h"

#include <cstddef>
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

// Sin mipmaps no hay otro filtro de lejos que GL_NEAREST o GL_LINEAR: con el
// GL_NEAREST_MIPMAP_LINEAR que GL trae por defecto, la textura no estaría completa y se leería
// negra.
GLint GlFilter(Filter filter)
{
    return static_cast<GLint>(filter == Filter::Linear ? GL_LINEAR : GL_NEAREST);
}

// El original pide GL_CLAMP (0x2900), que el perfil core no tiene (glTexParameteri daría
// GL_INVALID_ENUM): va GL_CLAMP_TO_EDGE, y la diferencia en el borde con GL_LINEAR está en Q-C73.
GLint GlWrap(Wrap wrap)
{
    return static_cast<GLint>(wrap == Wrap::Repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
}

// El formato con que sube el original cada uno, con el tamaño de 8 bits por canal fijo.
GLint InternalFormat(PixelFormat format)
{
    return static_cast<GLint>(format == PixelFormat::Rgb ? GL_RGB8 : GL_RGBA8);
}

GLenum PixelLayout(PixelFormat format)
{
    return format == PixelFormat::Rgb ? GL_RGB : GL_RGBA;
}

// Vacío si `texture` se puede subir: lados de 1 o más y los píxeles del buffer entero.
std::string CheckBuffer(const Texture& texture)
{
    if (texture.textureWidth <= 0 || texture.textureHeight <= 0)
    {
        return "una textura de " + std::to_string(texture.textureWidth) + " × " +
               std::to_string(texture.textureHeight) + " no se puede subir";
    }
    const std::size_t expected = static_cast<std::size_t>(texture.textureWidth) *
                                 static_cast<std::size_t>(texture.textureHeight) *
                                 Data::Textures::BytesPerPixel(texture.format);
    if (texture.pixels.size() != expected)
    {
        return "el buffer de " + std::to_string(texture.textureWidth) + " × " + std::to_string(texture.textureHeight) +
               " mide " + std::to_string(expected) + " bytes y la textura trae " +
               std::to_string(texture.pixels.size());
    }
    return {};
}
} // namespace

GlTexture::GlTexture(GlName name, int width, int height) : m_name(std::move(name)), m_width(width), m_height(height) {}

Core::Expected<GlTexture, std::string> GlTexture::Upload(const Platform::GlFunctions& gl, const Texture& texture,
                                                         Data::Textures::Sampling sampling)
{
    std::string bufferError = CheckBuffer(texture);
    if (!bufferError.empty())
    {
        return Core::Unexpected(std::move(bufferError));
    }

    GlName name = GenTexture(gl);
    gl.bindTexture(GL_TEXTURE_2D, name.Get());
    GLint previousAlignment = 0;
    gl.getIntegerv(GL_UNPACK_ALIGNMENT, &previousAlignment);
    gl.pixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl.texImage2D(GL_TEXTURE_2D, 0, InternalFormat(texture.format), texture.textureWidth, texture.textureHeight, 0,
                  PixelLayout(texture.format), GL_UNSIGNED_BYTE, texture.pixels.data());
    gl.pixelStorei(GL_UNPACK_ALIGNMENT, previousAlignment);
    // Como el original: el filtro va de cerca y de lejos, y el wrap en los dos ejes.
    gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GlFilter(sampling.filter));
    gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GlFilter(sampling.filter));
    gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GlWrap(sampling.wrap));
    gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GlWrap(sampling.wrap));
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
