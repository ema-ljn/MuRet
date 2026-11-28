#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// The ConnectServer's address comes from the config ([Server] Host and Port) or from the command line (/u<ip>
// /p<port>), and both validate it with the same functions, so they accept the same and say it the same way: the host
// is visible ASCII, which does not depend on the encoding (UTF-8 in the config, the ANSI page in Windows' argv), and
// the Q-C37 cap counts the same bytes in both. The GameServer's comes from the wire, in the F4 03
// (ReadServerAddress), and the ServerSession keeps it without passing it through ParseHost: an unusable host ends in
// a ResolveFailed of the connection.
namespace Client::Session
{
// The ConnectServer's compiled-in port, 0x6d5dc4 (02 § Del launcher al juego).
inline constexpr std::uint16_t DefaultConnectPort = 44405;
// Q-C37: the launcher's host reached 255 bytes and was copied without checking its length.
inline constexpr std::size_t MaxHostBytes = 255;

struct ServerAddress
{
    // Empty if not given: the host is only required when the network starts (CL4).
    std::string host;
    std::uint16_t port = DefaultConnectPort;
};

// A port written in decimal, digits only, from 1 to 65535. The error is "the port must be from 1 to 65535" or "the port
// must be a number".
Core::Expected<std::uint16_t, std::string> ParsePort(std::string_view text);

// 1 to MaxHostBytes bytes of visible ASCII (0x21 to 0x7E): an IP or a name, which with accents goes in punycode. It
// does not resolve names or check whether it is an IP.
Core::Expected<std::string, std::string> ParseHost(std::string_view text);
} // namespace Client::Session
