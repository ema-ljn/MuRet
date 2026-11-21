#include "App/CommandLineSystem.h"

#include "App/ServerAddress.h"
#include "Core/Ascii.h"
#include "Core/Expected.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace
{
using Client::Core::EqualsIgnoringAsciiCase;
using Client::Core::ToLowerAscii;
using Client::Core::Unexpected;

constexpr std::string_view ConnectWord = "connect";
constexpr std::string_view DoubleDash = "--";
constexpr std::string_view HelpQuestion = "/?";

// The launcher option's letter ("/u10.0.0.1" gives 'u') in lower case, or '\0' if it is not one.
char LauncherLetter(std::string_view argument)
{
    return argument.size() >= 2 && argument.front() == '/' ? ToLowerAscii(argument[1]) : '\0';
}
} // namespace

namespace Client::App
{
OptionStyle SystemOptionStyle()
{
    return OptionStyle::GnuAndWindows;
}

std::string_view SystemUsage()
{
    return "Usage: Main [OPTIONS] [connect /u<host> /p<port>]";
}

std::string_view SystemHelpFooter()
{
    return "Every option also takes /name value or /name:value, with the name in any case, and /? prints this help.\n"
           "connect /u<host> /p<port> is the launcher's --host and --port: connect is optional, the letters go in any "
           "case and order, and the value is attached to the letter.";
}

// The launcher passes connect /u<host> /p<port> (02 § Del launcher al juego). The original's unused parser (0x4b9890,
// Q-C36) looks for /u and /p in any case and without the connect.
Core::Expected<Leftovers, std::string> ReadLeftovers(std::span<const std::string> arguments)
{
    const auto end = std::ranges::find(arguments, DoubleDash);
    if (std::ranges::find(arguments.begin(), end, HelpQuestion) != end)
    {
        return Leftovers{.launcher = std::nullopt, .help = true};
    }
    std::optional<std::string> host;
    std::optional<std::uint16_t> port;
    for (auto it = arguments.begin(); it != end; ++it)
    {
        const std::string_view argument = *it;
        if (EqualsIgnoringAsciiCase(argument, ConnectWord))
        {
            continue;
        }
        const char letter = LauncherLetter(argument);
        if (letter != 'u' && letter != 'p')
        {
            // /y and /z, among others, are launcher options that MuRet does not read.
            return Unexpected(std::format("unknown argument \"{}\"", argument));
        }
        const std::string_view typed = argument.substr(0, 2);
        if ((letter == 'u' && host) || (letter == 'p' && port))
        {
            return Unexpected(std::format("{} is repeated", typed));
        }
        if (argument.size() == 2)
        {
            return Unexpected(std::string(letter == 'u' ? "/u needs the host attached (/u10.0.0.1)"
                                                        : "/p needs the port attached (/p44405)"));
        }
        if (letter == 'u')
        {
            auto parsed = ParseHost(argument.substr(2));
            if (!parsed.HasValue())
            {
                return Unexpected(std::format("{}: {}", typed, parsed.Error()));
            }
            host = std::move(parsed).Value();
        }
        else
        {
            const auto parsed = ParsePort(argument.substr(2));
            if (!parsed.HasValue())
            {
                return Unexpected(std::format("{}: {}", typed, parsed.Error()));
            }
            port = parsed.Value();
        }
    }
    // A half connect is a mistake of whoever starts the client: the config does not complete it.
    if (host && !port)
    {
        return Unexpected(std::string("/u needs /p with the port"));
    }
    if (port && !host)
    {
        return Unexpected(std::string("/p needs /u with the host"));
    }
    Leftovers leftovers;
    if (host && port)
    {
        leftovers.launcher = ServerAddress{std::move(*host), *port};
    }
    return leftovers;
}
} // namespace Client::App
