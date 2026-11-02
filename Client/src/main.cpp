#include "App/CommandLine.h"
#include "App/WindowState.h"
#include "Platform/GlLoader.h"
#include "Platform/SdlContext.h"
#include "Platform/Window.h"
#include "Platform/WindowEvents.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Punto de entrada del cliente: arma las capas y arranca, sin lógica.
//
// Ola CL3.1: la ventana y el contexto GL están en Platform/ (SdlContext, Window, PumpEvents) y el
// estado de la ventana en App/WindowState.h. El bucle es todavía el hola mundo de la CL0.3: limpia
// la ventana de un color, imprime GL_VERSION y GL_RENDERER y cierra con la X; la CL3.2 lo cambia
// por el bucle con tick fijo. Los argumentos están en App/CommandLine.h.
namespace
{
namespace App = Client::App;
namespace Platform = Client::Platform;

// Un azul oscuro: cualquier color que no sea negro sirve para ver que el clear anduvo.
constexpr GLfloat ClearRed = 0.08f;
constexpr GLfloat ClearGreen = 0.10f;
constexpr GLfloat ClearBlue = 0.18f;

void PrintGlInfo(const Platform::GlFunctions& gl)
{
    std::printf("MuRet Client (CL0.3)\n");
    std::printf("GL_VERSION:  %s\n", Platform::GlString(gl, GL_VERSION));
    std::printf("GL_RENDERER: %s\n", Platform::GlString(gl, GL_RENDERER));
    std::fflush(stdout);
}

void SetViewport(const Platform::GlFunctions& gl, App::PixelSize size)
{
    gl.viewport(0, 0, size.width, size.height);
}

// Dibuja hasta que se cierre la ventana o se llegue a `maxFrames`. Devuelve cuántos dibujó.
std::uint64_t RunFrames(Platform::Window& window, std::optional<std::uint64_t> maxFrames)
{
    const Platform::GlFunctions& gl = window.Gl();
    App::WindowState state;
    state.pixelSize = window.PixelSize();
    App::PixelSize viewportSize = state.pixelSize;
    SetViewport(gl, viewportSize);

    std::uint64_t frames = 0;
    while (true)
    {
        Platform::PumpEvents(state);
        if (!state.running)
        {
            break;
        }
        // El viewport sigue a la ventana.
        if (state.pixelSize.width != viewportSize.width || state.pixelSize.height != viewportSize.height)
        {
            viewportSize = state.pixelSize;
            SetViewport(gl, viewportSize);
        }

        gl.clearColor(ClearRed, ClearGreen, ClearBlue, 1.0f);
        gl.clear(GL_COLOR_BUFFER_BIT);
        window.Swap();

        ++frames;
        if (maxFrames && frames >= *maxFrames)
        {
            break;
        }
    }
    return frames;
}
} // namespace

int main(int argc, char** argv)
{
    std::vector<std::string_view> arguments;
    for (int i = 1; i < argc; ++i)
    {
        arguments.emplace_back(argv[i]);
    }
    std::string error;
    const std::optional<App::CommandLine> commandLine = App::ParseCommandLine(arguments, error);
    if (!commandLine)
    {
        std::fprintf(stderr, "MuRet: %s\n", error.c_str());
        return EXIT_FAILURE;
    }

    const Client::Core::Expected<std::unique_ptr<Platform::SdlContext>, std::string> sdl =
        Platform::SdlContext::Create();
    if (!sdl.HasValue())
    {
        std::fprintf(stderr, "MuRet: %s\n", sdl.Error().c_str());
        return EXIT_FAILURE;
    }
    const Platform::WindowOptions options;
    Client::Core::Expected<Platform::Window, std::string> created = Platform::Window::Create(*sdl.Value(), options);
    if (!created.HasValue())
    {
        std::fprintf(stderr, "MuRet: %s\n", created.Error().c_str());
        return EXIT_FAILURE;
    }
    // Se destruye antes que `sdl`, que se declaró antes: la ventana muere antes del SDL_Quit.
    Platform::Window window = std::move(created).Value();
    PrintGlInfo(window.Gl());

    // VSync. Si el driver no lo deja, el hola mundo igual anda, solo que más rápido.
    if (window.VSync() != options.vsync)
    {
        std::fprintf(stderr, "MuRet: VSync: se pidió %s y quedó %s: %s\n", Platform::VSyncModeName(options.vsync),
                     Platform::VSyncModeName(window.VSync()), window.VSyncError().c_str());
    }

    const std::uint64_t frames = RunFrames(window, commandLine->maxFrames);
    std::printf("frames dibujados: %llu\n", static_cast<unsigned long long>(frames));
    return EXIT_SUCCESS;
}
