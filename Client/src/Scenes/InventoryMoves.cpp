#include "Scenes/InventoryMoves.h"

#include "App/WindowState.h"
#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Tables/ItemTable.h"
#include "Protocol/Inventory.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorState.h"
#include "Scenes/ChatReceive.h"
#include "Scenes/GameMessages.h"
#include "Scenes/HelperTable.h"
#include "Scenes/NpcWindowState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/OwnItemPlacement.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "Scenes/WorldTransitionSupport.h"
#include "UI/GameText.h"
#include "UI/GameUiState.h"
#include "UI/MessageArea.h"
#include "World/Ftol.h"
#include "World/ItemState.h"

#include <algorithm>
#include <array>
#include <bit>
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

// El registro de Item.bmd (*[0x7758550] + tipo · 0x54): el lugar del equipo (0x5bee1a), el ancho, el alto y el
// byte que mira el Dark Lord (0x5bee96).
constexpr std::size_t ItemEquipSlotOffset = 0x22;
constexpr std::size_t ItemWidthOffset = 0x24;
constexpr std::size_t ItemHeightOffset = 0x25;
constexpr std::size_t ItemDarkLordOffset = 0x49;

// 0x5bd75c: el lugar que no es ninguno.
constexpr std::int32_t NoSlot = 0xff;
constexpr auto Columns = static_cast<std::int32_t>(InventoryColumns);
constexpr auto Rows = static_cast<std::int32_t>(InventoryCellCount / InventoryColumns);
constexpr auto FirstGridSlot = static_cast<std::int32_t>(FirstInventoryGridSlot);
constexpr auto FirstSecondSlot = static_cast<std::int32_t>(FirstSecondGridSlot);
// 0x6b8ad0 (0.05f) y 0x6b882c (0.5f): la casilla de 20 píxeles y el medio del ítem.
constexpr double CellScale = static_cast<double>(0.05f);
constexpr double Half = static_cast<double>(0.5f);

// 0x41ed10: mover un ítem y el F3 14.
constexpr std::int32_t ItemMoveSound = 0x1d;
constexpr std::int32_t ItemPutSound = 0x45;
// +0x44: entra (2), no entra (3) y la joya que podría ir sobre otro ítem (4).
// 0x5cf3c9..0x5cf40b: lo que no va a otra grilla que el inventario: 0x1c17 a 0x1c1a, 0x1a13, 0x1a14 y el 0x1c0b de
// nivel 13 (+4 & 0x78 en 0x68).
constexpr std::int32_t FirstUnmovedTicket = 0x1c17;
constexpr std::int32_t LastUnmovedTicket = 0x1c1a;
constexpr std::uint16_t FirstUnmovedOrb = 0x1a13;
constexpr std::uint16_t SecondUnmovedOrb = 0x1a14;
constexpr std::uint16_t UnmovedAtThirteen = 0x1c0b;
constexpr std::uint32_t LevelBits = 0x78;
constexpr std::uint32_t ThirteenBits = 0x68;
// 0x5cb3d8: los cambios del teclado del almacén, y sus diez teclas (rand() % 10).
constexpr int KeypadSwaps = 20;
constexpr int KeypadKeys = 10;
constexpr std::uint8_t FitsMark = 2;
constexpr std::uint8_t RefusedMark = 3;
constexpr std::uint8_t JewelMark = 4;

// Los lugares del equipo que se miran por separado.
constexpr std::size_t RightHandSlot = 0;
constexpr std::size_t LeftHandSlot = 1;
constexpr std::size_t PetSlot = 8;
constexpr std::size_t FirstRingSlot = 10;
constexpr std::size_t SecondRingSlot = 11;
constexpr std::uint8_t NoEquipSlot = 0;

// Item.bmd: Guardian Angel (0x1a00), Horn of Unilia (0x1a02), Horn of Dinorant (0x1a03), Dark Horse (0x1a04), Dark
// Raven (0x1a05), Wizard's Ring (0x1a14) y Horn of Doom (0x1a25).
constexpr std::uint16_t GuardianAngel = 0x1a00;
constexpr std::uint16_t HornOfUnilia = 0x1a02;
constexpr std::uint16_t HornOfDinorant = 0x1a03;
constexpr std::uint16_t DarkHorseHorn = 0x1a04;
constexpr std::uint16_t DarkRaven = 0x1a05;
constexpr std::uint16_t WizardsRing = 0x1a14;
constexpr std::uint16_t HornOfDoom = 0x1a25;
// Los ayudantes de 0x5be50e..0x5be5ea, los mismos que crea 0x5bc740.
constexpr std::uint16_t GuardianAngelHelper = 0x1c12;
constexpr std::uint16_t UniliaHelper = 0xc6;
constexpr std::uint16_t DinorantHelper = 0x110;
constexpr std::uint16_t DarkHorseHelper = 0xbf;
constexpr std::uint16_t FenrirFirstHelper = 0x14a;
constexpr std::uint16_t FenrirSecondHelper = 0x14c;
constexpr std::uint16_t FenrirOtherHelper = 0x14b;
constexpr std::uint8_t FenrirFirstKind = 1;
constexpr std::uint8_t FenrirSecondKind = 2;
// +4 & ~7 en 0x18: el nivel 3 del anillo; (+4 >> 3) & 0xf, el nivel.
constexpr std::uint32_t LevelBitsMask = ~std::uint32_t{7};
constexpr std::uint32_t LevelThreeBits = 0x18;
constexpr int LevelShift = 3;
constexpr std::uint32_t LevelMask = 0xf;
// +0x360 del héroe: 1 a 3 dejan el anillo del lugar 10 sin mirar; 0x5be65c y 0x5bcaa7 lo dejan en 4.
constexpr std::uint8_t FirstRingLayer = 1;
constexpr std::uint8_t LastRingLayer = 3;
constexpr std::uint8_t WizardsRingLayer = 4;
// La tail de 0x5bc740 con el último argumento en 0: debajo de 6 suelta la capa; arriba de 3, +0x360 en 0.
constexpr std::uint8_t LayerLimit = 6;

// Los que se apilan (0x5c9b40 y 0x5ca4c9..0x5ca542): las pociones y los otros de 0x1c00 a 0x1c08, las flechas y
// los virotes con el mismo nivel, el 0x1c1d y los de 0x1a20 a 0x1a22.
constexpr std::uint16_t FirstPotion = 0x1c00;
constexpr std::uint16_t LastPotion = 0x1c08;
constexpr std::uint16_t Bolts = 0x807;
constexpr std::uint16_t Arrows = 0x80f;
constexpr std::uint16_t StackedAlone = 0x1c1d;
constexpr std::uint16_t FirstStackedBox = 0x1a20;
constexpr std::uint16_t LastStackedBox = 0x1a22;
// Las joyas que se marcan con 4 (0x5ca3fc..0x5ca418): Bless, Soul, Life y Creation; las tres primeras van sobre
// otro ítem (0x5d82a8).
constexpr std::uint16_t JewelOfBless = 0x1c0d;
constexpr std::uint16_t JewelOfSoul = 0x1c0e;
constexpr std::uint16_t JewelOfLife = 0x1c10;
constexpr std::uint16_t JewelOfCreation = 0x1c16;

// 0x5bee04..0x5bf2a0, la prueba del equipo.
constexpr std::uint8_t ElfClass = 2;
constexpr std::uint8_t MagicGladiatorClass = 3;
constexpr std::uint8_t DarkLordClass = 4;
constexpr std::uint8_t DarkWizardClass = 0;
constexpr std::uint8_t ClassMask = 7;
constexpr std::uint8_t ClassUpperMask = 0xf8;
constexpr std::int32_t FirstBow = 0x800;
constexpr std::int32_t BowsEnd = 0xa00;
constexpr std::uint16_t FirstPetForbidden = 0x1a0e;
constexpr std::uint16_t LastPetForbidden = 0x1a13;
// 0x5bf03f: el 0x1a5 es 13 · 32 + 5, el Dark Raven con 32 tipos por grupo; con los 512 de Item.bmd es el 0x1a05, y el
// 0x1a5 es un tipo vacío del grupo 0 (Q-C775). El requisito es el nivel del cuervo ([0x723968]) · 15 + 0xb9.
constexpr std::uint16_t OldDarkRaven = 0x1a5;
constexpr std::uint32_t RavenLeadershipPerLevel = 15;
constexpr std::uint32_t RavenLeadershipBase = 0xb9;
constexpr std::uint8_t FirstLevelCheckedSlot = 7;
constexpr std::uint8_t LastLevelCheckedSlot = 11;
constexpr std::int32_t AtlansMap = 7;
constexpr std::int32_t IcarusMap = 10;
constexpr std::int32_t FirstChaosCastleMap = 18;
constexpr std::int32_t LastChaosCastleMap = 23;
constexpr std::uint8_t FirstCrywolfLockStage = 3;
constexpr std::uint8_t LastCrywolfLockStage = 4;
constexpr std::uint32_t CrywolfLockFlag = 0x200000;
constexpr std::uint16_t FirstRidingAction = 0xc5;
constexpr std::uint16_t LastRidingAction = 0xc8;
constexpr std::int32_t FirstOneHanded = 0;
constexpr std::int32_t OneHandedEnd = 0x600;
constexpr std::int32_t FirstStaff = 0xa00;
constexpr std::int32_t StavesEnd = 0xc00;
constexpr std::int32_t WideItem = 2;
// Text.bmd: 1862 (el de Fenrir), 269 (lo que no se tira al piso), 1915 (el de las cajas), 251 (se acabaron las
// flechas) y 250 (las flechas que se equipan). En el Text.bmd del repo, 1862 y 1915 están vacíos.
constexpr std::size_t FenrirText = 1862;
constexpr std::size_t KeptText = 269;
constexpr std::size_t BoxText = 1915;
constexpr std::size_t ArrowsText = 251;
constexpr std::size_t EquippedArrowsText = 250;

// 0x5579e0 y 0x561840: los arcos de la mano izquierda (0x800..0x806, 0x811, 0x814 y 0x815), que tiran flechas, y las
// ballestas de la derecha (0x808..0x80e y 0x810..0x9ff), que tiran virotes. −1, ninguna casilla.
constexpr std::int32_t FirstBowOfTheLeft = 0x800;
constexpr std::int32_t LastBowOfTheLeft = 0x806;
constexpr std::array<std::int32_t, 3> LateBowsOfTheLeft{0x811, 0x814, 0x815};
constexpr std::int32_t FirstCrossbow = 0x808;
constexpr std::int32_t LastCrossbow = 0x80e;
constexpr std::int32_t FirstLateCrossbow = 0x810;
constexpr std::int32_t LastLateCrossbow = 0x9ff;
constexpr std::int32_t NoArrowCell = -1;
constexpr std::int32_t EmptyHand = -1;
// 0x5df4de..0x5df544: los ocho tipos sueltos que no se tiran, el nivel de más y los bits de +0x1b; 0x5df54a..0x5df554,
// las cajas que avisan con 1915.
constexpr std::array<std::uint16_t, 8> KeptOnTheGround{0x13, 0xa0a, 0x812, 0x40d, 0x180f, 0x1a0e, 0x1a0f, 0x1a13};
constexpr std::uint32_t MaxDroppedLevel = 4;
constexpr std::uint8_t DroppedFlagsMask = 0x3f;
constexpr std::uint16_t LastBox = 0x1a25;
// 0x402b30: la joya del ítem, 2 y 3 para los paquetes de Bless y de Soul (+1); 0x5df574 los manda con JGE a 0x5e06b6,
// como al Jewel of Guardian (0x5df57c).
constexpr std::uint16_t BlessBundle = 0x181e;
constexpr std::uint16_t SoulBundle = 0x181f;
constexpr std::uint16_t JewelOfGuardian = 0x1c1f;

// 0x5c2c8b..0x5c2d25: lo que no se saca en Icarus sin otra cosa que vuele (las alas de 0x1800 a 0x1806 y Cape of
// Lord en el 7; Guardian Angel, Imp, Horn of Dinorant y Dark Horse en el 8) ni en Chaos Castle (0x1a02..0x1a05).
constexpr std::uint16_t FirstWing = 0x1800;
constexpr std::uint16_t LastWing = 0x1806;
constexpr std::uint16_t CapeOfLord = 0x1a1e;
constexpr std::uint16_t Imp = 0x1a01;
constexpr std::size_t WingSlot = 7;

// 0x449c00: la caja del entrenador.
constexpr std::int32_t TrainerWithBox = 3;
// 0x44acd0: lo único que va a la caja del entrenador, la joya del caos, las de la mezcla y 0x1a1f.
constexpr std::array<std::uint16_t, 6> TrainerBoxItems{0x180f, 0x1c0d, 0x1c0e, 0x1c10, 0x1c16, 0x1a1f};

bool InChaosCastle(std::int32_t map)
{
    return map >= FirstChaosCastleMap && map <= LastChaosCastleMap;
}

bool InRange(std::int32_t value, std::int32_t first, std::int32_t last)
{
    return value >= first && value <= last;
}

std::int32_t Signed(std::uint16_t type)
{
    return std::bit_cast<std::int16_t>(type);
}

std::uint32_t LevelOf(const ItemState& item)
{
    return (item.scalar1Low >> LevelShift) & LevelMask;
}

ActorState& HeroOf(WorldState& world)
{
    return world.Actors()[world.Runtime().hero];
}

Core::Expected<std::span<const std::byte, Data::Tables::ItemRecordSize>, std::string>
ItemRecordOf(const Data::Tables::ItemTable* items, std::uint16_t type, std::string_view where)
{
    if (items == nullptr)
        return Core::Unexpected(std::format("{}: lee *[0x7758550] (Item.bmd) y no está cargado", where));
    const std::optional<std::span<const std::byte, Data::Tables::ItemRecordSize>> record = items->Record(type);
    if (!record)
        return Core::Unexpected(std::format("{}: el tipo {:#x} lee fuera de Item.bmd", where, type));
    return *record;
}

Core::Expected<void, std::string> Dress(const InventoryServices& services, std::string_view where)
{
    if (!services.dressTheHero)
        return Core::Unexpected(std::format("{}: 0x512a30 sin el héroe publicado ni Player.bmd", where));
    if (auto dressed = services.dressTheHero(); !dressed.HasValue())
        return Core::Unexpected(std::format("{}, 0x512a30: {}", where, dressed.Error()));
    return {};
}

Core::Expected<void, std::string> Message(const InventoryServices& services, std::size_t text, std::int32_t type,
                                          std::string_view where)
{
    if (!services.systemMessage)
        return Core::Unexpected(std::format("{}: 0x555190 con el texto {} sin el chat", where, text));
    if (auto added = services.systemMessage(text, type); !added.HasValue())
        return Core::Unexpected(std::format("{}, 0x555190: {}", where, added.Error()));
    return {};
}

// Una casilla de `cells` por su índice, o el error de la que se pasa: el original escribe fuera de la grilla.
Core::Expected<ItemState*, std::string> CellAt(std::span<ItemState> cells, std::int32_t index, std::string_view where)
{
    if (index < 0 || index >= static_cast<std::int32_t>(cells.size()))
        return Core::Unexpected(
            std::format("{}: la casilla {} está fuera de las {} de la grilla (Q-C200)", where, index, cells.size()));
    return &cells[static_cast<std::size_t>(index)];
}

// 0x4d35f6..0x4d3630 y 0x4d3917..0x4d3951: la poción del lugar `place` del 26 o del 27, en la casilla place − 12 de
// la grilla del inventario: la durabilidad baja 1 si no es 0 y, en 0, la casilla queda vacía. Sin la huella: solo esa
// casilla.
Core::Expected<void, std::string> SpendThePotion(WorldState& world, std::uint8_t place, std::string_view where)
{
    auto cell = CellAt(world.Inventory().cells, place - FirstGridSlot, where);
    if (!cell.HasValue())
        return Core::Unexpected(cell.Error());
    ItemState& potion = *cell.Value();
    if (potion.durability > 0)
        --potion.durability;
    if (potion.durability == 0)
    {
        potion.id = EmptyOwnItemId;
        potion.occupied = 0;
    }
    return {};
}

// 0x5bd750 en la grilla (0x5bde68.. y 0x5bdfcc..): la huella del ítem de `origin`, desde su columna y su fila.
Core::Expected<void, std::string> RemoveFromTheGrid(const InventoryMoveContext& context, std::span<ItemState> cells,
                                                    std::int32_t origin, std::string_view where)
{
    auto first = CellAt(cells, origin, where);
    if (!first.HasValue())
        return Core::Unexpected(first.Error());
    const ItemState item = *first.Value();
    if (item.id == EmptyOwnItemId)
        return {};
    const auto footprint = ItemFootprintOf(context.services.items, item.id, where);
    if (!footprint.HasValue())
        return Core::Unexpected(footprint.Error());
    for (std::int32_t row = item.gridRow; row < item.gridRow + footprint.Value().height; ++row)
    {
        for (std::int32_t column = item.gridColumn; column < item.gridColumn + footprint.Value().width; ++column)
        {
            auto cell = CellAt(cells, row * Columns + column, where);
            if (!cell.HasValue())
                return Core::Unexpected(cell.Error());
            cell.Value()->id = EmptyOwnItemId;
            cell.Value()->occupied = 0;
        }
    }
    return {};
}

bool IsABowOfTheLeft(std::int32_t hand)
{
    return InRange(hand, FirstBowOfTheLeft, LastBowOfTheLeft) ||
           std::ranges::find(LateBowsOfTheLeft, hand) != LateBowsOfTheLeft.end();
}

bool IsACrossbow(std::int32_t hand)
{
    return InRange(hand, FirstCrossbow, LastCrossbow) || InRange(hand, FirstLateCrossbow, LastLateCrossbow);
}

// 0x5579e0: la casilla de las flechas del elfo. Con el registro propio abierto lee la clase y las dos manos y lo
// cierra (3.848 rand() si queda sin accesos); con otra clase, −1. Busca las flechas (0x80f) con un arco en la
// izquierda; si no, los virotes (0x807) con una ballesta en la derecha. Recorre la grilla del inventario de la
// columna 7 a la 0 y, en cada una, de la fila 7 a la 0, y da la primera casilla de ese tipo con +0x3c mayor que 0;
// sin ninguna, −1. Sin arco ni ballesta el tipo que busca es [ebp-0xc], lo que dejó ahí la búsqueda del registro en
// la tabla de protección al cerrarlo: cuántas entradas miró antes de la suya (Q-C772). MuRet no tiene la tabla: si
// alguna casilla puede ser esa, corta.
Core::Expected<std::int32_t, std::string> FindTheArrows(const InventoryMoveContext& context)
{
    WorldState& world = context.world;
    std::int32_t right = EmptyHand;
    std::int32_t left = EmptyHand;
    {
        const Render::Models::LogicalAccess own(world.Runtime().ownRecordDepth, context.random,
                                                Render::Models::OwnRecordCloseRandoms);
        const OwnCharacterRecord& record = world.OwnCharacter();
        if ((record.characterClass & ClassMask) != ElfClass)
            return NoArrowCell;
        right = Signed(record.equipment[RightHandSlot].id);
        left = Signed(record.equipment[LeftHandSlot].id);
    }
    std::optional<std::uint16_t> wanted;
    if (IsABowOfTheLeft(left))
        wanted = Arrows;
    else if (IsACrossbow(right))
        wanted = Bolts;
    const auto& cells = world.Inventory().cells;
    for (std::int32_t column = Columns - 1; column >= 0; --column)
    {
        for (std::int32_t row = Rows - 1; row >= 0; --row)
        {
            const std::int32_t index = row * Columns + column;
            const ItemState& cell = cells[static_cast<std::size_t>(index)];
            if (static_cast<std::int32_t>(cell.occupied) <= 0)
                continue;
            if (!wanted)
            {
                if (Signed(cell.id) >= 0)
                    return Core::Unexpected(std::format(
                        "0x557c62: sin arco ni ballesta, 0x5579e0 busca el tipo [ebp-0xc], la cuenta de la búsqueda "
                        "del registro propio en la tabla de protección (Q-C772), y la casilla {} ({:#x}) puede serlo",
                        index, cell.id));
                continue;
            }
            if (cell.id == *wanted)
                return index;
        }
    }
    return NoArrowCell;
}

// 0x561b45..0x561b9e y 0x561bcf..0x561c28: las flechas de la casilla `index` al cursor, fuera de la grilla
// (0x5bd750), el destino en 0x77f3cc8, el 24 (0x449d80, que con la traba puesta no hace nada) y el texto 250 con el
// tipo 1. El nombre (0x77f3bdc o 0x77f3be0) va vacío (P-C72 a). 0x78ab9b8, la grilla del cursor, queda como estaba.
Core::Expected<void, std::string> EquipTheArrows(const InventoryMoveContext& context, std::int32_t index,
                                                 std::uint8_t target, std::string_view where)
{
    WorldRuntime& runtime = context.world.Runtime();
    auto cell = CellAt(context.world.Inventory().cells, index, where);
    if (!cell.HasValue())
        return Core::Unexpected(cell.Error());
    runtime.cursorItem = *cell.Value();
    runtime.cursorItemCount = static_cast<std::int32_t>(cell.Value()->occupied);
    runtime.slot78a6758 = index + FirstGridSlot;
    if (auto removed = RemoveInventoryItem(context, index + FirstGridSlot); !removed.HasValue())
        return removed;
    runtime.itemMoveTarget = target;
    if (runtime.ui.field78abb39 == 0)
    {
        if (auto sent = SendTheCursorItem(context, target); !sent.HasValue())
            return sent;
    }
    return Message(context.services, EquippedArrowsText, UI::SystemMessage, where);
}

// 0x561840: con −1 de 0x5579e0, el texto 251 con el tipo 2 (0x561c31). Si no, abre el registro, mira si es un elfo
// con el cursor vacío y lo cierra (3.848 rand() si queda sin accesos, en línea), y recién ahí lee las manos: con el
// arco en la izquierda y la derecha vacía, las flechas van a la derecha (el 0); con una ballesta de 0x808 a 0x80e en
// la derecha, o de 0x810 a 0x9ff con la izquierda vacía, a la izquierda (el 1), aunque esté ocupada. Si el cierre dejó
// el registro sin accesos, las manos son lo que dejó en él (Q-C202): corta.
Core::Expected<void, std::string> UpdateTheArrows(const InventoryMoveContext& context)
{
    const auto found = FindTheArrows(context);
    if (!found.HasValue())
        return Core::Unexpected(found.Error());
    if (found.Value() == NoArrowCell)
        return Message(context.services, ArrowsText, UI::ErrorMessage, "0x561c31");
    WorldState& world = context.world;
    WorldRuntime& runtime = world.Runtime();
    bool equips = false;
    {
        const Render::Models::LogicalAccess own(runtime.ownRecordDepth, context.random,
                                                Render::Models::OwnRecordCloseRandoms);
        equips = (world.OwnCharacter().characterClass & ClassMask) == ElfClass && runtime.cursorItemCount == 0;
    }
    if (!equips)
        return {};
    if (runtime.ownRecordDepth == 0)
        return Core::Unexpected(std::string("0x561b13: 0x561840 lee las manos de [0x7681eb4] (+0xb6c y +0xbb4) con el "
                                            "registro propio cerrado, lo que dejó su último cierre (Q-C202)"));
    const OwnCharacterRecord& record = world.OwnCharacter();
    const std::int32_t right = Signed(record.equipment[RightHandSlot].id);
    const std::int32_t left = Signed(record.equipment[LeftHandSlot].id);
    if (IsABowOfTheLeft(left) && right == EmptyHand)
        return EquipTheArrows(context, found.Value(), static_cast<std::uint8_t>(RightHandSlot), "0x561b99");
    if (InRange(right, FirstCrossbow, LastCrossbow) ||
        (InRange(right, FirstLateCrossbow, LastLateCrossbow) && left == EmptyHand))
        return EquipTheArrows(context, found.Value(), static_cast<std::uint8_t>(LeftHandSlot), "0x561c23");
    return {};
}

// 0x5be4f0..0x5be5f2: el ayudante de cada mascota que vuelve al lugar 8.
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
    case DarkHorseHorn:
        return DarkHorseHelper;
    case HornOfDoom:
        if (item.flags1b == FenrirFirstKind)
            return FenrirFirstHelper;
        return item.flags1b == FenrirSecondKind ? FenrirSecondHelper : FenrirOtherHelper;
    default:
        return std::nullopt;
    }
}

// 0x5be612..0x5be65c y 0x5bca6c..0x5bcaa7: el lugar 11, o el 10 con +0x360 fuera de 1 a 3.
bool RingSlotLooksAtTheLayer(std::size_t slot, const ActorState& hero)
{
    if (slot == SecondRingSlot)
        return true;
    return slot == FirstRingSlot && (hero.byte360 < FirstRingLayer || hero.byte360 > LastRingLayer);
}

bool IsLevelThreeWizardsRing(const ItemState& item)
{
    return item.id == WizardsRing && (item.scalar1Low & LevelBitsMask) == LevelThreeBits;
}

// Las casillas de una grilla de 8 columnas que no es la del inventario: la de la tienda y del almacén (0x78a7070), la
// caja del caos (0x7d2d380) o la del entrenador (0x723c80).
std::span<ItemState> OtherGridCells(NpcWindowState& npc, CursorGrid grid)
{
    if (grid == CursorGrid::ChaosBox)
        return npc.chaosBox;
    if (grid == CursorGrid::TrainerBox)
        return npc.trainerBox;
    return npc.grid;
}

// 0x5be268..0x5be31f (y 0x5be32f.. con la caja del caos, 0x449c80 con la del entrenador): la copia del cursor en su
// huella de la grilla de 8 columnas, desde [0x78a6758] % 8 y / 8 con signo, con +0x3c en 1 solo en la casilla de su
// columna y su fila.
Core::Expected<void, std::string> ReturnToOtherGrid(const WorldRuntime& runtime, std::span<ItemState> cells,
                                                    ItemFootprint footprint)
{
    const ItemState cursor = runtime.cursorItem;
    const std::int32_t column = runtime.slot78a6758 % Columns;
    const std::int32_t row = runtime.slot78a6758 / Columns;
    for (std::int32_t y = row; y < row + footprint.height; ++y)
    {
        for (std::int32_t x = column; x < column + footprint.width; ++x)
        {
            auto cell = CellAt(cells, y * Columns + x, "0x5be150 (0x5be2d9)");
            if (!cell.HasValue())
                return Core::Unexpected(cell.Error());
            *cell.Value() = cursor;
            cell.Value()->occupied = y == cursor.gridRow && x == cursor.gridColumn ? 1u : 0u;
        }
    }
    return {};
}

// 0x5be4e2..0x5bebd7: el ítem del cursor vuelve a [0x78a6758].
Core::Expected<void, std::string> ReturnToTheSlot(const InventoryMoveContext& context, ItemFootprint footprint)
{
    WorldState& world = context.world;
    WorldRuntime& runtime = world.Runtime();
    const ItemState cursor = runtime.cursorItem;
    const std::int32_t slot = runtime.slot78a6758;
    if (slot >= FirstGridSlot)
    {
        // 0x5beb16..0x5bebd7: la columna y la fila del origen, con signo, y la copia en cada casilla de la huella.
        const std::int32_t origin = slot - FirstGridSlot;
        const std::int32_t column = origin % Columns;
        const std::int32_t row = origin / Columns;
        for (std::int32_t y = row; y < row + footprint.height; ++y)
        {
            for (std::int32_t x = column; x < column + footprint.width; ++x)
            {
                auto cell = CellAt(world.Inventory().cells, y * Columns + x, "0x5be150 (0x5beb7f)");
                if (!cell.HasValue())
                    return Core::Unexpected(cell.Error());
                *cell.Value() = cursor;
                cell.Value()->occupied = y == cursor.gridRow && x == cursor.gridColumn ? 1u : 0u;
            }
        }
        return {};
    }
    if (slot < 0)
        return Core::Unexpected(std::format("0x5be150: el lugar {} de 0x78a6758 no es del equipo", slot));
    const auto place = static_cast<std::size_t>(slot);
    ActorState& hero = HeroOf(world);
    bool lookAtTheRing = true;
    if (place == PetSlot)
    {
        const std::optional<std::uint16_t> helper = HelperOfThePet(cursor);
        lookAtTheRing = helper.has_value();
        if (helper)
        {
            if (auto created = CreateHelper(world, *helper, hero.position, runtime.hero, &context.random);
                !created.HasValue())
                return Core::Unexpected(std::format("0x5be150, 0x443fa0: {}", created.Error()));
        }
    }
    else if (place == LeftHandSlot)
    {
        lookAtTheRing = cursor.id == DarkRaven;
        if (lookAtTheRing)
        {
            if (!context.services.replaceTheRaven)
                return Core::Unexpected(std::string("0x5be60a: 0x420cf0 sin el cuervo"));
            if (auto raven = context.services.replaceTheRaven(); !raven.HasValue())
                return Core::Unexpected(std::format("0x5be60a, 0x420cf0: {}", raven.Error()));
        }
    }
    if (lookAtTheRing && RingSlotLooksAtTheLayer(place, hero) && IsLevelThreeWizardsRing(cursor))
    {
        ReleaseTheLayer(hero);
        hero.byte360 = WizardsRingLayer;
    }
    const Render::Models::LogicalAccess own(runtime.ownRecordDepth, context.random,
                                            Render::Models::OwnRecordCloseRandoms);
    world.OwnCharacter().equipment.at(place) = cursor;
    return {};
}

} // namespace

// 0x5bd405..0x5bd706: la cola de 0x5bc740 con el último argumento en 0, en cualquier grilla: con el registro abierto,
// los ayudantes sin mascota, el cuervo sin el lugar 1 y la capa sin el anillo de nivel 3.
void FinishTheMovedItem(const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    WorldRuntime& runtime = world.Runtime();
    ActorState& hero = HeroOf(world);
    const Render::Models::LogicalAccess own(runtime.ownRecordDepth, resources.cloth.random,
                                            Render::Models::OwnRecordCloseRandoms);
    const OwnCharacterRecord& record = world.OwnCharacter();
    if (record.equipment[PetSlot].id == EmptyOwnItemId)
        TurnOffHelpersOf(world.Helpers(), runtime.hero);
    if (record.equipment[LeftHandSlot].id == EmptyOwnItemId)
        hero.darkSpirit.reset();
    if (!IsLevelThreeWizardsRing(record.equipment[SecondRingSlot]) &&
        !IsLevelThreeWizardsRing(record.equipment[FirstRingSlot]) && hero.byte360 < LayerLimit)
    {
        ReleaseTheLayer(hero);
        if (hero.byte360 > LastRingLayer)
            hero.byte360 = 0;
    }
}

Core::Expected<void, std::string> PlaceMovedItem(std::uint8_t slot, const Protocol::Inventory::ItemData& item,
                                                 const WorldEntryResources& resources)
{
    const InventoryItemBytes bytes(item);
    if (InventoryItemType(bytes) == NoInventoryItem)
        return {};
    if (auto placed = PlaceInventoryItem(slot, bytes, resources); !placed.HasValue())
        return Core::Unexpected(placed.Error());
    FinishTheMovedItem(resources);
    return {};
}

Core::Expected<void, std::string> PlaceMovedItemInOtherGrid(std::span<World::ItemState> cells, std::int32_t columns,
                                                            std::uint8_t slot,
                                                            const Protocol::Inventory::ItemData& item,
                                                            const WorldEntryResources& resources,
                                                            std::string_view where)
{
    const InventoryItemBytes bytes(item);
    if (InventoryItemType(bytes) == NoInventoryItem)
        return {};
    if (auto placed = PlaceInOtherGrid(cells, columns, slot, bytes, resources, where); !placed.HasValue())
        return placed;
    FinishTheMovedItem(resources);
    return {};
}

namespace
{
// La copia del cursor y el lugar de origen en una pasada (0x5c52b1.. y 0x5c2d2b..).
void TakeIntoTheCursor(WorldRuntime& runtime, const ItemState& item, std::int32_t slot)
{
    runtime.cursorItem = item;
    runtime.cursorItemCount = static_cast<std::int32_t>(item.occupied);
    runtime.cursorGrid = CursorGrid::Inventory;
    runtime.slot78a6758 = slot;
}

// Lo que tiene un ítem en el cursor bajo el mouse (0x5c52f4.. y 0x5c2d67..): nada.
void ForgetTheHoveredItem(WorldRuntime& runtime)
{
    runtime.hoveredItemCopy.id = EmptyOwnItemId;
    runtime.ui.hoveredItem = {};
    runtime.ui.field78abb28 = 0;
}

// 0x5bee04..0x5bf1db: si el ítem del cursor va en `slot`.
Core::Expected<bool, std::string> FitsTheSlot(const InventoryMoveContext& context, std::size_t slot)
{
    WorldState& world = context.world;
    const WorldRuntime& runtime = world.Runtime();
    const OwnCharacterRecord& own = world.OwnCharacter();
    const ActorState& hero = HeroOf(world);
    const ItemState& cursor = runtime.cursorItem;
    const auto record = ItemRecordOf(context.services.items, cursor.id, "0x5bed9e");
    if (!record.HasValue())
        return Core::Unexpected(record.Error());
    const auto equipSlot = std::to_integer<std::uint8_t>(record.Value()[ItemEquipSlotOffset]);
    const std::int32_t right = Signed(own.equipment[RightHandSlot].id);
    const std::int32_t left = Signed(own.equipment[LeftHandSlot].id);
    const std::int32_t type = Signed(cursor.id);
    const auto heroClass = static_cast<std::uint8_t>(hero.characterClass & ClassMask);
    const auto isBowOrCrossbow = [](std::int32_t hand)
    { return hand >= FirstBow && hand < BowsEnd && hand != Arrows && hand != Bolts; };
    bool ok = true;
    bool skipToTheRequirements = false;
    if (equipSlot != slot && slot != LeftHandSlot && slot != SecondRingSlot)
        ok = false;
    else
    {
        ok = heroClass != DarkLordClass || std::to_integer<std::uint8_t>(record.Value()[ItemDarkLordOffset]) != 0;
        bool failed = (slot == LeftHandSlot && equipSlot != NoEquipSlot && equipSlot != LeftHandSlot) ||
                      (slot == SecondRingSlot && equipSlot != FirstRingSlot && equipSlot != SecondRingSlot);
        if (!failed && (heroClass == DarkLordClass || heroClass == DarkWizardClass) && slot != RightHandSlot &&
            equipSlot == NoEquipSlot)
            failed = true;
        if (failed)
            ok = false;
        else if (cursor.id == Arrows && slot == LeftHandSlot)
        {
            ok = false;
            skipToTheRequirements = true;
        }
        else if (heroClass == ElfClass && slot != RightHandSlot && equipSlot == NoEquipSlot &&
                 (type < FirstBow || type > BowsEnd))
            ok = false;
        else if (cursor.id != Arrows && cursor.id != Bolts && heroClass != MagicGladiatorClass &&
                 (slot == RightHandSlot || slot == LeftHandSlot))
        {
            if (isBowOrCrossbow(right))
                ok = false;
            else if (isBowOrCrossbow(left))
                ok = false;
        }
    }
    if (!skipToTheRequirements && InRange(type, FirstPetForbidden, LastPetForbidden))
        ok = false;
    // 0x5bef8a..0x5bf00f: los requisitos contra el registro propio, en words que dan la vuelta.
    const auto sum = [](std::uint16_t a, std::uint16_t b) { return static_cast<std::uint16_t>(a + b); };
    if (cursor.word1e > sum(own.word2a, own.strength) || cursor.word20 > sum(own.word2c, own.agility) ||
        cursor.word22 > sum(own.word30, own.energy) || cursor.word24 > sum(own.word2e, own.vitality) ||
        cursor.word26 > sum(own.word36, own.leadership) || cursor.word28 > own.level)
        ok = false;
    // 0x5bf013..0x5bf04a: 0x4221a0 copia los 20 bytes de 0x725a44 − tipo · 20 con cualquier tipo, y solo el 0x1a5 los
    // mira: 0x725a44 − 0x1a5 · 20 es 0x723960, la mascota 0 de 0x421db0 (el cuervo), y +8 su nivel. El dword · 15 +
    // 0xb9, en 16 bits (la palabra de arriba no llega), contra el liderazgo (+0x20) sin agregados.
    if (cursor.id == OldDarkRaven)
    {
        const auto required =
            static_cast<std::uint16_t>((runtime.ravenLevel * RavenLeadershipPerLevel + RavenLeadershipBase) & 0xffffu);
        if (required > own.leadership)
            ok = false;
    }
    if (equipSlot >= FirstLevelCheckedSlot && equipSlot <= LastLevelCheckedSlot && cursor.word28 > own.level)
        ok = false;
    // 0x5bf084..0x5bf185: el mapa, Crywolf y la montura.
    const std::int32_t map = runtime.map;
    const bool horn = InRange(type, HornOfUnilia, DarkHorseHorn) || cursor.id == HornOfDoom;
    if (equipSlot == PetSlot && (map == AtlansMap || InChaosCastle(map)) &&
        InRange(type, HornOfUnilia, DarkHorseHorn - 1))
        return false;
    bool lookAtTheMount = false;
    if (equipSlot == PetSlot && map == IcarusMap)
    {
        if (cursor.id == HornOfUnilia)
            return false;
        lookAtTheMount = true;
    }
    else if (InChaosCastle(map))
        lookAtTheMount = false;
    else
        lookAtTheMount = true;
    const bool crywolfLock = runtime.crywolf.stage >= FirstCrywolfLockStage &&
                             runtime.crywolf.stage <= LastCrywolfLockStage &&
                             (hero.flags7c & CrywolfLockFlag) == CrywolfLockFlag;
    if (lookAtTheMount && !crywolfLock)
    {
        if (equipSlot == PetSlot && horn && hero.animation.action >= FirstRidingAction &&
            hero.animation.action <= LastRidingAction)
            return false;
    }
    else if ((equipSlot == PetSlot && horn) || (equipSlot == LeftHandSlot && cursor.id == DarkRaven))
        return false;
    // 0x5bf18c..0x5bf1db.
    if (!ok)
        return false;
    if (equipSlot == slot || (equipSlot == FirstRingSlot && slot == SecondRingSlot))
        return true;
    if (equipSlot != NoEquipSlot || slot != LeftHandSlot)
        return false;
    return std::to_integer<std::int32_t>(record.Value()[ItemWidthOffset]) < WideItem;
}
} // namespace

InventoryServices InventoryServicesOf(const WorldEntryResources& resources)
{
    return InventoryServices{
        .items = resources.items,
        .dressTheHero = [&resources] { return DressTransitionHero(PublishedHero(resources), resources); },
        .replaceTheRaven = [&resources] { return ReplaceTheRaven(PublishedHero(resources), resources); },
        .systemMessage = [&resources](std::size_t index, std::int32_t type) -> Core::Expected<void, std::string>
        {
            const std::optional<std::string_view> text = UI::TextAt(resources.texts, index);
            if (!text)
                return Core::Unexpected(std::format("sin el texto {} de Text.bmd", index));
            if (resources.textMeasure == nullptr)
                return Core::Unexpected(std::string("sin el medidor de los textos"));
            const App::PixelSize screen = resources.screen ? resources.screen() : App::PixelSize{640, 480};
            return AddGameMessage(resources.world.Runtime(), *resources.textMeasure, screen, {}, *text, type);
        },
        .sendItemMove = {}};
}

Core::Expected<ItemFootprint, std::string> ItemFootprintOf(const Data::Tables::ItemTable* items, std::uint16_t type,
                                                           std::string_view where)
{
    const auto record = ItemRecordOf(items, type, where);
    if (!record.HasValue())
        return Core::Unexpected(record.Error());
    return ItemFootprint{.width = std::to_integer<std::int32_t>(record.Value()[ItemWidthOffset]),
                         .height = std::to_integer<std::int32_t>(record.Value()[ItemHeightOffset])};
}

GridCell CellUnderTheMouse(std::int32_t mouseX, std::int32_t mouseY, std::int32_t left, std::int32_t top,
                           ItemFootprint footprint)
{
    const auto axis = [](std::int32_t mouse, std::int32_t corner, std::int32_t size) {
        return World::FtolLow(static_cast<double>(mouse - corner) * CellScale - static_cast<double>(size) * Half +
                              Half);
    };
    return GridCell{.column = axis(mouseX, left, footprint.width), .row = axis(mouseY, top, footprint.height)};
}

bool ItemFitsInTheGrid(const ItemState& cursor, std::span<const ItemState> cells, std::int32_t columns,
                       std::int32_t rows, GridCell cell, ItemFootprint footprint)
{
    std::int32_t empty = 0;
    for (std::int32_t row = cell.row; row < cell.row + footprint.height; ++row)
    {
        for (std::int32_t column = cell.column; column < cell.column + footprint.width; ++column)
        {
            if (row < 0 || column < 0 || row >= rows || column >= columns)
                continue;
            if (cells[static_cast<std::size_t>(row * columns + column)].id == EmptyOwnItemId)
                ++empty;
        }
    }
    if (empty == footprint.width * footprint.height)
        return true;
    const std::int32_t index = cell.row * columns + cell.column;
    if (index < 0 || index >= static_cast<std::int32_t>(cells.size()))
        return false;
    const ItemState& under = cells[static_cast<std::size_t>(index)];
    if (under.id != cursor.id)
        return false;
    const std::int32_t type = Signed(cursor.id);
    if (InRange(type, FirstPotion, LastPotion))
        return true;
    if ((cursor.id == Bolts || cursor.id == Arrows) && under.scalar1Low == cursor.scalar1Low)
        return true;
    return cursor.id == StackedAlone || InRange(type, FirstStackedBox, LastStackedBox);
}

Core::Expected<void, std::string> RemoveInventoryItem(const InventoryMoveContext& context, std::int32_t slot)
{
    if (slot == NoSlot)
        return {};
    WorldState& world = context.world;
    if (slot >= FirstSecondSlot)
        return RemoveFromTheGrid(context, world.Inventory().secondCells, slot - FirstSecondSlot,
                                 "0x5bd750, la grilla de 0x78ab0b8");
    if (slot >= FirstGridSlot)
        return RemoveFromTheGrid(context, world.Inventory().cells, slot - FirstGridSlot,
                                 "0x5bd750, la grilla de 0x78a9bb0");
    if (slot < 0)
        return Core::Unexpected(std::format("0x5bd750: el lugar {} no es del equipo", slot));
    const auto place = static_cast<std::size_t>(slot);
    WorldRuntime& runtime = world.Runtime();
    ActorState& hero = HeroOf(world);
    {
        const Render::Models::LogicalAccess own(runtime.ownRecordDepth, context.random,
                                                Render::Models::OwnRecordCloseRandoms);
        ItemState& item = world.OwnCharacter().equipment.at(place);
        if (item.id == EmptyOwnItemId)
            return {};
        // 0x5bdbc5..0x5bdbd8: el cuervo de +0x430.
        if (place == LeftHandSlot && item.id == DarkRaven)
            hero.darkSpirit.reset();
        item.id = EmptyOwnItemId;
        item.occupied = 0xffffffffu;
        item.flags1b = 0;
        item.flags1c = 0;
    }
    if (place == RightHandSlot || place == LeftHandSlot)
    {
        if (auto arrows = UpdateTheArrows(context); !arrows.HasValue())
            return arrows;
        hero.hands.at(place).model = EmptyOwnItemId;
        hero.hands.at(place).flags1b = 0;
    }
    else if (place == PetSlot)
    {
        TurnOffHelpersOf(world.Helpers(), runtime.hero);
        hero.pet.model = EmptyOwnItemId;
        hero.pet.flags1b = 0;
    }
    return {};
}

Core::Expected<void, std::string> ReturnTheCursorItem(const InventoryMoveContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    if (runtime.cursorItemCount <= 0)
        return {};
    // 0x5be166..0x5be18e: el ancho y el alto del tipo. Con la grilla de la tienda y del almacén o la caja del caos
    // (0x5be25d.. y 0x5be324..), la copia vuelve a su huella desde [0x78a6758] (columna y fila con signo, de a 8);
    // con otra sigue en 0x5be4b4, donde 0x449c80 hace lo mismo con la caja del entrenador y da 0 con las demás.
    const auto footprint = ItemFootprintOf(context.services.items, runtime.cursorItem.id, "0x5be150 (0x5be183)");
    if (!footprint.HasValue())
        return Core::Unexpected(footprint.Error());
    if (runtime.cursorGrid == CursorGrid::NpcGrid || runtime.cursorGrid == CursorGrid::ChaosBox ||
        runtime.cursorGrid == CursorGrid::TrainerBox)
    {
        const std::span<ItemState> cells = OtherGridCells(runtime.npcWindows, runtime.cursorGrid);
        if (auto returned = ReturnToOtherGrid(runtime, cells, footprint.Value()); !returned.HasValue())
            return returned;
    }
    else if (auto returned = ReturnToTheSlot(context, footprint.Value()); !returned.HasValue())
        return returned;
    runtime.cursorItemCount = 0;
    runtime.cursorItem.occupied = 0;
    if (auto dressed = Dress(context.services, "0x5be150 (0x5bebea)"); !dressed.HasValue())
        return dressed;
    runtime.lastSoundId = ItemMoveSound;
    return {};
}

Core::Expected<void, std::string> SendTheCursorItem(const InventoryMoveContext& context, std::uint8_t targetSlot)
{
    const WorldRuntime& runtime = context.world.Runtime();
    return SendTheCursorItemBetween(context, InventoryMoveGrid, static_cast<std::uint8_t>(runtime.slot78a6758 & 0xff),
                                    InventoryMoveGrid, targetSlot);
}

Core::Expected<void, std::string> SendTheCursorItemBetween(const InventoryMoveContext& context, std::uint8_t sourceGrid,
                                                           std::uint8_t sourceSlot, std::uint8_t targetGrid,
                                                           std::uint8_t targetSlot)
{
    WorldRuntime& runtime = context.world.Runtime();
    if (runtime.ui.field78abb39 != 0)
        return {};
    runtime.ui.field78abb39 = 1;
    // El relleno y el serial del C3 (0x40b0a0).
    context.random.Discard(2);
    const ItemState& cursor = runtime.cursorItem;
    const auto byte = [](std::uint32_t value) { return static_cast<std::byte>(value & 0xffu); };
    const Protocol::Inventory::ItemMove move{
        .sourceFlag = sourceGrid,
        .sourceSlot = sourceSlot,
        .item = {byte(cursor.id), byte(cursor.scalar1Low), byte(cursor.durability), byte(cursor.flags1b),
                 byte(cursor.flags1c), byte((static_cast<std::uint32_t>(Signed(cursor.id)) >> 5) & 0xf0u), byte(0)},
        .targetFlag = targetGrid,
        .targetSlot = targetSlot};
    if (!context.services.sendItemMove)
        return Core::Unexpected(std::string("0x449d80: el C1 0E 24 sin la sesión"));
    return context.services.sendItemMove(move);
}

void ShuffleTheKeypad(Render::Models::CrtRandom& random, std::array<std::uint16_t, WarehouseKeyCount>& keys)
{
    for (std::size_t key = 0; key < keys.size(); ++key)
        keys[key] = static_cast<std::uint16_t>(key);
    for (int swap = 0; swap < KeypadSwaps; ++swap)
    {
        const auto first = static_cast<std::size_t>(random.Next() % KeypadKeys);
        const auto second = static_cast<std::size_t>(random.Next() % KeypadKeys);
        if (first != second)
            std::swap(keys[first], keys[second]);
    }
}

Core::Expected<void, std::string> PickUpFromTheGrid(const InventoryMoveContext& context, std::size_t cell)
{
    WorldState& world = context.world;
    WorldRuntime& runtime = world.Runtime();
    runtime.movement.pendingClick = false;
    const ItemState& hovered = world.Inventory().cells.at(cell);
    const std::int32_t topLeft = hovered.gridRow * Columns + hovered.gridColumn;
    auto source = CellAt(world.Inventory().cells, topLeft, "0x5c52b1");
    if (!source.HasValue())
        return Core::Unexpected(source.Error());
    TakeIntoTheCursor(runtime, *source.Value(), topLeft + FirstGridSlot);
    if (auto removed = RemoveInventoryItem(context, topLeft + FirstGridSlot); !removed.HasValue())
        return removed;
    ForgetTheHoveredItem(runtime);
    return {};
}

Core::Expected<void, std::string> PickUpFromOtherGrid(const InventoryMoveContext& context, CursorGrid grid,
                                                      std::size_t cell)
{
    WorldRuntime& runtime = context.world.Runtime();
    const std::span<ItemState> cells = OtherGridCells(runtime.npcWindows, grid);
    runtime.movement.pendingClick = false;
    const ItemState& hovered = cells[cell];
    const std::int32_t topLeft = hovered.gridRow * Columns + hovered.gridColumn;
    auto source = CellAt(cells, topLeft, "0x5c52b1");
    if (!source.HasValue())
        return Core::Unexpected(source.Error());
    TakeIntoTheCursor(runtime, *source.Value(), topLeft);
    runtime.cursorGrid = grid;
    // 0x5c5348: 0x5bd750(lugar, grilla, 8) con un lugar del 0 al 11 no toca el equipo: va por la rama de las otras
    // grillas.
    if (auto removed = RemoveFromTheGrid(context, cells, topLeft, "0x5bd750 (0x5bddf5)"); !removed.HasValue())
        return removed;
    ForgetTheHoveredItem(runtime);
    return {};
}

Core::Expected<void, std::string> PickUpFromTheEquipment(const InventoryMoveContext& context, std::size_t slot)
{
    WorldState& world = context.world;
    WorldRuntime& runtime = world.Runtime();
    if (runtime.movement.gateRequestBlocked != 0)
        return {};
    ItemState& item = world.OwnCharacter().equipment.at(slot);
    if (runtime.panels.repairMode != 0)
        return Core::Unexpected(
            std::format("0x5c2dd5: reparar el {:#x} del lugar {} del equipo es de la CL9.3", item.id, slot));
    runtime.movement.pendingClick = false;
    const std::int32_t type = Signed(item.id);
    const auto isWingOrCape = [](std::int32_t id) { return InRange(id, FirstWing, LastWing) || id == CapeOfLord; };
    if (runtime.map == IcarusMap)
    {
        // 0x5c2c90..0x5c2d0a: en Icarus, con una sola cosa que vuela, esa no se saca.
        const OwnCharacterRecord& own = world.OwnCharacter();
        std::int32_t swimming = isWingOrCape(Signed(own.equipment[WingSlot].id)) ? 1 : 0;
        const std::uint16_t pet = own.equipment[PetSlot].id;
        if (pet == GuardianAngel || pet == Imp || pet == HornOfDinorant || pet == DarkHorseHorn)
            ++swimming;
        if (swimming <= 1 && (isWingOrCape(type) || item.id == HornOfDinorant || item.id == DarkHorseHorn))
            return {};
    }
    else if (InChaosCastle(runtime.map) && InRange(type, HornOfUnilia, DarkRaven))
        return {};
    // 0x5c2d2b..0x5c2d5c: +0x3c queda como estaba.
    TakeIntoTheCursor(runtime, item, static_cast<std::int32_t>(slot));
    item.id = EmptyOwnItemId;
    item.scalar1Low = 0;
    item.flags1b = 0;
    item.flags1c = 0;
    if (auto dressed = Dress(context.services, "0x5c2d62"); !dressed.HasValue())
        return dressed;
    runtime.ui.hoveredItem = {};
    runtime.hoveredItemCopy.id = EmptyOwnItemId;
    runtime.ui.field78abb28 = 0;
    runtime.ui.field78abb2c = static_cast<std::int32_t>(slot) + FirstGridSlot;
    runtime.lastSoundId = ItemMoveSound;
    if (slot == PetSlot)
        TurnOffHelpersOf(world.Helpers(), runtime.hero);
    else if (slot == LeftHandSlot && runtime.cursorItem.id == DarkRaven)
        HeroOf(world).darkSpirit.reset();
    return {};
}

Core::Expected<void, std::string> MoveTheLeftWeaponToTheRight(const InventoryMoveContext& context)
{
    WorldState& world = context.world;
    WorldRuntime& runtime = world.Runtime();
    {
        // 0x59bd3e..0x59bf17: abre el registro propio y lo cierra con su cuenta (3.848 rand() sin otros accesos).
        const Render::Models::LogicalAccess own(runtime.ownRecordDepth, context.random,
                                                Render::Models::OwnRecordCloseRandoms);
        ItemState& left = world.OwnCharacter().equipment[LeftHandSlot];
        runtime.cursorItem = left;
        runtime.cursorItemCount = static_cast<std::int32_t>(left.occupied);
        left.id = EmptyOwnItemId;
        left.scalar1Low = 0;
    }
    if (auto dressed = Dress(context.services, "0x59bf23"); !dressed.HasValue())
        return dressed;
    // 0x59bf28..0x59bf50.
    runtime.cursorGrid = CursorGrid::Inventory;
    runtime.slot78a6758 = static_cast<std::int32_t>(LeftHandSlot);
    runtime.itemMoveTarget = 0;
    if (runtime.ui.field78abb39 != 0)
        return {};
    return SendTheCursorItem(context, static_cast<std::uint8_t>(RightHandSlot));
}

void HoverTheEquipment(const InventoryMoveContext& context, std::size_t slot, std::int32_t left, std::int32_t top,
                       float width)
{
    WorldState& world = context.world;
    WorldRuntime& runtime = world.Runtime();
    const OwnCharacterRecord& own = world.OwnCharacter();
    ItemState& item = world.OwnCharacter().equipment.at(slot);
    // 0x5c35bf..0x5c38ba: +0x26 contra la fuerza (Q-C475), +0x20, +0x22 y +0x1e. Los dos accesos que abre adentro
    // anidan en el de 0x5bec50 y no sacan rand().
    const auto strength = static_cast<std::uint16_t>(own.word2a + own.strength);
    const auto agility = static_cast<std::uint16_t>(own.word2c + own.agility);
    const auto energy = static_cast<std::uint16_t>(own.word30 + own.energy);
    const bool missing =
        item.word26 > strength || item.word20 > agility || item.word22 > energy || item.word1e > strength;
    item.warning = missing ? RefusedMark : FitsMark;
    runtime.ui.hoveredItem = UI::HoveredItem{.place = UI::HoveredItem::Place::Equipment, .index = slot};
    runtime.ui.field78aba27 = 0;
    runtime.hoveredItemCopy = item;
    runtime.ui.field78abb28 = 0;
    runtime.ui.field78abb2c = static_cast<std::int32_t>(slot);
    // 0x5c3acc..0x5c3ae3: _ftol del ancho, / 2 con signo.
    runtime.ui.skillTooltipX = left + World::FtolLow(static_cast<double>(width)) / 2;
    runtime.ui.skillTooltipY = top;
}

Core::Expected<void, std::string> DropOnTheEquipment(const InventoryMoveContext& context, std::size_t slot)
{
    WorldState& world = context.world;
    WorldRuntime& runtime = world.Runtime();
    const auto fits = FitsTheSlot(context, slot);
    if (!fits.HasValue())
        return Core::Unexpected(fits.Error());
    ItemState& place = world.OwnCharacter().equipment.at(slot);
    if (!fits.Value())
    {
        // 0x5bf0ba..0x5bf0e7.
        place.warning = RefusedMark;
        if (!runtime.movement.pendingClick)
            return {};
        runtime.movement.pendingClick = false;
        if (runtime.ui.field78abb39 != 0)
            return {};
        return ReturnTheCursorItem(context);
    }
    // 0x5bf1e1..0x5bf294.
    place.warning = FitsMark;
    if (!runtime.movement.pendingClick)
        return {};
    runtime.ui.field78abaac = 1;
    runtime.movement.pendingClick = false;
    runtime.itemMoveTarget = static_cast<std::int32_t>(slot);
    const ItemState& cursor = runtime.cursorItem;
    const std::int32_t type = Signed(cursor.id);
    const bool weapon = (type >= FirstOneHanded && type < OneHandedEnd) || (type >= FirstStaff && type < StavesEnd);
    if (slot == LeftHandSlot && weapon && world.OwnCharacter().equipment[RightHandSlot].id == EmptyOwnItemId)
        runtime.itemMoveTarget = 0;
    if (runtime.cursorGrid != CursorGrid::Inventory)
        return Core::Unexpected(std::string("0x5c007c: el ítem de otra grilla al equipo es de la CL9.3 y la CL9.4"));
    const auto record = ItemRecordOf(context.services.items, cursor.id, "0x5bf248");
    if (!record.HasValue())
        return Core::Unexpected(record.Error());
    const ActorState& hero = HeroOf(world);
    const auto heroClass = static_cast<std::uint8_t>(hero.characterClass & ClassMask);
    if (std::to_integer<std::uint8_t>(record.Value()[ItemEquipSlotOffset]) == PetSlot && cursor.id == HornOfDoom &&
        heroClass != MagicGladiatorClass && heroClass != DarkLordClass && (hero.characterClass & ClassUpperMask) == 0)
    {
        if (auto warned = Message(context.services, FenrirText, UI::ErrorMessage, "0x5bf279"); !warned.HasValue())
            return warned;
    }
    if (runtime.ui.field78abb39 != 0)
        return {};
    return SendTheCursorItem(context, static_cast<std::uint8_t>(runtime.itemMoveTarget & 0xff));
}

Core::Expected<bool, std::string> DropOnTheInventoryGrid(const InventoryMoveContext& context, const GridDrop& drop)
{
    WorldState& world = context.world;
    WorldRuntime& runtime = world.Runtime();
    const ItemState& cursor = runtime.cursorItem;
    const auto footprint = ItemFootprintOf(context.services.items, cursor.id, "0x5ca250 (0x5ca295)");
    if (!footprint.HasValue())
        return Core::Unexpected(footprint.Error());
    const GridCell cell = CellUnderTheMouse(drop.mouseX, drop.mouseY, drop.left, drop.top, footprint.Value());
    if (cell.column < 0 || cell.row < 0)
        return false;
    // 0x5ca339..0x5ca364: con 0x724580 en 3, 0x44acd0 deja pasar cualquier grilla que no sea la del entrenador.
    std::span<ItemState> cells(world.Inventory().cells);
    const bool fits = ItemFitsInTheGrid(cursor, cells, Columns, Rows, cell, footprint.Value());
    // 0x5ca385..0x5ca453: las marcas de la huella dentro de la grilla.
    const bool jewel = cursor.id == JewelOfBless || cursor.id == JewelOfSoul || cursor.id == JewelOfLife ||
                       cursor.id == JewelOfCreation;
    bool jewelMark = false;
    for (std::int32_t row = cell.row; row < cell.row + footprint.Value().height; ++row)
    {
        for (std::int32_t column = cell.column; column < cell.column + footprint.Value().width; ++column)
        {
            if (row >= Rows || column >= Columns)
                continue;
            ItemState& marked = cells[static_cast<std::size_t>(row * Columns + column)];
            if (fits)
                marked.warning = FitsMark;
            else if (jewel)
            {
                marked.warning = JewelMark;
                jewelMark = true;
            }
            else
                marked.warning = RefusedMark;
        }
    }
    // 0x5ca456..0x5ca46c: 0x7d368a1 MuRet no lo pone.
    if (!runtime.movement.pendingClick)
        return fits;
    if (!fits)
    {
        if (jewelMark && drop.inventoryOpen && cursor.id != JewelOfCreation)
            return Core::Unexpected(std::format("0x5d82a8: la joya {:#x} sobre otro ítem (0x5d7f50 y el C1 .. 26) es "
                                                "de la CL9.3",
                                                cursor.id));
        return false;
    }
    if (!drop.inventoryOpen)
        return true;
    const std::int32_t target = cell.row * Columns + cell.column + FirstGridSlot;
    runtime.itemMoveTarget = target;
    if (runtime.cursorGrid == CursorGrid::NpcGrid)
    {
        // 0x5cb386..0x5cb455: con la traba del almacén (0x6c932f) y sin 0x78abb1c, el movimiento espera la contraseña
        // (2, [0x78a6758], 0 y el destino), el teclado se baraja, 0x78abb20 en 1 y lo tecleado en 0.
        NpcWindowState& npc = runtime.npcWindows;
        if (npc.warehouseLocked != 0 && npc.field78abb1c == 0)
        {
            npc.pendingMove = PendingWarehouseMove{
                .slot = WarehouseMoveGrid, .second = runtime.slot78a6758, .third = InventoryMoveGrid, .fourth = target};
            ShuffleTheKeypad(context.random, npc.keypad);
            runtime.ui.field78abb20 = 1;
            npc.typedNumber = {};
            return true;
        }
        // 0x5cb45a..0x5cc2ca: el 24 del almacén al inventario.
        if (auto sent = SendTheCursorItemBetween(context, WarehouseMoveGrid,
                                                 static_cast<std::uint8_t>(runtime.slot78a6758 & 0xff),
                                                 InventoryMoveGrid, static_cast<std::uint8_t>(target & 0xff));
            !sent.HasValue())
            return Core::Unexpected(sent.Error());
        return true;
    }
    if (runtime.cursorGrid == CursorGrid::ChaosBox)
    {
        // 0x5cd684..0x5ce4f9: el 24 de la caja del caos al inventario.
        if (auto sent = SendTheCursorItemBetween(context, ChaosBoxMoveGrid,
                                                 static_cast<std::uint8_t>(runtime.slot78a6758 & 0xff),
                                                 InventoryMoveGrid, static_cast<std::uint8_t>(target & 0xff));
            !sent.HasValue())
            return Core::Unexpected(sent.Error());
        return true;
    }
    if (runtime.cursorGrid == CursorGrid::TrainerBox)
    {
        // 0x5ce4fe..0x5ce516: 0x449d50, el 24 de la caja del entrenador al inventario (0x449d80(5, lugar, 0, destino)).
        if (auto sent = SendTheCursorItemBetween(context, TrainerBoxMoveGrid,
                                                 static_cast<std::uint8_t>(runtime.slot78a6758 & 0xff),
                                                 InventoryMoveGrid, static_cast<std::uint8_t>(target & 0xff));
            !sent.HasValue())
            return Core::Unexpected(sent.Error());
        return true;
    }
    if (runtime.cursorGrid != CursorGrid::Inventory)
        return Core::Unexpected(std::string("0x5cc2cf: el ítem de la tienda personal o del comercio al inventario es "
                                            "de la CL9.4"));
    // 0x5ca4b3..0x5ca54a: a otro lugar, o al mismo con algo que se apila.
    bool moves = runtime.slot78a6758 != target;
    auto under = CellAt(cells, target - FirstGridSlot, "0x5ca4cf");
    if (!under.HasValue())
        return Core::Unexpected(under.Error());
    const ItemState& there = *under.Value();
    bool direct = false;
    if (there.id == cursor.id)
    {
        const std::int32_t type = Signed(cursor.id);
        if (InRange(type, FirstPotion, LastPotion))
            moves = true;
        if ((cursor.id == Bolts || cursor.id == Arrows) && there.scalar1Low == cursor.scalar1Low)
            moves = true;
        direct = cursor.id == StackedAlone;
    }
    if (!direct && !moves)
    {
        if (auto returned = ReturnTheCursorItem(context); !returned.HasValue())
            return Core::Unexpected(returned.Error());
        return true;
    }
    if (runtime.ui.field78abb39 != 0)
        return true;
    if (auto sent = SendTheCursorItem(context, static_cast<std::uint8_t>(target & 0xff)); !sent.HasValue())
        return Core::Unexpected(sent.Error());
    return true;
}

Core::Expected<bool, std::string> DropOnOtherGrid(const InventoryMoveContext& context, const OtherGridDrop& drop)
{
    WorldState& world = context.world;
    WorldRuntime& runtime = world.Runtime();
    NpcWindowState& npc = runtime.npcWindows;
    const bool chaosBox = drop.grid == CursorGrid::ChaosBox;
    const bool trainerBox = drop.grid == CursorGrid::TrainerBox;
    const std::span<ItemState> cells = OtherGridCells(npc, drop.grid);
    const std::int32_t rows =
        chaosBox || trainerBox ? ChaosBoxRows : static_cast<std::int32_t>(NpcGridCellCount) / NpcGridColumns;
    const std::uint8_t kind = chaosBox ? ChaosBoxMoveGrid : (trainerBox ? TrainerBoxMoveGrid : WarehouseMoveGrid);
    const ItemState& cursor = runtime.cursorItem;
    const auto footprint = ItemFootprintOf(context.services.items, cursor.id, "0x5ca250 (0x5ca295)");
    if (!footprint.HasValue())
        return Core::Unexpected(footprint.Error());
    const GridCell cell = CellUnderTheMouse(drop.mouseX, drop.mouseY, drop.left, drop.top, footprint.Value());
    if (cell.column < 0 || cell.row < 0)
        return false;
    // 0x5ca314..0x5ca364: con 0x724580 en 3, 0x44acd0 con otra grilla que la caja del caos; si no deja, sin 0x5c9b40.
    bool allowed = true;
    if (chaosBox)
    {
        if (npc.chaosMixState != 0 && runtime.panels.delegated.castleSiegeList == 0)
            return Core::Unexpected(std::string("0x5ca337: con la mezcla fuera de 0, 0x5ca250 no llama a 0x5c9b40 y "
                                                "lee [ebp − 0x2d] sin escribir (Q-C921)"));
    }
    else if (runtime.panels.delegated.trainer == TrainerWithBox && trainerBox)
        allowed = std::ranges::find(TrainerBoxItems, cursor.id) != TrainerBoxItems.end();
    const bool fits = allowed && ItemFitsInTheGrid(cursor, cells, NpcGridColumns, rows, cell, footprint.Value());
    // 0x5ca385..0x5ca453: las marcas de la huella dentro de la grilla.
    const bool jewel = cursor.id == JewelOfBless || cursor.id == JewelOfSoul || cursor.id == JewelOfLife ||
                       cursor.id == JewelOfCreation;
    for (std::int32_t row = cell.row; row < cell.row + footprint.Value().height; ++row)
    {
        for (std::int32_t column = cell.column; column < cell.column + footprint.Value().width; ++column)
        {
            if (row >= rows || column >= NpcGridColumns)
                continue;
            ItemState& marked = cells[static_cast<std::size_t>(row * NpcGridColumns + column)];
            marked.warning = fits ? FitsMark : (jewel ? JewelMark : RefusedMark);
        }
    }
    // 0x5ca456..0x5ca471: sin el clic, o sin entrar (la joya sobre un ítem es solo del tipo 0, 0x5d8266).
    if (!runtime.movement.pendingClick)
        return fits;
    if (!fits)
        return false;
    // 0x5cf3a5..0x5cf40b.
    const std::int32_t type = Signed(cursor.id);
    if (InRange(type, FirstUnmovedTicket, LastUnmovedTicket) || cursor.id == FirstUnmovedOrb ||
        cursor.id == SecondUnmovedOrb ||
        (cursor.id == UnmovedAtThirteen && (cursor.scalar1Low & LevelBits) == ThirteenBits))
        return true;
    // 0x5d1665..0x5d167c y 0x5d3342..0x5d3359: el destino sin el 12.
    const std::int32_t target = cell.row * NpcGridColumns + cell.column;
    runtime.itemMoveTarget = target;
    const std::uint8_t source = runtime.cursorGrid == CursorGrid::Inventory ? InventoryMoveGrid : kind;
    if (auto sent = SendTheCursorItemBetween(context, source, static_cast<std::uint8_t>(runtime.slot78a6758 & 0xff),
                                             kind, static_cast<std::uint8_t>(target & 0xff));
        !sent.HasValue())
        return Core::Unexpected(sent.Error());
    return true;
}

Core::Expected<void, std::string> DropOnTheGround(const InventoryMoveContext& context, const GroundDrop& drop)
{
    const WorldRuntime& runtime = context.world.Runtime();
    const ItemState& cursor = runtime.cursorItem;
    const std::int32_t type = Signed(cursor.id);
    const std::uint32_t level = LevelOf(cursor);
    // 0x5df449..0x5df544: las mascotas, las joyas, las alas, Dark Horse, Dark Raven, Cape of Lord, +0x1c % 4 en 1 o 2,
    // los ocho sueltos, un nivel de más de 4 debajo de las alas y +0x1b & 0x3f.
    const std::uint32_t kind = cursor.flags1c % 4u;
    const bool kept = InRange(type, GuardianAngel, HornOfDinorant) || cursor.id == JewelOfBless ||
                      cursor.id == JewelOfSoul || cursor.id == JewelOfLife || cursor.id == JewelOfCreation ||
                      InRange(type, FirstWing, LastWing) || cursor.id == DarkHorseHorn || cursor.id == DarkRaven ||
                      cursor.id == CapeOfLord || kind == 1 || kind == 2 ||
                      std::ranges::find(KeptOnTheGround, cursor.id) != KeptOnTheGround.end() ||
                      (level > MaxDroppedLevel && type < FirstWing) || (cursor.flags1b & DroppedFlagsMask) != 0;
    if (kept)
    {
        if (auto warned = Message(context.services, KeptText, UI::ErrorMessage, "0x5e0701 (0x5e06f5)");
            !warned.HasValue())
            return warned;
        return ReturnTheCursorItem(context);
    }
    if (InRange(type, FirstStackedBox, LastBox))
    {
        if (auto warned = Message(context.services, BoxText, UI::ErrorMessage, "0x5e0701 (0x5df556)");
            !warned.HasValue())
            return warned;
        return ReturnTheCursorItem(context);
    }
    // 0x5df567..0x5df585 → 0x5e06b6: los paquetes de joyas y el Jewel of Guardian. Con el inventario abierto y el
    // mouse a su derecha, solo 0x5be150; si no, el mismo aviso 269 antes.
    if (cursor.id == BlessBundle || cursor.id == SoulBundle || cursor.id == JewelOfGuardian)
    {
        if (!drop.inventoryOpen || drop.mouseX <= runtime.hud.inventoryX)
        {
            if (auto warned = Message(context.services, KeptText, UI::ErrorMessage, "0x5e0701 (0x5e06d9)");
                !warned.HasValue())
                return warned;
        }
        return ReturnTheCursorItem(context);
    }
    return Core::Unexpected(
        std::format("0x5df58b: tirar el {:#x} al piso (el resto de 0x5df430 y su envío) es de la CL9.3", cursor.id));
}

Core::Expected<void, std::string> ApplyItemMoveResult(const Protocol::Inventory::ItemMoveResult& result,
                                                      const WorldEntryResources& resources)
{
    WorldRuntime& runtime = resources.world.Runtime();
    runtime.ui.field78abb39 = 0;
    if (result.result == Protocol::Inventory::ItemMoveRejected)
    {
        const InventoryServices services = InventoryServicesOf(resources);
        return ReturnTheCursorItem(
            InventoryMoveContext{.world = resources.world, .random = resources.cloth.random, .services = services});
    }
    // 0x4d14c6..0x4d1520: las listas de la tienda personal (0x7245e4 y 0x7245e0) MuRet no las arma.
    runtime.cursorItemCount = 0;
    runtime.cursorItem.occupied = 0;
    switch (result.result)
    {
    case 0:
        if (auto placed = PlaceMovedItem(result.slot, result.item, resources); !placed.HasValue())
            return placed;
        break;
    case 1:
        return Core::Unexpected(std::string("0x4d1ce0: 0x5bc740 en la grilla del comercio (0x78a92a8) es de la CL9.4"));
    case 2:
        // 0x4d1cf6..0x4d1d0a: 0x5bc740(0x78a7070, 8, 15, lugar, datos, 0), la grilla del almacén.
        if (auto placed = PlaceMovedItemInOtherGrid(runtime.npcWindows.grid, NpcGridColumns, result.slot, result.item,
                                                    resources, "0x4d1d0a, 0x5bc740(0x78a7070)");
            !placed.HasValue())
            return placed;
        break;
    case 3:
        // 0x4d1d20..0x4d1d34: 0x5bc740(0x7d2d380, 8, 4, lugar, datos, 0), la caja del caos.
        if (auto placed = PlaceMovedItemInOtherGrid(runtime.npcWindows.chaosBox, NpcGridColumns, result.slot,
                                                    result.item, resources, "0x4d1d34, 0x5bc740(0x7d2d380)");
            !placed.HasValue())
            return placed;
        break;
    case 5:
        // 0x4d1d4a..0x4d1d55: 0x44ad70(lugar, datos, 0), 0x5bc740(0x723c80, 8, 4, lugar, datos, 0), la caja del
        // entrenador.
        if (auto placed = PlaceMovedItemInOtherGrid(runtime.npcWindows.trainerBox, NpcGridColumns, result.slot,
                                                    result.item, resources, "0x4d1d55, 0x5bc740(0x723c80)");
            !placed.HasValue())
            return placed;
        break;
    default:
        break;
    }
    runtime.lastSoundId = ItemMoveSound;
    return {};
}

Core::Expected<void, std::string> ApplyItemDelete(const Protocol::Inventory::ItemDelete& deleted,
                                                  const WorldEntryResources& resources)
{
    if (deleted.slot != Protocol::Inventory::NoItemToDelete)
    {
        if (deleted.slot >= FirstSecondGridSlot)
            return Core::Unexpected(std::format("0x4c5ece: borrar el lugar {} lee 0x6c931c y la grilla de la tienda "
                                                "personal, de la CL9.4",
                                                deleted.slot));
        const InventoryServices services = InventoryServicesOf(resources);
        if (auto removed = RemoveInventoryItem(
                InventoryMoveContext{.world = resources.world, .random = resources.cloth.random, .services = services},
                deleted.slot);
            !removed.HasValue())
            return removed;
    }
    if (deleted.endsTheWait != 0)
        resources.world.Runtime().ui.field5767954 = 0;
    return {};
}

Core::Expected<void, std::string> ApplyDurability(const Protocol::Inventory::Durability& durability,
                                                  const WorldEntryResources& resources)
{
    WorldState& world = resources.world;
    WorldRuntime& runtime = world.Runtime();
    if (durability.slot < FirstInventoryGridSlot)
    {
        const Render::Models::LogicalAccess own(runtime.ownRecordDepth, resources.cloth.random,
                                                Render::Models::OwnRecordCloseRandoms);
        world.OwnCharacter().equipment.at(durability.slot).durability = durability.durability;
    }
    else
    {
        // 0x4d3f4c..: la huella del tipo de la casilla, desde la casilla misma.
        const std::int32_t origin = durability.slot - FirstGridSlot;
        std::span<ItemState> cells(world.Inventory().cells);
        auto first = CellAt(cells, origin, "0x4d3f4c");
        if (!first.HasValue())
            return Core::Unexpected(first.Error());
        const auto footprint = ItemFootprintOf(resources.items, first.Value()->id, "0x4d3f4c");
        if (!footprint.HasValue())
            return Core::Unexpected(footprint.Error());
        for (std::int32_t row = 0; row < footprint.Value().height; ++row)
        {
            for (std::int32_t column = 0; column < footprint.Value().width; ++column)
            {
                auto cell = CellAt(cells, origin + row * Columns + column, "0x4d3f4c");
                if (!cell.HasValue())
                    return Core::Unexpected(cell.Error());
                cell.Value()->durability = durability.durability;
            }
        }
    }
    if (durability.endsTheWait != 0)
        runtime.ui.field5767954 = 0;
    return {};
}

Core::Expected<void, std::string> ApplyLifeRefill(const Protocol::Inventory::LifeRefill& refill, WorldState& world,
                                                  Render::Models::CrtRandom& random)
{
    WorldRuntime& runtime = world.Runtime();
    const Render::Models::LogicalAccess own(runtime.ownRecordDepth, random, Render::Models::OwnRecordCloseRandoms);
    switch (refill.place)
    {
    case Protocol::Inventory::EndTheWaitPlace:
        runtime.ui.field5767954 = 0;
        return {};
    case Protocol::Inventory::MaximumPlace:
        world.OwnCharacter().maxLife = refill.life;
        return {};
    case Protocol::Inventory::CurrentPlace:
        world.OwnCharacter().life = refill.life;
        return {};
    default:
        return SpendThePotion(world, refill.place, "0x4d35f6");
    }
}

Core::Expected<void, std::string> ApplyManaRefill(const Protocol::Inventory::ManaRefill& refill, WorldState& world,
                                                  Render::Models::CrtRandom& random)
{
    const Render::Models::LogicalAccess own(world.Runtime().ownRecordDepth, random,
                                            Render::Models::OwnRecordCloseRandoms);
    OwnCharacterRecord& record = world.OwnCharacter();
    switch (refill.place)
    {
    case Protocol::Inventory::MaximumPlace:
        record.maxMana = refill.mana;
        record.maxBp = refill.bp;
        return {};
    case Protocol::Inventory::CurrentPlace:
        record.mana = refill.mana;
        record.bp = refill.bp;
        return {};
    default:
        record.mana = refill.mana;
        return SpendThePotion(world, refill.place, "0x4d3917");
    }
}

Core::Expected<void, std::string> ApplyItemPut(const Protocol::Inventory::ItemPut& put,
                                               const WorldEntryResources& resources)
{
    WorldRuntime& runtime = resources.world.Runtime();
    runtime.cursorItemCount = 0;
    runtime.cursorItem.occupied = 0;
    if (auto placed = PlaceMovedItem(put.slot, put.item, resources); !placed.HasValue())
        return placed;
    runtime.lastSoundId = ItemPutSound;
    return {};
}

Core::Expected<void, std::string> ApplyEquipmentChange(const Protocol::Inventory::EquipmentChange& change,
                                                       const WorldEntryResources& resources)
{
    // 0x5139f0 con el índice de 0x5110d0 (el 400 sin ninguno, que en el arreglo de 401 existe) mira el tipo (+2) y,
    // si no es un jugador, vuelve sin tocar nada (0x513a12).
    const std::size_t index = ActorWithKey(resources.world, change.key);
    if (resources.world.Actors()[index].type != Data::Models::PlayerSkeletonModel)
        return {};
    return Core::Unexpected(
        std::format("0x513a1e: el equipo del jugador {} (0x5139f0, desde 0x4e316e) es de la CL9.2", index));
}
} // namespace Client::Scenes
