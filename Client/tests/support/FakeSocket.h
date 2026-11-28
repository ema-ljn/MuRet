#pragma once

#include "Session/ServerAddress.h"
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

// An ISocket that follows a script, without network: how many PollConnect stay in progress and how they end, what
// each Receive brings (one read per entry, as they arrive from the wire) and how much each Send takes, or what it
// fails with. It behaves like SystemSocket where the ISocket contract says so: the final connect result is kept and
// repeated even if the script changes, a ResolveFailed comes out of StartConnect and does not leave the socket open,
// an empty buffer gives 0, and without Connected, Receive and Send give Closed.
namespace Client::Tests
{
// A Send script entry: how much it takes (nullopt: 0), or the error it gives. It is written bare in the list, as
// {std::size_t{2}, std::nullopt, Network::SocketError::Closed}.
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
    // The script, which the test fills before or between calls.
    int pollsUntilConnected = 0;                       // how many PollConnect give InProgress
    std::optional<Network::SocketError> connectResult; // empty: Connected; ResolveFailed, at once
    std::deque<std::vector<std::byte>> reads;          // empty: Receive gives 0
    std::deque<SendStep> sendLimits;                   // one Send per entry; empty: takes everything
    std::optional<Network::SocketError> sendError;     // the next Send fails with this, before sendLimits
    bool peerClosed = false;                           // Receive gives Closed when there are no more reads
    int systemError = 0;                               // what LastSystemError gives
    // Runs at the start of each StartConnect, like the resolver, which goes before everything: a test moves the
    // FakeClock there so that the resolver "takes time".
    std::function<void()> onStartConnect;

    // What happened.
    std::vector<std::byte> sent;                  // everything Send took, in order
    std::vector<Session::ServerAddress> connects; // the host and the port of each StartConnect
    // Send attempts, including those that fail or take no bytes.
    std::size_t sendAttempts = 0;
    int closes = 0; // each Close(), and each StartConnect with one open

    [[nodiscard]] Core::Expected<Network::ConnectProgress, Network::SocketError>
    StartConnect(std::string_view host, std::uint16_t port) override;
    [[nodiscard]] Core::Expected<Network::ConnectProgress, Network::SocketError> PollConnect() override;
    // An entry of `reads` longer than the buffer comes out over several Receive.
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
    bool m_open = false;                           // what IsOpen gives
    std::optional<Network::SocketError> m_failure; // the final result, with m_state at Failed
};
} // namespace Client::Tests
