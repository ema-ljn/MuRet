#include "App/CommandLine.h"
#include "App/Config.h"
#include "Session/ConnectionKeyFiles.h"
#include "Session/Credentials.h"
#include "App/FontFiles.h"
#include "App/FrameRateMeter.h"
#include "App/GameClient.h"
#include "App/GameLoop.h"
#include "Kernel/LoopTiming.h"
#include "App/MakeObfuscation.h"
#include "Platform/Resolution.h"
#include "Session/ServerAddress.h"
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

// Last: on Windows it defines main as SDL_main, so it reaches no other header. Nothing it brings in is named, and
// include-cleaner would flag it as unused.
#include <SDL3/SDL_main.h> // IWYU pragma: keep

// The client's entry point: it builds the layers and starts, with no logic of its own. main goes in phases, like the
// WinMain steps in MuMain (InitializeWorkingDirectoryAndLog, MainLoop, ShutdownRuntime in
// App/Platform/Windows/Winmain.cpp), and those of more than one step have their own function:
//   0. The crash handler, before anything, like WinMain's first call (0x4b9b80, at 0x4b9b97): if the client
//      crashes, it writes MuError.dmp in the current folder and, with the log already open, its line
//      (Diagnostics/CrashHandler.h). If it cannot be installed, a warning and it goes on without it.
//   1. The command line (App/CommandLine.h): the help and the version go to stdout with exit code 0, a usage
//      error and the usage to stderr with exit code 2. Then ReadStartup, before SDL_Init: the executable's folder
//      and the config, and, with a host (from the config or the command line), the connection keys and, with
//      --auto-login, first the account and the password from the environment (ReadServerStartup). An error stops
//      here, without opening the window, and goes only to stderr: the config names the log file.
//   2. The whole log, stderr and the file. It stays in main because the TeeSink and the Log keep references to
//      their outputs, which must outlive them. With a data folder (--data or [Data] Path), what WinMain does
//      before scene 1 (ReadSceneStartup): Text.bmd and, with its text 0 as the face, the font files; an error
//      stops without opening the window. With a host, also the system network (NetworkInit).
//   3. SDL (SdlContext) and OpenWindow: the config's resolution, which must be valid (CheckResolution), and the
//      window with the GL context.
//   4. With the data folder, RunScenes: the devices (the texture table, the GL frame, the audio and, with a host,
//      the socket) and App::GameClient, which builds the session and the SceneManager from the load (CL5.4), with
//      SDL's text input on while the scene asks for it. Without it, the CL3.2 test scene. Both with RunLoop,
//      until the window closes, --frames is reached, a scene cannot enter or the OK of a notice closes the game
//      (Q-C69).
// The shutdown is the declaration order reversed: the loop, the GameClient (the current scene releases its textures
// and the session closes), the socket, the frame (with the text cache), the texture table, the window, the SdlLog,
// SDL_Quit (SdlContext), the network, the Log and its outputs, so that what closes earlier can write, and last the
// crash handler.
//
// With a host, the LoginScene starts the session on entering and shows its UI (CL5.5). The SceneManager polls the
// session every frame, before the scene, and with --auto-login, after the session, the AutoLogin, which chooses and
// enters without the UI. If the session fails, the scene shows its notice and does not reconnect by itself; the
// notice's OK closes the game with EXIT_SUCCESS, like the original's WM_CLOSE. A host requires the data folder (the
// keys are there), so the test scene never has network: without a host it reads neither the environment nor the
// keys, sets up no network, and `Main --frames 60` runs the same without a server or data.
namespace
{
namespace App = Client::App;
namespace Audio = Client::Audio;
namespace Core = Client::Core;
namespace Network = Client::Network;
namespace Platform = Client::Platform;
namespace Scenes = Client::Scenes;

// Next to the executable, unless [Log] Path says otherwise.
constexpr std::string_view LogFileName = "muret-client.log";

// The version the login shows in text 456 (02 § Del launcher al juego): the project's, from CMake.
#ifndef MU_VERSION
#error "MU_VERSION la define el CMake de Main"
#endif
constexpr std::string_view Version = MU_VERSION;

// What the session with the servers needs (CL4.5), read in phase 1 if there is a host. No operator<< or log: the
// account and the password are not printed (question 7), nor are the keys.
struct ServerStartup
{
    Client::Session::ServerAddress connectServer;
    // Only with --auto-login: without it, the player types them in the account window.
    std::optional<App::Credentials> credentials;
    Network::ConnectionKeys keys;
    // The executable that the reply to the GS's 03 sums (ServerSession::OnMainCheck).
    std::filesystem::path mainCheckFile;
};

struct Startup
{
    // Empty (the current folder) if SDL does not give it.
    std::filesystem::path folder;
    App::CommandLine commandLine;
    App::ClientConfig config;
    // Empty without a host.
    std::optional<ServerStartup> server;
};

// Phase 1, with a host: with --auto-login, first the account and the password from the environment, then the keys
// from the data folder (the CL4 plan, CL4.4). Each error says what is missing and how to give it, without a value.
// Without --auto-login the environment is not read.
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
    return ServerStartup{.connectServer = config.server.value_or(Client::Session::ServerAddress{}),
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

// Phase 1. SDL_GetBasePath works before SDL_Init: the executable's folder holds the config and, unless it says
// otherwise, the log.
std::optional<Startup> ReadStartup(int argc, char* argv[], App::CommandLine commandLine, Core::Log& log)
{
    const auto exeFolder = Platform::Sdl::ExecutableFolder();
    if (!exeFolder.HasValue())
    {
        log.Warning("{}; busco la config y el log en la carpeta actual", exeFolder.Error());
    }
    const std::filesystem::path folder = exeFolder.HasValue() ? exeFolder.Value() : std::filesystem::path();

    // The command line that 0x4ad4a0 gets with GetCommandLineA to sum its executable: argv[0] and the arguments
    // separated by a space. 0x4ad270 cuts the name at the first space or quote, so the quotes GetCommandLineA keeps do
    // not change the result.
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

// Phase 2, with a server: the system network, WSAStartup on Windows (Network/SocketApi.h). false if it does not
// start, with the error in the log.
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

// Phase 3, before the window: [Video] Width and Height must be one of the original's five or a monitor mode
// (Platform/Resolution.h). If SDL gives no modes, the five are valid and it warns. false if invalid, with the valid
// ones in the log.
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

// Phase 3: the window with the config's size and VSync. If the driver refuses the VSync asked for, it warns and goes
// on: the logic still runs at 25 steps per second and the drawing, faster.
std::optional<Platform::Sdl::Window> OpenWindow(const Platform::Sdl::SdlContext& sdl, const App::ClientConfig& config,
                                                Core::Log& log)
{
    if (!CheckResolution(sdl, config, log))
    {
        return std::nullopt;
    }
    // Resolve always sets the VSync: On in normal mode and Off in faithful, if not given.
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

// The FPS cap applies only when the VSync ended up off, as in MuMain (App::EffectiveMaxFps).
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

// The drawing area changes with PixelSizeChanged.
void FollowPixelSize(const Platform::Sdl::GlFunctions& gl, Core::PixelSize size, Core::PixelSize& viewport)
{
    if (size.width != viewport.width || size.height != viewport.height)
    {
        viewport = size;
        SetViewport(gl, viewport);
    }
}

// With the CL3.4 warning if it was clamped (over MaxFrameTime).
void RunFrame(App::GameLoop& loop, const Platform::WindowState& state, Client::Input::InputState& input, Core::Log& log)
{
    const std::uint64_t clampedBefore = loop.Stats().clampedFrames;
    loop.RunFrame(state, input);
    if (loop.Stats().clampedFrames != clampedBefore)
    {
        log.Warning("un frame pasó de {} ms: se corrieron solo los pasos del tope, y el resto se perdió",
                    std::chrono::duration_cast<std::chrono::milliseconds>(Client::Kernel::MaxFrameTime).count());
    }
}

void PrintStats(App::FrameRateMeter& meter, Core::Duration now, const App::FrameStats& stats)
{
    if (const auto rates = meter.Update(now, stats))
    {
        std::printf("FPS: %.2f, pasos por segundo: %.2f\n", rates->framesPerSecond, rates->stepsPerSecond);
        std::fflush(stdout);
    }
}

// Phase 4: until the window closes, --frames is reached or `stop` says not to go on. `afterFrame` runs after each
// frame, before `stop`.
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
        // The mouse in framebuffer pixels (CL5.5): the login UI uses them.
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

// The fonts of `files` for each screen width (Render/Frame/SceneText.h). If they do not open, FontOpenError's error:
// without [Video] FontFile, the path came from the search and does not go to the log.
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

struct SceneStartup
{
    Client::Tables::GameData data;
    App::FontFiles fonts;
};

// Phase 2, with a data folder: what WinMain does before scene 1, Text.bmd (0x4b9b9f) and the font files, with its
// text 0 as the face (App::ChooseFontFiles). Without [Video] FontFile, the face is looked up in the system fonts,
// which only Windows has. Each error goes to the log.
std::optional<SceneStartup> ReadSceneStartup(const App::ClientConfig& config, Core::Log& log)
{
    auto data = Client::Tables::LoadGameData(*config.dataPath);
    if (!data.HasValue())
    {
        log.Error("{}", data.Error());
        return std::nullopt;
    }
    // The face is a game text: it does not go to the log (App/FontFiles.h).
    const Client::Tables::GameData& loaded = data.Value();
    if (loaded.items && !loaded.items->checksumMatches)
    {
        log.Warning("Item.bmd: el checksum no coincide; se continúa con la tabla cargada");
    }
    const std::string_view face = loaded.text ? loaded.text->Text(0).value_or("") : "";
    // A search only where there is a system font list: elsewhere none, and ChooseFontFiles' error says the client does
    // not look up the face.
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

// SDL's text input, on only while the scene asks for it (IScene::WantsTextInput): SDL3 asks to turn it on only when
// text is wanted, because it can show an IME or an on-screen keyboard and skip keys (SDL_keyboard.h), and the
// original only has a focused EDIT while the account is up. Follow turns it on or off when the request changes; if
// SDL fails, one line to the log and no retry until the next change: without it you can choose and cancel, but not
// type.
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

// Phase 4, with a host. They outlive the GameClient, which holds the session.
struct HostDevices
{
    Network::SystemResolver resolver;
    Network::SystemSocket socket{resolver};
};

// Phase 4, with a host: what GameClient needs for the session. The padding of what the connection encrypts comes
// from std::random_device: it only has to change from one run to the next (Network/Connection.h). The chat rooms with
// a friend open a socket per room with the same resolver and the build's obfuscation.
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

// Phase 4: SDL's audio, or none with the warning in the log.
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

// Phase 4, after the loop, with the client and the window alive: the exit code of what stopped it.
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
        // Q-C180: MessageBox first, then WM_DESTROY. It closes even if SDL could not show it.
        const auto shown = window.ShowErrorMessage(exit->message);
        if (!shown.HasValue())
        {
            log.Error("{}", shown.Error());
        }
        return EXIT_FAILURE;
    }
    case App::GameExitKind::SceneFailure:
        // The SceneManager already logged the diagnostic of Enter or of applying the session.
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

// Phase 4 with a data folder: the devices (the texture table, the GL frame, the audio, the clipboard, the screen
// reading and, with a host, the socket and the obfuscation), then the GameClient, and the loop with text input while
// the scene asks for it. An error before the loop, or a scene that cannot enter, closes with EXIT_FAILURE and the
// error in the log; the OK of a notice that closes the game (Q-C69) and the GameServer's order (Q-C106) close with
// EXIT_SUCCESS, at the end of the frame they arrive in.
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
    // The game draws the cursor: the system one is hidden over the window (0x4ba238). If SDL cannot hide it, both show.
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
    // Off on every exit from the loop: the window, --frames, the failing scene, the OK or the GameServer's order.
    textInput.Follow(false);
    PrintFrames(stats);
    return ExitCodeOf(client.Exit(), window, log);
}
} // namespace

// With SDL_main.h, on Windows SDL provides the real main and calls this one (which becomes SDL_main) with the
// arguments in UTF-8: SDL_RunApp builds them with GetCommandLineW and CommandLineToArgvW. Without it, argv arrives
// in the ANSI code page and what does not fit in it arrives replaced by "?". On Linux nothing changes.
int main(int argc, char* argv[])
{
    // Phase 0. Declared first to die last: it also covers the shutdown.
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

    // Phase 2: stderr and the [Log] Path file (Resolve already took it from the executable's folder), or
    // muret-client.log next to the executable. If it does not open, only stderr.
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
    // A crash's line goes to the same file, like 0x4b9b3e's to the original's MuError.log.
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

    // Before SDL, so it closes after it: every socket must die before the NetworkInit.
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
    // After the SdlContext, so it dies first: SDL_Quit does not restore SDL's default output (Platform/Sdl/SdlLog.h).
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
    // Without data (the CI): the test scene, without network, because a host requires the data folder.
    Scenes::TestScene scene{[&window](const Scenes::TestScene::Color& color) { Present(*window, color); }};
    App::GameLoop loop{clock, scene, startup->config.pacing, LoopMaxFps(startup->config, *window, log)};
    Client::Input::InputState input;
    PrintFrames(RunLoop(*window, clock, loop, input, startup->commandLine, log));
    return EXIT_SUCCESS;
}
