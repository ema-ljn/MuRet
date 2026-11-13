#include "Protocol/CastleSiege.h"

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/PacketWriter.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Client::Protocol::CastleSiege
{
namespace
{
// 0x4dfc60, 0x4dfce0 y 0x4dfd80: la cuenta en pkt+8 y los registros desde pkt+0xc.
constexpr std::size_t CountAt = 8;
constexpr std::size_t RecordsAt = 0xc;
constexpr std::size_t NpcRecordSize = 0x1c;
constexpr std::size_t GuildListEntrySize = 0xe;
constexpr std::size_t RegisteredGuildEntrySize = 10;

std::uint8_t Byte(std::span<const std::byte> bytes, std::size_t at)
{
    return std::to_integer<std::uint8_t>(bytes[at]);
}

// La cuenta es un int: con el bit de arriba prendido el lazo del original (JLE/JGE con signo) no corre.
Core::Expected<std::size_t, ProtocolError> CountOf(const CastlePacket& packet)
{
    auto count = packet.U32Le(CountAt);
    if (!count.HasValue())
        return Core::Unexpected(count.Error());
    const auto signedCount = static_cast<std::int32_t>(count.Value());
    return signedCount > 0 ? static_cast<std::size_t>(signedCount) : std::size_t{0};
}
} // namespace

Core::Expected<std::uint8_t, ProtocolError> CastlePacket::Subcode() const
{
    const bool wide = type == Network::PacketType::C2 || type == Network::PacketType::C4;
    return U8(wide ? 4 : 3);
}

Core::Expected<std::uint8_t, ProtocolError> CastlePacket::U8(std::size_t at) const
{
    if (at >= bytes.size())
        return Core::Unexpected(ProtocolError::TooShort);
    return Byte(bytes, at);
}

Core::Expected<std::int8_t, ProtocolError> CastlePacket::S8(std::size_t at) const
{
    auto value = U8(at);
    if (!value.HasValue())
        return Core::Unexpected(value.Error());
    return static_cast<std::int8_t>(value.Value());
}

Core::Expected<std::uint32_t, ProtocolError> CastlePacket::U32Le(std::size_t at) const
{
    auto span = Bytes(at, 4);
    if (!span.HasValue())
        return Core::Unexpected(span.Error());
    const auto b = span.Value();
    return static_cast<std::uint32_t>(Byte(b, 0)) | (static_cast<std::uint32_t>(Byte(b, 1)) << 8u) |
           (static_cast<std::uint32_t>(Byte(b, 2)) << 16u) | (static_cast<std::uint32_t>(Byte(b, 3)) << 24u);
}

Core::Expected<std::uint32_t, ProtocolError> CastlePacket::U32Be(std::size_t at) const
{
    auto span = Bytes(at, 4);
    if (!span.HasValue())
        return Core::Unexpected(span.Error());
    const auto b = span.Value();
    return (static_cast<std::uint32_t>(Byte(b, 0)) << 24u) | (static_cast<std::uint32_t>(Byte(b, 1)) << 16u) |
           (static_cast<std::uint32_t>(Byte(b, 2)) << 8u) | static_cast<std::uint32_t>(Byte(b, 3));
}

Core::Expected<std::uint64_t, ProtocolError> CastlePacket::U64Be(std::size_t at) const
{
    auto span = Bytes(at, 8);
    if (!span.HasValue())
        return Core::Unexpected(span.Error());
    std::uint64_t value = 0;
    for (const std::byte b : span.Value())
        value = (value << 8u) | std::to_integer<std::uint64_t>(b);
    return value;
}

Core::Expected<std::span<const std::byte>, ProtocolError> CastlePacket::Bytes(std::size_t at, std::size_t count) const
{
    if (at > bytes.size() || bytes.size() - at < count)
        return Core::Unexpected(ProtocolError::TooShort);
    return std::span<const std::byte>(bytes).subspan(at, count);
}

Core::Expected<CastlePacket, ProtocolError> ReadCastlePacket(const Network::Packet& packet, std::uint8_t opcode)
{
    if (packet.Opcode() != opcode)
        return Core::Unexpected(ProtocolError::WrongPacket);
    const auto bytes = packet.Bytes();
    return CastlePacket{
        .opcode = opcode, .type = packet.Type(), .bytes = std::vector<std::byte>(bytes.begin(), bytes.end())};
}

Core::Expected<std::vector<NpcRecord>, ProtocolError> ReadNpcRecords(const CastlePacket& packet)
{
    auto count = CountOf(packet);
    if (!count.HasValue())
        return Core::Unexpected(count.Error());
    std::vector<NpcRecord> records;
    for (std::size_t i = 0; i < count.Value(); ++i)
    {
        auto span = packet.Bytes(RecordsAt + i * NpcRecordSize, NpcRecordSize);
        if (!span.HasValue())
            return Core::Unexpected(span.Error());
        NpcRecord record;
        std::ranges::copy(span.Value(), record.bytes.begin());
        records.push_back(record);
    }
    return records;
}

Core::Expected<std::vector<GuildListEntry>, ProtocolError> ReadGuildList(const CastlePacket& packet)
{
    auto count = CountOf(packet);
    if (!count.HasValue())
        return Core::Unexpected(count.Error());
    std::vector<GuildListEntry> entries;
    for (std::size_t i = 0; i < count.Value(); ++i)
    {
        const std::size_t at = RecordsAt + i * GuildListEntrySize;
        auto span = packet.Bytes(at, GuildListEntrySize);
        if (!span.HasValue())
            return Core::Unexpected(span.Error());
        const auto b = span.Value();
        GuildListEntry entry;
        for (std::size_t c = 0; c < entry.name.size(); ++c)
            entry.name[c] = static_cast<char>(Byte(b, c));
        entry.marks = static_cast<std::int32_t>(packet.U32Be(at + 8).Value());
        entry.first = Byte(b, 0xc);
        entry.second = Byte(b, 0xd);
        entries.push_back(entry);
    }
    return entries;
}

Core::Expected<std::vector<RegisteredGuildEntry>, ProtocolError> ReadRegisteredGuilds(const CastlePacket& packet)
{
    auto count = CountOf(packet);
    if (!count.HasValue())
        return Core::Unexpected(count.Error());
    std::vector<RegisteredGuildEntry> entries;
    for (std::size_t i = 0; i < count.Value(); ++i)
    {
        auto span = packet.Bytes(RecordsAt + i * RegisteredGuildEntrySize, RegisteredGuildEntrySize);
        if (!span.HasValue())
            return Core::Unexpected(span.Error());
        const auto b = span.Value();
        RegisteredGuildEntry entry;
        entry.first = Byte(b, 0);
        entry.second = Byte(b, 1);
        for (std::size_t c = 0; c < entry.name.size(); ++c)
            entry.name[c] = static_cast<char>(Byte(b, 2 + c));
        entries.push_back(entry);
    }
    return entries;
}

Outgoing WriteRegister()
{
    return Xor32C1(Network::PacketWriter::C1(CastleSiegeOpcode).PutU8(RegisterSubcode));
}

Outgoing WriteGiveUp()
{
    return Xor32C1(Network::PacketWriter::C1(CastleSiegeOpcode).PutU8(GiveUpSubcode).PutU8(1));
}

Outgoing WriteRegistrationInfoRequest()
{
    return Xor32C1(Network::PacketWriter::C1(CastleSiegeOpcode).PutU8(RegistrationInfoSubcode));
}

Outgoing WriteRegisterMark(std::uint8_t slot)
{
    return Xor32C1(Network::PacketWriter::C1(CastleSiegeOpcode).PutU8(RegisterMarkSubcode).PutU8(slot));
}

Outgoing WriteNpcBuy(std::uint32_t type, std::uint32_t index)
{
    return Xor32C1(Network::PacketWriter::C1(CastleSiegeOpcode).PutU8(NpcBuySubcode).PutU32Le(type).PutU32Le(index));
}

Outgoing WriteNpcRepair(std::uint32_t type, std::uint32_t index)
{
    return Xor32C1(
        Network::PacketWriter::C1(CastleSiegeOpcode).PutU8(NpcRepairSubcode).PutU32Le(type).PutU32Le(index));
}

Outgoing WriteNpcUpgrade(std::uint32_t type, std::uint32_t index, std::uint32_t kind, std::uint32_t value)
{
    return Xor32C1(Network::PacketWriter::C1(CastleSiegeOpcode)
                       .PutU8(NpcUpgradeSubcode)
                       .PutU32Le(type)
                       .PutU32Le(index)
                       .PutU32Le(kind)
                       .PutU32Le(value));
}

Outgoing WriteCastleInfoRequest()
{
    return Xor32C1(Network::PacketWriter::C1(CastleSiegeOpcode).PutU8(CastleInfoSubcode));
}

Outgoing WriteTaxRate(std::uint8_t kind, std::uint32_t value)
{
    return Xor32C1(Network::PacketWriter::C1(CastleSiegeOpcode)
                       .PutU8(TaxRateSubcode)
                       .PutU8(kind)
                       .PutU8(static_cast<std::uint8_t>(value >> 24u))
                       .PutU8(static_cast<std::uint8_t>(value >> 16u))
                       .PutU8(static_cast<std::uint8_t>(value >> 8u))
                       .PutU8(static_cast<std::uint8_t>(value)));
}

Outgoing WriteMoneyOut(std::uint32_t amount)
{
    return Xor32C1(Network::PacketWriter::C1(CastleSiegeOpcode).PutU8(MoneyOutSubcode).PutU32Le(amount));
}

Outgoing WriteHuntZoneGate(std::uint8_t allowed)
{
    return Xor32C1(Network::PacketWriter::C1(CastleSiegeOpcode).PutU8(HuntZoneGateSubcode).PutU8(allowed));
}

Outgoing WriteNpcListRequest(std::uint8_t kind)
{
    return Xor32C1(Network::PacketWriter::C1(NpcListOpcode).PutU8(kind));
}

Outgoing WriteGuildListRequest()
{
    return ClearC1(Network::PacketWriter::C1(GuildListOpcode));
}

Outgoing WriteRegisteredGuildsRequest()
{
    return ClearC1(Network::PacketWriter::C1(RegisteredGuildsOpcode));
}

Outgoing WriteHuntZoneEntry(std::uint32_t fee)
{
    return Xor32C1(Network::PacketWriter::C1(HuntZoneOpcode).PutU8(HuntZoneEntrySubcode).PutU32Le(fee));
}

Outgoing WriteCloseTalk()
{
    return ClearC1(Network::PacketWriter::C1(CloseTalkOpcode));
}

Outgoing WriteCloseMix()
{
    return ClearC1(Network::PacketWriter::C1(CloseMixOpcode));
}
} // namespace Client::Protocol::CastleSiege
