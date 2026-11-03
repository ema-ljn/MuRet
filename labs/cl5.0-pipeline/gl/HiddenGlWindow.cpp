#include "HiddenGlWindow.h"

#include <SDL3/SDL_error.h>

#include <cstdlib>
#include <string>
#include <utility>

namespace Labs
{
namespace
{
// La ventana no se dibuja: el tamaño da igual, y uno chico pide poco.
constexpr int HiddenWindowSide = 64;

const std::string& GlVersionText()
{
    static const std::string text =
        std::to_string(Client::Platform::GlMajorVersion) + "." + std::to_string(Client::Platform::GlMinorVersion);
    return text;
}

// La versión y el perfil del contexto actual, con las cinco funciones de la CL0.3. Vacío si sirve.
std::string CheckContext(const Client::Platform::GlFunctions& gl)
{
    GLint major = 0;
    GLint minor = 0;
    GLint profileMask = 0;
    gl.getIntegerv(GL_MAJOR_VERSION, &major);
    gl.getIntegerv(GL_MINOR_VERSION, &minor);
    // Antes de la 3.0 GL_MAJOR_VERSION no existe y da 0.0: GL_VERSION dice cuál es.
    const std::string driverVersion = Client::Platform::GlString(gl, GL_VERSION);
    if (!Client::Platform::HasRequiredGlVersion(major, minor))
    {
        return "se pidió OpenGL " + GlVersionText() + " y el driver dio " + std::to_string(major) + "." +
               std::to_string(minor) + " (" + driverVersion + ")";
    }
    gl.getIntegerv(GL_CONTEXT_PROFILE_MASK, &profileMask);
    if (!Client::Platform::IsCoreProfile(profileMask))
    {
        return "se pidió OpenGL " + GlVersionText() + " core y el driver dio un contexto sin el perfil core (" +
               driverVersion + ")";
    }
    return {};
}
} // namespace

bool IsGlRequired(const std::optional<std::string>& value)
{
    return value.has_value() && *value == "1";
}

std::optional<std::string> ReadRequireGl()
{
#ifdef _MSC_VER
    // getenv es "inseguro" para MSVC (C4996), como en Client/tests/support/GoldenDir.cpp.
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, RequireGlVariable) != 0 || value == nullptr)
    {
        return std::nullopt;
    }
    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(RequireGlVariable);
    if (value == nullptr)
    {
        return std::nullopt;
    }
    return std::string(value);
#endif
}

HiddenGlWindow::HiddenGlWindow(SdlPtr sdl, WindowPtr window, GlContextPtr context, const LabGl& gl)
    : m_sdl(std::move(sdl)), m_window(std::move(window)), m_context(std::move(context)), m_gl(gl)
{
}

// Si algo falla en el medio, lo creado se destruye al revés: el contexto, la ventana y SDL.
Client::Core::Expected<HiddenGlWindow, std::string> HiddenGlWindow::Create()
{
    Client::Core::Expected<std::unique_ptr<Client::Platform::SdlContext>, std::string> sdl =
        Client::Platform::SdlContext::Create();
    if (!sdl.HasValue())
    {
        return Client::Core::Unexpected(sdl.Error());
    }

    // Como Platform::Window::Create: los atributos del contexto van antes de crear la ventana.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, Client::Platform::GlMajorVersion);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, Client::Platform::GlMinorVersion);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    WindowPtr window(
        SDL_CreateWindow("MuRet CL5.0", HiddenWindowSide, HiddenWindowSide, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN));
    if (!window)
    {
        return Client::Core::Unexpected(std::string("no se pudo crear la ventana: ") + SDL_GetError());
    }
    // SDL_GL_CreateContext también lo deja como el contexto actual de este hilo.
    GlContextPtr context(SDL_GL_CreateContext(window.get()));
    if (!context)
    {
        return Client::Core::Unexpected("no se pudo crear el contexto OpenGL " + GlVersionText() +
                                        " core: " + SDL_GetError());
    }

    const Client::Core::Expected<Client::Platform::GlFunctions, std::string> base = Client::Platform::LoadGlFunctions();
    if (!base.HasValue())
    {
        return Client::Core::Unexpected(base.Error());
    }
    std::string contextError = CheckContext(base.Value());
    if (!contextError.empty())
    {
        return Client::Core::Unexpected(std::move(contextError));
    }
    const Client::Core::Expected<LabGl, std::string> gl = LoadLabGl(base.Value());
    if (!gl.HasValue())
    {
        return Client::Core::Unexpected(gl.Error());
    }
    return HiddenGlWindow(std::move(sdl).Value(), std::move(window), std::move(context), gl.Value());
}

const LabGl& HiddenGlWindow::Gl() const
{
    return m_gl;
}
} // namespace Labs
