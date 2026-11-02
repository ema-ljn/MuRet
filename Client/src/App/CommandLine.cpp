#include "App/CommandLine.h"

#include <algorithm>
#include <charconv>
#include <system_error>

namespace
{
using Client::App::PacingMode;
using Client::App::VSyncMode;
using Client::Core::Unexpected;

constexpr std::string_view FramesOption = "--frames";
constexpr std::string_view PacingOption = "--pacing";
constexpr std::string_view VSyncOption = "--vsync";
constexpr std::string_view StatsOption = "--stats";
constexpr std::string_view ConfigOption = "--config";
constexpr std::string_view DataOption = "--data";
// Lo que pasa el launcher adelante de /u y /p (02 § Del launcher al juego).
constexpr std::string_view ConnectWord = "connect";
constexpr std::string_view Usage = "uso: Main [connect /u<host> /p<puerto>] [--config RUTA] [--data RUTA] [--frames N] "
                                   "[--pacing decoupled|faithful] [--vsync on|adaptive|off] [--stats]";

char ToLowerAscii(char c)
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool EqualsIgnoringCase(std::string_view a, std::string_view b)
{
    return std::ranges::equal(a, b, [](char x, char y) { return ToLowerAscii(x) == ToLowerAscii(y); });
}

// La letra de una opción del launcher ("/u10.0.0.1" da 'u'), en minúscula, o '\0' si no es una.
char LauncherLetter(std::string_view argument)
{
    return argument.size() >= 2 && argument.front() == '/' ? ToLowerAscii(argument[1]) : '\0';
}

// Las opciones que llevan un valor atrás, o --stats: cada una va una vez.
bool AlreadyGiven(const Client::App::CommandLine& commandLine, std::string_view option)
{
    return (option == StatsOption && commandLine.stats) || (option == FramesOption && commandLine.maxFrames) ||
           (option == PacingOption && commandLine.pacing) || (option == VSyncOption && commandLine.vsync) ||
           (option == ConfigOption && commandLine.configPath) || (option == DataOption && commandLine.dataPath);
}

std::string Unknown(std::string_view argument)
{
    return "no conozco el argumento \"" + std::string(argument) + "\" (" + std::string(Usage) + ")";
}

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
Core::Expected<CommandLine, std::string> ParseCommandLine(std::span<const std::string_view> arguments)
{
    CommandLine commandLine;
    std::optional<std::string> host;
    std::optional<std::uint16_t> port;
    for (std::size_t i = 0; i < arguments.size(); ++i)
    {
        const std::string_view argument = arguments[i];
        if (AlreadyGiven(commandLine, argument))
        {
            return Unexpected(std::string(argument) + " está repetido");
        }
        if (argument == StatsOption)
        {
            commandLine.stats = true;
            continue;
        }
        // El launcher pasa "connect /u<ip> /p<puerto>"; el parser sin usar del original (0x4b9890,
        // Q-C36) busca /u y /p sin mirar mayúsculas y sin necesitar el connect.
        if (EqualsIgnoringCase(argument, ConnectWord))
        {
            continue;
        }
        const char letter = LauncherLetter(argument);
        if (letter == 'u' || letter == 'p')
        {
            if ((letter == 'u' && host) || (letter == 'p' && port))
            {
                return Unexpected(std::string(argument.substr(0, 2)) + " está repetido");
            }
            if (argument.size() == 2)
            {
                // "/u 10.0.0.1": el launcher lo pega a la letra.
                return Unexpected(letter == 'u' ? std::string("/u necesita el host pegado (/u10.0.0.1)")
                                                : std::string("/p necesita el puerto pegado (/p44405)"));
            }
            if (letter == 'u')
            {
                auto parsed = ParseHost(argument.substr(2));
                if (!parsed.HasValue())
                {
                    return Unexpected(parsed.Error());
                }
                host = std::move(parsed).Value();
            }
            else
            {
                const auto parsed = ParsePort(argument.substr(2));
                if (!parsed.HasValue())
                {
                    return Unexpected(parsed.Error());
                }
                port = parsed.Value();
            }
            continue;
        }
        if (argument != FramesOption && argument != PacingOption && argument != VSyncOption &&
            argument != ConfigOption && argument != DataOption)
        {
            // /y y /z, entre otras, son opciones del launcher que MuRet no lee.
            return Unexpected(Unknown(argument));
        }
        if (i + 1 == arguments.size())
        {
            const std::string_view expected = argument == FramesOption                             ? "un número"
                                              : argument == ConfigOption || argument == DataOption ? "una ruta"
                                                                                                   : "un valor";
            return Unexpected(std::string(argument) + " necesita " + std::string(expected) + " (" + std::string(Usage) +
                              ")");
        }
        const std::string_view value = arguments[++i];
        if (argument == ConfigOption || argument == DataOption)
        {
            // argv viene en la codificación del sistema (la página ANSI en Windows, porque main.cpp
            // no usa SDL_main.h), que es la que lee path(std::string): acá no va el UTF-8 de la
            // config. Límite conocido en Windows: lo que no entra en la página ya llega cambiado
            // por "?" (C:\Users\Łukasz en la 1252), y el CRT lee `"C:\MU\Data\"` como C:\MU\Data"
            // (la \ antes de la comilla la escapa). El arreglo es CommandLineToArgvW (plan, CL3.4).
            (argument == ConfigOption ? commandLine.configPath : commandLine.dataPath) =
                std::filesystem::path(std::string(value));
        }
        else if (argument == FramesOption)
        {
            commandLine.maxFrames = ParsePositive(value);
            if (!commandLine.maxFrames)
            {
                return Unexpected(BadValue(argument, "un número mayor que 0", value));
            }
        }
        else if (argument == PacingOption)
        {
            commandLine.pacing = ParsePacing(value);
            if (!commandLine.pacing)
            {
                return Unexpected(BadValue(argument, "decoupled o faithful", value));
            }
        }
        else
        {
            commandLine.vsync = ParseVSync(value);
            if (!commandLine.vsync)
            {
                return Unexpected(BadValue(argument, "on, adaptive u off", value));
            }
        }
    }
    // Un connect a medias es un error de quien lanza el cliente: no se completa con la config.
    if (host && !port)
    {
        return Unexpected(std::string("/u necesita /p con el puerto"));
    }
    if (port && !host)
    {
        return Unexpected(std::string("/p necesita /u con el host"));
    }
    if (host && port)
    {
        commandLine.connect = ServerAddress{std::move(*host), *port};
    }
    // Que el modo fiel vaya sin VSync lo mira Resolve, con la config y la línea de comandos juntas.
    return commandLine;
}
} // namespace Client::App
