#include "support/WireCapture.h"

#include "Core/Expected.h"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace Client::Tests
{
namespace
{
// Los campos de una línea: hora, puerto, sentido o ACC, len=N y los bytes.
constexpr std::size_t PortField = 1;
constexpr std::size_t KindField = 2;
constexpr std::size_t LengthField = 3;
constexpr std::size_t BytesField = 4;
constexpr std::size_t MinFields = 4;

constexpr std::string_view OpenKind = "ACC";
constexpr std::string_view ClientToServerKind = "C->S";
constexpr std::string_view ServerToClientKind = "S->C";
constexpr std::string_view LengthPrefix = "len=";

// Dígitos hex de un byte, y el valor de un nibble.
constexpr std::size_t HexDigitsPerByte = 2;
constexpr unsigned NibbleBits = 4;

std::vector<std::string_view> SplitFields(std::string_view line)
{
    std::vector<std::string_view> fields;
    std::size_t start = line.find_first_not_of(" \t");
    while (start != std::string_view::npos)
    {
        const std::size_t end = line.find_first_of(" \t", start);
        fields.push_back(line.substr(start, end - start));
        start = end == std::string_view::npos ? end : line.find_first_not_of(" \t", end);
    }
    return fields;
}

template <typename Number> std::optional<Number> ParseNumber(std::string_view text)
{
    Number value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size())
    {
        return std::nullopt;
    }
    return value;
}

std::optional<unsigned> HexNibble(char digit)
{
    if (digit >= '0' && digit <= '9')
    {
        return static_cast<unsigned>(digit - '0');
    }
    if (digit >= 'a' && digit <= 'f')
    {
        return static_cast<unsigned>(digit - 'a' + 10);
    }
    if (digit >= 'A' && digit <= 'F')
    {
        return static_cast<unsigned>(digit - 'A' + 10);
    }
    return std::nullopt;
}

std::optional<std::vector<std::byte>> ParseHex(std::string_view hex)
{
    if (hex.size() % HexDigitsPerByte != 0)
    {
        return std::nullopt;
    }
    std::vector<std::byte> bytes;
    bytes.reserve(hex.size() / HexDigitsPerByte);
    for (std::size_t i = 0; i < hex.size(); i += HexDigitsPerByte)
    {
        const std::optional<unsigned> high = HexNibble(hex[i]);
        const std::optional<unsigned> low = HexNibble(hex[i + 1]);
        if (!high || !low)
        {
            return std::nullopt;
        }
        bytes.push_back(static_cast<std::byte>((*high << NibbleBits) | *low));
    }
    return bytes;
}

std::optional<WireDirection> DirectionOf(std::string_view kind)
{
    if (kind == ClientToServerKind)
    {
        return WireDirection::ClientToServer;
    }
    if (kind == ServerToClientKind)
    {
        return WireDirection::ServerToClient;
    }
    return std::nullopt;
}

// Lee un segmento: el sentido, len=N y los N bytes. std::nullopt con el problema en `problem`.
std::optional<WireSegment> ParseSegment(const std::vector<std::string_view>& fields, WireCaptureProblem& problem)
{
    const std::optional<WireDirection> direction = DirectionOf(fields[KindField]);
    const std::string_view length = fields[LengthField];
    if (!direction || fields.size() <= BytesField || !length.starts_with(LengthPrefix))
    {
        problem = WireCaptureProblem::MalformedLine;
        return std::nullopt;
    }
    const std::optional<std::size_t> declared = ParseNumber<std::size_t>(length.substr(LengthPrefix.size()));
    std::optional<std::vector<std::byte>> bytes = ParseHex(fields[BytesField]);
    if (!declared || !bytes)
    {
        problem = declared ? WireCaptureProblem::BadHex : WireCaptureProblem::MalformedLine;
        return std::nullopt;
    }
    if (bytes->size() != *declared)
    {
        problem = WireCaptureProblem::LengthMismatch;
        return std::nullopt;
    }
    return WireSegment{*direction, std::move(*bytes)};
}

// Saca la primera línea de `text`, sin el fin de línea (\n o \r\n).
std::string_view TakeLine(std::string_view& text)
{
    const std::size_t end = text.find('\n');
    std::string_view line = text.substr(0, end);
    text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
    if (line.ends_with('\r'))
    {
        line.remove_suffix(1);
    }
    return line;
}
} // namespace

std::vector<std::span<const std::byte>> WireConnection::Reads(WireDirection direction) const
{
    std::vector<std::span<const std::byte>> reads;
    for (const WireSegment& segment : segments)
    {
        if (segment.direction == direction)
        {
            reads.emplace_back(segment.bytes);
        }
    }
    return reads;
}

Core::Expected<std::vector<WireConnection>, WireCaptureError> ParseWireCapture(std::string_view text)
{
    std::vector<WireConnection> connections;
    std::map<std::uint16_t, std::size_t> openByPort; // puerto -> su conexión abierta
    std::size_t lineNumber = 0;
    while (!text.empty())
    {
        ++lineNumber;
        const std::string_view line = TakeLine(text);
        // Un \r que no es el del final: un log con fines de línea de Mac viejo, que leído así
        // sería una sola línea larga.
        if (line.find('\r') != std::string_view::npos)
        {
            return Core::Unexpected(WireCaptureError{lineNumber, WireCaptureProblem::MalformedLine});
        }
        const std::vector<std::string_view> fields = SplitFields(line);
        if (fields.empty())
        {
            continue;
        }
        const std::optional<std::uint16_t> port =
            fields.size() >= MinFields ? ParseNumber<std::uint16_t>(fields[PortField]) : std::nullopt;
        if (!port)
        {
            return Core::Unexpected(WireCaptureError{lineNumber, WireCaptureProblem::MalformedLine});
        }
        if (fields[KindField] == OpenKind)
        {
            openByPort[*port] = connections.size();
            connections.push_back(WireConnection{*port, {}});
            continue;
        }
        WireCaptureProblem problem{};
        std::optional<WireSegment> segment = ParseSegment(fields, problem);
        if (!segment)
        {
            return Core::Unexpected(WireCaptureError{lineNumber, problem});
        }
        const auto open = openByPort.find(*port);
        if (open == openByPort.end())
        {
            return Core::Unexpected(WireCaptureError{lineNumber, WireCaptureProblem::NoConnection});
        }
        connections[open->second].segments.push_back(std::move(*segment));
    }
    return connections;
}
} // namespace Client::Tests
