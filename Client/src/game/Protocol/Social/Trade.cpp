#include "Protocol/Social/Trade.h"

#include "Core/Expected.h"
#include "Network/Field.h"
#include "Network/LayoutReader.h"
#include "Network/Packet.h"
#include "Network/PacketLayout.h"
#include "Network/PacketWriter.h"
#include "Network/ProtocolError.h"
#include "Protocol/FrontEnd/CharacterName.h"
#include "Protocol/Items/Inventory.h"
#include "Protocol/Opcodes.h"
#include "Protocol/Outgoing.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace Client::Protocol::Social
{
namespace
{
// 0x626bd8: the byte before the zen in the 3A.
constexpr std::uint8_t MoneyPad = 0;

using RequesterName = Network::Field<FrontEnd::NameBytes, 3>;
constexpr Network::PacketLayout TradeRequestLayout{
    {Network::PacketType::C1, TradeRequestOpcode, std::nullopt}, RequesterName::End, 0x4d4af0};

using Response = Network::Field<std::uint8_t, 3>;
using ResponderName = Network::Field<FrontEnd::NameBytes, 4>;
using ResponderLevel = Network::Field<std::uint16_t, 0xe>;
using ResponderGuild = Network::Field<std::uint32_t, 0x10>;
constexpr Network::PacketLayout TradeResponseLayout{
    {Network::PacketType::C1, TradeResponseOpcode, std::nullopt}, ResponderGuild::End, 0x4d5570};

// The 38, the GS's 3A, the 3C and the 3D: one byte at pkt[3].
using TheByte = Network::Field<std::uint8_t, 3>;
constexpr Network::PacketLayout TradeItemRemovedLayout{
    {Network::PacketType::C1, TradeItemRemovedOpcode, std::nullopt}, TheByte::End, 0x4e3d6a};
constexpr Network::PacketLayout TradeMoneyResultLayout{
    {Network::PacketType::C1, TradeMoneyOpcode, std::nullopt}, TheByte::End, 0x4e3dd0};
constexpr Network::PacketLayout TradeOkStateLayout{
    {Network::PacketType::C1, TradeOkButtonOpcode, std::nullopt}, TheByte::End, 0x4e3e0a};
constexpr Network::PacketLayout TradeResultLayout{
    {Network::PacketType::C1, TradeResultOpcode, std::nullopt}, TheByte::End, 0x4d57f0};
constexpr std::size_t ResultHeadSize = 4;

using AddedSlot = Network::Field<std::uint8_t, 3>;
using AddedItem = Network::Field<Items::ItemData, 4>;
constexpr Network::PacketLayout TradeItemAddedLayout{
    {Network::PacketType::C1, TradeItemAddedOpcode, std::nullopt}, AddedItem::End, 0x4e3d99};

// Nobody reads pkt[3].
using OtherMoney = Network::Field<std::uint32_t, 4>;
constexpr Network::PacketLayout TradeOtherMoneyLayout{
    {Network::PacketType::C1, TradeOtherMoneyOpcode, std::nullopt}, OtherMoney::End, 0x4e3df3};
} // namespace

Core::Expected<TradeRequest, Network::ProtocolError> ReadTradeRequest(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, TradeRequestLayout, [](Network::LayoutReader& reader)
                               { return TradeRequest{reader.Read<RequesterName>()}; });
}

Core::Expected<TradeResponse, Network::ProtocolError> ReadTradeResponse(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, TradeResponseLayout,
                               [](Network::LayoutReader& reader)
                               {
                                   return TradeResponse{reader.Read<Response>(), reader.Read<ResponderName>(),
                                                        reader.Read<ResponderLevel>(), reader.Read<ResponderGuild>()};
                               });
}

Core::Expected<TradeItemRemoved, Network::ProtocolError> ReadTradeItemRemoved(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, TradeItemRemovedLayout,
                               [](Network::LayoutReader& reader) { return TradeItemRemoved{reader.Read<TheByte>()}; });
}

Core::Expected<TradeItemAdded, Network::ProtocolError> ReadTradeItemAdded(const Network::Packet& packet)
{
    return Network::ReadPacket(
        packet, TradeItemAddedLayout, [](Network::LayoutReader& reader)
        { return TradeItemAdded{.slot = reader.Read<AddedSlot>(), .item = reader.Read<AddedItem>()}; });
}

Core::Expected<TradeMoneyResult, Network::ProtocolError> ReadTradeMoneyResult(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, TradeMoneyResultLayout,
                               [](Network::LayoutReader& reader) { return TradeMoneyResult{reader.Read<TheByte>()}; });
}

Core::Expected<TradeOtherMoney, Network::ProtocolError> ReadTradeOtherMoney(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, TradeOtherMoneyLayout, [](Network::LayoutReader& reader)
                               { return TradeOtherMoney{reader.Read<OtherMoney>()}; });
}

Core::Expected<TradeOkState, Network::ProtocolError> ReadTradeOkState(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, TradeOkStateLayout,
                               [](Network::LayoutReader& reader) { return TradeOkState{reader.Read<TheByte>()}; });
}

Core::Expected<TradeResult, Network::ProtocolError> ReadTradeResult(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, TradeResultLayout,
                               [](Network::LayoutReader& reader)
                               {
                                   TradeResult read{.result = reader.Read<TheByte>()};
                                   std::ranges::transform(reader.Bytes(0, ResultHeadSize), read.head.begin(),
                                                          [](std::byte b) { return std::to_integer<std::uint8_t>(b); });
                                   return read;
                               });
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
