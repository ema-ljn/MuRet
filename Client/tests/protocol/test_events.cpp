#include "Protocol/Events.h"

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
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// CL9 hito 11, 04 § Los eventos: lo que el dispatcher 0x4e2b10 y sus handlers leen de cada paquete de los eventos,
// con la cabecera C1 (pkt[0] es el 0xc1), y los dos C→S (el C1 03 97 de 0x56b4e8 y el AF 02 de 0x4fbee8). Los
// vectores salen del ASM de c32bbd5f y de los PMSG_* del GS reconstruido, sin ejecutar el original.
namespace
{
namespace Events = Client::Protocol::Events;
using Client::Network::Delivery;
using Client::Network::Crypto::Xor32Decode;
using Client::Protocol::ProtocolError;
using Client::Tests::Bytes;
using Client::Tests::ErrorOf;
using Client::Tests::PacketOf;
using Client::Tests::SameBytes;

template <typename T> T Read(std::vector<std::byte> bytes)
{
    const auto read = Events::ReadEventPacket(PacketOf(std::move(bytes)));
    EXPECT_TRUE(read.HasValue());
    if (!read.HasValue())
        return T{};
    const T* packet = std::get_if<T>(&read.Value());
    EXPECT_NE(packet, nullptr);
    return packet != nullptr ? *packet : T{};
}

// 0x4e3594: PMSG_EVENTSTATE, C1 05 0B estado evento.
TEST(EventsProtocolTest, ReadsTheEventState)
{
    const auto state = Read<Events::EventState>(Bytes({0xC1, 0x05, 0x0B, 0x01, 0x03}));
    EXPECT_EQ(state.state, 1);
    EXPECT_EQ(state.event, 3);
}

// 0x4dab90: el tipo 0 lee rectángulos de 4 bytes y el 1 celdas de 2, desde pkt[7]; otro tipo no lee nada más.
TEST(EventsProtocolTest, ReadsTheMapAttributesByType)
{
    const auto areas = Read<Events::MapAttributes>(
        Bytes({0xC1, 0x0F, 0x46, 0x00, 0x08, 0x01, 0x02, 13, 15, 15, 23, 0x0D, 0x4C, 0x0F, 0x50}));
    EXPECT_EQ(areas.type, 0);
    EXPECT_EQ(areas.attribute, 8);
    EXPECT_EQ(areas.mode, 1);
    ASSERT_EQ(areas.areas.size(), 2u);
    EXPECT_TRUE(areas.cells.empty());
    EXPECT_EQ(areas.areas[0].x1, 13);
    EXPECT_EQ(areas.areas[0].y1, 15);
    EXPECT_EQ(areas.areas[0].x2, 15);
    EXPECT_EQ(areas.areas[0].y2, 23);
    EXPECT_EQ(areas.areas[1].x1, 0x0D);
    EXPECT_EQ(areas.areas[1].y2, 0x50);

    const auto cells = Read<Events::MapAttributes>(Bytes({0xC1, 0x0B, 0x46, 0x01, 0x04, 0x00, 0x02, 10, 11, 12, 13}));
    EXPECT_EQ(cells.type, 1);
    ASSERT_EQ(cells.cells.size(), 2u);
    EXPECT_TRUE(cells.areas.empty());
    EXPECT_EQ(cells.cells[1].x, 12);
    EXPECT_EQ(cells.cells[1].y, 13);

    const auto other = Read<Events::MapAttributes>(Bytes({0xC1, 0x07, 0x46, 0x02, 0x04, 0x00, 0x09}));
    EXPECT_EQ(other.count, 9);
    EXPECT_TRUE(other.cells.empty());
    EXPECT_TRUE(other.areas.empty());

    EXPECT_EQ(ErrorOf(Events::ReadEventPacket(PacketOf(Bytes({0xC1, 0x0A, 0x46, 0x00, 0x08, 0x01, 0x02, 1, 2, 3})))),
              ProtocolError::TooShort);
}

// 0x4d6990: la clave big-endian de pkt[3..4] (0x4d69a1..0x4d69aa), el dword de pkt[8] y pkt[0xc].
TEST(EventsProtocolTest, ReadsTheItemAcquiredKeyBigEndian)
{
    const auto item = Read<Events::ItemAcquired>(
        Bytes({0xC1, 0x0D, 0x47, 0x12, 0x34, 0x00, 0x00, 0x00, 0x78, 0x56, 0x34, 0x12, 0x07}));
    EXPECT_EQ(item.key, 0x1234);
    EXPECT_EQ(item.item, 0x12345678u);
    EXPECT_EQ(item.level, 7);
}

// 0x4da8c0: el 1 y el 2 leen pkt[4]; el 4, pkt[4..6] (que el GS no manda: 08); los demás, solo el tipo.
TEST(EventsProtocolTest, ReadsTheOpenTimeByKind)
{
    const auto one = Read<Events::EventOpenTime>(Bytes({0xC1, 0x05, 0x91, 0x01, 0x0F}));
    EXPECT_EQ(one.kind, 1);
    EXPECT_EQ(one.value4, 0x0F);
    const auto four = Read<Events::EventOpenTime>(Bytes({0xC1, 0x07, 0x91, 0x04, 0x01, 0x05, 0x02}));
    EXPECT_EQ(four.value4, 1);
    EXPECT_EQ(four.value5, 5);
    EXPECT_EQ(four.value6, 2);
    const auto three = Read<Events::EventOpenTime>(Bytes({0xC1, 0x04, 0x91, 0x03}));
    EXPECT_EQ(three.kind, 3);
    EXPECT_EQ(three.value4, 0);
    EXPECT_EQ(ErrorOf(Events::ReadEventPacket(PacketOf(Bytes({0xC1, 0x05, 0x91, 0x04, 0x01})))),
              ProtocolError::TooShort);
}

// 0x4e4959: PMSG_DEVILSQUARERESULT, el puesto, la cuenta y los registros de 24 bytes que siguen.
TEST(EventsProtocolTest, ReadsTheResultAndKeepsTheRest)
{
    std::vector<std::byte> bytes = Bytes({0xC1, 0x05 + 24, 0x93, 0x02, 0x01});
    for (int index = 0; index < 24; ++index)
        bytes.push_back(static_cast<std::byte>(index));
    const auto result = Read<Events::EventResult>(bytes);
    EXPECT_EQ(result.rank, 2);
    EXPECT_EQ(result.count, 1);
    ASSERT_EQ(result.scores.size(), 24u);
    EXPECT_EQ(result.scores[23], std::byte{23});
}

// 0x4dd8a0: PMSG_EVENTCHIPINFO, el tipo y el word de pkt[4]; los tres del Muto, si los trae.
TEST(EventsProtocolTest, ReadsTheChipInfoWithAndWithoutTheMutoNumbers)
{
    const auto chips =
        Read<Events::EventChipInfo>(Bytes({0xC1, 0x0C, 0x94, 0x00, 0x34, 0x12, 0x01, 0x00, 0x02, 0x00, 0x03, 0x00}));
    EXPECT_EQ(chips.type, 0);
    EXPECT_EQ(chips.count, 0x1234);
    ASSERT_TRUE(chips.muto.has_value());
    EXPECT_EQ(*chips.muto, (std::array<std::uint16_t, 3>{1, 2, 3}));
    const auto shorter = Read<Events::EventChipInfo>(Bytes({0xC1, 0x06, 0x94, 0x02, 0x05, 0x00}));
    EXPECT_FALSE(shorter.muto.has_value());
}

// 0x4e49d4, 0x4e49fb, 0x4e4930, 0x4e4870, 0x4e4a40, 0x4da780 y 0x4e4ad4.
TEST(EventsProtocolTest, ReadsTheSmallOnes)
{
    EXPECT_EQ(Read<Events::EventChipCount>(Bytes({0xC1, 0x06, 0x95, 0x00, 0xFF, 0xFF})).count, 0xFFFF);
    EXPECT_EQ(Read<Events::MutoNumbers>(Bytes({0xC1, 0x0A, 0x96, 0x00, 0x0A, 0x00, 0x0B, 0x00, 0x0C, 0x00})).numbers,
              (std::array<std::uint16_t, 3>{10, 11, 12}));
    EXPECT_EQ(Read<Events::EventCountdown>(Bytes({0xC1, 0x04, 0x92, 0x0B})).type, 0x0B);
    EXPECT_EQ(Read<Events::DevilSquareEntryResult>(Bytes({0xC1, 0x04, 0x90, 0x03})).result, 3);
    EXPECT_EQ(Read<Events::EventDialog>(Bytes({0xC1, 0x04, 0x99, 0x01})).kind, 1);
    EXPECT_EQ(Read<Events::BloodCastleEntryResult>(Bytes({0xC1, 0x04, 0x9A, 0x05})).result, 5);
    EXPECT_EQ(Read<Events::EventSound>(Bytes({0xC1, 0x06, 0x9E, 0x00, 0x02, 0x00})).sound, 2);
}

// 0x4e4999: PMSG_STATEBLOODCASTLE, los words little-endian de pkt[4..0xb] y el arma en pkt[0xc].
TEST(EventsProtocolTest, ReadsThePlayState)
{
    const auto state = Read<Events::EventPlayState>(
        Bytes({0xC1, 0x0D, 0x9B, 0x01, 0x2C, 0x01, 0x28, 0x00, 0x05, 0x00, 0x2A, 0x80, 0x02}));
    EXPECT_EQ(state.state, 1);
    EXPECT_EQ(state.remaining, 300);
    EXPECT_EQ(state.maxKill, 40);
    EXPECT_EQ(state.currentKill, 5);
    EXPECT_EQ(state.weaponOwner, 0x802A);
    EXPECT_EQ(state.weapon, 2);
    EXPECT_EQ(ErrorOf(Events::ReadEventPacket(PacketOf(Bytes({0xC1, 0x0C, 0x9B, 1, 2, 3, 4, 5, 6, 7, 8, 9})))),
              ProtocolError::TooShort);
}

// 0x4e4a86: el resultado y los 0x40 bytes desde pkt[4].
TEST(EventsProtocolTest, ReadsTheSerialResult)
{
    std::vector<std::byte> bytes = Bytes({0xC1, 0x44, 0x9D, 0x05});
    for (int index = 0; index < 0x40; ++index)
        bytes.push_back(static_cast<std::byte>(0x40 + index));
    const auto serial = Read<Events::SerialResult>(bytes);
    EXPECT_EQ(serial.result, 5);
    EXPECT_EQ(serial.data[0], std::byte{0x40});
    EXPECT_EQ(serial.data[0x3f], std::byte{0x7f});
}

// AF 01 (0x4daa90), BC 00 y 01 (0x4e4547, 0x4da540) y BD 00 (0x4e5941); otro subcódigo no es de los eventos.
TEST(EventsProtocolTest, ReadsTheSubcodedOnes)
{
    EXPECT_EQ(Read<Events::ChaosCastleEntryResult>(Bytes({0xC1, 0x05, 0xAF, 0x01, 0x07})).result, 7);
    const auto mix = Read<Events::JewelMixResult>(Bytes({0xC1, 0x05, 0xBC, 0x01, 0x08}));
    EXPECT_EQ(mix.subcode, 1);
    EXPECT_EQ(mix.result, 8);
    const auto crywolf = Read<Events::CrywolfState>(Bytes({0xC1, 0x06, 0xBD, 0x00, 0x02, 0x05}));
    EXPECT_EQ(crywolf.state, 2);
    EXPECT_EQ(crywolf.stage, 5);
    EXPECT_EQ(ErrorOf(Events::ReadEventPacket(PacketOf(Bytes({0xC1, 0x05, 0xAF, 0x02, 0x07})))),
              ProtocolError::WrongPacket);
    EXPECT_EQ(ErrorOf(Events::ReadEventPacket(PacketOf(Bytes({0xC1, 0x06, 0xBD, 0x01, 0x02, 0x05})))),
              ProtocolError::WrongPacket);
}

// Los BD de la interfaz de Crywolf (0x4e620c): 02 (0x4e0fe0), 04, 05, 07, 08 (0x4e1680) y 09 con los bytes que lee el
// original; el 03 y el 0C enteros, para cortar al aplicarlos; el 06 y el 0A no hacen nada. GS 0.97 no manda ninguno.
TEST(EventsProtocolTest, ReadsTheCrywolfInterfaceOnes)
{
    const auto altars = Read<Events::CrywolfAltars>(
        Bytes({0xC1, 0x0D, 0xBD, 0x02, 0x40, 0x00, 0x00, 0x00, 0x11, 0x02, 0x01, 0x12, 0x00}));
    EXPECT_EQ(altars.light, 0x40);
    EXPECT_EQ(altars.altars, (std::array<std::uint8_t, 5>{0x11, 2, 1, 0x12, 0}));
    const auto timer = Read<Events::CrywolfTimer>(Bytes({0xC1, 0x06, 0xBD, 0x04, 0x03, 0x0a}));
    EXPECT_EQ(timer.value4, 3);
    EXPECT_EQ(timer.minutes, 10);
    const auto boss = Read<Events::CrywolfBoss>(Bytes({0xC1, 0x09, 0xBD, 0x05, 0x50, 0x00, 0x00, 0x00, 0x04}));
    EXPECT_EQ(boss.life, 0x50);
    EXPECT_EQ(boss.count, 4);
    const auto rank =
        Read<Events::CrywolfRank>(Bytes({0xC1, 0x0C, 0xBD, 0x07, 0x03, 0x00, 0x00, 0x00, 0x39, 0x30, 0x00, 0x00}));
    EXPECT_EQ(rank.rank, 3);
    EXPECT_EQ(rank.experience, 12345);
    std::vector<std::byte> ranking = Bytes({0xC1, 0x00, 0xBD, 0x08, 0x02});
    for (const std::uint8_t place : {std::uint8_t{0}, std::uint8_t{4}})
    {
        const std::vector<std::byte> entry =
            Bytes({place, 'L', 'o', 'b', 'o', 'G', 'r', 'i', 's', '0', '1', 0, 0x10, 0x27, 0, 0, 0x08, 0, 0, 0});
        ranking.insert(ranking.end(), entry.begin(), entry.end());
    }
    ranking[1] = static_cast<std::byte>(ranking.size());
    const auto read = Read<Events::CrywolfRanking>(ranking);
    ASSERT_EQ(read.entries.size(), 2u);
    EXPECT_EQ(read.entries[1].place, 4);
    EXPECT_EQ(std::string(read.entries[1].name.data(), read.entries[1].name.size()), "LoboGris01");
    EXPECT_EQ(read.entries[1].score, 10000);
    EXPECT_EQ(read.entries[1].classByte, 8);
    ranking.pop_back();
    ranking[1] = static_cast<std::byte>(ranking.size());
    EXPECT_EQ(ErrorOf(Events::ReadEventPacket(PacketOf(ranking))), ProtocolError::TooShort);
    EXPECT_EQ(Read<Events::CrywolfChaosBox>(Bytes({0xC1, 0x05, 0xBD, 0x09, 0x01})).value, 1);
    // BD 03 (0x4e1034..0x4e1046): pkt[4], pkt[5] y la clave big-endian de pkt[6] y pkt[7], que lee siempre.
    const auto contract = Read<Events::CrywolfContract>(Bytes({0xC1, 0x08, 0xBD, 0x03, 0x01, 0x12, 0x01, 0x3e}));
    EXPECT_EQ(contract.result, 1);
    EXPECT_EQ(contract.value, 0x12);
    EXPECT_EQ(contract.key, 0x013e);
    EXPECT_EQ(ErrorOf(Events::ReadEventPacket(PacketOf(Bytes({0xC1, 0x07, 0xBD, 0x03, 0x00, 0x00, 0x01})))),
              ProtocolError::TooShort);
    const auto effects = Read<Events::CrywolfUnported>(Bytes({0xC1, 0x05, 0xBD, 0x0C, 0x00}));
    EXPECT_EQ(effects.subcode, 0xc);
    EXPECT_EQ(effects.bytes.size(), 5u);
    for (const std::uint8_t nothing : {std::uint8_t{6}, std::uint8_t{0xa}, std::uint8_t{0xd}})
        EXPECT_EQ(ErrorOf(Events::ReadEventPacket(PacketOf(Bytes({0xC1, 0x06, 0xBD, nothing, 0x02, 0x05})))),
                  ProtocolError::WrongPacket);
    EXPECT_EQ(ErrorOf(Events::ReadEventPacket(PacketOf(Bytes({0xC1, 0x08, 0xBD, 0x05, 0, 0, 0, 0})))),
              ProtocolError::TooShort);
}

// F3 22 (0x4e31f8: el byte de pkt[4] y el word de pkt[6], no el de pkt[4] del PMSG_F3_22 del GS), F3 23 (0x4d9f00)
// y F3 40 (0x4da070).
TEST(EventsProtocolTest, ReadsTheWorldSubcodes)
{
    const auto time = Read<Events::EventTime>(Bytes({0xC1, 0x08, 0xF3, 0x22, 0x03, 0x00, 0x10, 0x0E}));
    EXPECT_EQ(time.value4, 3);
    EXPECT_EQ(time.value6, 0x0E10);
    const auto score = Read<Events::SoccerScore>(
        Bytes({0xC1, 0x16, 0xF3, 0x23, 'A', 'z', 'u', 'l', 0, 0, 0, 0, 0x02, 'R', 'o', 'j', 'o', 0, 0, 0, 0, 0x01}));
    EXPECT_EQ(score.firstName[0], 'A');
    EXPECT_EQ(score.firstScore, 2);
    EXPECT_EQ(score.secondName[3], 'o');
    EXPECT_EQ(score.secondScore, 1);
    const auto command = Read<Events::ServerCommand>(Bytes({0xC1, 0x07, 0xF3, 0x40, 0x07, 0x00, 0x03}));
    EXPECT_EQ(command.command, 7);
    EXPECT_EQ(command.value6, 3);
    EXPECT_EQ(ErrorOf(Events::ReadEventPacket(PacketOf(Bytes({0xC1, 0x05, 0xF3, 0x30, 0x00})))),
              ProtocolError::WrongPacket);
}

// Otro opcode, o un C3, no son de los eventos.
TEST(EventsProtocolTest, RejectsWhatIsNotAnEventPacket)
{
    EXPECT_EQ(ErrorOf(Events::ReadEventPacket(PacketOf(Bytes({0xC1, 0x04, 0x97, 0x00})))), ProtocolError::WrongPacket);
    EXPECT_EQ(ErrorOf(Events::ReadEventPacket(PacketOf(Bytes({0xC2, 0x00, 0x05, 0x0B, 0x01})))),
              ProtocolError::WrongPacket);
}

// 0x56b4e8..0x56b5a0: C1 03 97 en claro, sin XOR32.
TEST(EventsProtocolTest, WritesTheEventWindowLeaveInTheClear)
{
    const Client::Protocol::Outgoing leave = Events::WriteEventWindowLeave();
    EXPECT_EQ(leave.delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(leave.packet.Bytes(), Bytes({0xC1, 0x03, 0x97})));
}

// La ventana de 0x78abb00 (0x5e6520): el C1 04 98 01 (0x5e65f6..0x5e67a0), el C1 05 95 tipo lugar (0x5e6a1b..0x5e6bc9,
// el lugar crudo: el GS le suma 12) y el C1 12 9D con los tres pedazos de 4 bytes del EDIT, cada uno con un 0
// (0x5e6d34..0x5e7184), todos con el XOR32 desde el byte 3 y por el send en claro.
TEST(EventsProtocolTest, WritesTheChipWindowRequestsWithTheXor32)
{
    const Client::Protocol::Outgoing muto = Events::WriteMutoNumberRequest();
    EXPECT_EQ(muto.delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(muto.packet.Bytes(), Bytes({0xC1, 0x04, 0x98, 0x10})));
    EXPECT_TRUE(SameBytes(Xor32Decode(muto.packet).Bytes(), Bytes({0xC1, 0x04, 0x98, 0x01})));

    const Client::Protocol::Outgoing rena = Events::WriteEventChipRegister(0, 0x1f);
    EXPECT_EQ(rena.delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(rena.packet.Bytes(), Bytes({0xC1, 0x05, 0x95, 0x1C, 0xBF})));
    const Client::Protocol::Outgoing stone = Events::WriteEventChipRegister(1, 0x05);
    EXPECT_TRUE(SameBytes(stone.packet.Bytes(), Bytes({0xC1, 0x05, 0x95, 0x1D, 0xA4})));
    EXPECT_TRUE(SameBytes(Xor32Decode(stone.packet).Bytes(), Bytes({0xC1, 0x05, 0x95, 0x01, 0x05})));

    Events::SerialPieces pieces{};
    const std::string_view serial = "AB12CD34EF56";
    for (std::size_t at = 0; at < serial.size(); ++at)
        pieces[at / 4][at % 4] = static_cast<std::uint8_t>(serial[at]);
    const Client::Protocol::Outgoing number = Events::WriteSerialNumber(pieces);
    EXPECT_EQ(number.delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(number.packet.Bytes(), Bytes({0xC1, 0x12, 0x9D, 0x55, 0xAB, 0x28, 0x85, 0xF6, 0x96, 0x7A,
                                                        0xB7, 0x35, 0x7C, 0x64, 0x1B, 0x73, 0xCF, 0x04})));
    EXPECT_TRUE(SameBytes(Xor32Decode(number.packet).Bytes(), Bytes({0xC1, 0x12, 0x9D, 'A', 'B', '1', '2', 0, 'C', 'D',
                                                                     '3', '4', 0, 'E', 'F', '5', '6', 0})));
}

// 0x435d1c..0x4361a2: C1 06 BD 03 alto bajo de 0x723be0 (SAR 8 y el byte bajo), con el XOR32 en claro. Con 0x013d:
// 03 ^ 89 ^ BD = 37, 01 ^ BC ^ 37 = 8A y 3D ^ B2 ^ 8A = 05; una clave negativa (MOVSX de 0x8140) da 81 y 40.
TEST(EventsProtocolTest, WritesTheCrywolfContractWithTheXor32)
{
    const Client::Protocol::Outgoing contract = Events::WriteCrywolfContract(0x013d);
    EXPECT_EQ(contract.delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(contract.packet.Bytes(), Bytes({0xC1, 0x06, 0xBD, 0x37, 0x8A, 0x05})));
    EXPECT_TRUE(SameBytes(Xor32Decode(contract.packet).Bytes(), Bytes({0xC1, 0x06, 0xBD, 0x03, 0x01, 0x3D})));
    const Client::Protocol::Outgoing negative = Events::WriteCrywolfContract(std::int16_t{-0x7ec0});
    EXPECT_TRUE(SameBytes(negative.packet.Bytes(), Bytes({0xC1, 0x06, 0xBD, 0x37, 0x0A, 0xF8})));
    EXPECT_TRUE(SameBytes(Xor32Decode(negative.packet).Bytes(), Bytes({0xC1, 0x06, 0xBD, 0x03, 0x81, 0x40})));
}

// 0x4fbeed..0x4fc231: C1 06 AF 02 x y con el XOR32 byte a byte desde el 02 (la clave en 3, 4 y 5: 0x89, 0xbc y
// 0xb2), por el send en claro. Con (5, 6): 02 ^ 89 ^ AF = 24, 05 ^ BC ^ 24 = 9D y 06 ^ B2 ^ 9D = 29.
TEST(EventsProtocolTest, WritesTheChaosCastleRepositionWithTheXor32)
{
    const Client::Protocol::Outgoing reposition = Events::WriteChaosCastleReposition(5, 6);
    EXPECT_EQ(reposition.delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(reposition.packet.Bytes(), Bytes({0xC1, 0x06, 0xAF, 0x24, 0x9D, 0x29})));
    EXPECT_TRUE(SameBytes(Xor32Decode(reposition.packet).Bytes(), Bytes({0xC1, 0x06, 0xAF, 0x02, 0x05, 0x06})));
}

// 0x5e5b2f..0x5e5d7e y 0x5e6156..0x5e62cb: C1 05 90 cuadrado lugar y C1 05 9A puente lugar, con el XOR32 en claro. Con
// (2, 0x1d): 02 ^ 89 ^ 90 = 1B y 1D ^ BC ^ 1B = BA; con (3, 0x15): 03 ^ 89 ^ 9A = 10 y 15 ^ BC ^ 10 = B9.
TEST(EventsProtocolTest, WritesTheEventEntriesWithTheXor32)
{
    const Client::Protocol::Outgoing square = Events::WriteDevilSquareEntry(2, 0x1d);
    EXPECT_EQ(square.delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(square.packet.Bytes(), Bytes({0xC1, 0x05, 0x90, 0x1B, 0xBA})));
    EXPECT_TRUE(SameBytes(Xor32Decode(square.packet).Bytes(), Bytes({0xC1, 0x05, 0x90, 0x02, 0x1D})));
    const Client::Protocol::Outgoing bridge = Events::WriteBloodCastleEntry(3, 0x15);
    EXPECT_EQ(bridge.delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(bridge.packet.Bytes(), Bytes({0xC1, 0x05, 0x9A, 0x10, 0xB9})));
    EXPECT_TRUE(SameBytes(Xor32Decode(bridge.packet).Bytes(), Bytes({0xC1, 0x05, 0x9A, 0x03, 0x15})));
}
} // namespace
