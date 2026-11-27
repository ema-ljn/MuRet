#include "Player/OwnStatHelpers.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Kernel/CharacterClass.h"
#include "Tables/ItemTable.h"
#include "Models/LogicalAccess.h"
#include "Actors/FullSetScan.h"
#include "Player/ItemSetOptionList.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Scenes/OwnItemOptions.h"
#include "Kernel/WorldRuntime.h"
#include "Math/Ftol.h"
#include "Kernel/ItemState.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// 0x54efe0 (2,304 bytes), 0x54f9b0 (873), 0x54fd40 (193), 0x54fe10 (2,471), 0x5507e0 (425), 0x550990 (653), 0x550c30
// (46) and 0x550c60 (63), with 0x54ee00 (187), 0x54e680, 0x54e6b0 and 0x54e6d0, checked against the PE c32bbd5f: 06,
// "Los ocho helpers de los stats propios".
namespace Client::Player
{
OwnEquipmentOverride HeldItemOf(const Kernel::WorldRuntime& runtime)
{
    return OwnEquipmentOverride{.dword7879fbc = runtime.cursorItemCount,
                                .window78ab9b8Is78a9bb0 = runtime.cursorGrid == Kernel::CursorGrid::Inventory,
                                .slot78a6758 = static_cast<std::uint32_t>(runtime.slot78a6758),
                                .item7879f80 = runtime.cursorItem};
}

namespace
{
using Kernel::BaseClassOf;
using Kernel::CharacterClass;
using Kernel::ItemState;
using Tables::ItemTable;

constexpr std::uint16_t EmptyItem = 0xffff;

// The places (06, "El equipo propio en el personaje"): the hands, the body parts from 2 to 6 in Kernel::BodyPart's
// order, the wings and the pet. 9 to 11 have no credited name.
constexpr std::size_t RightHandSlot = 0;
constexpr std::size_t LeftHandSlot = 1;
constexpr std::size_t HelmSlot = 2;
constexpr std::size_t ArmorSlot = 3;
constexpr std::size_t PantsSlot = 4;
constexpr std::size_t GlovesSlot = 5;
constexpr std::size_t BootsSlot = 6;
constexpr std::size_t WingsSlot = 7;
constexpr std::size_t PetSlot = 8;
constexpr std::size_t Slot9 = 9;
constexpr std::size_t Slot10 = 10;
constexpr std::size_t Slot11 = 11;

// D+4's level, (D+4 >> 3) & 15, as in 0x54ee00 and 0x54f7f2.
constexpr int LevelShift = 3;
constexpr std::uint32_t LevelMask = 0xf;

// An item's type in Item.bmd: 16 sections of 512 (ItemTable.h). The ASM compares it signed.
constexpr std::int16_t ItemType(int group, int index)
{
    constexpr int ItemsPerGroup = 512;
    return static_cast<std::int16_t>(group * ItemsPerGroup + index);
}

// Item.bmd's record byte that 0x54ee00 gets (movsx of the type, *[0x7758550] + type · 0x54).
constexpr std::size_t BaseDurabilityOffset = 0x2d;

// 0x54ee00 (0x54ee1c..0x54ee56) and its .rdata constants.
constexpr std::uint32_t CheapLevels = 4;
constexpr std::int32_t CheapLevelStep = 1;
constexpr std::int32_t LevelStep = 2;
constexpr std::uint32_t KindModulus = 4;
constexpr std::uint32_t FirstBonusKind = 1;
constexpr std::uint32_t SecondBonusKind = 2;
constexpr std::int32_t KindBonus = 20;
constexpr std::uint8_t ExcellentBits = 0x3f;
constexpr std::int32_t ExcellentBonus = 15;
constexpr float HighWearLimit = 0.8f;    // 0x6b8a38
constexpr float MiddleWearLimit = 0.7f;  // 0x6b8a5c
constexpr float LowWearLimit = 0.5f;     // 0x6b882c
constexpr float HighWearFactor = 0.5f;   // 0x6b882c
constexpr float MiddleWearFactor = 0.3f; // 0x6b89dc
constexpr float LowWearFactor = 0.2f;    // 0x6b8854
constexpr float NoFactor = 0.0f;         // 0x6b86a0

// The selectors of 0x54e540 (adds the options' values) and of 0x54e590 (adds a percentage).
constexpr std::uint32_t Options50 = 0x50;
constexpr std::uint32_t Options51 = 0x51;
constexpr std::uint32_t Options52 = 0x52;
constexpr std::uint32_t Options53 = 0x53;
constexpr std::uint32_t Options5a = 0x5a;
constexpr std::uint32_t Options5d = 0x5d;
constexpr std::uint32_t Options5e = 0x5e;
constexpr std::uint32_t Options5f = 0x5f;
constexpr std::uint32_t Options60 = 0x60;
constexpr std::uint32_t Options61 = 0x61;
constexpr std::uint32_t Options96 = 0x96;
constexpr std::uint32_t SmallPercent = 2;
constexpr std::uint32_t LargePercent = 10;

// The selectors of [0x6eed3c]'s list (0x40d690, 0x40d730 and 0x40d7f0).
constexpr std::uint32_t SetOptions91 = 0x91;
constexpr std::uint32_t SetOptions92 = 0x92;
constexpr std::uint32_t SetOptions93 = 0x93;
constexpr std::uint32_t SetOptions95 = 0x95;
constexpr std::uint32_t SetOptions96 = 0x96;
constexpr std::uint32_t SetOptionsA2 = 0xa2;
constexpr std::uint32_t SetOptionsA4 = 0xa4;
constexpr std::uint32_t SetOptionsA5 = 0xa5;
constexpr std::uint32_t SetOptionsA6 = 0xa6;
constexpr std::uint32_t SetOptionsA7 = 0xa7;
constexpr std::uint32_t SetOptionsA8 = 0xa8;

// A record WORD: the low 16 bits, like the original's mov word and add word.
std::uint16_t Low16(std::int64_t value)
{
    return static_cast<std::uint16_t>(value);
}

void AddTo(std::uint16_t& word, std::int64_t value)
{
    word = Low16(word + value);
}

CharacterClass ClassOf(const Kernel::OwnCharacterRecord& own)
{
    return BaseClassOf(own.characterClass);
}

std::uint32_t LevelOf(const ItemState& item)
{
    return (item.scalar1Low >> LevelShift) & LevelMask;
}

std::int16_t SignedType(const ItemState& item)
{
    return static_cast<std::int16_t>(item.id);
}

bool WornWithDurability(const ItemState& item)
{
    return item.id != EmptyItem && item.durability != 0;
}

// Place `slot`, or 0x7879f80's item if it replaces it. Each helper asks for it only at the places of its jump table.
const ItemState& SlotOrHeld(const Kernel::OwnCharacterRecord& own, const OwnEquipmentOverride& held, std::size_t slot)
{
    const bool replaces = held.dword7879fbc > 0 && held.window78ab9b8Is78a9bb0 && held.slot78a6758 == slot;
    return replaces ? held.item7879f80 : own.equipment[slot];
}

// Byte +0x2d of the type's Item.bmd record, read signed. Outside the table (−1, negative, or 8,192 or more) the
// original reads memory that is not the table's, as in Q-C184.
Core::Expected<std::uint8_t, std::string> BaseDurabilityOf(const ItemState& item, const ItemTable* items,
                                                           std::string_view where)
{
    if (items == nullptr)
    {
        return Core::Unexpected(std::format("{}: pide Item.bmd y no está cargado", where));
    }
    const std::int16_t type = SignedType(item);
    const std::optional<std::span<const std::byte, Tables::ItemRecordSize>> record =
        type < 0 ? std::nullopt : items->Record(static_cast<std::size_t>(type));
    if (!record)
    {
        return Core::Unexpected(
            std::format("{}: el tipo {} lee *[0x7758550] fuera de Item.bmd (como Q-C184)", where, type));
    }
    return std::to_integer<std::uint8_t>((*record)[BaseDurabilityOffset]);
}

// 0x54ee00 as the hands and places 1 to 6 call it, with D+0x1b and D+0x1c unsigned.
float FactorOf(const ItemState& item, std::uint8_t baseDurability)
{
    return ItemDurabilityFactor(item.durability, baseDurability, item.scalar1Low, item.flags1b, item.flags1c);
}

// And as the wings call it: 0 and 0 instead of D+0x1b and D+0x1c.
float WingsFactorOf(const ItemState& item, std::uint8_t baseDurability)
{
    return ItemDurabilityFactor(item.durability, baseDurability, item.scalar1Low, 0, 0);
}

// value − _ftol(value · factor): fild of the unsigned WORD, fmul by 0x54ee00's float and 0x69fd3c. The product, 16 by
// 24 bits, is exact in double.
std::uint16_t Reduced(std::uint16_t value, float factor)
{
    const double product = static_cast<double>(value) * static_cast<double>(factor);
    return Low16(value - Math::FtolLow(product));
}

// 0x54e540 and 0x54e590 read nothing from an item of type −1; with more than nine options they would read past
// ItemState's nine (as in AddCapeOfLordOptions). An error before the first of those reads.
Core::Expected<void, std::string> CheckOptionCount(const ItemState& item, std::string_view where)
{
    if (item.id == EmptyItem || item.optionCount <= Kernel::MaxItemOptions)
    {
        return {};
    }
    return Core::Unexpected(std::format("{}: el tipo {:#x} con {} opciones lee más allá de sus nueve, que es lo "
                                        "que MuRet proyecta",
                                        where, item.id, item.optionCount));
}

std::array<std::byte, 2> LittleEndianWord(std::uint16_t value)
{
    return {static_cast<std::byte>(value & 0xffu), static_cast<std::byte>(value >> 8)};
}

std::uint16_t FromLittleEndian(std::span<const std::byte, 2> bytes)
{
    return static_cast<std::uint16_t>(std::to_integer<unsigned>(bytes[0]) | (std::to_integer<unsigned>(bytes[1]) << 8));
}

// 0x54e540 on a record WORD or a local. Precondition: CheckOptionCount.
void AddOptions(std::uint16_t& word, std::uint32_t selector, const ItemState& item)
{
    std::array<std::byte, 2> bytes = LittleEndianWord(word);
    Scenes::AddOwnItemOptionValues(bytes, selector, {item.id, item.durability, item.optionCount, item.optionBytes});
    word = FromLittleEndian(bytes);
}

// 0x54e590. Precondition: CheckOptionCount.
void ApplyOptionPercent(std::uint16_t& word, std::uint32_t selector, const ItemState& item, std::uint32_t factor)
{
    std::array<std::byte, 2> bytes = LittleEndianWord(word);
    Scenes::ApplyOwnItemOptionPercent(bytes, selector, {item.id, item.durability, item.optionCount, item.optionBytes},
                                      factor);
    word = FromLittleEndian(bytes);
}

// An item's four calls on a WORD pair, in the ASM's order: 0x54e540 with `added` on both and 0x54e590 with `percent`
// on both. Precondition: CheckOptionCount.
void AddOptionsToPair(std::uint16_t& first, std::uint16_t& second, const ItemState& item, std::uint32_t added,
                      std::uint32_t percent, std::uint32_t factor)
{
    AddOptions(first, added, item);
    AddOptions(second, added, item);
    ApplyOptionPercent(first, percent, item, factor);
    ApplyOptionPercent(second, percent, item, factor);
}

Core::Unexpected<std::string> MissingSetOptions(std::string_view where)
{
    return Core::Unexpected(std::format("{}: pide la lista de opciones de conjunto de [0x6eed3c], que arma "
                                        "0x40e1b0, y MuRet no la tiene",
                                        where));
}

// ---- 0x54efe0 ----

struct WordPair
{
    std::uint16_t first = 0;
    std::uint16_t second = 0;
};

// One call to 0x40d7f0: the base and the selector.
struct PercentOfBase
{
    std::uint16_t base = 0;
    std::uint32_t selector = 0;
};

// 0x54f09b..0x54f108: 0x54f28e's branch, with the types signed (jl/jge). In the repo's Item.bmd these are the
// crossbows in the right hand and the bows in the left, with durability.
bool TakesTheArcherBranch(const ItemState& right, const ItemState& left)
{
    const std::int16_t rightType = SignedType(right);
    const std::int16_t leftType = SignedType(left);
    const bool rightRange = (rightType >= ItemType(4, 8) && rightType < ItemType(4, 15)) ||
                            (rightType >= ItemType(4, 16) && rightType < ItemType(5, 0));
    const bool leftRange = (leftType >= ItemType(4, 0) && leftType < ItemType(4, 7)) || leftType == ItemType(4, 17) ||
                           leftType == ItemType(4, 20) || leftType == ItemType(4, 21);
    return (rightRange && right.durability != 0) || (leftRange && left.durability != 0);
}

// 0x54f108..0x54f2f2: +0x50 and +0x52 by class (0x54f904's jump table) or by the crossbows' branch; +0x54 and +0x56
// copy them in every branch.
WordPair BaseOfWords50And52(CharacterClass characterClass, int strength, int agility, int energy, bool archer)
{
    if (archer)
    {
        return {Low16(strength / 14 + agility / 7), Low16((agility >> 2) + (strength >> 3))};
    }
    switch (characterClass)
    {
    case CharacterClass::DarkKnight:
        return {Low16(strength / 6), Low16(strength / 4)};
    case CharacterClass::Elf:
        return {Low16((strength + agility) / 7), Low16((strength + agility) / 4)};
    case CharacterClass::MagicGladiator:
        return {Low16(strength / 6 + energy / 12), Low16(strength / 4 + energy / 8)};
    case CharacterClass::DarkLord:
        return {Low16(strength / 7 + energy / 14), Low16(strength / 5 + energy / 10)};
    default:
        return {Low16(strength >> 3), Low16(strength >> 2)};
    }
}

// 0x54f2f8..0x54f3c0: the wings, with no replacement, with the type and Item.bmd's +0x2d other than 0 (it does not
// look at the durability): their 0x50 options, reduced by 0x54ee00, to the four.
Core::Expected<void, std::string> AddWingsToWords50To56(Kernel::OwnCharacterRecord& own, const ItemTable* items)
{
    const ItemState& wings = own.equipment[WingsSlot];
    if (wings.id == EmptyItem)
    {
        return {};
    }
    const auto base = BaseDurabilityOf(wings, items, "0x54efe0 (0x54f30e), las alas");
    if (!base.HasValue())
    {
        return Core::Unexpected(base.Error());
    }
    if (base.Value() == 0)
    {
        return {};
    }
    const float factor = WingsFactorOf(wings, base.Value());
    if (auto checked = CheckOptionCount(wings, "0x54efe0 (0x54f35d), las alas"); !checked.HasValue())
    {
        return checked;
    }
    WordPair options;
    AddOptions(options.first, Options50, wings);
    AddOptions(options.second, Options50, wings);
    const WordPair reduced{Reduced(options.first, factor), Reduced(options.second, factor)};
    AddTo(own.word50, reduced.first);
    AddTo(own.word52, reduced.second);
    AddTo(own.word54, reduced.first);
    AddTo(own.word56, reduced.second);
    return {};
}

// 0x54f3c3..0x54f477 and 0x54f4e9..0x54f59e: a hand's D+0xc and D+0xe, plus its 0x50 options, reduced by 0x54ee00.
Core::Expected<WordPair, std::string> HandWords(const ItemState& hand, const ItemTable* items, std::string_view where)
{
    const auto base = BaseDurabilityOf(hand, items, where);
    if (!base.HasValue())
    {
        return Core::Unexpected(base.Error());
    }
    const float factor = FactorOf(hand, base.Value());
    if (auto checked = CheckOptionCount(hand, where); !checked.HasValue())
    {
        return Core::Unexpected(checked.Error());
    }
    WordPair words{hand.word0c, hand.word0e};
    AddOptions(words.first, Options50, hand);
    AddOptions(words.second, Options50, hand);
    return WordPair{Reduced(words.first, factor), Reduced(words.second, factor)};
}

// 0x54f3c3..0x54f4e6: the right hand goes to +0x50 and +0x52, except with a type from 0xa00 to 0xc00 (signed, jl and
// jg), which goes to +0x54 and +0x56; its 0x5d and 0x5e options, always to +0x50 and +0x52.
Core::Expected<void, std::string> AddRightHandToWords50To56(Kernel::OwnCharacterRecord& own, const ItemState& right,
                                                            const ItemTable* items)
{
    if (!WornWithDurability(right))
    {
        return {};
    }
    const auto words = HandWords(right, items, "0x54efe0 (0x54f3e1), la mano derecha");
    if (!words.HasValue())
    {
        return Core::Unexpected(words.Error());
    }
    const std::int16_t type = SignedType(right);
    const bool toSecondPair = type >= ItemType(5, 0) && type <= ItemType(6, 0);
    AddTo(toSecondPair ? own.word54 : own.word50, words.Value().first);
    AddTo(toSecondPair ? own.word56 : own.word52, words.Value().second);
    AddOptionsToPair(own.word50, own.word52, right, Options5d, Options5e, SmallPercent);
    return {};
}

// 0x54f4e9..0x54f5ec: the left, to +0x54 and +0x56 (+0x56 first), with its options.
Core::Expected<void, std::string> AddLeftHandToWords50To56(Kernel::OwnCharacterRecord& own, const ItemState& left,
                                                           const ItemTable* items)
{
    if (!WornWithDurability(left))
    {
        return {};
    }
    const auto words = HandWords(left, items, "0x54efe0 (0x54f507), la mano izquierda");
    if (!words.HasValue())
    {
        return Core::Unexpected(words.Error());
    }
    AddTo(own.word56, words.Value().second);
    AddTo(own.word54, words.Value().first);
    AddOptionsToPair(own.word54, own.word56, left, Options5d, Options5e, SmallPercent);
    return {};
}

// 0x54f5ef..0x54f703: place 9 (options 0x5d and 0x5e by 2 to both pairs) and places 10 and 11 (0x5e by 10 to the
// four).
Core::Expected<void, std::string> AddAccessoriesToWords50To56(Kernel::OwnCharacterRecord& own, const ItemState& slot9,
                                                              const ItemState& slot10, const ItemState& slot11)
{
    if (WornWithDurability(slot9))
    {
        if (auto checked = CheckOptionCount(slot9, "0x54efe0 (0x54f5ff), el lugar 9"); !checked.HasValue())
        {
            return checked;
        }
        AddOptionsToPair(own.word50, own.word52, slot9, Options5d, Options5e, SmallPercent);
        AddOptionsToPair(own.word54, own.word56, slot9, Options5d, Options5e, SmallPercent);
    }
    for (const ItemState* ring : {&slot10, &slot11})
    {
        if (!WornWithDurability(*ring))
        {
            continue;
        }
        if (auto checked = CheckOptionCount(*ring, "0x54efe0 (0x54f680), los lugares 10 y 11"); !checked.HasValue())
        {
            return checked;
        }
        for (std::uint16_t* word : {&own.word50, &own.word52, &own.word54, &own.word56})
        {
            ApplyOptionPercent(*word, Options5e, *ring, LargePercent);
        }
    }
    return {};
}

// 0x54f706..0x54f7b3: 0x40d690 with 0x91 to +0x54 and +0x50, with 0x92 to +0x56 and +0x52, and 0x40d7f0 with the
// agility (0xa5) and with the strength (0xa4) to the four; each on a local at 0.
Core::Expected<void, std::string> AddSetOptionsToWords50To56(Kernel::OwnCharacterRecord& own,
                                                             ItemSetOptionList* setOptions, std::uint16_t strength,
                                                             std::uint16_t agility)
{
    if (setOptions == nullptr)
    {
        return MissingSetOptions("0x54efe0 (0x40d690 en 0x54f71c)");
    }
    std::uint16_t first = 0;
    if (auto added = AddItemSetOptionValues(*setOptions, first, SetOptions91); !added.HasValue())
    {
        return added;
    }
    AddTo(own.word54, first);
    AddTo(own.word50, first);
    std::uint16_t second = 0;
    if (auto added = AddItemSetOptionValues(*setOptions, second, SetOptions92); !added.HasValue())
    {
        return added;
    }
    AddTo(own.word56, second);
    AddTo(own.word52, second);
    for (const auto& [base, selector] : {PercentOfBase{agility, SetOptionsA5}, PercentOfBase{strength, SetOptionsA4}})
    {
        std::uint16_t share = 0;
        if (auto added = AddItemSetOptionPercentOf(*setOptions, share, base, selector); !added.HasValue())
        {
            return added;
        }
        for (std::uint16_t* word : {&own.word50, &own.word54, &own.word52, &own.word56})
        {
            AddTo(*word, share);
        }
    }
    return {};
}

// word + _ftol(word · (2 · level + 1) · 0.01f + 1.0f) (0x54f80a..0x54f8d6). All exact in double: the multiplier has 5
// by 24 bits, the product 16 more and the 1 stays under 2^-30.
std::uint16_t WithAmmunition(std::uint16_t word, std::uint32_t level)
{
    constexpr float AmmunitionStep = 0.01f; // 0x6b88d4
    constexpr double One = 1.0;             // 0x6b87d4, 1.0f
    const double multiplier = static_cast<double>(2 * level + 1) * static_cast<double>(AmmunitionStep);
    return Low16(word + Math::FtolLow(static_cast<double>(word) * multiplier + One));
}

// 0x54f7b0..0x54f8d6: with both hands from 0x800 to 0x9ff (signed), a Bolt with a level on the left adjusts +0x50 and
// +0x52 and returns; otherwise an Arrow with a level on the right adjusts +0x54 and +0x56.
void AddAmmunition(Kernel::OwnCharacterRecord& own, const ItemState& right, const ItemState& left)
{
    const auto inBowGroup = [](const ItemState& item)
    { return SignedType(item) >= ItemType(4, 0) && SignedType(item) < ItemType(5, 0); };
    if (!inBowGroup(right) || !inBowGroup(left))
    {
        return;
    }
    if (left.id == static_cast<std::uint16_t>(ItemType(4, 7)) && LevelOf(left) >= 1)
    {
        own.word50 = WithAmmunition(own.word50, LevelOf(left));
        own.word52 = WithAmmunition(own.word52, LevelOf(left));
        return;
    }
    if (right.id == static_cast<std::uint16_t>(ItemType(4, 15)) && LevelOf(right) >= 1)
    {
        own.word54 = WithAmmunition(own.word54, LevelOf(right));
        own.word56 = WithAmmunition(own.word56, LevelOf(right));
    }
}

// ---- 0x54f9b0 ----

// 0x54fa5e..0x54fb22: the wings, with no replacement, with the type (it does not look at the durability or +0x2d):
// their 0x51 options, reduced, to +0x5a and +0x5c.
Core::Expected<void, std::string> AddWingsToWords5aAnd5c(Kernel::OwnCharacterRecord& own, const ItemTable* items)
{
    const ItemState& wings = own.equipment[WingsSlot];
    if (wings.id == EmptyItem)
    {
        return {};
    }
    const auto base = BaseDurabilityOf(wings, items, "0x54f9b0 (0x54fa79), las alas");
    if (!base.HasValue())
    {
        return Core::Unexpected(base.Error());
    }
    const float factor = WingsFactorOf(wings, base.Value());
    if (auto checked = CheckOptionCount(wings, "0x54f9b0 (0x54fac1), las alas"); !checked.HasValue())
    {
        return checked;
    }
    WordPair options;
    AddOptions(options.first, Options51, wings);
    AddOptions(options.second, Options51, wings);
    AddTo(own.word5a, Reduced(options.first, factor));
    AddTo(own.word5c, Reduced(options.second, factor));
    return {};
}

// 0x54fb28..0x54fc2a: the right hand adds its 0x51 options (0x50 with 0x15, 0x17 and 0x1f, the 0 21, the 0 23 and the
// 0 31), reduced, and then 0x5f and 0x60 by 2.
Core::Expected<void, std::string> AddRightHandToWords5aAnd5c(Kernel::OwnCharacterRecord& own, const ItemState& right,
                                                             const ItemTable* items)
{
    if (!WornWithDurability(right))
    {
        return {};
    }
    const auto base = BaseDurabilityOf(right, items, "0x54f9b0 (0x54fb40), la mano derecha");
    if (!base.HasValue())
    {
        return Core::Unexpected(base.Error());
    }
    const float factor = FactorOf(right, base.Value());
    if (auto checked = CheckOptionCount(right, "0x54f9b0 (0x54fb9c), la mano derecha"); !checked.HasValue())
    {
        return checked;
    }
    const std::int16_t type = SignedType(right);
    const bool usesOptions50 = type == ItemType(0, 21) || type == ItemType(0, 31) || type == ItemType(0, 23);
    const std::uint32_t selector = usesOptions50 ? Options50 : Options51;
    WordPair options;
    AddOptions(options.first, selector, right);
    AddOptions(options.second, selector, right);
    AddTo(own.word5a, Reduced(options.first, factor));
    AddTo(own.word5c, Reduced(options.second, factor));
    AddOptionsToPair(own.word5a, own.word5c, right, Options5f, Options60, SmallPercent);
    return {};
}

// 0x54fc30..0x54fccc: place 9 (0x5f and 0x60 by 2) and places 10 and 11 (0x60 by 10).
Core::Expected<void, std::string> AddAccessoriesToWords5aAnd5c(Kernel::OwnCharacterRecord& own, const ItemState& slot9,
                                                               const ItemState& slot10, const ItemState& slot11)
{
    if (WornWithDurability(slot9))
    {
        if (auto checked = CheckOptionCount(slot9, "0x54f9b0 (0x54fc40), el lugar 9"); !checked.HasValue())
        {
            return checked;
        }
        AddOptionsToPair(own.word5a, own.word5c, slot9, Options5f, Options60, SmallPercent);
    }
    for (const ItemState* ring : {&slot10, &slot11})
    {
        if (!WornWithDurability(*ring))
        {
            continue;
        }
        if (auto checked = CheckOptionCount(*ring, "0x54f9b0 (0x54fc81), los lugares 10 y 11"); !checked.HasValue())
        {
            return checked;
        }
        ApplyOptionPercent(own.word5a, Options60, *ring, LargePercent);
        ApplyOptionPercent(own.word5c, Options60, *ring, LargePercent);
    }
    return {};
}

// 0x54fccf..0x54fd0f: 0x40d690 adds to +0xef4 without zeroing it, which goes to +0x5a and +0x5c; then +0xef4 at 0,
// 0x40d7f0 with the energy, and again to both. +0xef4 is left with 0x40d7f0's.
Core::Expected<void, std::string> AddSetOptionsToWords5aAnd5c(Kernel::OwnCharacterRecord& own,
                                                              ItemSetOptionList* setOptions, std::uint16_t energy)
{
    if (setOptions == nullptr)
    {
        return MissingSetOptions("0x54f9b0 (0x40d690 en 0x54fce1)");
    }
    if (auto added = AddItemSetOptionValues(*setOptions, own.wordef4, SetOptions93); !added.HasValue())
    {
        return added;
    }
    AddTo(own.word5a, own.wordef4);
    AddTo(own.word5c, own.wordef4);
    own.wordef4 = 0;
    if (auto added = AddItemSetOptionPercentOf(*setOptions, own.wordef4, energy, SetOptionsA8); !added.HasValue())
    {
        return added;
    }
    AddTo(own.word5a, own.wordef4);
    AddTo(own.word5c, own.wordef4);
    return {};
}

// ---- 0x54fe10 ----

// 0x54fe1e..0x54fef8: the agility divisors (plus +0x2c) for +0x4c and +0x58, by class.
WordPair BaseSpeeds(CharacterClass characterClass, int agility)
{
    switch (characterClass)
    {
    case CharacterClass::Elf:
        return {Low16(agility / 50), Low16(agility / 50)};
    case CharacterClass::DarkKnight:
    case CharacterClass::MagicGladiator:
        return {Low16(agility / 15), Low16(agility / 20)};
    case CharacterClass::DarkLord:
        return {Low16(agility / 10), Low16(agility / 10)};
    default:
        return {Low16(agility / 20), Low16(agility / 10)};
    }
}

void AddToSpeeds(Kernel::OwnCharacterRecord& own, std::int64_t value)
{
    AddTo(own.attackSpeed, value);
    AddTo(own.magicSpeed, value);
}

// 0x54ff78..0x54ffbf: a weapon counts with a type from 0 to 0xbff (signed), except Bolt and Arrow.
bool CountsForSpeed(const ItemState& hand)
{
    const std::int16_t type = SignedType(hand);
    return type != ItemType(4, 7) && type != ItemType(4, 15) && type >= 0 && type < ItemType(6, 0);
}

// 0x55001a..0x550072: each hand's D+0x17 (0 without durability); with both counting, the average.
void AddWeaponSpeeds(Kernel::OwnCharacterRecord& own, const ItemState& right, const ItemState& left)
{
    const int rightSpeed = right.durability > 0 ? right.byte17 : 0;
    const int leftSpeed = left.durability > 0 ? left.byte17 : 0;
    if (CountsForSpeed(right))
    {
        AddToSpeeds(own, CountsForSpeed(left) ? (rightSpeed + leftSpeed) / 2 : rightSpeed);
        return;
    }
    if (CountsForSpeed(left))
    {
        AddToSpeeds(own, leftSpeed);
    }
}

// 0x54ffd4..0x550016: Wizard's Ring (0x1a14, the 13 20) with durability and (D+4 >> 3), signed and without the level
// mask, at 0 or 3.
bool IsSpeedRing(const ItemState& ring)
{
    constexpr std::int32_t PlainRing = 0;
    constexpr std::int32_t ThirdLevelRing = 3;
    if (ring.durability == 0 || SignedType(ring) != ItemType(13, 20))
    {
        return false;
    }
    const std::int32_t shifted = std::bit_cast<std::int32_t>(ring.scalar1Low) >> LevelShift;
    return shifted == PlainRing || shifted == ThirdLevelRing;
}

// 0x550075..0x5506fb: with the ring in place 11 or, otherwise, in 10, 10 more to both speeds and bit 0x04 of +0x3c
// set; with neither, and with the bit set, the bit cleared. Each change of the bit goes between an opening and a
// closing of the record (inline; the last closing, with its own rand() loop). The bit is read before opening: with
// the record closed the original would read the encrypted byte.
void UpdateRingSpeeds(Kernel::OwnCharacterRecord& own, const ItemState& slot10, const ItemState& slot11,
                      std::uint8_t& ownRecordAccesses, Crt::CrtRandom& random)
{
    constexpr std::int64_t RingBonus = 10;
    constexpr std::uint8_t RingBit = 0x04;
    constexpr std::uint8_t AllButRingBit = 0xfb;
    if (IsSpeedRing(slot11) || IsSpeedRing(slot10))
    {
        AddToSpeeds(own, RingBonus);
        const Models::LogicalAccess access(ownRecordAccesses, random, Models::OwnRecordCloseRandoms);
        own.speedAdjustBits = static_cast<std::uint8_t>(own.speedAdjustBits | RingBit);
        return;
    }
    if ((own.speedAdjustBits & RingBit) == 0)
    {
        return;
    }
    const Models::LogicalAccess access(ownRecordAccesses, random, Models::OwnRecordCloseRandoms);
    own.speedAdjustBits = static_cast<std::uint8_t>(own.speedAdjustBits & AllButRingBit);
}

// 0x55073b..0x5507ad: the 0x61 options of the record's places 0, 1, 9 and 8, with no replacement, to both speeds.
Core::Expected<void, std::string> AddSpeedOptions(Kernel::OwnCharacterRecord& own)
{
    for (const std::size_t slot : {RightHandSlot, LeftHandSlot, Slot9, PetSlot})
    {
        const ItemState& item = own.equipment[slot];
        if (auto checked = CheckOptionCount(item, std::format("0x54fe10 (0x54e540 con 0x61), el lugar {}", slot));
            !checked.HasValue())
        {
            return checked;
        }
        AddOptions(own.attackSpeed, Options61, item);
        AddOptions(own.magicSpeed, Options61, item);
    }
    return {};
}

// ---- 0x550990 ----

// 0x5509b6..0x550a3d: the agility divisor (plus +0x2c) for +0x62.
std::uint16_t BaseOfWord62(CharacterClass characterClass, int agility)
{
    switch (characterClass)
    {
    case CharacterClass::Elf:
        return Low16(agility / 10);
    case CharacterClass::DarkKnight:
        return Low16(agility / 3);
    case CharacterClass::DarkWizard:
        return Low16(agility >> 2);
    case CharacterClass::DarkLord:
        return Low16(agility / 7);
    default:
        return Low16(agility / 5);
    }
}

// 0x54e680: an item's D+0x12 plus its 0x53 options, or 0 with type −1. Precondition: CheckOptionCount.
std::uint16_t Word12WithOptions(const ItemState& item)
{
    if (item.id == EmptyItem)
    {
        return 0;
    }
    std::uint16_t value = item.word12;
    AddOptions(value, Options53, item);
    return value;
}

// 0x550a40..0x550b1b: for each place from 1 to 7 with durability, 0x54e680 reduced by 0x54ee00 (the wings, with 0 and
// 0), added in 32 bits; +0x62 adds the low 16. With type −1 the WORD's share is 0: 0x54e680 returns AX=0 and every
// factor 0x54ee00 returns is finite.
Core::Expected<void, std::string> AddSlotsToWord62(Kernel::OwnCharacterRecord& own, const ItemTable* items)
{
    std::uint32_t sum = 0;
    for (std::size_t slot = LeftHandSlot; slot <= WingsSlot; ++slot)
    {
        const ItemState& item = own.equipment[slot];
        if (item.durability == 0 || item.id == EmptyItem)
        {
            continue;
        }
        const std::string where = std::format("0x550990 (0x550a6c), el lugar {}", slot);
        if (auto checked = CheckOptionCount(item, where); !checked.HasValue())
        {
            return checked;
        }
        const std::uint16_t value = Word12WithOptions(item);
        const auto base = BaseDurabilityOf(item, items, where);
        if (!base.HasValue())
        {
            return Core::Unexpected(base.Error());
        }
        const float factor = slot == WingsSlot ? WingsFactorOf(item, base.Value()) : FactorOf(item, base.Value());
        sum += Reduced(value, factor);
    }
    AddTo(own.word62, sum);
    return {};
}

// 0x550b1e..0x550b83: with [0x743a17c] other than 0, +0x62 adds _ftol(+0x62 · f), with f by [0x743a178] (0x550c20's
// jump table): 0.05f, 0.1f, 0.15f or 0.2f from 10 to 13, and 0 otherwise.
void AddScanToWord62(Kernel::OwnCharacterRecord& own, const Actors::Scan505820& scan)
{
    constexpr std::uint32_t FirstScanLevel = 10;
    constexpr std::array<float, 4> ScanFactors{0.05f, 0.1f, 0.15f, 0.2f}; // 0x6b8a48, 0x6b87b8, 0x6b8ae8, 0x6b8854
    if (scan.byte743a17c == 0)
    {
        return;
    }
    // add eax, −10 and ja with 3: the subtraction wraps and compares unsigned.
    const std::uint32_t index = std::bit_cast<std::uint32_t>(scan.dword743a178) - FirstScanLevel;
    const float factor = index < ScanFactors.size() ? ScanFactors[index] : NoFactor;
    const double product = static_cast<double>(own.word62) * static_cast<double>(factor);
    own.word62 = Low16(own.word62 + Math::FtolLow(product));
}

// 0x550b86..0x550be1: 0x40d690 with 0x96, 0x40d7f0 with the agility (0xa6) and with the vitality (0xa7) and, with a
// type from 0xc00 to 0xdff (signed) in place 1, 0x40d730 with 0xa2.
Core::Expected<void, std::string> AddSetOptionsToWord62(Kernel::OwnCharacterRecord& own, ItemSetOptionList* setOptions,
                                                        std::uint16_t agility, std::uint16_t vitality)
{
    if (setOptions == nullptr)
    {
        return MissingSetOptions("0x550990 (0x40d690 en 0x550b92)");
    }
    if (auto added = AddItemSetOptionValues(*setOptions, own.word62, SetOptions96); !added.HasValue())
    {
        return added;
    }
    for (const auto& [base, selector] : {PercentOfBase{agility, SetOptionsA6}, PercentOfBase{vitality, SetOptionsA7}})
    {
        if (auto added = AddItemSetOptionPercentOf(*setOptions, own.word62, base, selector); !added.HasValue())
        {
            return added;
        }
    }
    const std::int16_t type = SignedType(own.equipment[LeftHandSlot]);
    if (type < ItemType(6, 0) || type >= ItemType(7, 0))
    {
        return {};
    }
    return ApplyItemSetOptionPercent(*setOptions, own.word62, SetOptionsA2);
}

// 0x550be6..0x550c14: place 8's 0x96 options and, if it is Cape of Lord (0x1a1e), place 7's.
Core::Expected<void, std::string> AddPetAndCapeToWord62(Kernel::OwnCharacterRecord& own)
{
    const ItemState& pet = own.equipment[PetSlot];
    if (auto checked = CheckOptionCount(pet, "0x550990 (0x550bf3), el lugar 8"); !checked.HasValue())
    {
        return checked;
    }
    AddOptions(own.word62, Options96, pet);
    const ItemState& wings = own.equipment[WingsSlot];
    if (wings.id != static_cast<std::uint16_t>(ItemType(13, 30)))
    {
        return {};
    }
    if (auto checked = CheckOptionCount(wings, "0x550990 (0x550c0f), Cape of Lord"); !checked.HasValue())
    {
        return checked;
    }
    AddOptions(own.word62, Options96, wings);
    return {};
}
} // namespace

float ItemDurabilityFactor(std::uint8_t durability, std::uint8_t baseDurability, std::uint32_t scalar1,
                           std::uint8_t flags1b, std::uint8_t flags1c)
{
    std::int32_t maximum = baseDurability;
    const std::uint32_t level = (scalar1 >> LevelShift) & LevelMask;
    for (std::uint32_t step = 0; step < level; ++step)
    {
        maximum += step < CheapLevels ? CheapLevelStep : LevelStep;
    }
    // and 0x80000003 with the unsigned byte: the remainder, 0 to 3.
    const std::uint32_t kind = flags1c % KindModulus;
    if (kind == FirstBonusKind || kind == SecondBonusKind)
    {
        maximum += KindBonus;
    }
    else if ((flags1b & ExcellentBits) != 0)
    {
        maximum += ExcellentBonus;
    }
    if (maximum == 0)
    {
        return NoFactor;
    }
    // fild, fidiv and fsubr of 1.0f: each rounds to 53 bits, as in double.
    const double remaining = 1.0 - static_cast<double>(durability) / static_cast<double>(maximum);
    if (remaining > static_cast<double>(HighWearLimit))
    {
        return HighWearFactor;
    }
    if (remaining > static_cast<double>(MiddleWearLimit))
    {
        return MiddleWearFactor;
    }
    if (remaining > static_cast<double>(LowWearLimit))
    {
        return LowWearFactor;
    }
    return NoFactor;
}

Core::Expected<void, std::string> UpdateOwnWords50To56(Kernel::OwnCharacterRecord& own,
                                                       const OwnEquipmentOverride& held, const ItemTable* items,
                                                       ItemSetOptionList* setOptions)
{
    const ItemState& right = SlotOrHeld(own, held, RightHandSlot);
    const ItemState& left = SlotOrHeld(own, held, LeftHandSlot);
    const std::uint16_t strength = Low16(own.word2a + own.strength);
    const std::uint16_t agility = Low16(own.word2c + own.agility);
    const std::uint16_t energy = Low16(own.word30 + own.energy);
    const WordPair base =
        BaseOfWords50And52(ClassOf(own), strength, agility, energy, TakesTheArcherBranch(right, left));
    own.word50 = base.first;
    own.word52 = base.second;
    own.word54 = base.first;
    own.word56 = base.second;
    if (auto wings = AddWingsToWords50To56(own, items); !wings.HasValue())
    {
        return wings;
    }
    if (auto hand = AddRightHandToWords50To56(own, right, items); !hand.HasValue())
    {
        return hand;
    }
    if (auto hand = AddLeftHandToWords50To56(own, left, items); !hand.HasValue())
    {
        return hand;
    }
    if (auto accessories = AddAccessoriesToWords50To56(own, SlotOrHeld(own, held, Slot9), SlotOrHeld(own, held, Slot10),
                                                       SlotOrHeld(own, held, Slot11));
        !accessories.HasValue())
    {
        return accessories;
    }
    if (auto added = AddSetOptionsToWords50To56(own, setOptions, strength, agility); !added.HasValue())
    {
        return added;
    }
    AddAmmunition(own, right, left);
    return {};
}

Core::Expected<void, std::string> UpdateOwnWords5aAnd5c(Kernel::OwnCharacterRecord& own,
                                                        const OwnEquipmentOverride& held, const ItemTable* items,
                                                        ItemSetOptionList* setOptions)
{
    constexpr int EnergyDivisor = 9;
    constexpr int EnergyShift = 2;
    const std::uint16_t energy = Low16(own.word30 + own.energy);
    own.word5a = Low16(energy / EnergyDivisor);
    own.word5c = Low16(energy >> EnergyShift);
    if (auto wings = AddWingsToWords5aAnd5c(own, items); !wings.HasValue())
    {
        return wings;
    }
    if (auto hand = AddRightHandToWords5aAnd5c(own, SlotOrHeld(own, held, RightHandSlot), items); !hand.HasValue())
    {
        return hand;
    }
    if (auto accessories = AddAccessoriesToWords5aAnd5c(own, SlotOrHeld(own, held, Slot9),
                                                        SlotOrHeld(own, held, Slot10), SlotOrHeld(own, held, Slot11));
        !accessories.HasValue())
    {
        return accessories;
    }
    return AddSetOptionsToWords5aAnd5c(own, setOptions, energy);
}

Core::Expected<void, std::string> UpdateOwnWord4e(Kernel::OwnCharacterRecord& own, ItemSetOptionList* setOptions)
{
    const int agility = Low16(own.word2c + own.agility);
    const int strength = Low16(own.word2a + own.strength);
    // imul cx, cx, 5: the level times 5 in 16 bits, which is added in 16 anyway.
    const int levelTerm = own.level * 5;
    if (ClassOf(own) == CharacterClass::DarkLord)
    {
        const int leadership = Low16(own.word36 + own.leadership);
        own.word4e = Low16(strength / 6 + leadership / 10 + agility * 5 / 2 + levelTerm);
    }
    else
    {
        own.word4e = Low16(agility * 3 / 2 + levelTerm + (strength >> 2));
    }
    if (setOptions == nullptr)
    {
        return MissingSetOptions("0x54fd40 (0x40d690 en 0x54fdf5)");
    }
    return AddItemSetOptionValues(*setOptions, own.word4e, SetOptions95);
}

Core::Expected<void, std::string> UpdateOwnAttackAndMagicSpeeds(Kernel::OwnCharacterRecord& own,
                                                                const OwnEquipmentOverride& held,
                                                                std::uint8_t& ownRecordAccesses, Crt::CrtRandom& random)
{
    constexpr std::uint8_t BonusBits = 0x01 | 0x08;
    constexpr std::int64_t BitsBonus = 20;
    const WordPair base = BaseSpeeds(ClassOf(own), Low16(own.word2c + own.agility));
    own.attackSpeed = base.first;
    own.magicSpeed = base.second;
    AddWeaponSpeeds(own, SlotOrHeld(own, held, RightHandSlot), SlotOrHeld(own, held, LeftHandSlot));
    UpdateRingSpeeds(own, SlotOrHeld(own, held, Slot10), SlotOrHeld(own, held, Slot11), ownRecordAccesses, random);
    const ItemState& gloves = SlotOrHeld(own, held, GlovesSlot);
    if (gloves.durability != 0 && gloves.id != EmptyItem)
    {
        AddToSpeeds(own, gloves.byte17);
    }
    if ((own.speedAdjustBits & BonusBits) != 0)
    {
        AddToSpeeds(own, BitsBonus);
    }
    return AddSpeedOptions(own);
}

Core::Expected<void, std::string> UpdateOwnWord60(Kernel::OwnCharacterRecord& own, const ItemTable* items)
{
    const int agility = Low16(own.word2c + own.agility);
    const CharacterClass characterClass = ClassOf(own);
    own.word60 = characterClass == CharacterClass::Elf        ? Low16(agility >> 2)
                 : characterClass == CharacterClass::DarkLord ? Low16(agility / 7)
                                                              : Low16(agility / 3);
    const ItemState& left = own.equipment[LeftHandSlot];
    if (WornWithDurability(left))
    {
        const auto base = BaseDurabilityOf(left, items, "0x5507e0 (0x55086b), el lugar 1");
        if (!base.HasValue())
        {
            return Core::Unexpected(base.Error());
        }
        const float factor = FactorOf(left, base.Value());
        AddTo(own.word60, Reduced(left.byte10, factor));
        if (auto checked = CheckOptionCount(left, "0x5507e0 (0x5508d1), el lugar 1"); !checked.HasValue())
        {
            return checked;
        }
        std::uint16_t options = 0;
        AddOptions(options, Options52, left);
        AddTo(own.word60, Reduced(options, factor));
        ApplyOptionPercent(own.word60, Options5a, left, LargePercent);
    }
    for (const std::size_t slot : {HelmSlot, ArmorSlot, PantsSlot, GlovesSlot, BootsSlot, Slot11, Slot10})
    {
        const ItemState& item = own.equipment[slot];
        if (auto checked = CheckOptionCount(item, std::format("0x5507e0 (0x54e590), el lugar {}", slot));
            !checked.HasValue())
        {
            return checked;
        }
        ApplyOptionPercent(own.word60, Options5a, item, LargePercent);
    }
    return {};
}

Core::Expected<void, std::string> UpdateOwnWord62(Kernel::OwnCharacterRecord& own, const ItemTable* items,
                                                  const Actors::Scan505820& scan, ItemSetOptionList* setOptions)
{
    const std::uint16_t vitality = Low16(own.word2e + own.vitality);
    const std::uint16_t agility = Low16(own.word2c + own.agility);
    own.word62 = BaseOfWord62(ClassOf(own), agility);
    if (auto slots = AddSlotsToWord62(own, items); !slots.HasValue())
    {
        return slots;
    }
    AddScanToWord62(own, scan);
    if (auto added = AddSetOptionsToWord62(own, setOptions, agility, vitality); !added.HasValue())
    {
        return added;
    }
    return AddPetAndCapeToWord62(own);
}

void UpdateOwnWord64(Kernel::OwnCharacterRecord& own)
{
    for (std::size_t slot = HelmSlot; slot <= WingsSlot; ++slot)
    {
        const ItemState& item = own.equipment[slot];
        if (item.durability == 0)
        {
            continue;
        }
        // 0x54e6b0: D+0x14, or 0 with type −1.
        own.word64 = item.id == EmptyItem ? std::uint16_t{0} : item.word14;
    }
}

void UpdateOwnWord66(Kernel::OwnCharacterRecord& own)
{
    // 0x54e6d0: D+0x18, or 0 with type −1.
    const auto word18Of = [](const ItemState& item) { return item.id == EmptyItem ? std::uint16_t{0} : item.word18; };
    const ItemState& boots = own.equipment[BootsSlot];
    if (boots.durability != 0)
    {
        own.word66 = word18Of(boots);
    }
    const ItemState& wings = own.equipment[WingsSlot];
    if (wings.durability != 0)
    {
        AddTo(own.word66, word18Of(wings));
    }
}
} // namespace Client::Player
