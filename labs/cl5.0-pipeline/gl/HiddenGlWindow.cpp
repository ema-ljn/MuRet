#include "HiddenGlWindow.h"

#include "Core/Environment.h"

#include <SDL3/SDL_error.h>

#include <memory>
#include <string>
#include <utility>

namespace Labs
{
namespace
{
// La ventana no se dibuja: el tamaño da igual, y uno chico pide poco.
constexpr int HiddenWindowSide = 64;

// Con el contexto ya actual: la tabla del cliente, que mira la versión y el perfil antes de pedir
// el resto (desde la CL5.1), y recién ahí la del lab.
Client::Core::Expected<LabGl, std::string> LoadCheckedGl()
{
    const Client::Core::Expected<Client::Platform::GlFunctions, std::string> base = Client::Platform::LoadGlFunctions();
    if (!base.HasValue())
    {
        return Client::Core::Unexpected(base.Error());
    }
    return LoadLabGl(base.Value());
}
} // namespace

bool IsGlRequired(const std::optional<std::string>& value)
{
    return value.has_value() && *value == "1";
}

std::optional<std::string> ReadRequireGl()
{
    return Client::Core::ReadEnvironmentVariable(RequireGlVariable);
}

HiddenGlWindow::HiddenGlWindow(SdlPtr sdl, WindowPtr window, GlContextPtr context, const LabGl& gl)
    : m_sdl(std::move(sdl)), m_window(std::move(window)), m_context(std::move(context)), m_gl(gl)
{
}

// Si algo falla en el medio, lo creado se destruye al revés: el contexto, la ventana y SDL.
Client::Core::Expected<std::unique_ptr<HiddenGlWindow>, std::string> HiddenGlWindow::Create()
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
        return Client::Core::Unexpected("no se pudo crear el contexto OpenGL " + Client::Platform::GlVersionText() +
                                        " core: " + SDL_GetError());
    }

    Client::Core::Expected<LabGl, std::string> gl = LoadCheckedGl();
    if (!gl.HasValue())
    {
        return Client::Core::Unexpected(gl.Error());
    }
    // El constructor es privado: make_unique no lo ve.
    return std::unique_ptr<HiddenGlWindow>(
        new HiddenGlWindow(std::move(sdl).Value(), std::move(window), std::move(context), gl.Value()));
}

const LabGl& HiddenGlWindow::Gl() const
{
    return m_gl;
}
} // namespace Labs
