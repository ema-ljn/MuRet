#include "Protocol/CastleSiege.h"

#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include "support/Bytes.h"
#include "support/ExpectedError.h"
#include "support/Packets.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <utility>
#include <vector>

// Los paquetes del castillo fuera de la batalla (CL9 hito 12; 04 § Castle Siege, las ventanas del castillo). El
// GameServer 0.97 no tiene Castle Siege: cada byte sale del ASM del cliente (*estático*). Los nombres son inventados.
namespace
{
namespace Castle = Client::Protocol::CastleSiege;
using Client::Network::Delivery;
using Client::Network::Crypto::Xor32Decode;
using Client::Protocol::ProtocolError;
using Client::Tests::Bytes;
using Client::Tests::ErrorOf;
using Client::Tests::PacketOf;
using Client::Tests::SameBytes;

Castle::CastleSiegePacket Read(std::vector<std::byte> bytes)
{
    auto read = Castle::ReadCastleSiegePacket(PacketOf(std::move(bytes)));
    EXPECT_TRUE(read.HasValue());
    return read.HasValue() ? std::move(read).Value() : Castle::CastleSiegePacket{};
}

std::optional<ProtocolError> ErrorReading(std::vector<std::byte> bytes)
{
    return ErrorOf(Castle::ReadCastleSiegePacket(PacketOf(std::move(bytes))));
}

template <std::size_t Size> std::array<std::byte, Size> Name(std::initializer_list<int> values)
{
    std::array<std::byte, Size> name{};
    std::size_t i = 0;
    for (const int value : values)
        name.at(i++) = static_cast<std::byte>(value);
    return name;
}

// B2 00 con 1 (0x4e50f2 → 0x64b0f0): pkt[5] con signo, las tres fechas de 6 bytes (el año arriba), el gremio de
// pkt[0x18], el maestro de pkt[0x20] y lo que falta, pkt[0x2a..0x2d] con el primero arriba.
std::vector<std::byte> StatePacket(int result)
{
    return Bytes({0xc1, 0x2e, 0xb2, 0x00, result, 0xfe,                     // estado −2
                  0x07, 0xea, 0x0b, 0x0e, 0x14,   0x1e,                     // 2026, 11, 14, 20, 30
                  0x07, 0xeb, 0x01, 0x02, 0x03,   0x04,                     //
                  0x00, 0x01, 0xff, 0x80, 0x7f,   0x00,                     //
                  'R',  'o',  'c',  'a',  0,      'x',  'y', 'z',           // el gremio, con basura detrás del 0
                  'L',  'e',  'c',  'h',  'u',    'z',  'a', 'a', 'b', 'c', // el maestro, los 10 llenos
                  0x01, 0x02, 0x03, 0x04});
}

TEST(CastleSiegeProtocolTest, StateWithOneReadsEveryFieldAtItsOffset)
{
    const auto state = std::get<Castle::CastleState>(Read(StatePacket(1)));
    EXPECT_EQ(state.result, 1);
    EXPECT_EQ(state.state, -2);
    EXPECT_EQ(state.dates[0].year, 0x07ea);
    EXPECT_EQ(state.dates[0].rest, (std::array<std::uint8_t, 4>{0x0b, 0x0e, 0x14, 0x1e}));
    EXPECT_EQ(state.dates[1].year, 0x07eb);
    EXPECT_EQ(state.dates[1].rest, (std::array<std::uint8_t, 4>{1, 2, 3, 4}));
    EXPECT_EQ(state.dates[2].year, 0x0001);
    EXPECT_EQ(state.dates[2].rest, (std::array<std::uint8_t, 4>{0xff, 0x80, 0x7f, 0x00}));
    EXPECT_EQ(state.ownerGuild, (Name<8>({'R', 'o', 'c', 'a', 0, 'x', 'y', 'z'})));
    EXPECT_EQ(state.ownerMaster, (Name<10>({'L', 'e', 'c', 'h', 'u', 'z', 'a', 'a', 'b', 'c'})));
    EXPECT_EQ(state.remaining, 0x01020304u);
}

TEST(CastleSiegeProtocolTest, StateWithTwoReadsTheSameFields)
{
    const auto state = std::get<Castle::CastleState>(Read(StatePacket(2)));
    EXPECT_EQ(state.result, 2);
    EXPECT_EQ(state.remaining, 0x01020304u);
}

TEST(CastleSiegeProtocolTest, StateWithOtherResultsReadsOnlyTheResult)
{
    for (const int result : {0, 3, 4, 5, 0xff})
    {
        const auto state = std::get<Castle::CastleState>(Read(Bytes({0xc1, 5, 0xb2, 0, result})));
        EXPECT_EQ(state.result, result);
        EXPECT_EQ(state.state, 0);
    }
}

TEST(CastleSiegeProtocolTest, StateWithOneAndOneByteLessIsTooShort)
{
    auto bytes = StatePacket(1);
    bytes.pop_back();
    bytes[1] = std::byte{0x2d};
    EXPECT_EQ(ErrorReading(bytes), ProtocolError::TooShort);
    EXPECT_EQ(ErrorReading(Bytes({0xc1, 4, 0xb2, 0})), ProtocolError::TooShort);
}

// En un C2 el subcódigo es pkt[4] (0x4e509c), y el resultado que lee cada caso, también pkt[4]: el mismo byte.
TEST(CastleSiegeProtocolTest, InAC2TheResultIsTheSubcodeItself)
{
    EXPECT_EQ(std::get<Castle::CastleState>(Read(Bytes({0xc2, 0, 6, 0xb2, 0, 1}))).result, 0);
    EXPECT_EQ(std::get<Castle::RegisterResult>(Read(Bytes({0xc2, 0, 6, 0xb2, 1, 7}))).result, 1);
    EXPECT_EQ(std::get<Castle::GiveUpResult>(Read(Bytes({0xc2, 0, 5, 0xb2, 2}))).result, 2);
    // B2 10 en un C2: pkt[4] es 0x10, ni 0, ni 1, ni 2.
    EXPECT_EQ(std::get<Castle::MoneyOutResult>(Read(Bytes({0xc2, 0, 5, 0xb2, 0x10}))).result, 0x10);
}

TEST(CastleSiegeProtocolTest, RegisterAndGiveUpReadTheResult)
{
    EXPECT_EQ(std::get<Castle::RegisterResult>(Read(Bytes({0xc1, 5, 0xb2, 1, 8}))).result, 8);
    EXPECT_EQ(std::get<Castle::GiveUpResult>(Read(Bytes({0xc1, 5, 0xb2, 2, 3}))).result, 3);
    EXPECT_EQ(ErrorReading(Bytes({0xc1, 4, 0xb2, 1})), ProtocolError::TooShort);
}

// B2 03 (0x4e5186): con 1, las marcas de pkt[0xd..0x10] con el primero arriba y pkt[0x11].
TEST(CastleSiegeProtocolTest, RegistrationWithOneReadsTheMarksAndTheGiveUpByte)
{
    const auto info = std::get<Castle::RegistrationInfo>(
        Read(Bytes({0xc1, 0x12, 0xb2, 3, 1, 9, 9, 9, 9, 9, 9, 9, 9, 0x00, 0x00, 0x01, 0x2c, 1})));
    EXPECT_EQ(info.result, 1);
    EXPECT_EQ(info.marks, 300u);
    EXPECT_EQ(info.gaveUp, 1);
    EXPECT_EQ(ErrorReading(Bytes({0xc1, 0x11, 0xb2, 3, 1, 9, 9, 9, 9, 9, 9, 9, 9, 0, 0, 1, 0x2c})),
              ProtocolError::TooShort);
    EXPECT_EQ(std::get<Castle::RegistrationInfo>(Read(Bytes({0xc1, 5, 0xb2, 3, 2}))).result, 2);
}

TEST(CastleSiegeProtocolTest, RegisterMarkWithOneReadsTheMarks)
{
    const auto mark = std::get<Castle::RegisterMarkResult>(
        Read(Bytes({0xc1, 0x11, 0xb2, 4, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0x12, 0x34, 0x56, 0x78})));
    EXPECT_EQ(mark.marks, 0x12345678u);
    EXPECT_EQ(ErrorReading(Bytes({0xc1, 0x10, 0xb2, 4, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0x12, 0x34, 0x56})),
              ProtocolError::TooShort);
    EXPECT_EQ(std::get<Castle::RegisterMarkResult>(Read(Bytes({0xc1, 5, 0xb2, 4, 3}))).result, 3);
}

// B2 05 (0x4df806): el tipo de pkt+8 y el índice de pkt+0xc, dwords con el byte de abajo primero.
TEST(CastleSiegeProtocolTest, NpcBuyWithOneReadsTypeAndIndex)
{
    const auto buy = std::get<Castle::NpcBuyResult>(
        Read(Bytes({0xc1, 0x10, 0xb2, 5, 1, 0, 0, 0, 0x15, 0x01, 0, 0, 0x03, 0, 0, 0x80})));
    EXPECT_EQ(buy.type, 0x115u);
    EXPECT_EQ(buy.index, 0x80000003u);
    EXPECT_EQ(ErrorReading(Bytes({0xc1, 0x0f, 0xb2, 5, 1, 0, 0, 0, 0x15, 0x01, 0, 0, 0x03, 0, 0})),
              ProtocolError::TooShort);
    EXPECT_EQ(std::get<Castle::NpcBuyResult>(Read(Bytes({0xc1, 5, 0xb2, 5, 4}))).result, 4);
}

// B2 06 (0x4e52ec): la vida de pkt+0x10 y la máxima de pkt+0x14.
TEST(CastleSiegeProtocolTest, NpcRepairWithOneReadsBothLives)
{
    const auto repair = std::get<Castle::NpcRepairResult>(
        Read(Bytes({0xc1, 0x18, 0xb2, 6, 1, 0, 0, 0, 0x1b, 1, 0, 0, 2, 0, 0, 0, 0x10, 0x27, 0, 0, 0x20, 0x4e, 0, 0})));
    EXPECT_EQ(repair.type, 0x11bu);
    EXPECT_EQ(repair.index, 2u);
    EXPECT_EQ(repair.life, 10000u);
    EXPECT_EQ(repair.maxLife, 20000u);
    EXPECT_EQ(ErrorReading(
                  Bytes({0xc1, 0x17, 0xb2, 6, 1, 0, 0, 0, 0x1b, 1, 0, 0, 2, 0, 0, 0, 0x10, 0x27, 0, 0, 0x20, 0x4e, 0})),
              ProtocolError::TooShort);
}

// B2 07 (0x4df983): cuál en pkt+0x10; el valor de pkt+0x14 solo lo leen el 1, el 2 y el 3.
TEST(CastleSiegeProtocolTest, NpcUpgradeReadsTheValueOnlyForItsThreeKinds)
{
    const auto upgrade = std::get<Castle::NpcUpgradeResult>(
        Read(Bytes({0xc1, 0x18, 0xb2, 7, 1, 0, 0, 0, 0x15, 1, 0, 0, 1, 0, 0, 0, 3, 0, 0, 0, 0xa0, 0x86, 0x01, 0x00})));
    EXPECT_EQ(upgrade.type, 0x115u);
    EXPECT_EQ(upgrade.index, 1u);
    EXPECT_EQ(upgrade.kind, 3u);
    EXPECT_EQ(upgrade.value, 100000u);
    const auto other = std::get<Castle::NpcUpgradeResult>(
        Read(Bytes({0xc1, 0x14, 0xb2, 7, 1, 0, 0, 0, 0x15, 1, 0, 0, 1, 0, 0, 0, 4, 0, 0, 0})));
    EXPECT_EQ(other.kind, 4u);
    EXPECT_EQ(other.value, 0u);
    EXPECT_EQ(ErrorReading(Bytes({0xc1, 0x14, 0xb2, 7, 1, 0, 0, 0, 0x15, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0})),
              ProtocolError::TooShort);
    EXPECT_EQ(std::get<Castle::NpcUpgradeResult>(Read(Bytes({0xc1, 5, 0xb2, 7, 7}))).result, 7);
}

// B2 08 (0x669070): los impuestos de pkt[5] y pkt[6] y la plata de pkt[7..0xe] con el primero arriba.
TEST(CastleSiegeProtocolTest, TaxInfoWithOneReadsTheTaxesAndTheMoney)
{
    const auto info = std::get<Castle::TaxInfo>(
        Read(Bytes({0xc1, 0x0f, 0xb2, 8, 1, 3, 7, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08})));
    EXPECT_EQ(info.chaosTax, 3);
    EXPECT_EQ(info.storeTax, 7);
    EXPECT_EQ(info.money, 0x0102030405060708u);
    EXPECT_EQ(ErrorReading(Bytes({0xc1, 0x0e, 0xb2, 8, 1, 3, 7, 1, 2, 3, 4, 5, 6, 7})), ProtocolError::TooShort);
}

// B2 09 (0x4e53e7): con 1, cuál en pkt[5] y el valor de pkt[6..9] con el primero arriba para el 1, el 2 y el 3.
TEST(CastleSiegeProtocolTest, TaxRateReadsTheValueOnlyForItsThreeKinds)
{
    const auto fee = std::get<Castle::TaxRateResult>(Read(Bytes({0xc1, 0x0a, 0xb2, 9, 1, 3, 0x00, 0x01, 0x86, 0xa0})));
    EXPECT_EQ(fee.kind, 3);
    EXPECT_EQ(fee.value, 100000u);
    const auto other = std::get<Castle::TaxRateResult>(Read(Bytes({0xc1, 6, 0xb2, 9, 1, 4})));
    EXPECT_EQ(other.kind, 4);
    EXPECT_EQ(other.value, 0u);
    EXPECT_EQ(ErrorReading(Bytes({0xc1, 9, 0xb2, 9, 1, 1, 0, 0, 0})), ProtocolError::TooShort);
}

TEST(CastleSiegeProtocolTest, MoneyOutWithOneReadsTheMoney)
{
    const auto out =
        std::get<Castle::MoneyOutResult>(Read(Bytes({0xc1, 0x0d, 0xb2, 0x10, 1, 0, 0, 0, 0, 0x3b, 0x9a, 0xca, 0x00})));
    EXPECT_EQ(out.money, 1000000000u);
    EXPECT_EQ(ErrorReading(Bytes({0xc1, 0x0c, 0xb2, 0x10, 1, 0, 0, 0, 0, 0x3b, 0x9a, 0xca})), ProtocolError::TooShort);
}

// B2 1A (0x4e54f6) y B2 1F (0x4e5532): pkt[5] solo con pkt[4] en 1 o 2, o en 1.
TEST(CastleSiegeProtocolTest, FlagsAndHuntZoneGateReadTheirByte)
{
    const auto flags = std::get<Castle::SiegeFlags>(Read(Bytes({0xc1, 6, 0xb2, 0x1a, 2, 0x81})));
    EXPECT_EQ(flags.which, 2);
    EXPECT_EQ(flags.value, 0x81);
    EXPECT_EQ(std::get<Castle::SiegeFlags>(Read(Bytes({0xc1, 5, 0xb2, 0x1a, 3}))).which, 3);
    EXPECT_EQ(ErrorReading(Bytes({0xc1, 5, 0xb2, 0x1a, 1})), ProtocolError::TooShort);
    const auto gate = std::get<Castle::HuntZoneGateResult>(Read(Bytes({0xc1, 6, 0xb2, 0x1f, 1, 0x40})));
    EXPECT_EQ(gate.allowed, 0x40);
    EXPECT_EQ(std::get<Castle::HuntZoneGateResult>(Read(Bytes({0xc1, 5, 0xb2, 0x1f, 0}))).result, 0);
}

// Los B2 de la batalla (hito 13) y los del default (0x0A..0x0F, 0x1B, 0x1C) no los lee el hito 12.
TEST(CastleSiegeProtocolTest, FightAndDefaultSubcodesAreWrongPackets)
{
    for (const int subcode : {0x0a, 0x0f, 0x11, 0x14, 0x17, 0x19, 0x1b, 0x1c, 0x1d, 0x1e, 0x20, 0x21})
        EXPECT_EQ(ErrorReading(Bytes({0xc1, 5, 0xb2, subcode, 1})), ProtocolError::WrongPacket) << subcode;
    EXPECT_EQ(ErrorReading(Bytes({0xc1, 5, 0xb7, 1, 1})), ProtocolError::WrongPacket);
    EXPECT_EQ(ErrorReading(Bytes({0xc1, 5, 0xb9, 2, 1})), ProtocolError::WrongPacket);
    EXPECT_EQ(ErrorReading(Bytes({0xc1, 3, 0xb2})), ProtocolError::TooShort);
}

// B3 (0x4dfc60): la cuenta, un int en pkt+8, y los registros de 0x1c desde pkt+0xc, enteros.
TEST(CastleSiegeProtocolTest, NpcListReadsTheRecordsWhole)
{
    std::vector<std::byte> bytes = Bytes({0xc1, 0x44, 0xb3, 1, 1, 0, 0, 0, 2, 0, 0, 0});
    for (int record = 0; record < 2; ++record)
    {
        for (int i = 0; i < 0x1c; ++i)
            bytes.push_back(static_cast<std::byte>(record * 0x40 + i));
    }
    const auto list = std::get<Castle::NpcList>(Read(bytes));
    ASSERT_EQ(list.records.size(), 2u);
    EXPECT_EQ(list.records[0][0], std::byte{0x00});
    EXPECT_EQ(list.records[0][0x1b], std::byte{0x1b});
    EXPECT_EQ(list.records[1][0], std::byte{0x40});
    EXPECT_EQ(list.records[1][0x1b], std::byte{0x5b});
    bytes[8] = std::byte{3};
    EXPECT_EQ(ErrorReading(bytes), ProtocolError::TooShort);
}

// JLE/JL con signo: una cuenta en 0 o negativa no corre el lazo.
TEST(CastleSiegeProtocolTest, ListsWithACountOfZeroOrNegativeAreEmpty)
{
    EXPECT_TRUE(std::get<Castle::NpcList>(Read(Bytes({0xc1, 0x0c, 0xb3, 1, 1, 0, 0, 0, 0xff, 0xff, 0xff, 0xff})))
                    .records.empty());
    EXPECT_TRUE(std::get<Castle::SiegeGuildList>(Read(Bytes({0xc1, 0x0c, 0xb4, 0, 1, 0, 0, 0, 0, 0, 0, 0x80})))
                    .entries.empty());
    EXPECT_TRUE(std::get<Castle::RegisteredGuildList>(Read(Bytes({0xc1, 0x0c, 0xb5, 0, 1, 0, 0, 0, 0, 0, 0, 0})))
                    .entries.empty());
    EXPECT_TRUE(
        std::get<Castle::MinimapPoints>(Read(Bytes({0xc1, 8, 0xb6, 0, 0xfe, 0xff, 0xff, 0xff}))).points.empty());
    EXPECT_EQ(std::get<Castle::NpcList>(Read(Bytes({0xc1, 5, 0xb3, 1, 2}))).result, 2);
    EXPECT_EQ(ErrorReading(Bytes({0xc1, 0x0b, 0xb3, 1, 1, 0, 0, 0, 0, 0, 0})), ProtocolError::TooShort);
}

// B4 (0x4dfce0): registros de 0xe desde pkt+0xc, el nombre, las marcas con el primero arriba y dos bytes.
TEST(CastleSiegeProtocolTest, SiegeGuildListReadsEachEntryAsItGoesTo0x64d2c0)
{
    const auto list = std::get<Castle::SiegeGuildList>(Read(Bytes({
        0xc1, 0x28, 0xb4, 0,   1,   0,   0,   0,   2,    0,    0,    0,          //
        'P',  'i',  'n',  'o', 0,   0,   0,   0,   0x00, 0x00, 0x01, 0x00, 1, 0, //
        'A',  'b',  'e',  'd', 'u', 'l', 'S', 'X', 0x00, 0x0f, 0x42, 0x40, 0, 7, //
    })));
    ASSERT_EQ(list.entries.size(), 2u);
    EXPECT_EQ(list.entries[0].name, (Name<8>({'P', 'i', 'n', 'o'})));
    EXPECT_EQ(list.entries[0].marks, 256u);
    EXPECT_EQ(list.entries[0].first, 1);
    EXPECT_EQ(list.entries[0].second, 0);
    EXPECT_EQ(list.entries[1].name, (Name<8>({'A', 'b', 'e', 'd', 'u', 'l', 'S', 'X'})));
    EXPECT_EQ(list.entries[1].marks, 1000000u);
    EXPECT_EQ(list.entries[1].second, 7);
    EXPECT_EQ(ErrorReading(
                  Bytes({0xc1, 0x19, 0xb4, 0, 1, 0, 0, 0, 1, 0, 0, 0, 'P', 'i', 'n', 'o', 0, 0, 0, 0, 0, 0, 1, 0, 1})),
              ProtocolError::TooShort);
}

// B5 (0x4dfd80): registros de 10 desde pkt+0xc, dos bytes y el nombre.
TEST(CastleSiegeProtocolTest, RegisteredGuildListReadsEachEntryAsItGoesTo0x64d310)
{
    const auto list = std::get<Castle::RegisteredGuildList>(Read(Bytes({
        0xc1, 0x20, 0xb5, 0,   1,   0,   0,   0,   2,   0, 0, 0, //
        1,    0,    'C',  'e', 'i', 'b', 'o', 0,   0,   0,       //
        0,    1,    'T',  'a', 'l', 'a', 'r', 'e', 's', 0,       //
    })));
    ASSERT_EQ(list.entries.size(), 2u);
    EXPECT_EQ(list.entries[0].first, 1);
    EXPECT_EQ(list.entries[0].second, 0);
    EXPECT_EQ(list.entries[0].name, (Name<8>({'C', 'e', 'i', 'b', 'o'})));
    EXPECT_EQ(list.entries[1].second, 1);
    EXPECT_EQ(list.entries[1].name, (Name<8>({'T', 'a', 'l', 'a', 'r', 'e', 's'})));
    EXPECT_EQ(std::get<Castle::RegisteredGuildList>(Read(Bytes({0xc1, 5, 0xb5, 0, 3}))).result, 3);
}

// B6 (0x4e0ed0): la cuenta en pkt+4 y los pares desde pkt+8.
TEST(CastleSiegeProtocolTest, MinimapPointsReadThePairs)
{
    const auto points =
        std::get<Castle::MinimapPoints>(Read(Bytes({0xc1, 0x0c, 0xb6, 0, 2, 0, 0, 0, 0x10, 0x20, 0xc8, 0x05})));
    ASSERT_EQ(points.points.size(), 2u);
    EXPECT_EQ(points.points[0].x, 0x10);
    EXPECT_EQ(points.points[0].y, 0x20);
    EXPECT_EQ(points.points[1].x, 0xc8);
    EXPECT_EQ(points.points[1].y, 0x05);
    EXPECT_EQ(ErrorReading(Bytes({0xc1, 0x0b, 0xb6, 0, 2, 0, 0, 0, 0x10, 0x20, 0xc8})), ProtocolError::TooShort);
}

// B8 (0x4e5883): solo el subcódigo 1, y el valor de pkt[4], que en un C2 es el mismo subcódigo.
TEST(CastleSiegeProtocolTest, KillCountReadsOnlyTheSubcodeOne)
{
    EXPECT_EQ(std::get<Castle::KillCount>(Read(Bytes({0xc1, 5, 0xb8, 1, 9}))).value, 9);
    EXPECT_EQ(std::get<Castle::KillCount>(Read(Bytes({0xc2, 0, 5, 0xb8, 1}))).value, 1);
    EXPECT_EQ(ErrorReading(Bytes({0xc1, 5, 0xb8, 2, 9})), ProtocolError::WrongPacket);
    EXPECT_EQ(ErrorReading(Bytes({0xc1, 4, 0xb8, 1})), ProtocolError::TooShort);
}

struct Written
{
    const char* name;
    Client::Protocol::Outgoing packet;
    std::vector<std::byte> clear;
    std::vector<std::byte> sent;
};

// Lo que mandan las ventanas: XOR32 desde el byte 3, con la tabla de 0x6b85f4 en la pila, y send() directo.
TEST(CastleSiegeProtocolTest, EveryWriterSendsItsBytesWithXor32)
{
    const std::vector<Written> written{
        {"0x64c0b0", Castle::WriteRegister(), Bytes({0xc1, 4, 0xb2, 1}), Bytes({0xc1, 4, 0xb2, 0x3a})},
        {"0x64ccd0", Castle::WriteGiveUp(), Bytes({0xc1, 5, 0xb2, 2, 1}), Bytes({0xc1, 5, 0xb2, 0x39, 0x84})},
        {"0x64ae10", Castle::WriteRegistrationRequest(), Bytes({0xc1, 4, 0xb2, 3}), Bytes({0xc1, 4, 0xb2, 0x38})},
        {"0x64b9d0", Castle::WriteRegisterMark(0x0c), Bytes({0xc1, 5, 0xb2, 4, 0x0c}),
         Bytes({0xc1, 5, 0xb2, 0x3f, 0x8f})},
        {"0x6623e0", Castle::WriteNpcBuy(0x115, 3), Bytes({0xc1, 0x0c, 0xb2, 5, 0x15, 1, 0, 0, 3, 0, 0, 0}),
         Bytes({0xc1, 0x0c, 0xb2, 0x3e, 0x97, 0x24, 0xbb, 0xc8, 0xe8, 0x40, 0xbe, 0x08})},
        {"0x664370", Castle::WriteNpcRepair(0x11b, 2), Bytes({0xc1, 0x0c, 0xb2, 6, 0x1b, 1, 0, 0, 2, 0, 0, 0}),
         Bytes({0xc1, 0x0c, 0xb2, 0x3d, 0x9a, 0x29, 0xb6, 0xc5, 0xe4, 0x4c, 0xb2, 0x04})},
        {"0x662d50", Castle::WriteNpcUpgrade(0x115, 1, 3, 100000),
         Bytes({0xc1, 0x14, 0xb2, 7, 0x15, 1, 0, 0, 1, 0, 0, 0, 3, 0, 0, 0, 0xa0, 0x86, 1, 0}),
         Bytes({0xc1, 0x14, 0xb2, 0x3c, 0x95, 0x26, 0xb9, 0xca, 0xe8, 0x40,
                0xbe, 0x08, 0x42, 0x1f, 0x26, 0x7b, 0x51, 0x1c, 0x7e, 0xf3})},
        {"0x661310", Castle::WriteTaxInfoRequest(), Bytes({0xc1, 4, 0xb2, 8}), Bytes({0xc1, 4, 0xb2, 0x33})},
        {"0x666b30", Castle::WriteTaxRate(1, 5), Bytes({0xc1, 9, 0xb2, 9, 1, 0, 0, 0, 5}),
         Bytes({0xc1, 9, 0xb2, 0x32, 0x8f, 0x3d, 0xa2, 0xd1, 0xf7})},
        {"0x648a30", Castle::WriteTaxRate(3, 100000), Bytes({0xc1, 9, 0xb2, 9, 3, 0, 1, 0x86, 0xa0}),
         Bytes({0xc1, 9, 0xb2, 0x32, 0x8d, 0x3f, 0xa1, 0x54, 0xd7})},
        {"0x667a40", Castle::WriteMoneyOut(1000000), Bytes({0xc1, 8, 0xb2, 0x10, 0x40, 0x42, 0x0f, 0}),
         Bytes({0xc1, 8, 0xb2, 0x2b, 0xd7, 0x27, 0xb7, 0xc4})},
        {"0x648c55", Castle::WriteHuntZoneGate(1), Bytes({0xc1, 5, 0xb2, 0x1f, 1}), Bytes({0xc1, 5, 0xb2, 0x24, 0x99})},
        {"0x661310 (1)", Castle::WriteNpcListRequest(1), Bytes({0xc1, 4, 0xb3, 1}), Bytes({0xc1, 4, 0xb3, 0x3b})},
        {"0x661310 (2)", Castle::WriteNpcListRequest(2), Bytes({0xc1, 4, 0xb3, 2}), Bytes({0xc1, 4, 0xb3, 0x38})},
        {"0x64d350 (B4)", Castle::WriteSiegeGuildListRequest(), Bytes({0xc1, 3, 0xb4}), Bytes({0xc1, 3, 0xb4})},
        {"0x64d350 (B5)", Castle::WriteRegisteredGuildListRequest(), Bytes({0xc1, 3, 0xb5}), Bytes({0xc1, 3, 0xb5})},
        {"0x648a30 (B9 05)", Castle::WriteHuntZoneEntry(10000), Bytes({0xc1, 8, 0xb9, 5, 0x10, 0x27, 0, 0}),
         Bytes({0xc1, 8, 0xb9, 0x35, 0x99, 0x0c, 0x93, 0xe0})},
    };
    for (const Written& each : written)
    {
        SCOPED_TRACE(each.name);
        EXPECT_EQ(each.packet.delivery, Delivery::Clear);
        EXPECT_TRUE(SameBytes(each.packet.packet.Bytes(), each.sent));
        EXPECT_TRUE(SameBytes(Xor32Decode(PacketOf(each.sent)).Bytes(), each.clear));
    }
}
} // namespace
