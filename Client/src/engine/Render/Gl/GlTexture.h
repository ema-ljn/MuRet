#pragma once

#include "Core/Expected.h"
#include "Data/Textures/Sampling.h"
#include "Data/Textures/Texture.h"
#include "OpenGLApi.h"
#include "Platform/Sdl/GlLoader.h"
#include "Render/Gl/GlName.h"

#include <string>

// Uploaded like the original: the whole power-of-2 buffer in the file's format (GL_RGBA for OZT, 0x63bd00; GL_RGB for
// OZJ, 0x63b870), rows not flipped and no mipmaps (05 § Texturas). Move-only; needs a live context.
namespace Client::Render::Gl
{
class OPENGL_API GlTexture
{
public:
    // GL_UNPACK_ALIGNMENT is 1 during the upload (a 1- or 2-pixel RGB row is 3 or 6 bytes, and GL's 4 would skew the
    // next row), then restored. Like the original, the new texture stays bound to GL_TEXTURE_2D, which whoever tracks
    // the binding (0x620780, Q-C64) must know. GL errors (AUD-C03): one pending before the upload is reported as
    // another call's and nothing is uploaded; one after it fails the upload, deletes the new name and leaves
    // GL_TEXTURE_2D at 0. All read errors are drained. After GL_OUT_OF_MEMORY GL promises nothing about the context;
    // Upload does not try to fix it.
    [[nodiscard]] static Core::Expected<GlTexture, std::string> Upload(const Platform::Sdl::GlFunctions& gl,
                                                                       const Data::Textures::Texture& texture,
                                                                       Data::Textures::Sampling sampling);

    [[nodiscard]] GLuint Name() const;
    // The buffer size, textureWidth × textureHeight: the GL texture's, not that of the image in its corner.
    [[nodiscard]] int Width() const;
    [[nodiscard]] int Height() const;

private:
    GlTexture(GlName name, int width, int height);

    GlName m_name;
    int m_width;
    int m_height;
};
} // namespace Client::Render::Gl
