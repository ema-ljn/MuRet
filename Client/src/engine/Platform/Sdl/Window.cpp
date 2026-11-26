#include "Platform/Sdl/Window.h"

#include "Platform/VSyncMode.h"
#include "Core/Expected.h"
#include "Core/PixelSize.h"
#include "Platform/Sdl/GlLoader.h"
#include "Platform/Sdl/SdlContext.h"
#include "Platform/Sdl/SwapInterval.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_messagebox.h>
#include <SDL3/SDL_video.h>

#include <string>
#include <utility>

namespace Client::Platform::Sdl
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
    // The context attributes are requested before creating the window.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, GlMajorVersion);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, GlMinorVersion);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    // The original's 16-bit depth (cDepthBits at 0x4b8e10), SDL3's default: explicit, so the window and the tests'
    // framebuffer have the same one.
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);

    WindowPtr window(SDL_CreateWindow(options.title.c_str(), options.width, options.height, SDL_WINDOW_OPENGL));
    if (!window)
    {
        return Core::Unexpected(std::string("no se pudo crear la ventana: ") + SDL_GetError());
    }
    // SDL_GL_CreateContext also makes it this thread's current context.
    GlContextPtr context(SDL_GL_CreateContext(window.get()));
    if (!context)
    {
        return Core::Unexpected("no se pudo crear el contexto OpenGL " + GlVersionText() + " core: " + SDL_GetError());
    }

    // LoadGlFunctions also checks the context's version and profile (3.3 or more, core).
    const Core::Expected<GlFunctions, std::string> gl = LoadGlFunctions();
    if (!gl.HasValue())
    {
        return Core::Unexpected(gl.Error());
    }

    SwapIntervalResult vsync = ApplySwapInterval(options.vsync);
    return Window(std::move(window), std::move(context), gl.Value(), std::move(vsync));
}

Core::Expected<void, std::string> Window::ShowErrorMessage(const std::string& message)
{
    if (!SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, SDL_GetWindowTitle(m_window.get()), message.c_str(),
                                  m_window.get()))
        return Core::Unexpected(std::string("no se pudo mostrar el aviso de datos: ") + SDL_GetError());
    return {};
}

void Window::Swap()
{
    SDL_GL_SwapWindow(m_window.get());
}

Core::PixelSize Window::PixelSize() const
{
    Core::PixelSize size;
    SDL_GetWindowSizeInPixels(m_window.get(), &size.width, &size.height);
    return size;
}

SDL_WindowID Window::Id() const
{
    return SDL_GetWindowID(m_window.get());
}

VSyncMode Window::VSync() const
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

float Window::PixelDensity() const
{
    return SDL_GetWindowPixelDensity(m_window.get());
}

Core::Expected<void, std::string> Window::StartTextInput()
{
    if (!SDL_StartTextInput(m_window.get()))
    {
        return Core::Unexpected(std::string("no se pudo prender el texto tecleado: ") + SDL_GetError());
    }
    return {};
}

Core::Expected<void, std::string> Window::StopTextInput()
{
    if (!SDL_StopTextInput(m_window.get()))
    {
        return Core::Unexpected(std::string("no se pudo apagar el texto tecleado: ") + SDL_GetError());
    }
    return {};
}

Core::Expected<void, std::string> Window::HideSystemCursor()
{
    // SDL_HideCursor is SDL-wide, not per window: it belongs to the window because the window is the only one.
    if (!SDL_HideCursor())
    {
        return Core::Unexpected(std::string("no se pudo esconder el cursor del sistema: ") + SDL_GetError());
    }
    return {};
}

bool Window::TextInputActive() const
{
    return SDL_TextInputActive(m_window.get());
}
} // namespace Client::Platform::Sdl
