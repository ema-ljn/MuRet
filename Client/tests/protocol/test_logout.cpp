#include "Protocol/Connection/Logout.h"

#include "Protocol/ProtocolError.h"

#include "support/Bytes.h"
#include "support/ExpectedError.h"
#include "support/Packets.h"
#include "support/ProtocolPrinters.h"

#include <gtest/gtest.h>

// Client::Protocol::Connection, la salida: la respuesta F1 02 que manda el GameServer (04, "El
// flujo de conexión"; Q-C106).
namespace
{
using Client::Protocol::ProtocolError;
using Client::Protocol::Connection::LogoutQuitsTheGame;
using Client::Protocol::Connection::ReadLogoutResult;
using Client::Tests::Bytes;
using Client::Tests::ErrorOf;
using Client::Tests::PacketOf;

// El resultado sale como llegó: el 0 es el que cierra el juego, y el 1, el 2 y cualquier otro se
// leen igual, porque qué hacer con cada uno es de la sesión (0x4c2710 salta por el valor).
TEST(LogoutTest, ReadsTheLogoutResult)
{
    const auto quit = ReadLogoutResult(PacketOf(Bytes({0xC1, 0x05, 0xF1, 0x02, 0x00})));
    const auto select = ReadLogoutResult(PacketOf(Bytes({0xC1, 0x05, 0xF1, 0x02, 0x01})));
    const auto other = ReadLogoutResult(PacketOf(Bytes({0xC1, 0x05, 0xF1, 0x02, 0x07})));

    ASSERT_TRUE(quit.HasValue());
    EXPECT_EQ(quit.Value(), LogoutQuitsTheGame);
    ASSERT_TRUE(select.HasValue());
    EXPECT_EQ(select.Value(), 1);
    ASSERT_TRUE(other.HasValue());
    EXPECT_EQ(other.Value(), 7);
}

// Lo que sigue al resultado no se mira, como en el F1 01 (Q-C42).
TEST(LogoutTest, ALogoutResultWithTrailingBytesIsReadTheSame)
{
    const auto result = ReadLogoutResult(PacketOf(Bytes({0xC1, 0x06, 0xF1, 0x02, 0x00, 0x01})));

    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(result.Value(), LogoutQuitsTheGame);
}

// Sin el resultado es TooShort (el original lee pkt[4] igual, de su buffer), y otro F1 u otro
// opcode, WrongPacket.
TEST(LogoutTest, OnlyAWholeF102IsALogoutResult)
{
    EXPECT_EQ(ErrorOf(ReadLogoutResult(PacketOf(Bytes({0xC1, 0x04, 0xF1, 0x02})))), ProtocolError::TooShort);
    EXPECT_EQ(ErrorOf(ReadLogoutResult(PacketOf(Bytes({0xC1, 0x05, 0xF1, 0x01, 0x00})))), ProtocolError::WrongPacket);
    EXPECT_EQ(ErrorOf(ReadLogoutResult(PacketOf(Bytes({0xC1, 0x05, 0xF3, 0x02, 0x00})))), ProtocolError::WrongPacket);
}
} // namespace
