#include "Items/OwnItemPlacement.h"

#include "Actors/ActionReply.h"
#include "Core/Expected.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Tables/ItemTable.h"
#include "Maps/MapId.h"
#include "Models/LogicalAccess.h"
#include "Models/Skeleton.h"
#include "Actors/ActorActions.h"
#include "Actors/ActorEquipment.h"
#include "Kernel/ActorState.h"
#include "Pets/DarkHorse.h"
#include "Pets/HelperTable.h"
#include "Monsters/MonsterCreation.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/OwnInventory.h"
#include "Player/OwnStatHelpers.h"
#include "Kernel/SceneId.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldMap.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Session/WorldTransitionSupport.h"
#include "Kernel/ItemState.h"
#include "Effects/ModelEffects.h"
#include "Assets/FaunaModels.h"

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
using Kernel::ItemState;

// 0x54b3a0: d[0] + 2 · (((d[5] & 0xf0) << 4) + (d[3] & 0x80)).
constexpr std::uint32_t TypeHighBitMask = 0x80;
constexpr std::uint32_t TypeHighNibbleMask = 0xf0;
constexpr int TypeHighBitShift = 1;
constexpr int TypeHighNibbleShift = 5;

// The Item.bmd record (*[0x7758550] + type · 0x54), read at 0x5bcc38 and 0x5bcc4f.
constexpr std::size_t ItemWidthOffset = 0x24;
constexpr std::size_t ItemHeightOffset = 0x25;

// 0x5bc8a0..0x5bc8ad and 0x420d1a..0x420d22: on maps 18 to 23 it creates no pets or raven.
using Maps::IsChaosCastle;
// The slots 0x5bc740 checks after 0x512a30 (0x5bc897, 0x5bca53, 0x5bca6c and 0x5bca71).
constexpr std::uint8_t LeftHandSlot = 1;
constexpr std::uint8_t PetSlot = 8;
constexpr std::uint8_t FirstRingSlot = 10;
constexpr std::uint8_t SecondRingSlot = 11;
// Horn of Doom (0x1a25) is the one that dresses Fenrir.
constexpr std::uint16_t GuardianAngel = 0x1a00;
constexpr std::uint16_t HornOfUnilia = 0x1a02;
constexpr std::uint16_t HornOfDinorant = 0x1a03;
constexpr std::uint16_t DarkHorse = 0x1a04;
constexpr std::uint16_t DarkRaven = 0x1a05;
constexpr std::uint16_t WizardsRing = 0x1a14;
constexpr std::uint16_t HornOfDoom = 0x1a25;
// The helpers 0x443fa0 creates with each one (0x5bc8dc, 0x5bc94a, 0x5bc95d, 0x5bc9a0 and 0x5bc9ec..0x5bca07):
// Fenrir's, 0x14a with the item's +0x1b at 1, 0x14c with 2 and 0x14b with another.
constexpr std::uint16_t GuardianAngelHelper = 0x1c12;
constexpr std::uint16_t UniliaHelper = 0xc6;
constexpr std::uint16_t DinorantHelper = 0x110;
constexpr std::uint16_t FenrirFirstHelper = 0x14a;
constexpr std::uint16_t FenrirSecondHelper = 0x14c;
constexpr std::uint16_t FenrirOtherHelper = 0x14b;
constexpr std::uint8_t FenrirFirstKind = 1;
constexpr std::uint8_t FenrirSecondKind = 2;
// 0x5bca8d..0x5bca93: D+4 & ~7 at 0x18, level 3.
constexpr std::uint32_t LevelBitsMask = ~std::uint32_t{7};
constexpr std::uint32_t LevelThreeBits = 0x18;
// The hero's +0x360: with 1 to 3, slot 10's ring is not checked (0x5bca76..0x5bca82); the level 3 one sets it to 4.
constexpr std::uint8_t FirstRingLayer = 1;
constexpr std::uint8_t LastRingLayer = 3;
constexpr std::uint8_t WizardsRingLayer = 4;
// 0x4225d0, which 0x54bf40 calls at 0x54cc5c and only does something with the Dark Horse: option 0x96 (0x4225f2)
// with 5 + the own record's agility (+0x1a) / 20 + 2 · the horse's level ([0x72397c]).
constexpr std::uint8_t DarkHorseOption = 0x96;
constexpr std::uint32_t AgilityDivisor = 20;
constexpr std::uint32_t HorseLevelFactor = 2;
constexpr std::uint32_t DarkHorseOptionBase = 5;
// The upper 24 bits of the scalars 0x5bc740 pushes with the data's low byte.
constexpr std::uint32_t UpperBits = ~std::uint32_t{0xff};

std::uint32_t Byte(InventoryItemBytes bytes, std::size_t index)
{
    return std::to_integer<std::uint32_t>(bytes[index]);
}

Core::Expected<std::span<const std::byte, Tables::ItemRecordSize>, std::string>
ItemRecordOf(std::uint16_t type, const Scenes::WorldEntryResources& resources, std::string_view where)
{
    if (resources.items == nullptr)
    {
        return Core::Unexpected(std::format("{}: lee *[0x7758550] (Item.bmd) y no está cargado", where));
    }
    const std::optional<std::span<const std::byte, Tables::ItemRecordSize>> record = resources.items->Record(type);
    if (!record)
    {
        return Core::Unexpected(std::format("{}: el tipo {:#x} lee fuera de Item.bmd", where, type));
    }
    return *record;
}
} // namespace

// 0x4225ec..0x422606: the loop that looks for 0x96.
std::uint8_t DarkHorseOptionPlace(const ItemState& item)
{
    for (std::uint8_t index = 0; index < item.optionCount; ++index)
    {
        if (item.OptionTag(index) == DarkHorseOption)
            return index != 0 ? index : item.optionCount;
    }
    return item.optionCount;
}

void AddTheDarkHorseOption(ItemState& item, const Scenes::WorldEntryResources& resources)
{
    AddTheDarkHorseOption(item, resources.world, resources.world.Runtime().ownRecordDepth, resources.cloth.random);
}

// 0x4225d0(item) from 0x54cc5c, with the Dark Horse (0x4225dc). It looks for option 0x96 from the first; at the first
// it does not take it (0x422600: with the index at 0 it goes on as if it were absent, Q-C415). With it, it rewrites
// its value; without it, it appends it at the end. The value, with the own record open (0x40b240/0x40ad90 and
// 0x40b440, and 0x40b4d0 closes it, with its 3848 rand() if left with no accesses): (+0x1a / 20 signed, over the
// unsigned word) + 2 · the low byte of [0x72397c] + 5, in one byte. The tag goes after closing (0x42281c, 0x42291c).
void AddTheDarkHorseOption(ItemState& item, const Kernel::WorldState& world, std::uint8_t& ownRecordDepth,
                           Crt::CrtRandom& random)
{
    const std::uint8_t index = DarkHorseOptionPlace(item);
    const bool found = index != item.optionCount;
    {
        const Models::LogicalAccess access(ownRecordDepth, random, Models::OwnRecordCloseRandoms);
        // OptionTag stops with a diagnostic if the index goes past the nine ItemState keeps.
        static_cast<void>(item.OptionTag(index));
        const std::uint32_t agility = world.OwnCharacter().agility;
        const auto level = static_cast<std::uint8_t>(world.Runtime().pets[Kernel::DarkHorsePetStats].level);
        item.optionBytes[Kernel::ItemOptionValueOffset + index] =
            static_cast<std::uint8_t>((agility / AgilityDivisor) + (HorseLevelFactor * level) + DarkHorseOptionBase);
    }
    item.optionBytes[index] = DarkHorseOption;
    if (!found)
    {
        ++item.optionCount;
    }
}

namespace
{
// 0x54bf40(item, B1, B3, B4) from 0x5bc740. contextWord is [0x7681eb0] + 0xe, the own record's level, which is only
// valid with the record open: closed, the last close left rand() & 0xff in each byte (Q-C125), and MuRet does not
// have it (Q-C202).
Core::Expected<void, std::string> InitializeInventoryItem(ItemState& item, Kernel::ItemInitializationScalars scalars,
                                                          const Scenes::WorldEntryResources& resources,
                                                          std::string_view where)
{
    const auto record = ItemRecordOf(item.id, resources, std::format("{}, 0x54bf40", where));
    if (!record.HasValue())
    {
        return Core::Unexpected(record.Error());
    }
    const Kernel::WorldRuntime& runtime = resources.world.Runtime();
    if (Kernel::ReadsTheContextWord(item.id, scalars.b3) && runtime.ownRecordDepth == 0)
    {
        return Core::Unexpected(std::format("{}: 0x54bf40 lee el nivel de [0x7681eb0] + 0xe (0x54cd97) con el registro "
                                            "propio cerrado, lo que dejó su último cierre (Q-C202): no corre 0x54bf40",
                                            where));
    }
    Kernel::InitializeItemState(item, record.Value(), scalars, resources.world.OwnCharacter().level,
                                [&resources](ItemState& horse) { AddTheDarkHorseOption(horse, resources); });
    return {};
}

// 0x5bc840..0x5bc873: what it writes in an equipment slot before 0x54bf40.
void WriteEquipmentFields(ItemState& item, std::uint16_t type, InventoryItemBytes bytes)
{
    item.durability = static_cast<std::uint8_t>(Byte(bytes, 2));
    item.flags1c = static_cast<std::uint8_t>(Byte(bytes, 4));
    item.id = type;
    item.flags1b = static_cast<std::uint8_t>(Byte(bytes, 3));
    item.occupied = 1;
}

// 0x5bc8b3..0x5bca0c: the helper of each pet of slot 8; Horn of Doom picks Fenrir's by the item's +0x1b
// (0x5bc9d6..0x5bca07). The others create nothing.
std::optional<std::uint16_t> HelperOfThePet(const ItemState& item)
{
    switch (item.id)
    {
    case GuardianAngel:
        return GuardianAngelHelper;
    case HornOfUnilia:
        return UniliaHelper;
    case HornOfDinorant:
        return DinorantHelper;
    case DarkHorse:
        return Pets::DarkHorseModel;
    case HornOfDoom:
        if (item.flags1b == FenrirFirstKind)
        {
            return FenrirFirstHelper;
        }
        return item.flags1b == FenrirSecondKind ? FenrirSecondHelper : FenrirOtherHelper;
    default:
        return std::nullopt;
    }
}

// 0x5bc88b..0x5bcaa7, with the hero it rereads from 0x743a170: what it creates or releases after dressing it.
Core::Expected<void, std::string> AfterTheDressing(std::uint8_t slot, const ItemState& item,
                                                   const Scenes::WorldEntryResources& resources)
{
    Kernel::WorldState& world = resources.world;
    const Kernel::WorldRuntime& runtime = world.Runtime();
    Kernel::ActorState& hero = world.Hero();
    const std::int32_t map = runtime.map;
    const std::uint16_t type = item.id;
    if (slot == PetSlot && !IsChaosCastle(map))
    {
        if (type == HornOfDoom)
        {
            // 0x5bc9d6..0x5bc9db: the item's byte +0x1b to +0x31b, the pet's, before creating it.
            hero.pet.flags1b = item.flags1b;
        }
        const std::optional<std::uint16_t> helper = HelperOfThePet(item);
        if (!helper)
        {
            return {};
        }
        // 0x443fa0(helper, hero + 0x10, hero, 0, 0), with the shared rand(): 0x1c12 draws three for its place
        // (0x443f21..0x443f8c), between 0x512a30's tail and 0x5bcaae's record close.
        if (auto created = Pets::CreateHelper(world, *helper, hero.position, runtime.hero, &resources.cloth.random);
            !created.HasValue())
        {
            return Core::Unexpected(std::format("0x5bc740 (0x5bc8cc), 0x443fa0: {}", created.Error()));
        }
        // 0x5bc967..0x5bca46: other than 0x1c12, with the published hero's +0x3e2 at 0, 0x518cf0(0x61e, hero + 0x10,
        // hero + 0x1c, hero + 0xf4, 1, hero, −1, 0, 0, 0, 0) creates the decal on the hero, even if 0x443fa0 created
        // nothing. No rand(); with the table full, none.
        if (type != GuardianAngel && hero.safeZone == 0)
        {
            static_cast<void>(world.Map().effects.CreatePetSummon(hero.position, hero.angles, hero.light, &hero));
        }
        return {};
    }
    if (slot == LeftHandSlot && type == DarkRaven)
    {
        // 0x5bca62: 0x420cf0 releases the raven of +0x430 (0x421d60) and, outside maps 18 to 23, creates it again.
        if (auto raven = Scenes::ReplaceTheRaven(hero, resources); !raven.HasValue())
        {
            return Core::Unexpected(std::format("0x5bc740 (0x5bca62), {}", raven.Error()));
        }
        return {};
    }
    // 0x5bca6c..0x5bcaa7: slot 11, or 10 with +0x360 outside 1 to 3, with the level 3 Wizard's Ring: 0x414340 releases
    // the layer at +0x364 and +0x360 stays at 4.
    const bool ring = slot == SecondRingSlot ||
                      (slot == FirstRingSlot && (hero.byte360 < FirstRingLayer || hero.byte360 > LastRingLayer));
    if (ring && type == WizardsRing && (item.scalar1Low & LevelBitsMask) == LevelThreeBits)
    {
        Kernel::ReleaseTheLayer(hero);
        hero.byte360 = WizardsRingLayer;
    }
    return {};
}

// 0x5bc77f..0x5bcc0c: an equipment slot, with the record opened inline for the whole path and 0x5bcaae's inline close
// (0x40b4d0's loop, with its rand() at 0x5bcbd5).
Core::Expected<Actors::ActorStopResult, std::string> PlaceInEquipment(std::uint8_t slot, std::uint16_t type,
                                                                      InventoryItemBytes bytes,
                                                                      const Scenes::WorldEntryResources& resources)
{
    Kernel::WorldState& world = resources.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    const Models::LogicalAccess access(runtime.ownRecordDepth, resources.cloth.random, Models::OwnRecordCloseRandoms);
    ItemState& item = world.OwnCharacter().equipment[slot];
    WriteEquipmentFields(item, type, bytes);
    // 0x5bc866..0x5bc86b: B1 and B4 with the upper 24 bits of [0x7681eb4], which 0x54bf40 does not read, and B3 with
    // those of slot · 9, which with the slot from 0 to 11 are 0.
    const Kernel::ItemInitializationScalars scalars{
        .b1 = Byte(bytes, 1), .b3 = ((slot * 9u) & UpperBits) | Byte(bytes, 3), .b4 = Byte(bytes, 4)};
    if (auto initialized = InitializeInventoryItem(item, scalars, resources, "0x5bc740 (0x5bc87a)");
        !initialized.HasValue())
    {
        return Core::Unexpected(initialized.Error());
    }
    Kernel::ActorState& hero = world.Hero();
    const Models::Skeleton* player = resources.models.SkeletonOf(Tables::PlayerSkeletonModelId);
    const bool isPlayer = hero.type == Tables::PlayerSkeletonModel;
    if (player == nullptr && isPlayer)
    {
        return Core::Unexpected(std::string("0x5bc740 (0x5bc886): 0x512a30 necesita Player.bmd (0x20a) para 0x4f2250"));
    }
    static const Models::Skeleton EmptySkeleton;
    const Models::Skeleton* playerSkeleton = player != nullptr ? player : &EmptySkeleton;
    const Models::Skeleton* actorSkeleton = isPlayer ? nullptr : Actors::MonsterSkeletonOf(resources, hero.type);
    const Actors::ActorEquipmentContext equipment{
        .own = world.OwnCharacter(),
        .ownRecordAccesses = runtime.ownRecordDepth,
        .publishedHero = hero,
        .fullSetScan = runtime.fullSetScan,
        .stop =
            Actors::ActorStopContext{
                .runtime = runtime,
                .scene = Kernel::SceneId::Game,
                .playerSkeleton = *playerSkeleton,
                .items = resources.items,
                .random = resources.cloth.random,
                .actorSkeleton = actorSkeleton,
                .modelSounds = Assets::FaunaModelsOrEmpty(Scenes::FaunaModelsOf(resources)).SoundActions(hero.type)},
        .itemSetTables = resources.itemSetTables,
        .itemSets = &world.ItemSets(),
        .held = Player::HeldItemOf(runtime)};
    const auto dressed = Actors::ApplyActorEquipment(hero, equipment);
    if (!dressed.HasValue())
    {
        return Core::Unexpected(dressed.Error());
    }
    if (auto after = AfterTheDressing(slot, item, resources); !after.HasValue())
    {
        return Core::Unexpected(after.Error());
    }
    return dressed.Value();
}

// What each path leaves in the upper 24 bits of 0x54bf40's B1, B3 and B4: a register the original reuses.
enum class GridScalars : std::uint8_t
{
    // 0x5bcc9c, 0x5bccd5 (the two inventory grids): B1 and B3 with those of row · 8 + origin, B4 with the type's.
    Inventory,
    // 0x5bc1ee..0x5bc223 (0x5bc150, the grid at 0x78a7070): B1 and B4 with those of the slot's row, which does not go
    // past 31, and B3 with the type's.
    NpcGrid,
    // 0x5bd378..0x5bd3c7 (0x5bc740 outside 0x78a9bb0): B1 and B3 with those of cell · 9, B4 with the type's.
    Generic,
};

Kernel::ItemInitializationScalars ScalarsOf(GridScalars kind, std::int32_t rowBase, std::int32_t cell,
                                            std::uint16_t type, InventoryItemBytes bytes)
{
    const std::uint32_t typeUpper = type & UpperBits;
    switch (kind)
    {
    case GridScalars::Inventory:
    {
        const auto upper = static_cast<std::uint32_t>(rowBase) & UpperBits;
        return {.b1 = upper | Byte(bytes, 1), .b3 = upper | Byte(bytes, 3), .b4 = typeUpper | Byte(bytes, 4)};
    }
    case GridScalars::NpcGrid:
        return {.b1 = Byte(bytes, 1), .b3 = typeUpper | Byte(bytes, 3), .b4 = Byte(bytes, 4)};
    case GridScalars::Generic:
        break;
    }
    const auto upper = (static_cast<std::uint32_t>(cell) * 9u) & UpperBits;
    return {.b1 = upper | Byte(bytes, 1), .b3 = upper | Byte(bytes, 3), .b4 = typeUpper | Byte(bytes, 4)};
}

// A grid of `columns` per row from `origin`: the column and the row are the origin's (% 8 and / 8 fixed, signed, on
// all three paths). Each cell is row · columns + origin + column, unbounded: an item that goes past the last column
// continues on the row below (Q-C201), and one that goes past the last cell writes outside the grid (Q-C200).
Core::Expected<void, std::string> PlaceInGrid(std::span<ItemState> cells, std::int32_t columns, std::int32_t origin,
                                              std::uint16_t type, InventoryItemBytes bytes, GridScalars kind,
                                              const Scenes::WorldEntryResources& resources, std::string_view where)
{
    const auto record = ItemRecordOf(type, resources, where);
    if (!record.HasValue())
    {
        return Core::Unexpected(record.Error());
    }
    const auto width = std::to_integer<std::int32_t>(record.Value()[ItemWidthOffset]);
    const auto height = std::to_integer<std::int32_t>(record.Value()[ItemHeightOffset]);
    constexpr auto FixedColumns = static_cast<std::int32_t>(Kernel::InventoryColumns);
    const auto column = static_cast<std::uint8_t>(origin % FixedColumns);
    const auto row = static_cast<std::uint8_t>(origin / FixedColumns);
    for (std::int32_t y = 0; y < height; ++y)
    {
        const std::int32_t rowBase = y * columns + origin;
        for (std::int32_t x = 0; x < width; ++x)
        {
            const std::int32_t cell = rowBase + x;
            if (cell >= static_cast<std::int32_t>(cells.size()))
            {
                return Core::Unexpected(
                    std::format("{}: el tipo {:#x} ({} por {}) desde el origen {} escribe la casilla "
                                "{}, fuera de las {} de la grilla (Q-C200)",
                                where, type, width, height, origin, cell, cells.size()));
            }
            ItemState& item = cells[static_cast<std::size_t>(cell)];
            item.flags1c = static_cast<std::uint8_t>(Byte(bytes, 4));
            item.gridColumn = column;
            item.gridRow = row;
            item.durability = static_cast<std::uint8_t>(Byte(bytes, 2));
            item.id = type;
            item.flags1b = static_cast<std::uint8_t>(Byte(bytes, 3));
            item.occupied = y == 0 && x == 0 ? 1u : 0u;
            const Kernel::ItemInitializationScalars scalars = ScalarsOf(kind, rowBase, cell, type, bytes);
            if (auto initialized = InitializeInventoryItem(item, scalars, resources, where); !initialized.HasValue())
            {
                return initialized;
            }
        }
    }
    return {};
}

// 0x5bcd0b..0x5bcf94 and 0x5bd08b..0x5bd302: with [0x78a6758] under 12, it opens the own record inline, calls
// 0x54fe10 once for each WORD, +0x3e and +0x42, that is not 0 (0x5bcdd9 and 0x5bcdf0), and closes it with 0x40b4d0
// (0x5bcf8f). 0x54fe10 reads the cursor item with its globals (HeldItemOf).
Core::Expected<void, std::string> UpdateTheSpeedsAfterTheGrid(const Scenes::WorldEntryResources& resources)
{
    Kernel::WorldRuntime& runtime = resources.world.Runtime();
    if (runtime.slot78a6758 >= static_cast<std::int32_t>(FirstInventoryGridSlot))
    {
        return {};
    }
    const Models::LogicalAccess access(runtime.ownRecordDepth, resources.cloth.random, Models::OwnRecordCloseRandoms);
    Kernel::OwnCharacterRecord& own = resources.world.OwnCharacter();
    for (const std::uint16_t* word : {&own.word3e, &own.word42})
    {
        if (*word == 0)
        {
            continue;
        }
        if (auto updated = Player::UpdateOwnAttackAndMagicSpeeds(own, Player::HeldItemOf(runtime),
                                                                 runtime.ownRecordDepth, resources.cloth.random);
            !updated.HasValue())
        {
            return updated;
        }
    }
    return {};
}
} // namespace

Core::Expected<void, std::string> PlaceItemInOtherGrid(std::span<ItemState> cells, std::int32_t columns,
                                                       std::int32_t origin, InventoryItemBytes bytes,
                                                       const Scenes::WorldEntryResources& resources,
                                                       std::string_view where)
{
    const std::uint16_t type = InventoryItemType(bytes);
    if (type == NoInventoryItem)
        return {};
    const auto record = ItemRecordOf(type, resources, where);
    if (!record.HasValue())
        return Core::Unexpected(record.Error());
    const auto width = std::to_integer<std::int32_t>(record.Value()[ItemWidthOffset]);
    const auto height = std::to_integer<std::int32_t>(record.Value()[ItemHeightOffset]);
    // 0x5bd352..0x5bd36d: the origin's column and row (% 8 and / 8 signed; the origin is not negative), with 8 even if
    // the grid has other columns.
    const auto column = static_cast<std::uint8_t>(origin & 7);
    const auto row = static_cast<std::uint8_t>(origin >> 3);
    for (std::int32_t y = 0; y < height; ++y)
    {
        for (std::int32_t x = 0; x < width; ++x)
        {
            const std::int32_t cell = y * columns + origin + x;
            if (cell < 0 || cell >= static_cast<std::int32_t>(cells.size()))
            {
                return Core::Unexpected(std::format("{}: el tipo {:#x} ({} por {}) desde el origen {} escribe la "
                                                    "casilla {}, fuera de las {} de la grilla (Q-C200)",
                                                    where, type, width, height, origin, cell, cells.size()));
            }
            ItemState& item = cells[static_cast<std::size_t>(cell)];
            item.flags1c = static_cast<std::uint8_t>(Byte(bytes, 4));
            item.gridColumn = column;
            item.gridRow = row;
            item.durability = static_cast<std::uint8_t>(Byte(bytes, 2));
            item.id = type;
            item.flags1b = static_cast<std::uint8_t>(Byte(bytes, 3));
            item.occupied = y == 0 && x == 0 ? 1u : 0u;
            // 0x5bd376..0x5bd3c5: B1 and B3 with the upper 24 bits of cell · 9 (what the index leaves in EAX,
            // 0x5bd378), and B4 with the type's (0x5bd384).
            const auto upper = static_cast<std::uint32_t>(cell * 9) & UpperBits;
            const Kernel::ItemInitializationScalars scalars{
                .b1 = upper | Byte(bytes, 1), .b3 = upper | Byte(bytes, 3), .b4 = (type & UpperBits) | Byte(bytes, 4)};
            if (auto initialized = InitializeInventoryItem(item, scalars, resources, where); !initialized.HasValue())
                return initialized;
        }
    }
    return {};
}

Core::Expected<std::optional<std::int32_t>, std::string>
FirstCellPastTheGrid(std::size_t cells, std::int32_t columns, std::int32_t origin, InventoryItemBytes bytes,
                     const Scenes::WorldEntryResources& resources, std::string_view where)
{
    const std::uint16_t type = InventoryItemType(bytes);
    if (type == NoInventoryItem)
        return std::optional<std::int32_t>{};
    const auto record = ItemRecordOf(type, resources, where);
    if (!record.HasValue())
        return Core::Unexpected(record.Error());
    const auto width = std::to_integer<std::int32_t>(record.Value()[ItemWidthOffset]);
    const auto height = std::to_integer<std::int32_t>(record.Value()[ItemHeightOffset]);
    for (std::int32_t y = 0; y < height; ++y)
    {
        for (std::int32_t x = 0; x < width; ++x)
        {
            const std::int32_t cell = y * columns + origin + x;
            if (cell < 0 || cell >= static_cast<std::int32_t>(cells))
                return std::optional<std::int32_t>{cell};
        }
    }
    return std::optional<std::int32_t>{};
}

Core::Expected<std::optional<std::string>, std::string>
InventoryPlacementPastTheGrid(std::uint8_t slot, InventoryItemBytes bytes, const Scenes::WorldEntryResources& resources)
{
    if (slot < FirstInventoryGridSlot)
        return std::optional<std::string>{};
    const Kernel::OwnInventory& inventory = resources.world.Inventory();
    const bool first = slot < FirstSecondGridSlot;
    const std::size_t cells = first ? inventory.cells.size() : inventory.secondCells.size();
    const std::int32_t origin = first ? slot - FirstInventoryGridSlot : slot - FirstSecondGridSlot;
    const std::string_view grid = first ? "0x78a9bb0" : "0x78ab0b8";
    constexpr auto Columns = static_cast<std::int32_t>(Kernel::InventoryColumns);
    auto past =
        FirstCellPastTheGrid(cells, Columns, origin, bytes, resources, std::format("0x5bc740, la grilla de {}", grid));
    if (!past.HasValue())
        return Core::Unexpected(std::move(past).Error());
    if (!past.Value())
        return std::optional<std::string>{};
    return std::optional<std::string>{std::format("0x5bc740: el tipo {:#x} desde el lugar {} escribiría la casilla {} "
                                                  "de {}, fuera de sus {} (Q-C200)",
                                                  InventoryItemType(bytes), slot, *past.Value(), grid, cells)};
}

std::uint16_t InventoryItemType(InventoryItemBytes bytes)
{
    const std::uint32_t highBit = (Byte(bytes, 3) & TypeHighBitMask) << TypeHighBitShift;
    const std::uint32_t highNibble = (Byte(bytes, 5) & TypeHighNibbleMask) << TypeHighNibbleShift;
    return static_cast<std::uint16_t>(Byte(bytes, 0) | highBit | highNibble);
}

Core::Expected<Actors::ActorStopResult, std::string> PlaceInventoryItem(std::uint8_t slot, InventoryItemBytes bytes,
                                                                        const Scenes::WorldEntryResources& resources)
{
    const std::uint16_t type = InventoryItemType(bytes);
    if (type == NoInventoryItem)
    {
        return Actors::ActorStopResult{};
    }
    if (slot < FirstInventoryGridSlot)
    {
        return PlaceInEquipment(slot, type, bytes, resources);
    }
    Kernel::OwnInventory& inventory = resources.world.Inventory();
    const bool first = slot < FirstSecondGridSlot;
    constexpr auto Columns = static_cast<std::int32_t>(Kernel::InventoryColumns);
    const auto placed =
        first ? PlaceInGrid(inventory.cells, Columns, slot - FirstInventoryGridSlot, type, bytes,
                            GridScalars::Inventory, resources, "0x5bc740, la grilla de 0x78a9bb0 (0x5bcc11)")
              : PlaceInGrid(inventory.secondCells, Columns, slot - FirstSecondGridSlot, type, bytes,
                            GridScalars::Inventory, resources, "0x5bc740, la grilla de 0x78ab0b8 (0x5bcf99)");
    if (!placed.HasValue())
    {
        return Core::Unexpected(placed.Error());
    }
    if (auto speeds = UpdateTheSpeedsAfterTheGrid(resources); !speeds.HasValue())
    {
        return Core::Unexpected(speeds.Error());
    }
    return Actors::ActorStopResult{};
}

Core::Expected<void, std::string> PlaceInTheNpcGrid(std::span<Kernel::ItemState> cells, std::uint8_t slot,
                                                    InventoryItemBytes bytes,
                                                    const Scenes::WorldEntryResources& resources)
{
    const std::uint16_t type = InventoryItemType(bytes);
    if (type == NoInventoryItem)
        return {};
    return PlaceInGrid(cells, static_cast<std::int32_t>(Kernel::InventoryColumns), slot, type, bytes,
                       GridScalars::NpcGrid, resources, "0x5bc150, la grilla de 0x78a7070");
}
bool DiscardGridOverflow(std::size_t cells, std::int32_t columns, std::int32_t origin, InventoryItemBytes bytes,
                         const Scenes::WorldEntryResources& resources, std::string_view where)
{
    const auto past = FirstCellPastTheGrid(cells, columns, origin, bytes, resources, where);
    if (!past.HasValue() || !past.Value())
        return false;
    resources.log.Warning("Q-C200: se descarta el paquete {}: la casilla {} cae fuera de la grilla", where,
                          *past.Value());
    return true;
}

bool DiscardInventoryOverflow(std::uint8_t slot, InventoryItemBytes bytes, const Scenes::WorldEntryResources& resources)
{
    const auto past = InventoryPlacementPastTheGrid(slot, bytes, resources);
    if (!past.HasValue() || !past.Value())
        return false;
    resources.log.Warning("Q-C200: se descarta el paquete de inventario: {}", *past.Value());
    return true;
}
} // namespace Client::Items
