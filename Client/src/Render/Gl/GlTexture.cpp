#include "Render/Gl/GlTexture.h"

#include "Core/Expected.h"
#include "Data/Textures/Sampling.h"
#include "Data/Textures/Texture.h"
#include "Platform/GlLoader.h"
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

// Cuántas veces se llama a glGetError para vaciar los errores. GL puede tener varios guardados
// (uno por bandera, y glGetError da uno por llamada); el tope es por si un driver no deja de dar
// errores, para no quedarse en un bucle.
constexpr int MaxPendingErrors = 16;

// Saca los errores que tiene GL y da el primero, o GL_NO_ERROR si no había ninguno.
GLenum TakeErrors(const Platform::GlFunctions& gl)
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

// El nombre de un error de glGetError con su número, para el mensaje: "GL_INVALID_VALUE (0x501)".
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

// Vacío si `texture` se puede subir: lados de 1 o más y los píxeles del buffer entero.
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

Core::Expected<GlTexture, std::string> GlTexture::Upload(const Platform::GlFunctions& gl, const Texture& texture,
                                                         Data::Textures::Sampling sampling)
{
    std::string bufferError = CheckBuffer(texture);
    if (!bufferError.empty())
    {
        return Core::Unexpected(std::move(bufferError));
    }

    // glGetError no dice qué llamada dejó un error: uno que ya estaba es de otra, y no se le echa
    // la culpa a esta subida (AUD-C03 de la auditoría del 2026-11-03).
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
    // Como el original: el filtro va de cerca y de lejos, y el wrap en los dos ejes.
    gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GlFilter(sampling.filter));
    gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GlFilter(sampling.filter));
    gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GlWrap(sampling.wrap));
    gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GlWrap(sampling.wrap));

    // Lo que GL marcó desde el chequeo de arriba es de esta subida: el glTexImage2D o, con
    // GL_OUT_OF_MEMORY, cualquier otra de las llamadas. Se lee después de la última, así no queda
    // nada para la llamada siguiente. El GlName borra el nombre al salir: GL desata lo que se
    // borra, y GL_TEXTURE_2D queda en 0.
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
