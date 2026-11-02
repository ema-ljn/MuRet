#pragma once

#include "App/VSyncMode.h"
#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Platform/GlLoader.h"
#include "Platform/SdlContext.h"

#include <SDL3/SDL_video.h>

#include <memory>
#include <string>

// La ventana del cliente con su contexto OpenGL 3.3 core (CL3.1, lo que en la CL0.3 era
// CreateGlWindow en main.cpp). El contexto es un recurso que vive dentro de la ventana: se crea
// después y se destruye antes.
namespace Client::Platform
{
struct WindowOptions
{
    // La resolución de referencia del original (D-C11 del roadmap del cliente).
    int width = 640;
    int height = 480;
    std::string title = "MuRet";
    bool resizable = true;
    App::VSyncMode vsync = App::VSyncMode::On;
};

struct WindowDeleter
{
    void operator()(SDL_Window* window) const;
};

struct GlContextDeleter
{
    void operator()(SDL_GLContextState* context) const;
};

class Window
{
public:
    // Necesita un SdlContext vivo. Crea la ventana, el contexto 3.3 core, carga GL y pide VSync.
    [[nodiscard]] static Core::Expected<Window, std::string> Create(const SdlContext& sdl,
                                                                    const WindowOptions& options);

    void Swap();
    [[nodiscard]] App::PixelSize PixelSize() const;
    // El que quedó después de ApplySwapInterval.
    [[nodiscard]] App::VSyncMode VSync() const;
    // La tabla de funciones de GL del contexto de esta ventana.
    [[nodiscard]] const GlFunctions& Gl() const;

private:
    using WindowPtr = std::unique_ptr<SDL_Window, WindowDeleter>;
    using GlContextPtr = std::unique_ptr<SDL_GLContextState, GlContextDeleter>;

    Window(WindowPtr window, GlContextPtr context, const GlFunctions& gl, App::VSyncMode vsync);

    // El contexto se declara después de la ventana: los miembros se destruyen al revés, así que el
    // contexto se libera antes que la ventana en la que vive.
    WindowPtr m_window;
    GlContextPtr m_context;
    GlFunctions m_gl;
    App::VSyncMode m_vsync;
};
} // namespace Client::Platform
