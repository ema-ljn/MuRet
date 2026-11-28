#include "Items/ItemPrice.h"

#include "Core/Expected.h"
#include "Tables/ItemTable.h"
#include "Items/HudItems.h"
#include "Math/Ftol.h"
#include "Kernel/ItemState.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Items
{
namespace
{
using Record = std::span<const std::byte, Tables::ItemRecordSize>;

constexpr std::uint16_t NoItem = 0xffff;
// D+0x2b: the first option.
constexpr std::size_t OptionsAt = 0x2b;
// What it reads from Item.bmd: the fixed price (+0x40, a signed dword), the item level (+0x20, a word), the byte +0x1e
// (1 in the two-handed weapons of the repo's file), the durability (+0x2d) and the potions' value (+0x3c).
constexpr std::size_t FixedPriceAt = 0x40;
constexpr std::size_t ItemLevelAt = 0x20;
constexpr std::size_t TwoHandedAt = 0x1e;
constexpr std::size_t DurabilityAt = 0x2d;
constexpr std::size_t PotionValueAt = 0x3c;

// 0x54d1b2..0x54d1d3: the group (the type / 512, signed) and the level ((D+4 >> 3) & 0xf).
constexpr std::int32_t GroupSize = 512;
constexpr std::uint32_t LevelShift = 3;
constexpr std::uint32_t LevelMask = 0xf;
constexpr std::int32_t WingGroup = 12;
constexpr std::int32_t HelperGroup = 13;
constexpr std::int32_t ScrollGroup = 15;
constexpr std::int32_t LastWeaponGroup = 6;

// 0x54d1dc..0x54d20b: the base, Item.bmd's level + 3 per level, + 25 with any option from 0x56 to 0x63.
constexpr std::int32_t BasePerLevel = 3;
constexpr std::uint8_t FirstExcellentTag = 0x56;
constexpr std::uint8_t LastExcellentTag = 0x63;
constexpr std::int32_t ExcellentBase = 25;

// 0x54da1d..0x54da2b: the cap of the cases that go through there.
constexpr std::int64_t PriceCap = 3'000'000'000;
// 0x54d145, 0x54d6cf and 0x54da51: what the modes other than 0 divide by.
constexpr std::int64_t ModeDivisor = 3;

constexpr std::uint16_t Bolts = 0x807;
constexpr std::uint16_t Arrows = 0x80f;
constexpr std::uint16_t BlessBundle = 0x181e;
constexpr std::uint16_t SoulBundle = 0x181f;
constexpr std::uint16_t Dinorant = 0x1a03;
constexpr std::uint16_t FirstPet = 0x1a04;
constexpr std::uint16_t SecondPet = 0x1a05;
constexpr std::uint16_t Archangel = 0x1a10;
constexpr std::uint16_t BloodBone = 0x1a11;
constexpr std::uint16_t Cloak = 0x1a12;
constexpr std::uint16_t WizardRing = 0x1a14;
constexpr std::uint16_t Spirit = 0x1a1f;
constexpr std::uint16_t CapeOfLord = 0x1a1e;
constexpr std::uint16_t Ale = 0x1c09;
constexpr std::uint16_t Kundun = 0x1c1d;
constexpr std::uint16_t Rena = 0x1c15;
constexpr std::uint16_t SiegePotion = 0x1c07;
constexpr std::uint16_t DevilEye = 0x1c11;
constexpr std::uint16_t DevilKey = 0x1c12;
constexpr std::uint16_t DevilInvitation = 0x1c13;
constexpr std::uint16_t LastEarlyWing = 0x1806;
constexpr std::uint16_t FirstPotion = 0x1c00;
constexpr std::uint16_t LastPotion = 0x1c08;

struct ConstantPrice
{
    std::uint16_t type = 0;
    std::int64_t price = 0;
};

// 0x54d2b1..0x54d63a: the fixed prices, which go through 0x54da30 (no cap).
constexpr std::array<ConstantPrice, 11> ConstantPrices{{{.type = 0x1c0d, .price = 9'000'000},
                                                        {.type = 0x1c0e, .price = 6'000'000},
                                                        {.type = 0x180f, .price = 810'000},
                                                        {.type = 0x1c10, .price = 45'000'000},
                                                        {.type = 0x1c16, .price = 36'000'000},
                                                        {.type = 0x1a0f, .price = 33'000'000},
                                                        {.type = 0x1c1c, .price = 600'000},
                                                        {.type = 0x1a1d, .price = 5'000},
                                                        {.type = 0x1c14, .price = 900},
                                                        {.type = 0x1c0a, .price = 750},
                                                        {.type = 0x1c1f, .price = 60'000'000}}};

struct FirstLevelsPrice
{
    std::uint16_t type = 0;
    std::array<std::int64_t, 2> prices{};
};

// Those of levels 0 and 1 (through 0x54da30); with another level, 0 and to 0x54da38. 0x1a14 and 0x1a1f come after
// the potions (0x54d6ef and 0x54d714).
constexpr std::array<FirstLevelsPrice, 3> FirstLevelsPrices{{{.type = 0x1a0e, .prices = {180'000, 7'500'000}},
                                                             {.type = 0x1a07, .prices = {1'500'000, 1'200'000}},
                                                             {.type = 0x1a0b, .prices = {100'000, 2'400'000}}}};
constexpr FirstLevelsPrice WizardRingPrices{.type = WizardRing, .prices = {30'000, 0}};
constexpr FirstLevelsPrice SpiritPrices{.type = Spirit, .prices = {30'000'000, 15'000'000}};

// 0x54d21b..0x54d265: the bolts and the arrows, by level.
constexpr std::array<std::int32_t, 3> BoltPrices{100, 1'400, 2'200};
constexpr std::array<std::int32_t, 3> ArrowPrices{70, 1'200, 2'000};
// 0x402d60: the jewel bundles, (5 · level + 5) · 18000000 and (level + 1) · 60000000.
constexpr std::int32_t BlessBundleStep = 18'000'000;
constexpr std::int32_t BlessBundleLevels = 5;
constexpr std::int32_t SoulBundleStep = 60'000'000;
// 0x54d340..0x54d37a: the Dinorant, 960000 and 300000 for each option 0x61, 0x67 or 0x68.
constexpr std::int64_t DinorantPrice = 960'000;
constexpr std::int64_t DinorantOptionPrice = 300'000;
constexpr std::array<std::uint8_t, 3> DinorantOptions{0x61, 0x67, 0x68};
// 0x54d9c4..0x54da0e: the scroll and the bone, by level from 1 to 7, times 3.
constexpr std::array<std::int64_t, 7> ArchangelPrices{5'000, 7'000, 10'000, 13'000, 16'000, 20'000, 25'000};
constexpr std::int64_t TripledPrice = 3;
// 0x54d3b3..0x54d3d8: the cloak, (5 · level + 45) · 4000, or 50000 at level 1, times 3.
constexpr std::int32_t CloakLevels = 5;
constexpr std::int32_t CloakBase = 45;
constexpr std::int32_t CloakStep = 4'000;
constexpr std::int64_t CloakFirstLevelPrice = 50'000;
// 0x54d3f5..0x54d46e and 0x54d59f..0x54d5e8: the ones priced by the durability (D+0x1a).
constexpr std::int64_t KundunPerUnit = 30'000;
constexpr std::int64_t RenaPrice = 9'000;
constexpr std::int32_t RenaUnitLevel = 3;
constexpr std::int64_t RenaPerUnit = 3'900;
constexpr std::array<std::int64_t, 2> SiegePotionPerUnit{900'000, 450'000};
constexpr std::int64_t AlePrice = 1'000;
// 0x54d47b..0x54d546: the Devil Square ones, by level up to 4.
constexpr std::array<std::int64_t, 5> DevilPrices{30'000, 15'000, 21'000, 30'000, 45'000};
constexpr std::array<std::int64_t, 5> InvitationPrices{120'000, 60'000, 84'000, 120'000, 180'000};

// 0x54d64d..0x54d66a: the value of +0x3c, 10 · v² / 12. 0x54d694..0x54d6ee: the potions' one times 2^level and times
// the durability, and the rounding to 10 in 32 bits.
constexpr std::int32_t PotionScale = 10;
constexpr std::int32_t PotionDivisor = 12;
constexpr std::int64_t PotionRound = 10;

// 0x54d762..0x54d79f: what each level from 5 to 13 adds to the base (the table at 0x54dc68).
constexpr std::int32_t FirstStepLevel = 5;
constexpr std::array<std::int32_t, 9> LevelSteps{4, 10, 25, 45, 65, 95, 135, 185, 245};
// 0x54d7b8..0x54d7df: (b + 40) · b², times 11 plus 40000000 for the wings, or / 8 + 100.
constexpr std::uint32_t CubeOffset = 40;
constexpr std::uint32_t WingFactor = 11;
constexpr std::uint32_t WingBase = 40'000'000;
constexpr std::int32_t GearDivisor = 8;
constexpr std::int32_t GearBase = 100;
// 0x54d803..0x54d814: the one-handed weapons, at 80 %.
constexpr std::int64_t OneHandedPercent = 80;
constexpr std::int64_t Percent = 100;
// 0x54d963..0x54d96b: the helpers, the scrolls and the wings after 0x1806, b³ + 100.
constexpr std::uint32_t HelperBase = 100;

// The weapon and armor options (the table at 0x54dca4 over the option − 0x12).
constexpr std::uint8_t ExtraOption = 0x55;
constexpr std::uint8_t StepByFiveOption = 0x52;
constexpr std::uint8_t ShiftOfExtraOption = 2;
// 0x6b8b94 (1.5f), 0x6ba400 (0.25) and 0x6ba420..0x6ba408: the four step doubles, each just above 0.6, 1.4, 2.8 and
// 5.6.
constexpr float SkillFactor = 1.5f;
constexpr double QuarterFactor = 0.25;
constexpr std::array<double, 4> StepFactors{0x1.3333333333334p-1, 0x1.6666666666667p+0, 0x1.6666666666667p+1,
                                            0x1.6666666666667p+2};
constexpr std::uint32_t StepOfFour = 4;
constexpr std::uint32_t StepOfFive = 5;

// 0x422930: the horse's level times 2000000 and the raven's times 1000000, in 32 bits.
constexpr std::uint32_t HorsePricePerLevel = 2'000'000;
constexpr std::uint32_t RavenPricePerLevel = 1'000'000;

// 0x54da69..0x54db10: the ones that overwrite the price after the mode.
constexpr std::uint16_t FirstOverridden = 0x1a20;
constexpr std::uint16_t LastOverridden = 0x1a25;
constexpr std::array<std::int64_t, 4> PerUnitOverrides{50, 100, 1'000, 10'000};
constexpr std::array<std::int64_t, 2> FixedOverrides{30'000, 50'000};

// 0x54dbea..0x54dc04: the missing durability, times the price and times 0.6f (0x6b8a20).
constexpr float DurabilityShare = 0.6f;

// 0x54dc17..0x54dc5c: the rounding, to 100 from 1000 and to 10 from 100.
constexpr std::int64_t HundredsFrom = 1'000;
constexpr std::int64_t TensFrom = 100;
constexpr std::int64_t Hundred = 100;
constexpr std::int64_t Ten = 10;

// 0x54da42..0x54da4f: the angel and the imp are not divided by the mode.
constexpr std::uint16_t Angel = 0x1a00;
constexpr std::uint16_t Imp = 0x1a01;

// 0x5b13e0: the cap (0x17d78400, signed), 3.0f (0x6b86b8), 1.4f without durability (0x6ba610), the low byte of
// [0x78abb10] times 0.05f (0x6b8a48) and the rounding of 0x5b1464..0x5b14a3.
constexpr std::int32_t RepairPriceCap = 400'000'000;
constexpr float RepairRootFactor = 3.0f;
constexpr float BrokenFactor = 1.4f;
constexpr std::int32_t SelfRepairMask = 0xff;
constexpr float SelfRepairShare = 0.05f;
constexpr std::int32_t RepairHundredsFrom = 1'000;
constexpr std::int32_t RepairTensFrom = 100;
constexpr std::int32_t RepairHundred = 100;
constexpr std::int32_t RepairTen = 10;
// 0x5b14a5..0x5b14dd: the thousands groups.
constexpr std::uint32_t Thousand = 1'000;
constexpr std::uint32_t Million = 1'000'000;
constexpr std::uint32_t Billion = 1'000'000'000;

// FILD qword over an integer of up to 53 bits is the same double: with the x87 at PC53 (ADR 0012), the product rounded
// once is the x87's. With a larger one, the x87 multiplies the exact integer and a double does not.
constexpr std::int64_t ExactInADouble = std::int64_t{1} << 53;

struct TypePrice
{
    std::int64_t value = 0;
    // Whether it leaves through 0x54da1d, which applies the cap; otherwise, through 0x54da30 or 0x54da38.
    bool capped = false;
    // The potions return at 0x54d6ee, without the cap, the mode or the final rounding.
    std::optional<std::uint32_t> returned{};
};

TypePrice Plain(std::int64_t value)
{
    return TypePrice{.value = value};
}

TypePrice Capped(std::int64_t value)
{
    return TypePrice{.value = value, .capped = true};
}

// __allmul (0x6a1da0): the 64-bit product, modulo 2^64.
std::int64_t AllMul(std::int64_t left, std::int64_t right)
{
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(left) * static_cast<std::uint64_t>(right));
}

// __alldiv (0x6a0150): the signed 64-bit division, toward 0. The divisors are positive constants.
std::int64_t AllDiv(std::int64_t dividend, std::int64_t divisor)
{
    return dividend / divisor;
}

std::uint32_t LowDword(std::int64_t value)
{
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(value));
}

std::uint8_t ByteAt(Record record, std::size_t offset)
{
    return std::to_integer<std::uint8_t>(record[offset]);
}

std::int32_t DwordAt(Record record, std::size_t offset)
{
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < sizeof(value); ++i)
        value |= static_cast<std::uint32_t>(ByteAt(record, offset + i)) << (8 * i);
    return static_cast<std::int32_t>(value);
}

std::int32_t WordAt(Record record, std::size_t offset)
{
    return static_cast<std::int32_t>(ByteAt(record, offset) | (ByteAt(record, offset + 1) << 8));
}

// D+0x2b + offset: the options from +0x2b and their values from +0x33. MuRet has the item's bytes up to +0x3b.
Core::Expected<std::uint8_t, std::string> OptionByte(const Kernel::ItemState& item, std::size_t offset,
                                                     std::string_view address)
{
    if (offset >= item.optionBytes.size())
        return Core::Unexpected(std::format("{}: el precio lee D+{:#x}, fuera de las opciones (D+0x2b..D+0x3b) que "
                                            "tiene MuRet",
                                            address, OptionsAt + offset));
    return item.optionBytes[offset];
}

Core::Expected<std::uint8_t, std::string> OptionValue(const Kernel::ItemState& item, std::size_t index,
                                                      std::string_view address)
{
    return OptionByte(item, Kernel::ItemOptionValueOffset + index, address);
}

// 0x54dc17..0x54dc5c (and 0x54d14e..0x54d1b1): the low dword, rounded with the 64-bit arithmetic.
std::uint32_t RoundThePrice(std::int64_t price)
{
    if (price < 0)
        return LowDword(price);
    if (price >= HundredsFrom)
        return LowDword(AllMul(AllDiv(price, Hundred), Hundred));
    if (price >= TensFrom)
        return LowDword(AllMul(AllDiv(price, Ten), Ten));
    return LowDword(price);
}

// The x87's multiplication of a 64-bit integer (FILD qword) at PC53.
Core::Expected<double, std::string> TimesInX87(std::int64_t price, double factor, std::string_view address)
{
    if (price > ExactInADouble || price < -ExactInADouble)
        return Core::Unexpected(std::format("{}: la x87 multiplica el precio {} entero, y no entra en los 53 bits de "
                                            "un double",
                                            address, price));
    return static_cast<double>(price) * factor;
}

// 0x69fd3c on a bounded product: an integer of up to 53 bits times 5.6 or less fits in 64 bits.
std::int64_t Ftol64(double value)
{
    return static_cast<std::int64_t>(value);
}

std::int32_t LevelOf(const Kernel::ItemState& item)
{
    return static_cast<std::int32_t>((item.scalar1Low >> LevelShift) & LevelMask);
}

// 0x54d1d6..0x54d20b.
Core::Expected<std::int32_t, std::string> BaseOf(const Kernel::ItemState& item, Record record, std::int32_t level)
{
    bool excellent = false;
    for (std::size_t i = 0; i < std::size_t{item.optionCount}; ++i)
    {
        auto tag = OptionByte(item, i, "0x54d1dc");
        if (!tag.HasValue())
            return Core::Unexpected(std::move(tag).Error());
        if (tag.Value() >= FirstExcellentTag && tag.Value() <= LastExcellentTag)
            excellent = true;
    }
    const std::int32_t base = WordAt(record, ItemLevelAt) + level * BasePerLevel;
    return excellent ? base + ExcellentBase : base;
}

// 0x54d21b..0x54d28a: the durability times the level's price, over Item.bmd's. From level 3, EBX keeps the item's
// address (0x54d23b and 0x54d267) and the price comes from it (Q-C735).
Core::Expected<TypePrice, std::string> AmmoPrice(const Kernel::ItemState& item, Record record, std::int32_t level)
{
    const std::uint8_t full = ByteAt(record, DurabilityAt);
    if (full == 0)
        return Plain(0);
    const auto& prices = item.id == Bolts ? BoltPrices : ArrowPrices;
    if (static_cast<std::size_t>(level) >= prices.size())
        return Core::Unexpected(
            std::format("0x54d283: el precio del {:#x} de nivel {} multiplica la durabilidad por la "
                        "dirección del ítem",
                        item.id, level));
    return Capped(std::int64_t{item.durability * prices[static_cast<std::size_t>(level)] / full});
}

// 0x402d60 (with 0x402b30 at 2 or 3).
TypePrice BundlePrice(const Kernel::ItemState& item, std::int32_t level)
{
    if (item.id == BlessBundle)
        return Capped(std::int64_t{(BlessBundleLevels * level + BlessBundleLevels) * BlessBundleStep});
    return Capped(std::int64_t{(level + 1) * SoulBundleStep});
}

Core::Expected<TypePrice, std::string> DinorantPriceOf(const Kernel::ItemState& item)
{
    if (item.optionCount == 0)
        return Plain(DinorantPrice);
    std::int64_t price = DinorantPrice;
    for (std::size_t i = 0; i < std::size_t{item.optionCount}; ++i)
    {
        auto tag = OptionByte(item, i, "0x54d35c");
        if (!tag.HasValue())
            return Core::Unexpected(std::move(tag).Error());
        if (std::ranges::find(DinorantOptions, tag.Value()) != DinorantOptions.end())
            price += DinorantOptionPrice;
    }
    return Capped(price);
}

std::optional<TypePrice> LevelTablePrice(std::uint16_t type, std::int32_t level)
{
    const std::size_t clamped = std::min(static_cast<std::size_t>(level), DevilPrices.size() - 1);
    if (type == DevilEye || type == DevilKey)
        return Capped(DevilPrices[clamped]);
    if (type == DevilInvitation)
        return Capped(InvitationPrices[clamped]);
    if (type == Archangel || type == BloodBone)
    {
        const bool listed = level >= 1 && static_cast<std::size_t>(level) <= ArchangelPrices.size();
        return Capped(AllMul(listed ? ArchangelPrices[static_cast<std::size_t>(level - 1)] : 0, TripledPrice));
    }
    if (type == Cloak)
    {
        const std::int64_t price =
            level == 1 ? CloakFirstLevelPrice : std::int64_t{(CloakLevels * level + CloakBase) * CloakStep};
        return Capped(AllMul(price, TripledPrice));
    }
    for (const FirstLevelsPrice& entry : FirstLevelsPrices)
    {
        if (entry.type == type)
            return level <= 1 ? Plain(entry.prices[static_cast<std::size_t>(level)]) : Plain(0);
    }
    return std::nullopt;
}

std::optional<TypePrice> DurabilityPrice(const Kernel::ItemState& item, std::int32_t level)
{
    const std::int64_t units = item.durability;
    if (item.id == Kundun)
        return Capped(units * KundunPerUnit);
    if (item.id == Rena)
    {
        if (level <= 1)
            return Plain(RenaPrice);
        return level == RenaUnitLevel ? Capped(units * RenaPerUnit) : Plain(0);
    }
    if (item.id == SiegePotion)
        return level <= 1 ? Capped(units * SiegePotionPerUnit[static_cast<std::size_t>(level)]) : Plain(0);
    return std::nullopt;
}

// 0x54d2b1..0x54d63a: the types with their own price, before looking at +0x3c.
std::optional<TypePrice> NamedTypePrice(const Kernel::ItemState& item, std::int32_t level)
{
    for (const ConstantPrice& entry : ConstantPrices)
    {
        if (entry.type == item.id)
            return Plain(entry.price);
    }
    if (item.id == Ale && level == 1)
        return Plain(AlePrice);
    if (auto byLevel = LevelTablePrice(item.id, level))
        return byLevel;
    return DurabilityPrice(item, level);
}

// 0x54d64d..0x54d6ee: the value of +0x3c; from 0x1c00 to 0x1c08, times 2^level and times the durability, and returns.
TypePrice PotionPrice(const Kernel::ItemState& item, Record record, std::int32_t mode, std::int32_t level)
{
    const std::int32_t value = ByteAt(record, PotionValueAt);
    const std::int64_t base = value * value * PotionScale / PotionDivisor;
    if (item.id < FirstPotion || item.id > LastPotion)
        return Capped(base);
    // 0x6a1b80's pow(2.0, level) and 0x69fd3c: exact.
    std::int64_t price = level > 0 ? AllMul(std::int64_t{1} << level, base) : base;
    price = AllMul(item.durability, price);
    if (mode != 0)
        price = AllDiv(price, ModeDivisor);
    const std::uint32_t tenths = LowDword(AllDiv(price, PotionRound));
    return TypePrice{.returned = tenths * static_cast<std::uint32_t>(PotionRound)};
}

// 0x54d963..0x54d9c2: b³ + 100, times the value + 1 of each option 0x55.
Core::Expected<TypePrice, std::string> HelperPrice(const Kernel::ItemState& item, std::int32_t base)
{
    const auto side = static_cast<std::uint32_t>(base);
    std::int64_t price = static_cast<std::int32_t>(side * side * side + HelperBase);
    for (std::size_t i = 0; i < std::size_t{item.optionCount}; ++i)
    {
        auto tag = OptionByte(item, i, "0x54d991");
        if (!tag.HasValue())
            return Core::Unexpected(std::move(tag).Error());
        if (tag.Value() != ExtraOption)
            continue;
        auto value = OptionValue(item, i, "0x54d99e");
        if (!value.HasValue())
            return Core::Unexpected(std::move(value).Error());
        price = AllMul(std::int64_t{value.Value()} + 1, price);
    }
    return Capped(price);
}

// A weapon or armor option (0x54d838..0x54d958, the table at 0x54dca4): what it does to the price.
enum class OptionEffect
{
    None,
    Skill,      // 0x12..0x18 and 0x38: adds the low dword of P · 1.5f
    Step,       // 0x50, 0x51, 0x53 and 0x55: adds P times the value's step of 4, 8, 12 or 16
    StepByFive, // 0x52: the same with 5, 10, 15 or 20
    Quarter,    // 0x54 and 0x64..0x68: adds P · 0.25
    Double,     // 0x56..0x63 and 0x6e: P · 2
};

OptionEffect EffectOf(std::uint8_t tag)
{
    if ((tag >= 0x12 && tag <= 0x18) || tag == 0x38)
        return OptionEffect::Skill;
    if (tag == 0x50 || tag == 0x51 || tag == 0x53 || tag == ExtraOption)
        return OptionEffect::Step;
    if (tag == StepByFiveOption)
        return OptionEffect::StepByFive;
    if (tag == 0x54 || (tag >= 0x64 && tag <= 0x68))
        return OptionEffect::Quarter;
    if ((tag >= 0x56 && tag <= 0x63) || tag == 0x6e)
        return OptionEffect::Double;
    return OptionEffect::None;
}

// The tables at 0x54dd18, 0x54dd3c and 0x54dd60: a value's step, or none.
std::optional<std::size_t> StepOf(std::uint32_t value, std::uint32_t unit)
{
    if (value < unit || value % unit != 0 || value / unit > StepFactors.size())
        return std::nullopt;
    return static_cast<std::size_t>(value / unit - 1);
}

// 0x54d87d..0x54d8f3: the step. In the wings up to 0x1806, the value of 0x55 goes by 4 (0x54d898).
Core::Expected<std::optional<std::size_t>, std::string>
StepOfTheOption(const Kernel::ItemState& item, std::size_t index, std::uint8_t tag, bool earlyWing)
{
    if (tag == StepByFiveOption)
    {
        auto value = OptionValue(item, index, "0x54d8df");
        if (!value.HasValue())
            return Core::Unexpected(std::move(value).Error());
        return StepOf(value.Value(), StepOfFive);
    }
    auto value = OptionValue(item, index, earlyWing ? "0x54d892" : "0x54d8bb");
    if (!value.HasValue())
        return Core::Unexpected(std::move(value).Error());
    const std::uint32_t shifted = earlyWing && tag == ExtraOption ? std::uint32_t{value.Value()} << ShiftOfExtraOption
                                                                  : std::uint32_t{value.Value()};
    return StepOf(shifted, StepOfFour);
}

Core::Expected<std::int64_t, std::string> AddTheOption(const Kernel::ItemState& item, std::size_t index,
                                                       std::int64_t price, bool earlyWing)
{
    auto tag = OptionByte(item, index, "0x54d83b");
    if (!tag.HasValue())
        return Core::Unexpected(std::move(tag).Error());
    switch (EffectOf(tag.Value()))
    {
    case OptionEffect::None:
        return price;
    case OptionEffect::Double:
        return AllMul(price, 2);
    case OptionEffect::Skill:
    {
        // 0x54d870: ADD ESI, EAX and ADC EDI, 0: only the _ftol's low dword, unsigned (Q-C736).
        auto product = TimesInX87(price, static_cast<double>(SkillFactor), "0x54d862");
        if (!product.HasValue())
            return Core::Unexpected(std::move(product).Error());
        return price + std::int64_t{LowDword(Ftol64(product.Value()))};
    }
    case OptionEffect::Quarter:
    {
        auto product = TimesInX87(price, QuarterFactor, "0x54d93a");
        if (!product.HasValue())
            return Core::Unexpected(std::move(product).Error());
        return price + Ftol64(product.Value());
    }
    case OptionEffect::Step:
    case OptionEffect::StepByFive:
        break;
    }
    auto step = StepOfTheOption(item, index, tag.Value(), earlyWing);
    if (!step.HasValue())
        return Core::Unexpected(std::move(step).Error());
    if (!step.Value().has_value())
        return price;
    auto product = TimesInX87(price, StepFactors[*step.Value()], "0x54d8fa");
    if (!product.HasValue())
        return Core::Unexpected(std::move(product).Error());
    return price + Ftol64(product.Value());
}

// 0x54d762..0x54d95e: the weapons, the armors, the wings up to 0x1806 and the rest without a case.
Core::Expected<TypePrice, std::string> GearPrice(const Kernel::ItemState& item, Record record, std::int32_t group,
                                                 std::int32_t level, std::int32_t base)
{
    if (level >= FirstStepLevel && static_cast<std::size_t>(level - FirstStepLevel) < LevelSteps.size())
        base += LevelSteps[static_cast<std::size_t>(level - FirstStepLevel)];
    const auto side = static_cast<std::uint32_t>(base);
    const std::uint32_t cube = (side + CubeOffset) * side * side;
    const bool earlyWing = group == WingGroup && item.id <= LastEarlyWing;
    // 0x54d7b1: the cape is in group 13 and never gets here (Q-C737).
    const std::int32_t value = earlyWing || item.id == CapeOfLord
                                   ? static_cast<std::int32_t>(cube * WingFactor + WingBase)
                                   : static_cast<std::int32_t>(cube) / GearDivisor + GearBase;
    std::int64_t price = value;
    if (group >= 0 && group <= LastWeaponGroup && ByteAt(record, TwoHandedAt) == 0)
        price = AllDiv(AllMul(price, OneHandedPercent), Percent);
    for (std::size_t i = 0; i < std::size_t{item.optionCount}; ++i)
    {
        auto added = AddTheOption(item, i, price, earlyWing);
        if (!added.HasValue())
            return Core::Unexpected(std::move(added).Error());
        price = added.Value();
    }
    return Capped(price);
}

std::optional<TypePrice> LatePrice(const Kernel::ItemState& item, std::int32_t level)
{
    for (const FirstLevelsPrice& entry : {WizardRingPrices, SpiritPrices})
    {
        if (entry.type == item.id)
            return level <= 1 ? Plain(entry.prices[static_cast<std::size_t>(level)]) : Plain(0);
    }
    return std::nullopt;
}

// 0x54d1b2..0x54da1a: each type's price, before the cap and the mode.
Core::Expected<TypePrice, std::string> PriceByType(const Kernel::ItemState& item, Record record, std::int32_t mode,
                                                   std::int32_t level)
{
    auto base = BaseOf(item, record, level);
    if (!base.HasValue())
        return Core::Unexpected(std::move(base).Error());
    if (item.id == Bolts || item.id == Arrows)
        return AmmoPrice(item, record, level);
    if (item.id == BlessBundle || item.id == SoulBundle)
        return BundlePrice(item, level);
    if (item.id == Dinorant)
        return DinorantPriceOf(item);
    if (auto named = NamedTypePrice(item, level))
        return *named;
    if (ByteAt(record, PotionValueAt) != 0)
        return PotionPrice(item, record, mode, level);
    if (auto late = LatePrice(item, level))
        return *late;
    const std::int32_t group = item.id / GroupSize;
    if ((group == WingGroup && item.id > LastEarlyWing) || group == HelperGroup || group == ScrollGroup)
        return HelperPrice(item, base.Value());
    return GearPrice(item, record, group, level, base.Value());
}

// 0x54da69..0x54db12.
std::int64_t Overridden(const Kernel::ItemState& item, std::int64_t price)
{
    if (item.id < FirstOverridden || item.id > LastOverridden)
        return price;
    const auto index = static_cast<std::size_t>(item.id - FirstOverridden);
    if (index < PerUnitOverrides.size())
        return item.durability * PerUnitOverrides[index];
    return FixedOverrides[index - PerUnitOverrides.size()];
}

// 0x54db15..0x54db3a: 0x422930(1) with the horse and 0x422930(0) with the raven.
Core::Expected<std::int64_t, std::string> PetPrice(const Kernel::ItemState& item,
                                                   const std::optional<PetPriceLevels>& pets, std::int64_t price)
{
    if (item.id != FirstPet && item.id != SecondPet)
        return price;
    if (!pets)
        return Core::Unexpected(std::format("0x54db28: el precio del {:#x} lee el nivel de su mascota (0x422930), que "
                                            "escribe el A9 (P-C126)",
                                            item.id));
    return std::int64_t{PetBasePrice(item.id, *pets)};
}

// 0x54db3d..0x54dbc0: only in mode 1.
bool DiscountsTheDurability(std::uint16_t type, std::int32_t mode)
{
    const auto in = [type](std::uint16_t first, std::uint16_t last) { return type >= first && type <= last; };
    if (type == 0x1a0a || type == WizardRing || type == 0x1a1d || type == Bolts || type == Arrows ||
        type >= FirstPotion || in(0x1807, 0x1813) || in(0x1a0e, 0x1a13) || type == Rena || type == SiegePotion ||
        type == 0x1a07 || type == 0x1a0b)
        return false;
    return mode == InventoryPrice && !in(FirstOverridden, LastOverridden);
}

// 0x54dbc2..0x54dc14: P − the low dword of _ftol((1 − durability / maximum) · P · 0.6f), except for 0x1a1f.
Core::Expected<std::int64_t, std::string> DiscountTheDurability(const Kernel::ItemState& item, Record record,
                                                                std::int32_t level, std::int64_t price)
{
    const std::uint16_t maximum = MaxDurability(item, record, level);
    if (item.id == Spirit)
        return price;
    if (maximum == 0)
    {
        // FIDIV by 0, masked: a NaN (0 / 0, or −infinity from a price at 0) gives 0 in the _ftol (Q-C186); an infinity
        // was not observed (P-C34).
        if (item.durability == 0 || price == 0)
            return price;
        return Core::Unexpected(std::format("0x54dc04: el precio del {:#x} con la durabilidad máxima en 0 llega "
                                            "infinito al _ftol (P-C34)",
                                            item.id));
    }
    const double share = 1.0 - static_cast<double>(item.durability) / static_cast<double>(maximum);
    auto product = TimesInX87(price, share, "0x54dbf9");
    if (!product.HasValue())
        return Core::Unexpected(std::move(product).Error());
    const double discount = product.Value() * static_cast<double>(DurabilityShare);
    return price - std::int64_t{LowDword(Ftol64(discount))};
}

// 0x5b13f2..0x5b143f: 3 · p^(3/4) · (1 − durability / maximum, in float) + 1, times 1.4f without durability; 0 if
// nothing is missing (or with the maximum at 0: the FIDIV gives +infinity or NaN, which do not pass the > 0).
double RepairFee(std::int32_t price, std::int32_t durability, std::int32_t maxDurability)
{
    if (maxDurability == 0)
        return 0.0;
    const double missing = 1.0 - static_cast<double>(durability) / static_cast<double>(maxDurability);
    if (!(missing > 0.0))
        return 0.0;
    // A negative price gives NaN in the FSQRT, and 0x5b145d's _ftol leaves it at 0.
    const double root = std::sqrt(static_cast<double>(price));
    double fee = root * std::sqrt(root);
    fee *= static_cast<double>(RepairRootFactor);
    fee *= static_cast<double>(static_cast<float>(missing));
    fee += 1.0;
    if (durability <= 0)
        fee *= static_cast<double>(BrokenFactor);
    return fee;
}

// 0x5b14a5..0x5b1566: the thousands with "%d,%03d" and its siblings, in unsigned divisions.
std::string GoldText(std::uint32_t gold)
{
    const std::uint32_t units = gold % Thousand;
    const std::uint32_t thousands = gold % Million / Thousand;
    const std::uint32_t millions = gold % Billion / Million;
    if (gold >= Billion)
        return std::format("{},{:03},{:03},{:03}", gold / Billion, millions, thousands, units);
    if (gold >= Million)
        return std::format("{},{:03},{:03}", millions, thousands, units);
    if (gold >= Thousand)
        return std::format("{},{:03}", thousands, units);
    return std::format("{}", units);
}
} // namespace

std::uint32_t PetBasePrice(std::uint16_t type, const PetPriceLevels& pets)
{
    return type == FirstPet ? pets.horse * HorsePricePerLevel : pets.raven * RavenPricePerLevel;
}

Core::Expected<std::uint32_t, std::string> ItemPrice(const Kernel::ItemState& item, const Tables::ItemTable& items,
                                                     std::int32_t mode, const std::optional<PetPriceLevels>& pets)
{
    if (item.id == NoItem)
        return 0u;
    const std::int32_t type = static_cast<std::int16_t>(item.id);
    const auto record = type < 0 ? std::nullopt : items.Record(static_cast<std::size_t>(type));
    if (!record)
        return Core::Unexpected(std::format("0x54d133: el tipo {:#x} lee fuera de Item.bmd", item.id));
    // 0x54d133..0x54d1b1: with a fixed price, it leaves with it (divided by 3 in the modes other than 0), rounded.
    if (const std::int32_t fixed = DwordAt(*record, FixedPriceAt); fixed != 0)
        return RoundThePrice(mode != 0 ? AllDiv(fixed, ModeDivisor) : fixed);
    const std::int32_t level = LevelOf(item);
    auto priced = PriceByType(item, *record, mode, level);
    if (!priced.HasValue())
        return Core::Unexpected(std::move(priced).Error());
    const TypePrice& byType = priced.Value();
    if (byType.returned)
        return *byType.returned;
    std::int64_t price = byType.capped && byType.value >= PriceCap ? PriceCap : byType.value;
    // 0x54da38..0x54da66: the mode, except for the angel and the imp.
    if (mode != 0 && item.id != Angel && item.id != Imp)
        price = AllDiv(price, ModeDivisor);
    auto pet = PetPrice(item, pets, Overridden(item, price));
    if (!pet.HasValue())
        return Core::Unexpected(std::move(pet).Error());
    price = pet.Value();
    if (DiscountsTheDurability(item.id, mode))
    {
        auto discounted = DiscountTheDurability(item, *record, level, price);
        if (!discounted.HasValue())
            return Core::Unexpected(std::move(discounted).Error());
        price = discounted.Value();
    }
    return RoundThePrice(price);
}

RepairPrice RepairPriceOf(std::uint32_t price, std::int32_t durability, std::int32_t maxDurability,
                          std::int32_t field78abb10)
{
    // 0x5b13e3..0x5b13ef: the cap, signed.
    const auto capped = std::min(static_cast<std::int32_t>(price), RepairPriceCap);
    const double fee = RepairFee(capped, durability, maxDurability);
    // 0x5b1441..0x5b145d: + [0x78abb10] (its low byte) · fee · 0.05f, and the _ftol.
    const auto selfRepair = static_cast<double>(field78abb10 & SelfRepairMask);
    double total = selfRepair * fee;
    total *= static_cast<double>(SelfRepairShare);
    total += fee;
    std::int32_t gold = Math::FtolLow(total);
    // 0x5b1464..0x5b14a3: to 100 from 1000 and to 10 from 100, signed.
    if (gold >= RepairHundredsFrom)
        gold = gold / RepairHundred * RepairHundred;
    else if (gold >= RepairTensFrom)
        gold = gold / RepairTen * RepairTen;
    return RepairPrice{.gold = gold, .text = GoldText(static_cast<std::uint32_t>(gold))};
}
} // namespace Client::Items
