#include "Player/OwnDamage.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Kernel/CharacterClass.h"
#include "Tables/SkillTable.h"
#include "Models/LogicalAccess.h"
#include "Player/ItemSetOptionList.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Player/OwnStatHelpers.h"
#include "Kernel/ItemState.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>

// 0x5b8b80 (1,328 bytes), 0x5513d0 (783) and 0x5516e0 (769), checked against the PE c32bbd5f: 06, "La ventana del
// personaje" and "El cuerpo de la skill".
namespace Client::Player
{
namespace
{
// An item's type as the original reads it: a signed short.
std::int32_t TypeOf(const Kernel::ItemState& item)
{
    return static_cast<std::int16_t>(item.id);
}

bool InRange(std::int32_t type, std::int32_t first, std::int32_t last)
{
    return type >= first && type <= last;
}

// 0x5b8cb2..0x5b8d12: with the right hand in one of those three ranges, the first pair; otherwise the second, with the
// left in its own, the right empty or from 0xa00 to 0xbff.
bool UsesSecondPair(std::int32_t right, std::int32_t left)
{
    if (InRange(right, 0x808, 0x80e) || right == 0x810 || InRange(right, 0x812, 0x9ff))
        return false;
    return InRange(left, 0x800, 0x806) || left == 0x811 || left == 0x814 || left == 0x815 || right == -1 ||
           InRange(right, 0xa00, 0xbff);
}

// An arrow's or a bolt's level, (+4 & 0x78) at 8 or more.
bool HasLevel(const Kernel::ItemState& item)
{
    return (item.scalar1Low & 0x78u) >= 8u;
}

// imul by 0x37 and div by 100, unsigned.
std::uint32_t FiftyFivePercent(std::uint16_t value)
{
    return static_cast<std::uint32_t>(value) * 55u / 100u;
}

// Skill.bmd: an entry's damage byte and its sets byte (0x40d8c0).
constexpr std::size_t DamageByte = 0x21;
constexpr std::size_t SetOptionByte = 0x30;
// 0x40d690 with the magic's selector.
constexpr std::uint32_t MagicSetSelector = 0x9f;

// 0x551496 and 0x551791..0x5517c3: the 16 marks of +0x937c at 0, with four dwords, and the 32-bit p, at 0, with what
// 0x40d8c0 adds for the skill's byte +0x30 and, in its low 16 bits (add word ptr), 0x40d690 with 0x9f.
Core::Expected<std::int32_t, std::string> SetBonusOf(std::span<const std::byte, Tables::SkillRecordSize> record,
                                                     ItemSetOptionList& setOptions)
{
    setOptions.used.fill(0);
    std::int32_t bonus = 0;
    if (auto added = AddSkillSetOptionValues(setOptions, bonus, std::to_integer<std::uint8_t>(record[SetOptionByte]));
        !added.HasValue())
        return Core::Unexpected(added.Error());
    const auto bits = std::bit_cast<std::uint32_t>(bonus);
    auto low = static_cast<std::uint16_t>(bits);
    if (auto added = AddItemSetOptionValues(setOptions, low, MagicSetSelector); !added.HasValue())
        return Core::Unexpected(added.Error());
    return std::bit_cast<std::int32_t>((bits & 0xffff0000u) | low);
}
} // namespace

OwnAttackDamage AttackDamageOf(const Kernel::OwnCharacterRecord& own, Kernel::CharacterClass heroClass,
                               const OwnEquipmentOverride& held, std::uint8_t& ownRecordDepth, Crt::CrtRandom& random)
{
    const Models::LogicalAccess access(ownRecordDepth, random, Models::OwnRecordCloseRandoms);
    // 0x5b8c45..0x5b8cad: the cursor item in place of a hand.
    const Kernel::ItemState* rightItem = &own.equipment[0];
    const Kernel::ItemState* leftItem = &own.equipment[1];
    if (held.dword7879fbc > 0 && held.window78ab9b8Is78a9bb0)
    {
        if (held.slot78a6758 == 0)
            rightItem = &held.item7879f80;
        else if (held.slot78a6758 == 1)
            leftItem = &held.item7879f80;
    }
    const std::int32_t right = TypeOf(*rightItem);
    const std::int32_t left = TypeOf(*leftItem);
    OwnAttackDamage damage;
    if (UsesSecondPair(right, left))
    {
        damage.min = own.word54;
        damage.max = own.word56;
    }
    else
    {
        damage.min = own.word50;
        damage.max = own.word52;
    }
    if (heroClass == Kernel::CharacterClass::DarkKnight || heroClass == Kernel::CharacterClass::MagicGladiator)
    {
        if (InRange(left, 0, 0xbff) && InRange(right, 0, 0xbff))
        {
            damage.highlighted = true;
            damage.min = FiftyFivePercent(own.word50) + FiftyFivePercent(own.word54);
            damage.max = FiftyFivePercent(own.word52) + FiftyFivePercent(own.word56);
        }
    }
    else if (heroClass == Kernel::CharacterClass::Elf && InRange(right, 0x800, 0x9ff) && InRange(left, 0x800, 0x9ff) &&
             ((left == 0x807 && HasLevel(*leftItem)) || (right == 0x80f && HasLevel(*rightItem))))
    {
        damage.highlighted = true;
    }
    constexpr std::uint8_t ExtraDamageBit = 0x02;
    constexpr std::uint32_t ExtraDamage = 15;
    if ((own.speedAdjustBits & ExtraDamageBit) != 0)
    {
        damage.min += ExtraDamage;
        damage.max += ExtraDamage;
    }
    return damage;
}

Core::Expected<OwnMagicDamage, std::string> MagicDamageOf(const Kernel::OwnCharacterRecord& own, std::uint8_t skill,
                                                          const Tables::SkillTable* skills, std::uint8_t& skillDepth,
                                                          Crt::CrtRandom& random, ItemSetOptionList& setOptions)
{
    if (skills == nullptr)
        return Core::Unexpected(std::string("0x5513d0: sin Skill.bmd"));
    const auto record = skills->Record(skill);
    if (!record)
        return Core::Unexpected(std::format("0x5513d0: Skill.bmd no tiene la skill {}", skill));
    const Models::LogicalAccess access(skillDepth, random, static_cast<std::uint16_t>(Tables::SkillRecordSize));
    const auto base = static_cast<std::int32_t>(std::to_integer<std::uint8_t>((*record)[DamageByte]));
    OwnMagicDamage damage{.min = own.word5a + base, .max = (base >> 1) + base + own.word5c};
    const auto bonus = SetBonusOf(*record, setOptions);
    if (!bonus.HasValue())
        return Core::Unexpected(bonus.Error());
    damage.min += bonus.Value();
    damage.max += bonus.Value();
    return damage;
}

Core::Expected<OwnSkillDamage, std::string> SkillDamageOf(std::uint8_t skill, const Tables::SkillTable* skills,
                                                          std::uint8_t& skillDepth, Crt::CrtRandom& random,
                                                          ItemSetOptionList& setOptions)
{
    if (skills == nullptr)
        return Core::Unexpected(std::string("0x5516e0: sin Skill.bmd"));
    const auto record = skills->Record(skill);
    if (!record)
        return Core::Unexpected(std::format("0x5516e0: Skill.bmd no tiene la skill {}", skill));
    const Models::LogicalAccess access(skillDepth, random, static_cast<std::uint16_t>(Tables::SkillRecordSize));
    // 0x551761..0x55178a: s and s + s / 2, with C's division (cdq, sub, sar).
    const auto base = static_cast<std::int32_t>(std::to_integer<std::uint8_t>((*record)[DamageByte]));
    OwnSkillDamage damage{.min = base, .max = (base >> 1) + base};
    const auto bonus = SetBonusOf(*record, setOptions);
    if (!bonus.HasValue())
        return Core::Unexpected(bonus.Error());
    damage.min += bonus.Value();
    damage.max += bonus.Value();
    return damage;
}
} // namespace Client::Player
