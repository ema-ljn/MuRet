#include "App/CommandLine.h"
#include "App/GameLoop.h"
#include "App/ILoopClient.h"
#include "App/PacingMode.h"
#include "App/VSyncMode.h"
#include "App/WindowState.h"
#include "Core/IClock.h"
#include "Platform/GlLoader.h"
#include "Platform/SdlClock.h"
#include "Platform/SdlContext.h"
#include "Platform/Window.h"
#include "Platform/WindowEvents.h"

#include <chrono>
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
// Ola CL3.2: la ventana y el contexto GL están en Platform/ (SdlContext, Window, PumpEvents), el
// reloj es el de SDL (SdlClock) y el bucle es App::GameLoop, con la lógica a 40 ms por paso y el
// dibujo interpolado. La escena es de prueba: el fondo cambia de color con un ciclo de 32 pasos.
// Los argumentos están en App/CommandLine.h.
namespace
{
namespace App = Client::App;
namespace Core = Client::Core;
namespace Platform = Client::Platform;

// El ciclo de 32 pasos de DAT_07d2d360, (n + 1) mod 32 en cada paso (02, "Qué corre por paso").
constexpr std::uint32_t CycleSteps = 32;
// El fondo va de un azul oscuro a uno claro y vuelve en cada ciclo, 1,28 s.
constexpr GLfloat DarkRed = 0.08f;
constexpr GLfloat DarkGreen = 0.10f;
constexpr GLfloat DarkBlue = 0.18f;
constexpr GLfloat LightRed = 0.20f;
constexpr GLfloat LightGreen = 0.35f;
constexpr GLfloat LightBlue = 0.60f;
// Cada cuánto imprime --stats los FPS y los pasos por segundo (paso 3 de la CL3.2).
constexpr Core::Duration StatsInterval = std::chrono::seconds{5};

// La escena de prueba de la CL3.2: Step avanza el ciclo y Render pinta el fondo entre el paso
// actual y el próximo, con el alfa, y presenta (el swap va adentro de Render, ILoopClient).
class TestScene final : public App::ILoopClient
{
public:
    explicit TestScene(Platform::Window& window) : m_window(window) {}

    void Step() override
    {
        m_cycle = (m_cycle + 1) % CycleSteps;
    }

    void Render(float alpha) override
    {
        // La posición en el ciclo, en [0, 1), y una rampa que sube y baja: en 32 vuelve a 0 sin
        // salto, así que la interpolación es continua también ahí.
        const float position = (static_cast<float>(m_cycle) + alpha) / static_cast<float>(CycleSteps);
        const float ramp = position < 0.5f ? 2.0f * position : 2.0f - 2.0f * position;
        const Platform::GlFunctions& gl = m_window.Gl();
        gl.clearColor(DarkRed + ramp * (LightRed - DarkRed), DarkGreen + ramp * (LightGreen - DarkGreen),
                      DarkBlue + ramp * (LightBlue - DarkBlue), 1.0f);
        gl.clear(GL_COLOR_BUFFER_BIT);
        m_window.Swap();
    }

private:
    Platform::Window& m_window;
    std::uint32_t m_cycle = 0;
};

void PrintGlInfo(const Platform::GlFunctions& gl)
{
    std::printf("MuRet Client\n");
    std::printf("GL_VERSION:  %s\n", Platform::GlString(gl, GL_VERSION));
    std::printf("GL_RENDERER: %s\n", Platform::GlString(gl, GL_RENDERER));
    std::fflush(stdout);
}

void SetViewport(const Platform::GlFunctions& gl, App::PixelSize size)
{
    gl.viewport(0, 0, size.width, size.height);
}

// Lo que imprime --stats: los frames y los pasos desde el último aviso, por segundo.
class StatsPrinter
{
public:
    explicit StatsPrinter(Core::Duration start) : m_last(start) {}

    void Update(Core::Duration now, const App::FrameStats& stats)
    {
        const Core::Duration elapsed = now - m_last;
        if (elapsed < StatsInterval)
        {
            return;
        }
        const double seconds = std::chrono::duration<double>(elapsed).count();
        std::printf("FPS: %.2f, pasos por segundo: %.2f\n", static_cast<double>(stats.frames - m_frames) / seconds,
                    static_cast<double>(stats.steps - m_steps) / seconds);
        std::fflush(stdout);
        m_last = now;
        m_frames = stats.frames;
        m_steps = stats.steps;
    }

private:
    Core::Duration m_last;
    std::uint64_t m_frames = 0;
    std::uint64_t m_steps = 0;
};

// Corre el bucle hasta que se cierre la ventana o se llegue a `maxFrames`.
App::FrameStats RunLoop(Platform::Window& window, Core::IClock& clock, App::GameLoop& loop,
                        const App::CommandLine& commandLine)
{
    const Platform::GlFunctions& gl = window.Gl();
    App::WindowState state;
    state.pixelSize = window.PixelSize();
    App::PixelSize viewportSize = state.pixelSize;
    SetViewport(gl, viewportSize);
    std::optional<StatsPrinter> stats;
    if (commandLine.stats)
    {
        stats.emplace(clock.Now());
    }

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

        loop.RunFrame(state);
        if (stats)
        {
            stats->Update(clock.Now(), loop.Stats());
        }
        if (commandLine.maxFrames && loop.Stats().frames >= *commandLine.maxFrames)
        {
            break;
        }
    }
    return loop.Stats();
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
    const App::PacingMode pacing = commandLine->pacing.value_or(App::PacingMode::Decoupled);
    if (pacing == App::PacingMode::Faithful)
    {
        std::fprintf(stderr, "MuRet: --pacing faithful todavía no está (llega en el PR B de la CL3.2)\n");
        return EXIT_FAILURE;
    }

    const Client::Core::Expected<std::unique_ptr<Platform::SdlContext>, std::string> sdl =
        Platform::SdlContext::Create();
    if (!sdl.HasValue())
    {
        std::fprintf(stderr, "MuRet: %s\n", sdl.Error().c_str());
        return EXIT_FAILURE;
    }
    Platform::WindowOptions options;
    options.vsync = commandLine->vsync.value_or(App::VSyncMode::On);
    Client::Core::Expected<Platform::Window, std::string> created = Platform::Window::Create(*sdl.Value(), options);
    if (!created.HasValue())
    {
        std::fprintf(stderr, "MuRet: %s\n", created.Error().c_str());
        return EXIT_FAILURE;
    }
    // Se destruye antes que `sdl`, que se declaró antes: la ventana muere antes del SDL_Quit.
    Platform::Window window = std::move(created).Value();
    PrintGlInfo(window.Gl());

    // Si el driver no deja el VSync pedido, el bucle igual anda: la lógica sigue a 25 pasos por
    // segundo y el dibujo va más rápido.
    if (window.VSync() != options.vsync)
    {
        std::fprintf(stderr, "MuRet: VSync: se pidió %s y quedó %s: %s\n", Platform::VSyncModeName(options.vsync),
                     Platform::VSyncModeName(window.VSync()), window.VSyncError().c_str());
    }

    Platform::SdlClock clock;
    TestScene scene{window};
    App::GameLoop loop{clock, scene, pacing};
    const App::FrameStats stats = RunLoop(window, clock, loop, *commandLine);
    std::printf("frames: %llu, pasos: %llu\n", static_cast<unsigned long long>(stats.frames),
                static_cast<unsigned long long>(stats.steps));
    return EXIT_SUCCESS;
}
