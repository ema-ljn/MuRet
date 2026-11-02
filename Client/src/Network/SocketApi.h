#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>

// Las llamadas del sistema para un socket TCP no bloqueante, iguales en Windows (Winsock 2) y en
// Linux (BSD): el único #ifdef _WIN32 de ClientLib está en SocketApi.cpp, y este header no incluye
// ningún header del sistema, así que <winsock2.h> y sus macros no llegan al resto del cliente.
// Contrato: el plan de la CL4 (CL4.1), "Contrato" y la tabla del paso 2, y 03-red-y-cifrado.md,
// "Sockets". Como referencia de estructura, Core/Platform/NonBlockingSocket.h de MuMain.
//
// Por qué los sockets del sistema y no una biblioteca (CODING_RULES.md §10; pregunta 3 del plan,
// confirmada con Context7 el 2026-11-02, con los puertos de la baseline de vcpkg.json):
// - asio standalone (puerto `asio`, 1.38.2): solo headers sin Boost, y io_context::poll() corre
//   los handlers listos sin esperar, así que entraría en un frame. Pero el modelo es otro:
//   operaciones asíncronas con handlers y buffers que tienen que vivir hasta el handler, para una
//   sola conexión que se consulta por frame. Y el roadmap del servidor la deja como comparación
//   (D-3).
// - SDL3_net (puerto `sdl3-net`, 3.2.0, estable): todo no bloqueante, NET_CreateClient conecta en
//   el fondo, NET_ResolveHostname resuelve en sus hilos y NET_ReadFromStreamSocket da 0 sin
//   datos. NET_WriteToStreamSocket encola adentro lo que no sale, sin tope, y
//   NET_GetStreamSocketPendingWrites dice cuánto quedó, así que el tope de 0x2000 del original se
//   armaría encima. No entra porque el puerto depende de sdl3: sumaría SDL3 a ClientLib, que hoy
//   no lo ve (solo ClientPlatform), y Network y sus tests dejarían de correr sin SDL3.
// - los sockets del sistema: estas pocas llamadas, sin dependencias. Lo único que se pierde es
//   resolver un nombre sin esperar, y el original tampoco lo hace (pregunta 5).
namespace Client::Network::SocketApi
{
// SOCKET (UINT_PTR) en Windows, int en POSIX: los dos entran en un uintptr_t. INVALID_SOCKET es
// ~0 en Windows; en POSIX un descriptor nunca llega ahí.
using Handle = std::uintptr_t;
inline constexpr Handle InvalidHandle = ~Handle{0};

// Lo que dio una llamada, ya traducido del errno o del WSAGetLastError.
enum class Status : std::uint8_t
{
    Done,       // salió bien; en Receive y Send, `bytes` dice cuántos
    TryLater,   // EWOULDBLOCK, EAGAIN, WSAEWOULDBLOCK o EINTR: no hay nada todavía
    InProgress, // el connect sigue: EINPROGRESS o EINTR en Linux, WSAEWOULDBLOCK en Windows
    Refused,    // ECONNREFUSED o WSAECONNREFUSED
    PeerClosed, // recv en 0, o ECONNRESET, ECONNABORTED, EPIPE, WSAECONNRESET o WSAECONNABORTED
    Failed,     // otro error: `systemError` lo dice
};

struct Result
{
    Status status = Status::Done;
    std::size_t bytes = 0;
    int systemError = 0; // el errno o el WSAGetLastError, para el log; 0 si no hubo
};

// Winsock recibe el largo de recv y send en un int: cada llamada pide INT_MAX bytes como mucho, y
// la que sigue manda el resto. En Linux se usa el mismo tope, así los dos se portan igual.
inline constexpr std::size_t MaxBytesPerCall = static_cast<std::size_t>(std::numeric_limits<int>::max());

[[nodiscard]] constexpr std::size_t BytesPerCall(std::size_t wanted)
{
    return std::min(wanted, MaxBytesPerCall);
}

// WSAStartup(MAKEWORD(2, 2)) y WSACleanup en Windows; nada en Linux. WSAStartup cuenta las
// llamadas, así que puede haber varios a la vez (main.cpp el suyo, cada test con sockets el suyo).
// Todo socket tiene que vivir menos que el NetworkInit que lo vio nacer.
class NetworkInit
{
public:
    NetworkInit();
    ~NetworkInit();
    NetworkInit(const NetworkInit&) = delete;
    NetworkInit& operator=(const NetworkInit&) = delete;
    NetworkInit(NetworkInit&&) = delete;
    NetworkInit& operator=(NetworkInit&&) = delete;

    // Si WSAStartup salió bien (en Linux, siempre). Si no, SystemError() dice por qué.
    [[nodiscard]] bool IsReady() const;
    [[nodiscard]] int SystemError() const;

private:
    int m_systemError = 0;
};

// RAII sobre un Handle: se mueve, no se copia, y el destructor cierra (close o closesocket).
class UniqueSocket
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
    // Cierra el que tenga y queda vacío.
    void Reset();

private:
    Handle m_handle = InvalidHandle;
};

// Un socket recién abierto, o vacío con lo que falló en `result`.
struct Opened
{
    UniqueSocket socket;
    Result result;
};

// socket(AF_INET, SOCK_STREAM, 0), IPv4 y TCP como el 0x4e82e0 del original, ya no bloqueante.
[[nodiscard]] Opened OpenTcp();

// Empieza a conectar: InProgress casi siempre; Done o Refused si el sistema ya lo sabe.
[[nodiscard]] Result Connect(Handle socket, std::array<std::uint8_t, 4> ip, std::uint16_t port);

// Si el connect terminó, sin esperar: Done, InProgress, Refused o Failed. Con poll en Linux
// (select no puede con un descriptor desde FD_SETSIZE), y con select con escritura y excepciones
// en Windows (ahí un connect que falló no se ve como escribible). El resultado sale de SO_ERROR,
// que se borra al leerlo: quien llama guarda el final.
[[nodiscard]] Result CheckConnect(Handle socket);

// Lo que haya, hasta buffer.size(): Done con los bytes, TryLater si no hay nada, PeerClosed si el
// otro lado cerró. Con un buffer vacío, Done con 0 sin llamar a recv: recv daría 0 si hay datos o
// si el otro lado cerró, y 0 es un cierre.
[[nodiscard]] Result Receive(Handle socket, std::span<std::byte> buffer);

// Done con lo que tomó el sistema, TryLater si no tomó nada. Con MSG_NOSIGNAL en Linux: un send a
// una conexión que el otro lado cerró da EPIPE en vez de la señal SIGPIPE, que mataría el proceso.
[[nodiscard]] Result Send(Handle socket, std::span<const std::byte> bytes);

// La IPv4 de un texto como "127.0.0.1", con inet_pton: no pregunta al DNS.
[[nodiscard]] std::optional<std::array<std::uint8_t, 4>> ParseIpv4(const std::string& text);

// La primera IPv4 de un nombre, con getaddrinfo y AF_INET: espera lo que tarde el DNS.
[[nodiscard]] std::optional<std::array<std::uint8_t, 4>> LookUpIpv4(const std::string& host);

// Del lado del servidor, para el LoopbackServer de los tests: el cliente no escucha.
//
// Un listener en 127.0.0.1 con el puerto que elija el sistema (bind al 0), no bloqueante.
[[nodiscard]] Opened ListenOnLoopback();
// El puerto local de un socket (getsockname), o 0 si no se pudo leer.
[[nodiscard]] std::uint16_t LocalPort(Handle socket);
// Un cliente que espera en el listener, ya no bloqueante (en Linux accept no lo hereda), o vacío
// con TryLater si no hay ninguno.
[[nodiscard]] Opened Accept(Handle listener);
// SO_LINGER {1, 0} y cerrar: el otro lado recibe un RST en vez de un FIN, como en el 0x4e8380 del
// original (el kick).
void CloseWithReset(UniqueSocket& socket);
} // namespace Client::Network::SocketApi
