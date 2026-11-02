#pragma once

#include "Core/Expected.h"
#include "Network/SocketError.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

// Un socket TCP de cliente que nunca espera, salvo al resolver el nombre (pregunta 5 del plan de
// la CL4): el bucle lo consulta una vez por frame, en su hilo (D-C5). Contrato: el plan de la CL4
// (CL4.1), "Contrato", y 03-red-y-cifrado.md, "Sockets".
//
// Hay dos: SystemSocket, con los sockets del sistema (Network/SocketApi.h), y el FakeSocket de los
// tests, que sigue un guion. Se puede volver a conectar después de Close().
namespace Client::Network
{
enum class ConnectProgress : std::uint8_t
{
    InProgress,
    Connected,
};

class ISocket
{
public:
    virtual ~ISocket() = default;

    // Resuelve, crea el socket, lo pone no bloqueante y empieza a conectar; antes cierra el que
    // hubiera. InProgress casi siempre; Connected o Refused si el sistema ya lo sabe.
    [[nodiscard]] virtual Core::Expected<ConnectProgress, SocketError> StartConnect(std::string_view host,
                                                                                    std::uint16_t port) = 0;

    // Si el connect terminó (SocketApi::CheckConnect). SO_ERROR se borra al leerlo, así que el
    // resultado final se guarda: después de Connected o de un error, da lo mismo sin volver a mirar
    // el socket. Sin un StartConnect desde el último Close, Closed.
    [[nodiscard]] virtual Core::Expected<ConnectProgress, SocketError> PollConnect() = 0;

    // Lo que haya, hasta buffer.size(); 0 si no llegó nada. Con un buffer vacío da 0 sin llamar a
    // recv, que daría 0 y se leería como un cierre. Antes de Connected, Closed.
    [[nodiscard]] virtual Core::Expected<std::size_t, SocketError> Receive(std::span<std::byte> buffer) = 0;

    // Cuánto tomó el sistema, de 0 a bytes.size(). Antes de Connected, Closed.
    [[nodiscard]] virtual Core::Expected<std::size_t, SocketError> Send(std::span<const std::byte> bytes) = 0;

    virtual void Close() = 0;

    // Si hay un socket del sistema: desde StartConnect (salvo que no haya resuelto) hasta Close.
    [[nodiscard]] virtual bool IsOpen() const = 0;

    // El errno o el WSAGetLastError del último Failed, para el log.
    [[nodiscard]] virtual int LastSystemError() const = 0;

protected:
    // Como IObfuscation: se copia y se mueve solo desde una clase hija (el slicing).
    ISocket() = default;
    ISocket(const ISocket&) = default;
    ISocket& operator=(const ISocket&) = default;
    ISocket(ISocket&&) = default;
    ISocket& operator=(ISocket&&) = default;
};
} // namespace Client::Network
