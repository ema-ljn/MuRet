#include "Protocol/CastleSiege/CastleSiege.h"

#include "Core/Expected.h"
#include "Network/Field.h"
#include "Network/LayoutReader.h"
#include "Network/Packet.h"
#include "Network/PacketCode.h"
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

namespace Client::Protocol::CastleSiege
{
namespace
{
using Result = Core::Expected<CastleSiegePacket, Network::ProtocolError>;

template <std::size_t At> using Byte = Network::Field<std::uint8_t, At>;
// MOV r32: little-endian.
template <std::size_t At> using Dword = Network::Field<std::uint32_t, At>;
// What the original builds byte by byte (SHL 8 and OR): the first byte on top.
template <std::size_t At> using BigWord = Network::Field<std::uint16_t, At, std::endian::big>;
template <std::size_t At> using BigDword = Network::Field<std::uint32_t, At, std::endian::big>;
template <std::size_t At> using BigQword = Network::Field<std::array<std::byte, sizeof(std::uint64_t)>, At>;

using ResultByte = Byte<4>;

// The handlers read at fixed offsets whatever the type: in a C2 the subcode and the result are the same pkt[4].
Network::PacketLayout LayoutOf(const Network::Packet& packet, Network::Opcode opcode,
                               std::optional<Network::Subcode> subcode, std::size_t minimumSize, std::uint32_t address)
{
    return {{packet.Type(), opcode, subcode}, minimumSize, address};
}

Network::PacketLayout CastleLayout(const Network::Packet& packet, Network::Subcode subcode, std::uint32_t address)
{
    return LayoutOf(packet, CastleOpcode, subcode, ResultByte::End, address);
}

template <typename ReadFields>
Result ReadAs(const Network::Packet& packet, const Network::PacketLayout& layout, ReadFields read)
{
    return Network::ReadPacket(packet, layout,
                               [&read](Network::LayoutReader& reader) { return CastleSiegePacket{read(reader)}; });
}

std::uint8_t U8At(std::span<const std::byte> bytes, std::size_t at)
{
    return std::to_integer<std::uint8_t>(bytes[at]);
}

std::uint64_t BigEndian(std::span<const std::byte> bytes)
{
    std::uint64_t value = 0;
    for (const std::byte b : bytes)
    {
        value = (value << 8U) | std::to_integer<std::uint64_t>(b);
    }
    return value;
}

template <std::size_t Size> std::array<std::byte, Size> CopyOf(std::span<const std::byte> bytes)
{
    std::array<std::byte, Size> out{};
    std::ranges::copy(bytes.first(Size), out.begin());
    return out;
}

// 0x4dfc60, 0x4dfce0 and 0x4dfd80: the count at pkt+8 and the records from pkt+0xc; 0x4e0ed0: the count at pkt+4 and
// the pairs from pkt+8.
using ListCount = Dword<8>;
constexpr std::size_t ListRecordsAt = 0xc;
constexpr std::size_t SiegeGuildEntrySize = 0xe;
constexpr std::size_t RegisteredGuildEntrySize = 10;
using MinimapCount = Dword<4>;
constexpr std::size_t MinimapPointsAt = 8;
constexpr std::size_t MinimapPointSize = 2;

// An int, and the loop (JLE before it, JL on the way back) does not run with 0 or a negative one.
template <typename F> std::size_t CountOf(Network::LayoutReader& reader)
{
    const auto count = static_cast<std::int32_t>(reader.Read<F>());
    return count > 0 ? static_cast<std::size_t>(count) : std::size_t{0};
}

// A case that, with pkt[4] at 0 or another value, only shows a message: the result and nothing else.
template <typename ResultPacket>
Result OnlyTheResult(const Network::Packet& packet, Network::Subcode subcode, std::uint32_t address)
{
    return ReadAs(packet, CastleLayout(packet, subcode, address),
                  [](Network::LayoutReader& reader) { return ResultPacket{.result = reader.Read<ResultByte>()}; });
}

// 0x64b0f0: each date is six bytes, the year on top.
constexpr std::size_t SiegeDateSize = 6;

template <std::size_t At> SiegeDate DateAt(Network::LayoutReader& reader)
{
    SiegeDate date{.year = reader.Read<BigWord<At>>(), .rest = {}};
    const auto rest = reader.Read<Network::Field<std::array<std::byte, 4>, At + 2>>();
    std::ranges::transform(rest, date.rest.begin(), [](std::byte b) { return std::to_integer<std::uint8_t>(b); });
    return date;
}

// B2 00, 0x4e50bd → 0x64b0f0 with pkt[4] at 1 or 2.
Result ReadState(const Network::Packet& packet)
{
    return ReadAs(packet, CastleLayout(packet, Castle::StateSubcode, 0x4e50bd),
                  [](Network::LayoutReader& reader)
                  {
                      CastleState state{.result = reader.Read<ResultByte>()};
                      if (state.result != 1 && state.result != 2)
                      {
                          return state;
                      }
                      state.state = static_cast<std::int8_t>(reader.Read<Byte<5>>());
                      state.dates = {DateAt<6>(reader), DateAt<6 + SiegeDateSize>(reader),
                                     DateAt<6 + 2 * SiegeDateSize>(reader)};
                      state.ownerGuild = reader.Read<Network::Field<GuildName, 0x18>>();
                      state.ownerMaster = reader.Read<Network::Field<FrontEnd::NameBytes, 0x20>>();
                      state.remaining = reader.Read<BigDword<0x2a>>();
                      return state;
                  });
}

// B2 03, 0x4e5186: with 1, the marks and pkt[0x11].
Result ReadRegistration(const Network::Packet& packet)
{
    return ReadAs(packet, CastleLayout(packet, Castle::RegistrationSubcode, 0x4e5186),
                  [](Network::LayoutReader& reader)
                  {
                      RegistrationInfo info{.result = reader.Read<ResultByte>()};
                      if (info.result == ResultOk)
                      {
                          info.marks = reader.Read<BigDword<0xd>>();
                          info.gaveUp = reader.Read<Byte<0x11>>();
                      }
                      return info;
                  });
}

// B2 04, 0x4e51fa: with 1, the marks.
Result ReadRegisterMark(const Network::Packet& packet)
{
    return ReadAs(packet, CastleLayout(packet, Castle::RegisterMarkSubcode, 0x4e51fa),
                  [](Network::LayoutReader& reader)
                  {
                      RegisterMarkResult mark{.result = reader.Read<ResultByte>()};
                      if (mark.result == ResultOk)
                      {
                          mark.marks = reader.Read<BigDword<0xd>>();
                      }
                      return mark;
                  });
}

// B2 05, 0x4df7d0: with 1, the type and the index.
Result ReadNpcBuy(const Network::Packet& packet)
{
    return ReadAs(packet, CastleLayout(packet, Castle::NpcBuySubcode, 0x4df7d0),
                  [](Network::LayoutReader& reader)
                  {
                      NpcBuyResult buy{.result = reader.Read<ResultByte>()};
                      if (buy.result == ResultOk)
                      {
                          buy.type = reader.Read<Dword<8>>();
                          buy.index = reader.Read<Dword<0xc>>();
                      }
                      return buy;
                  });
}

// B2 06, 0x4e52b7: with 1, the type, the index and the two lives.
Result ReadNpcRepair(const Network::Packet& packet)
{
    return ReadAs(packet, CastleLayout(packet, Castle::NpcRepairSubcode, 0x4e52b7),
                  [](Network::LayoutReader& reader)
                  {
                      NpcRepairResult repair{.result = reader.Read<ResultByte>()};
                      if (repair.result == ResultOk)
                      {
                          repair.type = reader.Read<Dword<8>>();
                          repair.index = reader.Read<Dword<0xc>>();
                          repair.life = reader.Read<Dword<0x10>>();
                          repair.maxLife = reader.Read<Dword<0x14>>();
                      }
                      return repair;
                  });
}

// B2 07, 0x4df950: with 1, the type, the index and which; only cases 1, 2 and 3 read the value (pkt+0x14).
Result ReadNpcUpgrade(const Network::Packet& packet)
{
    return ReadAs(packet, CastleLayout(packet, Castle::NpcUpgradeSubcode, 0x4df950),
                  [](Network::LayoutReader& reader)
                  {
                      NpcUpgradeResult upgrade{.result = reader.Read<ResultByte>()};
                      if (upgrade.result != ResultOk)
                      {
                          return upgrade;
                      }
                      upgrade.type = reader.Read<Dword<8>>();
                      upgrade.index = reader.Read<Dword<0xc>>();
                      upgrade.kind = reader.Read<Dword<0x10>>();
                      if (upgrade.kind >= 1 && upgrade.kind <= 3)
                      {
                          upgrade.value = reader.Read<Dword<0x14>>();
                      }
                      return upgrade;
                  });
}

// B2 08, 0x4e537b → 0x669070: with 1, the two taxes and the money.
Result ReadTaxInfo(const Network::Packet& packet)
{
    return ReadAs(packet, CastleLayout(packet, Castle::TaxInfoSubcode, 0x4e537b),
                  [](Network::LayoutReader& reader)
                  {
                      TaxInfo info{.result = reader.Read<ResultByte>()};
                      if (info.result == ResultOk)
                      {
                          info.chaosTax = reader.Read<Byte<5>>();
                          info.storeTax = reader.Read<Byte<6>>();
                          info.money = BigEndian(reader.Read<BigQword<7>>());
                      }
                      return info;
                  });
}

// B2 09, 0x4e53e7: with 1, which; 3 (0x4e541b) and 1 and 2 (0x6690f0) read the value.
Result ReadTaxRate(const Network::Packet& packet)
{
    return ReadAs(packet, CastleLayout(packet, Castle::TaxRateSubcode, 0x4e53e7),
                  [](Network::LayoutReader& reader)
                  {
                      TaxRateResult rate{.result = reader.Read<ResultByte>()};
                      if (rate.result != ResultOk)
                      {
                          return rate;
                      }
                      rate.kind = reader.Read<Byte<5>>();
                      if (rate.kind >= 1 && rate.kind <= 3)
                      {
                          rate.value = reader.Read<BigDword<6>>();
                      }
                      return rate;
                  });
}

// B2 10, 0x4e548a → 0x669160: with 1, the money.
Result ReadMoneyOut(const Network::Packet& packet)
{
    return ReadAs(packet, CastleLayout(packet, Castle::MoneyOutSubcode, 0x4e548a),
                  [](Network::LayoutReader& reader)
                  {
                      MoneyOutResult out{.result = reader.Read<ResultByte>()};
                      if (out.result == ResultOk)
                      {
                          out.money = BigEndian(reader.Read<BigQword<5>>());
                      }
                      return out;
                  });
}

// B2 1A, 0x4e54f6: pkt[5] only with pkt[4] at 1 or 2.
Result ReadSiegeFlags(const Network::Packet& packet)
{
    return ReadAs(packet, CastleLayout(packet, Castle::SiegeFlagsSubcode, 0x4e54f6),
                  [](Network::LayoutReader& reader)
                  {
                      SiegeFlags flags{.which = reader.Read<ResultByte>()};
                      if (flags.which == 1 || flags.which == 2)
                      {
                          flags.value = reader.Read<Byte<5>>();
                      }
                      return flags;
                  });
}

// B2 1F, 0x4e5532: pkt[5] only with 1.
Result ReadHuntZoneGate(const Network::Packet& packet)
{
    return ReadAs(packet, CastleLayout(packet, Castle::HuntZoneGateSubcode, 0x4e5532),
                  [](Network::LayoutReader& reader)
                  {
                      HuntZoneGateResult gate{.result = reader.Read<ResultByte>()};
                      if (gate.result == ResultOk)
                      {
                          gate.allowed = reader.Read<Byte<5>>();
                      }
                      return gate;
                  });
}

NpcRecord NpcRecordOf(std::span<const std::byte, NpcRecordSize> bytes)
{
    return CopyOf<NpcRecordSize>(bytes);
}

// B3, 0x4dfc60.
Result ReadNpcList(const Network::Packet& packet)
{
    return ReadAs(packet, LayoutOf(packet, CastleNpcListOpcode, std::nullopt, ResultByte::End, 0x4dfc60),
                  [](Network::LayoutReader& reader)
                  {
                      NpcList list{.result = reader.Read<ResultByte>(), .records = {}};
                      if (list.result == ResultOk)
                      {
                          list.records = Network::ReadList<NpcRecordSize>(reader, ListRecordsAt,
                                                                          CountOf<ListCount>(reader), NpcRecordOf);
                      }
                      return list;
                  });
}

// B4, 0x4dfce0: each record as it passes it to 0x64d2c0.
SiegeGuildEntry SiegeGuildEntryOf(std::span<const std::byte, SiegeGuildEntrySize> bytes)
{
    return {.name = CopyOf<GuildNameSize>(bytes),
            .marks = static_cast<std::uint32_t>(BigEndian(bytes.subspan<8, 4>())),
            .first = U8At(bytes, 0xc),
            .second = U8At(bytes, 0xd)};
}

Result ReadSiegeGuildList(const Network::Packet& packet)
{
    return ReadAs(packet, LayoutOf(packet, SiegeGuildListOpcode, std::nullopt, ResultByte::End, 0x4dfce0),
                  [](Network::LayoutReader& reader)
                  {
                      SiegeGuildList list{.result = reader.Read<ResultByte>(), .entries = {}};
                      if (list.result == ResultOk)
                      {
                          list.entries = Network::ReadList<SiegeGuildEntrySize>(
                              reader, ListRecordsAt, CountOf<ListCount>(reader), SiegeGuildEntryOf);
                      }
                      return list;
                  });
}

// B5, 0x4dfd80: each record as it passes it to 0x64d310.
RegisteredGuildEntry RegisteredGuildEntryOf(std::span<const std::byte, RegisteredGuildEntrySize> bytes)
{
    return {.first = U8At(bytes, 0), .second = U8At(bytes, 1), .name = CopyOf<GuildNameSize>(bytes.subspan<2>())};
}

Result ReadRegisteredGuildList(const Network::Packet& packet)
{
    return ReadAs(packet, LayoutOf(packet, RegisteredGuildListOpcode, std::nullopt, ResultByte::End, 0x4dfd80),
                  [](Network::LayoutReader& reader)
                  {
                      RegisteredGuildList list{.result = reader.Read<ResultByte>(), .entries = {}};
                      if (list.result == ResultOk)
                      {
                          list.entries = Network::ReadList<RegisteredGuildEntrySize>(
                              reader, ListRecordsAt, CountOf<ListCount>(reader), RegisteredGuildEntryOf);
                      }
                      return list;
                  });
}

// B6, 0x4e0ed0.
MinimapPoint MinimapPointOf(std::span<const std::byte, MinimapPointSize> bytes)
{
    return {.x = U8At(bytes, 0), .y = U8At(bytes, 1)};
}

Result ReadMinimapPoints(const Network::Packet& packet)
{
    return ReadAs(packet, LayoutOf(packet, SiegeMinimapOpcode, std::nullopt, MinimapCount::End, 0x4e0ed0),
                  [](Network::LayoutReader& reader)
                  {
                      return MinimapPoints{.points = Network::ReadList<MinimapPointSize>(
                                               reader, MinimapPointsAt, CountOf<MinimapCount>(reader), MinimapPointOf)};
                  });
}

// B8 01, 0x4e0700.
Result ReadKillCount(const Network::Packet& packet)
{
    return ReadAs(packet,
                  LayoutOf(packet, KillCountOpcode, Protocol::KillCount::CountSubcode, ResultByte::End, 0x4e0700),
                  [](Network::LayoutReader& reader) { return KillCount{.value = reader.Read<ResultByte>()}; });
}

// The B2 by its subcode, the table 0x4e6140.
Result ReadCastle(const Network::Packet& packet, Network::Subcode subcode)
{
    switch (Network::ByteOf(subcode))
    {
    case Network::ByteOf(Castle::StateSubcode):
        return ReadState(packet);
    case Network::ByteOf(Castle::RegisterSubcode):
        return OnlyTheResult<RegisterResult>(packet, subcode, 0x4df080);
    case Network::ByteOf(Castle::GiveUpSubcode):
        return OnlyTheResult<GiveUpResult>(packet, subcode, 0x4df1a0);
    case Network::ByteOf(Castle::RegistrationSubcode):
        return ReadRegistration(packet);
    case Network::ByteOf(Castle::RegisterMarkSubcode):
        return ReadRegisterMark(packet);
    case Network::ByteOf(Castle::NpcBuySubcode):
        return ReadNpcBuy(packet);
    case Network::ByteOf(Castle::NpcRepairSubcode):
        return ReadNpcRepair(packet);
    case Network::ByteOf(Castle::NpcUpgradeSubcode):
        return ReadNpcUpgrade(packet);
    case Network::ByteOf(Castle::TaxInfoSubcode):
        return ReadTaxInfo(packet);
    case Network::ByteOf(Castle::TaxRateSubcode):
        return ReadTaxRate(packet);
    case Network::ByteOf(Castle::MoneyOutSubcode):
        return ReadMoneyOut(packet);
    case Network::ByteOf(Castle::SiegeFlagsSubcode):
        return ReadSiegeFlags(packet);
    case Network::ByteOf(Castle::HuntZoneGateSubcode):
        return ReadHuntZoneGate(packet);
    default:
        return Core::Unexpected(Network::ProtocolError::WrongPacket);
    }
}

// 0x4e509c and 0x4e5883: the byte after the opcode, as the Dispatcher reads it.
std::optional<Network::Subcode> SubcodeOf(const Network::Packet& packet)
{
    const std::span<const std::byte> bytes = packet.Bytes();
    const std::size_t at = Network::SubcodeOffset(packet.Type());
    if (bytes.size() <= at)
    {
        return std::nullopt;
    }
    return Network::Subcode{std::to_integer<std::uint8_t>(bytes[at])};
}

// The original's writer for a C1 with the subcode: it encrypts from byte 3, like everything the windows send.
Network::PacketWriter Castle(Network::Subcode subcode)
{
    return Network::PacketWriter::C1(CastleOpcode, subcode);
}
} // namespace

Core::Expected<CastleSiegePacket, Network::ProtocolError> ReadCastleSiegePacket(const Network::Packet& packet)
{
    const auto opcode = packet.Opcode();
    if (!opcode)
    {
        return Core::Unexpected(Network::ProtocolError::WrongPacket);
    }
    switch (Network::ByteOf(*opcode))
    {
    case Network::ByteOf(CastleOpcode):
    {
        const auto subcode = SubcodeOf(packet);
        if (!subcode)
        {
            return Core::Unexpected(Network::ProtocolError::TooShort);
        }
        return ReadCastle(packet, *subcode);
    }
    case Network::ByteOf(KillCountOpcode):
        return ReadKillCount(packet);
    case Network::ByteOf(CastleNpcListOpcode):
        return ReadNpcList(packet);
    case Network::ByteOf(SiegeGuildListOpcode):
        return ReadSiegeGuildList(packet);
    case Network::ByteOf(RegisteredGuildListOpcode):
        return ReadRegisteredGuildList(packet);
    case Network::ByteOf(SiegeMinimapOpcode):
        return ReadMinimapPoints(packet);
    default:
        return Core::Unexpected(Network::ProtocolError::WrongPacket);
    }
}

Outgoing WriteStateRequest()
{
    return Xor32C1(Castle(Castle::StateSubcode));
}

Outgoing WriteRegister()
{
    return Xor32C1(Castle(Castle::RegisterSubcode));
}

Outgoing WriteGiveUp()
{
    return Xor32C1(Castle(Castle::GiveUpSubcode).PutU8(1));
}

Outgoing WriteRegistrationRequest()
{
    return Xor32C1(Castle(Castle::RegistrationSubcode));
}

Outgoing WriteRegisterMark(std::uint8_t slot)
{
    return Xor32C1(Castle(Castle::RegisterMarkSubcode).PutU8(slot));
}

Outgoing WriteNpcBuy(std::uint32_t type, std::uint32_t index)
{
    return Xor32C1(Castle(Castle::NpcBuySubcode).PutU32Le(type).PutU32Le(index));
}

Outgoing WriteNpcRepair(std::uint32_t type, std::uint32_t index)
{
    return Xor32C1(Castle(Castle::NpcRepairSubcode).PutU32Le(type).PutU32Le(index));
}

Outgoing WriteNpcUpgrade(std::uint32_t type, std::uint32_t index, std::uint32_t kind, std::uint32_t value)
{
    return Xor32C1(Castle(Castle::NpcUpgradeSubcode).PutU32Le(type).PutU32Le(index).PutU32Le(kind).PutU32Le(value));
}

Outgoing WriteTaxInfoRequest()
{
    return Xor32C1(Castle(Castle::TaxInfoSubcode));
}

Outgoing WriteTaxRate(std::uint8_t kind, std::uint32_t value)
{
    return Xor32C1(Castle(Castle::TaxRateSubcode)
                       .PutU8(kind)
                       .PutU8(static_cast<std::uint8_t>(value >> 24u))
                       .PutU8(static_cast<std::uint8_t>(value >> 16u))
                       .PutU8(static_cast<std::uint8_t>(value >> 8u))
                       .PutU8(static_cast<std::uint8_t>(value)));
}

Outgoing WriteMoneyOut(std::uint32_t amount)
{
    return Xor32C1(Castle(Castle::MoneyOutSubcode).PutU32Le(amount));
}

Outgoing WriteHuntZoneGate(std::uint8_t allowed)
{
    return Xor32C1(Castle(Castle::HuntZoneGateSubcode).PutU8(allowed));
}

Outgoing WriteNpcListRequest(std::uint8_t kind)
{
    return Xor32C1(Network::PacketWriter::C1(CastleNpcListOpcode).PutU8(kind));
}

Outgoing WriteSiegeGuildListRequest()
{
    return ClearC1(Network::PacketWriter::C1(SiegeGuildListOpcode));
}

Outgoing WriteRegisteredGuildListRequest()
{
    return ClearC1(Network::PacketWriter::C1(RegisteredGuildListOpcode));
}

Outgoing WriteHuntZoneEntry(std::uint32_t fee)
{
    return Xor32C1(Network::PacketWriter::C1(CastleInfoOpcode, CastleInfo::HuntZoneEntrySubcode).PutU32Le(fee));
}
} // namespace Client::Protocol::CastleSiege
