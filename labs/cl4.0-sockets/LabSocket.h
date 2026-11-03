#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

// Misión de la CL4.0 (docs/learning/ola-cl4.0-sockets-no-bloqueantes.md, actividad 3): un socket
// TCP de IPv4 con RAII, que se mueve y no se copia, y las llamadas del sistema que hacen falta para
// conectarse en 127.0.0.1 sin bloquear. Contrato: Client/docs/research/03-red-y-cifrado.md,
// "Sockets", y la sub-ola CL4.0 del plan de la CL4.
//
// Los #ifdef _WIN32 del lab están todos en LabSocket.cpp: Winsock 2 en Windows, los sockets BSD
// en Linux. Lo de afuera ve un handle entero y un IoStatus, iguales en los dos.
namespace Labs
{
// SOCKET (un UINT_PTR) en Windows, int en POSIX: los dos entran en un std::uintptr_t.
using SocketHandle = std::uintptr_t;
// INVALID_SOCKET en Windows (~0) y -1 en POSIX, que pasado a std::uintptr_t es el mismo ~0.
inline constexpr SocketHandle InvalidSocket = ~SocketHandle{0};

// Cómo terminó una llamada, igual en Windows y en Linux.
enum class IoStatus : std::uint8_t
{
    Done,       // hizo lo pedido (en Send y Receive, `bytes` dice cuántos)
    TryLater,   // "probá más tarde": EWOULDBLOCK, EAGAIN, WSAEWOULDBLOCK o EINTR
    InProgress, // el connect sigue: EINPROGRESS en Linux, WSAEWOULDBLOCK en Windows
    Refused,    // ECONNREFUSED o WSAECONNREFUSED: no hay nadie escuchando en ese puerto
    PeerClosed, // el otro lado cerró: recv en 0, o ECONNRESET, ECONNABORTED, EPIPE o sus WSA*
    Failed,     // otro error del sistema: está en systemError
};

struct IoResult
{
    IoStatus status = IoStatus::Done;
    std::size_t bytes = 0;
    // El errno o el WSAGetLastError de la llamada; 0 si no hubo error (también con recv en 0).
    int systemError = 0;
};

// WSAStartup(MAKEWORD(2, 2)) al construir y WSACleanup al destruir, en Windows; nada en Linux.
// Winsock cuenta las llamadas, así que cada test puede tener la suya.
class NetworkInit
{
public:
    NetworkInit();
    ~NetworkInit();
    NetworkInit(const NetworkInit&) = delete;
    NetworkInit& operator=(const NetworkInit&) = delete;
    NetworkInit(NetworkInit&&) = delete;
    NetworkInit& operator=(NetworkInit&&) = delete;

    [[nodiscard]] bool Ok() const;

private:
    bool m_ok = false;
};

class LabSocket
{
public:
    // Vacío: no tiene descriptor.
    LabSocket() = default;
    // Cierra el descriptor, si tiene.
    ~LabSocket();
    // El movido queda vacío, así que su destructor no cierra el descriptor que se llevó el otro.
    LabSocket(LabSocket&& other) noexcept;
    // Cierra el que tenía antes de tomar el del otro.
    LabSocket& operator=(LabSocket&& other) noexcept;
    // Dos dueños cerrarían dos veces el mismo descriptor, y el segundo close podría cerrar otro
    // que el sistema ya repartió con el mismo número.
    LabSocket(const LabSocket&) = delete;
    LabSocket& operator=(const LabSocket&) = delete;

    // socket(AF_INET, SOCK_STREAM, 0): IPv4 y TCP, bloqueante como todos al nacer. Vacío si el
    // sistema no da uno.
    [[nodiscard]] static LabSocket OpenTcp();

    [[nodiscard]] bool IsOpen() const;
    // Cierra y queda vacío. Con uno vacío no hace nada.
    void Close();
    // O_NONBLOCK con fcntl, o FIONBIO con ioctlsocket. false si no se pudo, o si está vacío.
    [[nodiscard]] bool SetNonBlocking();

    // bind a 127.0.0.1 y `port`; con 0, el sistema elige uno libre.
    [[nodiscard]] bool BindLoopback(std::uint16_t port);
    // El puerto que tiene (getsockname); 0 si no tiene, o si está vacío.
    [[nodiscard]] std::uint16_t LocalPort() const;
    [[nodiscard]] bool Listen();
    // Un cliente que ya llegó, o vacío si no hay ninguno (en uno no bloqueante, "probá más tarde")
    // o si falló. El aceptado nace bloqueante en Linux (accept no hereda O_NONBLOCK).
    [[nodiscard]] LabSocket Accept();

    // connect a 127.0.0.1 y `port`. En uno no bloqueante, InProgress casi siempre; Done o Refused
    // si el sistema ya lo sabe.
    [[nodiscard]] IoResult ConnectLoopback(std::uint16_t port);
    // Si el connect terminó, sin esperar: poll con POLLOUT en Linux, select con escritura y
    // excepciones en Windows, y SO_ERROR para saber cómo. InProgress si todavía no; Done, Refused
    // o Failed si sí. No guarda el resultado, y SO_ERROR se borra al leerlo: en Linux, preguntar de
    // nuevo después de un Refused da Done (el README del lab lo muestra).
    [[nodiscard]] IoResult CheckConnect();
    // Lo que el sistema tome de `bytes`, de 0 a bytes.size(). En Linux con MSG_NOSIGNAL, para que
    // mandar a uno que cerró no mate el proceso con SIGPIPE. Con `bytes` vacío no llama a send.
    [[nodiscard]] IoResult Send(std::span<const std::byte> bytes);
    // Lo que haya, hasta buffer.size(). Con un buffer vacío da 0 sin llamar a recv: con datos
    // esperando, recv daría 0, que se lee como un cierre.
    [[nodiscard]] IoResult Receive(std::span<std::byte> buffer);

private:
    explicit LabSocket(SocketHandle handle);

    SocketHandle m_handle = InvalidSocket;
};
} // namespace Labs
