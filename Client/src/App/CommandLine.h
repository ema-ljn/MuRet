#pragma once

#include "App/PacingMode.h"
#include "App/ServerAddress.h"
#include "App/VSyncMode.h"
#include "Core/Expected.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// Main's arguments, by each system's convention (AR1.3). Main --help lists them:
//
//   --host HOST --port PORT  the ConnectServer, instead of [Server] Host and Port; one needs the other
//   --auto-login             with a server, pick the first one and log in with the account and password from the
//                            environment, without the UI (App/AutoLogin.h); without a server it is an error (Resolve)
//   --config PATH            read this config instead of muret-client.ini next to the executable
//   --data PATH              the data folder, instead of [Data] Path
//   --frames N               draw N frames and exit
//   --pacing MODE            decoupled (MuRet's) or faithful (the original's pace)
//   --vsync MODE             on, adaptive or off; faithful pacing takes only off
//   --max-fps N              the FPS cap with VSync off: 0 (none) or MinFpsLimit to MaxFpsLimit
//   --stats                  print the FPS and the steps per second every 5 s
//
// Linux takes only these, as --name value or --name=value, and ignores what is not an option. Windows also takes
// /name value and /name:value, in any case, /? and the launcher's connect /u<host> /p<port> (Q-C36). -- ends the
// options on both.
namespace Client::App
{
struct CommandLine
{
    std::optional<std::uint64_t> maxFrames;
    std::optional<PacingMode> pacing;
    std::optional<VSyncMode> vsync;
    std::optional<int> maxFps;
    bool stats = false;
    bool autoLogin = false;
    std::optional<ServerAddress> connect;
    // Relative to the current folder: they belong to whoever starts the client.
    std::optional<std::filesystem::path> configPath;
    std::optional<std::filesystem::path> dataPath;
};

inline constexpr int UsageErrorExitCode = 2;

// The command line ends Main before it starts: the help or the version, to stdout with exit code 0, or a usage error,
// its line and the usage, to stderr with UsageErrorExitCode.
struct CommandLineExit
{
    std::string text;
    int exitCode = 0;
};

// The arguments without the program's name (argv[1..]). Resolve (Config.h) checks the combinations.
Core::Expected<CommandLine, CommandLineExit> ParseCommandLine(std::span<const std::string_view> arguments,
                                                              std::string_view version);
} // namespace Client::App
