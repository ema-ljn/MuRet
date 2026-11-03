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

// Lo único de ClientLib que mira _WIN32: estos includes y el bloque del principio del namespace
// anónimo, con lo que cambia entre Winsock y BSD (iniciar la red, cerrar, el no bloqueante, el
// último error y su traducción, y cómo se mira si terminó el connect). Lo de abajo de ese bloque es
// igual en los dos, con los tipos de los largos y los buffers que el bloque nombra.
#ifdef _WIN32
// Sin las macros min y max de <windows.h>, que pisan std::min, ni lo que Winsock no necesita.
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
// Cómo está un connect en curso, mirado sin esperar.
enum class ConnectCheck : std::uint8_t
{
    Pending,  // sigue
    Finished, // terminó, bien o mal: SO_ERROR dice cuál
    Error,    // no se pudo mirar: LastError() dice por qué
};

#ifdef _WIN32
using NativeSocket = SOCKET;
using AddressLength = int;
// recv y send: el largo en un int, y el buffer como char*.
using IoLength = int;
using IoBuffer = char*;
using ConstIoBuffer = const char*;
constexpr int SendFlags = 0;

// Lo que devuelve WSAStartup: 0, o el error (WSAGetLastError no sirve todavía).
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

// Winsock no tiene EPIPE: un send después del RST da WSAECONNRESET o WSAECONNABORTED.
bool IsPeerClosed(int error)
{
    return error == WSAECONNRESET || error == WSAECONNABORTED;
}

// En Windows un connect que falló no se ve como escribible, sino en las excepciones de select
// (WSAPoll no lo avisaba en versiones viejas). Los fd_set se arman a mano, sin las macros FD_SET:
// con un solo socket alcanza con su cuenta y su primer lugar, y select deja la cuenta en 0 si no
// está listo.
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
// Un send a una conexión que el otro lado cerró da EPIPE sin la señal SIGPIPE, que por defecto
// mata el proceso.
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

// EAGAIN y EWOULDBLOCK valen lo mismo en Linux, pero POSIX deja que sean distintos.
bool IsTryLater(int error)
{
    return error == EAGAIN || error == EWOULDBLOCK || error == EINTR;
}

// Con EINTR, el connect sigue en el fondo: se mira igual que uno en curso.
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

// poll y no select: select no puede con un descriptor desde FD_SETSIZE. Un connect que falló da
// POLLOUT y además POLLERR (y POLLHUP); SO_ERROR dice cuál fue.
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

// Lo que dio recv o send con -1: un error del sistema que no es "probá más tarde" ni un cierre del
// otro lado es una falla.
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

// El resultado de un connect que terminó o que falló enseguida, con su error (0 si conectó).
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
    // Los cuatro bytes ya van en el orden de la red: 127.0.0.1 es 7F 00 00 01.
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

// Si no se puede poner no bloqueante, lo cierra el destructor de `opened`, ya leído el error.
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
    // sockaddr_in se pasa como el sockaddr genérico: así lo pide la API de sockets.
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
    // Si no se pudiera, el cierre sería un FIN: el test que lo pide lo ve.
    static_cast<void>(setsockopt(ToNative(socket.Get()), SOL_SOCKET, SO_LINGER,
                                 reinterpret_cast<ConstIoBuffer>(&abortive),
                                 static_cast<AddressLength>(sizeof(abortive))));
    socket.Reset();
}
} // namespace Client::Network::SocketApi
