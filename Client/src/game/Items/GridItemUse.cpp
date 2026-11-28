#include "Items/GridItemUse.h"

#include "Core/Expected.h"
#include "Kernel/CharacterClass.h"
#include "Tables/ItemTable.h"
#include "Models/LogicalAccess.h"
#include "Protocol/Events/Events.h"
#include "Items/InventoryMoves.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/OwnInventory.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameDialogs.h"
#include "Ui/GameText.h"
#include "Ui/PanelRegistry.h"
#include "Kernel/ItemState.h"
#include "Math/Ftol.h"
#include "Sound/SoundIds.h"
#include "Audio/SoundId.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Client::Items
{
namespace
{
using Kernel::ItemState;

constexpr auto Columns = static_cast<std::int32_t>(Kernel::InventoryColumns);
constexpr std::int32_t FirstGridSlot = 12;
constexpr int LevelShift = 3;
constexpr std::uint32_t LevelMask = 0xf;

// 0x5c7bc6..0x5c7c0a: the books and the orbs, in signed comparisons of AX. 0x1810..0x1813, 0x1814 and 0x1815..0x1818
// are three tests in a row: together, 0x1810..0x1818. The 0x180f is not included.
constexpr std::int32_t FirstBook = 0x1e00;
constexpr std::int32_t LastBook = 0x1fff;
constexpr std::int32_t FirstOrb = 0x1807;
constexpr std::int32_t LastFirstOrbs = 0x180e;
constexpr std::int32_t FirstLaterOrb = 0x1810;
constexpr std::int32_t LastOrb = 0x1818;
// 0x5c90ca: a WORD in the Item.bmd record.
constexpr std::size_t RequiredLevelOffset = 0x3a;
// 0x5c912b, 0x5c84bb: the address of notice 474 of the inlined queries.
constexpr std::string_view SkillItemRefusal = "0x5c912b";
constexpr std::string_view Item1a0bRefusal = "0x5c84bb";

// 0x5c7c10..0x5c8447: the fruit. 0x5c8061: from level 10; 0x5c83ad: the level 4 one (the dword +4 as an integer at
// 0x20) is class 4's; the notices of 0x632e30 (the address − 0x76bd1dc, over 300: 0x76f3f98, 0x7748ef8 and 0x7748a48)
// and the dialog of 0x633300(1, 0x178, 0x96).
constexpr std::uint16_t Fruit = 0x1a0f;
constexpr std::uint16_t FruitLevel = 10;
constexpr std::uint32_t CommandFruitBits = 0x20;
constexpr std::uint8_t CommandFruitClass = Kernel::ClassNumberOf(Kernel::CharacterClass::DarkLord);
constexpr std::size_t FruitLevelText = 749;
constexpr std::size_t FruitEquipmentText = 1909;
constexpr std::size_t FruitClassText = 1905;
constexpr std::int32_t FruitDialogLines = 1;
constexpr std::int32_t FruitDialogText = 0x178;

// 0x5c8448..0x5c8473: the 0x1a0b of level 0 or 1; the level 1 one, without the hero as guild master (+0x368 at 0x80).
constexpr std::uint16_t Item1a0b = 0x1a0b;
constexpr std::uint8_t GuildMaster = 0x80;

// 0x5c67e9, 0x5c6c36 and 0x5c7010: the three that send the C1 .. 91.
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

// 0x632e30 with no sprintf.
Core::Expected<void, std::string> Notice(const InventoryMoveContext& context, std::size_t index, std::string_view where)
{
    const std::optional<std::string_view> text = Ui::TextAt(context.services.texts, index);
    if (!text)
        return Core::Unexpected(std::format("{}: falta el texto {} de Text.bmd", where, index));
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Ui::OpenMessageBox(runtime.dialogBox, runtime.dialogs, *text);
    return {};
}

// 0x5c8fb9..0x5c991b. Nobody reads 0x5c8fb9's CMP AX, 0x1e12: it jumps to 0x5c8fd4, which overwrites the flags before
// looking at them. 0x5c8fd4..0x5c909c opens the own record and 0x5c991b closes it, also from the notices and the
// exits: with the count at 0, the 3848 rand() come after the send's.
Core::Expected<void, std::string> UseTheSkillItem(const InventoryMoveContext& context, Ui::PanelRegistry& panels,
                                                  const ItemState& hovered, std::int32_t cell)
{
    Kernel::WorldState& world = context.world;
    const Models::LogicalAccess own(world.Runtime().ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
    if (context.services.items == nullptr)
        return Core::Unexpected(std::string("0x5c90c1: lee *[0x7758550] (Item.bmd) y no está cargado"));
    const auto record = context.services.items->Record(hovered.id);
    if (!record)
        return Core::Unexpected(std::format("0x5c90ca: el tipo {:#x} lee fuera de Item.bmd", hovered.id));
    const auto required =
        static_cast<std::uint16_t>(std::to_integer<std::uint16_t>((*record)[RequiredLevelOffset]) |
                                   (std::to_integer<std::uint16_t>((*record)[RequiredLevelOffset + 1]) << 8));
    // 0x5c90ab..0x5c90e3: three JC on WORD, unsigned; the energy and the strength with what +0x30 and +0x2a add, in 16
    // bits.
    const Kernel::OwnCharacterRecord& stats = world.OwnCharacter();
    if (stats.level < required)
        return {};
    if (static_cast<std::uint16_t>(stats.word30 + stats.energy) < hovered.word22)
        return {};
    if (static_cast<std::uint16_t>(stats.word2a + stats.strength) < hovered.word1e)
        return {};
    return UseInventoryItemOn(context, panels, InventoryUse{.slot = cell + FirstGridSlot, .where = SkillItemRefusal});
}

// 0x5c7c10..0x5c8447. Both accesses to the own record are inlined: 0x5c7c35..0x5c7e59 opens it and
// 0x5c7e59..0x5c8061 closes it, with its rand() per byte at 0x5c8026; 0x5c806e..0x5c8219 opens it again and
// 0x5c8250..0x5c83a7 closes it with 0x40b4d0.
Core::Expected<void, std::string> UseTheFruit(const InventoryMoveContext& context, const ItemState& hovered,
                                              std::int32_t cell)
{
    Kernel::WorldState& world = context.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    std::uint16_t level = 0;
    {
        const Models::LogicalAccess own(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
        level = world.OwnCharacter().level;
    }
    if (level < FruitLevel)
        return Notice(context, FruitLevelText, "0x5c842f");
    // 0x5c806b..0x5c8099: the fruit of the top left cell in 0x78a59c8, with the cell at +9.
    const ItemState& fruit = world.Inventory().cells[static_cast<std::size_t>(cell)];
    runtime.dialogItem = fruit;
    runtime.dialogItemCell = static_cast<std::uint8_t>(cell & 0xff);
    bool unequipped = true;
    std::uint8_t heroClass = 0;
    {
        const Models::LogicalAccess own(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
        // 0x5c8219..0x5c8237: the twelve equipment slots, 0x48 apart from +0xb6c; 0x5c8245: the class (+0xb), whole.
        for (const ItemState& worn : world.OwnCharacter().equipment)
        {
            if (worn.id != Kernel::EmptyOwnItemId)
                unequipped = false;
        }
        heroClass = world.OwnCharacter().characterClass;
    }
    if (!unequipped)
        return Notice(context, FruitEquipmentText, "0x5c8411");
    // 0x5c83ad: it checks the level in EBX, the cell under the mouse (0x5c589e), not in the copy at 0x78a59c8.
    if (hovered.scalar1Low == CommandFruitBits && heroClass != CommandFruitClass)
        return Notice(context, FruitClassText, "0x5c83be");
    // 0x5c83e7..0x5c83f3: dialog 0x96 names the item copied from 0x78a59c8.
    return Ui::OpenTextDialog(runtime.dialogBox, runtime.dialogs, context.services.texts, FruitDialogLines,
                              FruitDialogText, Ui::ItemNameDialog, &runtime.dialogItem);
}

// 0x5c9e10: keeps the temporary mouse target until 0x5ca250 consumes it.
Core::Expected<void, std::string> AimTheAutomaticMove(const InventoryMoveContext& context, bool inventory)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
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
            Math::FtolLow(left + CellSize * (free->column + (footprint.Value().width - 1) * 0.5));
        runtime.ui.automaticMouseY = Math::FtolLow(top + CellSize * (free->row + (footprint.Value().height - 1) * 0.5));
    }
    return {};
}

// 0x5c8e37..0x5c8fb8: the copy of the grid's cell `cell` into the cursor and the grid in 0x78ab9b8. From the
// inventory (0x5c8e58), 0x78abb28 at 1 and, with the warehouse open, 0x5c9e10 in its grid (0x5c8e8f); from another
// one (0x5c8ef8, the own trade grid), without 0x78abb28 and, with the warehouse open, 0x5c9e10 in the inventory
// (0x5c8f09).
Core::Expected<void, std::string> CopyIntoTheCursor(const InventoryMoveContext& context, Ui::PanelRegistry& panels,
                                                    UsedGrid grid, std::int32_t cell)
{
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    runtime.cursorItem = grid.cells[static_cast<std::size_t>(cell)];
    runtime.cursorGrid = grid.grid;
    const bool inventory = grid.grid == Kernel::CursorGrid::Inventory;
    if (inventory)
        runtime.ui.field78abb28 = 1;
    if (panels.IsOpen(Ui::WarehousePanel))
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
    const bool pendingClick = runtime.mouse.pendingClick;
    const auto picked = inventory ? PickUpFromTheGrid(context, static_cast<std::size_t>(cell))
                        : grid.grid == Kernel::CursorGrid::OwnTrade
                            ? PickUpFromTheTradeGrid(context, static_cast<std::size_t>(cell))
                            : PickUpFromOtherGrid(context, grid.grid, static_cast<std::size_t>(cell));
    runtime.mouse.pendingClick = pendingClick;
    if (inventory)
        runtime.ui.field78abb28 = 1;
    if (!picked.HasValue())
        return picked;
    constexpr Audio::SoundId ItemMoveSound = Sound::GetItemSound; // 0x5c8f7a.
    runtime.RequestSound(ItemMoveSound);
    return {};
}
} // namespace

bool IsAnEventTicket(std::int32_t type)
{
    return type == EventTicket1a1d || type == EventTicket1c13 || type == EventTicket1a12;
}

Core::Expected<void, std::string> UseTheEventTicket(const InventoryMoveContext& context, Ui::PanelRegistry& panels,
                                                    UsedGrid grid, std::size_t index)
{
    if (index >= grid.cells.size())
        return Core::Unexpected(std::string("0x5c58aa: entrada de evento fuera de la grilla"));
    const ItemState& item = grid.cells[index];
    const auto type = std::bit_cast<std::int16_t>(item.id);
    if (type == EventTicket1a1d)
    {
        // 0x5c67f3..0x5c685f: PUSH 0x10000, 0x40, 0x20, 0x1000 and 0x4000, each with its JNZ to 0x5c9b0b.
        for (const Ui::PanelId panel :
             {Ui::NpcShopPanel, Ui::WarehousePanel, Ui::TradePanel, Ui::EventNpcPanel, Ui::SecondEventNpcPanel})
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
        const auto cell = static_cast<std::size_t>(item.gridRow) * Kernel::InventoryColumns + item.gridColumn;
        if (cell >= grid.cells.size())
            return Core::Unexpected(std::string("0x5c6c20: el origen de la entrada está fuera de la grilla"));
        Kernel::WorldRuntime& runtime = context.world.Runtime();
        runtime.slot78a6758 = static_cast<std::int32_t>(cell) + FirstGridSlot;
        runtime.dialogItem = grid.cells[cell];
    }
    return {};
}

Core::Expected<void, std::string> UseTheLaterGridItem(const InventoryMoveContext& context, Ui::PanelRegistry& panels,
                                                      UsedGrid grid, std::size_t index)
{
    Kernel::WorldState& world = context.world;
    const std::span<ItemState> cells = grid.cells;
    if (index >= cells.size())
        return Core::Unexpected(std::format("0x5c7bc6: la casilla {} está fuera de la grilla", index));
    const ItemState& hovered = cells[index];
    // 0x5c589e..0x5c58b4: the top left cell with the grid's width ([EBP + 0x14]), in ESI and in [EBP + 0x1c].
    const std::int32_t cell = hovered.gridRow * Columns + hovered.gridColumn;
    if (cell >= static_cast<std::int32_t>(cells.size()))
        return Core::Unexpected(std::format("0x5c58aa: el origen {} está fuera de la grilla (Q-C200)", cell));
    const std::int32_t type = std::bit_cast<std::int16_t>(hovered.id);
    if (IsSkillItem(type))
        return UseTheSkillItem(context, panels, hovered, cell);
    // 0x5c7c1a..0x5c7c2f: also the item of that cell in the inventory grid (0x78a9bb0), whatever the grid (07 Q-C964);
    // from the inventory, it is the same one.
    if (hovered.id == Fruit && world.Inventory().cells[static_cast<std::size_t>(cell)].id == Fruit)
        return UseTheFruit(context, hovered, cell);
    if (hovered.id == Item1a0b)
    {
        const std::uint32_t level = (hovered.scalar1Low >> LevelShift) & LevelMask;
        if (level > 1)
            return {};
        if (level == 1 && world.Hero().guildStatus == GuildMaster)
            return {};
        return UseInventoryItemOn(context, panels,
                                  InventoryUse{.slot = cell + FirstGridSlot, .where = Item1a0bRefusal});
    }
    return CopyIntoTheCursor(context, panels, grid, cell);
}
} // namespace Client::Items
