#include "Protocol/Events.h"

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/PacketWriter.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace Client::Protocol::Events
{
namespace
{
// Los offsets son del paquete entero, con la cabecera C1 (pkt[0] es el 0xc1), como los lee el ASM.
class Fields
{
public:
    explicit Fields(std::span<const std::byte> bytes) : m_bytes(bytes) {}

    [[nodiscard]] bool Has(std::size_t size) const
    {
        return m_bytes.size() >= size;
    }

    [[nodiscard]] std::uint8_t U8(std::size_t at) const
    {
        return std::to_integer<std::uint8_t>(m_bytes[at]);
    }

    // Una carga x86 de word: little-endian.
    [[nodiscard]] std::uint16_t U16(std::size_t at) const
    {
        return static_cast<std::uint16_t>(U8(at) | (U8(at + 1) << 8u));
    }

    [[nodiscard]] std::uint32_t U32(std::size_t at) const
    {
        return static_cast<std::uint32_t>(U16(at)) | (static_cast<std::uint32_t>(U16(at + 2)) << 16u);
    }

    [[nodiscard]] std::span<const std::byte> From(std::size_t at) const
    {
        return at < m_bytes.size() ? m_bytes.subspan(at) : std::span<const std::byte>{};
    }

private:
    std::span<const std::byte> m_bytes;
};

using Read = Core::Expected<EventPacket, ProtocolError>;

Read TooShort()
{
    return Core::Unexpected(ProtocolError::TooShort);
}

// 0x4dab90: el tipo 0 lee 4 bytes por rectángulo y el 1, 2 por celda, desde pkt[7]; otro tipo, nada.
Read ReadMapAttributes(const Fields& fields)
{
    constexpr std::size_t CountAt = 6;
    constexpr std::size_t FirstAt = 7;
    if (!fields.Has(FirstAt))
        return TooShort();
    MapAttributes attributes{fields.U8(3), fields.U8(4), fields.U8(5), fields.U8(CountAt), {}, {}};
    if (attributes.type == 0)
    {
        if (!fields.Has(FirstAt + std::size_t{4} * attributes.count))
            return TooShort();
        for (std::size_t index = 0; index < attributes.count; ++index)
        {
            const std::size_t at = FirstAt + index * 4;
            attributes.areas.push_back({fields.U8(at), fields.U8(at + 1), fields.U8(at + 2), fields.U8(at + 3)});
        }
    }
    else if (attributes.type == 1)
    {
        if (!fields.Has(FirstAt + std::size_t{2} * attributes.count))
            return TooShort();
        for (std::size_t index = 0; index < attributes.count; ++index)
        {
            const std::size_t at = FirstAt + index * 2;
            attributes.cells.push_back({fields.U8(at), fields.U8(at + 1)});
        }
    }
    return EventPacket{std::move(attributes)};
}

// 0x4da8c0: el 1 y el 2 leen pkt[4]; el 4, pkt[4], pkt[5] y pkt[6]; los demás, nada.
Read ReadEventOpenTime(const Fields& fields)
{
    if (!fields.Has(4))
        return TooShort();
    EventOpenTime time{fields.U8(3)};
    if (time.kind == 1 || time.kind == 2)
    {
        if (!fields.Has(5))
            return TooShort();
        time.value4 = fields.U8(4);
    }
    else if (time.kind == 4)
    {
        if (!fields.Has(7))
            return TooShort();
        time.value4 = fields.U8(4);
        time.value5 = fields.U8(5);
        time.value6 = fields.U8(6);
    }
    return EventPacket{std::move(time)};
}

Read ReadEventChipInfo(const Fields& fields)
{
    if (!fields.Has(6))
        return TooShort();
    EventChipInfo info{fields.U8(3), fields.U16(4), std::nullopt};
    // 0x4dd8f0..0x4dd940: los tres words se leen solo con el tipo 0 (0x78abb00 en 1).
    if (fields.Has(0xc))
        info.muto = std::array<std::uint16_t, 3>{fields.U16(6), fields.U16(8), fields.U16(0xa)};
    return EventPacket{std::move(info)};
}

Read ReadSerialResult(const Fields& fields)
{
    constexpr std::size_t DataAt = 4;
    SerialResult result{};
    if (!fields.Has(DataAt + result.data.size()))
        return TooShort();
    result.result = fields.U8(3);
    const auto data = fields.From(DataAt).first(result.data.size());
    std::ranges::copy(data, result.data.begin());
    return EventPacket{std::move(result)};
}

Read ReadSoccerScore(const Fields& fields)
{
    // 0x4d9f00 lee hasta el word de pkt[0x15] (que toma un byte más allá de los 0x16 del GS, 08).
    if (!fields.Has(0x16))
        return TooShort();
    SoccerScore score{};
    for (std::size_t index = 0; index < score.firstName.size(); ++index)
    {
        score.firstName[index] = static_cast<char>(fields.U8(4 + index));
        score.secondName[index] = static_cast<char>(fields.U8(0xd + index));
    }
    score.firstScore = fields.U8(0xc);
    score.secondScore = fields.U8(0x15);
    return EventPacket{std::move(score)};
}

Read ReadWorld(const Fields& fields, std::uint8_t subcode)
{
    switch (subcode)
    {
    case EventTimeSubcode:
        if (!fields.Has(8))
            return TooShort();
        return EventPacket{EventTime{fields.U8(4), fields.U16(6)}};
    case SoccerScoreSubcode:
        return ReadSoccerScore(fields);
    case ServerCommandSubcode:
        if (!fields.Has(7))
            return TooShort();
        return EventPacket{ServerCommand{fields.U8(4), fields.U8(5), fields.U8(6)}};
    default:
        return Core::Unexpected(ProtocolError::WrongPacket);
    }
}

Read ReadByOpcode(const Fields& fields, std::uint8_t opcode)
{
    switch (opcode)
    {
    case EventStateOpcode:
        if (!fields.Has(5))
            return TooShort();
        return EventPacket{EventState{fields.U8(3), fields.U8(4)}};
    case MapAttributesOpcode:
        return ReadMapAttributes(fields);
    case ItemAcquiredOpcode:
        if (!fields.Has(0xd))
            return TooShort();
        // 0x4d69a1..0x4d69aa: pkt[3] << 8 + pkt[4].
        return EventPacket{ItemAcquired{static_cast<std::uint16_t>((fields.U8(3) << 8u) | fields.U8(4)), fields.U32(8),
                                        fields.U8(0xc)}};
    case DevilSquareEntryOpcode:
        if (!fields.Has(4))
            return TooShort();
        return EventPacket{DevilSquareEntryResult{fields.U8(3)}};
    case EventOpenTimeOpcode:
        return ReadEventOpenTime(fields);
    case EventCountdownOpcode:
        if (!fields.Has(4))
            return TooShort();
        return EventPacket{EventCountdown{fields.U8(3)}};
    case EventResultOpcode:
    {
        if (!fields.Has(5))
            return TooShort();
        const auto rest = fields.From(5);
        return EventPacket{EventResult{fields.U8(3), fields.U8(4), std::vector<std::byte>(rest.begin(), rest.end())}};
    }
    case EventChipInfoOpcode:
        return ReadEventChipInfo(fields);
    case EventChipCountOpcode:
        if (!fields.Has(6))
            return TooShort();
        return EventPacket{EventChipCount{fields.U16(4)}};
    case MutoNumbersOpcode:
        if (!fields.Has(0xa))
            return TooShort();
        return EventPacket{MutoNumbers{{fields.U16(4), fields.U16(6), fields.U16(8)}}};
    case EventDialogOpcode:
        if (!fields.Has(4))
            return TooShort();
        return EventPacket{EventDialog{fields.U8(3)}};
    case BloodCastleEntryOpcode:
        if (!fields.Has(4))
            return TooShort();
        return EventPacket{BloodCastleEntryResult{fields.U8(3)}};
    case EventPlayStateOpcode:
        if (!fields.Has(0xd))
            return TooShort();
        return EventPacket{
            EventPlayState{fields.U8(3), fields.U16(4), fields.U16(6), fields.U16(8), fields.U16(0xa), fields.U8(0xc)}};
    case SerialResultOpcode:
        return ReadSerialResult(fields);
    case EventSoundOpcode:
        if (!fields.Has(6))
            return TooShort();
        return EventPacket{EventSound{fields.U16(4)}};
    default:
        return Core::Unexpected(ProtocolError::WrongPacket);
    }
}

// 0x4e1680..0x4e16b9 (BD 08): pkt[4] registros de 0x14 bytes desde pkt+5, sin mirar el tamaño del paquete.
constexpr std::size_t RankingFirstAt = 5;
constexpr std::size_t RankingEntryBytes = 0x14;

Read ReadCrywolfRanking(const Fields& fields)
{
    if (!fields.Has(RankingFirstAt))
        return TooShort();
    const std::uint8_t count = fields.U8(4);
    if (!fields.Has(RankingFirstAt + RankingEntryBytes * count))
        return TooShort();
    CrywolfRanking ranking;
    for (std::size_t index = 0; index < count; ++index)
    {
        const std::size_t at = RankingFirstAt + RankingEntryBytes * index;
        CrywolfRankingEntry entry{.place = fields.U8(at),
                                  .score = static_cast<std::int32_t>(fields.U32(at + 0xc)),
                                  .classByte = fields.U8(at + 0x10)};
        for (std::size_t letter = 0; letter < entry.name.size(); ++letter)
            entry.name[letter] = static_cast<char>(fields.U8(at + 1 + letter));
        ranking.entries.push_back(entry);
    }
    return EventPacket{std::move(ranking)};
}

// 0x4e5941: el BD por pkt[3] con la tabla de 0x4e620c; el 1, el 6, el 0xa, el 0xb y los de más de 0xc no hacen nada.
Read ReadCrywolf(const Fields& fields, std::uint8_t subcode)
{
    switch (subcode)
    {
    case CrywolfStateSubcode:
        if (!fields.Has(6))
            return TooShort();
        return EventPacket{CrywolfState{fields.U8(4), fields.U8(5)}};
    case CrywolfAltarsSubcode:
        if (!fields.Has(0xd))
            return TooShort();
        return EventPacket{CrywolfAltars{static_cast<std::int32_t>(fields.U32(4)),
                                         {fields.U8(8), fields.U8(9), fields.U8(0xa), fields.U8(0xb), fields.U8(0xc)}}};
    case CrywolfTimerSubcode:
        if (!fields.Has(6))
            return TooShort();
        return EventPacket{CrywolfTimer{fields.U8(4), fields.U8(5)}};
    case CrywolfBossSubcode:
        if (!fields.Has(9))
            return TooShort();
        return EventPacket{CrywolfBoss{static_cast<std::int32_t>(fields.U32(4)), fields.U8(8)}};
    case CrywolfRankSubcode:
        if (!fields.Has(0xc))
            return TooShort();
        return EventPacket{CrywolfRank{fields.U8(4), static_cast<std::int32_t>(fields.U32(8))}};
    case CrywolfRankingSubcode:
        return ReadCrywolfRanking(fields);
    case CrywolfChaosBoxSubcode:
        if (!fields.Has(5))
            return TooShort();
        return EventPacket{CrywolfChaosBox{fields.U8(4)}};
    case CrywolfContractSubcode:
    case CrywolfEffectSubcode:
    {
        const auto all = fields.From(0);
        return EventPacket{CrywolfUnported{subcode, std::vector<std::byte>(all.begin(), all.end())}};
    }
    default:
        return Core::Unexpected(ProtocolError::WrongPacket);
    }
}
} // namespace

Core::Expected<EventPacket, ProtocolError> ReadEventPacket(const Network::Packet& packet)
{
    if (packet.Type() != Network::PacketType::C1)
        return Core::Unexpected(ProtocolError::WrongPacket);
    const std::optional<std::uint8_t> read = packet.Opcode();
    if (!read)
        return Core::Unexpected(ProtocolError::TooShort);
    const Fields fields(packet.Bytes());
    const std::uint8_t opcode = *read;
    const bool withSubcode =
        opcode == ChaosCastleOpcode || opcode == JewelMixOpcode || opcode == CrywolfOpcode || opcode == WorldOpcode;
    if (!withSubcode)
        return ReadByOpcode(fields, opcode);
    if (!fields.Has(4))
        return TooShort();
    const std::uint8_t subcode = fields.U8(3);
    if (opcode == WorldOpcode)
        return ReadWorld(fields, subcode);
    if (opcode == CrywolfOpcode)
        return ReadCrywolf(fields, subcode);
    const bool known = (opcode == ChaosCastleOpcode && subcode == ChaosCastleEntrySubcode) ||
                       (opcode == JewelMixOpcode && (subcode == JewelMixSubcode || subcode == JewelUnmixSubcode));
    if (!known)
        return Core::Unexpected(ProtocolError::WrongPacket);
    if (!fields.Has(5))
        return TooShort();
    if (opcode == ChaosCastleOpcode)
        return EventPacket{ChaosCastleEntryResult{fields.U8(4)}};
    return EventPacket{JewelMixResult{subcode, fields.U8(4)}};
}

Outgoing WriteEventWindowLeave()
{
    return ClearC1(Network::PacketWriter::C1(EventWindowLeaveOpcode));
}

Outgoing WriteMutoNumberRequest()
{
    return Xor32C1(Network::PacketWriter::C1(MutoNumberRequestOpcode).PutU8(MutoNumberRequestByte));
}

Outgoing WriteEventChipRegister(std::uint8_t type, std::uint8_t itemPlace)
{
    return Xor32C1(Network::PacketWriter::C1(EventChipCountOpcode).PutU8(type).PutU8(itemPlace));
}

Outgoing WriteSerialNumber(const SerialPieces& pieces)
{
    Network::PacketWriter writer = Network::PacketWriter::C1(SerialResultOpcode);
    for (const auto& piece : pieces)
    {
        writer.PutBytes(std::as_bytes(std::span(piece)));
        writer.PutU8(0);
    }
    return Xor32C1(writer);
}

Outgoing WriteChaosCastleReposition(std::uint8_t x, std::uint8_t y)
{
    return Xor32C1(Network::PacketWriter::C1(ChaosCastleOpcode).PutU8(ChaosCastleRepositionSubcode).PutU8(x).PutU8(y));
}

Outgoing WriteDevilSquareEntry(std::uint8_t square, std::uint8_t itemPlace)
{
    return Xor32C1(Network::PacketWriter::C1(DevilSquareEntryOpcode).PutU8(square).PutU8(itemPlace));
}

Outgoing WriteBloodCastleEntry(std::uint8_t bridge, std::uint8_t itemPlace)
{
    return Xor32C1(Network::PacketWriter::C1(BloodCastleEntryOpcode).PutU8(bridge).PutU8(itemPlace));
}
} // namespace Client::Protocol::Events
