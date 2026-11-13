#include "Protocol/Trade.h"

#include "Core/ByteReader.h"
#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/PacketWriter.h"
#include "Protocol/Character/Opcodes.h"
#include "Protocol/Connection/Opcodes.h"
#include "Protocol/Inventory.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace Client::Protocol::Trade
{
namespace
{
// 0x626bd8: el byte que va antes del zen en el 3A.
constexpr std::uint8_t MoneyPad = 0;
constexpr std::size_t ResultHeadSize = 4;

// Un C1 de `opcode`: el lector desde pkt[3].
Core::Expected<Core::ByteReader, ProtocolError> FieldsOf(const Network::Packet& packet, std::uint8_t opcode)
{
    if (packet.Type() != Network::PacketType::C1 || packet.Opcode() != opcode)
        return Core::Unexpected(ProtocolError::WrongPacket);
    return Core::ByteReader(packet.Bytes().subspan(Network::MinPacketSize(Network::PacketType::C1)));
}

std::optional<Name> ReadName(Core::ByteReader& reader)
{
    const auto bytes = reader.ReadBytes(Character::NameSize);
    if (!bytes)
        return std::nullopt;
    Name name{};
    std::ranges::copy(*bytes, name.begin());
    return name;
}

// Un byte desde pkt[3]: el 38, el 3A del GS y el 3C del GS.
Core::Expected<std::uint8_t, ProtocolError> ReadTheByte(const Network::Packet& packet, std::uint8_t opcode)
{
    auto fields = FieldsOf(packet, opcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    const auto value = fields.Value().ReadU8();
    if (!value)
        return Core::Unexpected(ProtocolError::TooShort);
    return *value;
}
} // namespace

Core::Expected<TradeRequest, ProtocolError> ReadTradeRequest(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, RequestOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    const auto name = ReadName(fields.Value());
    if (!name)
        return Core::Unexpected(ProtocolError::TooShort);
    return TradeRequest{*name};
}

Core::Expected<TradeResponse, ProtocolError> ReadTradeResponse(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, ResponseOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto response = reader.ReadU8();
    const auto name = ReadName(reader);
    const auto level = reader.ReadU16Le();
    const auto guild = reader.ReadU32Le();
    if (!response || !name || !level || !guild)
        return Core::Unexpected(ProtocolError::TooShort);
    return TradeResponse{*response, *name, *level, *guild};
}

Core::Expected<TradeItemRemoved, ProtocolError> ReadTradeItemRemoved(const Network::Packet& packet)
{
    const auto slot = ReadTheByte(packet, ItemRemovedOpcode);
    if (!slot.HasValue())
        return Core::Unexpected(slot.Error());
    return TradeItemRemoved{slot.Value()};
}

Core::Expected<TradeItemAdded, ProtocolError> ReadTradeItemAdded(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, ItemAddedOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto slot = reader.ReadU8();
    const auto bytes = reader.ReadBytes(Inventory::ItemDataSize);
    if (!slot || !bytes)
        return Core::Unexpected(ProtocolError::TooShort);
    TradeItemAdded added{.slot = *slot};
    std::ranges::copy(*bytes, added.item.begin());
    return added;
}

Core::Expected<TradeMoneyResult, ProtocolError> ReadTradeMoneyResult(const Network::Packet& packet)
{
    const auto accepted = ReadTheByte(packet, MoneyOpcode);
    if (!accepted.HasValue())
        return Core::Unexpected(accepted.Error());
    return TradeMoneyResult{accepted.Value()};
}

Core::Expected<TradeOtherMoney, ProtocolError> ReadTradeOtherMoney(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, OtherMoneyOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto pad = reader.ReadU8();
    const auto money = reader.ReadU32Le();
    if (!pad || !money)
        return Core::Unexpected(ProtocolError::TooShort);
    return TradeOtherMoney{*money};
}

Core::Expected<TradeOkState, ProtocolError> ReadTradeOkState(const Network::Packet& packet)
{
    const auto state = ReadTheByte(packet, OkButtonOpcode);
    if (!state.HasValue())
        return Core::Unexpected(state.Error());
    return TradeOkState{state.Value()};
}

Core::Expected<TradeResult, ProtocolError> ReadTradeResult(const Network::Packet& packet)
{
    const auto result = ReadTheByte(packet, ResultOpcode);
    if (!result.HasValue())
        return Core::Unexpected(result.Error());
    TradeResult read{.result = result.Value()};
    const auto bytes = packet.Bytes().first(ResultHeadSize);
    std::ranges::transform(bytes, read.head.begin(), [](std::byte b) { return std::to_integer<std::uint8_t>(b); });
    return read;
}

Outgoing WriteTradeRequest(std::uint16_t key)
{
    return EncryptedC1(Network::PacketWriter::C1(RequestOpcode)
                           .PutU8(static_cast<std::uint8_t>(key >> 8))
                           .PutU8(static_cast<std::uint8_t>(key & 0xff)));
}

Outgoing WriteTradeResponse(std::uint8_t response)
{
    return Xor32C1(Network::PacketWriter::C1(ResponseOpcode).PutU8(response));
}

Outgoing WriteTradeMoney(std::uint32_t money)
{
    return Xor32C1(Network::PacketWriter::C1(MoneyOpcode).PutU8(MoneyPad).PutU32Le(money));
}

Outgoing WriteTradeOkButton(std::uint8_t state)
{
    return EncryptedC1(Network::PacketWriter::C1(OkButtonOpcode).PutU8(state));
}

Outgoing WriteTradeCancel()
{
    return EncryptedC1(Network::PacketWriter::C1(ResultOpcode));
}

Outgoing WriteClearTradeRequestReport()
{
    return EncryptedC1(Network::PacketWriter::C1(Connection::JoinOpcode)
                           .PutU8(Connection::DecryptErrorSubcode)
                           .PutU8(0)
                           .PutU8(RequestOpcode));
}
} // namespace Client::Protocol::Trade
