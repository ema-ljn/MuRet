#include "Player/OwnDerivedStats.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/ItemTable.h"
#include "Models/LogicalAccess.h"
#include "Actors/FullSetScan.h"
#include "Player/ItemSetOptionList.h"
#include "Player/ItemSetState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Items/OwnItemOptions.h"
#include "Player/OwnItemSets.h"
#include "Player/OwnStatHelpers.h"
#include "Kernel/ItemState.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>

// 0x550d10, checked against the PE c32bbd5f (1,698 bytes), with 0x54ef00 (31) and 0x40dc50 (66): 06, "Los stats del
// registro propio". 0x40e1b0 and 0x40da20 are in Player/OwnItemSets.cpp.
namespace Client::Player
{
namespace
{
// Item.bmd's record byte that 0x40dc50 returns (0x40dc88).
constexpr std::size_t ItemStatKindOffset = 0x44;
// (D+0x1c >> 2) & 3 (0x40dc58..0x40dc6e).
constexpr int ItemStatValueShift = 2;
constexpr int ItemStatValueMask = 3;
// lea eax, [eax + eax·4] before each sum (0x550d82).
constexpr int ItemStatValueFactor = 5;
// The 0x40dc50 types that add, by 0x5513b4's jump table, with each one's WORD.
constexpr std::uint8_t KindOfWord2a = 1;
constexpr std::uint8_t KindOfWord2c = 2;
constexpr std::uint8_t KindOfWord30 = 3;
constexpr std::uint8_t KindOfWord2e = 4;

// 0x40da20's selectors for each WORD (0x550db3..0x550e12): the strength, the agility, the energy, the vitality and
// +0x36.
constexpr std::uint32_t SetOptionSelectorOfWord2a = 0x8c;
constexpr std::uint32_t SetOptionSelectorOfWord2c = 0x8d;
constexpr std::uint32_t SetOptionSelectorOfWord30 = 0x8e;
constexpr std::uint32_t SetOptionSelectorOfWord2e = 0x8f;
constexpr std::uint32_t SetOptionSelectorOfWord36 = 0x90;

// The wings' place, [0x7681eb4] + 0xd64 = +0xb6c + 7 · 0x48, and the type it looks at (0x550e2a).
constexpr std::size_t WingsSlot = 7;
constexpr std::uint16_t CapeOfLordId = 0x1a1e;
// 0x54e540's selector at 0x55104b.
constexpr std::uint32_t Word36OptionSelector = 0x90;

// The tail's two caps (0x55130e) and the modulus of its two draws.
constexpr std::uint16_t TailCap = 100;
constexpr int TailPercent = 100;

// A record WORD: the low 16 bits, like the original's `mov word` and `add word`.
std::uint16_t Low16(int value)
{
    return static_cast<std::uint16_t>(value);
}

// The WORD that type `kind` adds to (0x550d7f..0x550da9), or nullptr if that type does not add.
std::uint16_t* BonusWord(Kernel::OwnCharacterRecord& own, std::uint8_t kind)
{
    switch (kind)
    {
    case KindOfWord2a:
        return &own.word2a;
    case KindOfWord2c:
        return &own.word2c;
    case KindOfWord30:
        return &own.word30;
    case KindOfWord2e:
        return &own.word2e;
    default:
        return nullptr;
    }
}

std::array<std::byte, 2> LittleEndianWord(std::uint16_t value)
{
    return {static_cast<std::byte>(value & 0xffu), static_cast<std::byte>(value >> 8)};
}

std::uint16_t FromLittleEndian(std::span<const std::byte, 2> bytes)
{
    return static_cast<std::uint16_t>(std::to_integer<unsigned>(bytes[0]) | (std::to_integer<unsigned>(bytes[1]) << 8));
}

// 0x551287..0x5512ba, the eight helpers in their order (thiscall on the record, no arguments), and the tail
// 0x5512bf..0x5513b1.
Core::Expected<void, std::string> RunOwnStatHelpersAndTail(Kernel::OwnCharacterRecord& own,
                                                           const OwnStatsContext& context)
{
    ItemSetOptionList& setOptions = context.itemSets->list;
    if (auto done = UpdateOwnWords50To56(own, context.held, context.items, &setOptions); !done.HasValue())
    {
        return done;
    }
    if (auto done = UpdateOwnWords5aAnd5c(own, context.held, context.items, &setOptions); !done.HasValue())
    {
        return done;
    }
    if (auto done = UpdateOwnWord4e(own, &setOptions); !done.HasValue())
    {
        return done;
    }
    if (auto done = UpdateOwnAttackAndMagicSpeeds(own, context.held, context.ownRecordAccesses, context.random);
        !done.HasValue())
    {
        return done;
    }
    if (auto done = UpdateOwnWord60(own, context.items); !done.HasValue())
    {
        return done;
    }
    if (!context.scan505820)
    {
        return Core::Unexpected(
            std::string("0x550d10 (0x5512ac): 0x550990 lee [0x743a17c] y [0x743a178], y MuRet no sabe qué dejó el "
                        "último 0x505820: el último frame del original que llegó a los personajes (0x510d00) no "
                        "llevó a ninguno hasta 0x50c91e (0x50ba30)"));
    }
    if (auto done = UpdateOwnWord62(own, context.items, *context.scan505820, &setOptions); !done.HasValue())
    {
        return done;
    }
    UpdateOwnWord64(own);
    UpdateOwnWord66(own);
    return UpdateOwnDerivedStatsTail(own, context.random);
}
} // namespace

Core::Expected<void, std::string> RecalculateOwnStats(Kernel::OwnCharacterRecord& own, const OwnStatsContext& context)
{
    const auto sets = RecalculateOwnItemSets(own, context);
    if (!sets.HasValue())
    {
        return Core::Unexpected(std::format("0x550d10 (0x550d24): {}", sets.Error()));
    }
    ClearItemSetOptionMarks(context.itemSets->list);
    ClearOwnItemStatWords(own);
    auto bonuses = AddOwnItemStatBonuses(own, context.items);
    if (!bonuses.HasValue())
    {
        return bonuses;
    }
    auto setOptions = AddOwnItemSetOptionValues(own, context.itemSets->list);
    if (!setOptions.HasValue())
    {
        return setOptions;
    }
    auto cape = AddCapeOfLordOptions(own, context.ownRecordAccesses, context.random);
    if (!cape.HasValue())
    {
        return cape;
    }
    return RunOwnStatHelpersAndTail(own, context);
}

Core::Expected<void, std::string> AddOwnItemSetOptionValues(Kernel::OwnCharacterRecord& own, ItemSetOptionList& list)
{
    struct WordAndSelector
    {
        std::uint16_t* word;
        std::uint32_t selector;
    };
    const std::array<WordAndSelector, 5> calls = {WordAndSelector{&own.word2a, SetOptionSelectorOfWord2a},
                                                  WordAndSelector{&own.word2c, SetOptionSelectorOfWord2c},
                                                  WordAndSelector{&own.word30, SetOptionSelectorOfWord30},
                                                  WordAndSelector{&own.word2e, SetOptionSelectorOfWord2e},
                                                  WordAndSelector{&own.word36, SetOptionSelectorOfWord36}};
    for (const WordAndSelector& call : calls)
    {
        auto added = AddItemSetOptionValues(list, *call.word, call.selector);
        if (!added.HasValue())
        {
            return Core::Unexpected(std::format("0x550d10 (0x40da20 con {:#x}): {}", call.selector, added.Error()));
        }
    }
    return {};
}

void ClearOwnItemStatWords(Kernel::OwnCharacterRecord& own)
{
    own.word2a = 0;
    own.word2c = 0;
    own.word2e = 0;
    own.word30 = 0;
    own.word36 = 0;
    own.word32 = 0;
    own.word34 = 0;
}

std::uint16_t ItemStatBonusValueOf(const Kernel::ItemState& item)
{
    return static_cast<std::uint16_t>((item.flags1c >> ItemStatValueShift) & ItemStatValueMask);
}

Core::Expected<ItemStatBonus, std::string> ItemStatBonusOf(const Kernel::ItemState& item,
                                                           const Tables::ItemTable& items)
{
    // The value is written before the table is read (0x40dc6e); the type, signed (movsx, 0x40dc77).
    const std::uint16_t value = ItemStatBonusValueOf(item);
    const auto type = static_cast<std::int16_t>(item.id);
    const std::optional<std::span<const std::byte, Tables::ItemRecordSize>> record =
        type < 0 ? std::nullopt : items.Record(static_cast<std::size_t>(type));
    if (!record)
    {
        return Core::Unexpected(std::format("0x40dc50: el tipo {} lee *[0x7758550] fuera de Item.bmd (Q-C184)", type));
    }
    return ItemStatBonus{.kind = std::to_integer<std::uint8_t>((*record)[ItemStatKindOffset]), .value = value};
}

Core::Expected<void, std::string> AddOwnItemStatBonuses(Kernel::OwnCharacterRecord& own, const Tables::ItemTable* items)
{
    for (std::size_t slot = 0; slot < own.equipment.size(); ++slot)
    {
        const Kernel::ItemState& item = own.equipment[slot];
        // 0x550d72..0x550da9: for value 0, the 256 selectors produce the same WORDs. The byte before Item.bmd is not
        // invented and the place's leftover data is not erased.
        if (item.durability == 0 || (item.id == Kernel::EmptyOwnItemId && ItemStatBonusValueOf(item) == 0))
        {
            continue;
        }
        if (items == nullptr)
        {
            return Core::Unexpected(
                std::format("0x550d10: el lugar {} pide Item.bmd (0x40dc50) y no está cargado", slot));
        }
        const auto bonus = ItemStatBonusOf(item, *items);
        if (!bonus.HasValue())
        {
            return Core::Unexpected(std::format("0x550d10, lugar {}: {}", slot, bonus.Error()));
        }
        if (std::uint16_t* word = BonusWord(own, bonus.Value().kind))
        {
            *word = Low16(*word + bonus.Value().value * ItemStatValueFactor);
        }
    }
    return {};
}

Core::Expected<void, std::string> AddCapeOfLordOptions(Kernel::OwnCharacterRecord& own, std::uint8_t& ownRecordAccesses,
                                                       Crt::CrtRandom& random)
{
    const Kernel::ItemState& wings = own.equipment[WingsSlot];
    if (wings.id != CapeOfLordId)
    {
        return {};
    }
    const Models::LogicalAccess access(ownRecordAccesses, random, Models::OwnRecordCloseRandoms);
    if (static_cast<std::size_t>(wings.optionCount) > Kernel::MaxItemOptions)
    {
        return Core::Unexpected(std::format("0x54e540 (0x551051): Cape of Lord con {} opciones lee más allá de sus "
                                            "nueve, en el lugar de la mascota, que MuRet no proyecta así",
                                            wings.optionCount));
    }
    std::array<std::byte, 2> word36 = LittleEndianWord(own.word36);
    Scenes::AddOwnItemOptionValues(word36, Word36OptionSelector,
                                   {wings.id, wings.durability, wings.optionCount, wings.optionBytes});
    own.word36 = FromLittleEndian(word36);
    return {};
}

Core::Expected<void, std::string> UpdateOwnDerivedStatsTail(Kernel::OwnCharacterRecord& own, Crt::CrtRandom& random)
{
    own.wordef8 = Low16(own.wordef0 - own.wordee6);
    own.wordefa = Low16(own.wordef2 - own.wordee6);
    // 0x5512eb..0x551306: the two unsigned WORDs, the rand() and the signed 32-bit IDIV.
    const int minimum = own.wordee2;
    const int draw = random.Next();
    const int divisor = own.wordee4 - minimum + 1;
    if (divisor == 0)
    {
        return Core::Unexpected(std::format("0x550d10: el IDIV de 0x551306 divide por 0 con +0xee2 = {} y +0xee4 = {} "
                                            "(Q-C185)",
                                            own.wordee2, own.wordee4));
    }
    own.wordefc = Low16(draw % divisor + minimum - own.word62);
    own.wordefe = Low16(own.word4e - own.wordeec);
    own.wordf00 = Low16(own.word60 - own.wordeea);
    // cmp and jbe: the caps, unsigned (0x551337 and 0x55134a).
    if (own.wordefe > TailCap)
    {
        own.wordefe = TailCap;
    }
    if (own.wordf00 > TailCap)
    {
        own.wordf00 = TailCap;
    }
    // Signed jge against the unsigned WORD (0x55136d and 0x551392).
    own.bytef02 = static_cast<std::uint8_t>(random.Next() % TailPercent < own.wordefe);
    own.bytef03 = static_cast<std::uint8_t>(random.Next() % TailPercent < own.wordf00);
    return {};
}
} // namespace Client::Player
