#include "Scenes/GridItemUse.h"

#include "Core/Expected.h"
#include "Data/Tables/ItemTable.h"
#include "Render/Models/LogicalAccess.h"
#include "Protocol/Events.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameDialogs.h"
#include "UI/GameText.h"
#include "UI/PanelRegistry.h"
#include "World/ItemState.h"
#include "World/Ftol.h"

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

constexpr auto Columns = static_cast<std::int32_t>(InventoryColumns);
constexpr std::int32_t FirstGridSlot = 12;
constexpr int LevelShift = 3;
constexpr std::uint32_t LevelMask = 0xf;

// 0x5c7bc6..0x5c7c0a: los libros (0x1e00..0x1fff) y las esferas, en comparaciones con signo de AX. 0x1810..0x1813,
// 0x1814 y 0x1815..0x1818 son tres pruebas seguidas: juntas, 0x1810..0x1818. El 0x180f no entra.
constexpr std::int32_t FirstBook = 0x1e00;
constexpr std::int32_t LastBook = 0x1fff;
constexpr std::int32_t FirstOrb = 0x1807;
constexpr std::int32_t LastFirstOrbs = 0x180e;
constexpr std::int32_t FirstLaterOrb = 0x1810;
constexpr std::int32_t LastOrb = 0x1818;
// 0x5c90ca: el nivel que pide Item.bmd, un WORD en +0x3a del registro.
constexpr std::size_t RequiredLevelOffset = 0x3a;
// 0x5c912b, 0x5c84bb: la dirección del aviso 474 de las consultas en línea.
constexpr std::string_view SkillItemRefusal = "0x5c912b";
constexpr std::string_view Item1a0bRefusal = "0x5c84bb";

// 0x5c7c10..0x5c8447: la fruta. 0x5c8061: desde el nivel 10; 0x5c83ad: la de nivel 4 (el dword +4 entero en 0x20) es
// de la clase 4; los avisos de 0x632e30 (la dirección − 0x76bd1dc, sobre 300: 0x76f3f98, 0x7748ef8 y 0x7748a48) y el
// diálogo de 0x633300(1, 0x178, 0x96).
constexpr std::uint16_t Fruit = 0x1a0f;
constexpr std::uint16_t FruitLevel = 10;
constexpr std::uint32_t CommandFruitBits = 0x20;
constexpr std::uint8_t CommandFruitClass = 4;
constexpr std::size_t FruitLevelText = 749;
constexpr std::size_t FruitEquipmentText = 1909;
constexpr std::size_t FruitClassText = 1905;
constexpr std::int32_t FruitDialogLines = 1;
constexpr std::int32_t FruitDialogText = 0x178;

// 0x5c8448..0x5c8473: el 0x1a0b de nivel 0 o 1; el de nivel 1, sin el héroe maestro del gremio (+0x368 en 0x80).
constexpr std::uint16_t Item1a0b = 0x1a0b;
constexpr std::uint8_t GuildMaster = 0x80;

// 0x5c67e9, 0x5c6c36 y 0x5c7010: los tres que mandan el C1 .. 91.
constexpr std::int32_t EventTicket1a1d = 0x1a1d;
constexpr std::int32_t EventTicket1c13 = 0x1c13;
constexpr std::int32_t EventTicket1a12 = 0x1a12;

bool InRange(std::int32_t value, std::int32_t first, std::int32_t last)
{
    return value >= first && value <= last;
}

bool IsSkillItem(std::int32_t type)
{
    return InRange(type, FirstBook, LastBook) || InRange(type, FirstOrb, LastFirstOrbs) ||
           InRange(type, FirstLaterOrb, LastOrb);
}

// 0x632e30 con el texto `index` de Text.bmd, sin sprintf.
Core::Expected<void, std::string> Notice(const InventoryMoveContext& context, std::size_t index, std::string_view where)
{
    const std::optional<std::string_view> text = UI::TextAt(context.services.texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: falta el texto {} de Text.bmd", where, index));
    WorldRuntime& runtime = context.world.Runtime();
    UI::OpenMessageBox(runtime.dialogBox, runtime.dialogs, *text);
    return {};
}

// 0x5c8fb9..0x5c991b. El CMP AX, 0x1e12 de 0x5c8fb9 no lo lee nadie: salta a 0x5c8fd4, que pisa las banderas antes
// de mirarlas. 0x5c8fd4..0x5c909c abre el registro propio y 0x5c991b lo cierra, también desde los avisos y las
// salidas: con la cuenta en 0, los 3.848 rand() van después de los del envío.
Core::Expected<void, std::string> UseTheSkillItem(const InventoryMoveContext& context, UI::PanelRegistry& panels,
                                                  const ItemState& hovered, std::int32_t cell)
{
    WorldState& world = context.world;
    const Render::Models::LogicalAccess own(world.Runtime().ownRecordDepth, context.random,
                                            Render::Models::OwnRecordCloseRandoms);
    if (context.services.items == nullptr)
        return Core::Unexpected(std::string("0x5c90c1: lee *[0x7758550] (Item.bmd) y no está cargado"));
    const auto record = context.services.items->Record(hovered.id);
    if (!record)
        return Core::Unexpected(std::format("0x5c90ca: el tipo {:#x} lee fuera de Item.bmd", hovered.id));
    const auto required =
        static_cast<std::uint16_t>(std::to_integer<std::uint16_t>((*record)[RequiredLevelOffset]) |
                                   (std::to_integer<std::uint16_t>((*record)[RequiredLevelOffset + 1]) << 8));
    // 0x5c90ab..0x5c90e3: tres JC sobre WORD, sin signo; la energía y la fuerza con lo de +0x30 y +0x2a sumados en 16
    // bits.
    const OwnCharacterRecord& stats = world.OwnCharacter();
    if (stats.level < required)
        return {};
    if (static_cast<std::uint16_t>(stats.word30 + stats.energy) < hovered.word22)
        return {};
    if (static_cast<std::uint16_t>(stats.word2a + stats.strength) < hovered.word1e)
        return {};
    return UseInventoryItemOn(context, panels, InventoryUse{.slot = cell + FirstGridSlot, .where = SkillItemRefusal});
}

// 0x5c7c10..0x5c8447. Los dos accesos al registro propio son en línea: 0x5c7c35..0x5c7e59 lo abre y 0x5c7e59..0x5c8061
// lo cierra, con su rand() por byte en 0x5c8026; 0x5c806e..0x5c8219 lo vuelve a abrir y 0x5c8250..0x5c83a7 lo cierra
// con 0x40b4d0.
Core::Expected<void, std::string> UseTheFruit(const InventoryMoveContext& context, const ItemState& hovered,
                                              std::int32_t cell)
{
    WorldState& world = context.world;
    WorldRuntime& runtime = world.Runtime();
    std::uint16_t level = 0;
    {
        const Render::Models::LogicalAccess own(runtime.ownRecordDepth, context.random,
                                                Render::Models::OwnRecordCloseRandoms);
        level = world.OwnCharacter().level;
    }
    if (level < FruitLevel)
        return Notice(context, FruitLevelText, "0x5c842f");
    // 0x5c806b..0x5c8099: la fruta de la casilla de arriba a la izquierda en 0x78a59c8, con la casilla en +9.
    const ItemState& fruit = world.Inventory().cells[static_cast<std::size_t>(cell)];
    runtime.dialogItem = fruit;
    runtime.dialogItemCell = static_cast<std::uint8_t>(cell & 0xff);
    bool unequipped = true;
    std::uint8_t heroClass = 0;
    {
        const Render::Models::LogicalAccess own(runtime.ownRecordDepth, context.random,
                                                Render::Models::OwnRecordCloseRandoms);
        // 0x5c8219..0x5c8237: los doce lugares del equipo, de a 0x48 desde +0xb6c; 0x5c8245: la clase (+0xb), entera.
        for (const ItemState& worn : world.OwnCharacter().equipment)
        {
            if (worn.id != EmptyOwnItemId)
                unequipped = false;
        }
        heroClass = world.OwnCharacter().characterClass;
    }
    if (!unequipped)
        return Notice(context, FruitEquipmentText, "0x5c8411");
    // 0x5c83ad: el nivel lo mira en EBX, la casilla bajo el mouse (0x5c589e), no en la copia de 0x78a59c8.
    if (hovered.scalar1Low == CommandFruitBits && heroClass != CommandFruitClass)
        return Notice(context, FruitClassText, "0x5c83be");
    // 0x5c83e7..0x5c83f3: el diálogo 0x96 nombra el ítem de 0x78a59c8: corta con la dirección de su texto.
    return UI::OpenTextDialog(runtime.dialogBox, runtime.dialogs, context.services.texts, FruitDialogLines,
                              FruitDialogText, UI::ItemNameDialog);
}

// 0x5c9e10: conserva el destino del mouse temporal hasta que 0x5ca250 lo consume.
Core::Expected<void, std::string> AimTheAutomaticMove(const InventoryMoveContext& context, bool inventory)
{
    WorldRuntime& runtime = context.world.Runtime();
    const auto footprint = ItemFootprintOf(context.services.items, runtime.cursorItem.id, "0x5c9e10");
    if (!footprint.HasValue())
        return Core::Unexpected(footprint.Error());
    constexpr std::int32_t WarehouseRows = 15;
    constexpr std::int32_t InventoryRows = 8;
    constexpr std::int32_t CellSize = 20;
    constexpr std::int32_t GridLeft = 15;
    constexpr std::int32_t WarehouseTop = 50;
    constexpr std::int32_t InventoryTop = 200;
    const std::span<const ItemState> destination = inventory
                                                       ? std::span<const ItemState>(runtime.npcWindows.grid)
                                                       : std::span<const ItemState>(context.world.Inventory().cells);
    const auto free =
        FindFreeGridCell(destination, Columns, inventory ? WarehouseRows : InventoryRows, footprint.Value());
    runtime.ui.automaticMove = free.has_value();
    if (free)
    {
        const std::int32_t left = (inventory ? runtime.ui.npcWindowX : runtime.hud.inventoryX) + GridLeft;
        const std::int32_t top =
            inventory ? runtime.ui.npcWindowY + WarehouseTop : runtime.hud.inventoryY + InventoryTop;
        runtime.ui.automaticMouseX =
            World::FtolLow(left + CellSize * (free->column + (footprint.Value().width - 1) * 0.5));
        runtime.ui.automaticMouseY =
            World::FtolLow(top + CellSize * (free->row + (footprint.Value().height - 1) * 0.5));
    }
    return {};
}

// 0x5c8e37..0x5c8fb8: la copia de la casilla `cell` de la grilla al cursor y la grilla en 0x78ab9b8. Desde el
// inventario (0x5c8e58), 0x78abb28 en 1 y, con el almacén abierto, 0x5c9e10 en su grilla (0x5c8e8f); desde otra
// (0x5c8ef8, la propia del comercio), sin 0x78abb28 y, con el almacén abierto, 0x5c9e10 en el inventario (0x5c8f09).
Core::Expected<void, std::string> CopyIntoTheCursor(const InventoryMoveContext& context, UI::PanelRegistry& panels,
                                                    UsedGrid grid, std::int32_t cell)
{
    WorldRuntime& runtime = context.world.Runtime();
    runtime.cursorItem = grid.cells[static_cast<std::size_t>(cell)];
    runtime.cursorGrid = grid.grid;
    const bool inventory = grid.grid == CursorGrid::Inventory;
    if (inventory)
        runtime.ui.field78abb28 = 1;
    if (panels.IsOpen(UI::WarehousePanel))
    {
        if (auto aimed = AimTheAutomaticMove(context, inventory); !aimed.HasValue())
            return aimed;
    }
    if (!runtime.ui.automaticMove)
    {
        runtime.cursorItem = ItemState{};
        runtime.cursorItemCount = 0;
        return {};
    }
    const bool pendingClick = runtime.movement.pendingClick;
    const auto picked = inventory ? PickUpFromTheGrid(context, static_cast<std::size_t>(cell))
                        : grid.grid == CursorGrid::OwnTrade
                            ? PickUpFromTheTradeGrid(context, static_cast<std::size_t>(cell))
                            : PickUpFromOtherGrid(context, grid.grid, static_cast<std::size_t>(cell));
    runtime.movement.pendingClick = pendingClick;
    if (inventory)
        runtime.ui.field78abb28 = 1;
    if (!picked.HasValue())
        return picked;
    constexpr std::int32_t ItemMoveSound = 0x1d; // 0x5c8f7a.
    runtime.lastSoundId = ItemMoveSound;
    return {};
}
} // namespace

bool IsAnEventTicket(std::int32_t type)
{
    return type == EventTicket1a1d || type == EventTicket1c13 || type == EventTicket1a12;
}

Core::Expected<void, std::string> UseTheEventTicket(const InventoryMoveContext& context, UI::PanelRegistry& panels,
                                                    UsedGrid grid, std::size_t index)
{
    if (index >= grid.cells.size())
        return Core::Unexpected(std::string("0x5c58aa: entrada de evento fuera de la grilla"));
    const ItemState& item = grid.cells[index];
    const auto type = std::bit_cast<std::int16_t>(item.id);
    if (type == EventTicket1a1d)
    {
        // 0x5c67f3..0x5c685f: PUSH 0x10000, 0x40, 0x20, 0x1000 y 0x4000, cada uno con su JNZ a 0x5c9b0b.
        for (const UI::PanelId panel :
             {UI::NpcShopPanel, UI::WarehousePanel, UI::TradePanel, UI::EventNpcPanel, UI::SecondEventNpcPanel})
        {
            if (panels.IsOpen(panel))
                return {};
        }
    }
    const std::uint8_t kind = type == EventTicket1a1d ? 4 : type == EventTicket1c13 ? 1 : 2;
    if (!context.services.sendGamePacket)
        return Core::Unexpected(std::string("0x5c6bfa: la entrada de evento sin el envío del juego"));
    context.services.sendGamePacket(Protocol::Events::WriteEventTimeRequest(kind));
    if (type == EventTicket1a1d)
    {
        const auto cell = static_cast<std::size_t>(item.gridRow) * InventoryColumns + item.gridColumn;
        if (cell >= grid.cells.size())
            return Core::Unexpected(std::string("0x5c6c20: el origen de la entrada está fuera de la grilla"));
        WorldRuntime& runtime = context.world.Runtime();
        runtime.slot78a6758 = static_cast<std::int32_t>(cell) + FirstGridSlot;
        runtime.dialogItem = grid.cells[cell];
    }
    return {};
}

Core::Expected<void, std::string> UseTheLaterGridItem(const InventoryMoveContext& context, UI::PanelRegistry& panels,
                                                      UsedGrid grid, std::size_t index)
{
    WorldState& world = context.world;
    const std::span<ItemState> cells = grid.cells;
    if (index >= cells.size())
        return Core::Unexpected(std::format("0x5c7bc6: la casilla {} está fuera de la grilla", index));
    const ItemState& hovered = cells[index];
    // 0x5c589e..0x5c58b4: la casilla de arriba a la izquierda con el ancho de la grilla ([EBP + 0x14]), en ESI y en
    // [EBP + 0x1c].
    const std::int32_t cell = hovered.gridRow * Columns + hovered.gridColumn;
    if (cell >= static_cast<std::int32_t>(cells.size()))
        return Core::Unexpected(std::format("0x5c58aa: el origen {} está fuera de la grilla (Q-C200)", cell));
    const std::int32_t type = std::bit_cast<std::int16_t>(hovered.id);
    if (IsSkillItem(type))
        return UseTheSkillItem(context, panels, hovered, cell);
    // 0x5c7c1a..0x5c7c2f: también el ítem de esa casilla en la grilla del inventario (0x78a9bb0), sea cual sea la
    // grilla (07 Q-C964); desde el inventario, es el mismo.
    if (hovered.id == Fruit && world.Inventory().cells[static_cast<std::size_t>(cell)].id == Fruit)
        return UseTheFruit(context, hovered, cell);
    if (hovered.id == Item1a0b)
    {
        const std::uint32_t level = (hovered.scalar1Low >> LevelShift) & LevelMask;
        if (level > 1)
            return {};
        if (level == 1 && world.Actors()[world.Runtime().hero].guildStatus == GuildMaster)
            return {};
        return UseInventoryItemOn(context, panels,
                                  InventoryUse{.slot = cell + FirstGridSlot, .where = Item1a0bRefusal});
    }
    return CopyIntoTheCursor(context, panels, grid, cell);
}
} // namespace Client::Scenes
