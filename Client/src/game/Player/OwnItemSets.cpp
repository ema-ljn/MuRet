#include "Player/OwnItemSets.h"

#include "Core/Expected.h"
#include "Kernel/CharacterClass.h"
#include "Tables/ItemSetTables.h"
#include "Models/LogicalAccess.h"
#include "Kernel/ActorState.h"
#include "Player/ItemSetOptionList.h"
#include "Player/ItemSetState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Player/OwnDerivedStats.h"
#include "Kernel/ItemState.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>

// 0x40e1b0 (1,290 bytes), 0x40cfa0 (100), 0x40d010 (1,298) and 0x40de10 (898), checked against the PE c32bbd5f: 06,
// "Los stats del registro propio". 0x40da20 is Player::AddItemSetOptionValues (0x40d690).
namespace Client::Player
{
namespace
{
using OptionRecord = std::span<const std::byte, Tables::ItemSetOptionRecordSize>;

// An item's set is its byte +0x1c & 3 (0x40cfa7; the byte arrives unsigned and 0x40cfae's signed remainder is never
// negative). 0, none.
constexpr std::uint8_t SetIndexMask = 3;
// In ItemSetType.bmd, no set (0x40cfc4 and 0x40cfc9).
constexpr std::uint8_t NoSet = 0xff;

// The places 0x40e1b0 skips (0x40e1e6..0x40e1f8) and the two pairs in which the same item counts once: the hands and
// the rings (0x40e27a..0x40e2b3 and 0x40e326..0x40e34a).
constexpr std::size_t WingsSlot = 7;
constexpr std::size_t PetSlot = 8;
constexpr std::size_t RightHandSlot = 0;
constexpr std::size_t LeftHandSlot = 1;
constexpr std::size_t FirstRingSlot = 10;
constexpr std::size_t SecondRingSlot = 11;

// An ItemSetOption.bmd record, as 0x40d010 reads it.
constexpr std::size_t NameSize = 0x20;            // +0, the name with its 0
constexpr std::size_t PairIdsOffset = 0x20;       // six option pairs (0x40d25d)
constexpr std::size_t PairValuesOffset = 0x2c;    // and their values (0x40d262)
constexpr int PairCount = 6;                      // 0x40d09c..0x40d0a7
constexpr std::size_t ExtraIdsOffset = 0x38;      // 0x40d28a
constexpr std::size_t ExtraValuesOffset = 0x3a;   // 0x40d291
constexpr std::size_t FullSetPiecesOffset = 0x3c; // the set's pieces + 1 (0x40d363)
constexpr std::size_t FullIdsOffset = 0x3d;       // 0x40d381
constexpr std::size_t FullValuesOffset = 0x42;    // 0x40d376
constexpr std::size_t FullOptionCount = 5;        // 0x40d448
constexpr std::size_t ClassMarksOffset = 0x47;    // one per class (0x40d0d0..0x40d140)
// A set applies from two pieces (0x40d08e), and is complete with pieces − 1 ≥ +0x3c − 2 (0x40d366).
constexpr std::uint8_t MinimumPieces = 2;
constexpr int FullSetMargin = 2;
// Options from 0x18 on ask for the class mark (0x40d29c); 0xff is none.
constexpr std::uint8_t FirstClassOption = 0x18;
constexpr std::uint8_t NoOption = 0xff;
// The marks: 1 holds for the class; 2, with the evolution (+0x1d4 >> 3) other than 0. Classes 0 to 2 look at both;
// class 3 only at 1, and class 4 has none (0x40d0d0..0x40d14d).
constexpr std::uint8_t ClassMark = 1;
constexpr std::uint8_t EvolvedClassMark = 2;
constexpr std::uint8_t ClassesWithEvolvedMark = 3;
constexpr std::uint8_t LastMarkedClass = Kernel::ClassNumberOf(Kernel::CharacterClass::MagicGladiator);
constexpr std::uint8_t ClassBits = 7;
constexpr int EvolutionShift = 3;

// The options of each piece kind that fit in 0x40d010's local lists (-0x64, -0x84 and -0x104, 16 each).
constexpr std::size_t LocalOptionCapacity = 16;

// 0x40de10 calls 0x40da20 with these, in this order (0x40e14c..0x40e17c).
constexpr std::uint32_t StrengthSelector = 0x8c;
constexpr std::uint32_t AgilitySelector = 0x8d;
constexpr std::uint32_t EnergySelector = 0x8e;
constexpr std::uint32_t VitalitySelector = 0x8f;
// The 0x40dc50 types that add, by 0x40e194's jump table, and lea eax, [eax + eax·4].
constexpr std::uint8_t KindOfStrength = 1;
constexpr std::uint8_t KindOfAgility = 2;
constexpr std::uint8_t KindOfEnergy = 3;
constexpr std::uint8_t KindOfVitality = 4;
constexpr int ItemStatValueFactor = 5;

// What 0x40e1b0 keeps from place 0 or 10 so as not to count the same item twice in 1 or 11 (-0x7c and -0x60). In the
// original it has no value on entry, but place 0 always writes it before place 1 reads it, and it carries over from
// one pass to the other.
struct PairedPiece
{
    std::uint16_t type = 0;
    std::uint8_t setIndex = 0;
};

// An option in 0x40d010's local lists, before it is copied to [0x6eed3c]'s.
struct CollectedOption
{
    std::uint8_t option = 0;
    std::uint8_t classMatch = 0;
    std::uint32_t value = 0;
};

struct CollectedOptions
{
    std::array<std::array<CollectedOption, LocalOptionCapacity>, ItemSetKindCount> byKind{};
    std::array<std::uint8_t, ItemSetKindCount> counts{};
};

// What 0x40d010 knows of the set it is applying.
struct SetBeingApplied
{
    std::uint8_t setId = 0;
    OptionRecord record;
    std::uint8_t classLow = 0;
    int evolution = 0;
    std::uint8_t classMatch = 0;
};

std::uint8_t ByteAt(OptionRecord record, std::size_t offset)
{
    return std::to_integer<std::uint8_t>(record[offset]);
}

std::uint8_t SetIndexOf(const Kernel::ItemState& item)
{
    return static_cast<std::uint8_t>(item.flags1c & SetIndexMask);
}

// 0x40e1e6..0x40e1f8 in the first pass, 0x40e4cf..0x40e4e1 in the second.
bool CountsSlot(std::size_t slot)
{
    return slot != WingsSlot && slot != PetSlot;
}

// 0x40e27a..0x40e2b3: place 1 or 11 with the same type and set as 0 or 10.
bool RepeatsItsPair(std::size_t slot, const Kernel::ItemState& item, const PairedPiece& previous)
{
    const bool second = slot == LeftHandSlot || slot == SecondRingSlot;
    return second && previous.type == item.id && previous.setIndex == SetIndexOf(item);
}

// 0x40e563..0x40e592: a requirement (+0x20 the agility, +0x22 the energy, +0x1e the strength) above the copy,
// unsigned.
bool MeetsRequirements(const Kernel::ItemState& item, const OwnStatCopies& copies)
{
    return item.word20 <= copies.agility && item.word22 <= copies.energy && item.word1e <= copies.strength;
}

// 0x40d0d0..0x40d14d.
std::uint8_t ClassMatchOf(OptionRecord record, std::uint8_t classLow, int evolution)
{
    for (std::uint8_t markedClass = 0; markedClass < ClassesWithEvolvedMark; ++markedClass)
    {
        const std::uint8_t mark = ByteAt(record, ClassMarksOffset + markedClass);
        const bool evolved = mark == EvolvedClassMark && evolution != 0;
        if (classLow == markedClass && (mark == ClassMark || evolved))
        {
            return 1;
        }
    }
    const bool third = classLow == LastMarkedClass && ByteAt(record, ClassMarksOffset + LastMarkedClass) == ClassMark;
    return third ? 1 : 0;
}

// The hero's class mark, rec[0x47 + class] (0x40d2a7): with class 5 to 7 it falls in the next record, which sits right
// after it in [0x6eed3c] + 0x8004; after the last one, outside the table (Q-C187).
Core::Expected<std::uint8_t, std::string> ClassMarkOf(const Tables::ItemSetOptionTable& options,
                                                      const SetBeingApplied& set)
{
    const std::size_t offset = ClassMarksOffset + set.classLow;
    const std::size_t index = set.setId + offset / Tables::ItemSetOptionRecordSize;
    const auto record = options.Record(index);
    if (!record)
    {
        return Core::Unexpected(std::format("0x40d010: la marca de la clase {} del conjunto {} cae fuera de "
                                            "ItemSetOption.bmd (Q-C187)",
                                            set.classLow, set.setId));
    }
    return ByteAt(*record, offset % Tables::ItemSetOptionRecordSize);
}

// 0x40d297..0x40d2bd and 0x40d394..0x40d3f4.
Core::Expected<bool, std::string> AllowsOption(const Tables::ItemSetOptionTable& options, const SetBeingApplied& set,
                                               std::uint8_t option)
{
    if (option == NoOption)
    {
        return false;
    }
    if (option < FirstClassOption)
    {
        return true;
    }
    const auto mark = ClassMarkOf(options, set);
    if (!mark.HasValue())
    {
        return Core::Unexpected(mark.Error());
    }
    // Signed cmp: an evolution below the mark − 1 does not have it.
    return mark.Value() != 0 && set.evolution >= mark.Value() - 1;
}

// 0x40d2bf..0x40d2e9: the option at the end of its piece kind's local list.
Core::Expected<void, std::string> Collect(CollectedOptions& collected, std::size_t kind, CollectedOption option)
{
    std::uint8_t& count = collected.counts[kind];
    if (std::size_t{count} >= LocalOptionCapacity)
    {
        return Core::Unexpected(std::format("0x40d010: la clase de pieza {} junta más de {} opciones y el original "
                                            "escribe fuera de sus listas locales (Q-C187)",
                                            kind, LocalOptionCapacity));
    }
    collected.byKind[kind][count] = option;
    count = static_cast<std::uint8_t>(count + 1u);
    return {};
}

Core::Expected<void, std::string> CollectIfAllowed(CollectedOptions& collected, std::size_t kind,
                                                   const Tables::ItemSetOptionTable& options,
                                                   const SetBeingApplied& set, std::uint8_t option, std::uint8_t value)
{
    const auto allowed = AllowsOption(options, set, option);
    if (!allowed.HasValue())
    {
        return Core::Unexpected(allowed.Error());
    }
    if (!allowed.Value())
    {
        return {};
    }
    return Collect(collected, kind, {.option = option, .classMatch = set.classMatch, .value = value});
}

// 0x40d16d..0x40d233: strcpy to its piece kind's name or, if that holds another set's, to the first empty one of the
// two or to the second, with +0x9307 at pieces − 1.
Core::Expected<void, std::string> CopySetName(ItemSetState& state, std::size_t kind, const SetBeingApplied& set,
                                              std::uint8_t pieces)
{
    const std::span<const std::byte> name = set.record.first(NameSize);
    const auto end = std::ranges::find(name, std::byte{0});
    if (end == name.end())
    {
        return Core::Unexpected(std::format("0x40d010: el nombre del conjunto {} no termina en sus {} bytes y strcpy "
                                            "sigue leyendo (Q-C187)",
                                            set.setId, NameSize));
    }
    const std::size_t length = static_cast<std::size_t>(end - name.begin()) + 1;
    const auto copyTo = [&name, length](std::array<char, ItemSetNameSize>& slot)
    {
        for (std::size_t index = 0; index < length; ++index)
        {
            slot[index] = static_cast<char>(std::to_integer<unsigned char>(name[index]));
        }
    };
    std::array<char, ItemSetNameSize>& own = state.names[kind];
    const auto same = [&name, length](const std::array<char, ItemSetNameSize>& slot)
    {
        return std::ranges::equal(name.first(length), std::span(slot).first(length), {}, [](std::byte byte)
                                  { return static_cast<char>(std::to_integer<unsigned char>(byte)); });
    };
    if (own[0] == '\0' || same(own))
    {
        copyTo(own);
        return {};
    }
    copyTo(state.names[0][0] == '\0' ? state.names[0] : state.names[1]);
    state.byte9307 = static_cast<std::uint8_t>(pieces - 1u);
    return {};
}

// 0x40d233..0x40d358: with n = pieces − 1, the first min(n, 6) option pairs and then, while i < n − min(n, 6), those
// of +0x38 (with n ≤ 9, as 0x40e1b0 leaves it, there are none).
Core::Expected<void, std::string> CollectPieceOptions(CollectedOptions& collected, std::size_t kind,
                                                      const Tables::ItemSetOptionTable& options,
                                                      const SetBeingApplied& set, int optionPieces)
{
    const auto collectAt = [&](std::size_t idOffset, std::size_t valueOffset) -> Core::Expected<void, std::string>
    {
        if (valueOffset >= Tables::ItemSetOptionRecordSize)
        {
            return Core::Unexpected(std::format("0x40d010: con {} piezas el conjunto {} lee fuera de su registro "
                                                "(Q-C187)",
                                                optionPieces + 1, set.setId));
        }
        return CollectIfAllowed(collected, kind, options, set, ByteAt(set.record, idOffset),
                                ByteAt(set.record, valueOffset));
    };
    const int pairs = std::min(optionPieces, PairCount);
    for (int step = 0; step < optionPieces; ++step)
    {
        const auto i = static_cast<std::size_t>(step);
        if (step < pairs)
        {
            auto first = collectAt(PairIdsOffset + 2 * i, PairValuesOffset + 2 * i);
            if (!first.HasValue())
            {
                return first;
            }
            auto second = collectAt(PairIdsOffset + 2 * i + 1, PairValuesOffset + 2 * i + 1);
            if (!second.HasValue())
            {
                return second;
            }
        }
        else if (step < optionPieces - pairs)
        {
            auto extra = collectAt(ExtraIdsOffset + i, ExtraValuesOffset + i);
            if (!extra.HasValue())
            {
                return extra;
            }
        }
    }
    return {};
}

// 0x40d35e..0x40d457: the complete set adds its five options, each with +0x9307 + 1 if it is not 0, and sets the
// published hero's +0x358 to 1.
Core::Expected<void, std::string> CollectFullSetOptions(ItemSetState& state, CollectedOptions& collected,
                                                        std::size_t kind, const Tables::ItemSetOptionTable& options,
                                                        const SetBeingApplied& set, int optionPieces,
                                                        Kernel::ActorState& publishedHero)
{
    if (optionPieces < ByteAt(set.record, FullSetPiecesOffset) - FullSetMargin)
    {
        return {};
    }
    for (std::size_t full = 0; full < FullOptionCount; ++full)
    {
        const std::uint8_t option = ByteAt(set.record, FullIdsOffset + full);
        const std::uint8_t countBefore = collected.counts[kind];
        const auto added =
            CollectIfAllowed(collected, kind, options, set, option, ByteAt(set.record, FullValuesOffset + full));
        if (!added.HasValue())
        {
            return added;
        }
        if (collected.counts[kind] != countBefore && state.byte9307 != 0)
        {
            state.byte9307 = static_cast<std::uint8_t>(state.byte9307 + 1u);
        }
    }
    publishedHero.byte358 = 1;
    return {};
}

// 0x40d087..0x40d45e: one set of the list.
Core::Expected<void, std::string> ApplyPiece(ItemSetState& state, CollectedOptions& collected,
                                             const ItemSetPiece& piece, const Tables::ItemSetOptionTable& options,
                                             Kernel::ActorState& publishedHero)
{
    const auto record = options.Record(piece.setId);
    if (!record)
    {
        return Core::Unexpected(
            std::format("0x40d010: el conjunto {} está fuera de ItemSetOption.bmd (Q-C187)", piece.setId));
    }
    const std::uint8_t heroClass = publishedHero.characterClass;
    SetBeingApplied set{.setId = piece.setId,
                        .record = *record,
                        .classLow = static_cast<std::uint8_t>(heroClass & ClassBits),
                        .evolution = heroClass >> EvolutionShift};
    set.classMatch = ClassMatchOf(set.record, set.classLow, set.evolution);
    if (std::size_t{piece.kind} >= ItemSetKindCount)
    {
        return Core::Unexpected(std::format("0x40d010: la clase de pieza {} del conjunto {} escribe sobre +0x9348 y "
                                            "lee una cuenta sin valor (Q-C187)",
                                            piece.kind, piece.setId));
    }
    state.list.setIds[piece.kind] = piece.setId;
    auto named = CopySetName(state, piece.kind, set, piece.count);
    if (!named.HasValue())
    {
        return named;
    }
    const int optionPieces = piece.count - 1;
    auto byPieces = CollectPieceOptions(collected, piece.kind, options, set, optionPieces);
    if (!byPieces.HasValue())
    {
        return byPieces;
    }
    return CollectFullSetOptions(state, collected, piece.kind, options, set, optionPieces, publishedHero);
}

// 0x40d473..0x40d515: kind 0's from the start, kind 1's after them, and the counts.
Core::Expected<void, std::string> StoreCollected(ItemSetState& state, const CollectedOptions& collected)
{
    const std::size_t total = std::size_t{collected.counts[0]} + collected.counts[1];
    if (total > ItemSetOptionCapacity)
    {
        return Core::Unexpected(std::format("0x40d010: {} opciones no entran en las {} de +0x934c y el original "
                                            "escribe sobre +0x937c y +0x938c (Q-C187)",
                                            total, ItemSetOptionCapacity));
    }
    std::size_t index = 0;
    for (std::size_t kind = 0; kind < ItemSetKindCount; ++kind)
    {
        for (std::size_t entry = 0; entry < std::size_t{collected.counts[kind]}; ++entry)
        {
            const CollectedOption& option = collected.byKind[kind][entry];
            std::array<std::uint8_t, 3>& listed = state.list.entries[index];
            listed[0] = option.option;
            listed[1] = option.classMatch;
            // The value is a record byte: it enters the signed DWORD unchanged.
            state.list.values[index] = static_cast<std::int32_t>(option.value);
            state.list.secondValues[index] = static_cast<std::int32_t>(option.value);
            ++index;
        }
    }
    state.list.count9348 = collected.counts[0];
    state.list.count9349 = collected.counts[1];
    return {};
}

// 0x40e1ce..0x40e1e3 and 0x40e4bf..0x40e4cc: the 16 bytes of +0x934c, five whole options and the sixth's first byte.
void FillOptionListHead(ItemSetOptionList& list, std::uint8_t value)
{
    constexpr std::size_t WholeEntries = 5;
    for (std::size_t entry = 0; entry < WholeEntries; ++entry)
    {
        list.entries[entry] = {value, value, value};
    }
    list.entries[WholeEntries][0] = value;
}

std::uint16_t* CopyOf(OwnStatCopies& copies, std::uint8_t kind)
{
    switch (kind)
    {
    case KindOfStrength:
        return &copies.strength;
    case KindOfAgility:
        return &copies.agility;
    case KindOfEnergy:
        return &copies.energy;
    case KindOfVitality:
        return &copies.vitality;
    default:
        return nullptr;
    }
}

// 0x40de23..0x40e132, one place: opens the record and, with durability, 0x40dc50 and the sum; closes it.
Core::Expected<void, std::string> AddSlotBonusToCopies(OwnStatCopies& copies, const Kernel::ItemState& item,
                                                       std::size_t slot, const OwnStatsContext& context)
{
    const Models::LogicalAccess access(context.ownRecordAccesses, context.random, Models::OwnRecordCloseRandoms);
    // 0x40e038..0x40e075: a value-0 contribution is null whatever the selector. The logical access stays around it,
    // with its native closing and RNG.
    if (item.durability == 0 || (item.id == Kernel::EmptyOwnItemId && ItemStatBonusValueOf(item) == 0))
    {
        return {};
    }
    if (context.items == nullptr)
    {
        return Core::Unexpected(std::format("0x40de10: el lugar {} pide Item.bmd (0x40dc50) y no está cargado", slot));
    }
    const auto bonus = ItemStatBonusOf(item, *context.items);
    if (!bonus.HasValue())
    {
        return Core::Unexpected(std::format("0x40de10, lugar {}: {}", slot, bonus.Error()));
    }
    if (std::uint16_t* copy = CopyOf(copies, bonus.Value().kind))
    {
        *copy = static_cast<std::uint16_t>(*copy + bonus.Value().value * ItemStatValueFactor);
    }
    return {};
}

// 0x40e301..0x40e320: a negative type (an empty place's −1) does not count.
Core::Expected<void, std::string> CountPiece(ItemSetPieces& pieces, const Tables::ItemSetTypeTable& types,
                                             const Kernel::ItemState& item)
{
    const auto type = static_cast<std::int16_t>(item.id);
    if (type < 0)
    {
        return {};
    }
    return AddItemSetPiece(pieces, types, static_cast<std::size_t>(type), item.flags1c);
}

// One pass of 0x40e1b0 (0x40e1e6..0x40e3a1 and 0x40e4cf..0x40e6a1): with `requirements`, the second.
Core::Expected<void, std::string> CountPieces(ItemSetPieces& pieces, PairedPiece& previous,
                                              const Kernel::OwnCharacterRecord& own,
                                              const Tables::ItemSetTypeTable& types, const OwnStatsContext& context,
                                              const OwnStatCopies* requirements)
{
    for (std::size_t slot = 0; slot < own.equipment.size(); ++slot)
    {
        if (!CountsSlot(slot))
        {
            continue;
        }
        const Models::LogicalAccess access(context.ownRecordAccesses, context.random, Models::OwnRecordCloseRandoms);
        const Kernel::ItemState& item = own.equipment[slot];
        if ((requirements != nullptr && !MeetsRequirements(item, *requirements)) ||
            RepeatsItsPair(slot, item, previous))
        {
            continue;
        }
        const auto counted = CountPiece(pieces, types, item);
        if (!counted.HasValue())
        {
            return Core::Unexpected(std::format("0x40e1b0, lugar {}: {}", slot, counted.Error()));
        }
        if (slot == RightHandSlot || slot == FirstRingSlot)
        {
            previous = {.type = item.id, .setIndex = SetIndexOf(item)};
        }
    }
    return {};
}

// 0x40e3b3..0x40e494: opens the record, copies its four stats and closes it.
OwnStatCopies ReadStatCopies(const Kernel::OwnCharacterRecord& own, const OwnStatsContext& context)
{
    const Models::LogicalAccess access(context.ownRecordAccesses, context.random, Models::OwnRecordCloseRandoms);
    return {.strength = own.strength, .agility = own.agility, .vitality = own.vitality, .energy = own.energy};
}
} // namespace

Core::Expected<void, std::string> AddItemSetPiece(ItemSetPieces& pieces, const Tables::ItemSetTypeTable& types,
                                                  std::size_t type, std::uint8_t flags1c)
{
    const std::uint8_t setIndex = static_cast<std::uint8_t>(flags1c & SetIndexMask);
    if (setIndex == 0)
    {
        return {};
    }
    const auto record = types.Record(type);
    if (!record)
    {
        return Core::Unexpected(std::format("0x40cfa0: el tipo {} lee fuera de ItemSetType.bmd (Q-C187)", type));
    }
    const std::uint8_t setId = std::to_integer<std::uint8_t>((*record)[setIndex - 1u]);
    if (setId == NoSet || setId == 0)
    {
        return {};
    }
    // 0x40cfd2..0x40cfe3: the same set or the first free place.
    const auto piece = std::ranges::find_if(pieces, [setId](const ItemSetPiece& listed)
                                            { return listed.setId == 0 || listed.setId == setId; });
    if (piece == pieces.end())
    {
        return {};
    }
    piece->setId = setId;
    piece->count = static_cast<std::uint8_t>(piece->count + 1u);
    piece->kind = static_cast<std::uint8_t>(setIndex - 1u);
    return {};
}

Core::Expected<void, std::string> ApplyItemSetOptions(ItemSetState& state, const ItemSetPieces& pieces,
                                                      const Tables::ItemSetOptionTable& options,
                                                      Kernel::ActorState& publishedHero)
{
    // 0x40d01d..0x40d07d.
    state.byte9307 = 0;
    state.names = {};
    state.list.setIds = {};
    state.list.count9348 = 0;
    state.list.count9349 = 0;
    publishedHero.byte358 = 0;
    CollectedOptions collected;
    for (const ItemSetPiece& piece : pieces)
    {
        if (piece.count < MinimumPieces)
        {
            continue;
        }
        auto applied = ApplyPiece(state, collected, piece, options, publishedHero);
        if (!applied.HasValue())
        {
            return applied;
        }
    }
    return StoreCollected(state, collected);
}

void ClearItemSetOptionMarks(ItemSetOptionList& list)
{
    list.used = {};
}

Core::Expected<void, std::string>
AddOwnBonusesToStatCopies(OwnStatCopies& copies, const Kernel::OwnCharacterRecord& own, const OwnStatsContext& context)
{
    for (std::size_t slot = 0; slot < own.equipment.size(); ++slot)
    {
        auto added = AddSlotBonusToCopies(copies, own.equipment[slot], slot, context);
        if (!added.HasValue())
        {
            return added;
        }
    }
    if (context.itemSets == nullptr)
    {
        return Core::Unexpected(std::string("0x40de10: 0x40da20 lee la lista de [0x6eed3c] y no está"));
    }
    struct CopyAndSelector
    {
        std::uint16_t* copy;
        std::uint32_t selector;
    };
    const std::array<CopyAndSelector, 4> setOptions = {
        CopyAndSelector{&copies.strength, StrengthSelector}, CopyAndSelector{&copies.agility, AgilitySelector},
        CopyAndSelector{&copies.energy, EnergySelector}, CopyAndSelector{&copies.vitality, VitalitySelector}};
    for (const CopyAndSelector& call : setOptions)
    {
        // 0x40da20, 0x40d690's code.
        auto added = AddItemSetOptionValues(context.itemSets->list, *call.copy, call.selector);
        if (!added.HasValue())
        {
            return Core::Unexpected(std::format("0x40de10 (0x40da20 con {:#x}): {}", call.selector, added.Error()));
        }
    }
    return {};
}

Core::Expected<void, std::string> RecalculateOwnItemSets(const Kernel::OwnCharacterRecord& own,
                                                         const OwnStatsContext& context)
{
    if (context.itemSetTables == nullptr)
    {
        return Core::Unexpected(std::string("0x40e1b0: faltan las tablas de [0x6eed3c], ItemSetType.bmd e "
                                            "ItemSetOption.bmd (0x40c600)"));
    }
    if (context.itemSets == nullptr)
    {
        return Core::Unexpected(std::string("0x40e1b0: falta lo que escribe en [0x6eed3c] (WorldState::ItemSets)"));
    }
    const Tables::ItemSetTables& tables = *context.itemSetTables;
    ItemSetState& state = *context.itemSets;
    ItemSetPieces pieces{};
    PairedPiece previous;
    FillOptionListHead(state.list, 0);
    auto first = CountPieces(pieces, previous, own, tables.types, context, nullptr);
    if (!first.HasValue())
    {
        return first;
    }
    auto applied = ApplyItemSetOptions(state, pieces, tables.options, context.publishedHero);
    if (!applied.HasValue())
    {
        return applied;
    }
    OwnStatCopies copies = ReadStatCopies(own, context);
    auto bonuses = AddOwnBonusesToStatCopies(copies, own, context);
    if (!bonuses.HasValue())
    {
        return bonuses;
    }
    pieces = {};
    constexpr std::uint8_t SecondPassFill = 0xff;
    FillOptionListHead(state.list, SecondPassFill);
    auto second = CountPieces(pieces, previous, own, tables.types, context, &copies);
    if (!second.HasValue())
    {
        return second;
    }
    return ApplyItemSetOptions(state, pieces, tables.options, context.publishedHero);
}
} // namespace Client::Player
