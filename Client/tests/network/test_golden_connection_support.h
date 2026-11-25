#pragma once

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Network/Connection.h"
#include "Crypto/SimpleModulus.h"
#include "Crypto/SimpleModulusKeyFile.h"
#include "Network/Delivery.h"
#include "Network/EncryptedPacket.h"
#include "Network/Packet.h"
#include "Network/PacketSerials.h"

#include "support/FakeClock.h"
#include "support/FakeSocket.h"
#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/KeyFiles.h"
#include "support/RecordingSink.h"
#include "support/WireCapture.h"
#include "support/WirePackets.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// What test_golden_connection.cpp and test_golden_connection_xor_remap.cpp share: a Connection that reads the lab's
// wire capture from a FakeSocket, with the client's real keys.
namespace Client::Tests::GoldenConnectionSupport
{
using Core::Log;
using Core::LogLevel;
using Network::Connection;
using Network::ConnectionKeys;
using Network::DecryptPacket;
using Network::Delivery;
using Network::Event;
using Network::Opened;
using Network::Packet;
using Network::PacketSerials;
using Network::PacketType;
using Network::Undecryptable;
using Network::Crypto::DecryptionKey;
using Network::Crypto::LoadDecryptionKey;
using Network::Crypto::LoadEncryptionKey;

inline constexpr std::string_view CaptureFile = "muproxy.log";

// The proxy's port in front of the lab's GameServer: the C3/C4 go through it.
inline constexpr std::uint16_t GameServerPort = 55901;

// Any address will do: the FakeSocket does not connect.
inline constexpr std::string_view GameHost = "10.0.0.2";

inline constexpr std::uint32_t PaddingSeed = 1;

// More than enough: the longest log has under a thousand reads and each Poll does up to eight.
inline constexpr int MaxPolls = 1000;

inline std::string Where(std::size_t connection, std::size_t index)
{
    return "connection " + std::to_string(connection) + ", packet " + std::to_string(index);
}

inline bool IsEncrypted(const Packet& packet)
{
    return packet.Type() == PacketType::C3 || packet.Type() == PacketType::C4;
}

struct Keys
{
    ConnectionKeys client; // Enc1.dat and Dec2.dat
    DecryptionKey server;  // Dec1.dat
};

// What the original client sent in a connection and what the Connection sent from it.
struct Resent
{
    std::vector<Packet> wire;  // the C3/C4 of the wire
    std::vector<Packet> plain; // the same, decrypted with Dec1.dat
    std::vector<Packet> mine;  // what the Connection sent, cut
};

class GoldenConnection : public GoldenAndWireDataTest
{
protected:
    void SetUp() override
    {
        GoldenAndWireDataTest::SetUp();
        if (IsSkipped() || HasFatalFailure())
        {
            return;
        }
        // if and FAIL, not ASSERT_TRUE, so clang-tidy sees the optional was checked.
        const auto path = WireFile(CaptureFile);
        if (!path)
        {
            FAIL() << CaptureFile << " is missing";
        }
        const auto file = ReadFileBytes(*path);
        if (!file)
        {
            FAIL() << CaptureFile << " could not be read";
        }
        auto parsed = ParseWireCapture(std::string_view(file->data(), file->size()));
        ASSERT_TRUE(parsed.HasValue()) << CaptureFile << ": line " << parsed.Error().line << " does not parse";
        m_connections = std::move(parsed).Value();

        // The client's keys come from the data folder and the server's from the wire folder (support/GoldenDir.h).
        const auto enc1 = LoadKeyFile(DataFile("Enc1.dat"), LoadEncryptionKey);
        const auto dec2 = LoadKeyFile(DataFile("Dec2.dat"), LoadDecryptionKey);
        const auto dec1 = LoadKeyFile(WireFile("Dec1.dat"), LoadDecryptionKey);
        if (!enc1 || !dec2 || !dec1)
        {
            FAIL() << "the keys do not load";
        }
        m_keys.emplace(Keys{ConnectionKeys{*enc1, *dec2}, *dec1});
    }

    void OpenTheGameServer(Connection& connection, const ConnectionKeys& keys, std::size_t index)
    {
        connection.Open(GameHost, GameServerPort, keys);
        const std::vector<Event> opened = connection.Poll();
        ASSERT_EQ(opened.size(), 1u) << "connection " << index;
        ASSERT_TRUE(std::holds_alternative<Opened>(opened.front())) << "connection " << index;
    }

    // Polls until the FakeSocket has nothing left, and once more. Any event that is not a packet fails the test.
    std::vector<Packet> PollEverything(Connection& connection, std::size_t index)
    {
        std::vector<Packet> packets;
        for (int poll = 0; poll < MaxPolls; ++poll)
        {
            const bool wasEmpty = m_socket.reads.empty();
            std::vector<Event> events = connection.Poll();
            for (Event& event : events)
            {
                if (Packet* packet = std::get_if<Packet>(&event))
                {
                    packets.push_back(std::move(*packet));
                }
                else
                {
                    ADD_FAILURE() << "connection " << index << ": "
                                  << (std::holds_alternative<Undecryptable>(event) ? "an Undecryptable"
                                                                                   : "an event that is not a packet");
                }
            }
            if (wasEmpty)
            {
                return packets;
            }
        }
        ADD_FAILURE() << "connection " << index << ": something was left unread";
        return packets;
    }

    // Decrypts with `server` each C3/C4 the original sent and sends it again through `connection`, Encrypted.
    void ResendEncrypted(Connection& connection, const std::vector<Packet>& sent, const DecryptionKey& server,
                         std::size_t index, Resent& resent)
    {
        const std::size_t sentBefore = m_socket.sent.size();
        PacketSerials wireSerials;
        for (const Packet& packet : sent)
        {
            if (!IsEncrypted(packet))
            {
                continue;
            }
            auto decrypted = DecryptPacket(packet, server, wireSerials);
            ASSERT_TRUE(decrypted.HasValue()) << Where(index, resent.wire.size());
            connection.Send(decrypted.Value().packet, Delivery::Encrypted);
            resent.wire.push_back(packet);
            resent.plain.push_back(std::move(decrypted).Value().packet);
        }
        const std::vector<std::byte> mine(m_socket.sent.begin() + static_cast<std::ptrdiff_t>(sentBefore),
                                          m_socket.sent.end());
        resent.mine = CutPackets({std::span<const std::byte>(mine)}, index);
    }

    // Counted and never printed: a warning says the opcode.
    [[nodiscard]] std::size_t WarningCount() const
    {
        return static_cast<std::size_t>(std::ranges::count(m_sink.levels, LogLevel::Warning));
    }

    std::vector<WireConnection> m_connections;
    std::optional<Keys> m_keys; // has a value if SetUp did not fail
    FakeSocket m_socket;
    FakeClock m_clock;
    RecordingSink m_sink;
    Log m_log{m_sink, LogLevel::Debug};
};
} // namespace Client::Tests::GoldenConnectionSupport
