#include "App/CommandLineSystem.h"

#include "Core/Ascii.h"
#include "Core/Expected.h"
#include "Core/PathText.h"

#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace
{
using Client::App::ServerTexts;
using Client::Core::EqualsIgnoringAsciiCase;
using Client::Core::ToLowerAscii;

constexpr std::string_view ConnectWord = "connect";

// The launcher option's letter ("/u10.0.0.1" gives 'u') in lower case, or '\0' if it is not one.
char LauncherLetter(std::string_view argument)
{
    return argument.size() >= 2 && argument.front() == '/' ? ToLowerAscii(argument[1]) : '\0';
}

std::string Unknown(std::string_view argument)
{
    const bool isOption = argument.size() > 1 && (argument.front() == '-' || argument.front() == '/');
    return std::format("unknown {} \"{}\"", isOption ? "option" : "argument", argument);
}

// One of the launcher's /u<host> and /p<port>, the letter in any case, or a usage error's line.
std::optional<std::string> ReadLauncherArgument(std::string_view argument, ServerTexts& launcher)
{
    const char letter = LauncherLetter(argument);
    if (letter != 'u' && letter != 'p')
    {
        // /y and /z, among others, are launcher options that MuRet does not read.
        return Unknown(argument);
    }
    const bool isHost = letter == 'u';
    std::optional<std::string>& value = isHost ? launcher.host : launcher.port;
    const std::string_view typed = argument.substr(0, 2);
    if (value)
    {
        return std::format("{} is repeated", typed);
    }
    if (argument.size() == 2)
    {
        return std::string(isHost ? "/u needs the host attached (/u10.0.0.1)" : "/p needs the port attached (/p44405)");
    }
    (isHost ? launcher.hostName : launcher.portName) = typed;
    value = argument.substr(2);
    return std::nullopt;
}
} // namespace

namespace Client::App
{
SystemSyntax ThisSystemSyntax()
{
    return {.style = OptionStyle::GnuAndWindows,
            .operands = "[connect /u<host> /p<port>]",
            .helpFooter =
                "Every option also takes /name value or /name:value, with the name in any case, and /? prints this "
                "help.\n"
                "connect /u<host> /p<port> is the launcher's --host and --port: connect is optional, the letters go in "
                "any case and order, and the value is attached to the letter.",
            .helpWord = "/?"};
}

// The launcher passes connect /u<host> /p<port> (02 § Del launcher al juego). As in 0x4b9890 (Q-C36), connect is
// optional and the order does not matter.
Core::Expected<std::optional<ServerTexts>, std::string> ReadSystemArguments(std::span<const std::string> arguments)
{
    ServerTexts launcher{.hostName = "/u", .host = std::nullopt, .portName = "/p", .port = std::nullopt};
    for (const std::string& argument : arguments)
    {
        if (EqualsIgnoringAsciiCase(argument, ConnectWord))
        {
            continue;
        }
        if (auto error = ReadLauncherArgument(argument, launcher))
        {
            return Core::Unexpected(std::move(*error));
        }
    }
    if (!launcher.host && !launcher.port)
    {
        return std::optional<ServerTexts>();
    }
    return std::optional<ServerTexts>(std::move(launcher));
}

// SDL gives the arguments in UTF-8 (SDL_main.h), and path(std::string) would read them in the ANSI code page.
std::optional<std::filesystem::path> PathFromArgument(std::string_view argument)
{
    return Core::PathFromUtf8(argument);
}
} // namespace Client::App
