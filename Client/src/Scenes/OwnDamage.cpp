#include "Scenes/OwnDamage.h"

#include "Core/Expected.h"
#include "Data/Tables/SkillTable.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ItemSetOptionList.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnStatHelpers.h"
#include "World/ItemState.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>

// 0x5b8b80 (1.328 bytes), 0x5513d0 (783) y 0x5516e0 (769), cotejados con el PE c32bbd5f: 06, "La ventana del
// personaje" y "El cuerpo de la skill".
namespace Client::Scenes
{
namespace
{
// El tipo de un ítem como lo lee el original: un short con signo.
std::int32_t TypeOf(const World::ItemState& item)
{
    return static_cast<std::int16_t>(item.id);
}

bool InRange(std::int32_t type, std::int32_t first, std::int32_t last)
{
    return type >= first && type <= last;
}

// 0x5b8cb2..0x5b8d12: con la derecha de una de esas tres franjas, el primer par; si no, el segundo con la izquierda
// de las suyas, la derecha vacía o de 0xa00 a 0xbff.
bool UsesSecondPair(std::int32_t right, std::int32_t left)
{
    if (InRange(right, 0x808, 0x80e) || right == 0x810 || InRange(right, 0x812, 0x9ff))
        return false;
    return InRange(left, 0x800, 0x806) || left == 0x811 || left == 0x814 || left == 0x815 || right == -1 ||
           InRange(right, 0xa00, 0xbff);
}

// El nivel de una flecha o de un virote, (+4 & 0x78) en 8 o más.
bool HasLevel(const World::ItemState& item)
{
    return (item.scalar1Low & 0x78u) >= 8u;
}

// imul por 0x37 y div por 100, sin signo.
std::uint32_t FiftyFivePercent(std::uint16_t value)
{
    return static_cast<std::uint32_t>(value) * 55u / 100u;
}

// Skill.bmd: el byte del daño y el de los conjuntos (0x40d8c0) de una entrada.
constexpr std::size_t DamageByte = 0x21;
constexpr std::size_t SetOptionByte = 0x30;
// 0x40d690 con el selector de la magia.
constexpr std::uint32_t MagicSetSelector = 0x9f;

// 0x551496 y 0x551791..0x5517c3: las 16 marcas de +0x937c en 0, con cuatro dwords, y el p de 32 bits, en 0, con lo que
// suma 0x40d8c0 por el byte +0x30 de la skill y, en sus 16 bits de abajo (add word ptr), 0x40d690 con 0x9f.
Core::Expected<std::int32_t, std::string> SetBonusOf(std::span<const std::byte, Data::Tables::SkillRecordSize> record,
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

OwnAttackDamage AttackDamageOf(const OwnCharacterRecord& own, std::uint32_t heroClass, const OwnEquipmentOverride& held,
                               std::uint8_t& ownRecordDepth, Render::Models::CrtRandom& random)
{
    const Render::Models::LogicalAccess access(ownRecordDepth, random, Render::Models::OwnRecordCloseRandoms);
    // 0x5b8c45..0x5b8cad: el ítem del cursor en lugar de una mano.
    const World::ItemState* rightItem = &own.equipment[0];
    const World::ItemState* leftItem = &own.equipment[1];
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
    constexpr std::uint32_t KnightClass = 1;
    constexpr std::uint32_t ElfClass = 2;
    constexpr std::uint32_t GladiatorClass = 3;
    if (heroClass == KnightClass || heroClass == GladiatorClass)
    {
        if (InRange(left, 0, 0xbff) && InRange(right, 0, 0xbff))
        {
            damage.highlighted = true;
            damage.min = FiftyFivePercent(own.word50) + FiftyFivePercent(own.word54);
            damage.max = FiftyFivePercent(own.word52) + FiftyFivePercent(own.word56);
        }
    }
    else if (heroClass == ElfClass && InRange(right, 0x800, 0x9ff) && InRange(left, 0x800, 0x9ff) &&
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

Core::Expected<OwnMagicDamage, std::string> MagicDamageOf(const OwnCharacterRecord& own, std::uint8_t skill,
                                                          const Data::Tables::SkillTable* skills,
                                                          std::uint8_t& skillDepth, Render::Models::CrtRandom& random,
                                                          ItemSetOptionList& setOptions)
{
    if (skills == nullptr)
        return Core::Unexpected(std::string("0x5513d0: sin Skill.bmd"));
    const auto record = skills->Record(skill);
    if (!record)
        return Core::Unexpected(std::format("0x5513d0: Skill.bmd no tiene la skill {}", skill));
    const Render::Models::LogicalAccess access(skillDepth, random,
                                               static_cast<std::uint16_t>(Data::Tables::SkillRecordSize));
    const auto base = static_cast<std::int32_t>(std::to_integer<std::uint8_t>((*record)[DamageByte]));
    OwnMagicDamage damage{.min = own.word5a + base, .max = (base >> 1) + base + own.word5c};
    const auto bonus = SetBonusOf(*record, setOptions);
    if (!bonus.HasValue())
        return Core::Unexpected(bonus.Error());
    damage.min += bonus.Value();
    damage.max += bonus.Value();
    return damage;
}

Core::Expected<OwnSkillDamage, std::string> SkillDamageOf(std::uint8_t skill, const Data::Tables::SkillTable* skills,
                                                          std::uint8_t& skillDepth, Render::Models::CrtRandom& random,
                                                          ItemSetOptionList& setOptions)
{
    if (skills == nullptr)
        return Core::Unexpected(std::string("0x5516e0: sin Skill.bmd"));
    const auto record = skills->Record(skill);
    if (!record)
        return Core::Unexpected(std::format("0x5516e0: Skill.bmd no tiene la skill {}", skill));
    const Render::Models::LogicalAccess access(skillDepth, random,
                                               static_cast<std::uint16_t>(Data::Tables::SkillRecordSize));
    // 0x551761..0x55178a: s y s + s / 2, con la división de C (cdq, sub, sar).
    const auto base = static_cast<std::int32_t>(std::to_integer<std::uint8_t>((*record)[DamageByte]));
    OwnSkillDamage damage{.min = base, .max = (base >> 1) + base};
    const auto bonus = SetBonusOf(*record, setOptions);
    if (!bonus.HasValue())
        return Core::Unexpected(bonus.Error());
    damage.min += bonus.Value();
    damage.max += bonus.Value();
    return damage;
}
} // namespace Client::Scenes
