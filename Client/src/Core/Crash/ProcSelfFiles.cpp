#include "Core/Crash/ProcSelfFiles.h"

#include "Core/Crash/CrashReport.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>

namespace Client::Core::Crash
{
namespace
{
// Una línea de /proc/self/maps: "inicio-fin permisos offset dispositivo inodo   ruta".
struct MapsLine
{
    std::uint64_t start = 0;
    std::uint64_t end = 0;
    std::string_view path{};
};

// El campo que sigue en `rest`, saltando los espacios de adelante, y `rest` queda después de él.
std::string_view NextField(std::string_view& rest)
{
    const std::size_t begin = std::min(rest.find_first_not_of(' '), rest.size());
    rest.remove_prefix(begin);
    const std::size_t length = std::min(rest.find(' '), rest.size());
    const std::string_view field = rest.substr(0, length);
    rest.remove_prefix(length);
    return field;
}

std::optional<std::uint64_t> ParseNumber(std::string_view text, int base)
{
    std::uint64_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, base);
    if (text.empty() || parsed.ptr != text.data() + text.size())
    {
        return std::nullopt;
    }
    return value;
}

std::optional<MapsLine> ParseMapsLine(std::string_view line)
{
    constexpr int Hexadecimal = 16;
    constexpr int FieldsBeforePath = 4; // permisos, offset, dispositivo e inodo
    std::string_view rest = line;
    const std::string_view range = NextField(rest);
    const std::size_t dash = range.find('-');
    if (dash == std::string_view::npos)
    {
        return std::nullopt;
    }
    const auto start = ParseNumber(range.substr(0, dash), Hexadecimal);
    const auto end = ParseNumber(range.substr(dash + 1), Hexadecimal);
    if (!start || !end || *end < *start)
    {
        return std::nullopt;
    }
    for (int field = 0; field < FieldsBeforePath; ++field)
    {
        static_cast<void>(NextField(rest));
    }
    const std::size_t pathStart = std::min(rest.find_first_not_of(' '), rest.size());
    return MapsLine{.start = *start, .end = *end, .path = rest.substr(pathStart)};
}

std::uint32_t Saturated32(std::uint64_t value)
{
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(value, std::numeric_limits<std::uint32_t>::max()));
}

// La ruta como queda guardada (cortada en 255 bytes), para reconocer los mapeos de un mismo archivo.
std::string_view StoredPath(const CrashModule& module)
{
    const std::string_view path(module.path.data(), module.path.size());
    return path.substr(0, path.find('\0'));
}

std::string_view Truncated(std::string_view path)
{
    return path.substr(0, CrashModulePathSize - 1);
}
} // namespace

std::size_t ModulesFromProcMaps(std::string_view maps, std::span<CrashModule, MaxCrashModules> modules)
{
    std::size_t count = 0;
    std::uint64_t firstStart = 0;
    while (!maps.empty())
    {
        const std::size_t lineEnd = std::min(maps.find('\n'), maps.size());
        const std::optional<MapsLine> line = ParseMapsLine(maps.substr(0, lineEnd));
        maps.remove_prefix(std::min(lineEnd + 1, maps.size()));
        if (!line || !line->path.starts_with('/'))
        {
            continue;
        }
        if (count > 0 && StoredPath(modules[count - 1]) == Truncated(line->path))
        {
            modules[count - 1].size = Saturated32(line->end - firstStart);
            continue;
        }
        if (count == modules.size())
        {
            break;
        }
        CrashModule& module = modules[count++];
        CopyCrashText(module.name, line->path.substr(line->path.rfind('/') + 1));
        CopyCrashText(module.path, line->path);
        module.base = static_cast<std::uint32_t>(line->start);
        module.size = Saturated32(line->end - line->start);
        firstStart = line->start;
    }
    return count;
}

std::optional<std::uint32_t> ThreadCountFromProcStat(std::string_view stat)
{
    // Los campos van desde el 3, después del nombre entre paréntesis, que puede tener espacios y
    // paréntesis: se cuenta desde el último ')'.
    constexpr int ThreadsField = 20;
    constexpr int FirstFieldAfterName = 3;
    constexpr int Decimal = 10;
    const std::size_t nameEnd = stat.rfind(')');
    if (nameEnd == std::string_view::npos)
    {
        return std::nullopt;
    }
    std::string_view rest = stat.substr(nameEnd + 1);
    std::string_view field;
    for (int number = FirstFieldAfterName; number <= ThreadsField; ++number)
    {
        field = NextField(rest);
    }
    const std::size_t digits = std::min(field.find_first_not_of("0123456789"), field.size());
    const auto threads = ParseNumber(field.substr(0, digits), Decimal);
    if (!threads)
    {
        return std::nullopt;
    }
    return Saturated32(*threads);
}
} // namespace Client::Core::Crash
