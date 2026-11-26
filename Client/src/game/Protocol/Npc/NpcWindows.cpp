#include "Protocol/Npc/NpcWindows.h"

#include "Core/Expected.h"
#include "Network/Field.h"
#include "Network/LayoutReader.h"
#include "Network/Packet.h"
#include "Network/PacketLayout.h"
#include "Network/PacketWriter.h"
#include "Network/ProtocolError.h"
#include "Network/ReadList.h"
#include "Protocol/Opcodes.h"
#include "Protocol/Outgoing.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace Client::Protocol::Npc
{
namespace
{
// pkt[3]: the first field of a C1, which the handlers read by its place, as the original does.
using Code = Network::Field<std::uint8_t, 3>;
// 0x4d1e30..0x4d24fb: the byte that closes the report, the opcode that did not arrive encrypted.
constexpr std::uint8_t ClearTalkHead = Network::ByteOf(TalkOpcode);
constexpr std::size_t RatesAt = 4;
// 0x4d25bc: pkt[4] decides between closing with the 87 and with the 31.
constexpr std::uint8_t SeniorReply = 12;
// 0x4c5f60: the grid at pkt[4], the count at pkt[5] and the 8-byte entries (the place and the item) from pkt[6].
using ListGrid = Network::Field<std::uint8_t, 4>;
using ListCount = Network::Field<std::uint8_t, 5>;
constexpr std::size_t ListedItemSize = 1 + ItemDataSize;
using ItemAfterCode = Network::Field<ItemData, 4>;
// pkt[4..7], the int after the result's byte (Money at +4 of PMSG_SELLRESULT).
using Money = Network::Field<std::uint32_t, 4>;
using InventoryMoney = Network::Field<std::uint32_t, 8>;

// A packet of `opcode`, whether a C1 or a C2: the handlers read its fields at fixed places.
Network::PacketLayout LayoutOf(const Network::Packet& packet, Network::Opcode opcode, std::size_t minimumSize,
                               std::uint32_t address)
{
    return {{packet.Type(), opcode, std::nullopt}, minimumSize, address};
}

ListedItem ReadListedItem(std::span<const std::byte, ListedItemSize> bytes)
{
    ListedItem listed{.slot = std::to_integer<std::uint8_t>(bytes[0])};
    std::ranges::copy(bytes.subspan<1, ItemDataSize>(), listed.item.begin());
    return listed;
}

// A C1 of `opcode` with no fields, in the clear.
Outgoing EmptyClearC1(Network::Opcode opcode)
{
    return ClearC1(Network::PacketWriter::C1(opcode));
}
} // namespace

Core::Expected<TalkResult, Network::ProtocolError> ReadTalkResult(const Network::Packet& packet)
{
    Network::ProtocolResult<Network::LayoutReader> opened =
        Network::LayoutReader::Open(packet, LayoutOf(packet, TalkOpcode, Code::End, 0x4d1e00));
    if (!opened.HasValue())
    {
        return Core::Unexpected(opened.Error());
    }
    Network::LayoutReader& reader = opened.Value();
    TalkResult talk{.result = reader.Read<Code>()};
    talk.ratesInPacket = std::min(ChaosRateCount, reader.Size() - RatesAt);
    std::ranges::transform(reader.Bytes(RatesAt, talk.ratesInPacket), talk.rates.begin(),
                           [](std::byte b) { return std::to_integer<std::uint8_t>(b); });
    if (talk.result == SeniorReply && talk.ratesInPacket == 0)
    {
        return Core::Unexpected(Network::ProtocolError::TooShort);
    }
    return talk;
}

Outgoing WriteClearTalkReport()
{
    return EncryptedC1(Network::PacketWriter::C1(JoinOpcode, Join::DecryptErrorSubcode).PutU8(0).PutU8(ClearTalkHead));
}

Core::Expected<ItemList, Network::ProtocolError> ReadItemList(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, LayoutOf(packet, NpcItemListOpcode, ListCount::End, 0x4c5f60),
                               [](Network::LayoutReader& reader)
                               {
                                   return ItemList{.grid = reader.Read<ListGrid>(),
                                                   .items = Network::ReadList<ListedItemSize>(reader, ListCount::End,
                                                                                              reader.Read<ListCount>(),
                                                                                              ReadListedItem)};
                               });
}

Core::Expected<BuyResult, Network::ProtocolError> ReadBuyResult(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, LayoutOf(packet, BuyOpcode, Code::End, 0x4e3c33),
                               [](Network::LayoutReader& reader)
                               {
                                   BuyResult bought{.slot = reader.Read<Code>()};
                                   if (bought.slot != BuyRejected)
                                   {
                                       bought.item = reader.Read<ItemAfterCode>();
                                   }
                                   return bought;
                               });
}

Core::Expected<SellResult, Network::ProtocolError> ReadSellResult(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, LayoutOf(packet, SellOpcode, Code::End, 0x4e3c8a),
                               [](Network::LayoutReader& reader)
                               {
                                   SellResult sold{.result = reader.Read<Code>()};
                                   if (sold.result != 0)
                                   {
                                       sold.money = reader.Read<Money>();
                                   }
                                   return sold;
                               });
}

Core::Expected<RepairResult, Network::ProtocolError> ReadRepairResult(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, LayoutOf(packet, RepairOpcode, Money::End, 0x4e3cc1),
                               [](Network::LayoutReader& reader)
                               { return RepairResult{.money = reader.Read<Money>()}; });
}

Core::Expected<WarehouseMoney, Network::ProtocolError> ReadWarehouseMoney(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, LayoutOf(packet, WarehouseMoneyOpcode, Code::End, 0x4e4692),
                               [](Network::LayoutReader& reader)
                               {
                                   const std::uint8_t result = reader.Read<Code>();
                                   if (result == 0)
                                   {
                                       return WarehouseMoney{};
                                   }
                                   return WarehouseMoney{.result = result,
                                                         .warehouseMoney = reader.Read<Money>(),
                                                         .inventoryMoney = reader.Read<InventoryMoney>()};
                               });
}

// Reads no field: only the opcode, 0x82 or 0x87.
Core::Expected<NpcWindowClosed, Network::ProtocolError> ReadNpcWindowClosed(const Network::Packet& packet)
{
    const auto opcode = packet.Opcode();
    if (opcode != WarehouseCloseOpcode && opcode != ChaosBoxCloseOpcode)
    {
        return Core::Unexpected(Network::ProtocolError::WrongPacket);
    }
    return NpcWindowClosed{.opcode = *opcode};
}

Core::Expected<WarehouseState, Network::ProtocolError> ReadWarehouseState(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, LayoutOf(packet, WarehouseStateOpcode, Code::End, 0x4e46df),
                               [](Network::LayoutReader& reader)
                               { return WarehouseState{.state = reader.Read<Code>()}; });
}

// 0x4da2ff and 0x4da378: only the 1 and the 100 read the item from pkt[4].
Core::Expected<ChaosMixResult, Network::ProtocolError> ReadChaosMixResult(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, LayoutOf(packet, ChaosMixOpcode, Code::End, 0x4da280),
                               [](Network::LayoutReader& reader)
                               {
                                   ChaosMixResult mixed{.result = reader.Read<Code>()};
                                   if (mixed.result == ChaosMixSucceeded || mixed.result == TrainerMixResult)
                                   {
                                       mixed.item = reader.Read<ItemAfterCode>();
                                   }
                                   return mixed;
                               });
}

Outgoing WriteBuy(std::uint8_t slot)
{
    return EncryptedC1(Network::PacketWriter::C1(BuyOpcode).PutU8(slot));
}

Outgoing WriteSell(std::uint8_t slot)
{
    return EncryptedC1(Network::PacketWriter::C1(SellOpcode).PutU8(slot));
}

Outgoing WriteRepair(std::uint8_t position, std::uint8_t requestPosition)
{
    return EncryptedC1(Network::PacketWriter::C1(RepairOpcode).PutU8(position).PutU8(requestPosition));
}

Outgoing WriteWarehouseMoney(std::uint8_t type, std::uint32_t money)
{
    return Xor32C1(Network::PacketWriter::C1(WarehouseMoneyOpcode).PutU8(type).PutU32Le(money));
}

Outgoing WriteWarehouseClose()
{
    return EmptyClearC1(WarehouseCloseOpcode);
}

Outgoing WriteWarehousePassword(std::uint8_t type, std::uint16_t password, const WarehouseNumber& number)
{
    return EncryptedC1(Network::PacketWriter::C1(WarehouseStateOpcode).PutU8(type).PutU16Le(password).PutBytes(number));
}

Outgoing WritePersonalNumber(const WarehouseNumber& number)
{
    return EncryptedC1(Network::PacketWriter::C1(PersonalNumberOpcode).PutBytes(number));
}

Outgoing WriteChaosMix(std::uint8_t kind)
{
    return Xor32C1(Network::PacketWriter::C1(ChaosMixOpcode).PutU8(kind));
}

Outgoing WriteChaosBoxClose()
{
    return EmptyClearC1(ChaosBoxCloseOpcode);
}

Outgoing WriteChaosRateRequest()
{
    return Xor32C1(Network::PacketWriter::C1(CrywolfOpcode, Crywolf::ChaosBoxSubcode));
}
} // namespace Client::Protocol::Npc
