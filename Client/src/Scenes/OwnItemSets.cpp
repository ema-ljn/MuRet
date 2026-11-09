#include "Scenes/OwnItemSets.h"

#include "Core/Expected.h"
#include "Data/Tables/ItemSetTables.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorState.h"
#include "Scenes/ItemSetOptionList.h"
#include "Scenes/ItemSetState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnDerivedStats.h"
#include "World/ItemState.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>

// 0x40e1b0 (1.290 bytes), 0x40cfa0 (100), 0x40d010 (1.298) y 0x40de10 (898), cotejados con el PE
// c32bbd5f: 06, "Los stats del registro propio". 0x40da20 es Scenes::AddItemSetOptionValues (0x40d690).
namespace Client::Scenes
{
namespace
{
using OptionRecord = std::span<const std::byte, Data::Tables::ItemSetOptionRecordSize>;

// El conjunto de un ítem es su byte +0x1c & 3 (0x40cfa7; el byte llega sin signo y el resto con signo
// de 0x40cfae nunca es negativo). 0, ninguno.
constexpr std::uint8_t SetIndexMask = 3;
// En ItemSetType.bmd, ningún conjunto (0x40cfc4 y 0x40cfc9).
constexpr std::uint8_t NoSet = 0xff;

// Los lugares que 0x40e1b0 salta (0x40e1e6..0x40e1f8) y los dos pares en que cuenta una vez el mismo
// ítem: las manos y los anillos (0x40e27a..0x40e2b3 y 0x40e326..0x40e34a).
constexpr std::size_t WingsSlot = 7;
constexpr std::size_t PetSlot = 8;
constexpr std::size_t RightHandSlot = 0;
constexpr std::size_t LeftHandSlot = 1;
constexpr std::size_t FirstRingSlot = 10;
constexpr std::size_t SecondRingSlot = 11;

// Un registro de ItemSetOption.bmd, como lo lee 0x40d010.
constexpr std::size_t NameSize = 0x20;            // +0, el nombre con su 0
constexpr std::size_t PairIdsOffset = 0x20;       // seis pares de opciones (0x40d25d)
constexpr std::size_t PairValuesOffset = 0x2c;    // y sus valores (0x40d262)
constexpr int PairCount = 6;                      // 0x40d09c..0x40d0a7
constexpr std::size_t ExtraIdsOffset = 0x38;      // 0x40d28a
constexpr std::size_t ExtraValuesOffset = 0x3a;   // 0x40d291
constexpr std::size_t FullSetPiecesOffset = 0x3c; // las piezas del conjunto + 1 (0x40d363)
constexpr std::size_t FullIdsOffset = 0x3d;       // 0x40d381
constexpr std::size_t FullValuesOffset = 0x42;    // 0x40d376
constexpr std::size_t FullOptionCount = 5;        // 0x40d448
constexpr std::size_t ClassMarksOffset = 0x47;    // una por clase (0x40d0d0..0x40d140)
// Un conjunto aplica desde dos piezas (0x40d08e), y está completo con piezas − 1 ≥ +0x3c − 2 (0x40d366).
constexpr std::uint8_t MinimumPieces = 2;
constexpr int FullSetMargin = 2;
// Las opciones de 0x18 en adelante piden la marca de la clase (0x40d29c); 0xff es ninguna.
constexpr std::uint8_t FirstClassOption = 0x18;
constexpr std::uint8_t NoOption = 0xff;
// Las marcas: 1 vale para la clase; 2, con la evolución (+0x1d4 >> 3) distinta de 0. Las clases 0 a
// 2 miran las dos; la 3 solo el 1, y la 4 no tiene ninguna (0x40d0d0..0x40d14d).
constexpr std::uint8_t ClassMark = 1;
constexpr std::uint8_t EvolvedClassMark = 2;
constexpr std::uint8_t ClassesWithEvolvedMark = 3;
constexpr std::uint8_t LastMarkedClass = 3;
constexpr std::uint8_t ClassBits = 7;
constexpr int EvolutionShift = 3;

// Las opciones de cada clase de pieza que caben en las listas locales de 0x40d010 (-0x64, -0x84 y
// -0x104, de a 16).
constexpr std::size_t LocalOptionCapacity = 16;

// 0x40de10 llama a 0x40da20 con estos, en este orden (0x40e14c..0x40e17c).
constexpr std::uint32_t StrengthSelector = 0x8c;
constexpr std::uint32_t AgilitySelector = 0x8d;
constexpr std::uint32_t EnergySelector = 0x8e;
constexpr std::uint32_t VitalitySelector = 0x8f;
// Los tipos de 0x40dc50 que suman, por la tabla de saltos de 0x40e194, y lea eax, [eax + eax·4].
constexpr std::uint8_t KindOfStrength = 1;
constexpr std::uint8_t KindOfAgility = 2;
constexpr std::uint8_t KindOfEnergy = 3;
constexpr std::uint8_t KindOfVitality = 4;
constexpr int ItemStatValueFactor = 5;

// Lo que 0x40e1b0 guarda del lugar 0 o 10 para no contar dos veces el mismo ítem en el 1 o el 11
// (-0x7c y -0x60). En el original no tiene valor al entrar, pero el lugar 0 siempre lo escribe antes
// de que el 1 lo lea, y pasa de una pasada a la otra.
struct PairedPiece
{
    std::uint16_t type = 0;
    std::uint8_t setIndex = 0;
};

// Una opción en las listas locales de 0x40d010, antes de copiarla a la de [0x6eed3c].
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

// Lo que 0x40d010 sabe del conjunto que está aplicando.
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

std::uint8_t SetIndexOf(const World::ItemState& item)
{
    return static_cast<std::uint8_t>(item.flags1c & SetIndexMask);
}

// 0x40e1e6..0x40e1f8 en la primera pasada, 0x40e4cf..0x40e4e1 en la segunda.
bool CountsSlot(std::size_t slot)
{
    return slot != WingsSlot && slot != PetSlot;
}

// 0x40e27a..0x40e2b3: el lugar 1 o el 11 con el mismo tipo y conjunto que el 0 o el 10.
bool RepeatsItsPair(std::size_t slot, const World::ItemState& item, const PairedPiece& previous)
{
    const bool second = slot == LeftHandSlot || slot == SecondRingSlot;
    return second && previous.type == item.id && previous.setIndex == SetIndexOf(item);
}

// 0x40e563..0x40e592: un requisito (+0x20 la agilidad, +0x22 la energía, +0x1e la fuerza) mayor que la
// copia, sin signo.
bool MeetsRequirements(const World::ItemState& item, const OwnStatCopies& copies)
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

// La marca de la clase del héroe, rec[0x47 + clase] (0x40d2a7): con la clase 5 a 7 cae en el registro
// siguiente, que está pegado en [0x6eed3c] + 0x8004; después del último, fuera de la tabla (Q-C187).
Core::Expected<std::uint8_t, std::string> ClassMarkOf(const Data::Tables::ItemSetOptionTable& options,
                                                      const SetBeingApplied& set)
{
    const std::size_t offset = ClassMarksOffset + set.classLow;
    const std::size_t index = set.setId + offset / Data::Tables::ItemSetOptionRecordSize;
    const auto record = options.Record(index);
    if (!record)
    {
        return Core::Unexpected(std::format("0x40d010: la marca de la clase {} del conjunto {} cae fuera de "
                                            "ItemSetOption.bmd (Q-C187)",
                                            set.classLow, set.setId));
    }
    return ByteAt(*record, offset % Data::Tables::ItemSetOptionRecordSize);
}

// 0x40d297..0x40d2bd y 0x40d394..0x40d3f4.
Core::Expected<bool, std::string> AllowsOption(const Data::Tables::ItemSetOptionTable& options,
                                               const SetBeingApplied& set, std::uint8_t option)
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
    // cmp con signo: la evolución menor que la marca − 1 no la tiene.
    return mark.Value() != 0 && set.evolution >= mark.Value() - 1;
}

// 0x40d2bf..0x40d2e9: la opción al final de la lista local de su clase de pieza.
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
                                                   const Data::Tables::ItemSetOptionTable& options,
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

// 0x40d16d..0x40d233: strcpy al nombre de su clase de pieza, o, si tiene el de otro conjunto, al primero
// vacío de los dos o al segundo, con +0x9307 en piezas − 1.
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

// 0x40d233..0x40d358: con n = piezas − 1, los primeros min(n, 6) pares de opciones, y después, mientras
// i < n − min(n, 6), las de +0x38 (con n ≤ 9, como deja 0x40e1b0, no hay ninguna).
Core::Expected<void, std::string> CollectPieceOptions(CollectedOptions& collected, std::size_t kind,
                                                      const Data::Tables::ItemSetOptionTable& options,
                                                      const SetBeingApplied& set, int optionPieces)
{
    const auto collectAt = [&](std::size_t idOffset, std::size_t valueOffset) -> Core::Expected<void, std::string>
    {
        if (valueOffset >= Data::Tables::ItemSetOptionRecordSize)
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

// 0x40d35e..0x40d457: el conjunto completo suma sus cinco opciones, cada una con +0x9307 + 1 si no es
// 0, y pone el +0x358 del héroe publicado en 1.
Core::Expected<void, std::string> CollectFullSetOptions(ItemSetState& state, CollectedOptions& collected,
                                                        std::size_t kind,
                                                        const Data::Tables::ItemSetOptionTable& options,
                                                        const SetBeingApplied& set, int optionPieces,
                                                        ActorState& publishedHero)
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

// 0x40d087..0x40d45e: un conjunto de la lista.
Core::Expected<void, std::string> ApplyPiece(ItemSetState& state, CollectedOptions& collected,
                                             const ItemSetPiece& piece, const Data::Tables::ItemSetOptionTable& options,
                                             ActorState& publishedHero)
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

// 0x40d473..0x40d515: las de la clase 0 desde el principio, las de la 1 detrás, y las cuentas.
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
            // El valor es un byte del registro: entra en el DWORD con signo sin cambiar.
            state.list.values[index] = static_cast<std::int32_t>(option.value);
            state.list.secondValues[index] = static_cast<std::int32_t>(option.value);
            ++index;
        }
    }
    state.list.count9348 = collected.counts[0];
    state.list.count9349 = collected.counts[1];
    return {};
}

// 0x40e1ce..0x40e1e3 y 0x40e4bf..0x40e4cc: los 16 bytes de +0x934c, cinco opciones enteras y el primer
// byte de la sexta.
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

// 0x40de23..0x40e132, un lugar: abre el registro, y con durabilidad 0x40dc50 y la suma; lo cierra.
Core::Expected<void, std::string> AddSlotBonusToCopies(OwnStatCopies& copies, const World::ItemState& item,
                                                       std::size_t slot, const OwnStatsContext& context)
{
    const Render::Models::LogicalAccess access(context.ownRecordAccesses, context.random,
                                               Render::Models::OwnRecordCloseRandoms);
    if (item.durability == 0)
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

// 0x40e301..0x40e320: un tipo negativo (el −1 de un lugar vacío) no cuenta.
Core::Expected<void, std::string> CountPiece(ItemSetPieces& pieces, const Data::Tables::ItemSetTypeTable& types,
                                             const World::ItemState& item)
{
    const auto type = static_cast<std::int16_t>(item.id);
    if (type < 0)
    {
        return {};
    }
    return AddItemSetPiece(pieces, types, static_cast<std::size_t>(type), item.flags1c);
}

// Una pasada de 0x40e1b0 (0x40e1e6..0x40e3a1 y 0x40e4cf..0x40e6a1): con `requirements`, la segunda.
Core::Expected<void, std::string> CountPieces(ItemSetPieces& pieces, PairedPiece& previous,
                                              const OwnCharacterRecord& own,
                                              const Data::Tables::ItemSetTypeTable& types,
                                              const OwnStatsContext& context, const OwnStatCopies* requirements)
{
    for (std::size_t slot = 0; slot < own.equipment.size(); ++slot)
    {
        if (!CountsSlot(slot))
        {
            continue;
        }
        const Render::Models::LogicalAccess access(context.ownRecordAccesses, context.random,
                                                   Render::Models::OwnRecordCloseRandoms);
        const World::ItemState& item = own.equipment[slot];
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

// 0x40e3b3..0x40e494: abre el registro, copia sus cuatro stats y lo cierra.
OwnStatCopies ReadStatCopies(const OwnCharacterRecord& own, const OwnStatsContext& context)
{
    const Render::Models::LogicalAccess access(context.ownRecordAccesses, context.random,
                                               Render::Models::OwnRecordCloseRandoms);
    return {.strength = own.strength, .agility = own.agility, .vitality = own.vitality, .energy = own.energy};
}
} // namespace

Core::Expected<void, std::string> AddItemSetPiece(ItemSetPieces& pieces, const Data::Tables::ItemSetTypeTable& types,
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
    // 0x40cfd2..0x40cfe3: el mismo conjunto o el primer lugar libre.
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
                                                      const Data::Tables::ItemSetOptionTable& options,
                                                      ActorState& publishedHero)
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

Core::Expected<void, std::string> AddOwnBonusesToStatCopies(OwnStatCopies& copies, const OwnCharacterRecord& own,
                                                            const OwnStatsContext& context)
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
        // 0x40da20, el código de 0x40d690.
        auto added = AddItemSetOptionValues(context.itemSets->list, *call.copy, call.selector);
        if (!added.HasValue())
        {
            return Core::Unexpected(std::format("0x40de10 (0x40da20 con {:#x}): {}", call.selector, added.Error()));
        }
    }
    return {};
}

Core::Expected<void, std::string> RecalculateOwnItemSets(const OwnCharacterRecord& own, const OwnStatsContext& context)
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
    const Data::Tables::ItemSetTables& tables = *context.itemSetTables;
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
} // namespace Client::Scenes
