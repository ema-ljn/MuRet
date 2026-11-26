#include "Protocol/CastleSiege/CastleBattle.h"

#include "Core/Expected.h"
#include "Network/Field.h"
#include "Network/LayoutReader.h"
#include "Network/Packet.h"
#include "Network/PacketCode.h"
#include "Network/PacketLayout.h"
#include "Network/ProtocolError.h"
#include "Network/ReadList.h"
#include "Protocol/Opcodes.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace Client::Protocol::CastleSiege
{
namespace
{
using Result = Core::Expected<CastleBattlePacket, Network::ProtocolError>;

template <std::size_t At> using Byte = Network::Field<std::uint8_t, At>;
// MOV r32: little-endian.
template <std::size_t At> using Dword = Network::Field<std::uint32_t, At>;
// A character's key: the first byte on top (SHL EAX, 8 and ADD).
template <std::size_t At> using Key = Network::Field<std::uint16_t, At, std::endian::big>;
template <std::size_t Size, std::size_t At> using Raw = Network::Field<std::array<std::byte, Size>, At>;

using Value = Byte<4>;

// 0x4e0f30: the count at pkt[4] and the triples from pkt+5 (ESI = pkt+6, and it reads [ESI − 1], [ESI] and
// [ESI + 1]).
using MembersCount = Byte<4>;
constexpr std::size_t MembersAt = 5;
constexpr std::size_t MemberSize = 3;

// The handlers read at fixed offsets whatever the type: in a C2 the subcode and pkt[4] are the same byte.
Network::PacketLayout LayoutOf(const Network::Packet& packet, Network::Opcode opcode,
                               std::optional<Network::Subcode> subcode, std::size_t minimumSize, std::uint32_t address)
{
    return {{packet.Type(), opcode, subcode}, minimumSize, address};
}

template <typename ReadFields>
Result ReadAs(const Network::Packet& packet, const Network::PacketLayout& layout, ReadFields read)
{
    return Network::ReadPacket(packet, layout,
                               [&read](Network::LayoutReader& reader) { return CastleBattlePacket{read(reader)}; });
}

template <std::size_t Size> std::array<std::uint8_t, Size> Unsigned(const std::array<std::byte, Size>& bytes)
{
    std::array<std::uint8_t, Size> values{};
    std::ranges::transform(bytes, values.begin(), [](std::byte b) { return std::to_integer<std::uint8_t>(b); });
    return values;
}

// B9 by its subcode, the table 0x4e61f8.
Result ReadBattle(const Network::Packet& packet, Network::Subcode subcode)
{
    switch (Network::ByteOf(subcode))
    {
    case Network::ByteOf(CastleInfo::NpcLevelSubcode):
        return ReadAs(packet, LayoutOf(packet, CastleInfoOpcode, subcode, Byte<6>::End, 0x4e0750),
                      [](Network::LayoutReader& reader)
                      { return NpcLevel{.key = reader.Read<Key<4>>(), .level = reader.Read<Byte<6>>()}; });
    case Network::ByteOf(CastleInfo::OwnerMarkSubcode):
    {
        using Mark = Raw<PackedMarkSize, 4>;
        return ReadAs(packet, LayoutOf(packet, CastleInfoOpcode, subcode, Mark::End, 0x4e0790),
                      [](Network::LayoutReader& reader) { return OwnerMark{.packed = Unsigned(reader.Read<Mark>())}; });
    }
    case Network::ByteOf(CastleInfo::HuntZoneInfoSubcode):
        // 0x4e07f0: with pkt[4] at 0 it reads nothing else.
        return ReadAs(packet, LayoutOf(packet, CastleInfoOpcode, subcode, Value::End, 0x4e07f0),
                      [](Network::LayoutReader& reader)
                      {
                          HuntZoneInfo info{.mode = reader.Read<Value>()};
                          if (info.mode != 0)
                          {
                              info.allowed = reader.Read<Byte<5>>();
                              info.fee = reader.Read<Dword<8>>();
                              info.at0c = reader.Read<Dword<0xc>>();
                              info.at10 = reader.Read<Dword<0x10>>();
                          }
                          return info;
                      });
    case Network::ByteOf(CastleInfo::HuntZoneEntrySubcode):
        return ReadAs(packet, LayoutOf(packet, CastleInfoOpcode, subcode, Value::End, 0x4e0860),
                      [](Network::LayoutReader& reader)
                      { return HuntZoneEntryResult{.result = reader.Read<Value>()}; });
    default:
        return Core::Unexpected(Network::ProtocolError::WrongPacket);
    }
}

// BA, 0x4cf1b0: pkt[6] only with the kind at 0x28 (CMP CL, 0x28 and JNZ before reading it).
Result ReadActorField(const Network::Packet& packet)
{
    return ReadAs(packet, LayoutOf(packet, ActorFieldOpcode, std::nullopt, Byte<5>::End, 0x4cf1b0),
                  [](Network::LayoutReader& reader)
                  {
                      ActorField field{.key = reader.Read<Key<3>>(), .kind = reader.Read<Byte<5>>()};
                      if (field.kind == ActorField1bcKind)
                      {
                          field.value = reader.Read<Byte<6>>();
                      }
                      return field;
                  });
}

MinimapMember MemberOf(std::span<const std::byte, MemberSize> bytes)
{
    return {.index = std::to_integer<std::uint8_t>(bytes[0]),
            .x = std::to_integer<std::uint8_t>(bytes[1]),
            .y = std::to_integer<std::uint8_t>(bytes[2])};
}

// BB, 0x4e0f30: the count is unsigned (TEST AL, AL and JBE; the loop reads pkt[4] again and compares with JL).
Result ReadMinimapMembers(const Network::Packet& packet)
{
    return ReadAs(packet, LayoutOf(packet, SiegeMinimapMembersOpcode, std::nullopt, MembersCount::End, 0x4e0f30),
                  [](Network::LayoutReader& reader)
                  {
                      return MinimapMembers{.members = Network::ReadList<MemberSize>(
                                                reader, MembersAt, reader.Read<MembersCount>(), MemberOf)};
                  });
}

// B2 20, 0x4e04e0: the 0x1b bytes from pkt+0, with the key of pkt[4] and pkt[5].
Result ReadSwitchHolder(const Network::Packet& packet, Network::Subcode subcode)
{
    using Whole = Raw<SwitchHolderSize, 0>;
    return ReadAs(packet, LayoutOf(packet, CastleOpcode, subcode, Whole::End, 0x4e04e0),
                  [](Network::LayoutReader& reader)
                  { return SwitchHolder{.key = reader.Read<Key<4>>(), .bytes = Unsigned(reader.Read<Whole>())}; });
}

// 0x4e55c5: the key only with 1.
GateOpen ReadGateOpen(Network::LayoutReader& reader)
{
    GateOpen gate{.result = reader.Read<Value>()};
    if (gate.result == 1)
    {
        gate.key = reader.Read<Key<5>>();
    }
    return gate;
}

// 0x4e55fd.
GateCommand ReadGateCommand(Network::LayoutReader& reader)
{
    GateCommand gate{.result = reader.Read<Value>()};
    if (gate.result == 1)
    {
        gate.open = reader.Read<Byte<5>>();
        gate.key = reader.Read<Key<6>>();
    }
    return gate;
}

// 0x4e00c0: the time only with 0 or 2.
CrownRegistration ReadCrownRegistration(Network::LayoutReader& reader)
{
    CrownRegistration crown{.result = reader.Read<Value>()};
    if (crown.result == 0 || crown.result == 2)
    {
        crown.elapsed = reader.Read<Dword<8>>();
    }
    return crown;
}

// 0x4e0650: the guild only with 0 or 1.
CastleOwner ReadCastleOwner(Network::LayoutReader& reader)
{
    CastleOwner owner{.result = reader.Read<Value>()};
    if (owner.result <= 1)
    {
        owner.guild = Unsigned(reader.Read<Raw<8, 5>>());
    }
    return owner;
}

// The battle's cases of the B2 table 0x4e6140; each reads pkt[4] first.
Result ReadCastle(const Network::Packet& packet, Network::Subcode subcode)
{
    if (subcode == Castle::SwitchHolderSubcode)
    {
        return ReadSwitchHolder(packet, subcode);
    }
    const auto castle = [&packet, subcode](std::uint32_t address, auto read)
    { return ReadAs(packet, LayoutOf(packet, CastleOpcode, subcode, Value::End, address), read); };
    switch (Network::ByteOf(subcode))
    {
    case 0x11:
        return castle(0x4e55c5, ReadGateOpen);
    case 0x12:
        return castle(0x4e55fd, ReadGateCommand);
    case 0x13:
        return castle(0x4e5629, [](Network::LayoutReader& reader)
                      { return GateState{.open = reader.Read<Value>(), .key = reader.Read<Key<5>>()}; });
    case 0x14:
        return castle(0x4dff10,
                      [](Network::LayoutReader& reader)
                      {
                          return CrownAccess{.firstKey = reader.Read<Key<4>>(),
                                             .secondKey = reader.Read<Key<6>>(),
                                             .status = reader.Read<Byte<8>>()};
                      });
    case 0x15:
        return castle(0x4e00c0, ReadCrownRegistration);
    case 0x16:
        return castle(0x4e0240,
                      [](Network::LayoutReader& reader) { return CrownState{.result = reader.Read<Value>()}; });
    case 0x17:
        return castle(0x4e56e0,
                      [](Network::LayoutReader& reader) { return SiegeActivation{.active = reader.Read<Value>()}; });
    case 0x18:
        return castle(0x4e0650, ReadCastleOwner);
    case 0x19:
        return castle(0x4e0340, [](Network::LayoutReader& reader) { return SiegeSide{.side = reader.Read<Value>()}; });
    case 0x1d:
        return castle(0x4e5742, [](Network::LayoutReader& reader)
                      { return PanelStatus{.bytes = Unsigned(reader.Read<Raw<4, 4>>())}; });
    case 0x1e:
        return castle(0x4e5776, [](Network::LayoutReader& reader)
                      { return SiegeTime{.hours = reader.Read<Value>(), .minutes = reader.Read<Byte<5>>()}; });
    default:
        return Core::Unexpected(Network::ProtocolError::WrongPacket);
    }
}

// 0x4e08a0: the mode and the key only with 1.
WeaponOpen ReadWeaponOpen(Network::LayoutReader& reader)
{
    WeaponOpen open{.result = reader.Read<Value>()};
    if (open.result == 1)
    {
        open.mode = reader.Read<Byte<5>>();
        open.key = reader.Read<Key<6>>();
    }
    return open;
}

// 0x4e08f0 → 0x44eea0(key, mode (pkt[4]), kind (pkt[7]), x, y): the rest only with a mode other than 0.
WeaponFire ReadWeaponFire(Network::LayoutReader& reader)
{
    WeaponFire fire{.mode = reader.Read<Value>()};
    if (fire.mode != 0)
    {
        fire.key = reader.Read<Key<5>>();
        fire.kind = reader.Read<Byte<7>>();
        fire.x = reader.Read<Byte<8>>();
        fire.y = reader.Read<Byte<9>>();
    }
    return fire;
}

// B7 00, 01 and 02.
Result ReadWeapon(const Network::Packet& packet, Network::Subcode subcode)
{
    switch (Network::ByteOf(subcode))
    {
    case 0:
        return ReadAs(packet, LayoutOf(packet, SiegeWeaponOpcode, subcode, Value::End, 0x4e08a0), ReadWeaponOpen);
    case 1:
        return ReadAs(packet, LayoutOf(packet, SiegeWeaponOpcode, subcode, Value::End, 0x4e08f0), ReadWeaponFire);
    case 2:
        return ReadAs(packet, LayoutOf(packet, SiegeWeaponOpcode, subcode, Byte<6>::End, 0x44efc0),
                      [](Network::LayoutReader& reader) {
                          return WeaponImpact{
                              .kind = reader.Read<Value>(), .x = reader.Read<Byte<5>>(), .y = reader.Read<Byte<6>>()};
                      });
    default:
        return Core::Unexpected(Network::ProtocolError::WrongPacket);
    }
}

// The byte after the opcode, as the Dispatcher reads it.
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
} // namespace

Core::Expected<CastleBattlePacket, Network::ProtocolError> ReadCastleBattlePacket(const Network::Packet& packet)
{
    const auto opcode = packet.Opcode();
    if (!opcode)
    {
        return Core::Unexpected(Network::ProtocolError::WrongPacket);
    }
    switch (Network::ByteOf(*opcode))
    {
    case Network::ByteOf(CastleInfoOpcode):
    case Network::ByteOf(CastleOpcode):
    case Network::ByteOf(SiegeWeaponOpcode):
    {
        const auto subcode = SubcodeOf(packet);
        if (!subcode)
        {
            return Core::Unexpected(Network::ProtocolError::TooShort);
        }
        if (*opcode == CastleInfoOpcode)
        {
            return ReadBattle(packet, *subcode);
        }
        if (*opcode == SiegeWeaponOpcode)
        {
            return ReadWeapon(packet, *subcode);
        }
        return ReadCastle(packet, *subcode);
    }
    case Network::ByteOf(ActorFieldOpcode):
        return ReadActorField(packet);
    case Network::ByteOf(SiegeMinimapMembersOpcode):
        return ReadMinimapMembers(packet);
    default:
        return Core::Unexpected(Network::ProtocolError::WrongPacket);
    }
}
} // namespace Client::Protocol::CastleSiege
