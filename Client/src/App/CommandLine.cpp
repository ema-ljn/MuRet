#include "App/CommandLine.h"

#include <charconv>
#include <system_error>

namespace
{
constexpr std::string_view FramesOption = "--frames";
constexpr std::string_view Usage = "uso: Main [--frames N]";

// Un entero positivo, sin signo ni nada atrás: from_chars ya rechaza "-1", "+5" y el desborde.
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
} // namespace

namespace Client::App
{
std::optional<CommandLine> ParseCommandLine(std::span<const std::string_view> arguments, std::string& error)
{
    CommandLine commandLine;
    for (std::size_t i = 0; i < arguments.size(); ++i)
    {
        const std::string_view argument = arguments[i];
        if (argument != FramesOption)
        {
            error = "no conozco el argumento \"" + std::string(argument) + "\" (" + std::string(Usage) + ")";
            return std::nullopt;
        }
        if (i + 1 == arguments.size())
        {
            error = "--frames necesita un número (" + std::string(Usage) + ")";
            return std::nullopt;
        }
        const std::string_view value = arguments[++i];
        commandLine.maxFrames = ParsePositive(value);
        if (!commandLine.maxFrames)
        {
            error = "--frames necesita un número mayor que 0, no \"" + std::string(value) + "\"";
            return std::nullopt;
        }
    }
    return commandLine;
}
} // namespace Client::App
