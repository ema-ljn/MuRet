#include "App/ServerAddress.h"

#include <algorithm>
#include <charconv>
#include <format>
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

// Los de ASCII: del 0x00 al 0x1F y el 0x7F. Un \0 cortaría el host en el c_str() de la CL4.
bool IsControl(char c)
{
    const auto byte = static_cast<unsigned char>(c);
    return byte < 0x20 || byte == 0x7F;
}
} // namespace

Core::Expected<std::uint16_t, std::string> ParsePort(std::string_view text)
{
    // Los dígitos se miran antes por el mensaje: sin esto, "12a" (from_chars se para en la "a") y
    // "-1" (sin signo no lo acepta) dirían "va de 1 a 65535".
    if (text.empty() || !std::ranges::all_of(text, IsDigit))
    {
        return Unexpected(std::string("el puerto tiene que ser un número"));
    }
    // Con solo dígitos, from_chars lee todo, y el único error que queda es el de 16 bits.
    std::uint16_t port = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), port);
    if (result.ec != std::errc{} || port == 0)
    {
        return Unexpected(std::string("el puerto va de 1 a 65535"));
    }
    return port;
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
    if (std::ranges::any_of(text, IsControl))
    {
        return Unexpected(std::string("el host no puede tener caracteres de control"));
    }
    return std::string(text);
}
} // namespace Client::App
