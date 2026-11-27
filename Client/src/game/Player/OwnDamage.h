#pragma once

#include "Core/Expected.h"
#include "Kernel/CharacterClass.h"
#include "Tables/SkillTable.h"

#include <cstdint>
#include <string>

namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Kernel
{
struct OwnCharacterRecord;
} // namespace Client::Kernel

// The own character's damages that the character window (0x5ee2e0) and the skill's tooltip (0x5b90b0) show: the
// attack's, 0x5b8b80, the magic's, 0x5513d0, and the skill's, 0x5516e0, with their protection table accesses and their
// rand() calls. Contract: Client/docs/research/06-render-escenas-y-ui.md, "La ventana del personaje" and "El cuerpo de
// la skill".
namespace Client::Player
{
struct ItemSetOptionList;
struct OwnEquipmentOverride;

// What 0x5b8b80 leaves in its two pointers and what it returns.
struct OwnAttackDamage
{
    std::uint32_t min = 0;
    std::uint32_t max = 0;
    // The byte it returns: with 1 the window paints the damage line orange. Both hands with a type from 0 to 0xbff
    // set it in classes 1 and 3, and in class 2, both from 0x800 to 0x9ff, the left's 0x807 or the right's 0x80f
    // with a level (+4 & 0x78 at 8 or more). What it means (both weapons, the arrows or the bolts with a level) is
    // *inferred*.
    bool highlighted = false;
};

// 0x5b8b80(&min, &max), cdecl: opens the own record (without spending) and, on closing it, if it is left at 0, draws
// 3,848 rand() calls. The hands are places 0 (+0xb6c) and 1 (+0xbb4), or the cursor item (0x7879f80) in the place of
// [0x78a6758] (0 or 1) with [0x7879fbc] > 0 and the inventory grid. With the right from 0x808 to 0x80e, 0x810 or from
// 0x812 to 0x9ff, the pair +0x50 and +0x52; otherwise, with the left from 0x800 to 0x806, 0x811, 0x814 or 0x815, the
// right at −1 or from 0xa00 to 0xbff, the pair +0x54 and +0x56; otherwise +0x50 and +0x52. Classes 1 and 3 with both
// hands from 0 to 0xbff add 55 % of each pair (+0x50 and +0x54, +0x52 and +0x56, unsigned and truncating); with bit
// 0x02 of +0x3c, 15 more to both. `heroClass` is the hero's +0x1d4 & 7.
[[nodiscard]] OwnAttackDamage AttackDamageOf(const Kernel::OwnCharacterRecord& own, Kernel::CharacterClass heroClass,
                                             const OwnEquipmentOverride& held, std::uint8_t& ownRecordDepth,
                                             Crt::CrtRandom& random);

// What 0x5513d0 leaves in its two pointers.
struct OwnMagicDamage
{
    std::int32_t min = 0;
    std::int32_t max = 0;
};

// 0x5513d0(skill, &min, &max), thiscall on the own record: opens Skill.bmd's entry `skill` (60 rand() calls on closing
// it at 0, `skillDepth`), and with s, the entry's byte +0x21, min = +0x5a + s and max = +0x5c + s + s / 2. It zeroes
// the 16 marks of [0x6eed3c]'s list (+0x937c), adds to a 32-bit p at 0 the entries of the skill's byte +0x30
// (0x40d8c0) and to its low 16 bits those of selector 0x9f (0x40d690), and adds p to both. Without Skill.bmd or the
// skill, an error before opening; with more than 16 entries in the list, 0x40d8c0's or 0x40d690's.
[[nodiscard]] Core::Expected<OwnMagicDamage, std::string>
MagicDamageOf(const Kernel::OwnCharacterRecord& own, std::uint8_t skill, const Tables::SkillTable* skills,
              std::uint8_t& skillDepth, Crt::CrtRandom& random, ItemSetOptionList& setOptions);

// What 0x5516e0 leaves in its two pointers.
struct OwnSkillDamage
{
    std::int32_t min = 0;
    std::int32_t max = 0;
};

// 0x5516e0(skill, &min, &max), thiscall on the own record, which it does not read: 0x5513d0 without the record's two
// words. It opens Skill.bmd's entry `skill` (60 rand() calls on closing it at 0, `skillDepth`) and, with s, its byte
// +0x21, min = s and max = s + s / 2; it zeroes the 16 marks of +0x937c and adds the same sets' p to both (0x40d8c0 and
// 0x40d690). Without Skill.bmd or the skill, an error before opening.
[[nodiscard]] Core::Expected<OwnSkillDamage, std::string>
SkillDamageOf(std::uint8_t skill, const Tables::SkillTable* skills, std::uint8_t& skillDepth, Crt::CrtRandom& random,
              ItemSetOptionList& setOptions);
} // namespace Client::Player
