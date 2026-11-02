#include "App/Config.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
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
    VideoWidth,
    VideoHeight,
    VideoVSync,
    VideoPacing,
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
    KnownKey{"Data", "Path", Key::DataPath},       KnownKey{"Server", "Host", Key::ServerHost},
    KnownKey{"Server", "Port", Key::ServerPort},   KnownKey{"Video", "Width", Key::VideoWidth},
    KnownKey{"Video", "Height", Key::VideoHeight}, KnownKey{"Video", "VSync", Key::VideoVSync},
    KnownKey{"Video", "Pacing", Key::VideoPacing}, KnownKey{"Log", "Path", Key::LogPath},
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

// UTF-8 bien formado: sin bytes sueltos, sin formas más largas de lo necesario, sin las mitades de
// UTF-16 (U+D800 a U+DFFF) y sin pasar de U+10FFFF. Es lo que rechaza MultiByteToWideChar con
// MB_ERR_INVALID_CHARS, que usa path en Windows: con un byte malo tiraría una excepción.
bool IsUtf8(std::string_view text)
{
    std::size_t i = 0;
    while (i < text.size())
    {
        const auto first = static_cast<unsigned char>(text[i]);
        std::size_t length = 1;
        std::uint32_t codePoint = first;
        if (first >= 0xC2 && first <= 0xDF)
        {
            length = 2;
            codePoint = first & 0x1Fu;
        }
        else if (first >= 0xE0 && first <= 0xEF)
        {
            length = 3;
            codePoint = first & 0x0Fu;
        }
        else if (first >= 0xF0 && first <= 0xF4)
        {
            length = 4;
            codePoint = first & 0x07u;
        }
        else if (first >= 0x80)
        {
            return false;
        }
        if (text.size() - i < length)
        {
            return false;
        }
        for (std::size_t k = 1; k < length; ++k)
        {
            const auto next = static_cast<unsigned char>(text[i + k]);
            if ((next & 0xC0u) != 0x80u)
            {
                return false;
            }
            codePoint = (codePoint << 6) | (next & 0x3Fu);
        }
        if ((length == 3 && codePoint < 0x800) || (length == 4 && codePoint < 0x10000) ||
            (codePoint >= 0xD800 && codePoint <= 0xDFFF) || codePoint > 0x10FFFF)
        {
            return false;
        }
        i += length;
    }
    return true;
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

// El archivo es UTF-8, y path(std::string) en Windows lee la página de códigos ANSI: una carpeta
// con una letra que no sea ASCII quedaría mal. Como char8_t, path lo lee como UTF-8; por eso el
// texto se valida antes.
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
    if (!IsUtf8(text))
    {
        return Unexpected(std::string("no es UTF-8"));
    }
    std::u8string utf8(text.size(), u8'\0');
    std::ranges::transform(text, utf8.begin(), [](char c) { return static_cast<char8_t>(c); });
    return std::filesystem::path(utf8);
}

// Guarda el valor en su campo; si no se puede leer, devuelve por qué.
std::optional<std::string> Store(ClientConfig& config, Key key, std::string_view value)
{
    switch (key)
    {
    case Key::DataPath:
    case Key::LogPath:
    {
        const auto path = ParsePath(value);
        if (!path.HasValue())
        {
            return path.Error();
        }
        (key == Key::DataPath ? config.dataPath : config.logPath) = path.Value();
        return std::nullopt;
    }
    case Key::ServerHost:
    {
        const std::string_view text = WithoutQuotes(value);
        if (!IsUtf8(text))
        {
            return std::string("no es UTF-8");
        }
        const auto host = ParseHost(text);
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
} // namespace Client::App
