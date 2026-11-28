#include "App/Config.h"

#include "App/CommandLine.h"
#include "App/ConfigError.h"
#include "Kernel/LoopTiming.h"
#include "Kernel/PacingMode.h"
#include "Session/ServerAddress.h"
#include "Platform/VSyncMode.h"
#include "Core/Ascii.h"
#include "Core/Expected.h"
#include "Core/PathText.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace Client::App
{
using Core::EqualsIgnoringAsciiCase;
using Core::Unexpected;

namespace
{
// Notepad puts it at the start when it saves as UTF-8.
constexpr std::string_view Utf8Bom = "\xEF\xBB\xBF";

enum class Key : std::uint8_t
{
    DataPath,
    ServerHost,
    ServerPort,
    VideoWidth,
    VideoHeight,
    VideoVSync,
    VideoPacing,
    VideoMaxFps,
    VideoFontFile,
    VideoBoldFontFile,
    AudioSoundOnOff,
    AudioMusicOnOff,
    LogPath,
};

struct KnownKey
{
    std::string_view section;
    std::string_view name;
    Key key;
};

constexpr std::array KnownKeys = {
    KnownKey{"Data", "Path", Key::DataPath},
    KnownKey{"Server", "Host", Key::ServerHost},
    KnownKey{"Server", "Port", Key::ServerPort},
    KnownKey{"Video", "Width", Key::VideoWidth},
    KnownKey{"Video", "Height", Key::VideoHeight},
    KnownKey{"Video", "VSync", Key::VideoVSync},
    KnownKey{"Video", "Pacing", Key::VideoPacing},
    KnownKey{"Video", "MaxFps", Key::VideoMaxFps},
    KnownKey{"Video", "FontFile", Key::VideoFontFile},
    KnownKey{"Video", "BoldFontFile", Key::VideoBoldFontFile},
    KnownKey{"Audio", "SoundOnOff", Key::AudioSoundOnOff},
    KnownKey{"Audio", "MusicOnOff", Key::AudioMusicOnOff},
    KnownKey{"Log", "Path", Key::LogPath},
};

bool IsBlank(char c)
{
    return c == ' ' || c == '\t';
}

bool IsDigit(char c)
{
    return c >= '0' && c <= '9';
}

bool IsControl(char c)
{
    const auto byte = static_cast<unsigned char>(c);
    return byte < 0x20 || byte == 0x7F;
}

// Strips a pair of surrounding double quotes, the ones Explorer's "Copy as path" adds. A lone quote stays part of
// the value.
std::string_view WithoutQuotes(std::string_view value)
{
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
    {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

std::string_view Trim(std::string_view text)
{
    while (!text.empty() && IsBlank(text.front()))
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && IsBlank(text.back()))
    {
        text.remove_suffix(1);
    }
    return text;
}

// Cuts at the first `;` that is at the start or after a space: "a;b" is a value.
std::string_view WithoutComment(std::string_view line)
{
    for (std::size_t i = 0; i < line.size(); ++i)
    {
        if (line[i] == ';' && (i == 0 || IsBlank(line[i - 1])))
        {
            return line.substr(0, i);
        }
    }
    return line;
}

bool IsKnownSection(std::string_view section)
{
    return std::ranges::any_of(KnownKeys, [section](const KnownKey& known)
                               { return EqualsIgnoringAsciiCase(known.section, section); });
}

// [Server] Obfuscation is gone: the build chooses the server's packet shape (App/MakeObfuscation.h).
bool IsRemovedObfuscationKey(std::string_view section, std::string_view name)
{
    return EqualsIgnoringAsciiCase(section, "Server") && EqualsIgnoringAsciiCase(name, "Obfuscation");
}

std::size_t FindKey(std::string_view section, std::string_view name)
{
    const auto found = std::ranges::find_if(
        KnownKeys, [section, name](const KnownKey& known)
        { return EqualsIgnoringAsciiCase(known.section, section) && EqualsIgnoringAsciiCase(known.name, name); });
    return static_cast<std::size_t>(found - KnownKeys.begin());
}

Core::Expected<int, std::string> ParseSize(std::string_view value)
{
    // As in ParsePort: the digits are checked first, for the message.
    if (!std::ranges::all_of(value, IsDigit))
    {
        return Unexpected(std::string("tiene que ser un número"));
    }
    int size = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), size);
    if (result.ec != std::errc{} || size == 0 || size > MaxWindowSize)
    {
        return Unexpected(std::format("va de 1 a {}", MaxWindowSize));
    }
    return size;
}

// As in ParseSize, the digits are checked first, for the message.
Core::Expected<int, std::string> ParseMaxFps(std::string_view value)
{
    if (!std::ranges::all_of(value, IsDigit))
    {
        return Unexpected(std::string("tiene que ser un número"));
    }
    int maxFps = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), maxFps);
    if (result.ec != std::errc{} || !Kernel::IsValidMaxFps(maxFps))
    {
        return Unexpected(std::format("va de {} a {}, o 0 (sin tope)", Kernel::MinFpsLimit, Kernel::MaxFpsLimit));
    }
    return maxFps;
}

Core::Expected<bool, std::string> ParseOnOff(std::string_view value)
{
    if (value == "0")
    {
        return false;
    }
    if (value == "1")
    {
        return true;
    }
    return Unexpected(std::string("tiene que ser 0 o 1"));
}

Core::Expected<Platform::VSyncMode, std::string> ParseVSync(std::string_view value)
{
    if (EqualsIgnoringAsciiCase(value, "on"))
    {
        return Platform::VSyncMode::On;
    }
    if (EqualsIgnoringAsciiCase(value, "adaptive"))
    {
        return Platform::VSyncMode::Adaptive;
    }
    if (EqualsIgnoringAsciiCase(value, "off"))
    {
        return Platform::VSyncMode::Off;
    }
    return Unexpected(std::string("tiene que ser on, adaptive u off"));
}

Core::Expected<Kernel::PacingMode, std::string> ParsePacing(std::string_view value)
{
    if (EqualsIgnoringAsciiCase(value, "decoupled"))
    {
        return Kernel::PacingMode::Decoupled;
    }
    if (EqualsIgnoringAsciiCase(value, "faithful"))
    {
        return Kernel::PacingMode::Faithful;
    }
    return Unexpected(std::string("tiene que ser decoupled o faithful"));
}

// The file is UTF-8, and path(std::string) on Windows reads the ANSI code page: a folder with a non-ASCII letter
// would come out wrong. Core::PathFromUtf8 reads it as UTF-8 and rejects text that is not.
Core::Expected<std::filesystem::path, std::string> ParsePath(std::string_view value)
{
    const std::string_view text = WithoutQuotes(value);
    if (text.empty())
    {
        return Unexpected(std::string("la ruta está vacía"));
    }
    if (std::ranges::any_of(text, IsControl))
    {
        return Unexpected(std::string("la ruta no puede tener caracteres de control"));
    }
    auto path = Core::PathFromUtf8(text);
    if (!path)
    {
        return Unexpected(std::string("no es UTF-8"));
    }
    return std::move(*path);
}

std::optional<std::filesystem::path>& PathField(ClientConfig& config, Key key)
{
    switch (key)
    {
    case Key::VideoFontFile:
        return config.fontFile;
    case Key::VideoBoldFontFile:
        return config.boldFontFile;
    case Key::LogPath:
        return config.logPath;
    default:
        return config.dataPath;
    }
}

// Returns why the value cannot be read, if it cannot.
std::optional<std::string> Store(ClientConfig& config, Key key, std::string_view value)
{
    switch (key)
    {
    case Key::DataPath:
    case Key::VideoFontFile:
    case Key::VideoBoldFontFile:
    case Key::LogPath:
    {
        const auto path = ParsePath(value);
        if (!path.HasValue())
        {
            return path.Error();
        }
        PathField(config, key) = path.Value();
        return std::nullopt;
    }
    case Key::ServerHost:
    {
        // ParseHost accepts only ASCII, so UTF-8 needs no check here.
        const auto host = Session::ParseHost(WithoutQuotes(value));
        if (!host.HasValue())
        {
            return host.Error();
        }
        if (!config.server.has_value())
        {
            config.server.emplace();
        }
        config.server->host = host.Value();
        return std::nullopt;
    }
    case Key::ServerPort:
    {
        const auto port = Session::ParsePort(value);
        if (!port.HasValue())
        {
            return port.Error();
        }
        if (!config.server.has_value())
        {
            config.server.emplace();
        }
        config.server->port = port.Value();
        return std::nullopt;
    }
    case Key::VideoWidth:
    case Key::VideoHeight:
    {
        const auto size = ParseSize(value);
        if (!size.HasValue())
        {
            return size.Error();
        }
        (key == Key::VideoWidth ? config.width : config.height) = size.Value();
        return std::nullopt;
    }
    case Key::VideoMaxFps:
    {
        const auto maxFps = ParseMaxFps(value);
        if (!maxFps.HasValue())
        {
            return maxFps.Error();
        }
        config.maxFps = maxFps.Value();
        return std::nullopt;
    }
    case Key::VideoVSync:
    {
        const auto vsync = ParseVSync(value);
        if (!vsync.HasValue())
        {
            return vsync.Error();
        }
        config.vsync = vsync.Value();
        return std::nullopt;
    }
    case Key::VideoPacing:
    {
        const auto pacing = ParsePacing(value);
        if (!pacing.HasValue())
        {
            return pacing.Error();
        }
        config.pacing = pacing.Value();
        return std::nullopt;
    }
    case Key::AudioSoundOnOff:
    case Key::AudioMusicOnOff:
    {
        const auto enabled = ParseOnOff(value);
        if (!enabled.HasValue())
        {
            return enabled.Error();
        }
        (key == Key::AudioSoundOnOff ? config.soundOnOff : config.musicOnOff) = enabled.Value();
        return std::nullopt;
    }
    }
    return std::nullopt;
}

ConfigError ErrorAt(std::size_t line, std::string message)
{
    return ConfigError{line, std::move(message)};
}

std::filesystem::path FromFolder(const std::filesystem::path& folder, const std::filesystem::path& path)
{
    return path.is_relative() ? folder / path : path;
}

// Empty if the file is missing; with `required`, missing is an error. Only not_found gives the defaults: a system
// error (a permission) is reported. Read without exceptions: with istreambuf_iterator, an opened folder on Linux
// throws ios_base::failure.
Core::Expected<std::optional<std::string>, std::string> ReadConfigFile(const std::filesystem::path& path, bool required)
{
    std::error_code error;
    const std::filesystem::file_status status = std::filesystem::status(path, error);
    if (status.type() == std::filesystem::file_type::not_found)
    {
        if (required)
        {
            return Unexpected(std::format("no existe la config {}", Core::PathText(path)));
        }
        return std::optional<std::string>();
    }
    if (error)
    {
        return Unexpected(std::format("no se pudo leer la config {}: {}", Core::PathText(path), error.message()));
    }
    if (!std::filesystem::is_regular_file(status))
    {
        return Unexpected(std::format("la config {} no es un archivo", Core::PathText(path)));
    }
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error)
    {
        return Unexpected(std::format("no se pudo leer la config {}: {}", Core::PathText(path), error.message()));
    }
    if (size > MaxConfigBytes)
    {
        return Unexpected(std::format("la config {} pasa de {} bytes", Core::PathText(path), MaxConfigBytes));
    }
    std::ifstream file(path, std::ios::binary);
    std::string text(static_cast<std::size_t>(size), '\0');
    file.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file.is_open() || file.bad())
    {
        return Unexpected(std::format("no se pudo leer la config {}", Core::PathText(path)));
    }
    text.resize(static_cast<std::size_t>(file.gcount()));
    return std::optional<std::string>(std::move(text));
}
} // namespace

Core::Expected<ClientConfig, ConfigError> ParseConfig(std::string_view text)
{
    if (text.starts_with(Utf8Bom))
    {
        text.remove_prefix(Utf8Bom.size());
    }
    ClientConfig config;
    std::string_view section;
    std::array<bool, KnownKeys.size()> seen{};
    std::size_t lineNumber = 0;
    while (!text.empty())
    {
        const std::size_t newline = text.find('\n');
        std::string_view rawLine = text.substr(0, newline);
        text.remove_prefix(newline == std::string_view::npos ? text.size() : newline + 1);
        ++lineNumber;

        // The line end is \n or \r\n. A lone \r (old Macs) does not end the line, and without this error everything
        // after a comment would end up inside it.
        if (rawLine.ends_with('\r'))
        {
            rawLine.remove_suffix(1);
        }
        if (rawLine.find('\r') != std::string_view::npos)
        {
            return Unexpected(ErrorAt(lineNumber, "hay un \\r sin \\n: el fin de línea es \\n o \\r\\n"));
        }
        const std::string_view line = Trim(WithoutComment(rawLine));
        if (line.empty())
        {
            continue;
        }
        if (line.front() == '[')
        {
            const std::size_t close = line.find(']');
            if (close == std::string_view::npos)
            {
                return Unexpected(ErrorAt(lineNumber, "a la sección le falta el ]"));
            }
            if (close != line.size() - 1)
            {
                return Unexpected(ErrorAt(lineNumber, "hay texto después del ] de la sección"));
            }
            section = Trim(line.substr(1, line.size() - 2));
            if (!IsKnownSection(section))
            {
                return Unexpected(ErrorAt(lineNumber, std::format("no existe la sección [{}]", section)));
            }
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos)
        {
            return Unexpected(ErrorAt(lineNumber, "se esperaba [Sección] o Clave = valor"));
        }
        const std::string_view name = Trim(line.substr(0, equals));
        const std::string_view value = Trim(line.substr(equals + 1));
        if (name.empty())
        {
            return Unexpected(ErrorAt(lineNumber, "falta el nombre de la clave"));
        }
        if (section.empty())
        {
            return Unexpected(ErrorAt(lineNumber, std::format("{} está antes de la primera sección", name)));
        }
        const std::size_t index = FindKey(section, name);
        if (index == KnownKeys.size() && IsRemovedObfuscationKey(section, name))
        {
            return Unexpected(ErrorAt(lineNumber, "Obfuscation no longer exists: the server's packet shape is chosen "
                                                  "when building (-DMU_XOR_REMAP=OFF for a server that speaks like "
                                                  "main.exe)"));
        }
        if (index == KnownKeys.size())
        {
            return Unexpected(ErrorAt(lineNumber, std::format("no existe la clave {} en [{}]", name, section)));
        }
        const KnownKey& known = KnownKeys[index];
        if (seen[index])
        {
            return Unexpected(ErrorAt(lineNumber, std::format("[{}] {} está repetida", known.section, known.name)));
        }
        seen[index] = true;
        if (value.empty())
        {
            return Unexpected(ErrorAt(lineNumber, std::format("[{}] {} no tiene valor", known.section, known.name)));
        }
        if (const auto error = Store(config, known.key, value))
        {
            return Unexpected(ErrorAt(lineNumber, std::format("[{}] {}: {}", known.section, known.name, *error)));
        }
    }
    return config;
}

Core::Expected<ClientConfig, std::string> Resolve(const std::filesystem::path& exeFolder,
                                                  const CommandLine& commandLine)
{
    const std::filesystem::path configPath = commandLine.configPath.value_or(exeFolder / DefaultConfigFileName);
    const auto text = ReadConfigFile(configPath, commandLine.configPath.has_value());
    if (!text.HasValue())
    {
        return Unexpected(text.Error());
    }
    ClientConfig config;
    if (text.Value())
    {
        auto parsed = ParseConfig(*text.Value());
        if (!parsed.HasValue())
        {
            return Unexpected(std::format("{}, línea {}: {}", Core::PathText(configPath), parsed.Error().line,
                                          parsed.Error().message));
        }
        config = std::move(parsed).Value();
        for (std::optional<std::filesystem::path>* path :
             {&config.dataPath, &config.logPath, &config.fontFile, &config.boldFontFile})
        {
            if (*path)
            {
                *path = FromFolder(exeFolder, **path);
            }
        }
    }
    if (commandLine.connect)
    {
        config.server = commandLine.connect;
    }
    if (commandLine.dataPath)
    {
        config.dataPath = commandLine.dataPath;
    }
    if (commandLine.pacing)
    {
        config.pacing = *commandLine.pacing;
    }
    if (commandLine.vsync)
    {
        config.vsync = commandLine.vsync;
    }
    if (commandLine.maxFps)
    {
        config.maxFps = *commandLine.maxFps;
    }
    // In faithful mode VSync is off, like the original (CL3.2).
    if (config.pacing == Kernel::PacingMode::Faithful)
    {
        if (config.vsync && *config.vsync != Platform::VSyncMode::Off)
        {
            return Unexpected(
                std::string("el modo fiel (Pacing o --pacing faithful) necesita VSync off (VSync o --vsync): "
                            "el original no pide VSync"));
        }
        config.vsync = Platform::VSyncMode::Off;
        if (config.maxFps != 0)
        {
            return Unexpected(
                std::string("el modo fiel (Pacing o --pacing faithful) no lleva tope de FPS (MaxFps o --max-fps): "
                            "espera como el original"));
        }
    }
    else if (!config.vsync)
    {
        config.vsync = Platform::VSyncMode::On;
    }
    // --auto-login enters a server (CL5.5): without a host there is none to enter.
    if (commandLine.autoLogin && !HasServerHost(config))
    {
        return Unexpected(
            std::string("--auto-login needs a server: --host and --port, or [Server] Host in the config"));
    }
    // The data folder, only if given (Q-C35: the original exited without a warning).
    if (config.dataPath)
    {
        const std::string folder = Core::PathText(*config.dataPath);
        std::error_code error;
        const std::filesystem::file_status status = std::filesystem::status(*config.dataPath, error);
        if (status.type() == std::filesystem::file_type::not_found)
        {
            return Unexpected(std::format("no existe la carpeta de datos {}", folder));
        }
        if (error)
        {
            return Unexpected(std::format("no se pudo leer la carpeta de datos {}: {}", folder, error.message()));
        }
        if (!std::filesystem::is_directory(status))
        {
            return Unexpected(std::format("la carpeta de datos {} no es una carpeta", folder));
        }
    }
    return config;
}

bool HasServerHost(const ClientConfig& config)
{
    return config.server && !config.server->host.empty();
}

int EffectiveMaxFps(int maxFps, Platform::VSyncMode vsync)
{
    return vsync == Platform::VSyncMode::Off ? maxFps : 0;
}
} // namespace Client::App
