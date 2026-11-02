#pragma once

#include "App/ServerAddress.h"
#include "Core/Expected.h"
#include "Network/ISocket.h"
#include "Network/SocketError.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// Un ISocket que sigue un guion, sin red: cuántos PollConnect quedan en curso y cómo terminan, qué
// trae cada Receive (una lectura por entrada, como llegan del cable) y cuánto toma cada Send, o
// con qué falla.
// Se porta como SystemSocket donde el contrato de ISocket lo dice: el resultado final del connect
// queda guardado y se repite aunque el guion cambie, un ResolveFailed sale del StartConnect y no
// deja el socket abierto, un buffer vacío da 0, y sin Connected, Receive y Send dan Closed.
namespace Client::Tests
{
// Una entrada del guion de Send: cuánto toma (nullopt: 0), o el error que da. Se escribe suelta en
// la lista, como {std::size_t{2}, std::nullopt, Network::SocketError::Closed}.
struct SendStep
{
    SendStep(std::size_t limit) : taken(limit) {}
    SendStep(std::nullopt_t) {}
    SendStep(Network::SocketError failure) : error(failure) {}

    std::size_t taken = 0;
    std::optional<Network::SocketError> error;
};

class FakeSocket final : public Network::ISocket
{
public:
    // El guion, que el test llena antes o entre llamadas.
    int pollsUntilConnected = 0;                       // cuántos PollConnect dan InProgress
    std::optional<Network::SocketError> connectResult; // vacío: Connected; ResolveFailed, enseguida
    std::deque<std::vector<std::byte>> reads;          // vacío: Receive da 0
    std::deque<SendStep> sendLimits;                   // un Send por entrada; vacío: toma todo
    std::optional<Network::SocketError> sendError;     // el próximo Send falla con esto
    bool peerClosed = false;                           // Receive da Closed cuando no hay más reads
    int systemError = 0;                               // lo que da LastSystemError
    // Corre al principio de cada StartConnect, como el resolver, que va antes que todo: un test
    // mueve ahí el FakeClock para que el resolver "tarde".
    std::function<void()> onStartConnect;

    // Lo que pasó.
    std::vector<std::byte> sent;              // todo lo que tomó Send, en orden
    std::vector<App::ServerAddress> connects; // el host y el puerto de cada StartConnect
    int closes = 0;                           // cada Close(), y cada StartConnect con uno abierto

    [[nodiscard]] Core::Expected<Network::ConnectProgress, Network::SocketError>
    StartConnect(std::string_view host, std::uint16_t port) override;
    [[nodiscard]] Core::Expected<Network::ConnectProgress, Network::SocketError> PollConnect() override;
    // Una entrada de `reads` más larga que el buffer sale en varios Receive.
    [[nodiscard]] Core::Expected<std::size_t, Network::SocketError> Receive(std::span<std::byte> buffer) override;
    [[nodiscard]] Core::Expected<std::size_t, Network::SocketError> Send(std::span<const std::byte> bytes) override;
    void Close() override;
    [[nodiscard]] bool IsOpen() const override;
    [[nodiscard]] int LastSystemError() const override;

private:
    enum class State : std::uint8_t
    {
        Idle,
        Connecting,
        Connected,
        Failed,
    };

    Core::Expected<Network::ConnectProgress, Network::SocketError> Finish();

    State m_state = State::Idle;
    int m_pollsLeft = 0;
    bool m_open = false;                           // lo que da IsOpen
    std::optional<Network::SocketError> m_failure; // el resultado final, con m_state en Failed
};
} // namespace Client::Tests
