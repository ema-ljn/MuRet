#include "App/CommandLine.h"

#include "App/CommandLineSystem.h"
#include "App/CommandLineValues.h"
#include "App/FrameRateMeter.h"
#include "App/LoopTiming.h"
#include "Core/Expected.h"

#include <CLI/App.hpp>
#include <CLI/Error.hpp>
#include <CLI/Option.hpp>
// CLI11 is header only: CLI::App needs the definitions of its help and config formatters.
#include <CLI/Config.hpp>    // IWYU pragma: keep
#include <CLI/Formatter.hpp> // IWYU pragma: keep

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using Client::App::CommandLine;
using Client::App::CommandLineExit;
using Client::App::OptionStyle;
using Client::App::OptionTexts;
using Client::App::SystemSyntax;
using Client::Core::Unexpected;
namespace OptionNames = Client::App::OptionNames;

constexpr std::string_view ProgramName = "Main";
constexpr std::string_view DoubleDash = "--";

template <typename Text> std::span<const Text> UpToDoubleDash(std::span<const Text> arguments)
{
    return {arguments.begin(), std::ranges::find(arguments, DoubleDash)};
}

// Not add_option(name, text): CLI11 2.7 turns "" into nullopt, and "" must stay a wrong value.
void AddValue(CLI::App& app, std::string_view name, std::optional<std::string>& text, std::string_view typeName,
              const std::string& help)
{
    app.add_option_function<std::string>(
           std::string(name), [&text](const std::string& value) { text = value; }, help)
        ->type_name(std::string(typeName));
}

void AddFlag(CLI::App& app, std::string_view name, bool& value, const std::string& help)
{
    app.add_flag(std::string(name), value, help)->disable_flag_override();
}

void AddOptions(CLI::App& app, OptionTexts& texts)
{
    AddValue(app, OptionNames::Host, texts.host, "HOST",
             "The ConnectServer's host, instead of [Server] Host (needs --port)");
    AddValue(app, OptionNames::Port, texts.port, "PORT",
             "The ConnectServer's port, instead of [Server] Port (needs --host)");
    AddFlag(app, OptionNames::AutoLogin, texts.autoLogin,
            "Pick the first server and log in with MU_CLIENT_ACCOUNT and MU_CLIENT_PASSWORD, without the UI "
            "(needs a server)");
    AddValue(app, OptionNames::Config, texts.configPath, "PATH", "Read this config instead of muret-client.ini");
    AddValue(app, OptionNames::Data, texts.dataPath, "PATH",
             "The original client's data folder, instead of [Data] Path");
    AddValue(app, OptionNames::Frames, texts.frames, "N", "Draw N frames and exit");
    AddValue(app, OptionNames::Pacing, texts.pacing, "MODE", "decoupled (MuRet's) or faithful (the original's pace)");
    AddValue(app, OptionNames::VSync, texts.vsync, "MODE", "on, adaptive or off; faithful pacing takes only off");
    AddValue(app, OptionNames::MaxFps, texts.maxFps, "N",
             std::format("The FPS cap with VSync off: 0 (none) or {} to {}", Client::App::MinFpsLimit,
                         Client::App::MaxFpsLimit));
    AddFlag(app, OptionNames::Stats, texts.stats,
            std::format("Print the FPS and the steps per second every {} s",
                        std::chrono::duration_cast<std::chrono::seconds>(Client::App::StatsInterval).count()));
}

std::string UsageLine(const SystemSyntax& syntax)
{
    const std::string usage = std::format("Usage: {} [OPTIONS]", ProgramName);
    return syntax.operands.empty() ? usage : std::format("{} {}", usage, syntax.operands);
}

void ConfigureSyntax(CLI::App& app, const SystemSyntax& syntax, std::string_view version)
{
    const bool windowsStyle = syntax.style == OptionStyle::GnuAndWindows;
    app.usage(UsageLine(syntax));
    app.footer(std::string(syntax.helpFooter));
    app.allow_extras();
    app.allow_windows_style_options(windowsStyle);
    app.option_defaults()->ignore_case(windowsStyle);
    // A repeat is MuRet's error (RepeatedOption): CLI11 keeps the last one and counts them.
    app.option_defaults()->multi_option_policy(CLI::MultiOptionPolicy::TakeLast);
    // After option_defaults, so that they take the case rule too.
    app.set_help_flag("-h,--help", "Print this help and exit");
    app.set_version_flag("--version", std::format("MuRet client {}", version), "Print the version and exit");
}

CommandLineExit HelpExit(const CLI::App& app)
{
    return {.text = app.help(), .exitCode = EXIT_SUCCESS};
}

CommandLineExit UsageError(const CLI::App& app, std::string_view line)
{
    return {.text = std::format("{}: {}\n\n{}", ProgramName, line, app.help()),
            .exitCode = Client::App::UsageErrorExitCode};
}

struct AttachedValue
{
    std::string option;
    std::string_view value;
};

// --name=value, and in the Windows style /name:value, with the option's GNU name.
std::optional<AttachedValue> SplitAttachedValue(std::string_view argument, OptionStyle style)
{
    if (argument.starts_with(DoubleDash))
    {
        const std::size_t equals = argument.find('=');
        if (equals != std::string_view::npos)
        {
            return AttachedValue{std::string(argument.substr(0, equals)), argument.substr(equals + 1)};
        }
    }
    else if (style == OptionStyle::GnuAndWindows && argument.starts_with('/'))
    {
        const std::size_t colon = argument.find(':');
        if (colon != std::string_view::npos)
        {
            return AttachedValue{std::format("--{}", argument.substr(1, colon - 1)), argument.substr(colon + 1)};
        }
    }
    return std::nullopt;
}

// CLI11 reads an empty attached value as none, and the option would take the next argument.
std::optional<std::string> AttachedValueError(const CLI::App& app, OptionStyle style,
                                              std::span<const std::string_view> options)
{
    for (const std::string_view argument : options)
    {
        const std::optional<AttachedValue> attached = SplitAttachedValue(argument, style);
        if (!attached)
        {
            continue;
        }
        const CLI::Option* const option = app.get_option_no_throw(attached->option);
        if (option == nullptr)
        {
            continue;
        }
        if (option->get_expected_max() == 0)
        {
            return std::format("{} takes no value", option->get_name());
        }
        if (attached->value.empty())
        {
            return std::format("{} needs a value", option->get_name());
        }
    }
    return std::nullopt;
}

// CLI11 reports through exceptions; none leaves here. The exit is the help, the version or a usage error.
std::optional<CommandLineExit> Parse(CLI::App& app, std::span<const std::string_view> arguments)
{
    // CLI11 takes the arguments last first.
    std::vector<std::string> reversed(arguments.rbegin(), arguments.rend());
    try
    {
        app.parse(std::move(reversed));
    }
    catch (const CLI::CallForHelp&)
    {
        return HelpExit(app);
    }
    catch (const CLI::CallForVersion& versionText)
    {
        return CommandLineExit{.text = std::format("{}\n", versionText.what()), .exitCode = EXIT_SUCCESS};
    }
    catch (const CLI::ParseError& error)
    {
        return UsageError(app, error.what());
    }
    return std::nullopt;
}

std::optional<std::string> RepeatedOption(const CLI::App& app)
{
    for (const CLI::Option* const option : app.get_options())
    {
        if (option->count() > 1)
        {
            return std::format("{} is repeated", option->get_name());
        }
    }
    return std::nullopt;
}

// The values go first: a missing value moves the next argument into the system's arguments.
std::optional<std::string> ReadParsed(const CLI::App& app, const OptionTexts& texts, CommandLine& commandLine)
{
    if (auto error = RepeatedOption(app))
    {
        return error;
    }
    if (auto error = Client::App::ReadOptions(texts, commandLine))
    {
        return error;
    }
    const std::vector<std::string> remaining = app.remaining();
    const auto launcher = Client::App::ReadSystemArguments(UpToDoubleDash(std::span<const std::string>(remaining)));
    if (!launcher.HasValue())
    {
        return launcher.Error();
    }
    return Client::App::ReadConnect(texts, launcher.Value(), commandLine);
}
} // namespace

namespace Client::App
{
Core::Expected<CommandLine, CommandLineExit> ParseCommandLine(std::span<const std::string_view> arguments,
                                                              std::string_view version)
{
    const SystemSyntax syntax = ThisSystemSyntax();
    CLI::App app;
    OptionTexts texts;
    ConfigureSyntax(app, syntax, version);
    AddOptions(app, texts);
    const std::span<const std::string_view> options = UpToDoubleDash(arguments);
    if (!syntax.helpWord.empty() && std::ranges::find(options, syntax.helpWord) != options.end())
    {
        return Unexpected(HelpExit(app));
    }
    if (const auto error = AttachedValueError(app, syntax.style, options))
    {
        return Unexpected(UsageError(app, *error));
    }
    if (auto earlyExit = Parse(app, arguments))
    {
        return Unexpected(std::move(*earlyExit));
    }
    CommandLine commandLine;
    if (const auto error = ReadParsed(app, texts, commandLine))
    {
        return Unexpected(UsageError(app, *error));
    }
    return commandLine;
}
} // namespace Client::App
