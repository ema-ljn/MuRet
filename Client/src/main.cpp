#include "App/AutoLogin.h"
#include "App/CommandLine.h"
#include "App/Config.h"
#include "App/ConnectionKeyFiles.h"
#include "App/Credentials.h"
#include "App/FontFiles.h"
#include "App/FrameRateMeter.h"
#include "App/GameLoop.h"
#include "App/LoopTiming.h"
#include "App/ObfuscationMode.h"
#include "App/Resolution.h"
#include "App/ServerAddress.h"
#include "App/ServerSession.h"
#include "App/SessionState.h"
#include "App/VSyncMode.h"
#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Core/FileSink.h"
#include "Core/IClock.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Core/StderrSink.h"
#include "Core/TeeSink.h"
#include "Input/InputState.h"
#include "Network/Connection.h"
#include "Network/FoxAntiObfuscation.h"
#include "Network/IObfuscation.h"
#include "Network/IdentityObfuscation.h"
#include "Network/SocketApi.h"
#include "Network/SystemResolver.h"
#include "Network/SystemSocket.h"
#include "Platform/BasePath.h"
#include "Platform/DisplayModes.h"
#include "Platform/GlLoader.h"
#include "Platform/SdlClock.h"
#include "Platform/SdlContext.h"
#include "Platform/SdlLog.h"
#include "Platform/SwapInterval.h"
#include "Platform/SystemFonts.h"
#include "Platform/Window.h"
#include "Platform/WindowEvents.h"
#include "Render/Gl/GlFrameTarget.h"
#include "Render/Gl/TextureTable.h"
#include "Render/Text/FreeTypeRasterizer.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Models/Cloth.h"
#include "Scenes/CharacterSelectScene.h"
#include "Scenes/GameData.h"
#include "Scenes/GameModels.h"
#include "Scenes/GameScene.h"
#include "Scenes/IScene.h"
#include "Scenes/LoadingScene.h"
#include "Scenes/WorldLoadingScene.h"
#include "Scenes/LoginScene.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SceneId.h"
#include "Scenes/SceneManager.h"
#include "Scenes/SceneText.h"
#include "Scenes/SceneTransitions.h"
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
//   1. ReadStartup, antes de SDL_Init: la carpeta del ejecutable, los argumentos y la config, y,
//      con un host (de la config o de /u /p), las claves de la conexión y, con --auto-login, antes
//      la cuenta y la clave del entorno (ReadServerStartup). Un error corta acá, sin abrir la
//      ventana, y va solo a stderr: el archivo del log lo dice la config.
//   2. El log entero, stderr y el archivo. Queda en main porque el TeeSink y el Log guardan
//      referencias a sus salidas, que tienen que vivir más que ellos. Con una carpeta de datos
//      (--data o [Data] Path), lo de WinMain antes de la escena 1 (ReadSceneStartup): Text.bmd y,
//      con su texto 0 como cara, los archivos de las fuentes; un error corta sin abrir la ventana.
//      Con un host, también la red del sistema (NetworkInit).
//   3. SDL (SdlContext) y OpenWindow: la resolución de la config, que tiene que valer
//      (CheckResolution), y la ventana con el contexto GL.
//   4. Con la carpeta de datos, RunScenes: la tabla de texturas, el frame de GL, con un host la
//      sesión (Online), y el SceneManager desde la carga (CL5.4), con el texto tecleado de SDL
//      prendido mientras la escena lo pide. Sin ella, la escena de prueba de la CL3.2. Las dos con
//      RunLoop, hasta que se cierre la ventana, se llegue a --frames, una escena no pueda entrar o
//      el OK de un aviso cierre el juego (Q-C69).
// El cierre es el orden de las declaraciones al revés: el bucle, el SceneManager (la escena de
// ahora suelta sus texturas), la sesión y su socket, el frame (con la caché del texto), la tabla de
// texturas, la ventana, el SdlLog, SDL_Quit (SdlContext), la red, el Log y al final sus salidas,
// para que lo que se cierra antes pueda escribir.
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
namespace Core = Client::Core;
namespace Network = Client::Network;
namespace Platform = Client::Platform;
namespace Scenes = Client::Scenes;

// El log, junto al ejecutable, si [Log] Path no dice otra cosa (CL3.4, fila "log" del contrato).
constexpr std::string_view LogFileName = "muret-client.log";

// La versión que muestra el login en el texto 456 (02 § Del launcher al juego): la del proyecto,
// del CMake.
#ifndef MURET_VERSION
#error "MURET_VERSION la define el CMake de Main"
#endif
constexpr std::string_view Version = MURET_VERSION;

// Lo que necesita la sesión con los servidores (CL4.5), leído en la fase 1 si hay un host. Sin
// operator<< ni log: la cuenta y la clave no se imprimen (pregunta 7), y las claves tampoco.
struct ServerStartup
{
    App::ServerAddress connectServer;
    // Solo con --auto-login: sin él, las teclea el jugador en la ventana de la cuenta.
    std::optional<App::Credentials> credentials;
    Network::ConnectionKeys keys;
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
                         .keys = keys.Value()};
}

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
    std::optional<ServerStartup> server;
    if (App::HasServerHost(config.Value()))
    {
        server = ReadServerStartup(config.Value(), commandLine.Value().autoLogin, log);
        if (!server)
        {
            return std::nullopt;
        }
    }
    return Startup{.folder = folder,
                   .commandLine = std::move(commandLine).Value(),
                   .config = std::move(config).Value(),
                   .server = std::move(server)};
}

void PrintGlInfo(const Platform::GlFunctions& gl)
{
    std::printf("MuRet Client\n");
    std::printf("GL_VERSION:  %s\n", Platform::GlString(gl, GL_VERSION));
    std::printf("GL_RENDERER: %s\n", Platform::GlString(gl, GL_RENDERER));
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
bool CheckResolution(const Platform::SdlContext& sdl, const App::ClientConfig& config, Core::Log& log)
{
    std::vector<App::Resolution> monitorModes;
    auto modes = Platform::MonitorResolutions(sdl);
    if (modes.HasValue())
    {
        monitorModes = std::move(modes).Value();
    }
    else
    {
        log.Warning("{}: valen solo las resoluciones del original", modes.Error());
    }
    const App::Resolution resolution{config.width, config.height};
    if (App::IsValidResolution(resolution, monitorModes))
    {
        return true;
    }
    log.Error("[Video] {} × {} no es una resolución que valga: van las del original y las del monitor ({})",
              resolution.width, resolution.height, App::DescribeValidResolutions(monitorModes));
    return false;
}

// Fase 3: la ventana con el tamaño y el VSync de la config. Si el driver no deja el VSync pedido,
// lo avisa y sigue: la lógica va igual a 25 pasos por segundo y el dibujo, más rápido.
std::optional<Platform::Window> OpenWindow(const Platform::SdlContext& sdl, const App::ClientConfig& config,
                                           Core::Log& log)
{
    if (!CheckResolution(sdl, config, log))
    {
        return std::nullopt;
    }
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

// La ofuscación de [Server] Obfuscation (pregunta 8 del plan de la CL4): la de FoxAnti es la de
// `foxAnti`, que vive en Online, y la identidad es una sola para todos.
const Network::IObfuscation& ChooseObfuscation(App::ObfuscationMode mode, const Network::FoxAntiObfuscation& foxAnti)
{
    if (mode == App::ObfuscationMode::FoxAnti)
    {
        return foxAnti;
    }
    return Network::IdentityObfuscation::Shared();
}

// Fase 4, con un servidor: el socket, la conexión con la ofuscación de la config, la sesión, que
// empieza la LoginScene al entrar, y con --auto-login el AutoLogin. Los miembros mueren al revés de
// como se declaran (el AutoLogin antes que la sesión que guarda), y todo antes que el NetworkInit,
// declarado antes en main.
//
// El relleno de lo que cifra la conexión sale de std::random_device: solo tiene que cambiar de una
// corrida a otra (Network/Connection.h), y los tests le dan uno fijo.
struct Online
{
    Online(const ServerStartup& server, App::ObfuscationMode obfuscation, Core::IClock& clock, Core::Log& log)
        : connection(socket, ChooseObfuscation(obfuscation, foxAnti), clock, log, std::random_device{}()),
          session(connection, clock, log, server.keys, App::SessionSettings{.connectServer = server.connectServer})
    {
        if (server.credentials)
        {
            autoLogin.emplace(session, *server.credentials, log);
        }
    }

    // El pollNetwork del SceneManager: la sesión y, después, el AutoLogin, que mira lo que llegó.
    void Poll()
    {
        session.Poll();
        if (autoLogin)
        {
            autoLogin->Poll();
        }
    }

    Network::SystemResolver resolver;
    Network::SystemSocket socket{resolver};
    const Network::FoxAntiObfuscation foxAnti;
    Network::Connection connection;
    App::ServerSession session;
    std::optional<App::AutoLogin> autoLogin;
};

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

// Fase 4: hasta que se cierre la ventana, se llegue a --frames o `stop` diga que no se sigue.
// `afterFrame` corre después de cada frame, antes de `stop`.
App::FrameStats RunLoop(Platform::Window& window, Core::IClock& clock, App::GameLoop& loop,
                        Client::Input::InputState& input, const App::CommandLine& commandLine, Core::Log& log,
                        const std::function<bool()>& stop = {}, const std::function<void()>& afterFrame = {})
{
    App::WindowState state;
    state.pixelSize = window.PixelSize();
    App::PixelSize viewport = state.pixelSize;
    SetViewport(window.Gl(), viewport);
    std::optional<App::FrameRateMeter> meter;
    if (commandLine.stats)
    {
        meter.emplace(clock.Now());
    }
    while (!commandLine.maxFrames || loop.Stats().frames < *commandLine.maxFrames)
    {
        // El mouse, en píxeles del framebuffer (CL5.5): la UI del login los usa.
        Platform::PumpEvents(state, input, window.Id(), window.PixelDensity());
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
Scenes::RasterizerFactory FontFactory(const App::ClientConfig& config, App::FontFiles files)
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

// Las cinco escenas (CL5.4 y CL6.3). Los modelos los lee la carga y los usa la selección.
Scenes::SceneFactory MakeSceneFactory(const Scenes::SceneContext& context, const Scenes::GameData& data,
                                      Scenes::GameModels& models, App::ServerSession* session)
{
    return [context, &data, &models, session](Scenes::SceneId id) -> std::unique_ptr<Scenes::IScene>
    {
        switch (id)
        {
        case Scenes::SceneId::Loading:
            return std::make_unique<Scenes::LoadingScene>(context, &models);
        case Scenes::SceneId::Login:
            return std::make_unique<Scenes::LoginScene>(context, data, models, session, Version, std::random_device{}(),
                                                        &models.TextureSlots());
        case Scenes::SceneId::CharacterSelect:
            return std::make_unique<Scenes::CharacterSelectScene>(context, data, models, session,
                                                                  &models.TextureSlots());
        case Scenes::SceneId::WorldLoading:
            return std::make_unique<Scenes::WorldLoadingScene>(context);
        case Scenes::SceneId::Game:
            return std::make_unique<Scenes::GameScene>(context, session);
        }
        return nullptr;
    };
}

// Lo que lee la fase 2 con una carpeta de datos.
struct SceneStartup
{
    Scenes::GameData data;
    App::FontFiles fonts;
};

// Fase 2, con una carpeta de datos: lo de WinMain antes de la escena 1, Text.bmd (0x4b9b9f) y los
// archivos de las fuentes, con su texto 0 como cara (App::ChooseFontFiles). Sin [Video] FontFile,
// la cara se busca en las fuentes del sistema, que solo Windows tiene. Cada error, al log.
std::optional<SceneStartup> ReadSceneStartup(const App::ClientConfig& config, Core::Log& log)
{
    auto data = Scenes::LoadGameData(*config.dataPath);
    if (!data.HasValue())
    {
        log.Error("{}", data.Error());
        return std::nullopt;
    }
    // La cara es un texto del juego: no va al log (App/FontFiles.h).
    const Scenes::GameData& loaded = data.Value();
    const std::string_view face = loaded.text ? loaded.text->Text(0).value_or("") : "";
    // La búsqueda, solo donde hay una lista de fuentes del sistema: en otro, ninguna, y el error de
    // ChooseFontFiles dice que el cliente no busca la cara.
    const App::SystemFontSearch search =
        Platform::HasSystemFontList() ? App::SystemFontSearch(Platform::FindSystemFontFile) : App::SystemFontSearch();
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
    TextInputFollower(Platform::Window& window, Core::Log& log)
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
    Platform::Window& m_window;
    Core::Log& m_log;
    bool m_on;
};

// Si el GameServer ordenó cerrar el juego (F1 02 con 0, Q-C106), en cualquier escena: el original
// hace WM_DESTROY apenas lo lee. Sin sesión, nunca.
bool GameServerOrderedTheQuit(const App::ServerSession* session)
{
    return session != nullptr && session->State() == App::SessionState::QuitOrdered;
}

// Fase 4 con una carpeta de datos: la tabla de texturas y el frame de GL, la sesión si hay un host,
// y el SceneManager desde la carga, con el texto tecleado mientras la escena lo pide. Un error
// antes del bucle, o una escena que no puede entrar, cierra con EXIT_FAILURE y el error en el log;
// el OK de un aviso que cierra el juego (Q-C69) y la orden del GameServer (Q-C106), con
// EXIT_SUCCESS, al final del frame en que llegan.
int RunScenes(const Startup& startup, const SceneStartup& scenes, Platform::Window& window, Core::IClock& clock,
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
    std::optional<Online> online;
    if (startup.server)
    {
        online.emplace(*startup.server, startup.config.obfuscation, clock, log);
    }
    App::ServerSession* session = online ? &online->session : nullptr;
    // Lo global de la tela: el rand() de la CRT sembrado una vez con la hora, como srand(time(NULL)) en
    // 0x4bad9a, el viento base y el contador de pasos de lógica.
    Client::Render::Models::ClothWorld clothWorld{
        .random = Client::Render::Models::CrtRandom(static_cast<std::uint32_t>(std::time(nullptr)))};
    // El teclado y el mouse del bucle: los pasos los reciben, y el dibujo del cursor los lee del contexto.
    Client::Input::InputState input;
    const Scenes::SceneContext context{.dataRoot = *startup.config.dataPath,
                                       .textures = textures,
                                       .log = log,
                                       .clock = &clock,
                                       .cloth = &clothWorld,
                                       .input = &input};
    // El cursor lo dibuja el juego: el del sistema no se ve sobre la ventana (0x4ba238). Si SDL no puede,
    // se ven los dos.
    if (const auto hidden = window.HideSystemCursor(); !hidden.HasValue())
    {
        log.Warning("{}", hidden.Error());
    }
    // Los modelos del personaje y de los ítems: los lee la carga (0x61ccb0) y quedan, como GameData.
    Scenes::GameModels models;
    Scenes::SceneManager manager(MakeSceneFactory(context, scenes.data, models, session), *frame.Value(), clock,
                                 online ? std::function<void()>([&online] { online->Poll(); }) : nullptr, log);
    if (!manager.Start(Scenes::FirstScene).HasValue())
    {
        return EXIT_FAILURE;
    }
    App::GameLoop loop{clock, manager, startup.config.pacing, LoopMaxFps(startup.config, window, log)};
    TextInputFollower textInput(window, log);
    // Lo que corta el bucle además de la ventana y el --frames.
    const auto stop = [&manager, session]
    { return manager.Failure().has_value() || manager.WantsToQuit() || GameServerOrderedTheQuit(session); };
    const App::FrameStats stats = RunLoop(window, clock, loop, input, startup.commandLine, log, stop,
                                          [&textInput, &manager] { textInput.Follow(manager.WantsTextInput()); });
    // Apagado en todas las salidas del bucle: la ventana, el --frames, la escena que falla, el OK o la
    // orden del GameServer.
    textInput.Follow(false);
    PrintFrames(stats);
    if (manager.Failure())
    {
        return EXIT_FAILURE;
    }
    if (GameServerOrderedTheQuit(session))
    {
        log.Info("se cierra el juego, como ordenó el GameServer");
    }
    else if (manager.WantsToQuit())
    {
        log.Info("se cierra el juego, como pidió el aviso");
    }
    return EXIT_SUCCESS;
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
