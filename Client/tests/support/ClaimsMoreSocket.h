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

// A broken ISocket that claims Extra more bytes than it could, to check its user does not overrun the buffer: the
// first Receive fills the buffer with `first` and claims Extra more, and the first Send claims Extra more than it was
// given. Then Receive gives `reads`, one per call, and 0 when empty, and Send takes 0. Connects at once and never
// closes. The Connection test uses the Receive lie, the SendQueue test the Send lie.
namespace Client::Tests
{
class ClaimsMoreSocket final : public Network::ISocket
{
public:
    static constexpr std::size_t Extra = 5;

    std::vector<std::byte> first;             // what the first Receive brings, without the Extra
    std::deque<std::vector<std::byte>> reads; // what the next ones bring
    int sends = 0;

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
