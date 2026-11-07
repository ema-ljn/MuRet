#include "Protocol/Character/InventoryList.h"

#include "Network/Packet.h"
#include "Protocol/ProtocolError.h"

#include "support/Bytes.h"
#include "support/ExpectedError.h"
#include "support/Packets.h"
#include "support/ProtocolPrinters.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

// Vectores sintéticos escritos a mano: no son capturas del cable ni decodifican ítems.
namespace
{
using Client::Network::Packet;
using Client::Protocol::ProtocolError;
using Client::Protocol::Character::InventoryList;
using Client::Protocol::Character::ReadInventoryList;
using Client::Tests::Bytes;
using Client::Tests::ErrorOf;
using Client::Tests::PacketOf;

// Solo forma la cabecera C2: permite que cada corte llegue al lector como un Packet válido.
Packet InventoryPacket(std::vector<std::byte> fields, bool wasDecrypted = false)
{
    std::vector<std::byte> wire = Bytes({0xC2, 0, 0, 0xF3, 0x10});
    wire.insert(wire.end(), fields.begin(), fields.end());
    wire[1] = static_cast<std::byte>(wire.size() >> 8);
    wire[2] = static_cast<std::byte>(wire.size());
    auto packet = Packet::FromBytes(std::move(wire), wasDecrypted);
    EXPECT_TRUE(packet.HasValue());
    return std::move(packet).Value();
}

TEST(InventoryListTest, ReadsAnEmptyList)
{
    const auto list = ReadInventoryList(PacketOf(Bytes({0xC2, 0x00, 0x06, 0xF3, 0x10, 0x00})));

    ASSERT_TRUE(list.HasValue());
    EXPECT_TRUE(list.Value().items.empty());
}

// Detecta un offset de cantidad incorrecto y copias de cinco o seis bytes en vez de siete.
TEST(InventoryListTest, ReadsOneItemWithSevenAsymmetricBytes)
{
    const auto list = ReadInventoryList(
        PacketOf(Bytes({0xC2, 0x00, 0x0E, 0xF3, 0x10, 0x01, 0x11, 0x31, 0x42, 0x53, 0x64, 0x75, 0x86, 0x97})));

    ASSERT_TRUE(list.HasValue());
    ASSERT_EQ(list.Value().items.size(), 1u);
    EXPECT_EQ(list.Value().items[0].slot, 0x11);
    const std::array expected = {std::byte{0x31}, std::byte{0x42}, std::byte{0x53}, std::byte{0x64},
                                 std::byte{0x75}, std::byte{0x86}, std::byte{0x97}};
    EXPECT_EQ(list.Value().items[0].item, expected);
}

// Los cinco lugares documentados, con datos propios: un stride de cinco/siete/nueve los desalinearía.
TEST(InventoryListTest, ReadsTheFiveDocumentedSlotsWithSyntheticData)
{
    const auto list = ReadInventoryList(InventoryPacket(Bytes({
        5,    0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x0C, 0x11, 0x21, 0x31, 0x41,
        0x51, 0x61, 0x71, 0x0E, 0x12, 0x22, 0x32, 0x42, 0x52, 0x62, 0x72, 0x11, 0x13, 0x23,
        0x33, 0x43, 0x53, 0x63, 0x73, 0x17, 0x14, 0x24, 0x34, 0x44, 0x54, 0x64, 0x74,
    })));

    ASSERT_TRUE(list.HasValue());
    ASSERT_EQ(list.Value().items.size(), 5u);
    constexpr std::array<std::uint8_t, 5> ExpectedSlots = {0, 0x0C, 0x0E, 0x11, 0x17};
    const std::array<std::array<std::byte, 7>, 5> expectedItems = {{
        {std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40}, std::byte{0x50}, std::byte{0x60},
         std::byte{0x70}},
        {std::byte{0x11}, std::byte{0x21}, std::byte{0x31}, std::byte{0x41}, std::byte{0x51}, std::byte{0x61},
         std::byte{0x71}},
        {std::byte{0x12}, std::byte{0x22}, std::byte{0x32}, std::byte{0x42}, std::byte{0x52}, std::byte{0x62},
         std::byte{0x72}},
        {std::byte{0x13}, std::byte{0x23}, std::byte{0x33}, std::byte{0x43}, std::byte{0x53}, std::byte{0x63},
         std::byte{0x73}},
        {std::byte{0x14}, std::byte{0x24}, std::byte{0x34}, std::byte{0x44}, std::byte{0x54}, std::byte{0x64},
         std::byte{0x74}},
    }};
    for (std::size_t index = 0; index < 5; ++index)
    {
        EXPECT_EQ(list.Value().items[index].slot, ExpectedSlots[index]);
        EXPECT_EQ(list.Value().items[index].item, expectedItems[index]);
    }
}

// Detecta filtrado a equipo, ordenamiento, deduplicación, signo y normalización de los datos.
TEST(InventoryListTest, PreservesOrderDuplicateSlotsAndAllBits)
{
    const auto list = ReadInventoryList(InventoryPacket(Bytes({
        3,    0xFF, 0,    0xFF, 0x80, 0x7F, 0x81, 0, 0xFE, 0x02, 0xAA, 0,    0x55,
        0xFF, 0x80, 0x11, 0xCC, 0xFF, 0xFF, 0x80, 0, 0x01, 0x02, 0x03, 0x04,
    })));

    ASSERT_TRUE(list.HasValue());
    ASSERT_EQ(list.Value().items.size(), 3u);
    EXPECT_EQ(list.Value().items[0].slot, 255);
    EXPECT_EQ(list.Value().items[1].slot, 2);
    EXPECT_EQ(list.Value().items[2].slot, 255);
    const std::array first = {std::byte{0},    std::byte{0xFF}, std::byte{0x80}, std::byte{0x7F},
                              std::byte{0x81}, std::byte{0},    std::byte{0xFE}};
    const std::array second = {std::byte{0xAA}, std::byte{0},    std::byte{0x55}, std::byte{0xFF},
                               std::byte{0x80}, std::byte{0x11}, std::byte{0xCC}};
    const std::array third = {std::byte{0xFF}, std::byte{0x80}, std::byte{0}, std::byte{1},
                              std::byte{2},    std::byte{3},    std::byte{4}};
    EXPECT_EQ(list.Value().items[0].item, first);
    EXPECT_EQ(list.Value().items[1].item, second);
    EXPECT_EQ(list.Value().items[2].item, third);
}

// El máximo unsigned necesita un C2 de 2046 bytes, con largo BE 07 FE.
TEST(InventoryListTest, ReadsAll255EntriesFromABigEndian2046BytePacket)
{
    std::vector<std::byte> wire = Bytes({0xC2, 0x07, 0xFE, 0xF3, 0x10, 0xFF});
    const auto item = Bytes({0x11, 0x22, 0x33, 0x44, 0x80, 0xFF, 0xA7});
    for (unsigned int slot = 0; slot < 255; ++slot)
    {
        wire.push_back(static_cast<std::byte>(slot));
        wire.insert(wire.end(), item.begin(), item.end());
    }
    const auto list = ReadInventoryList(PacketOf(wire));

    ASSERT_TRUE(list.HasValue());
    ASSERT_EQ(list.Value().items.size(), 255u);
    EXPECT_EQ(list.Value().items[0].slot, 0);
    EXPECT_EQ(list.Value().items[127].slot, 127);
    EXPECT_EQ(list.Value().items[128].slot, 128);
    EXPECT_EQ(list.Value().items[254].slot, 254);
    const std::array expected = {std::byte{0x11}, std::byte{0x22}, std::byte{0x33}, std::byte{0x44},
                                 std::byte{0x80}, std::byte{0xFF}, std::byte{0xA7}};
    for (const auto& entry : list.Value().items)
    {
        EXPECT_EQ(entry.item, expected);
    }
}

// Cortar cualquier byte desde la cantidad hasta el final exige error, nunca una lista parcial.
TEST(InventoryListTest, EveryIncompleteBodyIsTooShort)
{
    const auto complete =
        Bytes({0xC2, 0, 0x16, 0xF3, 0x10, 2, 0x0C, 1, 2, 3, 4, 5, 6, 7, 0x17, 8, 9, 10, 11, 12, 13, 14});
    for (std::size_t size = 5; size < complete.size(); ++size)
    {
        SCOPED_TRACE(size);
        std::vector<std::byte> cut(complete.begin(), complete.begin() + static_cast<std::ptrdiff_t>(size));
        cut[2] = static_cast<std::byte>(size);
        EXPECT_EQ(ErrorOf(ReadInventoryList(PacketOf(cut))), ProtocolError::TooShort);
    }
}

TEST(InventoryListTest, ACountAboveTheAvailableEntriesIsTooShort)
{
    const auto packet = InventoryPacket(Bytes({2, 0x0C, 1, 2, 3, 4, 5, 6, 7}));

    EXPECT_EQ(ErrorOf(ReadInventoryList(packet)), ProtocolError::TooShort);
}

// Ni una entrada completa detrás de count=0 forma parte de la lista.
TEST(InventoryListTest, AnEmptyListIgnoresTrailingBytes)
{
    const auto list = ReadInventoryList(InventoryPacket(Bytes({0, 0xFF, 1, 2, 3, 4, 5, 6, 7})));

    ASSERT_TRUE(list.HasValue());
    EXPECT_TRUE(list.Value().items.empty());
}

TEST(InventoryListTest, IgnoresZeroOneOrEightTrailingBytesAfterTheCountedEntry)
{
    const auto fields = Bytes({1, 0x17, 1, 2, 3, 4, 5, 6, 7});
    for (const std::size_t trailing : {0u, 1u, 8u})
    {
        SCOPED_TRACE(trailing);
        auto extended = fields;
        extended.insert(extended.end(), trailing, std::byte{0xFF});
        const auto list = ReadInventoryList(InventoryPacket(extended));

        ASSERT_TRUE(list.HasValue());
        ASSERT_EQ(list.Value().items.size(), 1u);
        EXPECT_EQ(list.Value().items[0].slot, 0x17);
        EXPECT_EQ(list.Value().items[0].item[6], std::byte{7});
    }
}

TEST(InventoryListTest, RejectsC1AndC3AndTheUnreconstructedC4)
{
    EXPECT_EQ(ErrorOf(ReadInventoryList(PacketOf(Bytes({0xC1, 5, 0xF3, 0x10, 0})))), ProtocolError::WrongPacket);
    EXPECT_EQ(ErrorOf(ReadInventoryList(PacketOf(Bytes({0xC3, 5, 0xF3, 0x10, 0})))), ProtocolError::WrongPacket);
    EXPECT_EQ(ErrorOf(ReadInventoryList(PacketOf(Bytes({0xC4, 0, 6, 0xF3, 0x10, 0})))), ProtocolError::WrongPacket);
}

TEST(InventoryListTest, RejectsAnotherOpcode)
{
    EXPECT_EQ(ErrorOf(ReadInventoryList(PacketOf(Bytes({0xC2, 0, 6, 0xF1, 0x10, 0})))), ProtocolError::WrongPacket);
}

TEST(InventoryListTest, RejectsAnotherSubcode)
{
    EXPECT_EQ(ErrorOf(ReadInventoryList(PacketOf(Bytes({0xC2, 0, 6, 0xF3, 0x03, 0})))), ProtocolError::WrongPacket);
}

TEST(InventoryListTest, AMissingSubcodeIsTooShort)
{
    EXPECT_EQ(ErrorOf(ReadInventoryList(PacketOf(Bytes({0xC2, 0, 4, 0xF3})))), ProtocolError::TooShort);
}

TEST(InventoryListTest, OwnsTheResultAfterPacketMutationAndDestruction)
{
    InventoryList retained;
    {
        auto packet = InventoryPacket(Bytes({1, 0xFF, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0xA7}));
        auto list = ReadInventoryList(packet);
        ASSERT_TRUE(list.HasValue());
        retained = std::move(list).Value();
        std::ranges::fill(packet.MutableBody(), std::byte{0});
        ASSERT_EQ(retained.items.size(), 1u);
        EXPECT_EQ(retained.items[0].slot, 255);
        EXPECT_EQ(retained.items[0].item[6], std::byte{0xA7});
    }
    ASSERT_EQ(retained.items.size(), 1u);
    EXPECT_EQ(retained.items[0].slot, 255);
    const std::array expected = {std::byte{0x11}, std::byte{0x22}, std::byte{0x33}, std::byte{0x44},
                                 std::byte{0x55}, std::byte{0x66}, std::byte{0xA7}};
    EXPECT_EQ(retained.items[0].item, expected);
}

TEST(InventoryListTest, DecryptionMetadataDoesNotChangeThePureReader)
{
    const auto fields = Bytes({1, 0x0E, 0, 0x80, 0xFF, 0x41, 0x52, 0x63, 0x74});
    for (const bool wasDecrypted : {false, true})
    {
        SCOPED_TRACE(wasDecrypted);
        const auto list = ReadInventoryList(InventoryPacket(fields, wasDecrypted));

        ASSERT_TRUE(list.HasValue());
        ASSERT_EQ(list.Value().items.size(), 1u);
        EXPECT_EQ(list.Value().items[0].slot, 0x0E);
        const std::array expected = {std::byte{0},    std::byte{0x80}, std::byte{0xFF}, std::byte{0x41},
                                     std::byte{0x52}, std::byte{0x63}, std::byte{0x74}};
        EXPECT_EQ(list.Value().items[0].item, expected);
    }
}
} // namespace
