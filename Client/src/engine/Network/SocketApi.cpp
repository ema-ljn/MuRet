#include "Network/SocketApi.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>

// The only part of ClientLib that looks at _WIN32: these includes and the block at the start of the anonymous
// namespace, with what differs between Winsock and BSD. Below that block the code is the same on both.
#ifdef _WIN32
// Without <windows.h>'s min and max macros, which override std::min, nor what Winsock does not need.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace Client::Network::SocketApi
{
namespace
{
enum class ConnectCheck : std::uint8_t
{
    Pending,  // sigue
    Finished, // finished, well or badly: SO_ERROR says which
    Error,    // could not be checked: LastError() says why
};

#ifdef _WIN32
using NativeSocket = SOCKET;
using AddressLength = int;
using IoLength = int;
using IoBuffer = char*;
using ConstIoBuffer = const char*;
constexpr int SendFlags = 0;

// WSAStartup returns 0 or the error (WSAGetLastError does not work yet).
int StartNetwork()
{
    WSADATA data{};
    return WSAStartup(MAKEWORD(2, 2), &data);
}

void StopNetwork()
{
    WSACleanup();
}

bool IsValid(NativeSocket socket)
{
    return socket != INVALID_SOCKET;
}

int LastError()
{
    return WSAGetLastError();
}

void CloseNative(NativeSocket socket)
{
    closesocket(socket);
}

bool SetNonBlocking(NativeSocket socket)
{
    u_long nonBlocking = 1;
    return ioctlsocket(socket, FIONBIO, &nonBlocking) == 0;
}

bool IsTryLater(int error)
{
    return error == WSAEWOULDBLOCK || error == WSAEINTR;
}

bool IsConnectInProgress(int error)
{
    return error == WSAEWOULDBLOCK;
}

bool IsRefused(int error)
{
    return error == WSAECONNREFUSED;
}

// Winsock has no EPIPE: a send after the RST gives WSAECONNRESET or WSAECONNABORTED.
bool IsPeerClosed(int error)
{
    return error == WSAECONNRESET || error == WSAECONNABORTED;
}

// On Windows a failed connect is not seen as writable but in select's exceptions (WSAPoll did not report it in old
// versions). The fd_sets are built by hand, without FD_SET: with one socket its count and first slot are enough, and
// select leaves the count at 0 if it is not ready.
ConnectCheck CheckConnectWithoutWaiting(NativeSocket socket)
{
    fd_set writable{};
    writable.fd_count = 1;
    writable.fd_array[0] = socket;
    fd_set failed = writable;
    timeval noWait{};
    const int ready = select(0, nullptr, &writable, &failed, &noWait);
    if (ready == SOCKET_ERROR)
    {
        return ConnectCheck::Error;
    }
    return ready > 0 ? ConnectCheck::Finished : ConnectCheck::Pending;
}
#else
using NativeSocket = int;
using AddressLength = socklen_t;
using IoLength = std::size_t;
using IoBuffer = void*;
using ConstIoBuffer = const void*;
// A send to a connection the other side closed gives EPIPE without SIGPIPE, which kills the process by default.
constexpr int SendFlags = MSG_NOSIGNAL;

int StartNetwork()
{
    return 0;
}

void StopNetwork() {}

bool IsValid(NativeSocket socket)
{
    return socket >= 0;
}

int LastError()
{
    return errno;
}

void CloseNative(NativeSocket socket)
{
    close(socket);
}

bool SetNonBlocking(NativeSocket socket)
{
    const int flags = fcntl(socket, F_GETFL, 0);
    return flags != -1 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) != -1;
}

// EAGAIN and EWOULDBLOCK are equal on Linux, but POSIX lets them differ.
bool IsTryLater(int error)
{
    return error == EAGAIN || error == EWOULDBLOCK || error == EINTR;
}

// With EINTR the connect goes on in the background: it is checked like one in progress.
bool IsConnectInProgress(int error)
{
    return error == EINPROGRESS || error == EINTR;
}

bool IsRefused(int error)
{
    return error == ECONNREFUSED;
}

bool IsPeerClosed(int error)
{
    return error == ECONNRESET || error == ECONNABORTED || error == EPIPE;
}

// poll, not select: select cannot handle a descriptor from FD_SETSIZE up. A failed connect gives POLLOUT plus POLLERR
// (and POLLHUP); SO_ERROR says which.
ConnectCheck CheckConnectWithoutWaiting(NativeSocket socket)
{
    pollfd entry{};
    entry.fd = socket;
    entry.events = POLLOUT;
    const int ready = poll(&entry, 1, 0);
    if (ready < 0)
    {
        return IsTryLater(errno) ? ConnectCheck::Pending : ConnectCheck::Error;
    }
    return ready > 0 && entry.revents != 0 ? ConnectCheck::Finished : ConnectCheck::Pending;
}
#endif

NativeSocket ToNative(Handle handle)
{
    return static_cast<NativeSocket>(handle);
}

Handle FromNative(NativeSocket socket)
{
    return IsValid(socket) ? static_cast<Handle>(socket) : InvalidHandle;
}

Result WithStatus(Status status, int error = 0)
{
    Result result;
    result.status = status;
    result.systemError = error;
    return result;
}

Result Failure(int error)
{
    return WithStatus(Status::Failed, error);
}

Result Transferred(std::size_t bytes)
{
    Result result;
    result.bytes = bytes;
    return result;
}

Opened FailedToOpen(Result result)
{
    Opened opened;
    opened.result = result;
    return opened;
}

// A recv or send error that is neither "try later" nor the other side closing is a failure.
Result TransferFailure(int error)
{
    if (IsTryLater(error))
    {
        return WithStatus(Status::TryLater);
    }
    if (IsPeerClosed(error))
    {
        return WithStatus(Status::PeerClosed, error);
    }
    return Failure(error);
}

// 0 if it connected.
Result ConnectOutcome(int error)
{
    if (error == 0)
    {
        return WithStatus(Status::Done);
    }
    if (IsRefused(error))
    {
        return WithStatus(Status::Refused, error);
    }
    return Failure(error);
}

sockaddr_in AddressOf(const std::array<std::uint8_t, 4>& ip, std::uint16_t port)
{
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    // The four bytes are already in network order: 127.0.0.1 is 7F 00 00 01.
    std::memcpy(&address.sin_addr, ip.data(), ip.size());
    return address;
}

std::array<std::uint8_t, 4> IpOf(const in_addr& address)
{
    std::array<std::uint8_t, 4> ip{};
    std::memcpy(ip.data(), &address, ip.size());
    return ip;
}

struct AddrInfoDeleter
{
    void operator()(addrinfo* list) const
    {
        freeaddrinfo(list);
    }
};
} // namespace

NetworkInit::NetworkInit() : m_systemError(StartNetwork()) {}

NetworkInit::~NetworkInit()
{
    if (IsReady())
    {
        StopNetwork();
    }
}

bool NetworkInit::IsReady() const
{
    return m_systemError == 0;
}

int NetworkInit::SystemError() const
{
    return m_systemError;
}

UniqueSocket::UniqueSocket(Handle handle) : m_handle(handle) {}

UniqueSocket::UniqueSocket(UniqueSocket&& other) noexcept : m_handle(std::exchange(other.m_handle, InvalidHandle)) {}

UniqueSocket& UniqueSocket::operator=(UniqueSocket&& other) noexcept
{
    if (this != &other)
    {
        Reset();
        m_handle = std::exchange(other.m_handle, InvalidHandle);
    }
    return *this;
}

UniqueSocket::~UniqueSocket()
{
    Reset();
}

Handle UniqueSocket::Get() const
{
    return m_handle;
}

bool UniqueSocket::IsOpen() const
{
    return m_handle != InvalidHandle;
}

void UniqueSocket::Reset()
{
    if (IsOpen())
    {
        CloseNative(ToNative(std::exchange(m_handle, InvalidHandle)));
    }
}

// If it cannot be made non-blocking, `opened`'s destructor closes it, with the error already read.
Opened OpenTcp()
{
    const NativeSocket native = socket(AF_INET, SOCK_STREAM, 0);
    UniqueSocket opened(FromNative(native));
    if (!opened.IsOpen())
    {
        return FailedToOpen(Failure(LastError()));
    }
    if (!SetNonBlocking(native))
    {
        return FailedToOpen(Failure(LastError()));
    }
    return Opened{std::move(opened), Result{}};
}

Result Connect(Handle socket, std::array<std::uint8_t, 4> ip, std::uint16_t port)
{
    const sockaddr_in address = AddressOf(ip, port);
    // sockaddr_in is passed as the generic sockaddr: the sockets API asks for it.
    const auto length = static_cast<AddressLength>(sizeof(address));
    if (connect(ToNative(socket), reinterpret_cast<const sockaddr*>(&address), length) == 0)
    {
        return WithStatus(Status::Done);
    }
    const int error = LastError();
    if (IsConnectInProgress(error))
    {
        return WithStatus(Status::InProgress);
    }
    return ConnectOutcome(error);
}

Result CheckConnect(Handle socket)
{
    const ConnectCheck check = CheckConnectWithoutWaiting(ToNative(socket));
    if (check == ConnectCheck::Error)
    {
        return Failure(LastError());
    }
    if (check == ConnectCheck::Pending)
    {
        return WithStatus(Status::InProgress);
    }
    int error = 0;
    auto length = static_cast<AddressLength>(sizeof(error));
    if (getsockopt(ToNative(socket), SOL_SOCKET, SO_ERROR, reinterpret_cast<IoBuffer>(&error), &length) != 0)
    {
        return Failure(LastError());
    }
    return ConnectOutcome(error);
}

Result Receive(Handle socket, std::span<std::byte> buffer)
{
    if (buffer.empty())
    {
        return Transferred(0);
    }
    const auto length = static_cast<IoLength>(BytesPerCall(buffer.size()));
    const auto received = recv(ToNative(socket), reinterpret_cast<IoBuffer>(buffer.data()), length, 0);
    if (received < 0)
    {
        return TransferFailure(LastError());
    }
    if (received == 0)
    {
        return WithStatus(Status::PeerClosed);
    }
    return Transferred(static_cast<std::size_t>(received));
}

Result Send(Handle socket, std::span<const std::byte> bytes)
{
    if (bytes.empty())
    {
        return Transferred(0);
    }
    const auto length = static_cast<IoLength>(BytesPerCall(bytes.size()));
    const auto sent = send(ToNative(socket), reinterpret_cast<ConstIoBuffer>(bytes.data()), length, SendFlags);
    if (sent < 0)
    {
        return TransferFailure(LastError());
    }
    return Transferred(static_cast<std::size_t>(sent));
}

std::optional<std::array<std::uint8_t, 4>> ParseIpv4(const std::string& text)
{
    in_addr address{};
    if (inet_pton(AF_INET, text.c_str(), &address) != 1)
    {
        return std::nullopt;
    }
    return IpOf(address);
}

std::optional<std::array<std::uint8_t, 4>> LookUpIpv4(const std::string& host)
{
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &found) != 0 || found == nullptr)
    {
        return std::nullopt;
    }
    const std::unique_ptr<addrinfo, AddrInfoDeleter> list(found);
    for (const addrinfo* entry = list.get(); entry != nullptr; entry = entry->ai_next)
    {
        if (entry->ai_family == AF_INET && entry->ai_addr != nullptr && entry->ai_addrlen >= sizeof(sockaddr_in))
        {
            sockaddr_in address{};
            std::memcpy(&address, entry->ai_addr, sizeof(address));
            return IpOf(address.sin_addr);
        }
    }
    return std::nullopt;
}

Opened BindOnLoopback()
{
    constexpr std::uint16_t AnyPort = 0;
    Opened opened = OpenTcp();
    if (!opened.socket.IsOpen())
    {
        return opened;
    }
    const sockaddr_in address = AddressOf({127, 0, 0, 1}, AnyPort);
    const auto length = static_cast<AddressLength>(sizeof(address));
    if (bind(ToNative(opened.socket.Get()), reinterpret_cast<const sockaddr*>(&address), length) != 0)
    {
        return FailedToOpen(Failure(LastError()));
    }
    return opened;
}

Opened ListenOnLoopback()
{
    constexpr int Backlog = 4;
    Opened opened = BindOnLoopback();
    if (!opened.socket.IsOpen())
    {
        return opened;
    }
    if (listen(ToNative(opened.socket.Get()), Backlog) != 0)
    {
        return FailedToOpen(Failure(LastError()));
    }
    return opened;
}

std::uint16_t LocalPort(Handle socket)
{
    sockaddr_in address{};
    auto length = static_cast<AddressLength>(sizeof(address));
    if (getsockname(ToNative(socket), reinterpret_cast<sockaddr*>(&address), &length) != 0)
    {
        return 0;
    }
    return ntohs(address.sin_port);
}

Opened Accept(Handle listener)
{
    const NativeSocket native = accept(ToNative(listener), nullptr, nullptr);
    UniqueSocket accepted(FromNative(native));
    if (!accepted.IsOpen())
    {
        const int error = LastError();
        return FailedToOpen(IsTryLater(error) ? WithStatus(Status::TryLater) : Failure(error));
    }
    if (!SetNonBlocking(native))
    {
        return FailedToOpen(Failure(LastError()));
    }
    return Opened{std::move(accepted), Result{}};
}

void CloseWithReset(UniqueSocket& socket)
{
    if (!socket.IsOpen())
    {
        return;
    }
    linger abortive{};
    abortive.l_onoff = 1;
    abortive.l_linger = 0;
    // If it failed, the close would be a FIN: the test that asks for it sees it.
    static_cast<void>(setsockopt(ToNative(socket.Get()), SOL_SOCKET, SO_LINGER,
                                 reinterpret_cast<ConstIoBuffer>(&abortive),
                                 static_cast<AddressLength>(sizeof(abortive))));
    socket.Reset();
}
} // namespace Client::Network::SocketApi
