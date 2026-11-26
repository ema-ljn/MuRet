#include "Protocol/NpcWindows.h"

#include "Core/ByteReader.h"
#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/PacketWriter.h"
#include "Protocol/Outgoing.h"
#include "Network/ProtocolError.h"
#include "Protocol/Opcodes.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace Client::Protocol::NpcWindows
{
namespace
{
// Los handlers leen el paquete por su posición, como el original: pkt[3] es el primer campo de un C1.
constexpr std::size_t FirstField = 3;
// 0x4c5f60: la grilla en pkt[4], la cuenta en pkt[5] y las entradas de 8 bytes (el lugar y el ítem) desde pkt[6].
constexpr std::size_t ListGridField = 4;
// 0x4d1e30..0x4d24fb: el byte que cierra el aviso, el opcode que no llegó cifrado.
constexpr std::uint8_t ClearTalkHead = Network::ByteOf(TalkOpcode);

// Un paquete de `opcode`, sin mirar si es C1 o C2: el lector desde `field`.
Core::Expected<Core::ByteReader, Network::ProtocolError> FieldsOf(const Network::Packet& packet, Network::Opcode opcode,
                                                                  std::size_t field = FirstField)
{
    if (packet.Opcode() != opcode)
        return Core::Unexpected(Network::ProtocolError::WrongPacket);
    const std::span<const std::byte> bytes = packet.Bytes();
    if (bytes.size() < field)
        return Core::Unexpected(Network::ProtocolError::TooShort);
    return Core::ByteReader(bytes.subspan(field));
}

std::optional<ItemData> ReadItem(Core::ByteReader& reader)
{
    const auto bytes = reader.ReadBytes(ItemDataSize);
    if (!bytes)
        return std::nullopt;
    ItemData item{};
    std::ranges::copy(*bytes, item.begin());
    return item;
}

// Un C1 de `opcode` sin campos, en claro.
Outgoing EmptyClearC1(Network::Opcode opcode)
{
    return ClearC1(Network::PacketWriter::C1(opcode));
}
} // namespace

Core::Expected<TalkResult, Network::ProtocolError> ReadTalkResult(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, TalkOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto result = reader.ReadU8();
    if (!result)
        return Core::Unexpected(Network::ProtocolError::TooShort);
    TalkResult talk{.result = *result};
    for (std::uint8_t& rate : talk.rates)
    {
        const auto value = reader.ReadU8();
        if (!value)
            break;
        rate = *value;
        ++talk.ratesInPacket;
    }
    constexpr std::uint8_t SeniorReply = 12; // 0x4d25bc: pkt4 decide el cierre87 frente a31.
    if (talk.result == SeniorReply && talk.ratesInPacket == 0)
        return Core::Unexpected(Network::ProtocolError::TooShort);
    return talk;
}

Outgoing WriteClearTalkReport()
{
    return EncryptedC1(Network::PacketWriter::C1(JoinOpcode, Join::DecryptErrorSubcode).PutU8(0).PutU8(ClearTalkHead));
}

Core::Expected<ItemList, Network::ProtocolError> ReadItemList(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, NpcItemListOpcode, ListGridField);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto grid = reader.ReadU8();
    const auto count = reader.ReadU8();
    if (!grid || !count)
        return Core::Unexpected(Network::ProtocolError::TooShort);
    ItemList list{.grid = *grid, .items = {}};
    list.items.reserve(*count);
    for (std::uint8_t entry = 0; entry < *count; ++entry)
    {
        const auto slot = reader.ReadU8();
        const auto item = ReadItem(reader);
        if (!slot || !item)
            return Core::Unexpected(Network::ProtocolError::TooShort);
        list.items.push_back(ListedItem{.slot = *slot, .item = *item});
    }
    return list;
}

Core::Expected<BuyResult, Network::ProtocolError> ReadBuyResult(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, BuyOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto slot = reader.ReadU8();
    if (!slot)
        return Core::Unexpected(Network::ProtocolError::TooShort);
    if (*slot == BuyRejected)
        return BuyResult{.slot = *slot, .item = {}};
    const auto item = ReadItem(reader);
    if (!item)
        return Core::Unexpected(Network::ProtocolError::TooShort);
    return BuyResult{.slot = *slot, .item = *item};
}

Core::Expected<SellResult, Network::ProtocolError> ReadSellResult(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, SellOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto result = reader.ReadU8();
    if (!result)
        return Core::Unexpected(Network::ProtocolError::TooShort);
    if (*result == 0)
        return SellResult{.result = 0, .money = 0};
    // pkt[4..7], el int que sigue al byte del resultado (Money en +4 del PMSG_SELLRESULT).
    const auto money = reader.ReadU32Le();
    if (!money)
        return Core::Unexpected(Network::ProtocolError::TooShort);
    return SellResult{.result = *result, .money = *money};
}

Core::Expected<RepairResult, Network::ProtocolError> ReadRepairResult(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, RepairOpcode, FirstField + 1);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    const auto money = fields.Value().ReadU32Le();
    if (!money)
        return Core::Unexpected(Network::ProtocolError::TooShort);
    return RepairResult{.money = *money};
}

Core::Expected<WarehouseMoney, Network::ProtocolError> ReadWarehouseMoney(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, WarehouseMoneyOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto result = reader.ReadU8();
    if (!result)
        return Core::Unexpected(Network::ProtocolError::TooShort);
    if (*result == 0)
        return WarehouseMoney{};
    const auto warehouse = reader.ReadU32Le();
    const auto inventory = reader.ReadU32Le();
    if (!warehouse || !inventory)
        return Core::Unexpected(Network::ProtocolError::TooShort);
    return WarehouseMoney{.result = *result, .warehouseMoney = *warehouse, .inventoryMoney = *inventory};
}

Core::Expected<NpcWindowClosed, Network::ProtocolError> ReadNpcWindowClosed(const Network::Packet& packet)
{
    const auto opcode = packet.Opcode();
    if (opcode != WarehouseCloseOpcode && opcode != ChaosBoxCloseOpcode)
        return Core::Unexpected(Network::ProtocolError::WrongPacket);
    return NpcWindowClosed{.opcode = *opcode};
}

Core::Expected<WarehouseState, Network::ProtocolError> ReadWarehouseState(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, WarehouseStateOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    const auto state = fields.Value().ReadU8();
    if (!state)
        return Core::Unexpected(Network::ProtocolError::TooShort);
    return WarehouseState{.state = *state};
}

Core::Expected<ChaosMixResult, Network::ProtocolError> ReadChaosMixResult(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, ChaosMixOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto result = reader.ReadU8();
    if (!result)
        return Core::Unexpected(Network::ProtocolError::TooShort);
    // 0x4da2ff y 0x4da378: solo el 1 y el 100 leen el ítem desde pkt[4].
    if (*result != ChaosMixSucceeded && *result != TrainerMixResult)
        return ChaosMixResult{.result = *result, .item = {}};
    const auto item = ReadItem(reader);
    if (!item)
        return Core::Unexpected(Network::ProtocolError::TooShort);
    return ChaosMixResult{.result = *result, .item = *item};
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
} // namespace Client::Protocol::NpcWindows
