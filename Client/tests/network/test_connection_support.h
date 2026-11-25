#pragma once

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Network/Connection.h"
#include "Network/Crypto/SimpleModulus.h"
#include "Network/IdentityObfuscation.h"
#include "Network/IObfuscation.h"
#include "Network/Packet.h"
#include "Network/PacketSerials.h"

#include "support/Bytes.h"
#include "support/FakeClock.h"
#include "support/FakeSocket.h"
#include "support/NetworkPrinters.h"
#include "support/RecordingSink.h"
#include "support/TestGameServer.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// What test_connection.cpp and test_connection_xor_remap.cpp share: a Connection over a FakeSocket and a FakeClock.
namespace Client::Tests::ConnectionSupport
{
using Core::Log;
using Core::LogLevel;
using Network::Closed;
using Network::Connection;
using Network::Event;
using Network::IdentityObfuscation;
using Network::IObfuscation;
using Network::Opened;
using Network::Packet;
using Network::Undecryptable;

using ByteVector = std::vector<std::byte>;
using Lines = std::vector<std::string>;

// Any address will do: the FakeSocket does not connect.
inline constexpr std::string_view ConnectHost = "127.0.0.1";
inline constexpr std::uint16_t ConnectPort = 44405;
inline constexpr std::string_view GameHost = "10.0.0.2";
inline constexpr std::uint16_t GamePort = 55901;

// The ConnectServer's connection: without keys, a C3/C4 that arrives is Undecryptable.
inline constexpr std::nullopt_t NoKeys = std::nullopt;

inline constexpr std::uint32_t PaddingSeed = 1;

inline std::string HexOf(std::span<const std::byte> bytes)
{
    std::string text;
    for (const std::byte value : bytes)
    {
        text += std::format("{}{:02X}", text.empty() ? "" : " ", std::to_integer<int>(value));
    }
    return text;
}

// A Poll batch in words ("Opened", "C1 04 00 01", "Closed(PeerClosed)"), so a test compares the whole batch in order.
inline Lines Describe(const std::vector<Event>& events)
{
    Lines lines;
    for (const Event& event : events)
    {
        if (std::holds_alternative<Opened>(event))
        {
            lines.emplace_back("Opened");
        }
        else if (const Closed* closed = std::get_if<Closed>(&event))
        {
            lines.push_back("Closed(" + ::testing::PrintToString(closed->reason) + ")");
        }
        else if (const Undecryptable* undecryptable = std::get_if<Undecryptable>(&event))
        {
            lines.push_back("Undecryptable(" +
                            (undecryptable->error ? ::testing::PrintToString(*undecryptable->error) : "no keys") + ")");
        }
        else
        {
            lines.push_back(HexOf(std::get<Packet>(event).Bytes()));
        }
    }
    return lines;
}

class ConnectionTest : public ::testing::Test
{
protected:
    void OpenAndConnect(Connection& connection)
    {
        connection.Open(ConnectHost, ConnectPort, NoKeys);
        ASSERT_EQ(Describe(connection.Poll()), Lines{"Opened"});
    }

    void OpenAndConnect()
    {
        OpenAndConnect(m_connection);
    }

    void OpenTheGameServer(Connection& connection)
    {
        connection.Open(GameHost, GamePort, GameClientKeys());
        ASSERT_EQ(Describe(connection.Poll()), Lines{"Opened"});
    }

    void OpenTheGameServer()
    {
        OpenTheGameServer(m_connection);
    }

    [[nodiscard]] bool HasWarning(std::string_view part) const
    {
        for (std::size_t index = 0; index < m_sink.lines.size(); ++index)
        {
            if (m_sink.levels[index] == LogLevel::Warning && m_sink.lines[index].find(part) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

    // Whole lines: comparing them also shows they carry no packet bytes, which in the login are the account.
    [[nodiscard]] Lines Warnings() const
    {
        Lines warnings;
        for (std::size_t index = 0; index < m_sink.lines.size(); ++index)
        {
            if (m_sink.levels[index] == LogLevel::Warning)
            {
                warnings.push_back(m_sink.lines[index]);
            }
        }
        return warnings;
    }

    // A C3 that fails its checksum, between a good one and a clear hello: the hello stays clear and the connection
    // goes on. `mask` is the XOR the server puts on what it encrypts, which `obfuscation` takes off.
    void ExpectAFailedDecryptionDoesNotContaminateTheNextClearPacket(const IObfuscation& obfuscation, std::byte mask)
    {
        const ByteVector hello = Bytes({0xC1, 0x04, 0x00, 0x01});
        Connection connection(m_socket, obfuscation, m_clock, m_log, PaddingSeed);
        OpenTheGameServer(connection);
        Network::PacketSerials server;
        ByteVector good = FromTheGameServer(hello, server);
        ByteVector bad = FromTheGameServer(hello, server);
        const std::size_t checksum =
            Network::HeaderSize(Network::PacketType::C3) + Network::Crypto::CipherBlockSize - 1;
        bad[checksum] ^= std::byte{0xFF};
        for (ByteVector* part : {&good, &bad})
        {
            for (std::byte& value : *part)
            {
                value ^= mask;
            }
            EXPECT_EQ(obfuscation.InboundMask(part->front()), mask);
        }
        // The last hello arrives clear: besides the origin, the mask changes after the error.
        EXPECT_EQ(obfuscation.InboundMask(hello.front()), std::byte{0});
        ByteVector read = good;
        read.insert(read.end(), bad.begin(), bad.end());
        read.insert(read.end(), hello.begin(), hello.end());
        m_socket.reads = {read};
        const auto events = connection.Poll();
        ASSERT_EQ(Describe(events), (Lines{"C1 04 00 01", "Undecryptable(ChecksumMismatch)", "C1 04 00 01"}));
        EXPECT_TRUE(std::get<Packet>(events[0]).WasDecrypted());
        EXPECT_FALSE(std::get<Packet>(events[2]).WasDecrypted());
    }

    FakeSocket m_socket;
    FakeClock m_clock;
    RecordingSink m_sink;
    Log m_log{m_sink, LogLevel::Debug};
    Connection m_connection{m_socket, IdentityObfuscation::Shared(), m_clock, m_log, PaddingSeed};
};
} // namespace Client::Tests::ConnectionSupport
