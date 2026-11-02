#pragma once

#include "Core/Expected.h"
#include "Network/IResolver.h"
#include "Network/ISocket.h"
#include "Network/SocketApi.h"
#include "Network/SocketError.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

// El ISocket con los sockets del sistema (Network/SocketApi.h): IPv4 y TCP, no bloqueante desde
// el principio, y el connect como una máquina de estados que el bucle consulta por frame.
// Contrato: el plan de la CL4 (CL4.1), "Contrato".
//
// Lo que cambia respecto del original: el connect no espera (el de 0x4e8570 sí, con el socket
// todavía bloqueante), 127.0.0.1 se acepta (0x4e8570 lo rechaza), y cerrar es un close normal, sin
// el SO_LINGER {1, 0} de 0x4e8380 que corta con un RST.
//
// Necesita un SocketApi::NetworkInit vivo mientras tenga un socket.
namespace Client::Network
{
class SystemSocket final : public ISocket
{
public:
    // El resolver tiene que vivir más que el socket. Uno temporal no compila.
    explicit SystemSocket(IResolver& resolver);
    SystemSocket(IResolver&&) = delete;

    // Uno movido queda cerrado, sin conexión: el socket del sistema es del otro.
    SystemSocket(SystemSocket&& other) noexcept;
    SystemSocket& operator=(SystemSocket&& other) noexcept;
    SystemSocket(const SystemSocket&) = delete;
    SystemSocket& operator=(const SystemSocket&) = delete;
    ~SystemSocket() override = default;

    [[nodiscard]] Core::Expected<ConnectProgress, SocketError> StartConnect(std::string_view host,
                                                                            std::uint16_t port) override;
    [[nodiscard]] Core::Expected<ConnectProgress, SocketError> PollConnect() override;
    [[nodiscard]] Core::Expected<std::size_t, SocketError> Receive(std::span<std::byte> buffer) override;
    [[nodiscard]] Core::Expected<std::size_t, SocketError> Send(std::span<const std::byte> bytes) override;
    void Close() override;
    [[nodiscard]] bool IsOpen() const override;
    [[nodiscard]] int LastSystemError() const override;

private:
    enum class State : std::uint8_t
    {
        Idle,       // sin StartConnect desde el último Close
        Connecting, // falta que CheckConnect diga cómo terminó
        Connected,
        Failed, // el connect terminó con m_failure
    };

    // Lo que dio CheckConnect o Connect, pasado al estado: el resultado final queda guardado.
    Core::Expected<ConnectProgress, SocketError> Settle(const SocketApi::Result& result);
    Core::Expected<ConnectProgress, SocketError> FailConnect(SocketError error, int systemError);
    // Lo que dio Receive o Send, como bytes o como error.
    Core::Expected<std::size_t, SocketError> Transferred(const SocketApi::Result& result);

    // Nunca nulo: un puntero, para que el socket se pueda mover.
    IResolver* m_resolver;
    SocketApi::UniqueSocket m_socket;
    State m_state = State::Idle;
    SocketError m_failure = SocketError::Closed; // el del connect, con m_state en Failed
    int m_lastSystemError = 0;
};
} // namespace Client::Network
