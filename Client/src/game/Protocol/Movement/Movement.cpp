#include "Protocol/Movement/Movement.h"

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
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace Client::Protocol::Movement
{
namespace
{
// 04, "Caminar y pegar": eight int32 pairs at 0x6c82a4.
struct Delta
{
    int x;
    int y;
};
constexpr std::array<Delta, 8> Directions{{{-1, -1}, {0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}}};
constexpr std::uint8_t DirectionMask = 7;
constexpr unsigned int DirectionShift = 4;
constexpr std::size_t MaxPackedBytes = 1 + MaxWireWalkCells / 2;
// 0x56b0b7..0x56b0cc: FADD 0x6b87dc, FMUL 0x6b87d8, FADD 0x6b87d4, with no FSTP.
constexpr double AngleHalfStep = 22.5;
constexpr double AngleScale = std::bit_cast<float>(std::uint32_t{0x3cb60b61});
constexpr double AngleBias = 1.0;
constexpr double MinConvertedDirection = -0x1p63;
constexpr double MaxConvertedDirection = 0x1p63;
constexpr std::uint16_t CharacterIdMask = 0x7fff;
constexpr std::uint16_t ShockMask = 0x8000;
constexpr std::uint8_t DamageTypeMask = 0x3f;
constexpr std::uint8_t DamageFlagMask = 0x80;
constexpr std::uint8_t DamageDoubleHitMask = 0x40;
// 0x56ad1d..0x56ad4f: MouseUpdateTimeMax before the wire's limit.
constexpr std::size_t ShortPathCells = 3;
constexpr std::uint32_t ShortPathWait = 5;
constexpr std::uint32_t WaitPerCell = 3;
constexpr std::uint32_t WaitBias = 4;

using WalkId = Network::Field<std::uint16_t, 3, std::endian::big>;
using WalkX = Network::Field<std::uint8_t, 5>;
using WalkY = Network::Field<std::uint8_t, 6>;
using WalkDirection = Network::Field<std::uint8_t, 7>;
constexpr Network::PacketLayout WalkLayout{
    {Network::PacketType::C1, WalkOpcode, std::nullopt}, WalkDirection::End, 0x4c6790};

using DamageId = Network::Field<std::uint16_t, 3, std::endian::big>;
using DamageAmount = Network::Field<std::uint16_t, 5, std::endian::big>;
using DamageType = Network::Field<std::uint8_t, 7>;
constexpr Network::PacketLayout DamageLayout{
    {Network::PacketType::C1, AttackOpcode, std::nullopt}, DamageType::End, 0x4caae0};

using ActionKey = Network::Field<std::uint16_t, 3, std::endian::big>;
using ActionDirection = Network::Field<std::uint8_t, 5>;
using ActionKind = Network::Field<std::uint8_t, 6>;
constexpr Network::PacketLayout ActionLayout{
    {Network::PacketType::C1, ActionOpcode, std::nullopt}, ActionKind::End, 0x4cb750};

std::optional<std::uint8_t> DirectionBetween(Cell from, Cell to)
{
    const int dx = static_cast<int>(to.x) - static_cast<int>(from.x);
    const int dy = static_cast<int>(to.y) - static_cast<int>(from.y);
    for (std::size_t index = 0; index < Directions.size(); ++index)
    {
        if (Directions[index].x == dx && Directions[index].y == dy)
            return static_cast<std::uint8_t>(index);
    }
    return std::nullopt;
}
} // namespace

Core::Expected<std::uint8_t, WalkWriteError> DirectionFromAngle(float angle)
{
    if (!std::isfinite(angle))
        return Core::Unexpected(WalkWriteError::InvalidAngle);
    const double converted = ((static_cast<double>(angle) + AngleHalfStep) * AngleScale) + AngleBias;
    if (converted < MinConvertedDirection || converted >= MaxConvertedDirection)
        return Core::Unexpected(WalkWriteError::InvalidAngle);
    const auto integer = static_cast<std::int64_t>(converted);
    return static_cast<std::uint8_t>(static_cast<std::uint64_t>(integer) & DirectionMask);
}

Core::Expected<Outgoing, WalkWriteError> WriteWalk(std::span<const Cell> path, Cell target, float angle)
{
    if (path.empty() || path.size() > MaxWalkCells)
        return Core::Unexpected(WalkWriteError::InvalidPathSize);
    path = path.first(std::min(path.size(), MaxWireWalkCells));
    std::array<std::byte, MaxPackedBytes> packed{};
    std::uint8_t lastDirection = 0;
    for (std::size_t index = 1; index < path.size(); ++index)
    {
        lastDirection = DirectionBetween(path[index - 1], path[index]).value_or(0);
        const unsigned int shift = index % 2 == 1 ? DirectionShift : 0u;
        packed[(index + 1) / 2] |= static_cast<std::byte>(lastDirection << shift);
    }
    std::uint8_t finalDirection = DirectionBetween(path.back(), target).value_or(lastDirection);
    if (path.size() == 1)
    {
        const auto direction = DirectionFromAngle(angle);
        if (!direction.HasValue())
            return Core::Unexpected(direction.Error());
        finalDirection = direction.Value();
    }
    packed[0] = static_cast<std::byte>((finalDirection << DirectionShift) | static_cast<std::uint8_t>(path.size() - 1));
    const auto writer = Network::PacketWriter::C1(WalkOpcode)
                            .PutU8(path.front().x)
                            .PutU8(path.front().y)
                            .PutBytes(std::span(packed).first(1 + path.size() / 2));
    return Xor32C1(writer);
}

Core::Expected<std::uint32_t, WalkWriteError> WalkUpdateTimeMax(std::size_t pathNum)
{
    if (pathNum == 0 || pathNum > MaxWalkCells)
        return Core::Unexpected(WalkWriteError::InvalidPathSize);
    if (pathNum < ShortPathCells)
        return 0u;
    if (pathNum == ShortPathCells)
        return ShortPathWait;
    return WaitPerCell * static_cast<std::uint32_t>(pathNum) + WaitBias;
}

Core::Expected<Walk, Network::ProtocolError> ReadWalk(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, WalkLayout,
                               [](Network::LayoutReader& reader)
                               {
                                   return Walk{
                                       reader.Read<WalkId>(),
                                       {reader.Read<WalkX>(), reader.Read<WalkY>()},
                                       static_cast<std::uint8_t>(reader.Read<WalkDirection>() >> DirectionShift)};
                               });
}

Core::Expected<Damage, Network::ProtocolError> ReadDamage(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, DamageLayout,
                               [](Network::LayoutReader& reader)
                               {
                                   const std::uint16_t id = reader.Read<DamageId>();
                                   const std::uint8_t type = reader.Read<DamageType>();
                                   return Damage{static_cast<std::uint16_t>(id & CharacterIdMask),
                                                 (id & ShockMask) != 0,
                                                 reader.Read<DamageAmount>(),
                                                 static_cast<std::uint8_t>(type & DamageTypeMask),
                                                 (type & DamageFlagMask) != 0,
                                                 (type & DamageDoubleHitMask) != 0};
                               });
}

Core::Expected<Action, Network::ProtocolError> ReadAction(const Network::Packet& packet)
{
    return Network::ReadPacket(
        packet, ActionLayout, [](Network::LayoutReader& reader)
        { return Action{reader.Read<ActionKey>(), reader.Read<ActionDirection>(), reader.Read<ActionKind>()}; });
}

Outgoing WriteAction(std::uint8_t direction, std::uint8_t action)
{
    return Xor32C1(Network::PacketWriter::C1(ActionOpcode).PutU8(direction).PutU8(action));
}
} // namespace Client::Protocol::Movement
