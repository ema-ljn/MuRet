#include "App/CommandLine.h"
#include "App/Config.h"
#include "App/GameLoop.h"
#include "App/ILoopClient.h"
#include "App/PacingMode.h"
#include "App/VSyncMode.h"
#include "App/LoopTiming.h"
#include "App/WindowState.h"
#include "Core/FileSink.h"
#include "Core/IClock.h"
#include "Core/Log.h"
#include "Core/StderrSink.h"
#include "Core/TeeSink.h"
#include "Input/InputState.h"
#include "Platform/BasePath.h"
#include "Platform/GlLoader.h"
#include "Platform/SdlClock.h"
#include "Platform/SdlContext.h"
#include "Platform/SdlLog.h"
#include "Platform/Window.h"
#include "Platform/WindowEvents.h"

#include <SDL3/SDL_filesystem.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Al final: en Windows define main como SDL_main, y así no alcanza a ningún otro header.
#include <SDL3/SDL_main.h>

// Punto de entrada del cliente: arma las capas y arranca, sin lógica.
//
// Ola CL3.2: la ventana y el contexto GL están en Platform/ (SdlContext, Window, PumpEvents), el
// reloj es el de SDL (SdlClock) y el bucle es App::GameLoop, con la lógica a 40 ms por paso y el
// dibujo interpolado, o con --pacing faithful, al ritmo del original (FaithfulPacer, VSync
// apagado). La escena es de prueba: el fondo cambia de color con un ciclo de 32 pasos. Desde la
// CL3.3, PumpEvents arma el Input::InputState del frame y el bucle se lo pasa a cada paso.
// Los argumentos están en App/CommandLine.h.
//
// Ola CL3.4 PR B: el log (Core::Log) va a stderr y a muret-client.log junto al ejecutable; si el
// archivo no abre, solo a stderr, con un aviso. Los mensajes de SDL entran por Platform::SdlLog.
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
// El log, junto al ejecutable (contrato de la CL3.4, fila "log"; `[Log] Path` llega con la config).
constexpr std::string_view LogFileName = "muret-client.log";
// Cada cuánto imprime --stats los FPS y los pasos por segundo (paso 3 de la CL3.2).
constexpr Core::Duration StatsInterval = std::chrono::seconds{5};

// La escena de prueba de la CL3.2: Step avanza el ciclo y Render pinta el fondo entre el paso
// actual y el próximo, con el alfa, y presenta (el swap va adentro de Render, ILoopClient).
class TestScene final : public App::ILoopClient
{
public:
    explicit TestScene(Platform::Window& window) : m_window(window) {}

    // La escena de prueba no lee el input todavía.
    void Step(const Client::Input::InputState& /*input*/) override
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
                        const App::CommandLine& commandLine, Core::Log& log)
{
    const Platform::GlFunctions& gl = window.Gl();
    App::WindowState state;
    Client::Input::InputState input;
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
        Platform::PumpEvents(state, input, window.Id());
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

        const std::uint64_t clampedBefore = loop.Stats().clampedFrames;
        loop.RunFrame(state, input);
        if (loop.Stats().clampedFrames != clampedBefore)
        {
            log.Warning("un frame pasó de {} ms: se corrieron solo los pasos del tope, y el resto se perdió",
                        std::chrono::duration_cast<std::chrono::milliseconds>(App::MaxFrameTime).count());
        }
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

// Con SDL_main.h, en Windows SDL pone el main de verdad y llama a este (que pasa a ser SDL_main)
// con los argumentos en UTF-8: SDL_RunApp los arma con GetCommandLineW y CommandLineToArgvW. Sin
// él, argv llega en la página ANSI y lo que no entra en ella llega cambiado por "?". En Linux no
// cambia nada.
int main(int argc, char* argv[])
{
    // La carpeta del ejecutable (SDL_GetBasePath, anda antes de SDL_Init): ahí están la config y,
    // si no dice otra cosa, el log. Sin ella, la carpeta actual.
    const auto exeFolder = Platform::ExecutableFolder();
    const std::filesystem::path folder = exeFolder.HasValue() ? exeFolder.Value() : std::filesystem::path();
    // Hasta resolver la config, los errores van solo a stderr: el archivo del log lo dice la config.
    Core::StderrSink errorOutput;
    Core::Log startupLog(errorOutput, Core::LogLevel::Info);
    if (!exeFolder.HasValue())
    {
        startupLog.Warning("{}; busco la config y el log en la carpeta actual", exeFolder.Error());
    }

    std::vector<std::string_view> arguments;
    for (int i = 1; i < argc; ++i)
    {
        arguments.emplace_back(argv[i]);
    }
    const auto commandLine = App::ParseCommandLine(arguments);
    if (!commandLine.HasValue())
    {
        startupLog.Error("{}", commandLine.Error());
        return EXIT_FAILURE;
    }
    // La config se resuelve antes de SDL_Init: un puerto malo o una carpeta de datos que no
    // existe terminan acá, sin abrir la ventana.
    const auto resolved = App::Resolve(folder, commandLine.Value());
    if (!resolved.HasValue())
    {
        startupLog.Error("{}", resolved.Error());
        return EXIT_FAILURE;
    }
    const App::ClientConfig& config = resolved.Value();

    // El log entero: stderr y el archivo de [Log] Path (Resolve ya lo tomó desde la carpeta del
    // ejecutable), o muret-client.log junto al ejecutable. Las salidas viven más que el Log, y el
    // Log más que el SdlLog (declarado más abajo, se destruye antes).
    const std::filesystem::path logPath = config.logPath.value_or(folder / LogFileName);
    Core::Expected<std::unique_ptr<Core::FileSink>, std::string> logFile = Core::FileSink::Open(logPath);
    std::optional<Core::TeeSink> tee;
    if (logFile.HasValue())
    {
        tee.emplace(errorOutput, *logFile.Value());
    }
    Core::ILogSink& logOutput = tee ? static_cast<Core::ILogSink&>(*tee) : errorOutput;
    Core::Log log(logOutput, Core::LogLevel::Info);
    if (!logFile.HasValue())
    {
        log.Warning("{}: el log sigue solo en stderr", logFile.Error());
    }
    log.Info("MuRet Client arranca");
    const App::PacingMode pacing = config.pacing;

    const Client::Core::Expected<std::unique_ptr<Platform::SdlContext>, std::string> sdl =
        Platform::SdlContext::Create();
    if (!sdl.HasValue())
    {
        log.Error("{}", sdl.Error());
        return EXIT_FAILURE;
    }
    // Después del Log y del SdlContext: se destruye antes que los dos. SDL_Quit no devuelve la
    // salida por defecto de SDL, así que el SdlLog tiene que morir antes (Platform/SdlLog.h).
    const Platform::SdlLog sdlLog(log);
    Platform::WindowOptions options;
    options.width = config.width;
    options.height = config.height;
    // Resolve siempre lo deja puesto: On en el modo normal y Off en el fiel, si no se dio.
    options.vsync = config.vsync.value_or(App::VSyncMode::On);
    Client::Core::Expected<Platform::Window, std::string> created = Platform::Window::Create(*sdl.Value(), options);
    if (!created.HasValue())
    {
        log.Error("{}", created.Error());
        return EXIT_FAILURE;
    }
    // Se destruye antes que `sdl`, que se declaró antes: la ventana muere antes del SDL_Quit.
    Platform::Window window = std::move(created).Value();
    PrintGlInfo(window.Gl());

    // Si el driver no deja el VSync pedido, el bucle igual anda: la lógica sigue a 25 pasos por
    // segundo y el dibujo va más rápido.
    if (window.VSync() != options.vsync)
    {
        log.Warning("VSync: se pidió {} y quedó {}: {}", Platform::VSyncModeName(options.vsync),
                    Platform::VSyncModeName(window.VSync()), window.VSyncError());
    }

    Platform::SdlClock clock;
    TestScene scene{window};
    App::GameLoop loop{clock, scene, pacing};
    const App::FrameStats stats = RunLoop(window, clock, loop, commandLine.Value(), log);
    std::printf("frames: %llu, pasos: %llu\n", static_cast<unsigned long long>(stats.frames),
                static_cast<unsigned long long>(stats.steps));
    return EXIT_SUCCESS;
}
