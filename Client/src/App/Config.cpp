#include "App/Config.h"

#include "App/LoopTiming.h"
#include "Core/PathText.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <fstream>
#include <ios>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace Client::App
{
using Core::Unexpected;

namespace
{
// El BOM de UTF-8: el Bloc de notas lo pone adelante al guardar en UTF-8.
constexpr std::string_view Utf8Bom = "\xEF\xBB\xBF";

enum class Key : std::uint8_t
{
    DataPath,
    ServerHost,
    ServerPort,
    ServerObfuscation,
    VideoWidth,
    VideoHeight,
    VideoVSync,
    VideoPacing,
    VideoMaxFps,
    VideoFontFile,
    VideoBoldFontFile,
    LogPath,
};

struct KnownKey
{
    std::string_view section;
    std::string_view name;
    Key key;
};

// Las claves de muret-client.ini, con la sección donde van (Config.h).
constexpr std::array KnownKeys = {
    KnownKey{"Data", "Path", Key::DataPath},
    KnownKey{"Server", "Host", Key::ServerHost},
    KnownKey{"Server", "Port", Key::ServerPort},
    KnownKey{"Server", "Obfuscation", Key::ServerObfuscation},
    KnownKey{"Video", "Width", Key::VideoWidth},
    KnownKey{"Video", "Height", Key::VideoHeight},
    KnownKey{"Video", "VSync", Key::VideoVSync},
    KnownKey{"Video", "Pacing", Key::VideoPacing},
    KnownKey{"Video", "MaxFps", Key::VideoMaxFps},
    KnownKey{"Video", "FontFile", Key::VideoFontFile},
    KnownKey{"Video", "BoldFontFile", Key::VideoBoldFontFile},
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

// Los de ASCII: del 0x00 al 0x1F y el 0x7F.
bool IsControl(char c)
{
    const auto byte = static_cast<unsigned char>(c);
    return byte < 0x20 || byte == 0x7F;
}

// Saca un par de comillas dobles de alrededor, las que pone "Copiar como ruta" del Explorador.
// Una comilla sola queda como parte del valor.
std::string_view WithoutQuotes(std::string_view value)
{
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
    {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

char ToLowerAscii(char c)
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool EqualsIgnoringCase(std::string_view a, std::string_view b)
{
    return std::ranges::equal(a, b, [](char x, char y) { return ToLowerAscii(x) == ToLowerAscii(y); });
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

// Corta en el primer `;` que esté al principio o después de un espacio: "a;b" es un valor.
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
    return std::ranges::any_of(KnownKeys,
                               [section](const KnownKey& known) { return EqualsIgnoringCase(known.section, section); });
}

// La posición en KnownKeys, o KnownKeys.size() si no está.
std::size_t FindKey(std::string_view section, std::string_view name)
{
    const auto found = std::ranges::find_if(
        KnownKeys, [section, name](const KnownKey& known)
        { return EqualsIgnoringCase(known.section, section) && EqualsIgnoringCase(known.name, name); });
    return static_cast<std::size_t>(found - KnownKeys.begin());
}

Core::Expected<int, std::string> ParseSize(std::string_view value)
{
    // Como en ParsePort: los dígitos se miran antes por el mensaje.
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

// [Video] MaxFps: 0 (sin tope), o de MinFpsLimit a MaxFpsLimit (IsValidMaxFps). Como en ParseSize,
// los dígitos se miran antes por el mensaje.
Core::Expected<int, std::string> ParseMaxFps(std::string_view value)
{
    if (!std::ranges::all_of(value, IsDigit))
    {
        return Unexpected(std::string("tiene que ser un número"));
    }
    int maxFps = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), maxFps);
    if (result.ec != std::errc{} || !IsValidMaxFps(maxFps))
    {
        return Unexpected(std::format("va de {} a {}, o 0 (sin tope)", MinFpsLimit, MaxFpsLimit));
    }
    return maxFps;
}

Core::Expected<VSyncMode, std::string> ParseVSync(std::string_view value)
{
    if (EqualsIgnoringCase(value, "on"))
    {
        return VSyncMode::On;
    }
    if (EqualsIgnoringCase(value, "adaptive"))
    {
        return VSyncMode::Adaptive;
    }
    if (EqualsIgnoringCase(value, "off"))
    {
        return VSyncMode::Off;
    }
    return Unexpected(std::string("tiene que ser on, adaptive u off"));
}

Core::Expected<PacingMode, std::string> ParsePacing(std::string_view value)
{
    if (EqualsIgnoringCase(value, "decoupled"))
    {
        return PacingMode::Decoupled;
    }
    if (EqualsIgnoringCase(value, "faithful"))
    {
        return PacingMode::Faithful;
    }
    return Unexpected(std::string("tiene que ser decoupled o faithful"));
}

Core::Expected<ObfuscationMode, std::string> ParseObfuscation(std::string_view value)
{
    if (EqualsIgnoringCase(value, "foxanti"))
    {
        return ObfuscationMode::FoxAnti;
    }
    if (EqualsIgnoringCase(value, "none"))
    {
        return ObfuscationMode::None;
    }
    return Unexpected(std::string("tiene que ser foxanti o none"));
}

// El archivo es UTF-8, y path(std::string) en Windows lee la página de códigos ANSI: una carpeta
// con una letra que no sea ASCII quedaría mal. Core::PathFromUtf8 la lee como UTF-8 y rechaza el
// texto que no lo es.
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

// El campo de una clave que es una ruta.
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

// Guarda el valor en su campo; si no se puede leer, devuelve por qué.
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
        // ParseHost acepta solo ASCII, así que acá no hace falta mirar el UTF-8.
        const auto host = ParseHost(WithoutQuotes(value));
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
        const auto port = ParsePort(value);
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
    case Key::ServerObfuscation:
    {
        // No arma config.server: la ofuscación no es parte de la dirección.
        const auto obfuscation = ParseObfuscation(value);
        if (!obfuscation.HasValue())
        {
            return obfuscation.Error();
        }
        config.obfuscation = obfuscation.Value();
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

// El texto del archivo, o vacío si no está; con `required`, que no esté es un error. Solo "no
// está" (not_found) da los valores por defecto: un error del sistema (un permiso) se dice. Se lee
// sin excepciones: con istreambuf_iterator, una carpeta abierta en Linux tira ios_base::failure.
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

        // El fin de línea es \n o \r\n. Un \r solo (el de los Mac viejos) no corta la línea, y
        // sin este error todo lo que sigue a un comentario quedaría adentro de él.
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
    // 1. El archivo.
    const std::filesystem::path configPath = commandLine.configPath.value_or(exeFolder / DefaultConfigFileName);
    const auto text = ReadConfigFile(configPath, commandLine.configPath.has_value());
    if (!text.HasValue())
    {
        return Unexpected(text.Error());
    }
    // 2. ParseConfig, con las rutas relativas desde la carpeta del ejecutable.
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
    // 3. La línea de comandos pisa.
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
    // 4. Las combinaciones: en el modo fiel VSync va apagado, como el original (CL3.2).
    if (config.pacing == PacingMode::Faithful)
    {
        if (config.vsync && *config.vsync != VSyncMode::Off)
        {
            return Unexpected(
                std::string("el modo fiel (Pacing o --pacing faithful) necesita VSync off (VSync o --vsync): "
                            "el original no pide VSync"));
        }
        config.vsync = VSyncMode::Off;
        if (config.maxFps != 0)
        {
            return Unexpected(
                std::string("el modo fiel (Pacing o --pacing faithful) no lleva tope de FPS (MaxFps o --max-fps): "
                            "espera como el original"));
        }
    }
    else if (!config.vsync)
    {
        config.vsync = VSyncMode::On;
    }
    // 5. La carpeta de datos, solo si se dio (Q-C35: el original salía sin avisar).
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

int EffectiveMaxFps(int maxFps, VSyncMode vsync)
{
    return vsync == VSyncMode::Off ? maxFps : 0;
}
} // namespace Client::App
