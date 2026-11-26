#include "Protocol/Social/Trade.h"

#include "Core/ByteReader.h"
#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/PacketWriter.h"
#include "Protocol/FrontEnd/CharacterName.h"
#include "Protocol/Items/Inventory.h"
#include "Protocol/Outgoing.h"
#include "Network/ProtocolError.h"
#include "Protocol/Opcodes.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace Client::Protocol::Social
{
namespace
{
// 0x626bd8: el byte que va antes del zen en el 3A.
constexpr std::uint8_t MoneyPad = 0;
constexpr std::size_t ResultHeadSize = 4;

// Un C1 de `opcode`: el lector desde pkt[3].
Core::Expected<Core::ByteReader, Network::ProtocolError> FieldsOf(const Network::Packet& packet, Network::Opcode opcode)
{
    if (packet.Type() != Network::PacketType::C1 || packet.Opcode() != opcode)
        return Core::Unexpected(Network::ProtocolError::WrongPacket);
    return Core::ByteReader(packet.Bytes().subspan(Network::MinPacketSize(Network::PacketType::C1)));
}

std::optional<FrontEnd::NameBytes> ReadName(Core::ByteReader& reader)
{
    const auto bytes = reader.ReadBytes(FrontEnd::NameSize);
    if (!bytes)
        return std::nullopt;
    FrontEnd::NameBytes name{};
    std::ranges::copy(*bytes, name.begin());
    return name;
}

// Un byte desde pkt[3]: el 38, el 3A del GS y el 3C del GS.
Core::Expected<std::uint8_t, Network::ProtocolError> ReadTheByte(const Network::Packet& packet, Network::Opcode opcode)
{
    auto fields = FieldsOf(packet, opcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    const auto value = fields.Value().ReadU8();
    if (!value)
        return Core::Unexpected(Network::ProtocolError::TooShort);
    return *value;
}
} // namespace

Core::Expected<TradeRequest, Network::ProtocolError> ReadTradeRequest(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, TradeRequestOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    const auto name = ReadName(fields.Value());
    if (!name)
        return Core::Unexpected(Network::ProtocolError::TooShort);
    return TradeRequest{*name};
}

Core::Expected<TradeResponse, Network::ProtocolError> ReadTradeResponse(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, TradeResponseOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto response = reader.ReadU8();
    const auto name = ReadName(reader);
    const auto level = reader.ReadU16Le();
    const auto guild = reader.ReadU32Le();
    if (!response || !name || !level || !guild)
        return Core::Unexpected(Network::ProtocolError::TooShort);
    return TradeResponse{*response, *name, *level, *guild};
}

Core::Expected<TradeItemRemoved, Network::ProtocolError> ReadTradeItemRemoved(const Network::Packet& packet)
{
    const auto slot = ReadTheByte(packet, TradeItemRemovedOpcode);
    if (!slot.HasValue())
        return Core::Unexpected(slot.Error());
    return TradeItemRemoved{slot.Value()};
}

Core::Expected<TradeItemAdded, Network::ProtocolError> ReadTradeItemAdded(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, TradeItemAddedOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto slot = reader.ReadU8();
    const auto bytes = reader.ReadBytes(Items::ItemDataSize);
    if (!slot || !bytes)
        return Core::Unexpected(Network::ProtocolError::TooShort);
    TradeItemAdded added{.slot = *slot};
    std::ranges::copy(*bytes, added.item.begin());
    return added;
}

Core::Expected<TradeMoneyResult, Network::ProtocolError> ReadTradeMoneyResult(const Network::Packet& packet)
{
    const auto accepted = ReadTheByte(packet, TradeMoneyOpcode);
    if (!accepted.HasValue())
        return Core::Unexpected(accepted.Error());
    return TradeMoneyResult{accepted.Value()};
}

Core::Expected<TradeOtherMoney, Network::ProtocolError> ReadTradeOtherMoney(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, TradeOtherMoneyOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto pad = reader.ReadU8();
    const auto money = reader.ReadU32Le();
    if (!pad || !money)
        return Core::Unexpected(Network::ProtocolError::TooShort);
    return TradeOtherMoney{*money};
}

Core::Expected<TradeOkState, Network::ProtocolError> ReadTradeOkState(const Network::Packet& packet)
{
    const auto state = ReadTheByte(packet, TradeOkButtonOpcode);
    if (!state.HasValue())
        return Core::Unexpected(state.Error());
    return TradeOkState{state.Value()};
}

Core::Expected<TradeResult, Network::ProtocolError> ReadTradeResult(const Network::Packet& packet)
{
    const auto result = ReadTheByte(packet, TradeResultOpcode);
    if (!result.HasValue())
        return Core::Unexpected(result.Error());
    TradeResult read{.result = result.Value()};
    const auto bytes = packet.Bytes().first(ResultHeadSize);
    std::ranges::transform(bytes, read.head.begin(), [](std::byte b) { return std::to_integer<std::uint8_t>(b); });
    return read;
}

Outgoing WriteTradeRequest(std::uint16_t key)
{
    return EncryptedC1(Network::PacketWriter::C1(TradeRequestOpcode)
                           .PutU8(static_cast<std::uint8_t>(key >> 8))
                           .PutU8(static_cast<std::uint8_t>(key & 0xff)));
}

Outgoing WriteTradeResponse(std::uint8_t response)
{
    return Xor32C1(Network::PacketWriter::C1(TradeResponseOpcode).PutU8(response));
}

Outgoing WriteTradeMoney(std::uint32_t money)
{
    return Xor32C1(Network::PacketWriter::C1(TradeMoneyOpcode).PutU8(MoneyPad).PutU32Le(money));
}

Outgoing WriteTradeOkButton(std::uint8_t state)
{
    return EncryptedC1(Network::PacketWriter::C1(TradeOkButtonOpcode).PutU8(state));
}

Outgoing WriteTradeCancel()
{
    return EncryptedC1(Network::PacketWriter::C1(TradeResultOpcode));
}

Outgoing WriteClearTradeRequestReport()
{
    return EncryptedC1(Network::PacketWriter::C1(JoinOpcode, Join::DecryptErrorSubcode)
                           .PutU8(0)
                           .PutU8(Network::ByteOf(TradeRequestOpcode)));
}
} // namespace Client::Protocol::Social
