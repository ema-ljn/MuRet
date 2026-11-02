#include "App/ServerAddress.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <limits>
#include <system_error>

namespace Client::App
{
using Core::Unexpected;

namespace
{
bool IsDigit(char c)
{
    return c >= '0' && c <= '9';
}

bool IsSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}
} // namespace

Core::Expected<std::uint16_t, std::string> ParsePort(std::string_view text)
{
    // from_chars acepta un "-" adelante; el puerto, solo dígitos.
    if (text.empty() || !std::ranges::all_of(text, IsDigit))
    {
        return Unexpected(std::string("el puerto tiene que ser un número"));
    }
    unsigned long value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value == 0 ||
        value > std::numeric_limits<std::uint16_t>::max())
    {
        return Unexpected(std::string("el puerto va de 1 a 65535"));
    }
    return static_cast<std::uint16_t>(value);
}

Core::Expected<std::string, std::string> ParseHost(std::string_view text)
{
    if (text.empty() || text.size() > MaxHostBytes)
    {
        return Unexpected(std::format("el host va de 1 a {} bytes", MaxHostBytes));
    }
    if (std::ranges::any_of(text, IsSpace))
    {
        return Unexpected(std::string("el host no puede tener espacios"));
    }
    return std::string(text);
}
} // namespace Client::App
