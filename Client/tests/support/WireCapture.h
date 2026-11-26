#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

// The lab's wire: reads the muproxy.log the lab's proxy wrote between the original client and the servers.
// One line per TCP segment: time, port, direction (C->S or S->C), len=N and the N bytes in hex. An ACC line opens a
// connection on that port, and the following lines of that port belong to it until the next ACC; its bytes are a
// proxy text, not the wire's, and are not read. What follows the bytes is ignored: the lab's log has a proxy mark on
// one line.
// The wire carries the player's login: whoever uses this does not print a payload, not even when failing
// (SameBytesWithoutValues, in SameBytes.h).
namespace Client::Tests
{
enum class WireDirection : std::uint8_t
{
    ClientToServer, // C->S
    ServerToClient, // S->C
};

// What a TCP segment brought, in one direction.
struct WireSegment
{
    WireDirection direction = WireDirection::ClientToServer;
    std::vector<std::byte> bytes;
};

// A connection, from its ACC: the proxy's port and the segments in the order they passed.
struct WireConnection
{
    std::uint16_t port = 0;
    std::vector<WireSegment> segments;

    // One direction's segments, in order: what each recv of the proxy returned, on the receiving side.
    [[nodiscard]] std::vector<std::span<const std::byte>> Reads(WireDirection direction) const;
};

enum class WireCaptureProblem : std::uint8_t
{
    MalformedLine,  // missing fields, the direction or the port cannot be parsed, or a \r in the middle
    BadHex,         // the bytes are not two-digit hex
    LengthMismatch, // len=N is not what the bytes measure
    NoConnection,   // a segment from a port without an ACC before
};

// Which line (from 1) and which problem: never its content.
struct WireCaptureError
{
    std::size_t line = 0;
    WireCaptureProblem problem = WireCaptureProblem::MalformedLine;
};

// The log's connections, in the order of their ACCs.
[[nodiscard]] Core::Expected<std::vector<WireConnection>, WireCaptureError> ParseWireCapture(std::string_view text);
} // namespace Client::Tests
