#pragma once

#include "Protocol/FrontEnd/CharacterName.h"
#include "Kernel/ItemState.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace Client::Core
{
class Log;
}

namespace Client::Protocol::FrontEnd
{
struct CharacterSelectResult;
}

// The own character's record, [0x7681eb0]: what the F3 03 reply writes into it and the clearing of its equipment. A
// typed projection, not the 0xf08-byte layout; each field says its offset. Contract:
// Client/docs/research/04-protocolo.md, "La entrada al mundo" (the reply's table and Q-C144), and
// 06-render-escenas-y-ui.md, "La lógica" of the selection (the identity) and "La entrada al mundo" (rows 5 and 7 of
// 0x4c3940).
//
// It starts at zero, like the block WinMain fills with zeros (04, 0x4bafbc..0x4bafc2). The original keeps it encrypted
// in the protection table (06, "La tabla de protección"): opening and closing it, with its rand() calls, is the
// caller's job, not this record's.
namespace Client::Kernel
{
// 06, row 7 of 0x4c3940: twelve places from +0xb68, every 0x48, with the type at −1 when cleared.
inline constexpr std::size_t OwnEquipmentSlotCount = 12;
inline constexpr std::uint16_t EmptyOwnItemId = 0xffff;
// +0x26c: the skill keys of each of the selection's five places, every 0x100.
inline constexpr std::size_t OwnSkillKeyPlaces = 5;
inline constexpr std::size_t OwnSkillKeyBytes = 0x100;

struct OwnCharacterRecord
{
    // The F3 03 reply's stats (0x4c3940), per 04's table; the PK and the CtlCode go to the hero, not here. It does not
    // touch the identity or the equipment: 0x512a30 dresses the hero afterwards with the old equipment (06, row 5).
    // Q-C144: the original copies to +0x48 and +0x4a the WORDs of pkt[0x34..0x37], past the packet's 0x34 bytes; MuRet
    // leaves them at 0 and says so in `log`.
    void ApplyStats(const Protocol::FrontEnd::CharacterSelectResult& reply, Core::Log& log);

    // 0x4c3940, row 7 (0x4c47c8..0x4c47da): in the twelve places, the type at −1 and the dword of +4 (scalar1Low) and
    // the byte of +0x1b (flags1b) at 0, because the F3 10 brings the equipment. The rest of each ItemState stays as it
    // was, as in the original.
    void ClearEquipmentForEntry();

    // The F3 10's common initialization (0x4c563c..0x4c565f, 06 "El F3 10, el inventario"), also unencrypted and with
    // count 0: in the twelve places, the type at −1 and the dword of +0x3c (occupied) and the byte of +0x1b (flags1b)
    // at 0. It is not the entry's clearing: this one keeps the dword of +4 (scalar1Low), and neither touches the
    // durability (Q-C203).
    void ClearEquipmentForInventory();

    // 0x633f20: +0x6a and +0x6c..+0xab at 0; only D+4 of the first eight items at 0.
    void ResetForSelection();

    // Skill byte `index` (+0x6c + index), the one the bar, the character window and the skill's tooltip read with the
    // hero's skill place (+0x425). The 256 from +0x6c to +0x16b: words6c's 64 and skillsAc's; one past them, nothing.
    [[nodiscard]] std::optional<std::uint8_t> SkillAt(std::size_t index) const;
    // Writes skill byte `index` (the F3 11, 0x4c5070). One past them, nothing.
    void SetSkillAt(std::size_t index, std::uint8_t skill);

    // 06, "La lógica": the selection copies it (0x6342c0) and the reply does not change it.
    std::array<std::byte, Protocol::FrontEnd::NameSize + 1> name{}; // +0, up to the class
    std::uint8_t characterClass = 0;                                // +0xb
    std::uint8_t skin = 0;                                          // +0xc
    std::uint16_t level = 0;                                        // +0xe

    // 04, the F3 03 reply's table: where each field goes.
    std::uint32_t experience = 0;     // +0x10
    std::uint32_t nextExperience = 0; // +0x14
    std::uint16_t strength = 0;       // +0x18
    std::uint16_t agility = 0;        // +0x1a
    std::uint16_t vitality = 0;       // +0x1c
    std::uint16_t energy = 0;         // +0x1e
    std::uint16_t leadership = 0;     // +0x20, from pkt[0x32], the struct's padding (Q-C144)
    std::uint16_t life = 0;           // +0x22
    std::uint16_t mana = 0;           // +0x24
    std::uint16_t maxLife = 0;        // +0x26
    std::uint16_t maxMana = 0;        // +0x28
    // +0x2a to +0x36: 0x550d10 zeroes them (0x54ef00) and adds the items' share (06, "Los stats del registro propio");
    // what they are, unknown.
    std::uint16_t word2a = 0;
    std::uint16_t word2c = 0;
    std::uint16_t word2e = 0;
    std::uint16_t word30 = 0;
    std::uint16_t word32 = 0;
    std::uint16_t word34 = 0;
    std::uint16_t word36 = 0;
    std::uint16_t bp = 0;    // +0x38
    std::uint16_t maxBp = 0; // +0x3a
    // +0x3c: the bits of the adjustment 0x4bee60 applies to the keepalive's speeds (04, the 0E). 0x54fe10 adds 20 to
    // the speeds with 0x01 or 0x08, and sets or clears 0x04.
    std::uint8_t speedAdjustBits = 0;
    // +0x3e, +0x40 and +0x42: the reply zeroes them; what they are, unknown.
    std::uint16_t word3e = 0;
    std::uint16_t word40 = 0;
    std::uint16_t word42 = 0;
    std::uint16_t addedPoints = 0;    // +0x44
    std::uint16_t maxAddedPoints = 0; // +0x46
    // +0x48 and +0x4a: Q-C144's; what they are, unknown.
    std::uint16_t word48 = 0;
    std::uint16_t word4a = 0;
    // +0x4c to +0x66: 0x550d10's eight helpers write them (06, "Los ocho helpers de los stats propios"). +0x4c and
    // +0x58 are the speeds the keepalive sends (04, the 0E); what the others are, unknown. 0x550d10's tail reads +0x4e,
    // +0x60 and +0x62.
    std::uint16_t attackSpeed = 0; // +0x4c
    std::uint16_t word4e = 0;
    std::uint16_t word50 = 0;
    std::uint16_t word52 = 0;
    std::uint16_t word54 = 0;
    std::uint16_t word56 = 0;
    std::uint16_t magicSpeed = 0; // +0x58
    std::uint16_t word5a = 0;
    std::uint16_t word5c = 0;
    std::uint16_t word60 = 0;
    std::uint16_t word62 = 0;
    std::uint16_t word64 = 0;
    std::uint16_t word66 = 0;
    std::uint16_t levelUpPoints = 0; // +0x68
    // +0x6a: how many skills the first 64 places hold, and +0x6b, how many of them have Skill.bmd's +0x31 at 1 (the
    // ones the bar does not show). The F3 11 counts them (0x4c5070).
    std::uint8_t byte6a = 0;
    std::uint8_t byte6b = 0;
    std::array<std::uint32_t, 16> words6c{};
    // +0xac to +0x16b: skill places 64 to 255. Only the F3 11 writes them (0x4c5070, which zeroes the 256 with the
    // whole list, and 0x78 to 0x7b for the Dark Lord); 0x633f20 does not touch them.
    std::array<std::uint8_t, 0xc0> skillsAc{};
    // +0x26c to +0x76b: the skill keys, 0x100 bytes per selection place (0x6d5edc, 0 to 4). The F3 30 fills the
    // chosen one's (0x4dd632..0x4dd6dc): 0xff in all and, at each record skill's place, its key's number. The HUD reads
    // them (0x5a61f9) and CL9 uses them.
    std::array<std::array<std::uint8_t, OwnSkillKeyBytes>, OwnSkillKeyPlaces> skillKeys{};
    // +0x76c to +0xb67: each skill's delay, by the bar's index (0x5531d0, which draws it over the icon). Nothing writes
    // it until CL9.2: it stays at 0.
    std::array<std::int32_t, 255> skillDelays{};

    std::array<Kernel::ItemState, OwnEquipmentSlotCount> equipment{}; // +0xb68

    // [0x7681eb4] + 0xecc: since the login, the same block as [0x7681eb0] (0x4bafc9).
    std::uint32_t zen = 0;
    // +0xed0: the warehouse's zen, which the 81 writes (0x4e46bd, CL9 milestone 6).
    std::uint32_t warehouseZen = 0;

    // +0xee2 to +0xf03, 0x550d10's tail (0x5512bf..0x5513b1): it reads the first seven and writes the others. The
    // eight helpers before it do not write those seven; who does, unread (08, P-C38). What they are, unknown.
    std::uint16_t wordee2 = 0;
    std::uint16_t wordee4 = 0;
    std::uint16_t wordee6 = 0;
    std::uint16_t wordeea = 0;
    std::uint16_t wordeec = 0;
    std::uint16_t wordef0 = 0;
    std::uint16_t wordef2 = 0;
    // +0xef4: 0x54f9b0 adds 0x40d690 to it without zeroing it first, adds it to +0x5a and +0x5c, and leaves it with
    // 0x40d7f0's.
    std::uint16_t wordef4 = 0;
    std::uint16_t wordef8 = 0;
    std::uint16_t wordefa = 0;
    std::uint16_t wordefc = 0;
    std::uint16_t wordefe = 0;
    std::uint16_t wordf00 = 0;
    std::uint8_t bytef02 = 0;
    std::uint8_t bytef03 = 0;
    // +0xf04: the count 0x518cf0 raises in some creations between the record's opening and closing (the 0xfa
    // earthquake's, 0x51a8f3..0x51a8fd, Q-C426); the previous one stays in the effect's +0x8c. What it counts, unknown.
    std::uint8_t bytef04 = 0;
};

// Skill byte `index` of the own record (+0x6c + index), the one the incoming F3 30 (0x4dd632) and the outgoing one
// (0x61c2e4) read: SkillAt, with 0 past +0x16b. Only the F3 11 writes +0xac to +0x16b (0x4c5070).
[[nodiscard]] std::uint8_t OwnSkillByte(const OwnCharacterRecord& own, std::size_t index);
} // namespace Client::Kernel
