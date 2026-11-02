#include "Platform/Window.h"

#include <SDL3/SDL_error.h>

#include <string>
#include <utility>

// Window: la ventana SDL3 y su contexto GL.
namespace Client::Platform
{
void WindowDeleter::operator()(SDL_Window* window) const
{
    SDL_DestroyWindow(window);
}

void GlContextDeleter::operator()(SDL_GLContextState* context) const
{
    SDL_GL_DestroyContext(context);
}

Window::Window(WindowPtr window, GlContextPtr context, const GlFunctions& gl, SwapIntervalResult vsync)
    : m_window(std::move(window)), m_context(std::move(context)), m_gl(gl), m_vsync(std::move(vsync))
{
}

Core::Expected<Window, std::string> Window::Create(const SdlContext& /*sdl*/, const WindowOptions& options)
{
    const std::string glVersion = std::to_string(GlMajorVersion) + "." + std::to_string(GlMinorVersion);

    // Los atributos del contexto se piden antes de crear la ventana.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, GlMajorVersion);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, GlMinorVersion);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_WindowFlags flags = SDL_WINDOW_OPENGL;
    if (options.resizable)
    {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    WindowPtr window(SDL_CreateWindow(options.title.c_str(), options.width, options.height, flags));
    if (!window)
    {
        return Core::Unexpected(std::string("no se pudo crear la ventana: ") + SDL_GetError());
    }
    // SDL_GL_CreateContext también lo deja como el contexto actual de este hilo.
    GlContextPtr context(SDL_GL_CreateContext(window.get()));
    if (!context)
    {
        return Core::Unexpected("no se pudo crear el contexto OpenGL " + glVersion + " core: " + SDL_GetError());
    }

    const Core::Expected<GlFunctions, std::string> gl = LoadGlFunctions();
    if (!gl.HasValue())
    {
        return Core::Unexpected(gl.Error());
    }
    GLint major = 0;
    GLint minor = 0;
    gl.Value().getIntegerv(GL_MAJOR_VERSION, &major);
    gl.Value().getIntegerv(GL_MINOR_VERSION, &minor);
    if (!HasRequiredGlVersion(major, minor))
    {
        return Core::Unexpected("se pidió OpenGL " + glVersion + " y el driver dio " + std::to_string(major) + "." +
                                std::to_string(minor));
    }

    SwapIntervalResult vsync = ApplySwapInterval(options.vsync);
    return Window(std::move(window), std::move(context), gl.Value(), std::move(vsync));
}

void Window::Swap()
{
    SDL_GL_SwapWindow(m_window.get());
}

App::PixelSize Window::PixelSize() const
{
    App::PixelSize size;
    SDL_GetWindowSizeInPixels(m_window.get(), &size.width, &size.height);
    return size;
}

App::VSyncMode Window::VSync() const
{
    return m_vsync.mode;
}

const std::string& Window::VSyncError() const
{
    return m_vsync.error;
}

const GlFunctions& Window::Gl() const
{
    return m_gl;
}
} // namespace Client::Platform
