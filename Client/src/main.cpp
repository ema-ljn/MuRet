#include "App/CommandLine.h"
#include "App/Config.h"
#include "App/FrameRateMeter.h"
#include "App/GameLoop.h"
#include "App/LoopTiming.h"
#include "App/VSyncMode.h"
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
#include "Scenes/TestScene.h"

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

// El punto de entrada del cliente: arma las capas y arranca, sin lógica propia. main va por fases,
// como los pasos de WinMain en MuMain (InitializeWorkingDirectoryAndLog, MainLoop, ShutdownRuntime
// en App/Platform/Windows/Winmain.cpp), y las de más de un paso tienen su función:
//   1. ReadStartup, antes de SDL_Init: la carpeta del ejecutable, los argumentos y la config. Un
//      error corta acá, sin abrir la ventana, y va solo a stderr: el archivo del log lo dice la
//      config.
//   2. El log entero, stderr y el archivo. Queda en main porque el TeeSink y el Log guardan
//      referencias a sus salidas, que tienen que vivir más que ellos.
//   3. SDL (SdlContext) y OpenWindow: la ventana y el contexto GL.
//   4. RunLoop: el bucle hasta que se cierre la ventana o se llegue a --frames.
// El cierre es el orden de las declaraciones de main al revés: el bucle y la escena, la ventana, el
// SdlLog, SDL_Quit (SdlContext), el Log y al final sus salidas, para que lo que se cierra antes
// pueda escribir.
namespace
{
namespace App = Client::App;
namespace Core = Client::Core;
namespace Platform = Client::Platform;
namespace Scenes = Client::Scenes;

// El log, junto al ejecutable, si [Log] Path no dice otra cosa (CL3.4, fila "log" del contrato).
constexpr std::string_view LogFileName = "muret-client.log";

// Lo que sale de la fase 1.
struct Startup
{
    // La carpeta del ejecutable, o vacía (la actual) si SDL no la da.
    std::filesystem::path folder;
    App::CommandLine commandLine;
    App::ClientConfig config;
};

// Fase 1. SDL_GetBasePath anda antes de SDL_Init: en la carpeta del ejecutable están la config y,
// si no dice otra cosa, el log.
std::optional<Startup> ReadStartup(int argc, char* argv[], Core::Log& log)
{
    const auto exeFolder = Platform::ExecutableFolder();
    if (!exeFolder.HasValue())
    {
        log.Warning("{}; busco la config y el log en la carpeta actual", exeFolder.Error());
    }
    const std::filesystem::path folder = exeFolder.HasValue() ? exeFolder.Value() : std::filesystem::path();

    std::vector<std::string_view> arguments;
    for (int i = 1; i < argc; ++i)
    {
        arguments.emplace_back(argv[i]);
    }
    auto commandLine = App::ParseCommandLine(arguments);
    if (!commandLine.HasValue())
    {
        log.Error("{}", commandLine.Error());
        return std::nullopt;
    }
    auto config = App::Resolve(folder, commandLine.Value());
    if (!config.HasValue())
    {
        log.Error("{}", config.Error());
        return std::nullopt;
    }
    return Startup{
        .folder = folder, .commandLine = std::move(commandLine).Value(), .config = std::move(config).Value()};
}

void PrintGlInfo(const Platform::GlFunctions& gl)
{
    std::printf("MuRet Client\n");
    std::printf("GL_VERSION:  %s\n", Platform::GlString(gl, GL_VERSION));
    std::printf("GL_RENDERER: %s\n", Platform::GlString(gl, GL_RENDERER));
    std::fflush(stdout);
}

// Fase 3: la ventana con el tamaño y el VSync de la config. Si el driver no deja el VSync pedido,
// lo avisa y sigue: la lógica va igual a 25 pasos por segundo y el dibujo, más rápido.
std::optional<Platform::Window> OpenWindow(const Platform::SdlContext& sdl, const App::ClientConfig& config,
                                           Core::Log& log)
{
    // Resolve siempre deja puesto el VSync: On en el modo normal y Off en el fiel, si no se dio.
    const Platform::WindowOptions options{
        .width = config.width, .height = config.height, .vsync = config.vsync.value_or(App::VSyncMode::On)};
    auto created = Platform::Window::Create(sdl, options);
    if (!created.HasValue())
    {
        log.Error("{}", created.Error());
        return std::nullopt;
    }
    Platform::Window window = std::move(created).Value();
    PrintGlInfo(window.Gl());
    if (window.VSync() != options.vsync)
    {
        log.Warning("VSync: se pidió {} y quedó {}: {}", Platform::VSyncModeName(options.vsync),
                    Platform::VSyncModeName(window.VSync()), window.VSyncError());
    }
    return window;
}

// El tope de FPS va solo con el VSync que quedó apagado, como en MuMain (App::EffectiveMaxFps).
int LoopMaxFps(const App::ClientConfig& config, const Platform::Window& window, Core::Log& log)
{
    const int maxFps = App::EffectiveMaxFps(config.maxFps, window.VSync());
    if (config.maxFps != maxFps)
    {
        log.Info("el tope de FPS no se usa con VSync {}: el swap ya frena al refresco",
                 Platform::VSyncModeName(window.VSync()));
    }
    return maxFps;
}

// Lo que la escena de prueba le pide a la ventana: el fondo de un color, y el swap.
void Present(Platform::Window& window, const Scenes::TestScene::Color& color)
{
    const Platform::GlFunctions& gl = window.Gl();
    gl.clearColor(color.red, color.green, color.blue, 1.0f);
    gl.clear(GL_COLOR_BUFFER_BIT);
    window.Swap();
}

void SetViewport(const Platform::GlFunctions& gl, App::PixelSize size)
{
    gl.viewport(0, 0, size.width, size.height);
}

// El viewport sigue al área de dibujo, que cambia con PixelSizeChanged.
void FollowPixelSize(const Platform::GlFunctions& gl, App::PixelSize size, App::PixelSize& viewport)
{
    if (size.width != viewport.width || size.height != viewport.height)
    {
        viewport = size;
        SetViewport(gl, viewport);
    }
}

// Un frame de GameLoop, con el aviso de la CL3.4 si se recortó (pasó de MaxFrameTime).
void RunFrame(App::GameLoop& loop, const App::WindowState& state, Client::Input::InputState& input, Core::Log& log)
{
    const std::uint64_t clampedBefore = loop.Stats().clampedFrames;
    loop.RunFrame(state, input);
    if (loop.Stats().clampedFrames != clampedBefore)
    {
        log.Warning("un frame pasó de {} ms: se corrieron solo los pasos del tope, y el resto se perdió",
                    std::chrono::duration_cast<std::chrono::milliseconds>(App::MaxFrameTime).count());
    }
}

// --stats: los FPS y los pasos por segundo, cada StatsInterval.
void PrintStats(App::FrameRateMeter& meter, Core::Duration now, const App::FrameStats& stats)
{
    if (const auto rates = meter.Update(now, stats))
    {
        std::printf("FPS: %.2f, pasos por segundo: %.2f\n", rates->framesPerSecond, rates->stepsPerSecond);
        std::fflush(stdout);
    }
}

// Fase 4: hasta que se cierre la ventana o se llegue a --frames.
App::FrameStats RunLoop(Platform::Window& window, Core::IClock& clock, App::GameLoop& loop,
                        const App::CommandLine& commandLine, Core::Log& log)
{
    App::WindowState state;
    state.pixelSize = window.PixelSize();
    Client::Input::InputState input;
    App::PixelSize viewport = state.pixelSize;
    SetViewport(window.Gl(), viewport);
    std::optional<App::FrameRateMeter> meter;
    if (commandLine.stats)
    {
        meter.emplace(clock.Now());
    }
    while (!commandLine.maxFrames || loop.Stats().frames < *commandLine.maxFrames)
    {
        Platform::PumpEvents(state, input, window.Id());
        if (!state.running)
        {
            break;
        }
        FollowPixelSize(window.Gl(), state.pixelSize, viewport);
        RunFrame(loop, state, input, log);
        if (meter)
        {
            PrintStats(*meter, clock.Now(), loop.Stats());
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
    Core::StderrSink errorOutput;
    Core::Log startupLog(errorOutput, Core::LogLevel::Info);
    const std::optional<Startup> startup = ReadStartup(argc, argv, startupLog);
    if (!startup)
    {
        return EXIT_FAILURE;
    }

    // Fase 2: stderr y el archivo de [Log] Path (Resolve ya lo tomó desde la carpeta del
    // ejecutable), o muret-client.log junto al ejecutable. Si no abre, solo stderr.
    const auto logFile = Core::FileSink::Open(startup->config.logPath.value_or(startup->folder / LogFileName));
    std::optional<Core::TeeSink> tee;
    if (logFile.HasValue())
    {
        tee.emplace(errorOutput, *logFile.Value());
    }
    Core::Log log(tee ? static_cast<Core::ILogSink&>(*tee) : errorOutput, Core::LogLevel::Info);
    if (!logFile.HasValue())
    {
        log.Warning("{}: el log sigue solo en stderr", logFile.Error());
    }
    log.Info("MuRet Client arranca");

    const auto sdl = Platform::SdlContext::Create();
    if (!sdl.HasValue())
    {
        log.Error("{}", sdl.Error());
        return EXIT_FAILURE;
    }
    // Después del SdlContext, así muere antes: SDL_Quit no devuelve la salida por defecto de SDL
    // (Platform/SdlLog.h).
    const Platform::SdlLog sdlLog(log);
    std::optional<Platform::Window> window = OpenWindow(*sdl.Value(), startup->config, log);
    if (!window)
    {
        return EXIT_FAILURE;
    }

    Platform::SdlClock clock;
    Scenes::TestScene scene{[&window](const Scenes::TestScene::Color& color) { Present(*window, color); }};
    App::GameLoop loop{clock, scene, startup->config.pacing, LoopMaxFps(startup->config, *window, log)};
    const App::FrameStats stats = RunLoop(*window, clock, loop, startup->commandLine, log);
    std::printf("frames: %llu, pasos: %llu\n", static_cast<unsigned long long>(stats.frames),
                static_cast<unsigned long long>(stats.steps));
    return EXIT_SUCCESS;
}
