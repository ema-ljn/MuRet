// Client::Network::PacketSerials: los seriales de envío y de recepción de los C3/C4, con Q-C04.

#include "Network/PacketSerials.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace
{

using Client::Network::PacketSerials;
using Client::Network::SerialCheck;

// Cuántos valores tiene un serial de un byte.
constexpr int SerialCount = 256;

// Unos seriales que esperan `expected` en el próximo paquete que llega.
PacketSerials ExpectingReceive(std::uint8_t expected)
{
    PacketSerials serials;
    for (int serial = 0; serial < expected; ++serial)
    {
        EXPECT_EQ(serials.Receive(static_cast<std::uint8_t>(serial)), SerialCheck::InOrder);
    }
    return serials;
}

// El de envío sale desde 0 y sube de a uno; después de 255 vuelve a 0.
TEST(PacketSerialsTest, SendCountsFromZeroAndWraps)
{
    PacketSerials serials;

    for (int serial = 0; serial < SerialCount; ++serial)
    {
        ASSERT_EQ(serials.TakeSend(), serial);
    }
    EXPECT_EQ(serials.TakeSend(), 0);
}

// Q-C04: esperando el 5, llegan el 5, el 9 y el 10. El original, con el 9, se queda en 9 sin sumar
// 1, y el 10 tampoco coincide: dos "Decrypt error", y el desfase sigue (caso 5 de
// red_reference.py). Acá el 9 resincroniza, y el 10 ya está en orden.
TEST(PacketSerialsTest, ResyncsWithTheOneThatCame)
{
    PacketSerials serials = ExpectingReceive(5);

    EXPECT_EQ(serials.Receive(5), SerialCheck::InOrder);
    EXPECT_EQ(serials.Receive(9), SerialCheck::Resynced);
    EXPECT_EQ(serials.Receive(10), SerialCheck::InOrder);
    EXPECT_EQ(serials.Receive(11), SerialCheck::InOrder);
}

// Un paquete repetido también resincroniza: después del 3 se espera el 4, no el 7.
TEST(PacketSerialsTest, ARepeatedSerialResyncsBackwards)
{
    PacketSerials serials = ExpectingReceive(7);

    EXPECT_EQ(serials.Receive(3), SerialCheck::Resynced);
    EXPECT_EQ(serials.Receive(4), SerialCheck::InOrder);
}

// Después del 255 se espera el 0.
TEST(PacketSerialsTest, ReceiveWrapsAfter255)
{
    PacketSerials serials = ExpectingReceive(255);

    EXPECT_EQ(serials.Receive(255), SerialCheck::InOrder);
    EXPECT_EQ(serials.Receive(0), SerialCheck::InOrder);
}

// Un paquete que no se pudo descifrar gasta el serial que se esperaba, también el 255.
TEST(PacketSerialsTest, SkipReceiveExpectsTheNextOne)
{
    PacketSerials serials;
    serials.SkipReceive();
    EXPECT_EQ(serials.Receive(1), SerialCheck::InOrder);

    PacketSerials atTheEnd = ExpectingReceive(255);
    atTheEnd.SkipReceive();
    EXPECT_EQ(atTheEnd.Receive(0), SerialCheck::InOrder);
}

// Los dos van aparte: enviar no cambia el que se espera, ni recibir el que sale.
TEST(PacketSerialsTest, SendAndReceiveAreIndependent)
{
    PacketSerials serials;

    EXPECT_EQ(serials.TakeSend(), 0);
    EXPECT_EQ(serials.TakeSend(), 1);
    EXPECT_EQ(serials.Receive(0), SerialCheck::InOrder);
    serials.SkipReceive();
    EXPECT_EQ(serials.TakeSend(), 2);
    EXPECT_EQ(serials.Receive(2), SerialCheck::InOrder);
}

// Reset() vuelve a 0 los dos, como 0x4c1340 al conectar al GameServer.
TEST(PacketSerialsTest, ResetStartsBothOver)
{
    constexpr std::uint8_t ReceivedBefore = 40; // los que llegaron antes de Reset()
    constexpr int SentBefore = 9;               // los que salieron antes de Reset()
    PacketSerials serials = ExpectingReceive(ReceivedBefore);
    for (int k = 0; k < SentBefore; ++k)
    {
        [[maybe_unused]] const std::uint8_t sent = serials.TakeSend();
    }

    serials.Reset();

    EXPECT_EQ(serials.TakeSend(), 0);
    EXPECT_EQ(serials.Receive(0), SerialCheck::InOrder);
}

} // namespace
