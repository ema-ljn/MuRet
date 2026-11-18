#include "UI/OriginalFormat.h"

#include "Core/Expected.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace Client::UI
{
namespace
{
struct Spec
{
    bool left = false;
    bool plus = false;
    bool space = false;
    bool alternate = false;
    bool zero = false;
    std::size_t width = 0;
    std::optional<std::size_t> precision{};
};

std::optional<std::uint32_t> RawValue(const FormatArgument& argument)
{
    if (const auto* number = std::get_if<std::int32_t>(&argument))
        return static_cast<std::uint32_t>(*number);
    if (const auto* text = std::get_if<TextArgument>(&argument))
        return TextTableAddress + static_cast<std::uint32_t>(text->index) * TextRecordBytes;
    return std::get<StringArgument>(argument).address;
}

std::string Pad(std::string body, const Spec& spec, std::string_view sign = {})
{
    const std::size_t length = sign.size() + body.size();
    if (length >= spec.width)
        return std::string(sign) + body;
    const std::size_t fill = spec.width - length;
    if (spec.left)
        return std::string(sign) + body + std::string(fill, ' ');
    if (spec.zero)
        return std::string(sign) + std::string(fill, '0') + body;
    return std::string(fill, ' ') + std::string(sign) + body;
}

std::string Integer(std::uint32_t raw, char conversion, const Spec& spec, FormatFunction function)
{
    std::string sign;
    std::string digits;
    if (conversion == 'd' || conversion == 'i')
    {
        const auto value = static_cast<std::int32_t>(raw);
        const std::int64_t magnitude = value < 0 ? -static_cast<std::int64_t>(value) : value;
        digits = std::to_string(magnitude);
        if (value < 0)
            sign = "-";
        else if (function == FormatFunction::Sprintf && spec.plus)
            sign = "+";
        else if (function == FormatFunction::Sprintf && spec.space)
            sign = " ";
    }
    else if (conversion == 'u')
    {
        digits = std::to_string(raw);
    }
    else
    {
        digits = conversion == 'x' ? std::format("{:x}", raw) : std::format("{:X}", raw);
        if (spec.alternate && raw != 0)
            sign = conversion == 'x' ? "0x" : "0X";
    }
    if (spec.precision && digits.size() < *spec.precision)
        digits.insert(0, *spec.precision - digits.size(), '0');
    Spec padding = spec;
    if (spec.precision)
        padding.zero = false;
    return Pad(std::move(digits), padding, sign);
}

bool IsFlag(char c, FormatFunction function)
{
    if (c == '-' || c == '#' || c == '0')
        return true;
    return function == FormatFunction::Sprintf && (c == '+' || c == ' ');
}

void SetFlag(Spec& spec, char c)
{
    switch (c)
    {
    case '-':
        spec.left = true;
        break;
    case '+':
        spec.plus = true;
        break;
    case ' ':
        spec.space = true;
        break;
    case '#':
        spec.alternate = true;
        break;
    default:
        spec.zero = true;
        break;
    }
}

std::size_t Digits(std::string_view format, std::size_t& at)
{
    std::size_t value = 0;
    while (at < format.size() && format[at] >= '0' && format[at] <= '9')
    {
        value = value * 10 + static_cast<std::size_t>(format[at] - '0');
        ++at;
    }
    return value;
}
} // namespace

Core::Expected<std::string, std::string> FormatOriginal(FormatFunction function, std::string_view format,
                                                        std::span<const FormatArgument> arguments,
                                                        std::string_view where)
{
    std::string out;
    std::size_t next = 0;
    std::size_t at = 0;
    while (at < format.size())
    {
        const char c = format[at++];
        if (c != '%')
        {
            out.push_back(c);
            continue;
        }
        Spec spec;
        while (at < format.size() && IsFlag(format[at], function))
            SetFlag(spec, format[at++]);
        spec.width = Digits(format, at);
        if (at < format.size() && format[at] == '.')
        {
            ++at;
            spec.precision = Digits(format, at);
        }
        while (at < format.size() && (format[at] == 'l' || format[at] == 'h'))
            ++at;
        if (at >= format.size())
            break;
        const char conversion = format[at++];
        if (conversion == '%')
        {
            out.push_back('%');
            continue;
        }
        const bool known = conversion == 'd' || conversion == 'i' || conversion == 'u' || conversion == 'x' ||
                           conversion == 'X' || conversion == 'c' || conversion == 's';
        if (!known)
        {
            if (function == FormatFunction::Wsprintf)
            {
                // P-C290: wsprintfA copia el carácter que no entiende y sigue sin tomar argumento.
                out.push_back(conversion);
                continue;
            }
            return Core::Unexpected(std::format("{}: el sprintf de \"{}\" usa la conversión '{}', que MuRet no tiene",
                                                where, format, conversion));
        }
        if (next >= arguments.size())
            return Core::Unexpected(std::format("{}: \"{}\" pide más argumentos que los {} que pasa el original, y "
                                                "leería la pila",
                                                where, format, arguments.size()));
        const FormatArgument& argument = arguments[next++];
        if (conversion == 's')
        {
            std::string_view body;
            if (const auto* text = std::get_if<TextArgument>(&argument))
                body = text->text;
            else if (const auto* string = std::get_if<StringArgument>(&argument))
                body = string->text;
            else
                return Core::Unexpected(std::format("{}: \"{}\" pasa un número ({}) a un %s y el original lo leería "
                                                    "como puntero",
                                                    where, format, std::get<std::int32_t>(argument)));
            if (spec.precision && body.size() > *spec.precision)
                body = body.substr(0, *spec.precision);
            Spec padding = spec;
            padding.zero = false;
            out += Pad(std::string(body), padding);
            continue;
        }
        const std::optional<std::uint32_t> value = RawValue(argument);
        if (!value)
            return Core::Unexpected(std::format("{}: \"{}\" imprime con %{} la dirección de \"{}\", que MuRet no sabe",
                                                where, format, conversion, std::get<StringArgument>(argument).text));
        if (conversion == 'c')
        {
            Spec padding = spec;
            padding.zero = false;
            out += Pad(std::string(1, static_cast<char>(*value & 0xffu)), padding);
            continue;
        }
        out += Integer(*value, conversion, spec, function);
    }
    return out;
}
} // namespace Client::UI
