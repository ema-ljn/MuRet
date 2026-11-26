#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/ProtocolError.h"
#include "Protocol/Opcodes.h"
#include "Protocol/Outgoing.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// The skills (04, "Las skills"): what arrives about the skills and the characters' states. The readers are the pure
// part of the handlers of 0x4e2b10; what they do with the world is Scenes' (SkillReplies). WrongPacket with another
// type or opcode, TooShort without a field, and extra bytes are ignored.
namespace Client::Protocol::Skills
{
// 0x4cc530: C1/C3 08 19 [skill] [source H L] [target H L], big-endian. The target's bit 15 says whether the skill
// landed (it goes to the source's +0x38d); the original looks the target up with the low 15 bits and the source with
// all 16.
struct MagicAttack
{
    std::uint8_t skill = 0;
    std::uint16_t source = 0;
    std::uint16_t target = 0; // without bit 15
    bool succeeded = false;
};

// 0x4ce000: C1/C3 09 1E [skill] [key H L] [x] [y] [heading].
struct DurationMagic
{
    std::uint8_t skill = 0;
    std::uint16_t key = 0;
    std::uint8_t x = 0;
    std::uint8_t y = 0;
    std::uint8_t direction = 0;
};

// 0x4cbe90: C1 06 1B [skill] [key H L], the end of a skill with a duration.
struct MagicCancel
{
    std::uint8_t skill = 0;
    std::uint16_t key = 0;
};

// 0x4cbe00: C1 0A 07 [type] [mask, 4 bytes little-endian] [key H L]. The GS 0.97 builds another one, of 8 bytes, and
// does not send it (08, P-C260).
struct ActorState
{
    std::uint8_t type = 0;
    std::uint32_t mask = 0;
    std::uint16_t key = 0;
};

// 0x4dbf50: C1 06 48 [key H L] [type]. The GS 0.97 does not send it (08, P-C262).
struct ActorEffect
{
    std::uint16_t key = 0;
    std::uint8_t kind = 0;
};

// 0x4cc290: C1 08 69 [skill] [source, 2 bytes] [target, 2 bytes], the keys little-endian and unmasked: 0x5110d0 does
// not find a target with bit 15 (07, Q-C1205). The GS 0.97 does not send it (08, P-C262).
struct SkillTarget
{
    std::uint8_t skill = 0;
    std::uint16_t source = 0;
    std::uint16_t target = 0; // whole, with bit 15
};

// 0x4c5070: C1 .. F3 11 [count] [list] and from pkt[6] what the count says: with 0xff, the place to empty; with 0xfe,
// the place and the skill; otherwise, with the list at 2, the place to empty; otherwise `count` entries of three bytes
// (the place, the skill and a third the client does not read). The GS builds the entry from pkt[5]: the repack's
// FoxAnti adds the byte at pkt[5] (08, P-C261).
struct SkillListEntry
{
    std::uint8_t slot = 0;
    std::uint8_t skill = 0;
};
struct SkillList
{
    std::uint8_t count = 0;
    std::uint8_t list = 0;
    // With 0xff, 0xfe or the list at 2: pkt[6] and pkt[7] (this one only with 0xfe). Otherwise, the entries.
    std::uint8_t slot = 0;
    std::uint8_t skill = 0;
    std::vector<SkillListEntry> entries;
};
inline constexpr std::uint8_t SkillListRemoveOne = 0xff;
inline constexpr std::uint8_t SkillListAddOne = 0xfe;
inline constexpr std::uint8_t SkillListClearSlot = 2;

// 0x4d30d0: the GS 1.02i's seven WORDs; pkt[0x12] and pkt[0x14] only if they are whole (Q-C144).
struct LevelUp
{
    std::uint16_t level = 0;
    std::uint16_t levelUpPoints = 0;
    std::uint16_t maxLife = 0;
    std::uint16_t maxMana = 0;
    std::uint16_t maxBp = 0;
    std::uint16_t addedPoints = 0;
    std::uint16_t maxAddedPoints = 0;
    std::uint16_t word48 = 0;
    std::uint16_t word4a = 0;
};

// 0x4e31e1: C1 05 F3 20 [life], the summon's life (0x5767960).
struct SummonLife
{
    std::uint8_t life = 0;
};

[[nodiscard]] Core::Expected<MagicAttack, Network::ProtocolError> ReadMagicAttack(const Network::Packet& packet);
[[nodiscard]] Core::Expected<DurationMagic, Network::ProtocolError> ReadDurationMagic(const Network::Packet& packet);
[[nodiscard]] Core::Expected<MagicCancel, Network::ProtocolError> ReadMagicCancel(const Network::Packet& packet);
[[nodiscard]] Core::Expected<ActorState, Network::ProtocolError> ReadActorState(const Network::Packet& packet);
[[nodiscard]] Core::Expected<ActorEffect, Network::ProtocolError> ReadActorEffect(const Network::Packet& packet);
[[nodiscard]] Core::Expected<SkillTarget, Network::ProtocolError> ReadSkillTarget(const Network::Packet& packet);
[[nodiscard]] Core::Expected<SkillList, Network::ProtocolError> ReadSkillList(const Network::Packet& packet);
[[nodiscard]] Core::Expected<LevelUp, Network::ProtocolError> ReadLevelUp(const Network::Packet& packet);
[[nodiscard]] Core::Expected<SummonLife, Network::ProtocolError> ReadSummonLife(const Network::Packet& packet);

// The report of the ones that did not arrive encrypted: C1 06 F1 03 00 [head] with the XOR32 and as a C3, without
// reading the packet. The head is the opcode's in the 19 (0x4cc562..0x4cce21) and in the 1E (0x4ce032..), and 0xf1 in
// the 73 (0x4d5d8c).
[[nodiscard]] Outgoing WriteClearSkillReport(Network::Opcode head);

inline constexpr Network::Opcode Report73Head = JoinOpcode;

// What the hero's skills send (04, "Las skills que salen"). 0x57cfc0, the per-class functions and 0x55cbc0 inline
// build them, with the XOR32 from pkt[3] and encrypted as C3s: the sender draws the padding's rand() and the serial
// (0x5767913) before send(). The Wizard's branch (0x58c1b6 and the others) and 0x55cbc0 also send the 1E
// (DurationMagicSend, below): [towards] at 0, [back] at 0 except with 0x4c (the cell of 0x4e9920, like the knight) and
// the magic's key (0x77f3bad) in 8, 9 and 0x4c (08, P-C221).
// The key sent without a target (0xffff at 0x58c8f0, 0x591865 and 0x55e683).
inline constexpr std::uint16_t NoSkillTargetKey = 0xffff;

// C1 06 1C 00 [x] [y] (0x593151..0x59350e): Teleport (skill 6) to a cell. The GS 0.97 (CGTeleportMagicRecv, 0x426450)
// takes the 0 as skill 6.
[[nodiscard]] Outgoing WriteTeleportMagic(std::uint8_t x, std::uint8_t y);

// C1 07 B0 [key L H] [x] [y] (0x59293b..0x592d1b): Teleport Ally (0xf) of the chosen one to a cell, with the key
// little-endian (the WORD of +0x1f4 as is), as the GS 0.97 reads it (CGTargetTeleportRecv, 0x426870).
[[nodiscard]] Outgoing WriteTargetTeleport(std::uint16_t key, std::uint8_t x, std::uint8_t y);

// The 19 the client sends (0x55b6f0, 0x559890, 0x55cbc0, 0x55faa0 and the branches of 0x57cfc0): C1 06 19 [skill]
// [key H] [key L], with the XOR32 over bytes 3 to 5 and as a C3 (its sender draws its two rand(), the padding and the
// serial). The key is the target's whole +0x1f4. It is the GameServer's PMSG_MAGICATTACK (CGMagicAttack).
[[nodiscard]] Outgoing WriteMagicAttack(std::uint8_t skill, std::uint16_t key);

// The 10 of 0x517a20 (a skill's targets around a point; the skill arrows' hit calls it, 0x520497): C1 [8 + 3n] 10
// [skill] [x] [y] [volley] [n] and, per target, [key H] [key L] [extra], with the XOR32 from pkt[3] and as a C3 like
// the 19. FoxAnti sends it as 1D (03, "La capa de FoxAnti"). The GameServer 0.97 (CGBeattackRecv, 0x426db0) reads the
// header the same (PMSG_BEATTACK_COUNT: the skill, x, y, the serial and the count) but two-byte entries: from the
// second target on it reads another key (07, Q-C1219).
inline constexpr std::size_t MaxSkillHits = 5;
struct SkillHitsSend
{
    std::uint8_t skill = 0;
    // ftol(position · 0.01) & 0xff, the point's cell.
    std::uint8_t x = 0;
    std::uint8_t y = 0;
    // The arrow's +0x8c: the count +0xf04 of its volley's own record.
    std::uint8_t volley = 0;
    // The targets' keys (+0x1f4), `hits` of them (up to MaxSkillHits).
    std::array<std::uint16_t, MaxSkillHits> keys{};
    std::uint8_t hits = 0;
    // The sixth argument of 0x517a20, after each key (0 from the arrow).
    std::uint8_t extra = 0;
};
[[nodiscard]] Outgoing WriteSkillHits(const SkillHitsSend& send);

// The 1E the client sends (the elf, 0x561f60 at 0x562b36 and 0x563f5a, the knight's branches, 0x582439..0x5896a3, and
// the 0x43 of the tail of 0x57cfc0, 0x594137..0x594de1):
// C1 0C 1E [skill] [X] [Y] [heading] [towards] [back] [key H] [key L] [magic's key], with the XOR32 over bytes 3 to 11
// and as a C3 like the 19. The GameServer reads only the first 7 (PMSG_DURATION_MAGIC_RECV, CGDurationMagicRecv
// 0x427870).
struct DurationMagicSend
{
    std::uint8_t skill = 0;
    std::uint8_t x = 0;
    std::uint8_t y = 0;
    std::uint8_t direction = 0;
    // ((dx + 8) << 4) | ((dy − 8) & 0xf), with the distance from the logical cell to the target between −8 and 7 (in
    // the tail's 0x43, 0x5ae300).
    std::uint8_t towards = 0;
    // The heading turned around (+180°) on the same 0 to 255 scale; the knight's 0x2a, 0x3e, 0x41 and 0x4c and the
    // tail's 0x43 put the cell of 0x4e9920.
    std::uint8_t back = 0;
    std::uint16_t key = 0;
    // The magic's key (0x57cf90, +0x1bd) of the knight's 0x4c; 0 in the others (the tail's 0x43 asks 0x57cf90 for it
    // with a null pointer: 0).
    std::uint8_t magicKey = 0;
};
[[nodiscard]] Outgoing WriteDurationMagic(const DurationMagicSend& send);

} // namespace Client::Protocol::Skills
