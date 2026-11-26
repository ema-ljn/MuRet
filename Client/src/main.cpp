#include "App/CommandLine.h"
#include "App/Config.h"
#include "Session/ConnectionKeyFiles.h"
#include "Session/Credentials.h"
#include "App/FontFiles.h"
#include "App/FrameRateMeter.h"
#include "App/GameClient.h"
#include "App/GameLoop.h"
#include "App/LoopTiming.h"
#include "App/MakeObfuscation.h"
#include "Platform/Resolution.h"
#include "App/ServerAddress.h"
#include "Session/ServerSession.h"
#include "Platform/VSyncMode.h"
#include "Platform/WindowState.h"
#include "Sound/SoundCatalog.h"
#include "Diagnostics/CrashHandler.h"
#include "Core/Expected.h"
#include "Core/FileSink.h"
#include "Core/IClock.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Core/PixelSize.h"
#include "Core/StderrSink.h"
#include "Core/TeeSink.h"
#include "Tables/ItemTable.h"
#include "Input/InputState.h"
#include "Network/Connection.h"
#include "Network/IObfuscation.h"
#include "Network/SocketApi.h"
#include "Network/SystemResolver.h"
#include "Network/SystemSocket.h"
#include "Platform/Sdl/BasePath.h"
#include "Platform/Sdl/Clipboard.h"
#include "Platform/Sdl/DisplayModes.h"
#include "Platform/Sdl/GlLoader.h"
#include "Platform/Sdl/LocalTime.h"
#include "Platform/Sdl/SdlClock.h"
#include "Audio/Sdl/SdlAudio.h"
#include "Platform/Sdl/SdlContext.h"
#include "Platform/Sdl/SdlLog.h"
#include "Platform/Sdl/SwapInterval.h"
#include "Platform/Sdl/SystemFonts.h"
#include "Platform/Sdl/Window.h"
#include "Platform/Sdl/WindowEvents.h"
#include "Protocol/Session/MainCheck.h"
#include "Render/Gl/GlFrameTarget.h"
#include "Render/Gl/GlScreenReader.h"
#include "Render/Gl/TextureTable.h"
#include "Render/Text/FreeTypeRasterizer.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Tables/GameData.h"
#include "Render/Frame/SceneText.h"
#include "Scenes/TestScene.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Al final: en Windows define main como SDL_main, y así no alcanza a ningún otro header. Nada
// de lo que trae se nombra, e include-cleaner lo daría por sobrante.
#include <SDL3/SDL_main.h> // IWYU pragma: keep

// El punto de entrada del cliente: arma las capas y arranca, sin lógica propia. main va por fases,
// como los pasos de WinMain en MuMain (InitializeWorkingDirectoryAndLog, MainLoop, ShutdownRuntime
// en App/Platform/Windows/Winmain.cpp), y las de más de un paso tienen su función:
//   0. El manejador de caídas, antes que nada, como la primera llamada de WinMain (0x4b9b80, en 0x4b9b97): si
//      el cliente se cae, escribe MuError.dmp en la carpeta actual y, con el log ya abierto, su
//      línea (Core/Crash/CrashHandler.h). Si no se puede instalar, un aviso y se sigue sin él.
//   1. The command line (App/CommandLine.h): the help and the version go to stdout with exit code 0, a usage
//      error and the usage to stderr with exit code 2. Then ReadStartup, before SDL_Init: the executable's folder
//      and the config, and, with a host (from the config or the command line), the connection keys and, with
//      --auto-login, first the account and the password from the environment (ReadServerStartup). An error stops
//      here, without opening the window, and goes only to stderr: the config names the log file.
//   2. El log entero, stderr y el archivo. Queda en main porque el TeeSink y el Log guardan
//      referencias a sus salidas, que tienen que vivir más que ellos. Con una carpeta de datos
//      (--data o [Data] Path), lo de WinMain antes de la escena 1 (ReadSceneStartup): Text.bmd y,
//      con su texto 0 como cara, los archivos de las fuentes; un error corta sin abrir la ventana.
//      Con un host, también la red del sistema (NetworkInit).
//   3. SDL (SdlContext) y OpenWindow: la resolución de la config, que tiene que valer
//      (CheckResolution), y la ventana con el contexto GL.
//   4. Con la carpeta de datos, RunScenes: los dispositivos (la tabla de texturas, el frame de GL,
//      el audio y, con un host, el socket) y App::GameClient, que arma la sesión y el SceneManager
//      desde la carga (CL5.4), con el texto tecleado de SDL prendido mientras la escena lo pide. Sin ella, la escena de
//      prueba de la CL3.2. Las dos con RunLoop, hasta que se cierre la ventana, se llegue a --frames, una escena no
//      pueda entrar o el OK de un aviso cierre el juego (Q-C69).
// El cierre es el orden de las declaraciones al revés: el bucle, el GameClient (la escena de ahora
// suelta sus texturas y la sesión se cierra), el socket, el frame (con la caché del texto), la tabla
// de texturas, la ventana, el SdlLog, SDL_Quit (SdlContext), la red, el Log y sus salidas, para que
// lo que se cierra antes pueda escribir, y al final el manejador de caídas.
//
// Con un host, la LoginScene empieza la sesión al entrar y muestra su UI (CL5.5): la lista cuando
// llega, la cuenta con el saludo del GameServer y los avisos. El SceneManager consulta la sesión en
// cada frame, antes que la escena, y con --auto-login, después de la sesión, el AutoLogin, que
// elige y entra sin la UI. La sesión deja en el log cada estado, del connect al login aceptado, y
// cada keepalive. Si falla, la escena muestra su aviso y no se reconecta sola; el OK del aviso
// cierra el juego con EXIT_SUCCESS, como el WM_CLOSE del original. Un host exige la carpeta de
// datos (las claves están ahí), así que la escena de prueba nunca tiene red: sin host no mira el
// entorno ni las claves, no arma la red, y `Main --frames 60` corre igual sin un servidor ni datos.
namespace
{
namespace App = Client::App;
namespace Audio = Client::Audio;
namespace Core = Client::Core;
namespace Network = Client::Network;
namespace Platform = Client::Platform;
namespace Scenes = Client::Scenes;

// El log, junto al ejecutable, si [Log] Path no dice otra cosa (CL3.4, fila "log" del contrato).
constexpr std::string_view LogFileName = "muret-client.log";

// La versión que muestra el login en el texto 456 (02 § Del launcher al juego): la del proyecto,
// del CMake.
#ifndef MU_VERSION
#error "MU_VERSION la define el CMake de Main"
#endif
constexpr std::string_view Version = MU_VERSION;

// Lo que necesita la sesión con los servidores (CL4.5), leído en la fase 1 si hay un host. Sin
// operator<< ni log: la cuenta y la clave no se imprimen (pregunta 7), y las claves tampoco.
struct ServerStartup
{
    App::ServerAddress connectServer;
    // Solo con --auto-login: sin él, las teclea el jugador en la ventana de la cuenta.
    std::optional<App::Credentials> credentials;
    Network::ConnectionKeys keys;
    // El ejecutable que suma la respuesta al 03 del GS (ServerSession::OnMainCheck).
    std::filesystem::path mainCheckFile;
};

// Lo que sale de la fase 1.
struct Startup
{
    // La carpeta del ejecutable, o vacía (la actual) si SDL no la da.
    std::filesystem::path folder;
    App::CommandLine commandLine;
    App::ClientConfig config;
    // Con un host: el servidor, las claves y, con --auto-login, la cuenta. Vacío sin host.
    std::optional<ServerStartup> server;
};

// Fase 1, con un host: con --auto-login, primero la cuenta y la clave del entorno, y después las
// claves de la carpeta de datos (el plan de la CL4, CL4.4). Cada error dice qué falta y cómo darlo,
// sin un valor. Sin --auto-login el entorno no se mira.
std::optional<ServerStartup> ReadServerStartup(const App::ClientConfig& config, bool autoLogin, Core::Log& log)
{
    std::optional<App::Credentials> credentials;
    if (autoLogin)
    {
        auto read = App::ReadCredentials(App::SystemEnvironment);
        if (!read.HasValue())
        {
            log.Error("{}", read.Error());
            return std::nullopt;
        }
        credentials = std::move(read).Value();
    }
    const auto keys = App::LoadConnectionKeys(config.dataPath);
    if (!keys.HasValue())
    {
        log.Error("{}", keys.Error());
        return std::nullopt;
    }
    return ServerStartup{.connectServer = config.server.value_or(App::ServerAddress{}),
                         .credentials = std::move(credentials),
                         .keys = keys.Value(),
                         .mainCheckFile = {}};
}

// Phase 1: argv[1..], in UTF-8 on Windows too (SDL_main.h).
Core::Expected<App::CommandLine, App::CommandLineExit> ParseArguments(int argc, char* argv[])
{
    std::vector<std::string_view> arguments;
    for (int i = 1; i < argc; ++i)
    {
        arguments.emplace_back(argv[i]);
    }
    return App::ParseCommandLine(arguments, Version);
}

// Fase 1. SDL_GetBasePath anda antes de SDL_Init: en la carpeta del ejecutable están la config y,
// si no dice otra cosa, el log.
std::optional<Startup> ReadStartup(int argc, char* argv[], App::CommandLine commandLine, Core::Log& log)
{
    const auto exeFolder = Platform::Sdl::ExecutableFolder();
    if (!exeFolder.HasValue())
    {
        log.Warning("{}; busco la config y el log en la carpeta actual", exeFolder.Error());
    }
    const std::filesystem::path folder = exeFolder.HasValue() ? exeFolder.Value() : std::filesystem::path();

    // La línea de comandos que 0x4ad4a0 pide con GetCommandLineA para sumar su ejecutable: argv[0] y
    // los argumentos separados por un espacio. 0x4ad270 corta el nombre en el primer espacio o
    // comilla, así que las comillas que GetCommandLineA conserva no cambian el resultado.
    std::string rawCommandLine;
    for (int i = 0; i < argc; ++i)
    {
        if (i > 0)
        {
            rawCommandLine += ' ';
        }
        rawCommandLine += argv[i];
    }
    auto config = App::Resolve(folder, commandLine);
    if (!config.HasValue())
    {
        log.Error("{}", config.Error());
        return std::nullopt;
    }
    std::optional<ServerStartup> server;
    if (App::HasServerHost(config.Value()))
    {
        server = ReadServerStartup(config.Value(), commandLine.autoLogin, log);
        if (!server)
        {
            return std::nullopt;
        }
        server->mainCheckFile = Client::Protocol::Session::MainCheckFileName(rawCommandLine);
    }
    return Startup{.folder = folder,
                   .commandLine = std::move(commandLine),
                   .config = std::move(config).Value(),
                   .server = std::move(server)};
}

void PrintGlInfo(const Platform::Sdl::GlFunctions& gl)
{
    std::printf("MuRet Client\n");
    std::printf("GL_VERSION:  %s\n", Platform::Sdl::GlString(gl, GL_VERSION));
    std::printf("GL_RENDERER: %s\n", Platform::Sdl::GlString(gl, GL_RENDERER));
    std::fflush(stdout);
}

// Fase 2, con un servidor: la red del sistema, WSAStartup en Windows (Network/SocketApi.h). false
// si no arranca, con el error en el log.
bool StartNetwork(std::optional<Network::SocketApi::NetworkInit>& network, Core::Log& log)
{
    network.emplace();
    if (!network->IsReady())
    {
        log.Error("no arranca la red del sistema (WSAStartup dio {})", network->SystemError());
        return false;
    }
    return true;
}

// Fase 3, antes de la ventana: [Video] Width y Height tienen que ser una de las cinco del original
// o un modo del monitor (App/Resolution.h). Si SDL no da los modos, valen las cinco y lo avisa.
// false si no vale, con las que valen en el log.
bool CheckResolution(const Platform::Sdl::SdlContext& sdl, const App::ClientConfig& config, Core::Log& log)
{
    std::vector<Platform::Resolution> monitorModes;
    auto modes = Platform::Sdl::MonitorResolutions(sdl);
    if (modes.HasValue())
    {
        monitorModes = std::move(modes).Value();
    }
    else
    {
        log.Warning("{}: valen solo las resoluciones del original", modes.Error());
    }
    const Platform::Resolution resolution{config.width, config.height};
    if (Platform::IsValidResolution(resolution, monitorModes))
    {
        return true;
    }
    log.Error("[Video] {} × {} no es una resolución que valga: van las del original y las del monitor ({})",
              resolution.width, resolution.height, Platform::DescribeValidResolutions(monitorModes));
    return false;
}

// Fase 3: la ventana con el tamaño y el VSync de la config. Si el driver no deja el VSync pedido,
// lo avisa y sigue: la lógica va igual a 25 pasos por segundo y el dibujo, más rápido.
std::optional<Platform::Sdl::Window> OpenWindow(const Platform::Sdl::SdlContext& sdl, const App::ClientConfig& config,
                                                Core::Log& log)
{
    if (!CheckResolution(sdl, config, log))
    {
        return std::nullopt;
    }
    // Resolve siempre deja puesto el VSync: On en el modo normal y Off en el fiel, si no se dio.
    const Platform::Sdl::WindowOptions options{
        .width = config.width, .height = config.height, .vsync = config.vsync.value_or(Platform::VSyncMode::On)};
    auto created = Platform::Sdl::Window::Create(sdl, options);
    if (!created.HasValue())
    {
        log.Error("{}", created.Error());
        return std::nullopt;
    }
    Platform::Sdl::Window window = std::move(created).Value();
    PrintGlInfo(window.Gl());
    if (window.VSync() != options.vsync)
    {
        log.Warning("VSync: se pidió {} y quedó {}: {}", Platform::Sdl::VSyncModeName(options.vsync),
                    Platform::Sdl::VSyncModeName(window.VSync()), window.VSyncError());
    }
    return window;
}

// El tope de FPS va solo con el VSync que quedó apagado, como en MuMain (App::EffectiveMaxFps).
int LoopMaxFps(const App::ClientConfig& config, const Platform::Sdl::Window& window, Core::Log& log)
{
    const int maxFps = App::EffectiveMaxFps(config.maxFps, window.VSync());
    if (config.maxFps != maxFps)
    {
        log.Info("el tope de FPS no se usa con VSync {}: el swap ya frena al refresco",
                 Platform::Sdl::VSyncModeName(window.VSync()));
    }
    return maxFps;
}

// Lo que la escena de prueba le pide a la ventana: el fondo de un color, y el swap.
void Present(Platform::Sdl::Window& window, const Scenes::TestScene::Color& color)
{
    const Platform::Sdl::GlFunctions& gl = window.Gl();
    gl.clearColor(color.red, color.green, color.blue, 1.0f);
    gl.clear(GL_COLOR_BUFFER_BIT);
    window.Swap();
}

void SetViewport(const Platform::Sdl::GlFunctions& gl, Core::PixelSize size)
{
    gl.viewport(0, 0, size.width, size.height);
}

// El viewport sigue al área de dibujo, que cambia con PixelSizeChanged.
void FollowPixelSize(const Platform::Sdl::GlFunctions& gl, Core::PixelSize size, Core::PixelSize& viewport)
{
    if (size.width != viewport.width || size.height != viewport.height)
    {
        viewport = size;
        SetViewport(gl, viewport);
    }
}

// Un frame de GameLoop, con el aviso de la CL3.4 si se recortó (pasó de MaxFrameTime).
void RunFrame(App::GameLoop& loop, const Platform::WindowState& state, Client::Input::InputState& input, Core::Log& log)
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

// Fase 4: hasta que se cierre la ventana, se llegue a --frames o `stop` diga que no se sigue.
// `afterFrame` corre después de cada frame, antes de `stop`.
App::FrameStats RunLoop(Platform::Sdl::Window& window, Core::IClock& clock, App::GameLoop& loop,
                        Client::Input::InputState& input, const App::CommandLine& commandLine, Core::Log& log,
                        const std::function<bool()>& stop = {}, const std::function<void()>& afterFrame = {})
{
    Platform::WindowState state;
    state.pixelSize = window.PixelSize();
    Core::PixelSize viewport = state.pixelSize;
    SetViewport(window.Gl(), viewport);
    std::optional<App::FrameRateMeter> meter;
    if (commandLine.stats)
    {
        meter.emplace(clock.Now());
    }
    while (!commandLine.maxFrames || loop.Stats().frames < *commandLine.maxFrames)
    {
        // El mouse, en píxeles del framebuffer (CL5.5): la UI del login los usa.
        Platform::Sdl::PumpEvents(state, input, window.Id(), window.PixelDensity());
        if (!state.running)
        {
            break;
        }
        FollowPixelSize(window.Gl(), state.pixelSize, viewport);
        RunFrame(loop, state, input, log);
        if (afterFrame)
        {
            afterFrame();
        }
        if (stop && stop())
        {
            break;
        }
        if (meter)
        {
            PrintStats(*meter, clock.Now(), loop.Stats());
        }
    }
    return loop.Stats();
}

void PrintFrames(const App::FrameStats& stats)
{
    std::printf("frames: %llu, pasos: %llu\n", static_cast<unsigned long long>(stats.frames),
                static_cast<unsigned long long>(stats.steps));
}

// Las fuentes de `files` para cada ancho de pantalla (Scenes/SceneText.h). Si no abren, el error
// de FontOpenError: sin [Video] FontFile, la ruta salió de la búsqueda y no va al log.
Client::Render::Frame::RasterizerFactory FontFactory(const App::ClientConfig& config, App::FontFiles files)
{
    return [&config, files = std::move(files)](
               int screenWidth) -> Core::Expected<std::unique_ptr<Client::Render::Text::IGlyphRasterizer>, std::string>
    {
        auto opened = Client::Render::Text::FreeTypeRasterizer::Open(files.regular, files.bold, screenWidth);
        if (!opened.HasValue())
        {
            return Core::Unexpected(App::FontOpenError(config, opened.Error()));
        }
        return std::unique_ptr<Client::Render::Text::IGlyphRasterizer>(std::move(opened).Value());
    };
}

// Lo que lee la fase 2 con una carpeta de datos.
struct SceneStartup
{
    Client::Tables::GameData data;
    App::FontFiles fonts;
};

// Fase 2, con una carpeta de datos: lo de WinMain antes de la escena 1, Text.bmd (0x4b9b9f) y los
// archivos de las fuentes, con su texto 0 como cara (App::ChooseFontFiles). Sin [Video] FontFile,
// la cara se busca en las fuentes del sistema, que solo Windows tiene. Cada error, al log.
std::optional<SceneStartup> ReadSceneStartup(const App::ClientConfig& config, Core::Log& log)
{
    auto data = Client::Tables::LoadGameData(*config.dataPath);
    if (!data.HasValue())
    {
        log.Error("{}", data.Error());
        return std::nullopt;
    }
    // La cara es un texto del juego: no va al log (App/FontFiles.h).
    const Client::Tables::GameData& loaded = data.Value();
    if (loaded.items && !loaded.items->checksumMatches)
    {
        log.Warning("Item.bmd: el checksum no coincide; se continúa con la tabla cargada");
    }
    const std::string_view face = loaded.text ? loaded.text->Text(0).value_or("") : "";
    // La búsqueda, solo donde hay una lista de fuentes del sistema: en otro, ninguna, y el error de
    // ChooseFontFiles dice que el cliente no busca la cara.
    const App::SystemFontSearch search = Platform::Sdl::HasSystemFontList()
                                             ? App::SystemFontSearch(Platform::Sdl::FindSystemFontFile)
                                             : App::SystemFontSearch();
    auto fonts = App::ChooseFontFiles(config, face, search);
    if (!fonts.HasValue())
    {
        log.Error("{}", fonts.Error());
        return std::nullopt;
    }
    return SceneStartup{.data = std::move(data).Value(), .fonts = std::move(fonts).Value()};
}

// El texto tecleado de SDL, prendido solo mientras la escena lo pide (IScene::WantsTextInput): SDL3
// pide prenderlo solo cuando se quiere texto, porque puede mostrar un IME o un teclado en pantalla
// y saltear teclas (SDL_keyboard.h), y el original solo tiene un EDIT con foco mientras está la
// cuenta. Follow prende o apaga cuando cambia el pedido; si SDL falla, una línea al log y no
// insiste hasta el próximo cambio: sin el texto se puede elegir y cancelar, pero no teclear.
class TextInputFollower
{
public:
    TextInputFollower(Platform::Sdl::Window& window, Core::Log& log)
        : m_window(window), m_log(log), m_on(window.TextInputActive())
    {
    }

    void Follow(bool wanted)
    {
        if (wanted == m_on)
        {
            return;
        }
        m_on = wanted;
        const auto switched = wanted ? m_window.StartTextInput() : m_window.StopTextInput();
        if (!switched.HasValue())
        {
            m_log.Warning("{}: {}", switched.Error(),
                          wanted ? "la ventana de la cuenta no va a recibir lo tecleado"
                                 : "el texto tecleado queda prendido");
        }
    }

private:
    Platform::Sdl::Window& m_window;
    Core::Log& m_log;
    bool m_on;
};

// Fase 4, con un host: el resolver y el socket del GameServer. Viven más que el GameClient, que guarda la sesión.
struct HostDevices
{
    Network::SystemResolver resolver;
    Network::SystemSocket socket{resolver};
};

// Fase 4, con un host: lo que GameClient necesita para la sesión. El relleno de lo que cifra la conexión sale de
// std::random_device: solo tiene que cambiar de una corrida a otra (Network/Connection.h). Las salas del chat con un
// amigo abren un socket por sala con el mismo resolver y la ofuscación del build.
App::GameClientServer ServerOf(const ServerStartup& server, HostDevices& host, const Network::IObfuscation& obfuscation)
{
    return App::GameClientServer{
        .socket = host.socket,
        .obfuscation = obfuscation,
        .keys = server.keys,
        .settings = App::SessionSettings{.connectServer = server.connectServer, .mainCheckFile = server.mainCheckFile},
        .autoLogin = server.credentials,
        .paddingSeed = std::random_device{}(),
        .chatRooms =
            App::ChatRoomSockets{.make = [&host] { return std::make_unique<Network::SystemSocket>(host.resolver); },
                                 .obfuscation = &obfuscation,
                                 .paddingSeed = std::random_device{}()}};
}

// Fase 4: el audio de SDL, o ninguno con el aviso en el log.
std::unique_ptr<Audio::Sdl::SdlAudio> OpenAudio(const App::ClientConfig& config, Core::Log& log)
{
    auto created = Audio::Sdl::SdlAudio::Create(*config.dataPath, Client::Sound::GameSoundCatalog(), config.soundOnOff,
                                                config.musicOnOff);
    if (!created.HasValue())
    {
        log.Warning("{}", created.Error());
        return nullptr;
    }
    std::unique_ptr<Audio::Sdl::SdlAudio> audio = std::move(created).Value();
    if (const auto driver = audio->DriverName(); !driver.empty())
    {
        log.Info("el audio usa el controlador SDL {}", driver);
    }
    return audio;
}

// Fase 4, después del bucle y con el cliente y la ventana vivos: el código de salida de lo que lo cortó.
int ExitCodeOf(const std::optional<App::GameExit>& exit, Platform::Sdl::Window& window, Core::Log& log)
{
    if (!exit)
    {
        return EXIT_SUCCESS;
    }
    switch (exit->kind)
    {
    case App::GameExitKind::DataError:
    {
        // Q-C180: primero MessageBox, después WM_DESTROY. Se cierra también si SDL no logró mostrarlo.
        const auto shown = window.ShowErrorMessage(exit->message);
        if (!shown.HasValue())
        {
            log.Error("{}", shown.Error());
        }
        return EXIT_FAILURE;
    }
    case App::GameExitKind::SceneFailure:
        // El SceneManager ya registró el diagnóstico de Enter o de aplicación de la sesión.
        return EXIT_FAILURE;
    case App::GameExitKind::SelectionFailure:
        log.Error("no se puede construir la selección: {}", exit->message);
        return EXIT_FAILURE;
    case App::GameExitKind::ServerOrder:
        log.Info("se cierra el juego, como ordenó el GameServer");
        return EXIT_SUCCESS;
    case App::GameExitKind::PlayerQuit:
        log.Info("se cierra el juego, como pidió el aviso");
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}

// Fase 4 con una carpeta de datos: los dispositivos (la tabla de texturas, el frame de GL, el audio, el portapapeles,
// la lectura de la pantalla y, con un host, el socket y la ofuscación), después el GameClient, y el bucle con el texto
// tecleado mientras la escena lo pide. Un error antes del bucle, o una escena que no puede entrar, cierra con
// EXIT_FAILURE y el error en el log; el OK de un aviso que cierra el juego (Q-C69) y la orden del GameServer (Q-C106),
// con EXIT_SUCCESS, al final del frame en que llegan.
int RunScenes(const Startup& startup, const SceneStartup& scenes, Platform::Sdl::Window& window, Core::IClock& clock,
              Core::Log& log)
{
    Client::Render::Gl::TextureTable textures(window.Gl());
    auto frame = Client::Render::Gl::GlFrameTarget::Create(
        window.Gl(), textures, FontFactory(startup.config, scenes.fonts), [&window] { return window.PixelSize(); },
        [&window] { window.Swap(); }, log);
    if (!frame.HasValue())
    {
        log.Error("{}", frame.Error());
        return EXIT_FAILURE;
    }
    Client::Input::InputState input;
    const std::unique_ptr<Audio::Sdl::SdlAudio> audio = OpenAudio(startup.config, log);
    Platform::Sdl::Clipboard clipboard;
    Client::Render::Gl::GlScreenReader screenReader(window.Gl());
    // The server's packet shape is chosen when building (MU_XOR_REMAP); it outlives the connection.
    const std::unique_ptr<const Network::IObfuscation> obfuscation = App::MakeObfuscation();
    std::optional<HostDevices> host;
    std::optional<App::GameClientServer> server;
    if (startup.server)
    {
        host.emplace();
        server.emplace(ServerOf(*startup.server, *host, *obfuscation));
    }
    App::GameClient client(
        scenes.data,
        App::GameClientDevices{.frame = *frame.Value(),
                               .textures = textures,
                               .textMeasure = frame.Value()->TextMeasure(),
                               .screenReader = screenReader,
                               .clock = clock,
                               .log = log,
                               .input = input,
                               .screen = [&window] { return window.PixelSize(); },
                               .localTime = Platform::Sdl::ReadLocalTime,
                               .playback = audio.get(),
                               .clipboard = &clipboard},
        App::GameClientOptions{.dataRoot = *startup.config.dataPath,
                               .version = std::string(Version),
                               .pacing = startup.config.pacing,
                               .music = startup.config.musicOnOff,
                               .crtSeed = static_cast<std::uint32_t>(std::time(nullptr)),
                               .loginSeed = [] { return static_cast<std::uint32_t>(std::random_device{}()); },
                               .screenshotFolder = {}},
        std::move(server));
    // El cursor lo dibuja el juego: el del sistema no se ve sobre la ventana (0x4ba238). Si SDL no puede,
    // se ven los dos.
    if (const auto hidden = window.HideSystemCursor(); !hidden.HasValue())
    {
        log.Warning("{}", hidden.Error());
    }
    if (!client.Start().HasValue())
    {
        return EXIT_FAILURE;
    }
    App::GameLoop loop{clock, client.Loop(), startup.config.pacing, LoopMaxFps(startup.config, window, log)};
    TextInputFollower textInput(window, log);
    const App::FrameStats stats = RunLoop(
        window, clock, loop, input, startup.commandLine, log, [&client] { return client.Exit().has_value(); },
        [&textInput, &client] { textInput.Follow(client.WantsTextInput()); });
    // Apagado en todas las salidas del bucle: la ventana, el --frames, la escena que falla, el OK o la
    // orden del GameServer.
    textInput.Follow(false);
    PrintFrames(stats);
    return ExitCodeOf(client.Exit(), window, log);
}
} // namespace

// Con SDL_main.h, en Windows SDL pone el main de verdad y llama a este (que pasa a ser SDL_main)
// con los argumentos en UTF-8: SDL_RunApp los arma con GetCommandLineW y CommandLineToArgvW. Sin
// él, argv llega en la página ANSI y lo que no entra en ella llega cambiado por "?". En Linux no
// cambia nada.
int main(int argc, char* argv[])
{
    // Fase 0. Se declara primero para morir último: cubre también el cierre.
    const auto crashHandler = Client::Diagnostics::CrashHandler::Install(Client::Diagnostics::CrashDumpFileName);
    Core::StderrSink errorOutput;
    Core::Log startupLog(errorOutput, Core::LogLevel::Info);
    if (!crashHandler.HasValue())
    {
        startupLog.Warning("{}: el cliente sigue sin manejador de caídas", crashHandler.Error());
    }
    auto commandLine = ParseArguments(argc, argv);
    if (!commandLine.HasValue())
    {
        const App::CommandLineExit& earlyExit = commandLine.Error();
        std::fputs(earlyExit.text.c_str(), earlyExit.exitCode == EXIT_SUCCESS ? stdout : stderr);
        return earlyExit.exitCode;
    }
    const std::optional<Startup> startup = ReadStartup(argc, argv, std::move(commandLine).Value(), startupLog);
    if (!startup)
    {
        return EXIT_FAILURE;
    }

    // Fase 2: stderr y el archivo de [Log] Path (Resolve ya lo tomó desde la carpeta del
    // ejecutable), o muret-client.log junto al ejecutable. Si no abre, solo stderr.
    const std::filesystem::path logPath = startup->config.logPath.value_or(startup->folder / LogFileName);
    const auto logFile = Core::FileSink::Open(logPath);
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
    // La línea de una caída va al mismo archivo, como la de 0x4b9b3e al MuError.log del original.
    if (logFile.HasValue() && crashHandler.HasValue() && !crashHandler.Value()->SetLogFile(logPath))
    {
        log.Warning("la ruta del log es demasiado larga para el manejador de caídas");
    }
    log.Info("MuRet Client arranca");
    std::optional<SceneStartup> scenes;
    if (startup->config.dataPath)
    {
        scenes = ReadSceneStartup(startup->config, log);
        if (!scenes)
        {
            return EXIT_FAILURE;
        }
    }

    // Antes de SDL, así cierra después: todo socket tiene que morir antes que el NetworkInit.
    std::optional<Network::SocketApi::NetworkInit> network;
    if (startup->server && !StartNetwork(network, log))
    {
        return EXIT_FAILURE;
    }

    const auto sdl = Platform::Sdl::SdlContext::Create();
    if (!sdl.HasValue())
    {
        log.Error("{}", sdl.Error());
        return EXIT_FAILURE;
    }
    // Después del SdlContext, así muere antes: SDL_Quit no devuelve la salida por defecto de SDL
    // (Platform/SdlLog.h).
    const Platform::Sdl::SdlLog sdlLog(log);
    std::optional<Platform::Sdl::Window> window = OpenWindow(*sdl.Value(), startup->config, log);
    if (!window)
    {
        return EXIT_FAILURE;
    }

    Platform::Sdl::SdlClock clock;
    if (scenes)
    {
        return RunScenes(*startup, *scenes, *window, clock, log);
    }
    // Sin datos (el CI): la escena de prueba, sin red, porque un host exige la carpeta de datos.
    Scenes::TestScene scene{[&window](const Scenes::TestScene::Color& color) { Present(*window, color); }};
    App::GameLoop loop{clock, scene, startup->config.pacing, LoopMaxFps(startup->config, *window, log)};
    Client::Input::InputState input;
    PrintFrames(RunLoop(*window, clock, loop, input, startup->commandLine, log));
    return EXIT_SUCCESS;
}
