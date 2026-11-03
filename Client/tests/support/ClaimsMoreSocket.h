#pragma once

#include "Core/Expected.h"
#include "Network/ISocket.h"
#include "Network/SocketError.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <string_view>
#include <vector>

// Un ISocket roto, que dice haber movido Extra bytes más de los que podía, para ver que quien lo
// usa no se pasa del buffer: el primer Receive llena el buffer con `first` y dice que leyó Extra
// más, y el primer Send dice que tomó Extra más de los que se le dieron. Después Receive da
// `reads`, una entrada por llamada, y 0 cuando no quedan, y Send toma 0. Conecta enseguida y no
// cierra nada. Cada test usa una de las dos mentiras: el de la Connection, la de Receive, y el de
// la SendQueue, la de Send.
namespace Client::Tests
{
class ClaimsMoreSocket final : public Network::ISocket
{
public:
    static constexpr std::size_t Extra = 5;

    std::vector<std::byte> first;             // lo que trae el primer Receive, sin los Extra
    std::deque<std::vector<std::byte>> reads; // lo que traen los siguientes
    int sends = 0;                            // cuántas veces se llamó a Send

    Core::Expected<Network::ConnectProgress, Network::SocketError> StartConnect(std::string_view,
                                                                                std::uint16_t) override
    {
        return Network::ConnectProgress::Connected;
    }
    Core::Expected<Network::ConnectProgress, Network::SocketError> PollConnect() override
    {
        return Network::ConnectProgress::Connected;
    }
    Core::Expected<std::size_t, Network::SocketError> Receive(std::span<std::byte> buffer) override
    {
        if (!m_claimedARead)
        {
            m_claimedARead = true;
            std::ranges::copy(first, buffer.begin());
            return buffer.size() + Extra;
        }
        if (reads.empty())
        {
            return std::size_t{0};
        }
        const std::vector<std::byte> next = reads.front();
        reads.pop_front();
        std::ranges::copy(next, buffer.begin());
        return next.size();
    }
    Core::Expected<std::size_t, Network::SocketError> Send(std::span<const std::byte> bytes) override
    {
        ++sends;
        if (sends == 1)
        {
            return bytes.size() + Extra;
        }
        return std::size_t{0};
    }
    void Close() override {}
    [[nodiscard]] bool IsOpen() const override
    {
        return true;
    }
    [[nodiscard]] int LastSystemError() const override
    {
        return 0;
    }

private:
    bool m_claimedARead = false;
};
} // namespace Client::Tests
