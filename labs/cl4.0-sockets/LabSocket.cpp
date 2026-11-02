#include "LabSocket.h"

#include <algorithm>
#include <climits>
#include <utility>

// Los #ifdef _WIN32 del lab están todos en este archivo. Arriba, lo que cambia entre Winsock 2 y
// los sockets BSD: el tipo del descriptor, el largo de una dirección, cómo se lee el último error y
// qué código es cada caso. Abajo, las llamadas, que son las mismas con esos nombres, salvo dónde
// se mira si el connect terminó y cómo se pone no bloqueante.
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace Labs
{
namespace
{
// 127.0.0.1 en el orden del host; htonl lo pasa al de la red. INADDR_LOOPBACK es lo mismo, pero en
// glibc trae un cast de C que -Wold-style-cast marca.
constexpr std::uint32_t LoopbackAddress = 0x7F000001;
// Cuántas conexiones esperan al accept: alcanza con una, el lab tiene un cliente.
constexpr int ListenBacklog = 1;

#ifdef _WIN32
using NativeSocket = SOCKET;
using AddressLength = int;
// send y recv de Winsock toman el largo como int: se manda de a INT_MAX como mucho.
using IoLength = int;
constexpr int SendFlags = 0;

int LastError()
{
    return WSAGetLastError();
}

int CloseNative(NativeSocket native)
{
    return closesocket(native);
}

bool IsTryLater(int error)
{
    return error == WSAEWOULDBLOCK || error == WSAEINTR;
}

// Un connect no bloqueante de Winsock vuelve con WSAEWOULDBLOCK, no con WSAEINPROGRESS.
bool IsConnectInProgress(int error)
{
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS || error == WSAEINTR;
}

bool IsRefused(int error)
{
    return error == WSAECONNREFUSED;
}

// Winsock no tiene EPIPE: mandar a uno que cerró da WSAECONNRESET o WSAECONNABORTED.
bool IsPeerClosed(int error)
{
    return error == WSAECONNRESET || error == WSAECONNABORTED;
}
#else
using NativeSocket = int;
using AddressLength = socklen_t;
using IoLength = std::size_t;
// Sin MSG_NOSIGNAL, un send a un socket que el otro lado cerró manda SIGPIPE, que por defecto
// termina el proceso.
constexpr int SendFlags = MSG_NOSIGNAL;

int LastError()
{
    return errno;
}

int CloseNative(NativeSocket native)
{
    return close(native);
}

bool IsTryLater(int error)
{
    return error == EWOULDBLOCK || error == EAGAIN || error == EINTR;
}

// Con EINTR el connect sigue en el fondo, como con EINPROGRESS (POSIX, connect).
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
#endif

NativeSocket Native(SocketHandle handle)
{
    return static_cast<NativeSocket>(handle);
}

// 127.0.0.1 y `port`, en el orden de la red.
sockaddr_in LoopbackEndpoint(std::uint16_t port)
{
    sockaddr_in endpoint{};
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons(port);
    endpoint.sin_addr.s_addr = htonl(LoopbackAddress);
    return endpoint;
}

// Lo que no es "probá más tarde", ni un cierre del otro lado, es un error.
IoResult ErrorResult(int error)
{
    if (IsTryLater(error))
    {
        return {IoStatus::TryLater, 0, error};
    }
    if (IsPeerClosed(error))
    {
        return {IoStatus::PeerClosed, 0, error};
    }
    return {IoStatus::Failed, 0, error};
}

// El resultado de un connect que terminó, en SO_ERROR. Leerlo lo borra.
IoResult ConnectErrorResult(NativeSocket native)
{
    int error = 0;
    auto length = static_cast<AddressLength>(sizeof(error));
    if (getsockopt(native, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length) != 0)
    {
        return {IoStatus::Failed, 0, LastError()};
    }
    if (error == 0)
    {
        return {IoStatus::Done, 0, 0};
    }
    return {IsRefused(error) ? IoStatus::Refused : IoStatus::Failed, 0, error};
}

// Si el connect terminó, sin esperar (tiempo 0).
#ifdef _WIN32
// select con escritura y excepciones: en Windows, un connect que falló no se ve escribible sino en
// las excepciones. Los fd_set se arman a mano, sin FD_SET, que es una macro con un do-while(0).
bool ConnectFinished(NativeSocket native, int& error)
{
    fd_set writable{};
    writable.fd_count = 1;
    writable.fd_array[0] = native;
    fd_set failed = writable;
    timeval noWait{};
    const int ready = select(0, nullptr, &writable, &failed, &noWait);
    error = ready == SOCKET_ERROR ? LastError() : 0;
    return ready > 0;
}
#else
// poll con POLLOUT: un connect que terminó, bien o mal, deja el socket escribible (o con POLLERR o
// POLLHUP, que poll da siempre). select no sirve con un descriptor desde FD_SETSIZE (1024).
bool ConnectFinished(NativeSocket native, int& error)
{
    pollfd entry{};
    entry.fd = native;
    entry.events = POLLOUT;
    const int ready = poll(&entry, 1, 0);
    error = ready < 0 ? LastError() : 0;
    return ready > 0;
}
#endif

// El largo de un send o un recv: en Winsock, un int, así que de a INT_MAX como mucho.
IoLength LengthFor(std::size_t size)
{
    return static_cast<IoLength>(std::min<std::size_t>(size, static_cast<std::size_t>(INT_MAX)));
}
} // namespace

NetworkInit::NetworkInit()
{
#ifdef _WIN32
    WSADATA data{};
    m_ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
    m_ok = true;
#endif
}

NetworkInit::~NetworkInit()
{
#ifdef _WIN32
    if (m_ok)
    {
        WSACleanup();
    }
#endif
}

bool NetworkInit::Ok() const
{
    return m_ok;
}

LabSocket::LabSocket(SocketHandle handle) : m_handle(handle) {}

LabSocket::~LabSocket()
{
    Close();
}

LabSocket::LabSocket(LabSocket&& other) noexcept : m_handle(std::exchange(other.m_handle, InvalidSocket)) {}

LabSocket& LabSocket::operator=(LabSocket&& other) noexcept
{
    if (this != &other)
    {
        Close();
        m_handle = std::exchange(other.m_handle, InvalidSocket);
    }
    return *this;
}

LabSocket LabSocket::OpenTcp()
{
    const NativeSocket opened = socket(AF_INET, SOCK_STREAM, 0);
    // -1 en POSIX e INVALID_SOCKET en Windows: los dos dan InvalidSocket como SocketHandle.
    return LabSocket{static_cast<SocketHandle>(opened)};
}

bool LabSocket::IsOpen() const
{
    return m_handle != InvalidSocket;
}

void LabSocket::Close()
{
    if (!IsOpen())
    {
        return;
    }
    CloseNative(Native(m_handle));
    m_handle = InvalidSocket;
}

bool LabSocket::SetNonBlocking()
{
    if (!IsOpen())
    {
        return false;
    }
#ifdef _WIN32
    // FIONBIO es un unsigned long que no entra en el long de ioctlsocket: el cast lo deja a la
    // vista.
    u_long nonBlocking = 1;
    return ioctlsocket(Native(m_handle), static_cast<long>(FIONBIO), &nonBlocking) == 0;
#else
    const int flags = fcntl(Native(m_handle), F_GETFL, 0);
    return flags != -1 && fcntl(Native(m_handle), F_SETFL, flags | O_NONBLOCK) != -1;
#endif
}

bool LabSocket::BindLoopback(std::uint16_t port)
{
    if (!IsOpen())
    {
        return false;
    }
    const sockaddr_in endpoint = LoopbackEndpoint(port);
    return bind(Native(m_handle), reinterpret_cast<const sockaddr*>(&endpoint),
                static_cast<AddressLength>(sizeof(endpoint))) == 0;
}

std::uint16_t LabSocket::LocalPort() const
{
    if (!IsOpen())
    {
        return 0;
    }
    sockaddr_in endpoint{};
    auto length = static_cast<AddressLength>(sizeof(endpoint));
    if (getsockname(Native(m_handle), reinterpret_cast<sockaddr*>(&endpoint), &length) != 0)
    {
        return 0;
    }
    return ntohs(endpoint.sin_port);
}

bool LabSocket::Listen()
{
    return IsOpen() && listen(Native(m_handle), ListenBacklog) == 0;
}

LabSocket LabSocket::Accept()
{
    if (!IsOpen())
    {
        return {};
    }
    const NativeSocket accepted = accept(Native(m_handle), nullptr, nullptr);
    return LabSocket{static_cast<SocketHandle>(accepted)};
}

IoResult LabSocket::ConnectLoopback(std::uint16_t port)
{
    if (!IsOpen())
    {
        return {IoStatus::Failed, 0, 0};
    }
    const sockaddr_in endpoint = LoopbackEndpoint(port);
    if (connect(Native(m_handle), reinterpret_cast<const sockaddr*>(&endpoint),
                static_cast<AddressLength>(sizeof(endpoint))) == 0)
    {
        return {IoStatus::Done, 0, 0};
    }
    const int error = LastError();
    if (IsConnectInProgress(error))
    {
        return {IoStatus::InProgress, 0, error};
    }
    return {IsRefused(error) ? IoStatus::Refused : IoStatus::Failed, 0, error};
}

IoResult LabSocket::CheckConnect()
{
    if (!IsOpen())
    {
        return {IoStatus::Failed, 0, 0};
    }
    int error = 0;
    if (!ConnectFinished(Native(m_handle), error))
    {
        // Sin error, o con un EINTR del poll, el connect sigue.
        if (error == 0 || IsTryLater(error))
        {
            return {IoStatus::InProgress, 0, error};
        }
        return {IoStatus::Failed, 0, error};
    }
    return ConnectErrorResult(Native(m_handle));
}

IoResult LabSocket::Send(std::span<const std::byte> bytes)
{
    if (!IsOpen())
    {
        return {IoStatus::Failed, 0, 0};
    }
    if (bytes.empty())
    {
        return {IoStatus::Done, 0, 0};
    }
    const auto sent =
        send(Native(m_handle), reinterpret_cast<const char*>(bytes.data()), LengthFor(bytes.size()), SendFlags);
    if (sent < 0)
    {
        return ErrorResult(LastError());
    }
    return {IoStatus::Done, static_cast<std::size_t>(sent), 0};
}

IoResult LabSocket::Receive(std::span<std::byte> buffer)
{
    if (!IsOpen())
    {
        return {IoStatus::Failed, 0, 0};
    }
    if (buffer.empty())
    {
        return {IoStatus::Done, 0, 0};
    }
    const auto received = recv(Native(m_handle), reinterpret_cast<char*>(buffer.data()), LengthFor(buffer.size()), 0);
    if (received == 0)
    {
        // El otro lado mandó su FIN: no hay error, y no va a llegar nada más.
        return {IoStatus::PeerClosed, 0, 0};
    }
    if (received < 0)
    {
        return ErrorResult(LastError());
    }
    return {IoStatus::Done, static_cast<std::size_t>(received), 0};
}
} // namespace Labs
