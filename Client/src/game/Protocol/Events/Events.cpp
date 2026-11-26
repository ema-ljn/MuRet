#include "Protocol/Events/Events.h"

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
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Client::Protocol::Events
{
namespace
{
using Result = Core::Expected<EventPacket, Network::ProtocolError>;

// The offsets are the whole packet's, with the C1 header (pkt[0] is the 0xc1), as the ASM reads them.
template <std::size_t At> using Byte = Network::Field<std::uint8_t, At>;
// An x86 word or dword load: little-endian.
template <std::size_t At> using Word = Network::Field<std::uint16_t, At>;
template <std::size_t At> using Dword = Network::Field<std::uint32_t, At>;
template <std::size_t At> using BigWord = Network::Field<std::uint16_t, At, std::endian::big>;

constexpr std::size_t SubcodeEnd = 4;

constexpr Network::PacketLayout Layout(Network::Opcode opcode, std::size_t minimumSize, std::uint32_t address)
{
    return {{Network::PacketType::C1, opcode, std::nullopt}, minimumSize, address};
}

constexpr Network::PacketLayout Layout(Network::Opcode opcode, Network::Subcode subcode, std::size_t minimumSize,
                                       std::uint32_t address)
{
    return {{Network::PacketType::C1, opcode, subcode}, minimumSize, address};
}

template <typename ReadFields>
Result ReadAs(const Network::Packet& packet, const Network::PacketLayout& layout, ReadFields read)
{
    return Network::ReadPacket(packet, layout,
                               [&read](Network::LayoutReader& reader) { return EventPacket{read(reader)}; });
}

template <std::size_t Size> std::array<char, Size> CharsAt(Network::LayoutReader& reader, std::size_t at)
{
    std::array<char, Size> chars{};
    std::ranges::transform(reader.Bytes(at, Size), chars.begin(),
                           [](std::byte value) { return std::to_integer<char>(value); });
    return chars;
}

std::vector<std::byte> RestFrom(Network::LayoutReader& reader, std::size_t at)
{
    const std::span<const std::byte> rest = reader.Bytes(at, reader.Size() - at);
    return {rest.begin(), rest.end()};
}

// 0x4dab90: type 0 reads 4 bytes per rectangle and type 1, 2 per cell, from pkt[7]; another type, nothing.
constexpr std::size_t AttributesFirstAt = 7;
constexpr std::size_t AttributeAreaSize = 4;
constexpr std::size_t AttributeCellSize = 2;

AttributeArea AreaOf(std::span<const std::byte, AttributeAreaSize> bytes)
{
    return {std::to_integer<std::uint8_t>(bytes[0]), std::to_integer<std::uint8_t>(bytes[1]),
            std::to_integer<std::uint8_t>(bytes[2]), std::to_integer<std::uint8_t>(bytes[3])};
}

AttributeCell CellOf(std::span<const std::byte, AttributeCellSize> bytes)
{
    return {std::to_integer<std::uint8_t>(bytes[0]), std::to_integer<std::uint8_t>(bytes[1])};
}

Result ReadMapAttributes(const Network::Packet& packet)
{
    return ReadAs(
        packet, Layout(MapAttributesOpcode, AttributesFirstAt, 0x4dab90),
        [](Network::LayoutReader& reader)
        {
            MapAttributes attributes{
                reader.Read<Byte<3>>(), reader.Read<Byte<4>>(), reader.Read<Byte<5>>(), reader.Read<Byte<6>>(), {}, {}};
            if (attributes.type == 0)
            {
                attributes.areas =
                    Network::ReadList<AttributeAreaSize>(reader, AttributesFirstAt, attributes.count, AreaOf);
            }
            else if (attributes.type == 1)
            {
                attributes.cells =
                    Network::ReadList<AttributeCellSize>(reader, AttributesFirstAt, attributes.count, CellOf);
            }
            return attributes;
        });
}

// 0x4da8c0: 1 and 2 read pkt[4]; 4, pkt[4], pkt[5] and pkt[6]; the others, nothing.
Result ReadEventOpenTime(const Network::Packet& packet)
{
    return ReadAs(packet, Layout(EventOpenTimeOpcode, 4, 0x4da8c0),
                  [](Network::LayoutReader& reader)
                  {
                      EventOpenTime time{reader.Read<Byte<3>>()};
                      if (time.kind == 1 || time.kind == 2)
                      {
                          time.value4 = reader.Read<Byte<4>>();
                      }
                      else if (time.kind == 4)
                      {
                          time.value4 = reader.Read<Byte<4>>();
                          time.value5 = reader.Read<Byte<5>>();
                          time.value6 = reader.Read<Byte<6>>();
                      }
                      return time;
                  });
}

Result ReadEventChipInfo(const Network::Packet& packet)
{
    return ReadAs(packet, Layout(EventChipInfoOpcode, Word<4>::End, 0x4dd8a0),
                  [](Network::LayoutReader& reader)
                  {
                      EventChipInfo info{reader.Read<Byte<3>>(), reader.Read<Word<4>>(), std::nullopt};
                      // 0x4dd8f0..0x4dd940: the three words are read only with type 0 (0x78abb00 at 1).
                      if (reader.Size() >= Word<0xa>::End)
                      {
                          info.muto = std::array<std::uint16_t, 3>{reader.Read<Word<6>>(), reader.Read<Word<8>>(),
                                                                   reader.Read<Word<0xa>>()};
                      }
                      return info;
                  });
}

// 0x4d9f00 reads up to the word at pkt[0x15] (which takes a byte past the GS's 0x16, 08).
Result ReadSoccerScore(const Network::Packet& packet)
{
    return ReadAs(packet, Layout(CharacterOpcode, Character::SoccerScoreSubcode, 0x16, 0x4d9f00),
                  [](Network::LayoutReader& reader)
                  {
                      return SoccerScore{.firstName = CharsAt<8>(reader, 4),
                                         .firstScore = reader.Read<Byte<0xc>>(),
                                         .secondName = CharsAt<8>(reader, 0xd),
                                         .secondScore = reader.Read<Byte<0x15>>()};
                  });
}

// F3 24, 0x4deef0: with present < 3 it copies the names of 10 and the scores; with another present it reads no more.
Result ReadGuildWarOutcome(const Network::Packet& packet)
{
    return ReadAs(packet, Layout(CharacterOpcode, Character::GuildWarOutcomeSubcode, Byte<4>::End, 0x4deef0),
                  [](Network::LayoutReader& reader)
                  {
                      GuildWarOutcome outcome{.present = reader.Read<Byte<4>>()};
                      if (outcome.present >= 3)
                      {
                          return outcome;
                      }
                      outcome.firstName = CharsAt<10>(reader, 5);
                      outcome.secondName = CharsAt<10>(reader, 0x12);
                      outcome.firstScore = reader.Read<Word<0x10>>();
                      outcome.secondScore = reader.Read<Word<0x1c>>();
                      return outcome;
                  });
}

Result ReadWorld(const Network::Packet& packet, Network::Subcode subcode)
{
    switch (Network::ByteOf(subcode))
    {
    case Network::ByteOf(Character::EventTimeSubcode):
        return ReadAs(packet, Layout(CharacterOpcode, subcode, Word<6>::End, 0x4e31f8),
                      [](Network::LayoutReader& reader)
                      { return EventTime{reader.Read<Byte<4>>(), reader.Read<Word<6>>()}; });
    case Network::ByteOf(Character::SoccerScoreSubcode):
        return ReadSoccerScore(packet);
    case Network::ByteOf(Character::GuildWarOutcomeSubcode):
        return ReadGuildWarOutcome(packet);
    case Network::ByteOf(Character::ServerCommandSubcode):
        return ReadAs(packet, Layout(CharacterOpcode, subcode, Byte<6>::End, 0x4da070),
                      [](Network::LayoutReader& reader) {
                          return ServerCommand{reader.Read<Byte<4>>(), reader.Read<Byte<5>>(), reader.Read<Byte<6>>()};
                      });
    default:
        return Core::Unexpected(Network::ProtocolError::WrongPacket);
    }
}

template <typename Packet>
Result ReadFirstByte(const Network::Packet& packet, Network::Opcode opcode, std::uint32_t address)
{
    return ReadAs(packet, Layout(opcode, Byte<3>::End, address),
                  [](Network::LayoutReader& reader) { return Packet{reader.Read<Byte<3>>()}; });
}

Result ReadByOpcode(const Network::Packet& packet, Network::Opcode opcode)
{
    switch (Network::ByteOf(opcode))
    {
    case Network::ByteOf(EventStateOpcode):
        return ReadAs(packet, Layout(opcode, Byte<4>::End, 0x4e3594), [](Network::LayoutReader& reader)
                      { return EventState{reader.Read<Byte<3>>(), reader.Read<Byte<4>>()}; });
    case Network::ByteOf(MapAttributesOpcode):
        return ReadMapAttributes(packet);
    case Network::ByteOf(ItemAcquiredOpcode):
        // 0x4d69a1..0x4d69aa: pkt[3] << 8 + pkt[4].
        return ReadAs(
            packet, Layout(opcode, Byte<0xc>::End, 0x4d6990), [](Network::LayoutReader& reader)
            { return ItemAcquired{reader.Read<BigWord<3>>(), reader.Read<Dword<8>>(), reader.Read<Byte<0xc>>()}; });
    case Network::ByteOf(DevilSquareEntryOpcode):
        return ReadFirstByte<DevilSquareEntryResult>(packet, opcode, 0x4e4870);
    case Network::ByteOf(EventOpenTimeOpcode):
        return ReadEventOpenTime(packet);
    case Network::ByteOf(EventCountdownOpcode):
        return ReadFirstByte<EventCountdown>(packet, opcode, 0x4e4930);
    case Network::ByteOf(EventResultOpcode):
        return ReadAs(
            packet, Layout(opcode, Byte<4>::End, 0x4e4959), [](Network::LayoutReader& reader)
            { return EventResult{reader.Read<Byte<3>>(), reader.Read<Byte<4>>(), RestFrom(reader, Byte<4>::End)}; });
    case Network::ByteOf(EventChipInfoOpcode):
        return ReadEventChipInfo(packet);
    case Network::ByteOf(EventChipCountOpcode):
        return ReadAs(packet, Layout(opcode, Word<4>::End, 0x4e49d4),
                      [](Network::LayoutReader& reader) { return EventChipCount{reader.Read<Word<4>>()}; });
    case Network::ByteOf(MutoNumbersOpcode):
        return ReadAs(packet, Layout(opcode, Word<8>::End, 0x4e49fb),
                      [](Network::LayoutReader& reader) {
                          return MutoNumbers{{reader.Read<Word<4>>(), reader.Read<Word<6>>(), reader.Read<Word<8>>()}};
                      });
    case Network::ByteOf(EventDialogOpcode):
        return ReadFirstByte<EventDialog>(packet, opcode, 0x4e4a40);
    case Network::ByteOf(BloodCastleEntryOpcode):
        return ReadFirstByte<BloodCastleEntryResult>(packet, opcode, 0x4da780);
    case Network::ByteOf(EventPlayStateOpcode):
        return ReadAs(packet, Layout(opcode, Byte<0xc>::End, 0x4e4999),
                      [](Network::LayoutReader& reader)
                      {
                          return EventPlayState{reader.Read<Byte<3>>(),   reader.Read<Word<4>>(),
                                                reader.Read<Word<6>>(),   reader.Read<Word<8>>(),
                                                reader.Read<Word<0xa>>(), reader.Read<Byte<0xc>>()};
                      });
    case Network::ByteOf(SerialResultOpcode):
    {
        using SerialData = Network::Field<std::array<std::byte, 0x40>, 4>;
        return ReadAs(packet, Layout(opcode, SerialData::End, 0x4e4a86), [](Network::LayoutReader& reader)
                      { return SerialResult{reader.Read<Byte<3>>(), reader.Read<SerialData>()}; });
    }
    case Network::ByteOf(EventSoundOpcode):
        return ReadAs(packet, Layout(opcode, Word<4>::End, 0x4e4ad4),
                      [](Network::LayoutReader& reader) { return EventSound{reader.Read<Word<4>>()}; });
    default:
        return Core::Unexpected(Network::ProtocolError::WrongPacket);
    }
}

// 0x4e1680..0x4e16b9 (BD 08): pkt[4] records of 0x14 bytes from pkt+5, without looking at the packet's size.
constexpr std::size_t RankingFirstAt = 5;
constexpr std::size_t RankingEntryBytes = 0x14;

CrywolfRankingEntry RankingEntryOf(std::span<const std::byte, RankingEntryBytes> bytes)
{
    constexpr std::size_t NameAt = 1;
    constexpr std::size_t ScoreAt = 0xc;
    constexpr std::size_t ClassAt = 0x10;
    CrywolfRankingEntry entry{.place = std::to_integer<std::uint8_t>(bytes[0]),
                              .score =
                                  static_cast<std::int32_t>(std::to_integer<std::uint32_t>(bytes[ScoreAt]) |
                                                            std::to_integer<std::uint32_t>(bytes[ScoreAt + 1]) << 8U |
                                                            std::to_integer<std::uint32_t>(bytes[ScoreAt + 2]) << 16U |
                                                            std::to_integer<std::uint32_t>(bytes[ScoreAt + 3]) << 24U),
                              .classByte = std::to_integer<std::uint8_t>(bytes[ClassAt])};
    std::ranges::transform(bytes.subspan(NameAt, entry.name.size()), entry.name.begin(),
                           [](std::byte value) { return std::to_integer<char>(value); });
    return entry;
}

// 0x4e5941: the BD by pkt[3] with the table of 0x4e620c; 1, 6, 0xa, 0xb and the ones over 0xc do nothing.
Result ReadCrywolf(const Network::Packet& packet, Network::Subcode subcode)
{
    switch (Network::ByteOf(subcode))
    {
    case Network::ByteOf(Crywolf::StateSubcode):
        return ReadAs(packet, Layout(CrywolfOpcode, subcode, Byte<5>::End, 0x4e5941), [](Network::LayoutReader& reader)
                      { return CrywolfState{reader.Read<Byte<4>>(), reader.Read<Byte<5>>()}; });
    case Network::ByteOf(Crywolf::AltarsSubcode):
        return ReadAs(packet, Layout(CrywolfOpcode, subcode, Byte<0xc>::End, 0x4e5990),
                      [](Network::LayoutReader& reader)
                      {
                          return CrywolfAltars{static_cast<std::int32_t>(reader.Read<Dword<4>>()),
                                               {reader.Read<Byte<8>>(), reader.Read<Byte<9>>(),
                                                reader.Read<Byte<0xa>>(), reader.Read<Byte<0xb>>(),
                                                reader.Read<Byte<0xc>>()}};
                      });
    case Network::ByteOf(Crywolf::TimerSubcode):
        return ReadAs(packet, Layout(CrywolfOpcode, subcode, Byte<5>::End, 0x4e59ba), [](Network::LayoutReader& reader)
                      { return CrywolfTimer{reader.Read<Byte<4>>(), reader.Read<Byte<5>>()}; });
    case Network::ByteOf(Crywolf::BossSubcode):
        return ReadAs(
            packet, Layout(CrywolfOpcode, subcode, Byte<8>::End, 0x4e59eb), [](Network::LayoutReader& reader)
            { return CrywolfBoss{static_cast<std::int32_t>(reader.Read<Dword<4>>()), reader.Read<Byte<8>>()}; });
    case Network::ByteOf(Crywolf::RankSubcode):
        return ReadAs(
            packet, Layout(CrywolfOpcode, subcode, Dword<8>::End, 0x4e5a09), [](Network::LayoutReader& reader)
            { return CrywolfRank{reader.Read<Byte<4>>(), static_cast<std::int32_t>(reader.Read<Dword<8>>())}; });
    case Network::ByteOf(Crywolf::RankingSubcode):
        return ReadAs(packet, Layout(CrywolfOpcode, subcode, RankingFirstAt, 0x4e1680),
                      [](Network::LayoutReader& reader)
                      {
                          return CrywolfRanking{Network::ReadList<RankingEntryBytes>(
                              reader, RankingFirstAt, reader.Read<Byte<4>>(), RankingEntryOf)};
                      });
    case Network::ByteOf(Crywolf::ChaosBoxSubcode):
        return ReadAs(packet, Layout(CrywolfOpcode, subcode, Byte<4>::End, 0x4e5a25),
                      [](Network::LayoutReader& reader) { return CrywolfChaosBox{reader.Read<Byte<4>>()}; });
    case Network::ByteOf(Crywolf::ContractSubcode):
        return ReadAs(
            packet, Layout(CrywolfOpcode, subcode, BigWord<6>::End, 0x4e1010), [](Network::LayoutReader& reader)
            { return CrywolfContract{reader.Read<Byte<4>>(), reader.Read<Byte<5>>(), reader.Read<BigWord<6>>()}; });
    case Network::ByteOf(Crywolf::EffectSubcode):
        // 0x4e15fa reads pkt[9]; without that byte a coordinate of the effect is missing.
        return ReadAs(packet, Layout(CrywolfOpcode, subcode, Byte<9>::End, 0x4e15f0),
                      [subcode](Network::LayoutReader& reader)
                      { return CrywolfUnported{Network::ByteOf(subcode), RestFrom(reader, 0)}; });
    default:
        return Core::Unexpected(Network::ProtocolError::WrongPacket);
    }
}
} // namespace

Core::Expected<EventPacket, Network::ProtocolError> ReadEventPacket(const Network::Packet& packet)
{
    if (packet.Type() != Network::PacketType::C1)
        return Core::Unexpected(Network::ProtocolError::WrongPacket);
    const std::optional<Network::Opcode> read = packet.Opcode();
    if (!read)
        return Core::Unexpected(Network::ProtocolError::TooShort);
    const Network::Opcode opcode = *read;
    const bool withSubcode =
        opcode == ChaosCastleOpcode || opcode == JewelMixOpcode || opcode == CrywolfOpcode || opcode == CharacterOpcode;
    if (!withSubcode)
        return ReadByOpcode(packet, opcode);
    const std::span<const std::byte> bytes = packet.Bytes();
    if (bytes.size() < SubcodeEnd)
        return Core::Unexpected(Network::ProtocolError::TooShort);
    const Network::Subcode subcode{std::to_integer<std::uint8_t>(bytes[3])};
    if (opcode == CharacterOpcode)
        return ReadWorld(packet, subcode);
    if (opcode == CrywolfOpcode)
        return ReadCrywolf(packet, subcode);
    if (opcode == ChaosCastleOpcode && subcode == ChaosCastle::EntrySubcode)
    {
        return ReadAs(packet, Layout(opcode, subcode, Byte<4>::End, 0x4daa90),
                      [](Network::LayoutReader& reader) { return ChaosCastleEntryResult{reader.Read<Byte<4>>()}; });
    }
    if (opcode == JewelMixOpcode && (subcode == JewelMix::MixSubcode || subcode == JewelMix::UnmixSubcode))
    {
        return ReadAs(
            packet, Layout(opcode, subcode, Byte<4>::End, subcode == JewelMix::MixSubcode ? 0x4e4547 : 0x4da540),
            [subcode](Network::LayoutReader& reader) { return JewelMixResult{subcode, reader.Read<Byte<4>>()}; });
    }
    return Core::Unexpected(Network::ProtocolError::WrongPacket);
}

Outgoing WriteEventWindowLeave()
{
    return ClearC1(Network::PacketWriter::C1(EventWindowLeaveOpcode));
}

Outgoing WriteEventTimeRequest(std::uint8_t kind)
{
    return Xor32C1(Network::PacketWriter::C1(EventOpenTimeOpcode).PutU8(kind));
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
    return Xor32C1(Network::PacketWriter::C1(ChaosCastleOpcode, ChaosCastle::RepositionSubcode).PutU8(x).PutU8(y));
}

Outgoing WriteCrywolfContract(std::int32_t key)
{
    constexpr unsigned HighShift = 8;
    return Xor32C1(Network::PacketWriter::C1(CrywolfOpcode, Crywolf::ContractSubcode)
                       .PutU8(static_cast<std::uint8_t>(key >> HighShift))
                       .PutU8(static_cast<std::uint8_t>(key)));
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
