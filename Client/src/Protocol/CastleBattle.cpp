#include "Protocol/CastleBattle.h"

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/ProtocolError.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace Client::Protocol::CastleBattle
{
namespace
{
// 0x4e0f30: la cuenta en pkt[4] y las ternas desde pkt+5 (ESI = pkt+6 y lee [ESI − 1], [ESI] y [ESI + 1]).
constexpr std::size_t MembersCountAt = 4;
constexpr std::size_t MembersAt = 5;
constexpr std::size_t MemberSize = 3;
// 0x4e07f0: los dwords de pkt+8, pkt+0xc y pkt+0x10.
constexpr std::size_t HuntZoneFeeAt = 8;
constexpr std::size_t HuntZoneAt0c = 0xc;
constexpr std::size_t HuntZoneAt10 = 0x10;

// El byte `at` desde el primero del paquete, si llegó.
std::optional<std::uint8_t> U8(std::span<const std::byte> bytes, std::size_t at)
{
    if (at >= bytes.size())
    {
        return std::nullopt;
    }
    return std::to_integer<std::uint8_t>(bytes[at]);
}

// MOV r32: el byte de abajo primero.
std::optional<std::uint32_t> U32Le(std::span<const std::byte> bytes, std::size_t at)
{
    if (at > bytes.size() || bytes.size() - at < 4)
    {
        return std::nullopt;
    }
    std::uint32_t value = 0;
    for (std::size_t i = 4; i-- > 0;)
    {
        value = (value << 8u) | std::to_integer<std::uint32_t>(bytes[at + i]);
    }
    return value;
}

// La clave de un personaje: el primer byte arriba (SHL EAX, 8 y ADD).
std::optional<std::uint16_t> KeyAt(std::span<const std::byte> bytes, std::size_t at)
{
    const auto high = U8(bytes, at);
    const auto low = U8(bytes, at + 1);
    if (!high || !low)
    {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>((*high << 8u) | *low);
}

Core::Unexpected<ProtocolError> TooShort()
{
    return Core::Unexpected(ProtocolError::TooShort);
}

// B9 01, 0x4e0750.
Core::Expected<CastleBattlePacket, ProtocolError> ReadNpcLevel(std::span<const std::byte> bytes)
{
    const auto key = KeyAt(bytes, 4);
    const auto level = U8(bytes, 6);
    if (!key || !level)
    {
        return TooShort();
    }
    return CastleBattlePacket{NpcLevel{.key = *key, .level = *level}};
}

// B9 02, 0x4e0790.
Core::Expected<CastleBattlePacket, ProtocolError> ReadOwnerMark(std::span<const std::byte> bytes)
{
    constexpr std::size_t MarkAt = 4;
    if (bytes.size() < MarkAt + PackedMarkSize)
    {
        return TooShort();
    }
    OwnerMark mark;
    std::ranges::transform(bytes.subspan(MarkAt, PackedMarkSize), mark.packed.begin(),
                           [](std::byte b) { return std::to_integer<std::uint8_t>(b); });
    return CastleBattlePacket{mark};
}

// B9 03, 0x4e07f0: con pkt[4] en 0 no lee nada más.
Core::Expected<CastleBattlePacket, ProtocolError> ReadHuntZoneInfo(std::span<const std::byte> bytes)
{
    const auto mode = U8(bytes, 4);
    if (!mode)
    {
        return TooShort();
    }
    HuntZoneInfo info{.mode = *mode};
    if (info.mode == 0)
    {
        return CastleBattlePacket{info};
    }
    const auto allowed = U8(bytes, 5);
    const auto fee = U32Le(bytes, HuntZoneFeeAt);
    const auto at0c = U32Le(bytes, HuntZoneAt0c);
    const auto at10 = U32Le(bytes, HuntZoneAt10);
    if (!allowed || !fee || !at0c || !at10)
    {
        return TooShort();
    }
    info.allowed = *allowed;
    info.fee = *fee;
    info.at0c = *at0c;
    info.at10 = *at10;
    return CastleBattlePacket{info};
}

// B9 05, 0x4e0860.
Core::Expected<CastleBattlePacket, ProtocolError> ReadHuntZoneEntryResult(std::span<const std::byte> bytes)
{
    const auto result = U8(bytes, 4);
    if (!result)
    {
        return TooShort();
    }
    return CastleBattlePacket{HuntZoneEntryResult{.result = *result}};
}

Core::Expected<CastleBattlePacket, ProtocolError> ReadBattle(std::span<const std::byte> bytes, std::uint8_t subcode)
{
    switch (subcode)
    {
    case NpcLevelSubcode:
        return ReadNpcLevel(bytes);
    case OwnerMarkSubcode:
        return ReadOwnerMark(bytes);
    case HuntZoneInfoSubcode:
        return ReadHuntZoneInfo(bytes);
    case HuntZoneEntrySubcode:
        return ReadHuntZoneEntryResult(bytes);
    default:
        return Core::Unexpected(ProtocolError::WrongPacket);
    }
}

// BA, 0x4cf1b0: pkt[6] solo con cuál en 0x28 (CMP CL, 0x28 y JNZ antes de leerlo).
Core::Expected<CastleBattlePacket, ProtocolError> ReadActorField(std::span<const std::byte> bytes)
{
    const auto key = KeyAt(bytes, 3);
    const auto kind = U8(bytes, 5);
    if (!key || !kind)
    {
        return TooShort();
    }
    ActorField field{.key = *key, .kind = *kind};
    if (field.kind != ActorField1bcKind)
    {
        return CastleBattlePacket{field};
    }
    const auto value = U8(bytes, 6);
    if (!value)
    {
        return TooShort();
    }
    field.value = *value;
    return CastleBattlePacket{field};
}

// BB, 0x4e0f30: la cuenta sin signo (TEST AL, AL y JBE; el lazo vuelve a leer pkt[4] y compara con JL).
Core::Expected<CastleBattlePacket, ProtocolError> ReadMinimapMembers(std::span<const std::byte> bytes)
{
    const auto count = U8(bytes, MembersCountAt);
    if (!count)
    {
        return TooShort();
    }
    if (bytes.size() < MembersAt + (std::size_t{*count} * MemberSize))
    {
        return TooShort();
    }
    MinimapMembers members;
    members.members.reserve(*count);
    for (std::size_t i = 0; i < *count; ++i)
    {
        const std::size_t at = MembersAt + (i * MemberSize);
        members.members.push_back(MinimapMember{.index = std::to_integer<std::uint8_t>(bytes[at]),
                                                .x = std::to_integer<std::uint8_t>(bytes[at + 1]),
                                                .y = std::to_integer<std::uint8_t>(bytes[at + 2])});
    }
    return CastleBattlePacket{members};
}

// B2 20, 0x4e04e0: los 0x1b bytes desde pkt+0, con la clave de pkt[4] y pkt[5].
Core::Expected<CastleBattlePacket, ProtocolError> ReadSwitchHolder(std::span<const std::byte> bytes)
{
    if (bytes.size() < SwitchHolderSize)
    {
        return TooShort();
    }
    SwitchHolder holder;
    holder.key = *KeyAt(bytes, 4);
    std::ranges::transform(bytes.first(SwitchHolderSize), holder.bytes.begin(),
                           [](std::byte b) { return std::to_integer<std::uint8_t>(b); });
    return CastleBattlePacket{holder};
}
} // namespace

Core::Expected<CastleBattlePacket, ProtocolError> ReadCastleBattlePacket(const Network::Packet& packet)
{
    const auto opcode = packet.Opcode();
    if (!opcode)
    {
        return Core::Unexpected(ProtocolError::WrongPacket);
    }
    const std::span<const std::byte> bytes = packet.Bytes();
    switch (*opcode)
    {
    case BattleOpcode:
    case CastleOpcode:
    {
        const auto subcode = U8(bytes, Network::MinPacketSize(packet.Type()));
        if (!subcode)
        {
            return TooShort();
        }
        if (*opcode == BattleOpcode)
        {
            return ReadBattle(bytes, *subcode);
        }
        if (*subcode != SwitchHolderSubcode)
        {
            return Core::Unexpected(ProtocolError::WrongPacket);
        }
        return ReadSwitchHolder(bytes);
    }
    case ActorFieldOpcode:
        return ReadActorField(bytes);
    case MinimapMembersOpcode:
        return ReadMinimapMembers(bytes);
    default:
        return Core::Unexpected(ProtocolError::WrongPacket);
    }
}
} // namespace Client::Protocol::CastleBattle
