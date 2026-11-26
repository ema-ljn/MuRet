#pragma once

#include "App/Credentials.h"
#include "App/ServerSession.h"
#include "App/SessionState.h"
#include "App/WorldEvent.h"
#include "Core/IClock.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Network/Connection.h"
#include "Network/IObfuscation.h"
#include "Network/Packet.h"
#include "Network/PacketSerials.h"
#include "Network/Xor32Packet.h"
#include "Protocol/Session/BuildConstants.h"
#include "Protocol/FrontEnd/CharacterSelectResult.h"
#include "Protocol/Items/InventoryList.h"

#include "support/Bytes.h"
#include "support/CharacterPackets.h"
#include "support/FakeClock.h"
#include "support/FakeSocket.h"
#include "support/RecordingSessionEventSink.h"
#include "support/RecordingSink.h"
#include "support/SameBytes.h"
#include "support/SessionPackets.h"
#include "support/TestGameServer.h"
#include "Protocol/Opcodes.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// What test_server_session.cpp and test_server_session_xor_remap.cpp share: a ServerSession over a real Connection
// and a FakeSocket, the steps that take it to each state, and the world packets of the entry.
namespace Client::Tests::ServerSessionSupport
{
using App::Credentials;
using App::ServerSession;
using App::SessionSettings;
using App::SessionState;
using App::WorldEvent;
using Core::Duration;
using Core::Log;
using Core::LogLevel;
using Network::Connection;
using Network::IObfuscation;
using Network::Packet;
using Network::PacketSerials;
using Network::PacketType;
using Network::SerialCheck;
using Network::Xor32Decode;
using Protocol::CharacterOpcode;
using Protocol::ClientVersion;
using Protocol::KeepaliveOpcode;
using Protocol::Character::CharacterSelectResult;
using Protocol::Character::InventoryList;

using std::chrono::milliseconds;

using ByteVector = std::vector<std::byte>;
using Lines = std::vector<std::string>;

inline constexpr std::string_view ConnectHost = "10.0.0.1";
inline constexpr std::uint16_t ConnectPort = 44405;
inline constexpr std::string_view GameHost = "10.0.0.2";
inline constexpr std::uint16_t GamePort = 55901;

inline constexpr std::uint32_t PaddingSeed = 1;

// One logic step, as in a loop at 25 Hz.
inline constexpr Duration Frame = milliseconds{40};

// The lab's server 0 as main.exe sees it: 0 plus FoxAnti.dll's delta, 400.
inline constexpr std::uint16_t LabServerCode = 400;

// The synthetic account of the login fixtures (MuClientNetFixtures.h).
inline constexpr std::string_view Account = "user0001";
inline constexpr std::string_view Password = "pass0002";

// The lab's list as it arrives on the wire, F4 02 with one server.
inline ByteVector LabServerList()
{
    return Bytes({0xC2, 0x00, 0x0A, 0xF4, 0x02, 0x01, 0x00, 0x00, 0x00, 0xCC});
}

inline SessionSettings Settings()
{
    return SessionSettings{{std::string(ConnectHost), ConnectPort}};
}

inline Credentials TheCredentials()
{
    return Credentials{std::string(Account), std::string(Password)};
}

inline std::vector<CharacterEntryFields> TwoCharacters()
{
    return {{0, "char0001"}, {3, "char0002"}};
}

// The obfuscation belongs to whoever builds the harness and must outlive it.
struct Harness
{
    explicit Harness(const IObfuscation& obfuscation, SessionSettings settings = Settings())
        : connection(socket, obfuscation, clock, log, PaddingSeed),
          session(connection, clock, log, GameClientKeys(), std::move(settings), eventSink)
    {
    }

    [[nodiscard]] Lines Warnings() const
    {
        Lines warnings;
        for (std::size_t index = 0; index < sink.lines.size(); ++index)
        {
            if (sink.levels[index] == LogLevel::Warning)
            {
                warnings.push_back(sink.lines[index]);
            }
        }
        return warnings;
    }

    [[nodiscard]] bool Logged(LogLevel level, std::string_view part) const
    {
        for (std::size_t index = 0; index < sink.lines.size(); ++index)
        {
            if (sink.levels[index] == level && sink.lines[index].find(part) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

    // The address is the server's and the log must not say it (ServerSession.cpp, OnServerAddress).
    [[nodiscard]] Lines LinesWithAnAddress() const
    {
        const std::string parts[] = {std::string(ConnectHost), std::string(GameHost), std::to_string(ConnectPort),
                                     std::to_string(GamePort)};
        Lines found;
        for (const std::string& line : sink.lines)
        {
            const auto says = [&line](const std::string& part) { return line.find(part) != std::string::npos; };
            if (std::ranges::any_of(parts, says))
            {
                found.push_back(line);
            }
        }
        return found;
    }

    void ReachServerList()
    {
        session.Start();
        socket.reads = {Hello()};
        static_cast<void>(session.Poll());
        ASSERT_EQ(session.State(), SessionState::WaitingServerList);
    }

    void ReachChoosingServer()
    {
        ASSERT_NO_FATAL_FAILURE(ReachServerList());
        socket.reads = {ServerList({LabServerCode})};
        static_cast<void>(session.Poll());
        ASSERT_EQ(session.State(), SessionState::ChoosingServer);
    }

    void ReachServerAddress()
    {
        ASSERT_NO_FATAL_FAILURE(ReachChoosingServer());
        session.ChooseServer(LabServerCode);
        ASSERT_EQ(session.State(), SessionState::WaitingServerAddress);
    }

    void ReachGameServer()
    {
        ASSERT_NO_FATAL_FAILURE(ReachServerAddress());
        socket.reads = {AddressReply(GameHost, GamePort)};
        static_cast<void>(session.Poll());
        ASSERT_EQ(session.State(), SessionState::ConnectingGameServer);
        gameServerStart = socket.sent.size();
    }

    void ReachGameHello()
    {
        ASSERT_NO_FATAL_FAILURE(ReachGameServer());
        static_cast<void>(session.Poll());
        ASSERT_EQ(session.State(), SessionState::WaitingGameHello);
    }

    void ReachWaitingCredentials()
    {
        ASSERT_NO_FATAL_FAILURE(ReachGameHello());
        socket.reads = {FromTheGameServer(GameHello(1, ClientVersion), gameServerSends)};
        static_cast<void>(session.Poll());
        ASSERT_EQ(session.State(), SessionState::WaitingCredentials);
    }

    void ReachLoginResult()
    {
        ASSERT_NO_FATAL_FAILURE(ReachWaitingCredentials());
        session.SubmitCredentials(TheCredentials());
        ASSERT_EQ(session.State(), SessionState::WaitingLoginResult);
    }

    void ReachLoggedIn()
    {
        ASSERT_NO_FATAL_FAILURE(ReachLoginResult());
        socket.reads = {FromTheGameServer(LoginResult(1), gameServerSends)};
        static_cast<void>(session.Poll());
        ASSERT_EQ(session.State(), SessionState::LoggedIn);
    }

    void ReachWaitingCharacterList()
    {
        ASSERT_NO_FATAL_FAILURE(ReachLoggedIn());
        session.RequestCharacterList();
        ASSERT_EQ(session.State(), SessionState::WaitingCharacterList);
    }

    // 04 § La selección de personaje: the GS sends the character list in the clear.
    void ReachChoosingCharacter(const std::vector<CharacterEntryFields>& characters = TwoCharacters())
    {
        ASSERT_NO_FATAL_FAILURE(ReachWaitingCharacterList());
        socket.reads = {CharacterListBytes(characters)};
        static_cast<void>(session.Poll());
        ASSERT_EQ(session.State(), SessionState::ChoosingCharacter);
    }

    void ReachEnteringWorld()
    {
        ASSERT_NO_FATAL_FAILURE(ReachChoosingCharacter());
        session.ChooseCharacter(std::uint8_t{0});
        session.EnterWorld();
        ASSERT_EQ(session.State(), SessionState::EnteringWorld);
    }

    // The F3 `subcode` packets the GS received, without the XOR32, in order.
    [[nodiscard]] std::vector<Packet> CharacterPacketsSent(Client::Network::Subcode subcode) const
    {
        std::vector<Packet> found;
        for (const Packet& packet : GameServerReads().packets)
        {
            Packet clear = Xor32Decode(packet);
            if (clear.Type() == PacketType::C1 && clear.Opcode() == CharacterOpcode && clear.Bytes().size() > 3 &&
                Client::Network::Subcode{std::to_integer<std::uint8_t>(clear.Bytes()[3])} == subcode)
            {
                found.push_back(std::move(clear));
            }
        }
        return found;
    }

    void LogInAt(Duration time)
    {
        ASSERT_NO_FATAL_FAILURE(ReachLoginResult());
        AdvanceTo(time);
        socket.reads = {FromTheGameServer(LoginResult(1), gameServerSends)};
        static_cast<void>(session.Poll());
        ASSERT_EQ(session.State(), SessionState::LoggedIn);
    }

    void AdvanceTo(Duration time)
    {
        clock.Advance(time - clock.Now());
    }

    // The Poll at `time` is included.
    void PollEachFrameUntil(Duration time)
    {
        while (clock.Now() + Frame <= time)
        {
            clock.Advance(Frame);
            static_cast<void>(session.Poll());
        }
    }

    // Everything the GS received must be in order: a keepalive before the login uses a serial too.
    [[nodiscard]] std::vector<Packet> Keepalives() const
    {
        const GameServerReceived received = GameServerReads();
        EXPECT_TRUE(
            std::ranges::all_of(received.serials, [](SerialCheck serial) { return serial == SerialCheck::InOrder; }));
        std::vector<Packet> keepalives;
        for (const Packet& packet : received.packets)
        {
            Packet clear = Xor32Decode(packet);
            if (clear.Opcode() == KeepaliveOpcode)
            {
                keepalives.push_back(std::move(clear));
            }
        }
        return keepalives;
    }

    [[nodiscard]] std::size_t CountLogged(LogLevel level, std::string_view part) const
    {
        std::size_t count = 0;
        for (std::size_t index = 0; index < sink.lines.size(); ++index)
        {
            if (sink.levels[index] == level && sink.lines[index].find(part) != std::string::npos)
            {
                ++count;
            }
        }
        return count;
    }

    // What the GS received since it opened, as it reads it: each C3 decrypted, serials from 0.
    [[nodiscard]] GameServerReceived GameServerReads() const
    {
        PacketSerials serials;
        return AsTheGameServerReceives(std::span(socket.sent).subspan(gameServerStart), serials);
    }

    [[nodiscard]] bool LoggedTheCredentials() const
    {
        return std::ranges::any_of(
            sink.lines, [](const std::string& line)
            { return line.find(Account) != std::string::npos || line.find(Password) != std::string::npos; });
    }

    FakeSocket socket;
    FakeClock clock;
    RecordingSink sink;
    Log log{sink, LogLevel::Debug};
    Connection connection;
    RecordingSessionEventSink eventSink;
    ServerSession session;
    std::size_t gameServerStart = 0; // where what goes to the GS starts in `socket.sent`
    PacketSerials gameServerSends;   // the serials of what the GS encrypts
};

// Synthetic wire bytes, independent of the readers and writers: the minimum 0x34, distinct padding and 23 fields.
inline ByteVector WorldEntryReply()
{
    return Bytes({0xC1, 0x34, 0xF3, 0x03, 0x81, 0x2A, 0x20, 0x00, 0xEF, 0xCD, 0xAB, 0x89, 0x76,
                  0x54, 0x32, 0x10, 0x11, 0x91, 0x22, 0xA2, 0x33, 0xB3, 0x44, 0xC4, 0x55, 0xD5,
                  0x66, 0xE6, 0x77, 0xF7, 0x88, 0x18, 0x99, 0x29, 0xAA, 0x3A, 0xBB, 0x4B, 0xDE,
                  0xAD, 0x98, 0xBA, 0xDC, 0xFE, 0x80, 0xFE, 0xCC, 0x5C, 0xDD, 0x6D, 0xFF, 0xFF});
}

inline ByteVector OtherWorldEntryReply()
{
    ByteVector bytes = WorldEntryReply();
    bytes[4] = std::byte{0};
    bytes[5] = std::byte{0xFF};
    bytes[6] = std::byte{0xFF};
    bytes[7] = std::byte{0xFF};
    return bytes;
}

// A C2 is encrypted as a C4 even when short (support/TestGameServer.h); no entry is a real item.
inline ByteVector InventoryReply()
{
    return Bytes({0xC2, 0x00, 0x1E, 0xF3, 0x10, 0x03, 0xFF, 0x00, 0x80, 0xFF, 0x01, 0x7F, 0xAA, 0x55, 0x0C,
                  0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0xFF, 0x81, 0x02, 0x40, 0xF0, 0x0F, 0xCC, 0x33});
}

// The fields WorldEntryReply and OtherWorldEntryReply share.
inline void ExpectTheStatsOfTheEntry(const CharacterSelectResult& entry)
{
    EXPECT_EQ(entry.experience, 0x89ABCDEFu);
    EXPECT_EQ(entry.nextExperience, 0x10325476u);
    EXPECT_EQ(entry.levelUpPoints, 0x9111);
    EXPECT_EQ(entry.strength, 0xA222);
    EXPECT_EQ(entry.agility, 0xB333);
    EXPECT_EQ(entry.vitality, 0xC444);
    EXPECT_EQ(entry.energy, 0xD555);
    EXPECT_EQ(entry.life, 0xE666);
    EXPECT_EQ(entry.maxLife, 0xF777);
    EXPECT_EQ(entry.mana, 0x1888);
    EXPECT_EQ(entry.maxMana, 0x2999);
    EXPECT_EQ(entry.bp, 0x3AAA);
    EXPECT_EQ(entry.maxBp, 0x4BBB);
    EXPECT_EQ(entry.zen, 0xFEDCBA98u);
    EXPECT_EQ(entry.pk, 0x80);
    EXPECT_EQ(entry.controlCode, 0xFE);
    EXPECT_EQ(entry.addedPoints, 0x5CCC);
    EXPECT_EQ(entry.maxAddedPoints, 0x6DDD);
    EXPECT_EQ(entry.leadership, 0xFFFF);
}

inline void ExpectWorldEntry(const WorldEvent& event)
{
    const auto* entry = std::get_if<CharacterSelectResult>(&event);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->x, 0x81);
    EXPECT_EQ(entry->y, 0x2A);
    EXPECT_EQ(entry->map, 32); // Receiving does not apply the 32 -> 9 remap.
    EXPECT_EQ(entry->direction, 0);
    ExpectTheStatsOfTheEntry(*entry);
}

inline void ExpectInventory(const WorldEvent& event)
{
    const auto* inventory = std::get_if<InventoryList>(&event);
    ASSERT_NE(inventory, nullptr);
    ASSERT_EQ(inventory->items.size(), 3u);
    EXPECT_EQ(inventory->items[0].slot, 0xFF);
    EXPECT_TRUE(SameBytes(inventory->items[0].item, Bytes({0x00, 0x80, 0xFF, 0x01, 0x7F, 0xAA, 0x55})));
    EXPECT_EQ(inventory->items[1].slot, 0x0C);
    EXPECT_TRUE(SameBytes(inventory->items[1].item, Bytes({0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32})));
    EXPECT_EQ(inventory->items[2].slot, 0xFF);
    EXPECT_TRUE(SameBytes(inventory->items[2].item, Bytes({0x81, 0x02, 0x40, 0xF0, 0x0F, 0xCC, 0x33})));
}

inline ByteVector EncryptedWorldSequence(Harness& tested)
{
    return Join({FromTheGameServer(WorldEntryReply(), tested.gameServerSends),
                 FromTheGameServer(InventoryReply(), tested.gameServerSends),
                 FromTheGameServer(OtherWorldEntryReply(), tested.gameServerSends)});
}

inline void ExpectWorldSequence(std::span<const WorldEvent> events)
{
    ASSERT_EQ(events.size(), 3u);
    ASSERT_NO_FATAL_FAILURE(ExpectWorldEntry(events[0]));
    ASSERT_NO_FATAL_FAILURE(ExpectInventory(events[1]));
    const auto* other = std::get_if<CharacterSelectResult>(&events[2]);
    ASSERT_NE(other, nullptr);
    EXPECT_EQ(other->x, 0);
    EXPECT_EQ(other->y, 0xFF);
    EXPECT_EQ(other->map, 0xFF);
    EXPECT_EQ(other->direction, 0xFF);
    ExpectTheStatsOfTheEntry(*other);
}
} // namespace Client::Tests::ServerSessionSupport
