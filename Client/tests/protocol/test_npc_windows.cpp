#include "Protocol/NpcWindows.h"

#include "Network/Crypto/Xor32.h"
#include "Network/Delivery.h"
#include "Protocol/ProtocolError.h"

#include "support/Bytes.h"
#include "support/ExpectedError.h"
#include "support/Packets.h"
#include "support/SameBytes.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace
{
using Client::Network::Delivery;
using Client::Network::Crypto::Xor32Decode;
using Client::Protocol::ProtocolError;
using Client::Tests::Bytes;
using Client::Tests::ErrorOf;
using Client::Tests::PacketOf;
using Client::Tests::SameBytes;
namespace NpcWindows = Client::Protocol::NpcWindows;

// Un ítem inventado: el byte 0, el nivel y las opciones, la durabilidad, los dos de opciones, el grupo y el último.
constexpr NpcWindows::ItemData InventedItem{std::byte{0x03}, std::byte{0x18}, std::byte{0x20}, std::byte{0x00},
                                            std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};

// 0x5c4bdc..: C1 04 32 con el lugar, con el XOR32 desde pkt[3] y para cifrar como C3.
TEST(NpcWindowsProtocolTest, TheBuyIsAnEncryptedC1WithTheSlot)
{
    const auto buy = NpcWindows::WriteBuy(0x11);
    EXPECT_EQ(buy.delivery, Delivery::Encrypted);
    EXPECT_TRUE(SameBytes(Xor32Decode(buy.packet).Bytes(), Bytes({0xc1, 0x04, 0x32, 0x11})));
}

// 0x5d8e1a..: C1 04 33 con el lugar de donde salió el ítem del cursor, cifrado.
TEST(NpcWindowsProtocolTest, TheSellIsAnEncryptedC1WithTheSlot)
{
    const auto sell = NpcWindows::WriteSell(0x2a);
    EXPECT_EQ(sell.delivery, Delivery::Encrypted);
    EXPECT_TRUE(SameBytes(Xor32Decode(sell.packet).Bytes(), Bytes({0xc1, 0x04, 0x33, 0x2a})));
}

// 0x5c4485.. y 0x5edaa7..: C1 05 34, un lugar con 0x78abb10 o todo (0xff y 0), cifrado.
TEST(NpcWindowsProtocolTest, TheRepairCarriesThePositionAndTheRequest)
{
    const auto one = NpcWindows::WriteRepair(0x0e, 1);
    EXPECT_EQ(one.delivery, Delivery::Encrypted);
    EXPECT_TRUE(SameBytes(Xor32Decode(one.packet).Bytes(), Bytes({0xc1, 0x05, 0x34, 0x0e, 0x01})));
    const auto all = NpcWindows::WriteRepair(NpcWindows::RepairEverything, 0);
    EXPECT_TRUE(SameBytes(Xor32Decode(all.packet).Bytes(), Bytes({0xc1, 0x05, 0x34, 0xff, 0x00})));
}

// 0x4e6f80: C1 08 81 con el tipo y el zen en little endian, con el XOR32 y en claro.
TEST(NpcWindowsProtocolTest, TheWarehouseMoneyIsAClearXor32C1)
{
    const auto money = NpcWindows::WriteWarehouseMoney(1, 0x0001e240);
    EXPECT_EQ(money.delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(Xor32Decode(money.packet).Bytes(), Bytes({0xc1, 0x08, 0x81, 0x01, 0x40, 0xe2, 0x01, 0x00})));
}

// 0x658574 y 0x6587.. (0x657ab0): C1 03 82 y C1 03 87 en claro, sin el XOR32.
TEST(NpcWindowsProtocolTest, TheClosesAreClearC1sWithoutBody)
{
    const auto warehouse = NpcWindows::WriteWarehouseClose();
    EXPECT_EQ(warehouse.delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(warehouse.packet.Bytes(), Bytes({0xc1, 0x03, 0x82})));
    const auto chaosBox = NpcWindows::WriteChaosBoxClose();
    EXPECT_EQ(chaosBox.delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(chaosBox.packet.Bytes(), Bytes({0xc1, 0x03, 0x87})));
}

// 0x5e7e90..: C1 10 83 con el tipo, la contraseña en little endian y los diez bytes del número, cifrado.
TEST(NpcWindowsProtocolTest, TheWarehousePasswordIsAnEncryptedC1OfSixteenBytes)
{
    NpcWindows::WarehouseNumber number{};
    for (std::size_t i = 0; i < number.size(); ++i)
        number[i] = static_cast<std::byte>('0' + i);
    const auto password = NpcWindows::WriteWarehousePassword(1, 1234, number);
    EXPECT_EQ(password.delivery, Delivery::Encrypted);
    EXPECT_TRUE(SameBytes(Xor32Decode(password.packet).Bytes(), Bytes({0xc1, 0x10, 0x83, 0x01, 0xd2, 0x04, '0', '1',
                                                                       '2', '3', '4', '5', '6', '7', '8', '9'})));
}

// 0x5e8706..: C1 0D 99 con los diez bytes del número personal, cifrado.
TEST(NpcWindowsProtocolTest, ThePersonalNumberIsAnEncryptedC1OfThirteenBytes)
{
    NpcWindows::WarehouseNumber number{};
    for (std::size_t i = 0; i < 7; ++i)
        number[i] = static_cast<std::byte>('3' + i % 3);
    const auto personal = NpcWindows::WritePersonalNumber(number);
    EXPECT_EQ(personal.delivery, Delivery::Encrypted);
    EXPECT_TRUE(SameBytes(Xor32Decode(personal.packet).Bytes(),
                          Bytes({0xc1, 0x0d, 0x99, '3', '4', '5', '3', '4', '5', '3', 0x00, 0x00, 0x00})));
}

// 0x5d9362..: C1 04 86 con el tipo de la mezcla, con el XOR32 y en claro; y 0x65a956..: C1 04 BD 09.
TEST(NpcWindowsProtocolTest, TheMixAndTheRateRequestAreClearXor32C1s)
{
    const auto mix = NpcWindows::WriteChaosMix(3);
    EXPECT_EQ(mix.delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(Xor32Decode(mix.packet).Bytes(), Bytes({0xc1, 0x04, 0x86, 0x03})));
    const auto rates = NpcWindows::WriteChaosRateRequest();
    EXPECT_EQ(rates.delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(Xor32Decode(rates.packet).Bytes(), Bytes({0xc1, 0x04, 0xbd, 0x09})));
}

// 0x4d1e30..: el aviso de un 30 sin cifrar, C1 06 F1 03 00 30, cifrado.
TEST(NpcWindowsProtocolTest, TheClearTalkReportIsF103WithTheOpcode)
{
    const auto report = NpcWindows::WriteClearTalkReport();
    EXPECT_EQ(report.delivery, Delivery::Encrypted);
    EXPECT_TRUE(SameBytes(Xor32Decode(report.packet).Bytes(), Bytes({0xc1, 0x06, 0xf1, 0x03, 0x00, 0x30})));
}

// 0x4d1e00: pkt[3] y las seis tasas de pkt[4..9]; el C3 de 8 bytes del GameServer 0.97 trae cuatro (Q-C920).
TEST(NpcWindowsProtocolTest, ReadsTheTalkResultWithTheRatesItCarries)
{
    const auto full = NpcWindows::ReadTalkResult(PacketOf(Bytes({0xc1, 0x0a, 0x30, 3, 10, 20, 30, 40, 50, 60})));
    ASSERT_TRUE(full.HasValue());
    EXPECT_EQ(full.Value().result, 3);
    EXPECT_EQ(full.Value().ratesInPacket, 6u);
    EXPECT_EQ(full.Value().rates, (std::array<std::uint8_t, 6>{10, 20, 30, 40, 50, 60}));
    const auto short97 = NpcWindows::ReadTalkResult(PacketOf(Bytes({0xc1, 0x08, 0x30, 3, 10, 20, 30, 40})));
    ASSERT_TRUE(short97.HasValue());
    EXPECT_EQ(short97.Value().ratesInPacket, 4u);
    EXPECT_EQ(short97.Value().rates, (std::array<std::uint8_t, 6>{10, 20, 30, 40, 0, 0}));
    const auto shop = NpcWindows::ReadTalkResult(PacketOf(Bytes({0xc1, 0x04, 0x30, 0})));
    ASSERT_TRUE(shop.HasValue());
    EXPECT_EQ(shop.Value().result, 0);
    EXPECT_EQ(ErrorOf(NpcWindows::ReadTalkResult(PacketOf(Bytes({0xc1, 0x04, 0x31, 0})))), ProtocolError::WrongPacket);
}

// 0x4c5f60: el C2 31 con la grilla, la cuenta y las entradas de 8 bytes.
TEST(NpcWindowsProtocolTest, ReadsTheItemList)
{
    const auto list = NpcWindows::ReadItemList(PacketOf(Bytes({0xc2, 0x00, 0x16, 0x31, 0, 2,       //
                                                               0x00, 0x03, 0x18, 0x20, 0, 0, 0, 0, //
                                                               0x10, 0x03, 0x18, 0x20, 0, 0, 0, 0})));
    ASSERT_TRUE(list.HasValue());
    EXPECT_EQ(list.Value().grid, 0);
    ASSERT_EQ(list.Value().items.size(), 2u);
    EXPECT_EQ(list.Value().items[1].slot, 0x10);
    EXPECT_EQ(list.Value().items[1].item, InventedItem);
    EXPECT_EQ(ErrorOf(NpcWindows::ReadItemList(PacketOf(Bytes({0xc2, 0x00, 0x0b, 0x31, 3, 1, 0, 3, 0x18, 0x20, 0})))),
              ProtocolError::TooShort);
}

// 0x4e3c33.., 0x4e3c8a.., 0x4e3cc1.. y 0x4e4692..: el 32, el 33, el 34 y el 81.
TEST(NpcWindowsProtocolTest, ReadsTheBuySellRepairAndMoneyResults)
{
    const auto bought = NpcWindows::ReadBuyResult(PacketOf(Bytes({0xc1, 0x0b, 0x32, 0x14, 3, 0x18, 0x20, 0, 0, 0, 0})));
    ASSERT_TRUE(bought.HasValue());
    EXPECT_EQ(bought.Value().slot, 0x14);
    EXPECT_EQ(bought.Value().item, InventedItem);
    const auto rejected = NpcWindows::ReadBuyResult(PacketOf(Bytes({0xc1, 0x04, 0x32, 0xff})));
    ASSERT_TRUE(rejected.HasValue());
    EXPECT_EQ(rejected.Value().slot, NpcWindows::BuyRejected);

    const auto sold = NpcWindows::ReadSellResult(PacketOf(Bytes({0xc1, 0x08, 0x33, 1, 0x10, 0x27, 0, 0})));
    ASSERT_TRUE(sold.HasValue());
    EXPECT_EQ(sold.Value().money, 10000u);
    const auto failed = NpcWindows::ReadSellResult(PacketOf(Bytes({0xc1, 0x04, 0x33, 0})));
    ASSERT_TRUE(failed.HasValue());
    EXPECT_EQ(failed.Value().result, 0);

    const auto repaired = NpcWindows::ReadRepairResult(PacketOf(Bytes({0xc1, 0x08, 0x34, 0, 0xe8, 0x03, 0, 0})));
    ASSERT_TRUE(repaired.HasValue());
    EXPECT_EQ(repaired.Value().money, 1000u);

    const auto money =
        NpcWindows::ReadWarehouseMoney(PacketOf(Bytes({0xc1, 0x0c, 0x81, 1, 0x64, 0, 0, 0, 0xc8, 0, 0, 0})));
    ASSERT_TRUE(money.HasValue());
    EXPECT_EQ(money.Value().warehouseMoney, 100u);
    EXPECT_EQ(money.Value().inventoryMoney, 200u);
    EXPECT_EQ(ErrorOf(NpcWindows::ReadWarehouseMoney(PacketOf(Bytes({0xc1, 0x08, 0x81, 1, 0x64, 0, 0, 0})))),
              ProtocolError::TooShort);
}

// 0x4e46df, 0x4e4819 y 0x4da280: el 83, el 82 y el 87, y el 86.
TEST(NpcWindowsProtocolTest, ReadsTheWarehouseStateTheClosesAndTheMix)
{
    const auto state = NpcWindows::ReadWarehouseState(PacketOf(Bytes({0xc1, 0x04, 0x83, 0x0c})));
    ASSERT_TRUE(state.HasValue());
    EXPECT_EQ(state.Value().state, 0x0c);
    const auto closed = NpcWindows::ReadNpcWindowClosed(PacketOf(Bytes({0xc1, 0x03, 0x87})));
    ASSERT_TRUE(closed.HasValue());
    EXPECT_EQ(closed.Value().opcode, 0x87);
    EXPECT_EQ(ErrorOf(NpcWindows::ReadNpcWindowClosed(PacketOf(Bytes({0xc1, 0x03, 0x83})))),
              ProtocolError::WrongPacket);
    const auto mixed =
        NpcWindows::ReadChaosMixResult(PacketOf(Bytes({0xc1, 0x0b, 0x86, 1, 3, 0x18, 0x20, 0, 0, 0, 0})));
    ASSERT_TRUE(mixed.HasValue());
    EXPECT_EQ(mixed.Value().item, InventedItem);
    const auto lost = NpcWindows::ReadChaosMixResult(PacketOf(Bytes({0xc1, 0x04, 0x86, 0})));
    ASSERT_TRUE(lost.HasValue());
    EXPECT_EQ(lost.Value().result, 0);
}
} // namespace
