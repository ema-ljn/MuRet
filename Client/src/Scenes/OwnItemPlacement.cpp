#include "Scenes/OwnItemPlacement.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Tables/ItemTable.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorActions.h"
#include "Scenes/ActorEquipment.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/DarkHorse.h"
#include "Scenes/HelperTable.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/OwnStatHelpers.h"
#include "Scenes/SceneId.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "Scenes/WorldTransitionSupport.h"
#include "World/ItemState.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Client::Scenes
{
namespace
{
using World::ItemState;

// 0x54b3a0: d[0] + 2 · (((d[5] & 0xf0) << 4) + (d[3] & 0x80)).
constexpr std::uint32_t TypeHighBitMask = 0x80;
constexpr std::uint32_t TypeHighNibbleMask = 0xf0;
constexpr int TypeHighBitShift = 1;
constexpr int TypeHighNibbleShift = 5;

// El registro de Item.bmd (*[0x7758550] + tipo · 0x54): el ancho en +0x24 y el alto en +0x25 (0x5bcc38,
// 0x5bcc4f).
constexpr std::size_t ItemWidthOffset = 0x24;
constexpr std::size_t ItemHeightOffset = 0x25;

// 0x5bc8a0..0x5bc8ad y 0x420d1a..0x420d22: en los mapas 18 a 23 no crea mascotas ni cuervo.
constexpr std::int32_t FirstChaosCastleMap = 18;
constexpr std::int32_t LastChaosCastleMap = 23;
// Los lugares que 0x5bc740 mira después de 0x512a30 (0x5bc897, 0x5bca53, 0x5bca6c y 0x5bca71).
constexpr std::uint8_t LeftHandSlot = 1;
constexpr std::uint8_t PetSlot = 8;
constexpr std::uint8_t FirstRingSlot = 10;
constexpr std::uint8_t SecondRingSlot = 11;
// Item.bmd: Gaurdian Angel (0x1a00), Horn of Unilia (0x1a02), Horn of Dinorant (0x1a03), Dark Horse
// (0x1a04), Dark Raven (0x1a05), Wizard's Ring (0x1a14) y Horn of Doom (0x1a25, el que viste a Fenrir).
constexpr std::uint16_t GuardianAngel = 0x1a00;
constexpr std::uint16_t HornOfUnilia = 0x1a02;
constexpr std::uint16_t HornOfDinorant = 0x1a03;
constexpr std::uint16_t DarkHorse = 0x1a04;
constexpr std::uint16_t DarkRaven = 0x1a05;
constexpr std::uint16_t WizardsRing = 0x1a14;
constexpr std::uint16_t HornOfDoom = 0x1a25;
// Los ayudantes que crea 0x443fa0 con cada una (0x5bc8dc, 0x5bc94a, 0x5bc95d, 0x5bc9a0 y 0x5bc9ec..0x5bca07):
// el de Fenrir, el 0x14a con el +0x1b del ítem en 1, el 0x14c con 2 y el 0x14b con otro.
constexpr std::uint16_t GuardianAngelHelper = 0x1c12;
constexpr std::uint16_t UniliaHelper = 0xc6;
constexpr std::uint16_t DinorantHelper = 0x110;
constexpr std::uint16_t FenrirFirstHelper = 0x14a;
constexpr std::uint16_t FenrirSecondHelper = 0x14c;
constexpr std::uint16_t FenrirOtherHelper = 0x14b;
constexpr std::uint8_t FenrirFirstKind = 1;
constexpr std::uint8_t FenrirSecondKind = 2;
// 0x5bca8d..0x5bca93: D+4 & ~7 en 0x18, el nivel 3.
constexpr std::uint32_t LevelBitsMask = ~std::uint32_t{7};
constexpr std::uint32_t LevelThreeBits = 0x18;
// 0x54cc5c: 0x54bf40 llama a 0x4225d0 con este tipo.
constexpr std::uint16_t Item4225d0Type = DarkHorse;
// Los 24 bits de arriba de los escalares que 0x5bc740 pone en la pila con el byte de abajo de los datos.
constexpr std::uint32_t UpperBits = ~std::uint32_t{0xff};

std::uint32_t Byte(InventoryItemBytes bytes, std::size_t index)
{
    return std::to_integer<std::uint32_t>(bytes[index]);
}

bool InChaosCastle(std::int32_t map)
{
    return map >= FirstChaosCastleMap && map <= LastChaosCastleMap;
}

Core::Expected<std::span<const std::byte, Data::Tables::ItemRecordSize>, std::string>
ItemRecordOf(std::uint16_t type, const WorldEntryResources& resources, std::string_view where)
{
    if (resources.items == nullptr)
    {
        return Core::Unexpected(std::format("{}: lee *[0x7758550] (Item.bmd) y no está cargado", where));
    }
    const std::optional<std::span<const std::byte, Data::Tables::ItemRecordSize>> record =
        resources.items->Record(type);
    if (!record)
    {
        return Core::Unexpected(std::format("{}: el tipo {:#x} lee fuera de Item.bmd", where, type));
    }
    return *record;
}

// 0x54bf40(item, B1, B3, B4) desde 0x5bc740. contextWord es [0x7681eb0] + 0xe, el nivel del registro
// propio, que solo vale con el registro abierto: cerrado, el último cierre dejó rand() & 0xff en cada byte
// (Q-C125), y MuRet no lo tiene (Q-C202).
Core::Expected<void, std::string> InitializeInventoryItem(ItemState& item, World::ItemInitializationScalars scalars,
                                                          const WorldEntryResources& resources, std::string_view where)
{
    const auto record = ItemRecordOf(item.id, resources, std::format("{}, 0x54bf40", where));
    if (!record.HasValue())
    {
        return Core::Unexpected(record.Error());
    }
    if (item.id == Item4225d0Type)
    {
        return Core::Unexpected(std::format("{}: 0x54bf40 llama a 0x4225d0 (0x54cc5c) con el tipo {:#x}, y MuRet no "
                                            "lo tiene: no corre 0x54bf40",
                                            where, item.id));
    }
    const WorldRuntime& runtime = resources.world.Runtime();
    if (World::ReadsTheContextWord(item.id, scalars.b3) && runtime.ownRecordDepth == 0)
    {
        return Core::Unexpected(std::format("{}: 0x54bf40 lee el nivel de [0x7681eb0] + 0xe (0x54cd97) con el registro "
                                            "propio cerrado, lo que dejó su último cierre (Q-C202): no corre 0x54bf40",
                                            where));
    }
    World::InitializeItemState(item, record.Value(), scalars, resources.world.OwnCharacter().level, {});
    return {};
}

// 0x5bc840..0x5bc873: lo que escribe en un lugar del equipo antes de 0x54bf40.
void WriteEquipmentFields(ItemState& item, std::uint16_t type, InventoryItemBytes bytes)
{
    item.durability = static_cast<std::uint8_t>(Byte(bytes, 2));
    item.flags1c = static_cast<std::uint8_t>(Byte(bytes, 4));
    item.id = type;
    item.flags1b = static_cast<std::uint8_t>(Byte(bytes, 3));
    item.occupied = 1;
}

// 0x5bc8b3..0x5bca0c: el ayudante de cada mascota del lugar 8; el Horn of Doom elige el de Fenrir por el
// +0x1b del ítem (0x5bc9d6..0x5bca07). Las demás no crean nada.
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
        return DarkHorseModel;
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

// 0x5bc88b..0x5bcaa7, con el héroe que relee de 0x743a170: lo que crea o suelta después de vestirlo.
Core::Expected<void, std::string> AfterTheDressing(std::uint8_t slot, const ItemState& item,
                                                   const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    const WorldRuntime& runtime = world.Runtime();
    ActorState& hero = world.Actors()[runtime.hero];
    const std::int32_t map = runtime.map;
    const std::uint16_t type = item.id;
    if (slot == PetSlot && !InChaosCastle(map))
    {
        if (type == HornOfDoom)
        {
            // 0x5bc9d6..0x5bc9db: el byte +0x1b del ítem a +0x31b, el de la mascota, antes de crearla.
            hero.pet.flags1b = item.flags1b;
        }
        const std::optional<std::uint16_t> helper = HelperOfThePet(item);
        if (!helper)
        {
            return {};
        }
        // 0x443fa0(ayudante, héroe + 0x10, héroe, 0, 0).
        if (auto created = CreateHelper(world, *helper, hero.position, runtime.hero); !created.HasValue())
        {
            return Core::Unexpected(std::format("0x5bc740 (0x5bc8cc), 0x443fa0: {}", created.Error()));
        }
        // 0x5bc967..0x5bca46: fuera del 0x1c12, con +0x3e2 del héroe publicado en 0, 0x518cf0 crea el
        // efecto 0x61e en el héroe, aunque 0x443fa0 no haya creado nada.
        if (type != GuardianAngel && hero.safeZone == 0)
        {
            return Core::Unexpected(std::string("0x5bca46: fuera de la zona segura, 0x518cf0 crea el efecto 0x61e "
                                                "sobre el héroe, que World::ModelEffects no tiene"));
        }
        return {};
    }
    if (slot == LeftHandSlot && type == DarkRaven)
    {
        // 0x5bca62: 0x420cf0 suelta el cuervo de +0x430 (0x421d60) y, fuera de los mapas 18 a 23, lo crea de nuevo.
        if (auto raven = ReplaceTheRaven(hero, resources); !raven.HasValue())
        {
            return Core::Unexpected(std::format("0x5bc740 (0x5bca62), {}", raven.Error()));
        }
        return {};
    }
    const bool ring = slot == SecondRingSlot || slot == FirstRingSlot;
    if (ring && type == WizardsRing && (item.scalar1Low & LevelBitsMask) == LevelThreeBits)
    {
        return Core::Unexpected(std::format("0x5bc740 (0x5bca76..0x5bcaa7): el {:#x} de nivel 3 en el lugar {} mira "
                                            "+0x360 del héroe y llama a 0x414340 (+0x364), que MuRet no tiene",
                                            type, slot));
    }
    return {};
}

// 0x5bc77f..0x5bcc0c: un lugar del equipo, con el registro abierto en línea durante todo el camino y el
// cierre en línea de 0x5bcaae (el lazo de 0x40b4d0, con su rand() en 0x5bcbd5).
Core::Expected<ActorStopResult, std::string>
PlaceInEquipment(std::uint8_t slot, std::uint16_t type, InventoryItemBytes bytes, const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    WorldRuntime& runtime = world.Runtime();
    const Render::Models::LogicalAccess access(runtime.ownRecordDepth, resources.cloth.random,
                                               Render::Models::OwnRecordCloseRandoms);
    ItemState& item = world.OwnCharacter().equipment[slot];
    WriteEquipmentFields(item, type, bytes);
    // 0x5bc866..0x5bc86b: B1 y B4 con los 24 bits de arriba de [0x7681eb4], que 0x54bf40 no lee, y B3 con
    // los de lugar · 9, que con el lugar de 0 a 11 son 0.
    const World::ItemInitializationScalars scalars{
        .b1 = Byte(bytes, 1), .b3 = ((slot * 9u) & UpperBits) | Byte(bytes, 3), .b4 = Byte(bytes, 4)};
    if (auto initialized = InitializeInventoryItem(item, scalars, resources, "0x5bc740 (0x5bc87a)");
        !initialized.HasValue())
    {
        return Core::Unexpected(initialized.Error());
    }
    const Render::Models::Skeleton* player = resources.models.SkeletonOf(Data::Models::PlayerSkeletonModel);
    if (player == nullptr)
    {
        return Core::Unexpected(std::string("0x5bc740 (0x5bc886): 0x512a30 necesita Player.bmd (0x20a) para 0x4f2250"));
    }
    ActorState& hero = world.Actors()[runtime.hero];
    const ActorEquipmentContext equipment{.own = world.OwnCharacter(),
                                          .ownRecordAccesses = runtime.ownRecordDepth,
                                          .publishedHero = hero,
                                          .fullSetScan = runtime.fullSetScan,
                                          .stop = ActorStopContext{.runtime = runtime,
                                                                   .scene = SceneId::Game,
                                                                   .playerSkeleton = *player,
                                                                   .items = resources.items,
                                                                   .random = resources.cloth.random},
                                          .itemSetTables = resources.itemSetTables,
                                          .itemSets = &world.ItemSets()};
    const auto dressed = ApplyActorEquipment(hero, equipment);
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

// Una de las dos grillas: la primera desde el lugar 12 (0x5bcc1a..0x5bcd05) y la segunda desde el 76
// (0x5bcf99..0x5bd085). El origen es el lugar menos el primero de la grilla; la columna y la fila, las del
// origen (% 8 y / 8 fijos, con signo). Cada casilla es fila · 8 + origen + columna, sin tope: un ítem que
// pasa de la última columna sigue en la fila de abajo (Q-C201), y uno que pasa de la última casilla escribe
// fuera de la grilla (Q-C200).
Core::Expected<void, std::string> PlaceInGrid(std::span<ItemState> cells, std::int32_t origin, std::uint16_t type,
                                              InventoryItemBytes bytes, const WorldEntryResources& resources,
                                              std::string_view where)
{
    const auto record = ItemRecordOf(type, resources, where);
    if (!record.HasValue())
    {
        return Core::Unexpected(record.Error());
    }
    const auto width = std::to_integer<std::int32_t>(record.Value()[ItemWidthOffset]);
    const auto height = std::to_integer<std::int32_t>(record.Value()[ItemHeightOffset]);
    constexpr auto Columns = static_cast<std::int32_t>(InventoryColumns);
    const auto column = static_cast<std::uint8_t>(origin % Columns);
    const auto row = static_cast<std::uint8_t>(origin / Columns);
    for (std::int32_t y = 0; y < height; ++y)
    {
        const std::int32_t rowBase = y * Columns + origin;
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
            // B1 y B3 con los 24 bits de arriba de fila · 8 + origen, y B4 con los del tipo (0x5bcc9c, 0x5bccd5).
            const auto upper = static_cast<std::uint32_t>(rowBase) & UpperBits;
            const World::ItemInitializationScalars scalars{
                .b1 = upper | Byte(bytes, 1), .b3 = upper | Byte(bytes, 3), .b4 = (type & UpperBits) | Byte(bytes, 4)};
            if (auto initialized = InitializeInventoryItem(item, scalars, resources, where); !initialized.HasValue())
            {
                return initialized;
            }
        }
    }
    return {};
}

// 0x5bcd0b..0x5bcf94 y 0x5bd08b..0x5bd302: con [0x78a6758] en menos de 12, abre el registro propio en línea,
// llama a 0x54fe10 una vez por cada WORD, +0x3e y +0x42, que no sea 0 (0x5bcdd9 y 0x5bcdf0), y lo cierra
// con 0x40b4d0 (0x5bcf8f). El ítem de 0x7879f80 no reemplaza nada: [0x7879fbc] está en 0
// (OwnStatsContext::held).
Core::Expected<void, std::string> UpdateTheSpeedsAfterTheGrid(const WorldEntryResources& resources)
{
    WorldRuntime& runtime = resources.world.Runtime();
    if (runtime.slot78a6758 >= static_cast<std::int32_t>(FirstInventoryGridSlot))
    {
        return {};
    }
    const Render::Models::LogicalAccess access(runtime.ownRecordDepth, resources.cloth.random,
                                               Render::Models::OwnRecordCloseRandoms);
    OwnCharacterRecord& own = resources.world.OwnCharacter();
    for (const std::uint16_t* word : {&own.word3e, &own.word42})
    {
        if (*word == 0)
        {
            continue;
        }
        if (auto updated = UpdateOwnAttackAndMagicSpeeds(own, OwnEquipmentOverride{}, runtime.ownRecordDepth,
                                                         resources.cloth.random);
            !updated.HasValue())
        {
            return updated;
        }
    }
    return {};
}
} // namespace

std::uint16_t InventoryItemType(InventoryItemBytes bytes)
{
    const std::uint32_t highBit = (Byte(bytes, 3) & TypeHighBitMask) << TypeHighBitShift;
    const std::uint32_t highNibble = (Byte(bytes, 5) & TypeHighNibbleMask) << TypeHighNibbleShift;
    return static_cast<std::uint16_t>(Byte(bytes, 0) | highBit | highNibble);
}

Core::Expected<ActorStopResult, std::string> PlaceInventoryItem(std::uint8_t slot, InventoryItemBytes bytes,
                                                                const WorldEntryResources& resources)
{
    const std::uint16_t type = InventoryItemType(bytes);
    if (type == NoInventoryItem)
    {
        return ActorStopResult{};
    }
    if (slot < FirstInventoryGridSlot)
    {
        return PlaceInEquipment(slot, type, bytes, resources);
    }
    OwnInventory& inventory = resources.world.Inventory();
    const bool first = slot < FirstSecondGridSlot;
    const auto placed = first ? PlaceInGrid(inventory.cells, slot - FirstInventoryGridSlot, type, bytes, resources,
                                            "0x5bc740, la grilla de 0x78a9bb0 (0x5bcc11)")
                              : PlaceInGrid(inventory.secondCells, slot - FirstSecondGridSlot, type, bytes, resources,
                                            "0x5bc740, la grilla de 0x78ab0b8 (0x5bcf99)");
    if (!placed.HasValue())
    {
        return Core::Unexpected(placed.Error());
    }
    if (auto speeds = UpdateTheSpeedsAfterTheGrid(resources); !speeds.HasValue())
    {
        return Core::Unexpected(speeds.Error());
    }
    return ActorStopResult{};
}
} // namespace Client::Scenes
