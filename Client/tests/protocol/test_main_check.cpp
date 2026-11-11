#include "Protocol/Connection/MainCheck.h"

#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include "support/Bytes.h"
#include "support/ExpectedError.h"
#include "support/NetworkPrinters.h"
#include "support/Packets.h"
#include "support/ProtocolPrinters.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Client::Protocol::Connection, la suma del ejecutable: el 03 del GameServer y su respuesta (04, "La
// suma del ejecutable (03)"; Q-C319). Los valores esperados salen de una transliteración por
// registros de 0x4ad400 y 0x4ad3d0 (ASM c32bbd5f), aparte del código de MuRet.
namespace
{
using Client::Network::Delivery;
using Client::Protocol::Outgoing;
using Client::Protocol::ProtocolError;
using Client::Protocol::Connection::MainCheckFileName;
using Client::Protocol::Connection::MainCheckSum;
using Client::Protocol::Connection::MainCheckTable;
using Client::Protocol::Connection::ReadMainCheckKey;
using Client::Protocol::Connection::WriteMainCheckReply;
using Client::Tests::Bytes;
using Client::Tests::ErrorOf;
using Client::Tests::PacketOf;
using Client::Tests::SameBytes;

// 0x4e1846: la clave es el u16 de pkt[4..5]; pkt[3] es el relleno del struct del GS y no se mira.
TEST(MainCheckTest, ReadsTheKeyAfterThePadding)
{
    const auto key = ReadMainCheckKey(PacketOf(Bytes({0xC1, 0x06, 0x03, 0x5A, 0x79, 0xB4})));

    ASSERT_TRUE(key.HasValue());
    EXPECT_EQ(key.Value(), 0xB479);
}

// Sin la clave es TooShort (el original la lee igual, de su buffer), y otro opcode, WrongPacket.
TEST(MainCheckTest, OnlyAC103WithItsKeyIsAKey)
{
    EXPECT_EQ(ErrorOf(ReadMainCheckKey(PacketOf(Bytes({0xC1, 0x05, 0x03, 0x00, 0x79})))), ProtocolError::TooShort);
    EXPECT_EQ(ErrorOf(ReadMainCheckKey(PacketOf(Bytes({0xC1, 0x06, 0x0E, 0x00, 0x79, 0xB4})))),
              ProtocolError::WrongPacket);
}

// La clave que arma el GS (EncryptCheckSumKey: la tabla en los bits 0..3 y 6..15, seis bits de rand()
// en 4..9 y el XOR con 0xB479) vuelve siempre a su tabla, con cualquiera de los 64 rand().
TEST(MainCheckTest, TheTableComesBackFromEveryKeyOfTheGameServer)
{
    for (std::uint32_t table = 0; table < 1024; ++table)
    {
        for (std::uint32_t random = 0; random < 64; ++random)
        {
            const auto key = static_cast<std::uint16_t>(
                ((((table & 0x3F0U) << 6U) | (random << 4U) | (table & 0xFU)) ^ 0xB479U) & 0xFFFFU);
            ASSERT_EQ(MainCheckTable(key), table) << "tabla " << table << ", rand " << random;
        }
    }
}

// 0x4ad400 con 23 bytes de 0x10 en adelante: cinco u32, y los tres bytes que sobran no cuentan.
TEST(MainCheckTest, SumsTheImageByWordsWithItsTable)
{
    std::vector<std::byte> image;
    for (int value = 0x10; value < 0x10 + 23; ++value)
        image.push_back(static_cast<std::byte>(value));

    struct Case
    {
        std::uint16_t table;
        std::uint32_t sum;
    };
    for (const Case& probe : {Case{0x000, 0xA5387465U}, Case{0x001, 0xD0202DF7U}, Case{0x002, 0x7D9E1800U},
                              Case{0x155, 0x7D471C9AU}, Case{0x3FF, 0xA792CCF5U}})
    {
        SCOPED_TRACE(probe.table);
        const auto sum = MainCheckSum(image, probe.table);
        ASSERT_TRUE(sum.HasValue()) << sum.Error();
        EXPECT_EQ(sum.Value(), probe.sum);
    }

    // Los bytes de más, después del último u32 entero, no cambian la suma.
    std::vector<std::byte> words(image.begin(), image.begin() + 20);
    EXPECT_EQ(MainCheckSum(words, 0x155).Value(), 0x7D471C9AU);
}

// Los tres casos del % 3, con u32 que llevan el bit alto: el shl por u32 % 11 pierde lo que sale.
TEST(MainCheckTest, TheThreeStepsWrapIn32Bits)
{
    const std::vector<std::byte> image =
        Bytes({0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x00, 0x80});

    EXPECT_EQ(MainCheckSum(image, 0).Value(), 0x84000084U);
    EXPECT_EQ(MainCheckSum(image, 1).Value(), 0x73800DCAU);
    EXPECT_EQ(MainCheckSum(image, 2).Value(), 0x9AF02352U);
}

// Con menos de cuatro bytes, tamaño − 4 da la vuelta y el original lee después del buffer: MuRet
// corta nombrando la dirección (Q-C319).
TEST(MainCheckTest, AnImageShorterThanAWordIsAnError)
{
    const auto sum = MainCheckSum(Bytes({0x01, 0x02, 0x03}), 0);

    ASSERT_FALSE(sum.HasValue());
    EXPECT_NE(sum.Error().find("0x4ad418"), std::string::npos) << sum.Error();
}

// 0x4ad270: lo que sigue a la última barra invertida de toda la línea, cortado en el primer espacio,
// comilla, barra o barra invertida.
TEST(MainCheckTest, TakesTheFileNameFromTheCommandLine)
{
    // Sin literales crudos: el preprocesador de MSVC los lee mal dentro de los argumentos de EXPECT_EQ (C4129).
    EXPECT_EQ(MainCheckFileName("\"C:\\Juegos\\MU\\main.exe\" connect /u127.0.0.1 /p55901"), "main.exe");
    EXPECT_EQ(MainCheckFileName("C:\\Program Files\\MU\\Main.exe connect"), "Main.exe");
    EXPECT_EQ(MainCheckFileName("main.exe connect /u127.0.0.1 /p55901"), "main.exe");
    EXPECT_EQ(MainCheckFileName("\\main.exe"), "main.exe");
    // Una barra invertida en un argumento gana: el nombre sale de ahí.
    EXPECT_EQ(MainCheckFileName("C:\\MU\\Main.exe --data D:\\MU\\Data"), "Data");
    // Sin barras invertidas, la línea entera hasta la primera barra.
    EXPECT_EQ(MainCheckFileName("./Main --frames 1"), ".");
    EXPECT_EQ(MainCheckFileName("/opt/mu/Main"), "");
    EXPECT_EQ(MainCheckFileName(""), "");
}

// 0x4e16c0: C1 08 03, el 0 de relleno y la suma, con el XOR32 desde pkt[3] (el relleno sale como
// 0x03 ^ 0x89) y para cifrar.
TEST(MainCheckTest, WritesTheReply)
{
    const Outgoing reply = WriteMainCheckReply(0x9B613B0EU);

    EXPECT_EQ(reply.delivery, Delivery::Encrypted);
    ASSERT_EQ(reply.packet.Bytes().size(), 8U);
    EXPECT_EQ(reply.packet.Bytes()[3], std::byte{0x8A});
    const Client::Network::Packet clear = Client::Network::Crypto::Xor32Decode(reply.packet);
    EXPECT_TRUE(SameBytes(clear.Bytes(), Bytes({0xC1, 0x08, 0x03, 0x00, 0x0E, 0x3B, 0x61, 0x9B})));
}
} // namespace
