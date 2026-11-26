#include "Protocol/Quests/Quest.h"

#include "Core/Expected.h"
#include "Network/Field.h"
#include "Network/LayoutReader.h"
#include "Network/Packet.h"
#include "Network/PacketLayout.h"
#include "Network/PacketWriter.h"
#include "Network/ProtocolError.h"
#include "Protocol/Opcodes.h"
#include "Protocol/Outgoing.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>

namespace Client::Protocol::Quests
{
namespace
{
using FlagIndex = Network::Field<std::uint8_t, 3>;
using FlagValue = Network::Field<std::uint8_t, 4>;
constexpr Network::PacketLayout QuestFlagLayout{
    {Network::PacketType::C1, QuestFlagOpcode, std::nullopt}, FlagValue::End, 0x4ddaa0};

using ListCount = Network::Field<std::uint8_t, 3>;
constexpr Network::PacketLayout QuestListLayout{
    {Network::PacketType::C1, QuestListOpcode, std::nullopt}, ListCount::End, 0x4ddad0};

using StateIndex = Network::Field<std::uint8_t, 3>;
using State = Network::Field<std::uint8_t, 4>;
constexpr Network::PacketLayout QuestStateLayout{
    {Network::PacketType::C1, QuestStateOpcode, std::nullopt}, State::End, 0x4ddb00};

using SetIndex = Network::Field<std::uint8_t, 3>;
using SetResult = Network::Field<std::uint8_t, 4>;
using SetState = Network::Field<std::uint8_t, 5>;
constexpr Network::PacketLayout SetQuestResultLayout{
    {Network::PacketType::C1, SetQuestOpcode, std::nullopt}, SetState::End, 0x4ddb40};

using PrizeKey = Network::Field<std::uint16_t, 3, std::endian::big>;
using PrizeKind = Network::Field<std::uint8_t, 5>;
using PrizeValue = Network::Field<std::uint8_t, 6>;
constexpr Network::PacketLayout QuestResultLayout{
    {Network::PacketType::C1, QuestResultOpcode, std::nullopt}, PrizeValue::End, 0x4ddb80};
constexpr std::uint16_t KeyMask = 0x7fff;
} // namespace

Core::Expected<QuestFlag, Network::ProtocolError> ReadQuestFlag(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, QuestFlagLayout, [](Network::LayoutReader& reader)
                               { return QuestFlag{reader.Read<FlagIndex>(), reader.Read<FlagValue>()}; });
}

Core::Expected<QuestList, Network::ProtocolError> ReadQuestList(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, QuestListLayout,
                               [](Network::LayoutReader& reader)
                               {
                                   QuestList list{.count = reader.Read<ListCount>(), .states = {}};
                                   std::ranges::transform(reader.Bytes(ListCount::End, reader.Size() - ListCount::End),
                                                          std::back_inserter(list.states), [](std::byte state)
                                                          { return std::to_integer<std::uint8_t>(state); });
                                   return list;
                               });
}

Core::Expected<QuestStateChange, Network::ProtocolError> ReadQuestState(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, QuestStateLayout, [](Network::LayoutReader& reader)
                               { return QuestStateChange{reader.Read<StateIndex>(), reader.Read<State>()}; });
}

Core::Expected<SetQuestResult, Network::ProtocolError> ReadSetQuestResult(const Network::Packet& packet)
{
    return Network::ReadPacket(
        packet, SetQuestResultLayout, [](Network::LayoutReader& reader)
        { return SetQuestResult{reader.Read<SetIndex>(), reader.Read<SetResult>(), reader.Read<SetState>()}; });
}

Core::Expected<QuestResult, Network::ProtocolError> ReadQuestResult(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, QuestResultLayout,
                               [](Network::LayoutReader& reader)
                               {
                                   return QuestResult{static_cast<std::uint16_t>(reader.Read<PrizeKey>() & KeyMask),
                                                      reader.Read<PrizeKind>(), reader.Read<PrizeValue>()};
                               });
}

Outgoing WriteQuestListRequest()
{
    return EncryptedC1(Network::PacketWriter::C1(QuestListOpcode));
}

Outgoing WriteSetQuest(std::uint8_t quest, std::uint8_t state)
{
    return EncryptedC1(Network::PacketWriter::C1(SetQuestOpcode).PutU8(quest).PutU8(state));
}

Outgoing WriteTalk(std::int16_t key)
{
    // 0x567cca..0x567ccd: MOV AX, word; SAR EAX, 8 (the high byte with its sign); then the low one.
    const auto high = static_cast<std::uint8_t>((static_cast<std::int32_t>(key) >> 8) & 0xff);
    const auto low = static_cast<std::uint8_t>(static_cast<std::uint16_t>(key) & 0xffu);
    return EncryptedC1(Network::PacketWriter::C1(TalkOpcode).PutU8(high).PutU8(low));
}
} // namespace Client::Protocol::Quests
