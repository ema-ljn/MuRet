#include "Protocol/Dispatcher.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Network/Packet.h"

#include "support/Bytes.h"
#include "support/Packets.h"
#include "support/ProtocolPrinters.h"
#include "support/RecordingSink.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Client::Protocol::Dispatcher: el opcode y el subcódigo de cada paquete, a su handler (04, "El
// dispatcher 0x4e2b10"), y lo que no tiene handler, al log una vez.
namespace
{
using Client::Core::Log;
using Client::Core::LogLevel;
using Client::Network::Packet;
using Client::Protocol::Dispatcher;
using Client::Protocol::DispatchResult;
using Client::Tests::Bytes;
using Client::Tests::PacketOf;
using Client::Tests::RecordingSink;

// Los opcodes de los tests: el keepalive (sin subcódigo), la entrada al GameServer y la lista de
// servidores (con subcódigo), y dos que no tienen handler en ningún test.
constexpr std::uint8_t Keepalive = 0x0E;
constexpr std::uint8_t Join = 0xF1;
constexpr std::uint8_t ServerList = 0xF4;
constexpr std::uint8_t NoHandler = 0x99;
constexpr std::uint8_t AnotherWithoutHandler = 0x98;

// Un Dispatcher con su log, que guarda las líneas.
struct DispatcherWithLog
{
    RecordingSink sink;
    Log log{sink, LogLevel::Info};
    Dispatcher dispatcher{log};
};

// Un handler que anota los bytes de cada paquete que recibe.
struct Calls
{
    std::vector<std::vector<std::byte>> packets;

    Client::Protocol::Handler Record()
    {
        return [this](const Packet& packet) { packets.emplace_back(packet.Bytes().begin(), packet.Bytes().end()); };
    }
};

bool Contains(const std::string& line, const std::string& text)
{
    return line.find(text) != std::string::npos;
}

TEST(DispatcherTest, CallsTheHandlerOfTheOpcode)
{
    DispatcherWithLog tested;
    Calls keepalive;
    ASSERT_TRUE(tested.dispatcher.On(Keepalive, keepalive.Record()));

    EXPECT_EQ(tested.dispatcher.Dispatch(PacketOf(Bytes({0xC1, 0x04, 0x0E, 0x00}))), DispatchResult::Handled);

    ASSERT_EQ(keepalive.packets.size(), 1u);
    EXPECT_EQ(keepalive.packets[0], Bytes({0xC1, 0x04, 0x0E, 0x00}));
    EXPECT_TRUE(tested.sink.lines.empty());
}

// El subcódigo de un C1 es pkt[3]: C1 05 F1 01 01 es el resultado del login (F1 01), y el 01 que
// sigue, el resultado, lo lee el handler.
TEST(DispatcherTest, RoutesBySubcode)
{
    DispatcherWithLog tested;
    Calls hello;
    Calls login;
    ASSERT_TRUE(tested.dispatcher.On(Join, 0x00, hello.Record()));
    ASSERT_TRUE(tested.dispatcher.On(Join, 0x01, login.Record()));

    EXPECT_EQ(tested.dispatcher.Dispatch(PacketOf(Bytes({0xC1, 0x05, 0xF1, 0x01, 0x01}))), DispatchResult::Handled);

    EXPECT_TRUE(hello.packets.empty());
    ASSERT_EQ(login.packets.size(), 1u);
    EXPECT_EQ(login.packets[0], Bytes({0xC1, 0x05, 0xF1, 0x01, 0x01}));
}

// En un C2 el opcode es pkt[3] y el subcódigo, pkt[4]: la lista F4 06 como la deja FoxAnti.
TEST(DispatcherTest, TheSubcodeOfAC2IsAfterItsOpcode)
{
    DispatcherWithLog tested;
    Calls list;
    Calls request;
    ASSERT_TRUE(tested.dispatcher.On(ServerList, 0x06, list.Record()));
    ASSERT_TRUE(tested.dispatcher.On(ServerList, 0x02, request.Record()));
    const std::vector<std::byte> bytes = Bytes({0xC2, 0x00, 0x0B, 0xF4, 0x06, 0x00, 0x01, 0x90, 0x01, 0x00, 0x00});

    EXPECT_EQ(tested.dispatcher.Dispatch(PacketOf(bytes)), DispatchResult::Handled);

    EXPECT_TRUE(request.packets.empty());
    ASSERT_EQ(list.packets.size(), 1u);
    EXPECT_EQ(list.packets[0], bytes);
}

// Sin handler, Unknown cada vez, y una línea de aviso la primera vez de cada opcode: nombra el
// opcode y no trae los bytes del paquete (ni su tipo ni su tamaño).
TEST(DispatcherTest, AnUnknownOpcodeIsLoggedOnce)
{
    DispatcherWithLog tested;

    EXPECT_EQ(tested.dispatcher.Dispatch(PacketOf(Bytes({0xC1, 0x04, NoHandler, 0x00}))), DispatchResult::Unknown);
    EXPECT_EQ(tested.dispatcher.Dispatch(PacketOf(Bytes({0xC1, 0x04, NoHandler, 0x00}))), DispatchResult::Unknown);

    ASSERT_EQ(tested.sink.lines.size(), 1u);
    EXPECT_EQ(tested.sink.levels[0], LogLevel::Warning);
    EXPECT_TRUE(Contains(tested.sink.lines[0], "99")) << tested.sink.lines[0];
    EXPECT_FALSE(Contains(tested.sink.lines[0], "C1")) << tested.sink.lines[0];
    EXPECT_FALSE(Contains(tested.sink.lines[0], "04")) << tested.sink.lines[0];

    // Otro opcode sin handler tiene su propia línea.
    EXPECT_EQ(tested.dispatcher.Dispatch(PacketOf(Bytes({0xC1, 0x04, AnotherWithoutHandler, 0x00}))),
              DispatchResult::Unknown);
    ASSERT_EQ(tested.sink.lines.size(), 2u);
    EXPECT_TRUE(Contains(tested.sink.lines[1], "98")) << tested.sink.lines[1];
}

// Un subcódigo sin handler, en un opcode que tiene otros: una línea por opcode y subcódigo.
TEST(DispatcherTest, AnUnknownSubcodeIsLoggedOnce)
{
    DispatcherWithLog tested;
    Calls hello;
    ASSERT_TRUE(tested.dispatcher.On(Join, 0x00, hello.Record()));

    EXPECT_EQ(tested.dispatcher.Dispatch(PacketOf(Bytes({0xC1, 0x05, 0xF1, 0x7F, 0x00}))), DispatchResult::Unknown);
    EXPECT_EQ(tested.dispatcher.Dispatch(PacketOf(Bytes({0xC1, 0x05, 0xF1, 0x7F, 0x00}))), DispatchResult::Unknown);

    EXPECT_TRUE(hello.packets.empty());
    ASSERT_EQ(tested.sink.lines.size(), 1u);
    EXPECT_EQ(tested.sink.levels[0], LogLevel::Warning);
    EXPECT_TRUE(Contains(tested.sink.lines[0], "F1")) << tested.sink.lines[0];
    EXPECT_TRUE(Contains(tested.sink.lines[0], "7F")) << tested.sink.lines[0];
    EXPECT_FALSE(Contains(tested.sink.lines[0], "C1")) << tested.sink.lines[0];

    EXPECT_EQ(tested.dispatcher.Dispatch(PacketOf(Bytes({0xC1, 0x05, 0xF1, 0x7E, 0x00}))), DispatchResult::Unknown);
    ASSERT_EQ(tested.sink.lines.size(), 2u);
    EXPECT_TRUE(Contains(tested.sink.lines[1], "7E")) << tested.sink.lines[1];
}

// C1 03 F1 termina en el opcode: el original lee pkt[3] igual, un byte que no llegó (Q-C42).
TEST(DispatcherTest, AnOpcodeWithoutItsSubcodeIsMalformed)
{
    DispatcherWithLog tested;
    Calls hello;
    ASSERT_TRUE(tested.dispatcher.On(Join, 0x00, hello.Record()));

    EXPECT_EQ(tested.dispatcher.Dispatch(PacketOf(Bytes({0xC1, 0x03, 0xF1}))), DispatchResult::Malformed);

    EXPECT_TRUE(hello.packets.empty());
}

// En un C2 el subcódigo es pkt[4]: C2 00 04 F4 termina en el opcode, igual que el C1 de arriba.
TEST(DispatcherTest, AC2WithoutItsSubcodeIsMalformed)
{
    DispatcherWithLog tested;
    Calls list;
    ASSERT_TRUE(tested.dispatcher.On(ServerList, 0x06, list.Record()));

    EXPECT_EQ(tested.dispatcher.Dispatch(PacketOf(Bytes({0xC2, 0x00, 0x04, 0xF4}))), DispatchResult::Malformed);

    EXPECT_TRUE(list.packets.empty());
}

// Sin subcódigos, un paquete que termina en el opcode es un paquete entero: lo que sigue lo lee el
// handler, si lo necesita.
TEST(DispatcherTest, AnOpcodeWithoutSubcodesNeedsNoMoreBytes)
{
    DispatcherWithLog tested;
    Calls keepalive;
    ASSERT_TRUE(tested.dispatcher.On(Keepalive, keepalive.Record()));

    EXPECT_EQ(tested.dispatcher.Dispatch(PacketOf(Bytes({0xC1, 0x03, 0x0E}))), DispatchResult::Handled);

    EXPECT_EQ(keepalive.packets.size(), 1u);
}

// Un C3 llega cifrado: su opcode viaja adentro y la Connection lo descifra antes de despachar
// (CL4.3). El 0E del byte 2 no es un opcode.
TEST(DispatcherTest, AnEncryptedPacketIsMalformed)
{
    DispatcherWithLog tested;
    Calls keepalive;
    ASSERT_TRUE(tested.dispatcher.On(Keepalive, keepalive.Record()));

    EXPECT_EQ(tested.dispatcher.Dispatch(PacketOf(Bytes({0xC3, 0x05, 0x0E, 0x00, 0x00}))), DispatchResult::Malformed);

    EXPECT_TRUE(keepalive.packets.empty());
    EXPECT_TRUE(tested.sink.lines.empty());
}

// Registrar dos veces es un error de quien arma la tabla: el segundo On da false y no pisa al
// primero. Un opcode entero choca también con uno de sus subcódigos, en los dos órdenes.
TEST(DispatcherTest, RegisteringTwiceKeepsTheFirst)
{
    DispatcherWithLog tested;
    Calls first;
    Calls second;
    ASSERT_TRUE(tested.dispatcher.On(Keepalive, first.Record()));
    EXPECT_FALSE(tested.dispatcher.On(Keepalive, second.Record()));
    EXPECT_FALSE(tested.dispatcher.On(Keepalive, 0x00, second.Record()));

    EXPECT_EQ(tested.dispatcher.Dispatch(PacketOf(Bytes({0xC1, 0x04, 0x0E, 0x00}))), DispatchResult::Handled);
    EXPECT_EQ(first.packets.size(), 1u);
    EXPECT_TRUE(second.packets.empty());

    Calls login;
    ASSERT_TRUE(tested.dispatcher.On(Join, 0x01, login.Record()));
    EXPECT_FALSE(tested.dispatcher.On(Join, 0x01, second.Record()));
    EXPECT_FALSE(tested.dispatcher.On(Join, second.Record()));

    EXPECT_EQ(tested.dispatcher.Dispatch(PacketOf(Bytes({0xC1, 0x05, 0xF1, 0x01, 0x01}))), DispatchResult::Handled);
    EXPECT_EQ(login.packets.size(), 1u);
    EXPECT_TRUE(second.packets.empty());
}

// Un handler vacío no se registra: Dispatch lo llamaría y std::function tiraría bad_function_call.
TEST(DispatcherTest, AnEmptyHandlerIsNotRegistered)
{
    DispatcherWithLog tested;

    EXPECT_FALSE(tested.dispatcher.On(Keepalive, Client::Protocol::Handler{}));
    EXPECT_FALSE(tested.dispatcher.On(Join, 0x01, Client::Protocol::Handler{}));

    EXPECT_EQ(tested.dispatcher.Dispatch(PacketOf(Bytes({0xC1, 0x04, 0x0E, 0x00}))), DispatchResult::Unknown);
    EXPECT_EQ(tested.dispatcher.Dispatch(PacketOf(Bytes({0xC1, 0x05, 0xF1, 0x01, 0x01}))), DispatchResult::Unknown);
    // Y no deja nada a medias: después se pueden registrar, también el opcode entero de F1.
    Calls keepalive;
    Calls join;
    EXPECT_TRUE(tested.dispatcher.On(Keepalive, keepalive.Record()));
    EXPECT_TRUE(tested.dispatcher.On(Join, join.Record()));
}
} // namespace
