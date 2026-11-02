#include "App/CommandLine.h"

#include <charconv>
#include <system_error>

namespace
{
using Client::App::PacingMode;
using Client::App::VSyncMode;

constexpr std::string_view FramesOption = "--frames";
constexpr std::string_view PacingOption = "--pacing";
constexpr std::string_view VSyncOption = "--vsync";
constexpr std::string_view StatsOption = "--stats";
constexpr std::string_view Usage =
    "uso: Main [--frames N] [--pacing decoupled|faithful] [--vsync on|adaptive|off] [--stats]";

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
    return std::string(option) + " necesita " + std::string(expected) + ", no \"" + std::string(value) + "\"";
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
        if (argument == StatsOption)
        {
            commandLine.stats = true;
            continue;
        }
        if (argument != FramesOption && argument != PacingOption && argument != VSyncOption)
        {
            error = "no conozco el argumento \"" + std::string(argument) + "\" (" + std::string(Usage) + ")";
            return std::nullopt;
        }
        if (i + 1 == arguments.size())
        {
            const std::string_view expected = argument == FramesOption ? "un número" : "un valor";
            error = std::string(argument) + " necesita " + std::string(expected) + " (" + std::string(Usage) + ")";
            return std::nullopt;
        }
        const std::string_view value = arguments[++i];
        if (argument == FramesOption)
        {
            commandLine.maxFrames = ParsePositive(value);
            if (!commandLine.maxFrames)
            {
                error = BadValue(argument, "un número mayor que 0", value);
                return std::nullopt;
            }
        }
        else if (argument == PacingOption)
        {
            commandLine.pacing = ParsePacing(value);
            if (!commandLine.pacing)
            {
                error = BadValue(argument, "decoupled o faithful", value);
                return std::nullopt;
            }
        }
        else
        {
            commandLine.vsync = ParseVSync(value);
            if (!commandLine.vsync)
            {
                error = BadValue(argument, "on, adaptive u off", value);
                return std::nullopt;
            }
        }
    }
    // En el modo fiel VSync va apagado, como el original: con VSync a 60 Hz, la espera de 46,875 ms
    // se engancharía a la grilla de 16,67 ms.
    if (commandLine.pacing == PacingMode::Faithful && commandLine.vsync && *commandLine.vsync != VSyncMode::Off)
    {
        error = "--pacing faithful necesita --vsync off: el original no pide VSync";
        return std::nullopt;
    }
    return commandLine;
}
} // namespace Client::App
