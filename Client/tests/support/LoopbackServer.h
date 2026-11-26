#pragma once

#include "Network/SocketApi.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

// A real listener on 127.0.0.1, on the port the system picks, non-blocking, polled on the client's thread: no threads
// and no outside network, so it runs in CI on both systems (question 4 of CL4's plan). It uses Network/SocketApi.h's
// calls, where the only #ifdef is. Needs a live SocketApi::NetworkInit, like every socket.
// It serves one client at a time: Accept takes the next one and drops the previous one.
namespace Client::Tests
{
// How long a test with real sockets waits for what the system does in the background: a connect, an accept, some
// bytes, a close. On Linux, on loopback, almost everything arrives in the same call; on Windows a refusal takes about
// 2 s, because of the retries (the plan, "Lo que más puede morder").
inline constexpr std::chrono::seconds LoopbackTimeout{5};

// Calls `attempt` until it gives true or LoopbackTimeout real seconds pass, without sleeping between tries (it yields
// the thread). true if it got there.
[[nodiscard]] bool RetryUntil(const std::function<bool()>& attempt);

class LoopbackServer
{
public:
    // If it cannot listen, the test fails (ADD_FAILURE) and Port() gives 0.
    LoopbackServer();

    [[nodiscard]] std::uint16_t Port() const;
    // true if a client was waiting, which becomes the one of Read, Write and the closes.
    [[nodiscard]] bool Accept();
    // Whatever arrived from the client, without waiting; empty if nothing arrived or if it closed.
    [[nodiscard]] std::vector<std::byte> Read();
    // Whether a Read saw the client close (a FIN or an RST). Accept sets it back to false.
    [[nodiscard]] bool ClientClosed() const;
    // Sends it whole, or the test fails: for a test's few bytes, the system takes them all.
    void Write(std::span<const std::byte> bytes);
    // close: the client gets a FIN, and its recv gives 0.
    void CloseClient();
    // SO_LINGER {1, 0} and close: an RST, like 0x4e8380's kick; the client's recv gives ECONNRESET.
    void ResetClient();

private:
    Network::SocketApi::UniqueSocket m_listener;
    Network::SocketApi::UniqueSocket m_client;
    std::uint16_t m_port = 0;
    bool m_clientClosed = false;
};
} // namespace Client::Tests
