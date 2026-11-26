#include "Protocol/Party/Party.h"

#include "Core/ByteReader.h"
#include "Core/Expected.h"
#include "Network/Field.h"
#include "Network/LayoutReader.h"
#include "Network/Packet.h"
#include "Network/PacketLayout.h"
#include "Network/PacketWriter.h"
#include "Network/ProtocolError.h"
#include "Network/ReadList.h"
#include "Protocol/FrontEnd/CharacterName.h"
#include "Protocol/Opcodes.h"
#include "Protocol/Outgoing.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <span>

namespace Client::Protocol::Party
{
namespace
{
using RequesterKey = Network::Field<std::uint16_t, 3, std::endian::big>;
constexpr Network::PacketLayout PartyRequestLayout{
    {Network::PacketType::C1, PartyRequestOpcode, std::nullopt}, RequesterKey::End, 0x4e3f41};

using RequestResult = Network::Field<std::uint8_t, 3>;
constexpr Network::PacketLayout PartyRequestResultLayout{
    {Network::PacketType::C1, PartyRequestResultOpcode, std::nullopt}, RequestResult::End, 0x4e3f6f};

using ListResult = Network::Field<std::uint8_t, 3>;
using MemberCount = Network::Field<std::uint8_t, 4>;
constexpr Network::PacketLayout PartyListLayout{
    {Network::PacketType::C1, PartyListOpcode, std::nullopt}, MemberCount::End, 0x4d67e0};
constexpr std::size_t NumberOffset = 0xa;
constexpr std::size_t MapOffset = 0xb;
constexpr std::size_t XOffset = 0xc;
constexpr std::size_t YOffset = 0xd;
// 0x4d67e0 does not copy +0xe and +0xf.
constexpr std::size_t LifeOffset = 0x10;
constexpr std::size_t LivesSize = 8;

// C1 03 43.
constexpr std::size_t PartyLeftSize = 3;
constexpr Network::PacketLayout PartyLeftLayout{
    {Network::PacketType::C1, PartyLeaveOpcode, std::nullopt}, PartyLeftSize, 0x4e409b};

using LifeCount = Network::Field<std::uint8_t, 3>;
constexpr Network::PacketLayout PartyLifeLayout{
    {Network::PacketType::C1, PartyLifeOpcode, std::nullopt}, LifeCount::End, 0x4e4174};

PartyMember ReadMember(std::span<const std::byte, MemberWireSize> bytes)
{
    PartyMember member{.number = std::to_integer<std::uint8_t>(bytes[NumberOffset]),
                       .map = std::to_integer<std::uint8_t>(bytes[MapOffset]),
                       .x = std::to_integer<std::uint8_t>(bytes[XOffset]),
                       .y = std::to_integer<std::uint8_t>(bytes[YOffset])};
    std::ranges::copy(bytes.first<FrontEnd::NameSize>(), member.name.begin());
    Core::ByteReader lives(bytes.subspan<LifeOffset, LivesSize>());
    member.life = *lives.ReadU32Le();
    member.maxLife = *lives.ReadU32Le();
    return member;
}
} // namespace

Outgoing WritePartyListRequest()
{
    return ClearC1(Network::PacketWriter::C1(PartyListOpcode));
}

Core::Expected<PartyRequest, Network::ProtocolError> ReadPartyRequest(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, PartyRequestLayout,
                               [](Network::LayoutReader& reader) { return PartyRequest{reader.Read<RequesterKey>()}; });
}

Core::Expected<PartyRequestResult, Network::ProtocolError> ReadPartyRequestResult(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, PartyRequestResultLayout, [](Network::LayoutReader& reader)
                               { return PartyRequestResult{reader.Read<RequestResult>()}; });
}

Core::Expected<PartyList, Network::ProtocolError> ReadPartyList(const Network::Packet& packet)
{
    return Network::ReadPacket(
        packet, PartyListLayout,
        [](Network::LayoutReader& reader)
        {
            return PartyList{.result = reader.Read<ListResult>(),
                             .members = Network::ReadList<MemberWireSize>(reader, MemberCount::End,
                                                                          reader.Read<MemberCount>(), ReadMember)};
        });
}

Core::Expected<PartyLeft, Network::ProtocolError> ReadPartyLeft(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, PartyLeftLayout, [](Network::LayoutReader&) { return PartyLeft{}; });
}

Core::Expected<PartyLife, Network::ProtocolError> ReadPartyLife(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, PartyLifeLayout,
                               [](Network::LayoutReader& reader)
                               {
                                   PartyLife life{};
                                   const std::span<const std::byte> bytes =
                                       reader.Bytes(LifeCount::End, reader.Read<LifeCount>());
                                   life.entries.reserve(bytes.size());
                                   std::ranges::transform(bytes, std::back_inserter(life.entries),
                                                          [](std::byte b) { return std::to_integer<std::uint8_t>(b); });
                                   return life;
                               });
}

Outgoing WritePartyRequest(std::uint16_t key)
{
    return EncryptedC1(Network::PacketWriter::C1(PartyRequestOpcode).PutU16Be(key));
}

Outgoing WritePartyRequestResult(std::uint8_t accepted, std::uint16_t key)
{
    return EncryptedC1(Network::PacketWriter::C1(PartyRequestResultOpcode).PutU8(accepted).PutU16Be(key));
}

Outgoing WritePartyLeave(std::uint8_t number)
{
    return Xor32C1(Network::PacketWriter::C1(PartyLeaveOpcode).PutU8(number));
}
} // namespace Client::Protocol::Party
