#include "Protocol/Skills/Skill.h"

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
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace Client::Protocol::Skills
{
namespace
{
constexpr std::uint16_t SucceededBit = 0x8000;
constexpr std::uint16_t TargetMask = 0x7fff;

// A C1 (or a decrypted C3, which arrives as a C1) of `opcode` that the reader at `address` reads up to `minimumSize`.
constexpr Network::PacketLayout C1Layout(Network::Opcode opcode, std::size_t minimumSize, std::uint32_t address)
{
    return {{Network::PacketType::C1, opcode, std::nullopt}, minimumSize, address};
}

using AttackSkill = Network::Field<std::uint8_t, 3>;
using AttackSource = Network::Field<std::uint16_t, 4, std::endian::big>;
using AttackTarget = Network::Field<std::uint16_t, 6, std::endian::big>;
constexpr Network::PacketLayout MagicAttackLayout = C1Layout(MagicAttackOpcode, AttackTarget::End, 0x4cc530);

using DurationSkill = Network::Field<std::uint8_t, 3>;
using DurationKey = Network::Field<std::uint16_t, 4, std::endian::big>;
using DurationX = Network::Field<std::uint8_t, 6>;
using DurationY = Network::Field<std::uint8_t, 7>;
using DurationDirection = Network::Field<std::uint8_t, 8>;
constexpr Network::PacketLayout DurationMagicLayout = C1Layout(DurationMagicOpcode, DurationDirection::End, 0x4ce000);

using CancelSkill = Network::Field<std::uint8_t, 3>;
using CancelKey = Network::Field<std::uint16_t, 4, std::endian::big>;
constexpr Network::PacketLayout MagicCancelLayout = C1Layout(MagicCancelOpcode, CancelKey::End, 0x4cbe90);

using StateType = Network::Field<std::uint8_t, 3>;
// 0x4cbe22: MOV ESI,[pkt+4].
using StateMask = Network::Field<std::uint32_t, 4>;
using StateKey = Network::Field<std::uint16_t, 8, std::endian::big>;
constexpr Network::PacketLayout ActorStateLayout = C1Layout(ActorStateOpcode, StateKey::End, 0x4cbe00);

// 0x4dbf59..0x4dbf61: AH = pkt[3] and AL = pkt[4].
using EffectKey = Network::Field<std::uint16_t, 3, std::endian::big>;
using EffectKind = Network::Field<std::uint8_t, 5>;
constexpr Network::PacketLayout ActorEffectLayout = C1Layout(ActorEffectOpcode, EffectKind::End, 0x4dbf50);

using TargetSkill = Network::Field<std::uint8_t, 3>;
// 0x4cc29d and 0x4cc2a3: MOV DI,[pkt+6] and MOV AX,[pkt+4].
using TargetSource = Network::Field<std::uint16_t, 4>;
using TargetKey = Network::Field<std::uint16_t, 6>;
constexpr Network::PacketLayout SkillTargetLayout = C1Layout(SkillTargetOpcode, TargetKey::End, 0x4cc290);

using ListCount = Network::Field<std::uint8_t, 4>;
using ListKind = Network::Field<std::uint8_t, 5>;
using ListSlot = Network::Field<std::uint8_t, 6>;
using ListSkill = Network::Field<std::uint8_t, 7>;
// 0x4c537e..0x4c5390: three bytes apart, but it reads only the place and the skill.
constexpr std::size_t ListEntrySize = 3;
constexpr std::size_t ListEntryRead = 2;
constexpr Network::PacketLayout SkillListLayout{
    {Network::PacketType::C1, CharacterOpcode, Character::SkillListSubcode}, ListKind::End, 0x4c5070};

using Level = Network::Field<std::uint16_t, 4>;
using LevelUpPoints = Network::Field<std::uint16_t, 6>;
using MaxLife = Network::Field<std::uint16_t, 8>;
using MaxMana = Network::Field<std::uint16_t, 0xa>;
using MaxBp = Network::Field<std::uint16_t, 0xc>;
using AddedPoints = Network::Field<std::uint16_t, 0xe>;
using MaxAddedPoints = Network::Field<std::uint16_t, 0x10>;
// Q-C1369: the 1.02i sender leaves these two words out.
using Word48 = Network::Field<std::uint16_t, 0x12>;
using Word4a = Network::Field<std::uint16_t, 0x14>;
constexpr Network::PacketLayout LevelUpLayout{
    {Network::PacketType::C1, CharacterOpcode, Character::LevelUpSubcode}, MaxAddedPoints::End, 0x4d30d0};

using Life = Network::Field<std::uint8_t, 4>;
constexpr Network::PacketLayout SummonLifeLayout{
    {Network::PacketType::C1, CharacterOpcode, Character::SummonLifeSubcode}, Life::End, 0x4e31e1};

template <typename F> std::uint16_t WordIfPresent(Network::LayoutReader& reader)
{
    return reader.Size() >= F::End ? reader.Read<F>() : std::uint16_t{0};
}

SkillListEntry ReadListEntry(std::span<const std::byte, ListEntryRead> bytes)
{
    return {std::to_integer<std::uint8_t>(bytes[0]), std::to_integer<std::uint8_t>(bytes[1])};
}
} // namespace

Core::Expected<MagicAttack, Network::ProtocolError> ReadMagicAttack(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, MagicAttackLayout,
                               [](Network::LayoutReader& reader)
                               {
                                   // 0x4cce2f..0x4cce55: SAR 0xf of the whole target and AND 0x7fff.
                                   const std::uint16_t target = reader.Read<AttackTarget>();
                                   return MagicAttack{reader.Read<AttackSkill>(), reader.Read<AttackSource>(),
                                                      static_cast<std::uint16_t>(target & TargetMask),
                                                      (target & SucceededBit) != 0};
                               });
}

Core::Expected<DurationMagic, Network::ProtocolError> ReadDurationMagic(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, DurationMagicLayout,
                               [](Network::LayoutReader& reader)
                               {
                                   return DurationMagic{reader.Read<DurationSkill>(), reader.Read<DurationKey>(),
                                                        reader.Read<DurationX>(), reader.Read<DurationY>(),
                                                        reader.Read<DurationDirection>()};
                               });
}

Core::Expected<MagicCancel, Network::ProtocolError> ReadMagicCancel(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, MagicCancelLayout, [](Network::LayoutReader& reader)
                               { return MagicCancel{reader.Read<CancelSkill>(), reader.Read<CancelKey>()}; });
}

Core::Expected<ActorState, Network::ProtocolError> ReadActorState(const Network::Packet& packet)
{
    return Network::ReadPacket(
        packet, ActorStateLayout, [](Network::LayoutReader& reader)
        { return ActorState{reader.Read<StateType>(), reader.Read<StateMask>(), reader.Read<StateKey>()}; });
}

Core::Expected<ActorEffect, Network::ProtocolError> ReadActorEffect(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, ActorEffectLayout, [](Network::LayoutReader& reader)
                               { return ActorEffect{reader.Read<EffectKey>(), reader.Read<EffectKind>()}; });
}

Core::Expected<SkillTarget, Network::ProtocolError> ReadSkillTarget(const Network::Packet& packet)
{
    return Network::ReadPacket(
        packet, SkillTargetLayout, [](Network::LayoutReader& reader)
        { return SkillTarget{reader.Read<TargetSkill>(), reader.Read<TargetSource>(), reader.Read<TargetKey>()}; });
}

Core::Expected<SkillList, Network::ProtocolError> ReadSkillList(const Network::Packet& packet)
{
    return Network::ReadPacket(
        packet, SkillListLayout,
        [](Network::LayoutReader& reader)
        {
            SkillList read{.count = reader.Read<ListCount>(),
                           .list = reader.Read<ListKind>(),
                           .slot = 0,
                           .skill = 0,
                           .entries = {}};
            // 0x4c507c, 0x4c5145 and 0x4c522e: which of the four forms.
            if (read.count == SkillListRemoveOne || read.count == SkillListAddOne || read.list == SkillListClearSlot)
            {
                // 0x4c52ab..0x4c52ad: list 2 empty does not touch pkt[6]; FE and FF take precedence by the count.
                if (read.count == 0)
                {
                    return read;
                }
                read.slot = reader.Read<ListSlot>();
                if (read.count == SkillListAddOne)
                {
                    read.skill = reader.Read<ListSkill>();
                }
                return read;
            }
            read.entries =
                Network::ReadList<ListEntrySize, ListEntryRead>(reader, ListSlot::Offset, read.count, ReadListEntry);
            return read;
        });
}

Core::Expected<LevelUp, Network::ProtocolError> ReadLevelUp(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, LevelUpLayout,
                               [](Network::LayoutReader& reader)
                               {
                                   return LevelUp{.level = reader.Read<Level>(),
                                                  .levelUpPoints = reader.Read<LevelUpPoints>(),
                                                  .maxLife = reader.Read<MaxLife>(),
                                                  .maxMana = reader.Read<MaxMana>(),
                                                  .maxBp = reader.Read<MaxBp>(),
                                                  .addedPoints = reader.Read<AddedPoints>(),
                                                  .maxAddedPoints = reader.Read<MaxAddedPoints>(),
                                                  .word48 = WordIfPresent<Word48>(reader),
                                                  .word4a = WordIfPresent<Word4a>(reader)};
                               });
}

Core::Expected<SummonLife, Network::ProtocolError> ReadSummonLife(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, SummonLifeLayout,
                               [](Network::LayoutReader& reader) { return SummonLife{reader.Read<Life>()}; });
}

Outgoing WriteClearSkillReport(Network::Opcode head)
{
    return EncryptedC1(
        Network::PacketWriter::C1(JoinOpcode, Join::DecryptErrorSubcode).PutU8(0).PutU8(Network::ByteOf(head)));
}

Outgoing WriteMagicAttack(std::uint8_t skill, std::uint16_t key)
{
    return EncryptedC1(Network::PacketWriter::C1(MagicAttackOpcode)
                           .PutU8(skill)
                           .PutU8(static_cast<std::uint8_t>(key >> 8))
                           .PutU8(static_cast<std::uint8_t>(key & 0xffu)));
}

Outgoing WriteSkillHits(const SkillHitsSend& send)
{
    auto writer = Network::PacketWriter::C1(SkillHitsOpcode);
    const std::size_t hits = std::min<std::size_t>(send.hits, MaxSkillHits);
    writer.PutU8(send.skill).PutU8(send.x).PutU8(send.y).PutU8(send.volley).PutU8(static_cast<std::uint8_t>(hits));
    for (std::size_t hit = 0; hit < hits; ++hit)
    {
        const std::uint16_t key = send.keys[hit];
        writer.PutU8(static_cast<std::uint8_t>(key >> 8))
            .PutU8(static_cast<std::uint8_t>(key & 0xffu))
            .PutU8(send.extra);
    }
    return EncryptedC1(writer);
}

Outgoing WriteTeleportMagic(std::uint8_t x, std::uint8_t y)
{
    return EncryptedC1(Network::PacketWriter::C1(TeleportOpcode).PutU8(0).PutU8(x).PutU8(y));
}

Outgoing WriteTargetTeleport(std::uint16_t key, std::uint8_t x, std::uint8_t y)
{
    return EncryptedC1(Network::PacketWriter::C1(TargetTeleportOpcode).PutU16Le(key).PutU8(x).PutU8(y));
}

Outgoing WriteDurationMagic(const DurationMagicSend& send)
{
    return EncryptedC1(Network::PacketWriter::C1(DurationMagicOpcode)
                           .PutU8(send.skill)
                           .PutU8(send.x)
                           .PutU8(send.y)
                           .PutU8(send.direction)
                           .PutU8(send.towards)
                           .PutU8(send.back)
                           .PutU8(static_cast<std::uint8_t>(send.key >> 8))
                           .PutU8(static_cast<std::uint8_t>(send.key & 0xffu))
                           .PutU8(send.magicKey));
}
} // namespace Client::Protocol::Skills
