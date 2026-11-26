#include "Protocol/Social/Duel.h"

#include "Core/Expected.h"
#include "Network/Field.h"
#include "Network/LayoutReader.h"
#include "Network/Packet.h"
#include "Network/PacketLayout.h"
#include "Network/PacketWriter.h"
#include "Network/ProtocolError.h"
#include "Protocol/FrontEnd/CharacterName.h"
#include "Protocol/Opcodes.h"
#include "Protocol/Outgoing.h"

#include <bit>
#include <cstdint>
#include <optional>

namespace Client::Protocol::Social
{
namespace
{
using StartResult = Network::Field<std::uint8_t, 3>;
using StartKey = Network::Field<std::uint16_t, 4, std::endian::big>;
using StartName = Network::Field<FrontEnd::NameBytes, 6>;
constexpr Network::PacketLayout DuelStartLayout{
    {Network::PacketType::C1, DuelStartOpcode, std::nullopt}, StartName::End, 0x4dac90};

// The AB and the AC: the key and the name.
using PeerKey = Network::Field<std::uint16_t, 3, std::endian::big>;
using PeerName = Network::Field<FrontEnd::NameBytes, 5>;
constexpr Network::PacketLayout DuelEndLayout{
    {Network::PacketType::C1, DuelEndOpcode, std::nullopt}, PeerName::End, 0x4e4d10};
constexpr Network::PacketLayout DuelRequestLayout{
    {Network::PacketType::C1, DuelRequestOpcode, std::nullopt}, PeerName::End, 0x4e4d75};

using FirstKey = Network::Field<std::uint16_t, 3, std::endian::big>;
using SecondKey = Network::Field<std::uint16_t, 5, std::endian::big>;
using FirstScore = Network::Field<std::uint8_t, 7>;
using SecondScore = Network::Field<std::uint8_t, 8>;
constexpr Network::PacketLayout DuelScoreLayout{
    {Network::PacketType::C1, DuelScoreOpcode, std::nullopt}, SecondScore::End, 0x4e4dd1};

template <typename Peer>
Core::Expected<Peer, Network::ProtocolError> ReadPeer(const Network::Packet& packet,
                                                      const Network::PacketLayout& layout)
{
    return Network::ReadPacket(packet, layout, [](Network::LayoutReader& reader)
                               { return Peer{reader.Read<PeerKey>(), reader.Read<PeerName>()}; });
}
} // namespace

Core::Expected<DuelStart, Network::ProtocolError> ReadDuelStart(const Network::Packet& packet)
{
    return Network::ReadPacket(
        packet, DuelStartLayout, [](Network::LayoutReader& reader)
        { return DuelStart{reader.Read<StartResult>(), reader.Read<StartKey>(), reader.Read<StartName>()}; });
}

Core::Expected<DuelEnd, Network::ProtocolError> ReadDuelEnd(const Network::Packet& packet)
{
    return ReadPeer<DuelEnd>(packet, DuelEndLayout);
}

Core::Expected<DuelRequest, Network::ProtocolError> ReadDuelRequest(const Network::Packet& packet)
{
    return ReadPeer<DuelRequest>(packet, DuelRequestLayout);
}

Core::Expected<DuelScore, Network::ProtocolError> ReadDuelScore(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, DuelScoreLayout,
                               [](Network::LayoutReader& reader)
                               {
                                   return DuelScore{reader.Read<FirstKey>(), reader.Read<SecondKey>(),
                                                    reader.Read<FirstScore>(), reader.Read<SecondScore>()};
                               });
}

Outgoing WriteDuelStart(std::uint16_t key, const FrontEnd::NameBytes& name)
{
    return EncryptedC1(Network::PacketWriter::C1(DuelStartOpcode).PutU16Be(key).PutBytes(name));
}

Outgoing WriteDuelEnd()
{
    return EncryptedC1(Network::PacketWriter::C1(DuelEndOpcode));
}

Outgoing WriteDuelAnswer(std::uint8_t accepted, std::uint16_t key, const FrontEnd::NameBytes& name)
{
    return EncryptedC1(Network::PacketWriter::C1(DuelRequestOpcode).PutU8(accepted).PutU16Be(key).PutBytes(name));
}
} // namespace Client::Protocol::Social
