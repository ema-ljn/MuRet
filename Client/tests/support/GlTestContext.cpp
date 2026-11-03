#include "support/GlTestContext.h"

#include "Core/Environment.h"

#include <SDL3/SDL_error.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Client::Tests
{
namespace
{
// La ventana no se dibuja: el tamaño da igual, y uno chico pide poco.
constexpr int HiddenWindowSide = 64;
} // namespace

bool IsGlRequired(const std::optional<std::string>& value)
{
    return value.has_value() && *value == "1";
}

GlTestContext::GlTestContext(SdlPtr sdl, WindowPtr window, GlContextPtr context, const Platform::GlFunctions& gl)
    : m_sdl(std::move(sdl)), m_window(std::move(window)), m_context(std::move(context)), m_gl(gl)
{
}

// Si algo falla en el medio, lo creado se destruye al revés: el contexto, la ventana y SDL.
Core::Expected<std::unique_ptr<GlTestContext>, std::string> GlTestContext::Create()
{
    Core::Expected<std::unique_ptr<Platform::SdlContext>, std::string> sdl = Platform::SdlContext::Create();
    if (!sdl.HasValue())
    {
        return Core::Unexpected(sdl.Error());
    }

    // Como Platform::Window::Create: los atributos del contexto van antes de crear la ventana.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, Platform::GlMajorVersion);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, Platform::GlMinorVersion);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    WindowPtr window(
        SDL_CreateWindow("MuRet tests", HiddenWindowSide, HiddenWindowSide, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN));
    if (!window)
    {
        return Core::Unexpected(std::string("no se pudo crear la ventana: ") + SDL_GetError());
    }
    // SDL_GL_CreateContext también lo deja como el contexto actual de este hilo.
    GlContextPtr context(SDL_GL_CreateContext(window.get()));
    if (!context)
    {
        return Core::Unexpected("no se pudo crear el contexto OpenGL " + Platform::GlVersionText() +
                                " core: " + SDL_GetError());
    }

    const Core::Expected<Platform::GlFunctions, std::string> gl = Platform::LoadGlFunctions();
    if (!gl.HasValue())
    {
        return Core::Unexpected(gl.Error());
    }
    // El constructor es privado: make_unique no lo ve.
    return std::unique_ptr<GlTestContext>(
        new GlTestContext(std::move(sdl).Value(), std::move(window), std::move(context), gl.Value()));
}

Data::Textures::Texture TextureOverTheGlLimit(const Platform::GlFunctions& gl)
{
    GLint maxSide = 0;
    gl.getIntegerv(GL_MAX_TEXTURE_SIZE, &maxSide);
    const int side = static_cast<int>(maxSide) + 1;
    constexpr Data::Textures::PixelFormat Format = Data::Textures::PixelFormat::Rgba;
    const std::size_t bytes = static_cast<std::size_t>(side) * Data::Textures::BytesPerPixel(Format);
    return Data::Textures::Texture{.width = side,
                                   .height = 1,
                                   .textureWidth = side,
                                   .textureHeight = 1,
                                   .format = Format,
                                   .pixels = std::vector<std::uint8_t>(bytes)};
}

const Platform::GlFunctions& GlTestContext::Gl() const
{
    return m_gl;
}

void GlTest::SetUp()
{
    Core::Expected<std::unique_ptr<GlTestContext>, std::string> context = GlTestContext::Create();
    if (!context.HasValue())
    {
        if (IsGlRequired(Core::ReadEnvironmentVariable(RequireGlVariable)))
        {
            FAIL() << RequireGlVariable << "=1 y no se pudo abrir OpenGL 3.3 core: " << context.Error();
        }
        GTEST_SKIP() << "sin OpenGL 3.3 core (" << context.Error() << "): test salteado; con " << RequireGlVariable
                     << "=1 falla";
    }
    m_context = std::move(context).Value();
}

const Platform::GlFunctions& GlTest::Gl() const
{
    return m_context->Gl();
}
} // namespace Client::Tests
