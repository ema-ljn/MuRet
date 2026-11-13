#include "Scenes/ItemHelpLevels.h"

#include "Data/Tables/ItemTable.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/GameHud.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Client::Scenes
{
namespace
{
using Record = std::span<const std::byte, Data::Tables::ItemRecordSize>;

// Los bytes de Item.bmd que lee 0x5afc4d..0x5afc5f y las palabras de 0x5afdc9..0x5afec6.
constexpr std::size_t MinDamageByte = 0x26;
constexpr std::size_t MaxDamageByte = 0x27;
constexpr std::size_t DefenseRateByte = 0x28;
constexpr std::size_t DefenseByte = 0x29;
constexpr std::size_t MagicByte = 0x2f;
constexpr std::size_t ItemLevelWord = 0x20;
constexpr std::size_t StrengthWord = 0x30;
constexpr std::size_t AgilityWord = 0x32;
constexpr std::size_t EnergyWord = 0x34;
constexpr std::size_t VitalityWord = 0x36;
constexpr std::size_t LeadershipWord = 0x38;
constexpr std::size_t RequiredLevelWord = 0x3a;

// 0x5afc78..0x5afca7: 3 por nivel hasta el 9 y, del 10 al 13, lo de la tabla 0x5b04f4 (4, 4 + 5, 4 + 5 + 6 y
// 4 + 5 + 6 + 7).
constexpr std::int32_t PerLevel = 3;
constexpr std::int32_t LastPlainLevel = 9;
constexpr std::int32_t FirstHighLevel = 10;
constexpr std::array<std::int32_t, 4> HighLevelBonus{4, 9, 15, 22};
// 0x5afd32: lo que suma cada nivel al poder, salvo en los cetros.
constexpr std::int32_t MagicPerLevel = 2;
// 0x5afd1f..0x5afd30 y 0x5aff75..0x5aff87: de 0x408 a 0x40d el poder va en la columna 5.
constexpr std::int32_t FirstScepter = 0x408;
constexpr std::int32_t LastScepter = 0x40d;
// 0x5afd47..0x5afd55: los escudos suman solo el nivel a la defensa.
constexpr std::int32_t FirstShield = 0xc00;
constexpr std::int32_t ShieldsEnd = 0xe00;
// 0x5afdea..0x5afe0e: (nivel del ítem + 3 · nivel) · requisito · 3 (4 la energía) / 100 + 20.
constexpr std::uint32_t RequirementFactor = 3;
constexpr std::uint32_t EnergyFactor = 4;
constexpr std::int32_t RequirementDivisor = 100;
constexpr std::int32_t RequirementBase = 20;

std::int32_t ByteAt(Record record, std::size_t offset)
{
    return std::to_integer<std::int32_t>(record[offset]);
}

std::uint16_t WordAt(Record record, std::size_t offset)
{
    return static_cast<std::uint16_t>(std::to_integer<std::uint16_t>(record[offset]) |
                                      (std::to_integer<std::uint16_t>(record[offset + 1]) << 8U));
}

// Un byte con el nivel: en 0 queda en 0.
std::int32_t Leveled(std::int32_t value, std::int32_t level)
{
    if (value <= 0)
        return 0;
    value += PerLevel * std::min(level, LastPlainLevel);
    if (level >= FirstHighLevel)
        value += HighLevelBonus[static_cast<std::size_t>(level - FirstHighLevel)];
    return value;
}

// Un requisito con el nivel, en 32 bits con signo (el imul y el lea envuelven); en 0 queda en 0.
std::int32_t Requirement(std::uint16_t required, std::uint16_t itemLevel, std::int32_t level, std::uint32_t factor)
{
    if (required == 0)
        return 0;
    const auto base = static_cast<std::uint32_t>(itemLevel) + static_cast<std::uint32_t>(PerLevel * level);
    const auto product = static_cast<std::int32_t>(base * required * factor);
    return product / RequirementDivisor + RequirementBase;
}

bool IsScepter(std::int32_t item)
{
    return item >= FirstScepter && item <= LastScepter;
}

// 0x5afc44..0x5aff9a: los valores de un nivel.
ItemHelpRow RowOf(std::int32_t item, Record record, std::int32_t level)
{
    ItemHelpRow row{};
    row[ItemHelpLevel] = level;
    row[ItemHelpMinDamage] = Leveled(ByteAt(record, MinDamageByte), level);
    row[ItemHelpMaxDamage] = Leveled(ByteAt(record, MaxDamageByte), level);
    std::int32_t magic = Leveled(ByteAt(record, MagicByte), level);
    if (magic > 0)
    {
        magic /= 2;
        if (!IsScepter(item))
            magic += MagicPerLevel * level;
    }
    row[IsScepter(item) ? ItemHelpScepterMagic : ItemHelpMagic] = magic;
    const std::int32_t defense = ByteAt(record, DefenseByte);
    row[ItemHelpDefense] =
        defense > 0 && item >= FirstShield && item < ShieldsEnd ? defense + level : Leveled(defense, level);
    row[ItemHelpDefenseRate] = Leveled(ByteAt(record, DefenseRateByte), level);
    const std::uint16_t itemLevel = WordAt(record, ItemLevelWord);
    row[ItemHelpStrength] = Requirement(WordAt(record, StrengthWord), itemLevel, level, RequirementFactor);
    row[ItemHelpAgility] = Requirement(WordAt(record, AgilityWord), itemLevel, level, RequirementFactor);
    row[ItemHelpVitality] = Requirement(WordAt(record, VitalityWord), itemLevel, level, RequirementFactor);
    row[ItemHelpEnergy] = Requirement(WordAt(record, EnergyWord), itemLevel, level, EnergyFactor);
    row[ItemHelpLeadership] = Requirement(WordAt(record, LeadershipWord), itemLevel, level, RequirementFactor);
    row[ItemHelpRequiredLevel] = WordAt(record, RequiredLevelWord);
    return row;
}

// Un stat más lo que le suman los ítems, en 16 bits (el add de ax de 0x5b0259..0x5b02b1).
std::int32_t WithBonus(std::uint16_t stat, std::uint16_t bonus)
{
    return static_cast<std::uint16_t>(stat + bonus);
}

// 0x5b0251..0x5b02d8: con el registro propio abierto, 1 si cumple cada requisito (comparado con signo) y el nivel.
std::int32_t UsableBy(const OwnCharacterRecord& own, const ItemHelpRow& row)
{
    const bool usable = row[ItemHelpStrength] <= WithBonus(own.strength, own.word2a) &&
                        row[ItemHelpAgility] <= WithBonus(own.agility, own.word2c) &&
                        row[ItemHelpEnergy] <= WithBonus(own.energy, own.word30) &&
                        row[ItemHelpVitality] <= WithBonus(own.vitality, own.word2e) &&
                        row[ItemHelpLeadership] <= WithBonus(own.leadership, own.word36) &&
                        row[ItemHelpRequiredLevel] <= static_cast<std::int32_t>(own.level);
    return usable ? 1 : 0;
}
} // namespace

void FillTheItemHelpLevels(GameHudContext& context, std::int32_t item, Record record)
{
    ItemHelpLevels& levels = context.hud.itemHelp;
    // 0x5afc09..0x5afc1d: con el ítem de la tabla, nada.
    if (levels.item == item)
        return;
    levels.item = item;
    for (std::size_t level = 0; level < ItemHelpLevelCount; ++level)
    {
        ItemHelpRow& row = levels.rows[level];
        row = RowOf(item, record, static_cast<std::int32_t>(level));
        // 0x5aff9b..0x5b04c3: el registro propio se abre y se cierra en cada nivel.
        const Render::Models::LogicalAccess access(context.ownRecordDepth, context.random,
                                                   Render::Models::OwnRecordCloseRandoms);
        row[ItemHelpUsable] = UsableBy(context.world.OwnCharacter(), row);
    }
}
} // namespace Client::Scenes
