#include "support/FakeSocket.h"

#include "support/Bytes.h"
#include "support/ExpectedError.h"
#include "support/NetworkPrinters.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <optional>
#include <span>

// Client::Tests::FakeSocket: el ISocket falso de los tests de la red (el plan de la CL4, CL4.1).
namespace
{
using Client::Network::ConnectProgress;
using Client::Network::SocketError;
using Client::Tests::Bytes;
using Client::Tests::ErrorOf;
using Client::Tests::FakeSocket;
using Client::Tests::SameBytes;

constexpr std::uint16_t AnyPort = 44405;

// Conectado en el StartConnect, sin PollConnect.
void Connect(FakeSocket& socket)
{
    ASSERT_EQ(socket.StartConnect("127.0.0.1", AnyPort).Value(), ConnectProgress::Connected);
}

TEST(FakeSocketTest, FollowsTheScript)
{
    FakeSocket socket;
    socket.pollsUntilConnected = 2;
    socket.reads = {Bytes({0xC1, 0x04}), Bytes({0x00, 0x01})};
    socket.sendLimits = {std::size_t{2}};

    EXPECT_EQ(socket.StartConnect("127.0.0.1", AnyPort).Value(), ConnectProgress::InProgress);
    EXPECT_EQ(socket.PollConnect().Value(), ConnectProgress::InProgress);
    EXPECT_EQ(socket.PollConnect().Value(), ConnectProgress::InProgress);
    EXPECT_EQ(socket.PollConnect().Value(), ConnectProgress::Connected);

    std::array<std::byte, 16> buffer{};
    ASSERT_EQ(socket.Receive(buffer).Value(), 2u);
    EXPECT_TRUE(SameBytes(std::span(buffer).first(2), Bytes({0xC1, 0x04})));
    ASSERT_EQ(socket.Receive(buffer).Value(), 2u);
    EXPECT_TRUE(SameBytes(std::span(buffer).first(2), Bytes({0x00, 0x01})));

    const auto packet = Bytes({0xC1, 0x04, 0xF4, 0x02});
    EXPECT_EQ(socket.Send(packet).Value(), 2u);
    EXPECT_TRUE(SameBytes(socket.sent, Bytes({0xC1, 0x04})));
}

TEST(FakeSocketTest, WritesDownEachConnectAndClose)
{
    FakeSocket socket;
    Connect(socket);
    socket.Close();
    EXPECT_FALSE(socket.IsOpen());
    ASSERT_EQ(socket.StartConnect("10.0.0.2", 55901).Value(), ConnectProgress::Connected);
    EXPECT_TRUE(socket.IsOpen());

    ASSERT_EQ(socket.connects.size(), 2u);
    EXPECT_EQ(socket.connects[0].host, "127.0.0.1");
    EXPECT_EQ(socket.connects[0].port, AnyPort);
    EXPECT_EQ(socket.connects[1].host, "10.0.0.2");
    EXPECT_EQ(socket.connects[1].port, 55901);
    EXPECT_EQ(socket.closes, 1);
}

// Como SystemSocket: el resultado final del connect se repite.
TEST(FakeSocketTest, AFailedConnectStaysFailed)
{
    FakeSocket socket;
    socket.pollsUntilConnected = 1;
    socket.connectResult = SocketError::Refused;

    EXPECT_EQ(socket.StartConnect("127.0.0.1", AnyPort).Value(), ConnectProgress::InProgress);
    EXPECT_EQ(socket.PollConnect().Value(), ConnectProgress::InProgress);
    EXPECT_EQ(ErrorOf(socket.PollConnect()), SocketError::Refused);
    // Lo guardado no es el guion: cambiarlo después no cambia lo que terminó.
    socket.connectResult.reset();
    EXPECT_EQ(ErrorOf(socket.PollConnect()), SocketError::Refused);
    EXPECT_EQ(ErrorOf(socket.Receive(std::span<std::byte>())), SocketError::Closed);
    // Un Refused deja el socket hasta Close, como SystemSocket.
    EXPECT_TRUE(socket.IsOpen());
}

// Como SystemSocket: un nombre que no resuelve falla en el StartConnect, aunque el guion pida
// PollConnect en curso, y no deja un socket abierto.
TEST(FakeSocketTest, AnUnresolvedHostIsNotOpen)
{
    FakeSocket socket;
    socket.pollsUntilConnected = 2;
    socket.connectResult = SocketError::ResolveFailed;

    EXPECT_EQ(ErrorOf(socket.StartConnect("nombre.invalid", AnyPort)), SocketError::ResolveFailed);
    EXPECT_FALSE(socket.IsOpen());
    EXPECT_EQ(ErrorOf(socket.PollConnect()), SocketError::ResolveFailed);

    // Otro StartConnect no cierra nada: no había un socket.
    socket.connectResult.reset();
    socket.pollsUntilConnected = 0;
    EXPECT_EQ(socket.StartConnect("127.0.0.1", AnyPort).Value(), ConnectProgress::Connected);
    EXPECT_TRUE(socket.IsOpen());
    EXPECT_EQ(socket.closes, 0);
    EXPECT_EQ(socket.connects.size(), 2u);
}

TEST(FakeSocketTest, AReadLongerThanTheBufferComesInPieces)
{
    FakeSocket socket;
    Connect(socket);
    socket.reads = {Bytes({1, 2, 3, 4, 5})};

    std::array<std::byte, 3> buffer{};
    ASSERT_EQ(socket.Receive(buffer).Value(), 3u);
    EXPECT_TRUE(SameBytes(buffer, Bytes({1, 2, 3})));
    ASSERT_EQ(socket.Receive(buffer).Value(), 2u);
    EXPECT_TRUE(SameBytes(std::span(buffer).first(2), Bytes({4, 5})));
    EXPECT_EQ(socket.Receive(buffer).Value(), 0u);
}

TEST(FakeSocketTest, APeerThatClosedIsClosedAfterTheReads)
{
    FakeSocket socket;
    Connect(socket);
    socket.reads = {Bytes({1})};
    socket.peerClosed = true;

    std::array<std::byte, 4> buffer{};
    EXPECT_EQ(socket.Receive(std::span<std::byte>()).Value(), 0u);
    EXPECT_EQ(socket.Receive(buffer).Value(), 1u);
    EXPECT_EQ(ErrorOf(socket.Receive(buffer)), SocketError::Closed);
}

TEST(FakeSocketTest, ASendLimitOfNulloptTakesNothingAndAnErrorIsOnce)
{
    FakeSocket socket;
    Connect(socket);
    socket.sendLimits = {std::nullopt};
    socket.sendError = SocketError::Closed;
    const auto packet = Bytes({1, 2, 3});

    EXPECT_EQ(ErrorOf(socket.Send(packet)), SocketError::Closed);
    EXPECT_EQ(socket.Send(packet).Value(), 0u);
    EXPECT_EQ(socket.Send(packet).Value(), 3u);
    EXPECT_TRUE(SameBytes(socket.sent, packet));
}

// Un error en el guion de sendLimits sale en su turno, después de los Send de antes.
TEST(FakeSocketTest, AScriptedSendErrorComesInItsTurn)
{
    FakeSocket socket;
    Connect(socket);
    socket.sendLimits = {std::size_t{2}, SocketError::Closed};
    const auto packet = Bytes({1, 2, 3});

    EXPECT_EQ(socket.Send(packet).Value(), 2u);
    EXPECT_EQ(ErrorOf(socket.Send(packet)), SocketError::Closed);
    EXPECT_EQ(socket.Send(packet).Value(), 3u);
    EXPECT_TRUE(SameBytes(socket.sent, Bytes({1, 2, 1, 2, 3})));
}

TEST(FakeSocketTest, WithoutAConnectionReceiveAndSendAreClosed)
{
    FakeSocket socket;
    std::array<std::byte, 4> buffer{};

    EXPECT_EQ(ErrorOf(socket.PollConnect()), SocketError::Closed);
    EXPECT_EQ(ErrorOf(socket.Receive(buffer)), SocketError::Closed);
    EXPECT_EQ(ErrorOf(socket.Send(buffer)), SocketError::Closed);
    EXPECT_TRUE(socket.sent.empty());
}
} // namespace
