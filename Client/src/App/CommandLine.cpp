#include "App/CommandLine.h"

#include "App/CommandLineSystem.h"
#include "App/LoopTiming.h"
#include "App/PacingMode.h"
#include "App/ServerAddress.h"
#include "App/VSyncMode.h"
#include "Core/Expected.h"
#include "Core/PathText.h"

#include <CLI/App.hpp>
#include <CLI/Error.hpp>
#include <CLI/Option.hpp>
// CLI11 is header only: this brings the definitions of the help and config formatters that CLI::App uses.
#include <CLI/CLI.hpp> // IWYU pragma: keep

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
using Client::App::CommandLine;
using Client::App::CommandLineExit;
using Client::App::Leftovers;
using Client::App::PacingMode;
using Client::App::ServerAddress;
using Client::App::VSyncMode;
using Client::Core::Unexpected;

constexpr std::string_view ProgramName = "Main";
const std::string HostOption = "--host";
const std::string PortOption = "--port";
const std::string AutoLoginOption = "--auto-login";
const std::string ConfigOption = "--config";
const std::string DataOption = "--data";
const std::string FramesOption = "--frames";
const std::string PacingOption = "--pacing";
const std::string VSyncOption = "--vsync";
const std::string MaxFpsOption = "--max-fps";
const std::string StatsOption = "--stats";

// What CLI11 collected, as typed. The values are checked after the parse, by MuRet's rules.
struct Texts
{
    std::string host;
    std::string port;
    std::string configPath;
    std::string dataPath;
    std::string frames;
    std::string pacing;
    std::string vsync;
    std::string maxFps;
    bool autoLogin = false;
    bool stats = false;
};

void AddOptions(CLI::App& app, Texts& texts)
{
    app.add_option(HostOption, texts.host, "The ConnectServer's host, instead of [Server] Host (needs --port)")
        ->type_name("HOST");
    app.add_option(PortOption, texts.port, "The ConnectServer's port, instead of [Server] Port (needs --host)")
        ->type_name("PORT");
    app.add_flag(AutoLoginOption, texts.autoLogin,
                 "Pick the first server and log in with MURET_CLIENT_ACCOUNT and MURET_CLIENT_PASSWORD, without the "
                 "UI (needs a server)")
        ->disable_flag_override();
    app.add_option(ConfigOption, texts.configPath, "Read this config instead of muret-client.ini")->type_name("PATH");
    app.add_option(DataOption, texts.dataPath, "The original client's data folder, instead of [Data] Path")
        ->type_name("PATH");
    app.add_option(FramesOption, texts.frames, "Draw N frames and exit")->type_name("N");
    app.add_option(PacingOption, texts.pacing, "decoupled (MuRet's) or faithful (the original's pace)")
        ->type_name("MODE");
    app.add_option(VSyncOption, texts.vsync, "on, adaptive or off; faithful pacing takes only off")->type_name("MODE");
    app.add_option(MaxFpsOption, texts.maxFps,
                   std::format("The FPS cap with VSync off: 0 (none) or {} to {}", Client::App::MinFpsLimit,
                               Client::App::MaxFpsLimit))
        ->type_name("N");
    app.add_flag(StatsOption, texts.stats, "Print the FPS and the steps per second every 5 s")->disable_flag_override();
}

// The parser of each system's syntax: the GNU one, and on Windows also its own (CommandLineSystem.h).
void Configure(CLI::App& app, std::string_view version)
{
    app.usage(std::string(Client::App::SystemUsage()));
    app.footer(std::string(Client::App::SystemHelpFooter()));
    app.allow_extras();
    if (Client::App::SystemOptionStyle() == Client::App::OptionStyle::GnuAndWindows)
    {
        app.allow_windows_style_options();
        app.option_defaults()->ignore_case();
    }
    // A repeat is MuRet's error, with its own message (Repeated): CLI11 keeps the last one and counts them.
    app.option_defaults()->multi_option_policy(CLI::MultiOptionPolicy::TakeLast);
    // After option_defaults, so that they take the case rule too.
    app.set_help_flag("-h,--help", "Print this help and exit");
    app.set_version_flag("--version", std::format("MuRet client {}", version), "Print the version and exit");
}

std::optional<std::string> Repeated(const CLI::App& app)
{
    for (const CLI::Option* option : app.get_options())
    {
        if (option->count() > 1)
        {
            return std::format("{} is repeated", option->get_name());
        }
    }
    return std::nullopt;
}

// A positive integer, with no sign and nothing after it: from_chars already rejects "-1", "+5" and the overflow.
std::optional<std::uint64_t> ParsePositive(std::string_view text)
{
    std::uint64_t value = 0;
    const char* const last = text.data() + text.size();
    const auto [end, error] = std::from_chars(text.data(), last, value);
    if (error != std::errc() || end != last || value == 0)
    {
        return std::nullopt;
    }
    return value;
}

// 0, or MinFpsLimit to MaxFpsLimit (IsValidMaxFps), with no sign and nothing after it.
std::optional<int> ParseMaxFps(std::string_view text)
{
    unsigned value = 0;
    const char* const last = text.data() + text.size();
    const auto [end, error] = std::from_chars(text.data(), last, value);
    // More than MaxFpsLimit stops before the cast: it need not fit in an int.
    if (error != std::errc() || end != last || value > static_cast<unsigned>(Client::App::MaxFpsLimit) ||
        !Client::App::IsValidMaxFps(static_cast<int>(value)))
    {
        return std::nullopt;
    }
    return static_cast<int>(value);
}

std::optional<PacingMode> ParsePacing(std::string_view text)
{
    if (text == "decoupled")
    {
        return PacingMode::Decoupled;
    }
    if (text == "faithful")
    {
        return PacingMode::Faithful;
    }
    return std::nullopt;
}

std::optional<VSyncMode> ParseVSync(std::string_view text)
{
    if (text == "on")
    {
        return VSyncMode::On;
    }
    if (text == "adaptive")
    {
        return VSyncMode::Adaptive;
    }
    if (text == "off")
    {
        return VSyncMode::Off;
    }
    return std::nullopt;
}

std::string BadValue(std::string_view option, std::string_view expected, std::string_view value)
{
    return std::format("{} needs {}, not \"{}\"", option, expected, value);
}

// The path of --config or --data, or empty if it is not UTF-8 where it must be. On Windows the arguments come in
// UTF-8 (main.cpp uses SDL_main.h, and SDL builds them with GetCommandLineW and CommandLineToArgvW): they are read with
// Core::PathFromUtf8, like the config, because path(std::string) would read the ANSI code page. On POSIX an argument
// is already the file system's bytes, and path keeps them as they are, so a name that is not UTF-8 works too.
std::optional<std::filesystem::path> PathFromArgument(std::string_view value)
{
    if constexpr (std::is_same_v<std::filesystem::path::value_type, char>)
    {
        return std::filesystem::path(value);
    }
    else
    {
        return Client::Core::PathFromUtf8(value);
    }
}

Client::Core::Expected<std::optional<std::filesystem::path>, std::string>
ReadPath(const CLI::App& app, const std::string& option, std::string_view text)
{
    if (app.count(option) == 0)
    {
        return std::optional<std::filesystem::path>();
    }
    auto path = PathFromArgument(text);
    if (!path)
    {
        return Unexpected(std::format("{} needs a path in UTF-8", option));
    }
    return path;
}

Client::Core::Expected<std::optional<ServerAddress>, std::string> ReadServer(const CLI::App& app, const Texts& texts)
{
    const bool hasHost = app.count(HostOption) > 0;
    const bool hasPort = app.count(PortOption) > 0;
    if (hasHost != hasPort)
    {
        return Unexpected(hasHost ? std::format("{} needs {}", HostOption, PortOption)
                                  : std::format("{} needs {}", PortOption, HostOption));
    }
    if (!hasHost)
    {
        return std::optional<ServerAddress>();
    }
    auto host = Client::App::ParseHost(texts.host);
    if (!host.HasValue())
    {
        return Unexpected(std::format("{}: {}", HostOption, host.Error()));
    }
    const auto port = Client::App::ParsePort(texts.port);
    if (!port.HasValue())
    {
        return Unexpected(std::format("{}: {}", PortOption, port.Error()));
    }
    return std::optional<ServerAddress>(ServerAddress{std::move(host).Value(), port.Value()});
}

// The values, after the parse. The error is a usage error's line.
Client::Core::Expected<CommandLine, std::string> Read(const CLI::App& app, const Texts& texts,
                                                      const Leftovers& leftovers)
{
    CommandLine commandLine;
    commandLine.stats = texts.stats;
    commandLine.autoLogin = texts.autoLogin;
    if (app.count(FramesOption) > 0)
    {
        commandLine.maxFrames = ParsePositive(texts.frames);
        if (!commandLine.maxFrames)
        {
            return Unexpected(BadValue(FramesOption, "a number greater than 0", texts.frames));
        }
    }
    if (app.count(PacingOption) > 0)
    {
        commandLine.pacing = ParsePacing(texts.pacing);
        if (!commandLine.pacing)
        {
            return Unexpected(BadValue(PacingOption, "decoupled or faithful", texts.pacing));
        }
    }
    if (app.count(VSyncOption) > 0)
    {
        commandLine.vsync = ParseVSync(texts.vsync);
        if (!commandLine.vsync)
        {
            return Unexpected(BadValue(VSyncOption, "on, adaptive or off", texts.vsync));
        }
    }
    if (app.count(MaxFpsOption) > 0)
    {
        commandLine.maxFps = ParseMaxFps(texts.maxFps);
        if (!commandLine.maxFps)
        {
            const std::string expected =
                std::format("0 or a number from {} to {}", Client::App::MinFpsLimit, Client::App::MaxFpsLimit);
            return Unexpected(BadValue(MaxFpsOption, expected, texts.maxFps));
        }
    }
    auto configPath = ReadPath(app, ConfigOption, texts.configPath);
    if (!configPath.HasValue())
    {
        return Unexpected(configPath.Error());
    }
    commandLine.configPath = std::move(configPath).Value();
    auto dataPath = ReadPath(app, DataOption, texts.dataPath);
    if (!dataPath.HasValue())
    {
        return Unexpected(dataPath.Error());
    }
    commandLine.dataPath = std::move(dataPath).Value();
    auto server = ReadServer(app, texts);
    if (!server.HasValue())
    {
        return Unexpected(server.Error());
    }
    if (server.Value() && leftovers.launcher)
    {
        return Unexpected(std::format("/u and /p do not go with {} and {}", HostOption, PortOption));
    }
    commandLine.connect = leftovers.launcher ? leftovers.launcher : std::move(server).Value();
    return commandLine;
}

CommandLineExit UsageError(const CLI::App& app, std::string_view line)
{
    return {.text = std::format("{}: {}\n\n{}", ProgramName, line, app.help()),
            .exitCode = Client::App::UsageErrorExitCode};
}
} // namespace

namespace Client::App
{
Core::Expected<CommandLine, CommandLineExit> ParseCommandLine(std::span<const std::string_view> arguments,
                                                              std::string_view version)
{
    CLI::App app;
    Configure(app, version);
    Texts texts;
    AddOptions(app, texts);
    // CLI11 takes the arguments last first.
    std::vector<std::string> reversed(arguments.rbegin(), arguments.rend());
    // CLI11 reports through exceptions; none leaves this function.
    try
    {
        app.parse(std::move(reversed));
    }
    catch (const CLI::CallForHelp&)
    {
        return Unexpected(CommandLineExit{.text = app.help(), .exitCode = 0});
    }
    catch (const CLI::CallForVersion& versionText)
    {
        return Unexpected(CommandLineExit{.text = std::format("{}\n", versionText.what()), .exitCode = 0});
    }
    catch (const CLI::ParseError& error)
    {
        return Unexpected(UsageError(app, error.what()));
    }
    if (const auto repeated = Repeated(app))
    {
        return Unexpected(UsageError(app, *repeated));
    }
    const auto leftovers = ReadLeftovers(app.remaining());
    if (!leftovers.HasValue())
    {
        return Unexpected(UsageError(app, leftovers.Error()));
    }
    if (leftovers.Value().help)
    {
        return Unexpected(CommandLineExit{.text = app.help(), .exitCode = 0});
    }
    auto commandLine = Read(app, texts, leftovers.Value());
    if (!commandLine.HasValue())
    {
        return Unexpected(UsageError(app, commandLine.Error()));
    }
    return std::move(commandLine).Value();
}
} // namespace Client::App
