#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/ItemTable.h"
#include "Pathfinding/PathCell.h"
#include "Network/Xor32Packet.h"
#include "Protocol/Items/FloorItems.h"
#include "Protocol/Items/Inventory.h"
#include "Models/LogicalAccess.h"
#include "Hud/GamePanels.h"
#include "Items/InventoryMoves.h"
#include "Npc/NpcWindowState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/OwnInventory.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameUiState.h"
#include "Kernel/ItemState.h"
#include "Audio/SoundId.h"

#include "support/Bytes.h"
#include "support/ItemFiles.h"
#include "support/SameBytes.h"
#include "support/SceneDataFiles.h"
#include "Social/SocialState.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Picking up, dropping and equipping the cursor item (CL8 milestone 4; 06 § El inventario, "El ítem del cursor"): the
// 8 × 8 grid with items of several sizes, the equipment, the floor and MoveHero, on a synthetic Item.bmd. The items,
// their bytes and their slots are invented.
namespace
{
using Client::Core::Expected;
using Client::Crt::CrtRandom;
using Client::Items::InventoryMoveContext;
using Client::Items::InventoryServices;
using Client::Kernel::EmptyOwnItemId;
using Client::Kernel::ItemState;
using Client::Kernel::WorldState;
using Client::Network::Xor32Decode;
using Client::Pathfinding::PathCell;
using Client::Protocol::Items::Drop;
using Client::Protocol::Items::ItemMove;
using Client::Tests::Bytes;
using Client::Tests::ItemTableOf;
using Client::Tests::SameBytes;
namespace Scenes = Client::Scenes;

constexpr int Own = Client::Models::OwnRecordCloseRandoms;
// The C3 send's two rand(): the padding and 0x40b0a0's serial.
constexpr int SendRandoms = 2;
constexpr std::int32_t Left = 0x1c2 + 15;
constexpr std::int32_t Top = 200;

// The synthetic Item.bmd's types, with their width, height and equipment slot (+0x22).
constexpr std::uint16_t Sword = 0x0001;    // 1 × 3, the right hand
constexpr std::uint16_t Shield = 0x0c01;   // 2 × 2, the left
constexpr std::uint16_t Helm = 0x0e01;     // 2 × 2, the head
constexpr std::uint16_t Potion = 0x1c00;   // 1 × 1, stacks
constexpr std::uint16_t Jewel = 0x1c0d;    // 1 × 1, Jewel of Bless: not dropped
constexpr std::uint16_t Box = 0x1a20;      // 1 × 1, warns with 1915
constexpr std::uint16_t Bundle = 0x181e;   // 1 × 1, the Bless bundle (0x402b30 gives 2)
constexpr std::uint16_t Wings = 0x1800;    // 3 × 2, the wings
constexpr std::uint16_t Bow = 0x0801;      // 2 × 4, the left: shoots arrows
constexpr std::uint16_t Crossbow = 0x0808; // 2 × 4, the right: shoots bolts
constexpr std::uint16_t Bolts = 0x0807;    // 1 × 1
constexpr std::uint16_t Arrows = 0x080f;   // 1 × 1
// 1 × 1, the right: the Dark Raven with 32 types per group, which 0x5bf03f compares (empty in the real Item.bmd).
constexpr std::uint16_t OldDarkRaven = 0x01a5;
constexpr std::uint16_t EntryTicket = 0x1c1c; // 1 × 1, the map 0x4379f0 checks.
constexpr std::uint16_t WizardsRing = 0x1a14; // 1 × 1, the first ring
constexpr std::uint8_t HeadSlot = 2;
constexpr std::uint8_t WingSlot = 7;
constexpr std::uint8_t NotWorn = 0xff;
constexpr std::uint8_t DarkKnight = 1;
constexpr std::uint8_t Elf = 2;
// 0x561840's texts: the arrows that get equipped (with type 1) and those that ran out (with 2).
constexpr std::size_t EquippedArrowsText = 250;
constexpr std::size_t NoArrowsText = 251;

// What the steps ask for outside: how many times the hero was dressed, the messages and the 24s that went out.
struct Outside
{
    int dressed = 0;
    int ravens = 0;
    std::vector<std::size_t> messages;
    std::vector<std::int32_t> messageTypes;
    std::vector<ItemMove> sent;
};

class CursorItemTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        world.OwnCharacter().ClearEquipmentForEntry();
        world.Inventory().ClearForInventoryReply();
        world.Runtime().hud.inventoryX = Left - 15;
        world.Runtime().hud.inventoryY = Top - 200;
        services = InventoryServices{.items = &items,
                                     .dressTheHero =
                                         [this]
                                     {
                                         ++outside.dressed;
                                         return Expected<void, std::string>{};
                                     },
                                     .replaceTheRaven =
                                         [this]
                                     {
                                         ++outside.ravens;
                                         return Expected<void, std::string>{};
                                     },
                                     .systemMessage =
                                         [this](std::size_t index, std::int32_t type)
                                     {
                                         outside.messages.push_back(index);
                                         outside.messageTypes.push_back(type);
                                         return Expected<void, std::string>{};
                                     },
                                     .sendItemMove =
                                         [this](const ItemMove& move)
                                     {
                                         outside.sent.push_back(move);
                                         return Expected<void, std::string>{};
                                     }};
    }

    [[nodiscard]] InventoryMoveContext Moves()
    {
        return InventoryMoveContext{.world = world, .random = random, .services = services};
    }

    // The item `type` with its footprint from cell (column, row), as 0x5bc740 leaves it: the copy in every cell, with
    // the origin's column and row, and +0x3c at 1 only at the origin.
    void Put(std::uint16_t type, std::uint8_t column, std::uint8_t row)
    {
        const auto footprint = Client::Items::ItemFootprintOf(&items, type, "Put");
        ASSERT_TRUE(footprint.HasValue()) << footprint.Error();
        for (std::int32_t y = 0; y < footprint.Value().height; ++y)
        {
            for (std::int32_t x = 0; x < footprint.Value().width; ++x)
            {
                ItemState& cell = Cell(column + x, row + y);
                cell = Invented(type);
                cell.gridColumn = column;
                cell.gridRow = row;
                cell.occupied = x == 0 && y == 0 ? 1u : 0u;
            }
        }
    }

    [[nodiscard]] static ItemState Invented(std::uint16_t type)
    {
        ItemState item;
        item.id = type;
        item.scalar1Low = 0x0b;
        item.durability = 0x2c;
        item.flags1b = 0x00;
        item.flags1c = 0x00;
        return item;
    }

    ItemState& Cell(std::int32_t column, std::int32_t row)
    {
        return world.Inventory().cells.at(static_cast<std::size_t>(row) * Client::Kernel::InventoryColumns +
                                          static_cast<std::size_t>(column));
    }

    // The mouse over cell (column, row) for a `width` × `height` item: 0x5ca2af's formula centers the footprint on the
    // mouse.
    static std::int32_t MouseX(std::int32_t column, std::int32_t width)
    {
        return Left + column * 20 + width * 10;
    }

    static std::int32_t MouseY(std::int32_t row, std::int32_t height)
    {
        return Top + row * 20 + height * 10;
    }

    [[nodiscard]] static CrtRandom Advanced(CrtRandom from, int count)
    {
        from.Discard(static_cast<std::uint32_t>(count));
        return from;
    }

    WorldState world;
    CrtRandom random{0x13579bdu};
    Client::Tables::ItemTable items = ItemTableOf({{Sword, 1, 3, 0},
                                                   {Shield, 2, 2, 1},
                                                   {Helm, 2, 2, HeadSlot},
                                                   {Potion, 1, 1, NotWorn},
                                                   {Jewel, 1, 1, NotWorn},
                                                   {Box, 1, 1, NotWorn},
                                                   {Bundle, 1, 1, NotWorn},
                                                   {Wings, 3, 2, WingSlot},
                                                   {Bow, 2, 4, 1},
                                                   {Crossbow, 2, 4, 0},
                                                   {Bolts, 1, 1, 1},
                                                   {Arrows, 1, 1, 0},
                                                   {OldDarkRaven, 1, 1, 0},
                                                   {WizardsRing, 1, 1, 10},
                                                   {EntryTicket, 1, 1, NotWorn}});
    Outside outside;
    InventoryServices services;
};

// 0x5ca2af..0x5ca2fd: _ftol of (mouse − corner) · 0.05f − size · 0.5f + 0.5f truncates toward 0, so up to a whole cell
// left of or above the grid still gives column or row 0.
TEST_F(CursorItemTest, TheCellUnderTheMouseTruncatesTowardZero)
{
    const Client::Items::ItemFootprint one{.width = 1, .height = 1};
    const Client::Items::ItemFootprint tall{.width = 1, .height = 3};
    const Client::Items::ItemFootprint wide{.width = 2, .height = 2};
    EXPECT_EQ(Client::Items::CellUnderTheMouse(Left + 10, Top + 10, Left, Top, one).column, 0);
    const auto cell = Client::Items::CellUnderTheMouse(Left + 3 * 20 + 10, Top + 5 * 20 + 10, Left, Top, one);
    EXPECT_EQ(cell.column, 3);
    EXPECT_EQ(cell.row, 5);
    EXPECT_EQ(Client::Items::CellUnderTheMouse(MouseX(4, 1), MouseY(2, 3), Left, Top, tall).row, 2);
    EXPECT_EQ(Client::Items::CellUnderTheMouse(MouseX(6, 2), MouseY(6, 2), Left, Top, wide).column, 6);
    EXPECT_EQ(Client::Items::CellUnderTheMouse(Left - 5, Top, Left, Top, one).column, 0);
    EXPECT_EQ(Client::Items::CellUnderTheMouse(Left - 15, Top, Left, Top, one).column, 0);
    EXPECT_EQ(Client::Items::CellUnderTheMouse(Left - 25, Top, Left, Top, one).column, -1);
}

// 0x5c9b40: every cell of the footprint inside the grid and empty; otherwise, the same stackable type in the top-left
// cell.
TEST_F(CursorItemTest, AnItemFitsWhereItsWholeFootprintIsFree)
{
    Put(Sword, 2, 1);
    Put(Shield, 5, 5);
    Put(Potion, 0, 7);
    const std::span<const ItemState> cells(world.Inventory().cells);
    const auto fits = [&](std::uint16_t type, std::int32_t column, std::int32_t row)
    {
        const auto footprint = Client::Items::ItemFootprintOf(&items, type, "test");
        return Client::Items::ItemFitsInTheGrid(
            Invented(type), cells, 8, 8, Client::Items::GridCell{.column = column, .row = row}, footprint.Value());
    };
    EXPECT_TRUE(fits(Helm, 0, 0));
    EXPECT_FALSE(fits(Helm, 1, 1));
    EXPECT_FALSE(fits(Helm, 7, 0));
    EXPECT_FALSE(fits(Helm, 6, 6));
    EXPECT_TRUE(fits(Helm, 6, 0));
    EXPECT_FALSE(fits(Sword, 3, 6));
    EXPECT_TRUE(fits(Potion, 0, 7));
    EXPECT_FALSE(fits(Potion, 2, 1));
    EXPECT_TRUE(fits(Wings, 5, 0));
    EXPECT_FALSE(fits(Wings, 6, 0));
}

// 0x5c52b1..0x5c5358: picking up from any cell of the footprint copies the top-left one to the cursor, with slot
// 12 + row · 8 + column, and 0x5bd750 empties the footprint, without opening the own record. 0x5be150 puts it back,
// with +0x3c at 1 at the origin, dresses the hero and asks for sound 0x1d.
TEST_F(CursorItemTest, PickingUpFromTheGridAndReturningItRestoresTheFootprint)
{
    Put(Sword, 2, 1);
    world.Runtime().mouse.pendingClick = true;
    world.Runtime().ui.hoveredItem =
        Client::Ui::HoveredItem{.place = Client::Ui::HoveredItem::Place::InventoryCell, .index = 2 * 8 + 2};
    const CrtRandom before = random;

    const auto picked = Client::Items::PickUpFromTheGrid(Moves(), 2 * 8 + 2);

    ASSERT_TRUE(picked.HasValue()) << picked.Error();
    const Client::Kernel::WorldRuntime& runtime = world.Runtime();
    EXPECT_EQ(runtime.cursorItem.id, Sword);
    EXPECT_EQ(runtime.cursorItemCount, 1);
    EXPECT_EQ(runtime.slot78a6758, 12 + 1 * 8 + 2);
    EXPECT_EQ(runtime.cursorGrid, Client::Kernel::CursorGrid::Inventory);
    EXPECT_FALSE(runtime.mouse.pendingClick);
    EXPECT_EQ(runtime.ui.hoveredItem.place, Client::Ui::HoveredItem::Place::None);
    EXPECT_EQ(runtime.hoveredItemCopy.id, EmptyOwnItemId);
    for (const std::int32_t row : {1, 2, 3})
    {
        EXPECT_EQ(Cell(2, row).id, EmptyOwnItemId) << row;
        EXPECT_EQ(Cell(2, row).occupied, 0u) << row;
    }
    EXPECT_EQ(random.State(), before.State());

    const auto returned = Client::Items::ReturnTheCursorItem(Moves());

    ASSERT_TRUE(returned.HasValue()) << returned.Error();
    EXPECT_EQ(runtime.cursorItemCount, 0);
    EXPECT_EQ(outside.dressed, 1);
    EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{0x1d});
    for (const std::int32_t row : {1, 2, 3})
    {
        EXPECT_EQ(Cell(2, row).id, Sword) << row;
        EXPECT_EQ(Cell(2, row).occupied, row == 1 ? 1u : 0u) << row;
    }
    EXPECT_EQ(random.State(), before.State());
}

// The trainer's box (0x723c80), 8 × 4 at (0x113, 0x6e) with 0x724580 at 3 (0x44ad10 and 0x44aca0).
constexpr std::int32_t TrainerLeft = 0x113;
constexpr std::int32_t TrainerTop = 0x6e;

// 0x5ca250 with kind 5 (0x44aca9): with 0x724580 at 3, 0x44acd0 only lets in the jewel of chaos, the mix ones and
// 0x1a1f; the jewel fits (marks 2) and, with the click, sends 0x449d80(0, 12, 5, row · 8 + column) with the send's two
// rand(). The weapon does not fit (marks 3, without 0x5c9b40) and sends nothing. In Lorencia and Devias.
TEST_F(CursorItemTest, TheTrainerBoxTakesOnlyItsJewels)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        SetUp();
        outside = Outside{};
        Client::Kernel::WorldRuntime& runtime = world.Runtime();
        runtime.map = map;
        runtime.ui.field78abb39 = 0;
        runtime.panels.delegated.trainer = 3;
        runtime.npcWindows.trainerBox = Client::Npc::EmptyTrainerBox();
        const auto drop = [&](std::int32_t column, std::int32_t row)
        {
            return Client::Items::DropOnOtherGrid(
                Moves(), Client::Items::OtherGridDrop{.mouseX = TrainerLeft + column * 20 + 10,
                                                      .mouseY = TrainerTop + row * 20 + 10,
                                                      .left = TrainerLeft,
                                                      .top = TrainerTop,
                                                      .grid = Client::Kernel::CursorGrid::TrainerBox});
        };

        Put(Jewel, 0, 0);
        runtime.mouse.pendingClick = true;
        ASSERT_TRUE(Client::Items::PickUpFromTheGrid(Moves(), 0).HasValue());
        runtime.mouse.pendingClick = true;
        const CrtRandom sent = Advanced(random, SendRandoms);
        const auto dropped = drop(3, 1);
        ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
        EXPECT_TRUE(dropped.Value());
        EXPECT_EQ(random.State(), sent.State());
        EXPECT_EQ(runtime.npcWindows.trainerBox[11].warning, 2);
        EXPECT_EQ(runtime.itemMoveTarget, 11);
        ASSERT_EQ(outside.sent.size(), 1u);
        EXPECT_EQ(outside.sent[0].sourceFlag, 0);
        EXPECT_EQ(outside.sent[0].sourceSlot, 12);
        EXPECT_EQ(outside.sent[0].targetFlag, 5);
        EXPECT_EQ(outside.sent[0].targetSlot, 11);

        runtime.ui.field78abb39 = 0;
        runtime.cursorItemCount = 0;
        Put(Sword, 2, 0);
        runtime.mouse.pendingClick = true;
        ASSERT_TRUE(Client::Items::PickUpFromTheGrid(Moves(), 2).HasValue());
        runtime.mouse.pendingClick = true;
        const CrtRandom refused = random;
        const auto kept = drop(5, 0);
        ASSERT_TRUE(kept.HasValue()) << kept.Error();
        EXPECT_FALSE(kept.Value());
        EXPECT_EQ(random.State(), refused.State());
        EXPECT_EQ(outside.sent.size(), 1u);
        for (const std::size_t cell : {5u, 13u, 21u})
            EXPECT_EQ(runtime.npcWindows.trainerBox[cell].warning, 3) << cell;
    }
}

// Picking up from the trainer's box (0x44ad10 → 0x5c4110): the copy to the cursor from its corner (the slot without
// the 12) and the footprint empty; 0x5be150 returns it with 0x449c80. Dropping it in the inventory sends 0x449d50,
// 0x449d80(5, slot, 0, destination). In Lorencia and Devias.
TEST_F(CursorItemTest, TheTrainerBoxItemGoesBackOrToTheInventory)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        SetUp();
        outside = Outside{};
        Client::Kernel::WorldRuntime& runtime = world.Runtime();
        runtime.map = map;
        runtime.ui.field78abb39 = 0;
        runtime.cursorItemCount = 0;
        runtime.panels.delegated.trainer = 3;
        auto& box = runtime.npcWindows.trainerBox;
        box = Client::Npc::EmptyTrainerBox();
        for (const std::size_t cell : {10u, 18u, 26u})
        {
            box[cell] = Invented(Sword);
            box[cell].gridColumn = 2;
            box[cell].gridRow = 1;
            box[cell].occupied = cell == 10 ? 1u : 0u;
        }
        const CrtRandom before = random;
        runtime.mouse.pendingClick = true;
        ASSERT_TRUE(Client::Items::PickUpFromOtherGrid(Moves(), Client::Kernel::CursorGrid::TrainerBox, 18).HasValue());
        EXPECT_EQ(runtime.cursorGrid, Client::Kernel::CursorGrid::TrainerBox);
        EXPECT_EQ(runtime.cursorItem.id, Sword);
        EXPECT_EQ(runtime.slot78a6758, 10);
        for (const std::size_t cell : {10u, 18u, 26u})
            EXPECT_EQ(box[cell].id, EmptyOwnItemId) << cell;

        ASSERT_TRUE(Client::Items::ReturnTheCursorItem(Moves()).HasValue());
        EXPECT_EQ(runtime.cursorItemCount, 0);
        for (const std::size_t cell : {10u, 18u, 26u})
        {
            EXPECT_EQ(box[cell].id, Sword) << cell;
            EXPECT_EQ(box[cell].occupied, cell == 10 ? 1u : 0u) << cell;
        }
        EXPECT_EQ(random.State(), before.State());

        runtime.mouse.pendingClick = true;
        ASSERT_TRUE(Client::Items::PickUpFromOtherGrid(Moves(), Client::Kernel::CursorGrid::TrainerBox, 10).HasValue());
        runtime.mouse.pendingClick = true;
        const CrtRandom sent = Advanced(random, SendRandoms);
        const auto dropped = Client::Items::DropOnTheInventoryGrid(
            Moves(),
            Client::Items::GridDrop{
                .mouseX = MouseX(4, 1), .mouseY = MouseY(2, 3), .left = Left, .top = Top, .inventoryOpen = true});
        ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
        EXPECT_TRUE(dropped.Value());
        EXPECT_EQ(random.State(), sent.State());
        ASSERT_EQ(outside.sent.size(), 1u);
        EXPECT_EQ(outside.sent[0].sourceFlag, 5);
        EXPECT_EQ(outside.sent[0].sourceSlot, 10);
        EXPECT_EQ(outside.sent[0].targetFlag, 0);
        EXPECT_EQ(outside.sent[0].targetSlot, 12 + 2 * 8 + 4);
    }
}

// 0x5ca250 with the click on a free place: marks 2 on the footprint, the lock, the send's two rand() and the C1 0E 24
// with the origin, bytes 0, +4, +0x1a, +0x1b, +0x1c, the type >> 5 & 0xf0 and 0, and the destination.
TEST_F(CursorItemTest, DroppingOnAFreePlaceOfTheGridSendsTheMove)
{
    Put(Sword, 2, 1);
    world.Runtime().mouse.pendingClick = true;
    ASSERT_TRUE(Client::Items::PickUpFromTheGrid(Moves(), 2 * 8 + 2).HasValue());
    world.Runtime().mouse.pendingClick = true;
    const CrtRandom expected = Advanced(random, SendRandoms);

    const auto dropped = Client::Items::DropOnTheInventoryGrid(
        Moves(), Client::Items::GridDrop{
                     .mouseX = MouseX(6, 1), .mouseY = MouseY(4, 3), .left = Left, .top = Top, .inventoryOpen = true});

    ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
    EXPECT_TRUE(dropped.Value());
    EXPECT_EQ(world.Runtime().ui.field78abb39, 1);
    EXPECT_EQ(world.Runtime().itemMoveTarget, 12 + 4 * 8 + 6);
    EXPECT_EQ(random.State(), expected.State());
    ASSERT_EQ(outside.sent.size(), 1u);
    const ItemMove& move = outside.sent.front();
    EXPECT_EQ(move.sourceFlag, 0);
    EXPECT_EQ(move.sourceSlot, 12 + 1 * 8 + 2);
    EXPECT_EQ(move.targetFlag, 0);
    EXPECT_EQ(move.targetSlot, 12 + 4 * 8 + 6);
    const Client::Protocol::Items::ItemData bytes{std::byte{0x01}, std::byte{0x0b}, std::byte{0x2c}, std::byte{0x00},
                                                  std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};
    EXPECT_EQ(move.item, bytes);
    for (const std::int32_t row : {4, 5, 6})
        EXPECT_EQ(Cell(6, row).warning, 2) << row;
}

// Over another item, mark 3 and, with the click, nothing; on the same place, 0x5be150; with the inventory closed, the
// click stays in the grid without sending anything. A lock already set does not send either.
TEST_F(CursorItemTest, TheGridRefusesOccupiedPlacesAndTakesItsOwnPlaceBack)
{
    Put(Sword, 2, 1);
    Put(Shield, 5, 5);
    world.Runtime().mouse.pendingClick = true;
    ASSERT_TRUE(Client::Items::PickUpFromTheGrid(Moves(), 2 * 8 + 2).HasValue());
    world.Runtime().mouse.pendingClick = true;
    const auto drop = [&](std::int32_t column, std::int32_t row, bool open)
    {
        return Client::Items::DropOnTheInventoryGrid(Moves(), Client::Items::GridDrop{.mouseX = MouseX(column, 1),
                                                                                      .mouseY = MouseY(row, 3),
                                                                                      .left = Left,
                                                                                      .top = Top,
                                                                                      .inventoryOpen = open});
    };

    const auto onTheShield = drop(5, 4, true);
    ASSERT_TRUE(onTheShield.HasValue()) << onTheShield.Error();
    EXPECT_FALSE(onTheShield.Value());
    EXPECT_EQ(Cell(5, 5).warning, 3);
    EXPECT_EQ(Cell(5, 4).warning, 3);

    const auto closed = drop(0, 0, false);
    ASSERT_TRUE(closed.HasValue()) << closed.Error();
    EXPECT_TRUE(closed.Value());
    EXPECT_TRUE(outside.sent.empty());
    EXPECT_EQ(world.Runtime().cursorItemCount, 1);

    const auto back = drop(2, 1, true);
    ASSERT_TRUE(back.HasValue()) << back.Error();
    EXPECT_TRUE(back.Value());
    EXPECT_TRUE(outside.sent.empty());
    EXPECT_EQ(world.Runtime().cursorItemCount, 0);
    EXPECT_EQ(Cell(2, 1).id, Sword);
}

// 0x5c007c..0x5c1d8a, in Lorencia, Devias and Noria: an item from another grid to the equipment sends the 24 from its
// grid with its slot without the 12 and the send's two rand(): the warehouse helmet to the head (2), the chaos box
// sword to the right (3) and the trainer's box shield to the left (0x449d50 at 0x5c1d80, the 5). With the warehouse
// lock, the move waits for the password (2, slot, 0, destination), the keypad shuffles (40 rand()), 0x78abb20 at 1 and
// what was typed at 0, without sending anything. The other grids use 1 and wait for the OK before the send's lock.
TEST_F(CursorItemTest, TheItemOfAnotherGridGoesToTheEquipment)
{
    for (const std::int32_t map : {0, 2, 3})
    {
        SCOPED_TRACE(map);
        SetUp();
        outside = Outside{};
        Client::Kernel::WorldRuntime& runtime = world.Runtime();
        runtime.map = map;
        runtime.ui.field78abb39 = 0;
        runtime.cursorItemCount = 0;
        runtime.panels.delegated.trainer = 3;
        world.Actors()[0].characterClass = DarkKnight;
        Client::Npc::NpcWindowState& npc = runtime.npcWindows;
        const auto place = [](std::span<ItemState> cells, std::uint16_t type, std::initializer_list<std::size_t> at)
        {
            for (const std::size_t cell : at)
            {
                cells[cell] = Invented(type);
                cells[cell].gridColumn = static_cast<std::uint8_t>(*at.begin() % 8);
                cells[cell].gridRow = static_cast<std::uint8_t>(*at.begin() / 8);
                cells[cell].occupied = cell == *at.begin() ? 1u : 0u;
            }
        };
        const auto pickUp = [&](Client::Kernel::CursorGrid grid, std::size_t cell)
        {
            runtime.ui.field78abb39 = 0;
            runtime.cursorItemCount = 0;
            runtime.mouse.pendingClick = true;
            ASSERT_TRUE(Client::Items::PickUpFromOtherGrid(Moves(), grid, cell).HasValue());
            EXPECT_EQ(runtime.cursorGrid, grid);
            runtime.mouse.pendingClick = true;
        };

        place(npc.grid, Helm, {9, 10, 17, 18});
        pickUp(Client::Kernel::CursorGrid::NpcGrid, 9);
        CrtRandom sent = Advanced(random, SendRandoms);
        auto dropped = Client::Items::DropOnTheEquipment(Moves(), HeadSlot);
        ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
        EXPECT_EQ(random.State(), sent.State());
        EXPECT_EQ(runtime.ui.field78abaac, 1);
        EXPECT_EQ(runtime.ui.field78abb39, 1);
        ASSERT_EQ(outside.sent.size(), 1u);
        EXPECT_EQ(outside.sent[0].sourceFlag, 2);
        EXPECT_EQ(outside.sent[0].sourceSlot, 9);
        EXPECT_EQ(outside.sent[0].item[0], std::byte{0x01});
        EXPECT_EQ(outside.sent[0].item[5], std::byte{0x70});
        EXPECT_EQ(outside.sent[0].targetFlag, 0);
        EXPECT_EQ(outside.sent[0].targetSlot, HeadSlot);

        // With the warehouse lock, the password: it does not check the send's lock or send.
        npc.warehouseLocked = 1;
        npc.field78abb1c = 0;
        npc.typedNumber.fill(0x37);
        runtime.mouse.pendingClick = true;
        CrtRandom shuffled = random;
        std::array<std::uint16_t, Client::Npc::WarehouseKeyCount> keys{};
        Client::Items::ShuffleTheKeypad(shuffled, keys);
        dropped = Client::Items::DropOnTheEquipment(Moves(), HeadSlot);
        ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
        EXPECT_EQ(random.State(), shuffled.State());
        EXPECT_EQ(random.State(), Advanced(sent, 40).State());
        EXPECT_EQ(npc.keypad, keys);
        EXPECT_EQ(npc.pendingMove.slot, 2);
        EXPECT_EQ(npc.pendingMove.second, 9);
        EXPECT_EQ(npc.pendingMove.third, 0);
        EXPECT_EQ(npc.pendingMove.fourth, HeadSlot);
        EXPECT_EQ(runtime.ui.field78abb20, 1);
        EXPECT_EQ(npc.typedNumber, (std::array<std::uint8_t, 11>{}));
        EXPECT_EQ(outside.sent.size(), 1u);
        npc.warehouseLocked = 0;
        runtime.ui.field78abb20 = 0;

        place(npc.chaosBox, Sword, {3, 11, 19});
        pickUp(Client::Kernel::CursorGrid::ChaosBox, 11);
        sent = Advanced(random, SendRandoms);
        dropped = Client::Items::DropOnTheEquipment(Moves(), 0);
        ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
        EXPECT_EQ(random.State(), sent.State());
        ASSERT_EQ(outside.sent.size(), 2u);
        EXPECT_EQ(outside.sent[1].sourceFlag, 3);
        EXPECT_EQ(outside.sent[1].sourceSlot, 3);
        EXPECT_EQ(outside.sent[1].targetFlag, 0);
        EXPECT_EQ(outside.sent[1].targetSlot, 0);

        runtime.npcWindows.trainerBox = Client::Npc::EmptyTrainerBox();
        place(runtime.npcWindows.trainerBox, Shield, {0, 1, 8, 9});
        pickUp(Client::Kernel::CursorGrid::TrainerBox, 8);
        sent = Advanced(random, SendRandoms);
        dropped = Client::Items::DropOnTheEquipment(Moves(), 1);
        ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
        EXPECT_EQ(random.State(), sent.State());
        ASSERT_EQ(outside.sent.size(), 3u);
        EXPECT_EQ(outside.sent[2].sourceFlag, 5);
        EXPECT_EQ(outside.sent[2].sourceSlot, 0);
        EXPECT_EQ(outside.sent[2].targetFlag, 0);
        EXPECT_EQ(outside.sent[2].targetSlot, 1);

        runtime.cursorGrid = Client::Kernel::CursorGrid::None;
        runtime.ui.field78abb39 = 0;
        runtime.mouse.pendingClick = true;
        const CrtRandom before = random;
        const auto droppedOther = Client::Items::DropOnTheEquipment(Moves(), 1);
        ASSERT_TRUE(droppedOther.HasValue()) << droppedOther.Error();
        EXPECT_EQ(random.State(), Advanced(before, SendRandoms).State());
        ASSERT_EQ(outside.sent.size(), 4u);
        EXPECT_EQ(outside.sent.back().sourceFlag, 1);
        EXPECT_EQ(runtime.social.trade.okWait, 150);
    }
}

// 0x5c1d90: the fallback also covers the personal shop; it sets the wait BEFORE testing 0x78abb39.
TEST_F(CursorItemTest, TradeAndShopItemsUseTheTradeSourceWhenDroppedOnEquipment)
{
    for (const auto grid : {Client::Kernel::CursorGrid::OwnTrade, Client::Kernel::CursorGrid::PersonalShop})
    {
        for (const std::uint8_t ownOk : std::array<std::uint8_t, 2>{0, 1})
        {
            for (const std::uint8_t locked : std::array<std::uint8_t, 2>{0, 1})
            {
                Client::Kernel::WorldRuntime& runtime = world.Runtime();
                runtime.cursorGrid = grid;
                runtime.cursorItem = Invented(Helm);
                runtime.slot78a6758 = 0x109;
                runtime.cursorItemCount = 1;
                runtime.ui.field78abb39 = locked;
                runtime.mouse.pendingClick = true;
                runtime.social.trade.ownOk = ownOk;
                runtime.social.trade.okWait = 7;
                outside.sent.clear();
                services.sendItemMove = [&](const Client::Protocol::Items::ItemMove& move)
                {
                    EXPECT_EQ(runtime.social.trade.okWait, ownOk == 0 ? 150 : 7);
                    outside.sent.push_back(move);
                    return Expected<void, std::string>{};
                };
                const CrtRandom before = random;
                const auto dropped = Client::Items::DropOnTheEquipment(Moves(), HeadSlot);
                ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
                EXPECT_EQ(runtime.social.trade.okWait, ownOk == 0 ? 150 : 7);
                EXPECT_EQ(random.State(), Advanced(before, locked == 0 ? SendRandoms : 0).State());
                EXPECT_EQ(runtime.ui.field78abb39, 1);
                if (locked != 0)
                {
                    EXPECT_TRUE(outside.sent.empty());
                    continue;
                }
                ASSERT_EQ(outside.sent.size(), 1u);
                const auto& move = outside.sent.front();
                EXPECT_EQ(move.sourceFlag, 1);
                EXPECT_EQ(move.sourceSlot, 9);
                EXPECT_EQ(move.targetFlag, 0);
                EXPECT_EQ(move.targetSlot, HeadSlot);
                EXPECT_EQ(move.item, (decltype(move.item){std::byte{1}, std::byte{0x0b}, std::byte{0x2c}, std::byte{0},
                                                          std::byte{0}, std::byte{0x70}, std::byte{0}}));
            }
        }
    }
}

// 0x5bee04..0x5bf2a0: the helmet goes on the head (mark 2 and, with the click, the 24 to slot 2) and not in the right
// hand (mark 3 and, with the click, 0x5be150). A one-handed weapon dropped on the left with the right empty goes to 0.
TEST_F(CursorItemTest, TheEquipmentTakesWhatGoesThere)
{
    world.Actors()[0].characterClass = DarkKnight;
    Put(Helm, 0, 0);
    world.Runtime().mouse.pendingClick = true;
    ASSERT_TRUE(Client::Items::PickUpFromTheGrid(Moves(), 0).HasValue());

    ASSERT_TRUE(Client::Items::DropOnTheEquipment(Moves(), 0).HasValue());
    EXPECT_EQ(world.OwnCharacter().equipment[0].warning, 3);
    ASSERT_TRUE(Client::Items::DropOnTheEquipment(Moves(), HeadSlot).HasValue());
    EXPECT_EQ(world.OwnCharacter().equipment[HeadSlot].warning, 2);
    EXPECT_TRUE(outside.sent.empty());

    world.Runtime().mouse.pendingClick = true;
    const CrtRandom expected = Advanced(random, SendRandoms);
    ASSERT_TRUE(Client::Items::DropOnTheEquipment(Moves(), HeadSlot).HasValue());
    EXPECT_FALSE(world.Runtime().mouse.pendingClick);
    EXPECT_EQ(world.Runtime().ui.field78abaac, 1);
    EXPECT_EQ(world.Runtime().itemMoveTarget, HeadSlot);
    EXPECT_EQ(random.State(), expected.State());
    ASSERT_EQ(outside.sent.size(), 1u);
    EXPECT_EQ(outside.sent.front().sourceSlot, 12);
    EXPECT_EQ(outside.sent.front().targetSlot, HeadSlot);

    world.Runtime().ui.field78abb39 = 0;
    world.Runtime().mouse.pendingClick = true;
    ASSERT_TRUE(Client::Items::DropOnTheEquipment(Moves(), 0).HasValue());
    EXPECT_EQ(world.Runtime().cursorItemCount, 0);
    EXPECT_EQ(Cell(0, 0).id, Helm);
    EXPECT_EQ(outside.sent.size(), 1u);

    Put(Sword, 4, 0);
    world.Runtime().mouse.pendingClick = true;
    ASSERT_TRUE(Client::Items::PickUpFromTheGrid(Moves(), 4).HasValue());
    world.Runtime().mouse.pendingClick = true;
    ASSERT_TRUE(Client::Items::DropOnTheEquipment(Moves(), 1).HasValue());
    EXPECT_EQ(world.Runtime().itemMoveTarget, 0);
    ASSERT_EQ(outside.sent.size(), 2u);
    EXPECT_EQ(outside.sent.back().targetSlot, 0);
}

// 0x5bf013..0x5bf04a: the 0x1a5 asks for leadership the raven's level ([0x723968]) · 15 + 0xb9, in 16 bits that wrap
// around, against +0x20 without additions (Q-C775).
TEST_F(CursorItemTest, TheOldDarkRavenAsksForTheLeadershipOfTheRavenLevel)
{
    world.Actors()[0].characterClass = DarkKnight;
    Client::Kernel::WorldRuntime& runtime = world.Runtime();
    runtime.cursorItem = Invented(OldDarkRaven);
    runtime.cursorItemCount = 1;
    Client::Kernel::OwnCharacterRecord& own = world.OwnCharacter();
    const auto mark = [&]
    {
        const auto dropped = Client::Items::DropOnTheEquipment(Moves(), 0);
        EXPECT_TRUE(dropped.HasValue()) << dropped.Error();
        return own.equipment[0].warning;
    };

    own.leadership = 0xb8;
    EXPECT_EQ(mark(), 3);
    own.leadership = 0xb9;
    EXPECT_EQ(mark(), 2);
    runtime.pets[Client::Kernel::RavenPetStats].level = 2;
    EXPECT_EQ(mark(), 3);
    own.leadership = 0xb9 + 30;
    EXPECT_EQ(mark(), 2);
    own.word36 = 100;
    own.leadership = 0xb9 + 29;
    EXPECT_EQ(mark(), 3);
    // 4357 · 15 + 0xb9 = 0x10004: 4 is left.
    runtime.pets[Client::Kernel::RavenPetStats].level = 4357;
    own.leadership = 4;
    EXPECT_EQ(mark(), 2);
}

// 0x5bf0f6..0x5bf185: with 0x435a00 (0x723c05 in 3 or 4) and the hero's 0x200000 in +0x7c, the horn does not go on the
// pet's slot and the Dark Raven does not go on the left hand. The items and their shapes are invented.
TEST_F(CursorItemTest, TheCrywolfLockNeedsTheStageThreeOrFourAndTheHerosFlag)
{
    constexpr std::uint16_t Horn = 0x1a03;
    constexpr std::uint16_t Raven = 0x1a05;
    constexpr std::size_t PetSlot = 8;
    constexpr std::size_t LeftHand = 1;
    constexpr std::uint32_t LockFlag = 0x200000;
    constexpr std::uint32_t EveryOtherFlag = ~LockFlag;
    constexpr std::int32_t Fits = 2;
    constexpr std::int32_t DoesNotFit = 3;
    items = ItemTableOf({{Horn, 1, 1, std::uint8_t{PetSlot}}, {Raven, 1, 1, std::uint8_t{LeftHand}}});
    world.Actors()[0].characterClass = DarkKnight;
    Client::Kernel::WorldRuntime& runtime = world.Runtime();
    Client::Kernel::OwnCharacterRecord& own = world.OwnCharacter();
    const auto mark = [&](std::uint16_t type, std::size_t slot, std::uint8_t stage, std::uint32_t flags)
    {
        runtime.cursorItem = Invented(type);
        runtime.cursorItemCount = 1;
        runtime.crywolf.stage = stage;
        world.Actors()[0].flags7c = flags;
        const auto dropped = Client::Items::DropOnTheEquipment(Moves(), slot);
        EXPECT_TRUE(dropped.HasValue()) << dropped.Error();
        return std::int32_t{own.equipment[slot].warning};
    };

    for (const std::uint8_t stage : std::initializer_list<std::uint8_t>{3, 4})
    {
        EXPECT_EQ(mark(Horn, PetSlot, stage, LockFlag), DoesNotFit) << "stage " << int{stage};
        EXPECT_EQ(mark(Raven, LeftHand, stage, LockFlag), DoesNotFit) << "stage " << int{stage};
        EXPECT_EQ(mark(Horn, PetSlot, stage, 0xffffffffu), DoesNotFit) << "stage " << int{stage};
        EXPECT_EQ(mark(Horn, PetSlot, stage, EveryOtherFlag), Fits) << "stage " << int{stage};
        EXPECT_EQ(mark(Raven, LeftHand, stage, 0), Fits) << "stage " << int{stage};
    }
    for (const std::uint8_t stage : std::initializer_list<std::uint8_t>{0, 1, 2, 5, 6, 0xff})
    {
        EXPECT_EQ(mark(Horn, PetSlot, stage, LockFlag), Fits) << "stage " << int{stage};
        EXPECT_EQ(mark(Raven, LeftHand, stage, LockFlag), Fits) << "stage " << int{stage};
    }
}

// 0x5c2c65..0x5c2dd0: picking up from the equipment leaves the slot at 0xffff (with +0x3c as it was), dresses the hero
// and asks for the sound. In Icarus, the only wings are not taken off.
TEST_F(CursorItemTest, PickingUpFromTheEquipmentEmptiesThePlace)
{
    ItemState& helm = world.OwnCharacter().equipment[HeadSlot];
    helm = Invented(Helm);
    helm.occupied = 1;
    world.Runtime().mouse.pendingClick = true;

    ASSERT_TRUE(Client::Items::PickUpFromTheEquipment(Moves(), HeadSlot).HasValue());

    const Client::Kernel::WorldRuntime& runtime = world.Runtime();
    EXPECT_EQ(runtime.cursorItem.id, Helm);
    EXPECT_EQ(runtime.cursorItemCount, 1);
    EXPECT_EQ(runtime.slot78a6758, HeadSlot);
    EXPECT_EQ(helm.id, EmptyOwnItemId);
    EXPECT_EQ(helm.occupied, 1u);
    EXPECT_EQ(outside.dressed, 1);
    EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{0x1d});
    EXPECT_FALSE(runtime.mouse.pendingClick);

    world.Runtime().cursorItemCount = 0;
    world.Runtime().map = 10;
    world.OwnCharacter().equipment[WingSlot] = Invented(Wings);
    world.Runtime().mouse.pendingClick = true;
    ASSERT_TRUE(Client::Items::PickUpFromTheEquipment(Moves(), WingSlot).HasValue());
    EXPECT_EQ(world.OwnCharacter().equipment[WingSlot].id, Wings);
    EXPECT_EQ(world.Runtime().cursorItemCount, 0);
}

// 0x5c35ad: mark 3 if a requirement is missing (+0x26 against strength, Q-C475) and 2 otherwise, the label item and
// its help's corner in the middle of the slot.
TEST_F(CursorItemTest, HoveringTheEquipmentMarksWhatTheHeroCannotUse)
{
    ItemState& helm = world.OwnCharacter().equipment[HeadSlot];
    helm = Invented(Helm);
    helm.word26 = 20;
    world.OwnCharacter().strength = 10;

    Client::Items::HoverTheEquipment(Moves(), HeadSlot, 300, 46, 40.0f);

    EXPECT_EQ(helm.warning, 3);
    EXPECT_EQ(world.Runtime().ui.hoveredItem.place, Client::Ui::HoveredItem::Place::Equipment);
    EXPECT_EQ(world.Runtime().ui.hoveredItem.index, HeadSlot);
    EXPECT_EQ(world.Runtime().hoveredItemCopy.id, Helm);
    EXPECT_EQ(world.Runtime().ui.skillTooltipX, 320);
    EXPECT_EQ(world.Runtime().ui.skillTooltipY, 46);

    world.OwnCharacter().strength = 20;
    Client::Items::HoverTheEquipment(Moves(), HeadSlot, 300, 46, 40.0f);
    EXPECT_EQ(helm.warning, 2);
}

// 0x5df449..0x5df58b: what cannot be dropped warns with 269 and goes back; the boxes, with 1915; the jewel bundles,
// with 269 except with the inventory open and the mouse to its right. The rest stops where it starts dropping it
// (CL9.3).
TEST_F(CursorItemTest, WhatCannotBeDroppedGoesBackWithItsWarning)
{
    const auto hold = [&](std::uint16_t type)
    {
        Put(type, 0, 0);
        world.Runtime().mouse.pendingClick = true;
        ASSERT_TRUE(Client::Items::PickUpFromTheGrid(Moves(), 0).HasValue());
    };
    const auto ground = [&](std::int32_t mouseX, bool open) {
        return Client::Items::DropOnTheGround(Moves(),
                                              Client::Items::GroundDrop{.mouseX = mouseX, .inventoryOpen = open});
    };

    hold(Jewel);
    ASSERT_TRUE(ground(100, false).HasValue());
    EXPECT_EQ(outside.messages, std::vector<std::size_t>{269});
    EXPECT_EQ(Cell(0, 0).id, Jewel);

    hold(Box);
    ASSERT_TRUE(ground(100, false).HasValue());
    EXPECT_EQ(outside.messages.back(), 1915u);

    hold(Bundle);
    ASSERT_TRUE(ground(Left, true).HasValue());
    EXPECT_EQ(outside.messages.size(), 2u);
    EXPECT_EQ(world.Runtime().cursorItemCount, 0);
    hold(Bundle);
    ASSERT_TRUE(ground(Left - 15, true).HasValue());
    EXPECT_EQ(outside.messages.size(), 3u);
    EXPECT_EQ(outside.messages.back(), 269u);

    // The rest goes on to the windows (0x5df58b), which ask for the panel registry.
    hold(Shield);
    const auto dropped = ground(100, false);
    ASSERT_FALSE(dropped.HasValue());
    EXPECT_TRUE(dropped.Error().starts_with("0x5df59a:")) << dropped.Error();
}

// 0x5df58b..0x5e069d (CL9 milestone 5): with no windows under the mouse, the ring (the own record), IsOpen(0x20), the
// terrain cell under the mouse and the C1 06 23 with the cell and the slot it came from, in C3; then [0x6c82ec] with
// that slot and the mouse wait at 6.
TEST_F(CursorItemTest, TheDropSendsTheCellAndTheSlotByteByByte)
{
    Put(Shield, 2, 1);
    world.Runtime().mouse.pendingClick = true;
    ASSERT_TRUE(Client::Items::PickUpFromTheGrid(Moves(), 1 * 8 + 2).HasValue());
    ASSERT_EQ(world.Runtime().slot78a6758, 12 + 10);
    std::vector<Drop> sent;
    services.sendDrop = [&sent](const Drop& drop)
    {
        sent.push_back(drop);
        return Expected<void, std::string>{};
    };
    services.pickTheGround = [] { return Expected<std::optional<PathCell>, std::string>(PathCell{130, 121}); };
    Client::Hud::GamePanels panels(world, random);
    // IsOpen(0x10000) and IsOpen(0x20) of the windows, the own record, IsOpen(0x20) again and the send.
    const CrtRandom expected = Advanced(random, 1 + 1 + Own + 1 + SendRandoms);
    world.Runtime().mouse.updateTime = 3;

    const auto dropped = Client::Items::DropOnTheGround(
        Moves(), Client::Items::GroundDrop{.mouseX = 100, .inventoryOpen = false, .panels = &panels.Registry()});

    ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
    EXPECT_EQ(random.State(), expected.State());
    ASSERT_EQ(sent.size(), 1u);
    const auto packet = Client::Protocol::Items::WriteDrop(sent.front());
    EXPECT_TRUE(SameBytes(Xor32Decode(packet.packet).Bytes(), Bytes({0xc1, 0x06, 0x23, 130, 121, 22})));
    EXPECT_EQ(world.Runtime().floor.pendingDrop, 22);
    EXPECT_EQ(world.Runtime().mouse.updateTime, 0);
    EXPECT_EQ(world.Runtime().mouse.updateTimeMax, 6);
    // The item stays on the cursor until the 23.
    EXPECT_EQ(world.Runtime().cursorItemCount, 1);
    EXPECT_EQ(Cell(2, 1).id, EmptyOwnItemId);
}

// 0x5dfdfc: with no terrain under the mouse nothing goes out and the item stays on the cursor. 0x5df58b: with the
// inventory open and the mouse on it, it goes back to its place.
TEST_F(CursorItemTest, WithoutGroundOrOverTheInventoryNothingIsDropped)
{
    Put(Potion, 0, 0);
    world.Runtime().mouse.pendingClick = true;
    ASSERT_TRUE(Client::Items::PickUpFromTheGrid(Moves(), 0).HasValue());
    int sent = 0;
    services.sendDrop = [&sent](const Drop&)
    {
        ++sent;
        return Expected<void, std::string>{};
    };
    services.pickTheGround = [] { return Expected<std::optional<PathCell>, std::string>(std::nullopt); };
    Client::Hud::GamePanels panels(world, random);
    const CrtRandom expected = Advanced(random, 1 + 1 + Own + 1);
    ASSERT_TRUE(
        Client::Items::DropOnTheGround(Moves(), Client::Items::GroundDrop{.mouseX = 100, .panels = &panels.Registry()})
            .HasValue());
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(sent, 0);
    EXPECT_EQ(world.Runtime().cursorItemCount, 1);
    EXPECT_EQ(world.Runtime().floor.pendingDrop, -1);

    ASSERT_TRUE(Client::Items::DropOnTheGround(
                    Moves(),
                    Client::Items::GroundDrop{.mouseX = Left - 15, .inventoryOpen = true, .panels = &panels.Registry()})
                    .HasValue());
    EXPECT_EQ(sent, 0);
    EXPECT_EQ(world.Runtime().cursorItemCount, 0);
    EXPECT_EQ(Cell(0, 0).id, Potion);
}

// 0x5df651..0x5df671: with the trade open, the mouse from its window's x (0x78a5a1c, its draw's at 0x104) does not
// allow dropping; before it, it does, but 0x5dfda8 checks the trade again (another rand()). Both return the item to
// its place (0x5be150) without sending anything: the window one saves that query.
TEST_F(CursorItemTest, WithTheTradeOpenTheItemGoesBackWithOrWithoutTheWindow)
{
    int sent = 0;
    services.sendDrop = [&sent](const Drop&)
    {
        ++sent;
        return Expected<void, std::string>{};
    };
    services.pickTheGround = [] { return Expected<std::optional<PathCell>, std::string>(PathCell{130, 121}); };
    world.Runtime().panels.trade = 1;
    world.Runtime().hud.tradeX = 0x104;
    Client::Hud::GamePanels panels(world, random);
    const auto drawsOf = [this](std::int32_t mouseX, Client::Hud::GamePanels& registry)
    {
        Put(Potion, 0, 0);
        world.Runtime().mouse.pendingClick = true;
        EXPECT_TRUE(Client::Items::PickUpFromTheGrid(Moves(), 0).HasValue());
        const CrtRandom start = random;
        const auto dropped = Client::Items::DropOnTheGround(
            Moves(), Client::Items::GroundDrop{.mouseX = mouseX, .panels = &registry.Registry()});
        EXPECT_TRUE(dropped.HasValue()) << dropped.Error();
        EXPECT_EQ(world.Runtime().cursorItemCount, 0);
        EXPECT_EQ(Cell(0, 0).id, Potion);
        CrtRandom probe = start;
        for (int count = 0; count < 2 * Own; ++count)
        {
            if (probe.State() == random.State())
                return count;
            static_cast<void>(probe.Next());
        }
        return -1;
    };
    const int over = drawsOf(0x104, panels);
    const int before = drawsOf(0x103, panels);
    EXPECT_EQ(sent, 0);
    EXPECT_GE(over, 1 + 1 + Own);
    EXPECT_EQ(before, over + 1);
}

// 0x5df6c8..0x5dfa52: the level 2 wizard's ring asks for level 80 (warning 923 with the number, of type 1) and goes
// back; the level 3 one goes back without a warning.
TEST_F(CursorItemTest, TheWizardsRingAsksForItsLevelBeforeBeingDropped)
{
    const auto texts = Client::Tests::MakeTextTable({{923, "hace falta el nivel %d"}});
    services.texts = &texts;
    std::vector<std::string> lines;
    services.chatLine = [&lines](std::string_view line, std::int32_t type)
    {
        EXPECT_EQ(type, 1);
        lines.emplace_back(line);
        return Expected<void, std::string>{};
    };
    world.OwnCharacter().level = 50;
    Client::Hud::GamePanels panels(world, random);
    for (const std::uint32_t level : {2u, 3u})
    {
        Put(WizardsRing, 0, 0);
        Cell(0, 0).scalar1Low = level << 3u;
        world.Runtime().mouse.pendingClick = true;
        ASSERT_TRUE(Client::Items::PickUpFromTheGrid(Moves(), 0).HasValue());
        ASSERT_TRUE(Client::Items::DropOnTheGround(
                        Moves(), Client::Items::GroundDrop{.mouseX = 100, .panels = &panels.Registry()})
                        .HasValue());
        EXPECT_EQ(world.Runtime().cursorItemCount, 0);
        EXPECT_EQ(Cell(0, 0).id, WizardsRing);
    }
    EXPECT_EQ(lines, std::vector<std::string>{"hace falta el nivel 80"});
}

// 0x5df5cf..0x5df6c4: each window reads the draw's retained x, with a signed comparison and without cutting off the
// UI.
TEST_F(CursorItemTest, NpcWindowsKeepTheDropOnlyOnTheirSideOfTheScreen)
{
    for (const int kind : {0, 1, 2, 3})
    {
        for (const std::int32_t mouseX : {259, 260})
        {
            auto& runtime = world.Runtime();
            runtime.panels.npcShop = kind == 0 ? 1 : 0;
            runtime.panels.warehouse = kind == 1 ? 1 : 0;
            runtime.panels.chaosBox = kind == 2 ? 1 : 0;
            runtime.panels.delegated.trainer = kind == 3 ? 3 : 0;
            runtime.ui.npcWindowX = 260;
            Put(Potion, 0, 0);
            runtime.mouse.pendingClick = true;
            ASSERT_TRUE(Client::Items::PickUpFromTheGrid(Moves(), 0).HasValue());
            std::vector<Drop> sent;
            services.sendDrop = [&sent](const Drop& drop)
            {
                sent.push_back(drop);
                return Expected<void, std::string>{};
            };
            services.pickTheGround = [] { return Expected<std::optional<PathCell>, std::string>(PathCell{130, 121}); };
            Client::Hud::GamePanels panels(world, random);
            const auto dropped = Client::Items::DropOnTheGround(
                Moves(), Client::Items::GroundDrop{.mouseX = mouseX, .panels = &panels.Registry()});
            ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
            EXPECT_EQ(sent.size(), mouseX < 260 ? 1u : 0u);
            EXPECT_EQ(runtime.cursorItemCount, mouseX < 260 ? 1 : 0);
        }
    }
}

// 0x5df635→0x6689d0: only mode 3, a non-negative WORD mark and master 0x80 keep it to the NPC's right.
TEST_F(CursorItemTest, TheSeniorCraftWindowKeepsTheDropWithItsNativeSignedGuildMark)
{
    struct Case
    {
        std::uint16_t mark;
        std::uint8_t guildStatus;
        std::int32_t mode;
        std::int32_t mouseX;
        bool kept;
    };
    constexpr std::array<Case, 7> cases{{{0, 0x80, 3, 260, true},
                                         {0x7fff, 0x80, 3, 260, true},
                                         {0x8000, 0x80, 3, 260, false},
                                         {0xffff, 0x80, 3, 260, false},
                                         {0, 0x7f, 3, 260, false},
                                         {0, 0x80, 2, 260, false},
                                         {0, 0x80, 3, 259, false}}};
    for (const Case& given : cases)
    {
        auto& runtime = world.Runtime();
        runtime.cursorItemCount = 0;
        runtime.ui.field78abb39 = 0;
        runtime.panels.delegated.castleSiegeList = 1;
        runtime.panels.delegated.castleSiegeListMode = given.mode;
        runtime.ui.npcWindowX = 260;
        auto& hero = world.Hero();
        hero.word1f2 = given.mark;
        hero.guildStatus = given.guildStatus;
        Put(Potion, 0, 0);
        runtime.mouse.pendingClick = true;
        ASSERT_TRUE(Client::Items::PickUpFromTheGrid(Moves(), 0).HasValue());
        std::vector<Drop> sent;
        services.sendDrop = [&sent](const Drop& drop)
        {
            sent.push_back(drop);
            return Expected<void, std::string>{};
        };
        services.pickTheGround = [] { return Expected<std::optional<PathCell>, std::string>(PathCell{130, 121}); };
        Client::Hud::GamePanels panels(world, random);
        const auto dropped = Client::Items::DropOnTheGround(
            Moves(), Client::Items::GroundDrop{.mouseX = given.mouseX, .panels = &panels.Registry()});
        ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
        EXPECT_EQ(sent.size(), given.kept ? 0u : 1u);
        EXPECT_EQ(runtime.cursorItemCount, given.kept ? 0 : 1);
    }
}

// 0x5dfb3e and 0x4379f0: the table's dwords and the maps cutoff are different from 0x437a20's safe zone.
TEST_F(CursorItemTest, EntryTicketsRespectTheNativeThresholdMapAndSafeZone)
{
    struct Case
    {
        std::uint8_t characterClass;
        std::uint16_t heroLevel;
        std::uint32_t itemLevel;
        std::int32_t map;
        std::uint8_t safeZone;
        bool sends;
        bool warns;
    };
    constexpr std::array<Case, 9> Cases{{{0, 40, 1, 0, 0, true, false},
                                         {0, 39, 1, 0, 0, false, false},
                                         {0, 65535, 0, 0, 0, false, false},
                                         {3, 350, 0, 0, 0, true, false},
                                         {4, 20, 1, 0, 0, true, false},
                                         {0, 40, 1, 24, 0, false, false},
                                         {0, 40, 1, 36, 0, false, false},
                                         {0, 40, 1, 0, 1, false, true},
                                         {4, 19, 12, 0, 0, false, true}}};
    const auto texts = Client::Tests::MakeTextTable({{1123, "Desde %d"}});
    services.texts = &texts;
    std::vector<std::string> lines;
    services.chatLine = [&lines](std::string_view line, std::int32_t type)
    {
        EXPECT_EQ(type, 2);
        lines.emplace_back(line);
        return Expected<void, std::string>{};
    };
    for (const Case& entry : Cases)
    {
        auto& runtime = world.Runtime();
        runtime.map = entry.map;
        world.Hero().characterClass = entry.characterClass;
        world.Hero().safeZone = entry.safeZone;
        world.OwnCharacter().level = entry.heroLevel;
        Put(EntryTicket, 0, 0);
        Cell(0, 0).scalar1Low = entry.itemLevel << 3;
        runtime.mouse.pendingClick = true;
        ASSERT_TRUE(Client::Items::PickUpFromTheGrid(Moves(), 0).HasValue());
        std::vector<Drop> sent;
        services.sendDrop = [&sent](const Drop& drop)
        {
            sent.push_back(drop);
            return Expected<void, std::string>{};
        };
        services.pickTheGround = [] { return Expected<std::optional<PathCell>, std::string>(PathCell{130, 121}); };
        Client::Hud::GamePanels panels(world, random);
        const auto before = random;
        outside.messages.clear();
        lines.clear();
        const auto dropped = Client::Items::DropOnTheGround(
            Moves(), Client::Items::GroundDrop{.mouseX = 100, .panels = &panels.Registry()});
        ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
        EXPECT_EQ(sent.size(), entry.sends ? 1u : 0u);
        EXPECT_EQ(runtime.cursorItemCount, entry.sends ? 1 : 0);
        EXPECT_EQ(!outside.messages.empty() || !lines.empty(), entry.warns);
        EXPECT_EQ(random.State(), Advanced(before, 2 + 2 * Own + (entry.sends ? 3 : 0)).State());
        if (entry.safeZone != 0)
        {
            EXPECT_EQ(outside.messages, std::vector<std::size_t>{1238});
        }
        if (entry.characterClass == 4 && entry.heroLevel == 19)
        {
            EXPECT_EQ(lines, std::vector<std::string>{"Desde 20"});
        }
    }
}

// 0x59bd3e..0x59bf56: MoveHero moves the left weapon to the cursor in one own record access, dresses the hero and
// sends the 24 from slot 1 to 0, unless the lock is set.
TEST_F(CursorItemTest, MoveHeroSendsTheLeftWeaponToTheRight)
{
    ItemState& left = world.OwnCharacter().equipment[1];
    left = Invented(Sword);
    left.occupied = 1;
    const CrtRandom expected = Advanced(random, Own + SendRandoms);

    ASSERT_TRUE(Client::Items::MoveTheLeftWeaponToTheRight(Moves()).HasValue());

    const Client::Kernel::WorldRuntime& runtime = world.Runtime();
    EXPECT_EQ(runtime.cursorItem.id, Sword);
    EXPECT_EQ(runtime.cursorItemCount, 1);
    EXPECT_EQ(left.id, EmptyOwnItemId);
    EXPECT_EQ(left.scalar1Low, 0u);
    EXPECT_EQ(outside.dressed, 1);
    EXPECT_EQ(runtime.cursorGrid, Client::Kernel::CursorGrid::Inventory);
    EXPECT_EQ(runtime.slot78a6758, 1);
    EXPECT_EQ(runtime.itemMoveTarget, 0);
    EXPECT_EQ(runtime.ui.field78abb39, 1);
    EXPECT_EQ(random.State(), expected.State());
    ASSERT_EQ(outside.sent.size(), 1u);
    EXPECT_EQ(outside.sent.front().sourceSlot, 1);
    EXPECT_EQ(outside.sent.front().targetSlot, 0);

    left = Invented(Sword);
    const CrtRandom locked = Advanced(random, Own);
    ASSERT_TRUE(Client::Items::MoveTheLeftWeaponToTheRight(Moves()).HasValue());
    EXPECT_EQ(random.State(), locked.State());
    EXPECT_EQ(outside.sent.size(), 1u);
}

// 0x5bd750 → 0x561840 → 0x5579e0 (Q-C477): removing a hand of another class closes the record twice (0x5bd750 and
// 0x5579e0) and warns with 251 and type 2.
TEST_F(CursorItemTest, RemovingAHandOfAnotherClassWarnsThatThereAreNoArrows)
{
    world.OwnCharacter().characterClass = DarkKnight;
    world.OwnCharacter().equipment[0] = Invented(Sword);
    const CrtRandom expected = Advanced(random, 2 * Own);

    ASSERT_TRUE(Client::Items::RemoveInventoryItem(Moves(), 0).HasValue());

    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(outside.messages, std::vector<std::size_t>{NoArrowsText});
    EXPECT_EQ(outside.messageTypes, std::vector<std::int32_t>{2});
    EXPECT_TRUE(outside.sent.empty());
}

// 0x5579e0 walks the grid from column 7 to 0 and, in each, bottom to top: with the bow on the left, the arrows at
// (6, 2) before those at (3, 5). With the record opened by the caller (like 0x561c50), 0x561840 reads the hands: the
// empty right one receives the arrows, with the 24 from slot 34 to 0 and 250 with type 1.
TEST_F(CursorItemTest, AnElfWithABowTakesTheArrowsOfTheLastColumnIntoTheRightHand)
{
    world.OwnCharacter().characterClass = Elf;
    world.OwnCharacter().equipment[0] = Invented(Arrows);
    world.OwnCharacter().equipment[1] = Invented(Bow);
    Put(Arrows, 3, 5);
    Put(Arrows, 6, 2);
    Put(Bolts, 7, 7);
    Client::Kernel::WorldRuntime& runtime = world.Runtime();
    const Client::Models::LogicalAccess caller(runtime.ownRecordDepth, random, Client::Models::OwnRecordCloseRandoms);
    const CrtRandom expected = Advanced(random, SendRandoms);

    ASSERT_TRUE(Client::Items::RemoveInventoryItem(Moves(), 0).HasValue());

    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(runtime.cursorItem.id, Arrows);
    EXPECT_EQ(runtime.cursorItemCount, 1);
    EXPECT_EQ(runtime.slot78a6758, 12 + 2 * 8 + 6);
    EXPECT_EQ(runtime.itemMoveTarget, 0);
    EXPECT_EQ(runtime.ui.field78abb39, 1);
    EXPECT_EQ(Cell(6, 2).id, EmptyOwnItemId);
    EXPECT_EQ(Cell(3, 5).id, Arrows);
    ASSERT_EQ(outside.sent.size(), 1u);
    EXPECT_EQ(outside.sent.front().sourceSlot, 12 + 2 * 8 + 6);
    EXPECT_EQ(outside.sent.front().targetSlot, 0);
    EXPECT_EQ(outside.messages, std::vector<std::size_t>{EquippedArrowsText});
    EXPECT_EQ(outside.messageTypes, std::vector<std::int32_t>{1});
    EXPECT_EQ(world.OwnCharacter().equipment[0].id, EmptyOwnItemId);
}

// With a crossbow from 0x808 to 0x80e on the right, the bolts go to the left (the 1). With the lock set, 0x449d80
// sends nothing nor takes its two rand(), and the warning goes out anyway.
TEST_F(CursorItemTest, AnElfWithACrossbowTakesTheBoltsIntoTheLeftHand)
{
    world.OwnCharacter().characterClass = Elf;
    world.OwnCharacter().equipment[0] = Invented(Crossbow);
    world.OwnCharacter().equipment[1] = Invented(Bolts);
    Put(Arrows, 5, 0);
    Put(Bolts, 1, 4);
    Client::Kernel::WorldRuntime& runtime = world.Runtime();
    runtime.ui.field78abb39 = 1;
    const Client::Models::LogicalAccess caller(runtime.ownRecordDepth, random, Client::Models::OwnRecordCloseRandoms);
    const CrtRandom expected = random;

    ASSERT_TRUE(Client::Items::RemoveInventoryItem(Moves(), 1).HasValue());

    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(runtime.cursorItem.id, Bolts);
    EXPECT_EQ(runtime.slot78a6758, 12 + 4 * 8 + 1);
    EXPECT_EQ(runtime.itemMoveTarget, 1);
    EXPECT_EQ(Cell(1, 4).id, EmptyOwnItemId);
    EXPECT_TRUE(outside.sent.empty());
    EXPECT_EQ(outside.messages, std::vector<std::size_t>{EquippedArrowsText});
}

// Without what it looks for, 251; with the cursor taken, 0x561840 closes its access and does nothing else. With the
// record closed by everyone, 0x561840's close leaves the hands as it left them (Q-C202) and stops; without bow or
// crossbow, the type is the count the protection table left (Q-C772) and it stops if any cell could be it.
TEST_F(CursorItemTest, TheArrowsStopWhereTheOriginalReadsWhatMuRetDoesNotHave)
{
    world.OwnCharacter().characterClass = Elf;
    ItemState& left = world.OwnCharacter().equipment[1];
    ItemState& right = world.OwnCharacter().equipment[0];
    left = Invented(Bow);
    right = Invented(Arrows);
    ASSERT_TRUE(Client::Items::RemoveInventoryItem(Moves(), 0).HasValue());
    EXPECT_EQ(outside.messages, std::vector<std::size_t>{NoArrowsText});

    Put(Arrows, 0, 0);
    right = Invented(Arrows);
    world.Runtime().cursorItemCount = 1;
    CrtRandom expected = Advanced(random, 3 * Own);
    ASSERT_TRUE(Client::Items::RemoveInventoryItem(Moves(), 0).HasValue());
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(outside.messages.size(), 1u);
    EXPECT_EQ(Cell(0, 0).id, Arrows);

    world.Runtime().cursorItemCount = 0;
    right = Invented(Arrows);
    expected = Advanced(random, 3 * Own);
    const auto closed = Client::Items::RemoveInventoryItem(Moves(), 0);
    ASSERT_FALSE(closed.HasValue());
    EXPECT_TRUE(closed.Error().starts_with("0x561b13:")) << closed.Error();
    EXPECT_EQ(random.State(), expected.State());

    left = Invented(Shield);
    right = Invented(Sword);
    const auto probe = Client::Items::RemoveInventoryItem(Moves(), 0);
    ASSERT_FALSE(probe.HasValue());
    EXPECT_TRUE(probe.Error().starts_with("0x557c62:")) << probe.Error();

    world.Inventory().ClearForInventoryReply();
    right = Invented(Sword);
    ASSERT_TRUE(Client::Items::RemoveInventoryItem(Moves(), 0).HasValue());
    EXPECT_EQ(outside.messages.size(), 2u);
    EXPECT_EQ(outside.messages.back(), NoArrowsText);
}

// 0x561c50 (the attack click, CL9 milestone 3): without a ranged weapon, one access and true; with a crossbow and its
// bolts with durability, two and true. Without them, 0x561840 inside the second access (nested: without Q-C202's stop)
// and false: 251 if there are no more in the grid, or the grid's arrows to the bow's empty right hand, with its 24.
TEST_F(CursorItemTest, TheAttackChecksTheArrowsOfTheRangedWeapon)
{
    world.OwnCharacter().characterClass = Elf;
    ItemState& right = world.OwnCharacter().equipment[0];
    ItemState& left = world.OwnCharacter().equipment[1];
    const auto checked = [this](int draws, bool expected)
    {
        const CrtRandom after = Advanced(random, draws);
        const auto arrows = Client::Items::CheckTheArrows(Moves());
        ASSERT_TRUE(arrows.HasValue()) << arrows.Error();
        EXPECT_EQ(arrows.Value(), expected);
        EXPECT_EQ(random.State(), after.State());
    };

    right = Invented(Sword);
    checked(Own, true);
    // 0x811 on the right is not a crossbow for 0x561c50.
    right = Invented(0x0811);
    checked(Own, true);

    right = Invented(Crossbow);
    left = Invented(Bolts);
    checked(2 * Own, true);
    EXPECT_TRUE(outside.messages.empty());

    left.durability = 0;
    checked(2 * Own, false);
    EXPECT_EQ(outside.messages, std::vector<std::size_t>{NoArrowsText});

    right = ItemState{};
    right.id = EmptyOwnItemId;
    left = Invented(Bow);
    Put(Arrows, 6, 2);
    checked(2 * Own + SendRandoms, false);
    EXPECT_EQ(outside.messages.back(), EquippedArrowsText);
    ASSERT_EQ(outside.sent.size(), 1u);
    EXPECT_EQ(outside.sent.front().sourceSlot, 12 + 2 * 8 + 6);
    EXPECT_EQ(outside.sent.front().targetSlot, 0);
    EXPECT_EQ(world.Runtime().cursorItem.id, Arrows);
}

// Without the send, the 24 stops after the lock and its two rand(); without Item.bmd, picking up stops before touching
// anything.
TEST_F(CursorItemTest, WithoutItsServicesAMoveStopsWithItsAddress)
{
    Put(Sword, 2, 1);
    services.sendItemMove = {};
    world.Runtime().mouse.pendingClick = true;
    ASSERT_TRUE(Client::Items::PickUpFromTheGrid(Moves(), 2 * 8 + 2).HasValue());
    const CrtRandom expected = Advanced(random, SendRandoms);
    const auto sent = Client::Items::SendTheCursorItem(Moves(), 30);
    ASSERT_FALSE(sent.HasValue());
    EXPECT_TRUE(sent.Error().starts_with("0x449d80")) << sent.Error();
    EXPECT_EQ(random.State(), expected.State());

    services.items = nullptr;
    world.Runtime().cursorItemCount = 0;
    const auto returned = Client::Items::DropOnTheInventoryGrid(
        Moves(),
        Client::Items::GridDrop{.mouseX = Left, .mouseY = Top, .left = Left, .top = Top, .inventoryOpen = true});
    ASSERT_FALSE(returned.HasValue());
    EXPECT_NE(returned.Error().find("0x7758550"), std::string::npos) << returned.Error();
}
} // namespace
