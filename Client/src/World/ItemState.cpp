#include "World/ItemState.h"

#include "Data/Tables/ItemTable.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Client::World
{
namespace
{
using Metadata = std::span<const std::byte, Data::Tables::ItemRecordSize>;

// Los offsets y literales de estas fases son los de 0x54bf40 (CL7.L3); su contrato todavía no está
// documentado en el research (P-C36 del 08).
constexpr std::uint32_t LowLevelMaximum = 9;
constexpr std::uint32_t FirstTableLevel = 10;
constexpr std::array<std::uint32_t, 4> LevelBonuses{4, 9, 15, 22};
constexpr std::array<std::uint16_t, 7> Item180bRequirements{30, 60, 90, 130, 170, 210, 300};

std::uint32_t Byte(Metadata metadata, std::size_t offset)
{
    return std::to_integer<std::uint32_t>(metadata[offset]);
}

std::uint32_t Word(Metadata metadata, std::size_t offset)
{
    return Byte(metadata, offset) | (Byte(metadata, offset + 1) << 8);
}

std::int32_t SignedId(const ItemState& item)
{
    constexpr std::int32_t SignBit = 0x8000;
    constexpr std::int32_t WordModulus = 0x10000;
    const auto id = static_cast<std::int32_t>(item.id);
    return id < SignBit ? id : id - WordModulus;
}

bool InRange(std::int32_t value, std::int32_t first, std::int32_t last)
{
    return value >= first && value <= last;
}

// Un índice inválido es un bug del caller; como Expected, se corta también en Release.
void RequireOptionIndex(std::uint8_t index)
{
    if (index < MaxItemOptions)
    {
        return;
    }
    std::fputs("ItemState: índice de opción fuera de rango\n", stderr);
    std::abort();
}

void AddWord(std::uint16_t& destination, std::uint32_t amount)
{
    destination = static_cast<std::uint16_t>(destination + amount);
}

void AddByte(std::uint8_t& destination, std::uint32_t amount)
{
    destination = static_cast<std::uint8_t>(destination + amount);
}

struct Calculation
{
    Metadata metadata;
    std::uint32_t level;
    std::uint32_t lowLevel;
    std::uint32_t levelBonus;
    std::uint32_t q;
    bool p;
    std::uint32_t initialDenominatorPlus30;
    std::uint32_t specialBonus;
};

Calculation Prepare(const ItemState& item, Metadata metadata, ItemInitializationScalars scalars)
{
    const auto a = static_cast<std::uint8_t>(scalars.b1);
    const auto b = static_cast<std::uint8_t>(scalars.b3);
    const auto c = static_cast<std::uint8_t>(scalars.b4 & 3u);
    const std::uint32_t level = (a >> 3) & 15u;
    const std::int32_t id = SignedId(item);
    std::uint32_t q = b & 0x3fu;
    if (InRange(id, 0x1803, 0x1806) || id == 0x13 || id == 0x812 || id == 0xa0a || id == 0x40d || id == 0x1a1e)
    {
        q = 0;
    }
    const bool p = c == 1 || c == 2;
    if (p)
    {
        q = 1;
    }
    const std::uint32_t bonus = level >= FirstTableLevel && level < FirstTableLevel + LevelBonuses.size()
                                    ? LevelBonuses[level - FirstTableLevel]
                                    : 0;
    const std::uint32_t special = id == 0x406 ? 15u : id == 0x806 ? 30u : id == 0xa07 ? 25u : 0u;
    return {metadata, level, std::min(level, LowLevelMaximum), bonus, q, p, Word(metadata, 0x20) + 30, special};
}

void CopyMetadata(ItemState& item, Metadata metadata)
{
    item.byte0a = static_cast<std::uint8_t>(Byte(metadata, 0x1e));
    item.byte17 = static_cast<std::uint8_t>(Byte(metadata, 0x2b));
    item.word0c = static_cast<std::uint16_t>(Byte(metadata, 0x26));
    item.word0e = static_cast<std::uint16_t>(Byte(metadata, 0x27));
    item.byte10 = static_cast<std::uint8_t>(Byte(metadata, 0x28));
    item.word12 = static_cast<std::uint16_t>(Byte(metadata, 0x29));
    item.word14 = static_cast<std::uint16_t>(Byte(metadata, 0x2a));
    item.word18 = static_cast<std::uint16_t>(Byte(metadata, 0x2c));
    item.byte16 = static_cast<std::uint8_t>(Byte(metadata, 0x2f));
}

std::uint32_t FirstBonus(const Calculation& calculation, std::uint32_t base)
{
    if (calculation.specialBonus != 0)
    {
        return calculation.specialBonus;
    }
    return 25 * base / Word(calculation.metadata, 0x20) + 5;
}

void AdjustFirstWord(std::uint16_t& destination, std::size_t field, const Calculation& calculation)
{
    if (Byte(calculation.metadata, field) == 0)
    {
        return;
    }
    if (calculation.q != 0 && Word(calculation.metadata, 0x20) != 0)
    {
        // Incluso D+e toma T+26 en este cociente, no T+27.
        AddWord(destination, FirstBonus(calculation, Byte(calculation.metadata, 0x26)));
    }
    if (calculation.p)
    {
        AddWord(destination, calculation.initialDenominatorPlus30 / 40 + 5);
    }
    AddWord(destination, 3 * calculation.lowLevel + calculation.levelBonus);
}

void AdjustByte16(ItemState& item, const Calculation& calculation)
{
    if (Byte(calculation.metadata, 0x2f) == 0)
    {
        return;
    }
    if (calculation.q != 0 && Word(calculation.metadata, 0x20) != 0)
    {
        AddByte(item.byte16, FirstBonus(calculation, Byte(calculation.metadata, 0x2f)));
    }
    if (calculation.p)
    {
        AddByte(item.byte16, calculation.initialDenominatorPlus30 / 60 + 2);
    }
    AddByte(item.byte16, 3 * calculation.lowLevel + calculation.levelBonus);
    item.byte16 = static_cast<std::uint8_t>(item.byte16 >> 1);
    if (!InRange(SignedId(item), 0x408, 0x40e))
    {
        AddByte(item.byte16, 2 * calculation.level);
    }
}

void AdjustByte10(ItemState& item, const Calculation& calculation)
{
    if (Byte(calculation.metadata, 0x28) == 0)
    {
        return;
    }
    const std::uint32_t denominator = Word(calculation.metadata, 0x20);
    if (calculation.q != 0 && denominator != 0)
    {
        AddByte(item.byte10, 25 * Byte(calculation.metadata, 0x28) / denominator + 5);
    }
    AddByte(item.byte10, 3 * calculation.lowLevel + calculation.levelBonus);
}

void AdjustWord12(ItemState& item, const Calculation& calculation)
{
    if (Byte(calculation.metadata, 0x29) == 0)
    {
        return;
    }
    if (InRange(SignedId(item), 0xc00, 0xdff))
    {
        AddWord(item.word12, calculation.level);
        if (calculation.p)
        {
            AddWord(item.word12, 20u * item.word12 / calculation.initialDenominatorPlus30 + 2);
        }
        return;
    }
    const std::uint32_t denominator = Word(calculation.metadata, 0x20);
    if (calculation.q != 0 && denominator != 0)
    {
        AddWord(item.word12, denominator / 5 + 12 * Byte(calculation.metadata, 0x29) / denominator + 4);
    }
    if (calculation.p)
    {
        AddWord(item.word12, 3u * item.word12 / calculation.initialDenominatorPlus30 +
                                 calculation.initialDenominatorPlus30 / 30 + 2);
    }
    const std::int32_t id = SignedId(item);
    const std::uint32_t multiplier = InRange(id, 0x1803, 0x1806) || id == 0x1a1e ? 2u : 3u;
    AddWord(item.word12, multiplier * calculation.lowLevel + calculation.levelBonus);
}

void AdjustStatistics(ItemState& item, const Calculation& calculation)
{
    AdjustFirstWord(item.word0c, 0x26, calculation);
    AdjustFirstWord(item.word0e, 0x27, calculation);
    AdjustByte16(item, calculation);
    AdjustByte10(item, calculation);
    AdjustWord12(item, calculation);
    if (Byte(calculation.metadata, 0x2a) != 0)
    {
        AddWord(item.word14, 3 * calculation.lowLevel + calculation.levelBonus);
    }
}

void InitializeWord28(ItemState& item, const Calculation& calculation)
{
    const std::uint32_t raw = Word(calculation.metadata, 0x3a);
    const std::int32_t id = SignedId(item);
    item.word28 = 0;
    if (raw == 0)
    {
        return;
    }
    // La prioridad original impide que 1807, 1a00 y 1a25 lleguen al ajuste siguiente.
    if (InRange(id, 0, 0x17ff) || id == 0x1a25 || InRange(id, 0x1807, 0x1a00))
    {
        item.word28 = static_cast<std::uint16_t>(raw);
        return;
    }
    if (InRange(id, 0x1800, 0x1807) || id >= 0x1a00)
    {
        const std::uint32_t multiplier = InRange(id, 0x1803, 0x1806) ? 5u : 4u;
        item.word28 = static_cast<std::uint16_t>(raw + multiplier * calculation.level);
    }
}

std::uint16_t Requirement(std::uint32_t base, std::uint32_t value, std::uint32_t multiplier)
{
    if (value == 0)
    {
        return 0;
    }
    // IMUL guarda low32 antes del IDIV signed. Precisión infinita cambia el resultado.
    const std::uint32_t product = base * value * multiplier;
    const std::int32_t quotient = std::bit_cast<std::int32_t>(product) / 100;
    return static_cast<std::uint16_t>(quotient + 20);
}

void InitializeRequirements(ItemState& item, const Calculation& calculation)
{
    std::uint32_t base = Word(calculation.metadata, 0x20);
    // 0x54bf40/c47f..c495: Prepare fuerza Q=1 cuando P es true, por eso P usa +25.
    // Se conserva la rama original +30, aunque sea inviable desde argumentos válidos.
    if (calculation.q != 0)
    {
        base += 25;
    }
    else if (calculation.p)
    {
        base += 30;
    }
    base += 3 * calculation.level;
    item.word1e = Requirement(base, Word(calculation.metadata, 0x30), 3);
    item.word20 = Requirement(base, Word(calculation.metadata, 0x32), 3);
    item.word24 = Requirement(base, Word(calculation.metadata, 0x36), 3);
    item.word22 = Requirement(base, Word(calculation.metadata, 0x34), 4);
    item.word26 = Requirement(base, Word(calculation.metadata, 0x38), 3);
}

bool ExcludesWord28Bonus(std::int32_t id)
{
    if (id == 0x1a25 || id == 0x16 || id == 0x17 || id == 0xa0c || id == 0x815 || id == 0x40e)
    {
        return true;
    }
    return InRange(id, 0xe1d, 0xe21) || InRange(id, 0x101d, 0x1021) || InRange(id, 0x121d, 0x1221) ||
           InRange(id, 0x141d, 0x1421) || InRange(id, 0x161d, 0x1621);
}

void ApplyOverrides(ItemState& item, const Calculation& calculation, std::uint32_t b3)
{
    if (item.id == 0x180b)
    {
        item.word22 = calculation.level < Item180bRequirements.size() ? Item180bRequirements[calculation.level]
                                                                      : static_cast<std::uint16_t>(b3);
    }
    const std::uint32_t raw = Word(calculation.metadata, 0x38);
    if (raw != 0)
    {
        item.word26 = static_cast<std::uint16_t>(item.id == 0x1c17 ? 15 * raw + 185 : raw);
    }
    if (item.id == 0x1a0a)
    {
        item.word28 = calculation.level <= 2 ? 20 : 50;
    }
    if (calculation.q != 0 && item.word28 != 0 && !ExcludesWord28Bonus(SignedId(item)))
    {
        AddWord(item.word28, 20);
    }
}

void AppendTag(ItemState& item, std::uint8_t tag)
{
    RequireOptionIndex(item.optionCount);
    item.optionBytes[item.optionCount] = tag;
    item.optionCount = static_cast<std::uint8_t>(item.optionCount + 1u);
}

void AppendOption(ItemState& item, std::uint8_t tag, std::uint32_t value)
{
    RequireOptionIndex(item.optionCount);
    item.optionBytes[ItemOptionValueOffset + item.optionCount] = static_cast<std::uint8_t>(value);
    AppendTag(item, tag);
}

void InitialOptions(ItemState& item, const Calculation& calculation, std::uint8_t r)
{
    const bool special = InRange(SignedId(item), 0x1803, 0x1806);
    if (!special && item.id != 0x1a1e)
    {
        return;
    }
    if (!special)
    {
        AppendOption(item, 0x96, 15 + 2 * calculation.lowLevel + calculation.levelBonus);
        AppendOption(item, 0x94, 2 * (calculation.level + 10));
    }
    if ((r & 1u) != 0)
    {
        AppendOption(item, 0x64, 5 * (calculation.level + 10));
    }
    if ((r & 2u) != 0)
    {
        AppendOption(item, 0x65, 5 * (calculation.level + 10));
    }
    if ((r & 4u) != 0)
    {
        AppendOption(item, 0x66, 3);
    }
    if ((r & 8u) != 0)
    {
        AppendOption(item, special ? 0x67 : 0x90, special ? 50 : 5 * (calculation.level + 2));
    }
    if (special && (r & 16u) != 0)
    {
        AppendOption(item, 0x61, 5);
    }
}

void ScalarOptions(ItemState& item, Metadata metadata, std::uint8_t a)
{
    if ((a & 0x80u) != 0 && Byte(metadata, 0x23) != 0)
    {
        AppendTag(item, static_cast<std::uint8_t>(Byte(metadata, 0x23)));
    }
    if ((a & 4u) == 0)
    {
        return;
    }
    const std::int32_t id = SignedId(item);
    if ((InRange(id, 0, 0x17ff) && id != 0x807 && id != 0x80f) || InRange(id, 0x1800, 0x1806) || id == 0x1a1e)
    {
        AppendTag(item, 0x54);
    }
}

void SpecialMagnitudeOptions(ItemState& item, std::uint32_t magnitude)
{
    if ((magnitude & 1u) != 0)
    {
        AppendOption(item, 0x68, 5);
    }
    if ((magnitude & 2u) != 0)
    {
        AppendOption(item, 0x67, 50);
    }
    if ((magnitude & 4u) != 0)
    {
        AppendOption(item, 0x61, 5);
    }
}

std::uint8_t MagnitudeTag(std::int32_t id, std::uint8_t b)
{
    const bool bit5 = (b & 0x20u) != 0;
    switch (id)
    {
    case 0x1800:
        return 0x55;
    case 0x1801:
        return 0x51;
    case 0x1802:
        return 0x50;
    case 0x1803:
        return bit5 ? 0x55 : 0x50;
    case 0x1804:
        return bit5 ? 0x51 : 0x55;
    case 0x1805:
        return bit5 ? 0x50 : 0x55;
    case 0x1806:
        return bit5 ? 0x50 : 0x51;
    case 0x1a1e:
        return 0x50;
    default:
        return 0;
    }
}

void MagnitudeOptions(ItemState& item, std::uint8_t a, std::uint8_t b)
{
    const std::uint32_t magnitude = (a & 3u) + 4 * ((b >> 6) & 1u);
    if (magnitude == 0)
    {
        return;
    }
    const std::int32_t id = SignedId(item);
    if (id == 0x1a03)
    {
        SpecialMagnitudeOptions(item, magnitude);
        return;
    }
    if (InRange(id, 0, 0x17ff))
    {
        if (id == 0x807 || id == 0x80f)
        {
            return;
        }
        const std::uint8_t tag = id < 0xa00 ? 0x50 : id < 0xc00 ? 0x51 : id < 0xe00 ? 0x52 : 0x53;
        AppendOption(item, tag, (tag == 0x52 ? 5u : 4u) * magnitude);
        AddWord(item.word1e, 5 * magnitude);
        return;
    }
    if (InRange(id, 0x1a08, 0x1bff) && id != 0x1a1e)
    {
        AppendOption(item, id == 0x1a18 ? 0xb0 : id == 0x1a1c ? 0xb1 : 0x55, magnitude);
        return;
    }
    const std::uint8_t tag = MagnitudeTag(id, b);
    if (tag != 0)
    {
        AppendOption(item, tag, (tag == 0x55 ? 1u : 4u) * magnitude);
    }
}

void LateArmorOptions(ItemState& item, std::uint8_t b)
{
    const std::int32_t id = SignedId(item);
    if (!InRange(id, 0xc00, 0x17ff) && !InRange(id, 0x1a08, 0x1a09) && !InRange(id, 0x1a15, 0x1a18))
    {
        return;
    }
    constexpr std::array<std::uint8_t, 6> Masks{32, 16, 8, 4, 2, 1};
    constexpr std::array<std::uint8_t, 6> Tags{0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b};
    for (std::size_t index = 0; index < Masks.size(); ++index)
    {
        if ((b & Masks[index]) != 0)
        {
            AppendTag(item, Tags[index]);
        }
    }
}

void LateWeaponOptions(ItemState& item, std::uint8_t b, const std::uint16_t& contextWord)
{
    const std::int32_t id = SignedId(item);
    if (!InRange(id, 0, 0xbff) && !InRange(id, 0x1a0c, 0x1a0d) && !InRange(id, 0x1a19, 0x1a1c))
    {
        return;
    }
    if ((b & 0x20u) != 0)
    {
        AppendTag(item, 0x5c);
    }
    const bool alternate = InRange(id, 0xa00, 0xbff) || item.id == 0x1a0c || item.id == 0x1a19 || item.id == 0x1a1b;
    if ((b & 0x10u) != 0)
    {
        AppendOption(item, alternate ? 0x5f : 0x5d, contextWord / 20u);
    }
    if ((b & 8u) != 0)
    {
        AppendOption(item, alternate ? 0x60 : 0x5e, 2);
    }
    if ((b & 4u) != 0)
    {
        AppendOption(item, 0x61, 7);
    }
    if ((b & 2u) != 0)
    {
        AppendTag(item, 0x62);
    }
    if ((b & 1u) != 0)
    {
        AppendTag(item, 0x63);
    }
}

void FinalOptions(ItemState& item, std::uint32_t level)
{
    if (item.id != 0x1a14 || (level != 0 && level != 3))
    {
        return;
    }
    AppendOption(item, 0x60, 10);
    AppendOption(item, 0x5e, 10);
    AppendOption(item, 0x61, 10);
}

std::uint8_t Classify(const ItemState& item, std::uint32_t level)
{
    const std::int32_t id = SignedId(item);
    if (InRange(id, 0x800, 0x807) || id == 0x811 || id == 0x1a05)
    {
        return 1;
    }
    if (id == 0x1a14 && (level == 0 || level == 3))
    {
        return 10;
    }
    struct RangeClass
    {
        std::int32_t first;
        std::int32_t last;
        std::uint8_t classification;
    };
    // Se mantiene el orden original, incluido 1a19..1a1c antes del rango general que lo cubre.
    constexpr std::array Ranges{
        RangeClass{0, 0xbff, 0},        RangeClass{0xc00, 0xdff, 1},    RangeClass{0xe00, 0xfff, 2},
        RangeClass{0x1000, 0x11ff, 3},  RangeClass{0x1200, 0x13ff, 4},  RangeClass{0x1400, 0x15ff, 5},
        RangeClass{0x1600, 0x17ff, 6},  RangeClass{0x1800, 0x1806, 7},  RangeClass{0x1a00, 0x1a07, 8},
        RangeClass{0x1a08, 0x1a0b, 10}, RangeClass{0x1a15, 0x1a18, 10}, RangeClass{0x1a19, 0x1a1c, 9},
        RangeClass{0x1a0c, 0x1bff, 9}};
    for (const auto& range : Ranges)
    {
        if (InRange(id, range.first, range.last))
        {
            return range.classification;
        }
    }
    return 0xff;
}

} // namespace

std::uint8_t ItemState::OptionTag(std::uint8_t index) const
{
    RequireOptionIndex(index);
    return optionBytes[index];
}

std::uint8_t ItemState::OptionValue(std::uint8_t index) const
{
    RequireOptionIndex(index);
    return optionBytes[ItemOptionValueOffset + index];
}

void InitializeItemState(ItemState& item, std::span<const std::byte, Data::Tables::ItemRecordSize> metadata,
                         ItemInitializationScalars scalars, const std::uint16_t& contextWord,
                         const Item4225d0& apply4225d0)
{
    const auto a = static_cast<std::uint8_t>(scalars.b1);
    const auto b = static_cast<std::uint8_t>(scalars.b3);
    item.scalar1Low = a; // Primer store de 0x54bf40, antes de leer metadata.
    const Calculation calculation = Prepare(item, metadata, scalars);
    CopyMetadata(item, metadata);
    AdjustStatistics(item, calculation);
    InitializeWord28(item, calculation);
    InitializeRequirements(item, calculation);
    ApplyOverrides(item, calculation, scalars.b3);
    item.optionCount = 0;
    InitialOptions(item, calculation, static_cast<std::uint8_t>(b & 0x3fu));
    ScalarOptions(item, metadata, a);
    MagnitudeOptions(item, a, b);
    if (item.id == 0x1a04)
    {
        apply4225d0(item);
    }
    LateArmorOptions(item, b);
    LateWeaponOptions(item, b, contextWord);
    FinalOptions(item, calculation.level);
    item.itemClass = Classify(item, calculation.level);
}
} // namespace Client::World
