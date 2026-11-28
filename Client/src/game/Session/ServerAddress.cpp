#include "Session/ServerAddress.h"

#include "Core/Expected.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <system_error>

namespace Client::Session
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
    // Digits first, for the message: "12a" (from_chars stops at the "a") and "-1" would say "from 1 to 65535".
    if (text.empty() || !std::ranges::all_of(text, IsDigit))
    {
        return Unexpected(std::string("the port must be a number"));
    }
    // With only digits, from_chars reads them all, and the only error left is the 16 bits.
    std::uint16_t port = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), port);
    if (result.ec != std::errc{} || port == 0)
    {
        return Unexpected(std::string("the port must be from 1 to 65535"));
    }
    return port;
}

Core::Expected<std::string, std::string> ParseHost(std::string_view text)
{
    if (text.empty() || text.size() > MaxHostBytes)
    {
        return Unexpected(std::format("the host must be 1 to {} bytes", MaxHostBytes));
    }
    if (std::ranges::any_of(text, IsSpace))
    {
        return Unexpected(std::string("the host cannot have spaces"));
    }
    if (std::ranges::any_of(text, IsControl))
    {
        return Unexpected(std::string("the host cannot have control characters"));
    }
    if (std::ranges::any_of(text, [](char c) { return static_cast<unsigned char>(c) >= 0x80; }))
    {
        return Unexpected(std::string("the host must be ASCII (a name with accents goes in punycode)"));
    }
    return std::string(text);
}
} // namespace Client::Session
