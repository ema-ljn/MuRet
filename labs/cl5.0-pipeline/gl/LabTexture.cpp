#include "LabTexture.h"

#include <string>
#include <utility>

namespace Labs
{
namespace
{
// El valor de GL_UNPACK_ALIGNMENT por defecto: cada fila empieza en un múltiplo de 4 bytes.
constexpr int DefaultUnpackAlignment = 4;

// Los que acepta glPixelStorei para GL_UNPACK_ALIGNMENT.
bool IsUnpackAlignment(int value)
{
    return value == 1 || value == 2 || value == 4 || value == 8;
}
} // namespace

std::size_t RequiredUnpackBytes(int width, int height, int channels, int unpackAlignment)
{
    if (width <= 0 || height <= 0)
    {
        return 0;
    }
    const std::size_t rowBytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(channels);
    const auto alignment = static_cast<std::size_t>(unpackAlignment);
    const std::size_t stride = (rowBytes + alignment - 1) / alignment * alignment;
    return stride * (static_cast<std::size_t>(height) - 1) + rowBytes;
}

Client::Core::Expected<GlName, std::string> UploadTexture(const LabGl& gl, const TextureImage& image,
                                                          int unpackAlignment)
{
    if (image.channels != 3 && image.channels != 4)
    {
        return Client::Core::Unexpected("una textura va con 3 o 4 canales, no con " + std::to_string(image.channels));
    }
    if (!IsUnpackAlignment(unpackAlignment))
    {
        return Client::Core::Unexpected("GL_UNPACK_ALIGNMENT va en 1, 2, 4 u 8, no en " +
                                        std::to_string(unpackAlignment));
    }
    const std::size_t required = RequiredUnpackBytes(image.width, image.height, image.channels, unpackAlignment);
    if (image.pixels.size() < required)
    {
        return Client::Core::Unexpected("la imagen de " + std::to_string(image.width) + " × " +
                                        std::to_string(image.height) + " con GL_UNPACK_ALIGNMENT en " +
                                        std::to_string(unpackAlignment) + " necesita " + std::to_string(required) +
                                        " bytes y hay " + std::to_string(image.pixels.size()));
    }

    GlName texture = GenTexture(gl);
    const GLenum format = image.channels == 4 ? GL_RGBA : GL_RGB;
    gl.bindTexture(GL_TEXTURE_2D, texture.Get());
    gl.pixelStorei(GL_UNPACK_ALIGNMENT, unpackAlignment);
    gl.texImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(format), image.width, image.height, 0, format, GL_UNSIGNED_BYTE,
                  image.pixels.data());
    gl.pixelStorei(GL_UNPACK_ALIGNMENT, DefaultUnpackAlignment);
    // Sin mipmaps: con el GL_NEAREST_MIPMAP_LINEAR que GL pone por defecto, la textura no estaría
    // completa y se leería en negro.
    gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return texture;
}
} // namespace Labs
