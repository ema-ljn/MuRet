#pragma once

#include "EngineApi.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>

// The system calls for a non-blocking TCP socket, the same on Winsock 2 and BSD. This header includes no system
// header, so <winsock2.h> and its macros do not reach the rest of the client (03-red-y-cifrado.md, "Sockets").
// Not a library (CODING_RULES §10, checked with Context7 on 2026-11-02): standalone asio's model is asynchronous
// handlers whose buffers must live until the handler, for one connection polled per frame; SDL3_net queues writes
// with no cap (the original's 0x2000 cap would sit on top) and depends on SDL3, which Engine does not see. The only
// loss is resolving a name without waiting, and the original does not do that either.
namespace Client::Network::SocketApi
{
// SOCKET (UINT_PTR) on Windows, int on POSIX: both fit in a uintptr_t. INVALID_SOCKET is ~0 on Windows; on POSIX a
// descriptor never gets there.
using Handle = std::uintptr_t;
inline constexpr Handle InvalidHandle = ~Handle{0};

// Already translated from errno or WSAGetLastError.
enum class Status : std::uint8_t
{
    Done,       // in Receive and Send, `bytes` says how many
    TryLater,   // EWOULDBLOCK, EAGAIN, WSAEWOULDBLOCK or EINTR: nothing yet
    InProgress, // EINPROGRESS or EINTR on Linux, WSAEWOULDBLOCK on Windows
    Refused,
    PeerClosed, // recv at 0, or ECONNRESET, ECONNABORTED, EPIPE, WSAECONNRESET or WSAECONNABORTED
    Failed,     // another error: `systemError` says which
};

struct Result
{
    Status status = Status::Done;
    std::size_t bytes = 0;
    int systemError = 0; // errno or WSAGetLastError, for the log; 0 if none
};

// Winsock takes recv and send's length as an int: each call asks for at most INT_MAX bytes and the next sends the
// rest. Linux uses the same cap so both behave alike.
inline constexpr std::size_t MaxBytesPerCall = static_cast<std::size_t>(std::numeric_limits<int>::max());

[[nodiscard]] constexpr std::size_t BytesPerCall(std::size_t wanted)
{
    return std::min(wanted, MaxBytesPerCall);
}

// WSAStartup(MAKEWORD(2, 2)) and WSACleanup on Windows; nothing on Linux. WSAStartup counts calls, so several can
// coexist (main.cpp's, each socket test's). Every socket must die before the NetworkInit it was born under.
class ENGINE_API NetworkInit
{
public:
    NetworkInit();
    ~NetworkInit();
    NetworkInit(const NetworkInit&) = delete;
    NetworkInit& operator=(const NetworkInit&) = delete;
    NetworkInit(NetworkInit&&) = delete;
    NetworkInit& operator=(NetworkInit&&) = delete;

    // Always on Linux. If not, SystemError() says why.
    [[nodiscard]] bool IsReady() const;
    [[nodiscard]] int SystemError() const;

private:
    int m_systemError = 0;
};

class ENGINE_API UniqueSocket
{
public:
    UniqueSocket() = default;
    explicit UniqueSocket(Handle handle);
    UniqueSocket(UniqueSocket&& other) noexcept;
    UniqueSocket& operator=(UniqueSocket&& other) noexcept;
    UniqueSocket(const UniqueSocket&) = delete;
    UniqueSocket& operator=(const UniqueSocket&) = delete;
    ~UniqueSocket();

    [[nodiscard]] Handle Get() const;
    [[nodiscard]] bool IsOpen() const;
    void Reset();

private:
    Handle m_handle = InvalidHandle;
};

// Or empty, with what failed in `result`.
struct Opened
{
    UniqueSocket socket;
    Result result;
};

// IPv4 and TCP like the original's 0x4e82e0, already non-blocking.
[[nodiscard]] ENGINE_API Opened OpenTcp();

// InProgress almost always; Done or Refused if the system already knows.
[[nodiscard]] Result Connect(Handle socket, std::array<std::uint8_t, 4> ip, std::uint16_t port);

// Without waiting: poll on Linux (select cannot handle a descriptor from FD_SETSIZE up), select with write and
// exception sets on Windows (there a failed connect is not seen as writable). The result comes from SO_ERROR, which
// is cleared when read: the caller keeps the final one.
[[nodiscard]] Result CheckConnect(Handle socket);

// TryLater if nothing, PeerClosed if the other side closed. An empty buffer gives Done with 0 without calling recv:
// recv would give 0 both with data and on a close, and 0 is a close.
[[nodiscard]] ENGINE_API Result Receive(Handle socket, std::span<std::byte> buffer);

// TryLater if the system took nothing. MSG_NOSIGNAL on Linux: a send to a connection the other side closed gives
// EPIPE instead of SIGPIPE, which would kill the process.
[[nodiscard]] ENGINE_API Result Send(Handle socket, std::span<const std::byte> bytes);

// With inet_pton: no DNS query.
[[nodiscard]] std::optional<std::array<std::uint8_t, 4>> ParseIpv4(const std::string& text);

// With getaddrinfo and AF_INET: waits as long as the DNS takes.
[[nodiscard]] std::optional<std::array<std::uint8_t, 4>> LookUpIpv4(const std::string& host);

// Server side, for the tests (the LoopbackServer and a port nobody listens on): the client does not listen. Bound to
// 127.0.0.1 on a port the system picks, no listen, non-blocking. While it lives nobody else takes the port, and a
// connect to it is refused.
[[nodiscard]] ENGINE_API Opened BindOnLoopback();
[[nodiscard]] ENGINE_API Opened ListenOnLoopback();
// 0 if it could not be read.
[[nodiscard]] ENGINE_API std::uint16_t LocalPort(Handle socket);
// Already non-blocking (on Linux accept does not inherit it), or empty with TryLater if there is none.
[[nodiscard]] ENGINE_API Opened Accept(Handle listener);
// SO_LINGER {1, 0} and close: the other side gets a RST instead of a FIN, like the original's 0x4e8380 (the kick).
ENGINE_API void CloseWithReset(UniqueSocket& socket);
} // namespace Client::Network::SocketApi
