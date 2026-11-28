#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/ItemTable.h"
#include "Tables/TextTable.h"
#include "Network/Xor32Packet.h"
#include "Protocol/Items/FloorItems.h"
#include "Protocol/Items/Inventory.h"
#include "Models/LogicalAccess.h"
#include "Hud/GamePanels.h"
#include "Items/GridItemUse.h"
#include "Items/InventoryMoves.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/OwnInventory.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameDialogs.h"
#include "Ui/GameUiState.h"
#include "Kernel/ItemState.h"
#include "Audio/SoundId.h"

#include "support/Bytes.h"
#include "support/ItemFiles.h"
#include "support/SameBytes.h"
#include "support/SceneDataFiles.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The right click on the grid item from 0x5c7bc6 and the jewel on an item (0x5d82a8), CL9 milestone 5, phase 2: the
// books and orbs, the fruit, the 0x1a0b, the rest and the three jewels, with the C1 06 26 byte by byte and the rand()
// count. The items, their slots, the stats and the texts are made up.
namespace
{
using Client::Core::Expected;
using Client::Crt::CrtRandom;
using Client::Items::InventoryMoveContext;
using Client::Items::InventoryServices;
using Client::Kernel::ItemState;
using Client::Kernel::WorldState;
using Client::Network::Xor32Decode;
using Client::Protocol::Items::UseItem;
using Client::Tests::Bytes;
using Client::Tests::SameBytes;
namespace Scenes = Client::Scenes;

constexpr int Own = Client::Models::OwnRecordCloseRandoms;
// The two rand() of the C3 send: the padding and the serial.
constexpr int SendRandoms = 2;
constexpr std::int32_t Left = 0x1c2 + 15;
constexpr std::int32_t Top = 200;
constexpr std::uint8_t Kind = 3;

constexpr std::uint16_t Sword = 0x0001;           // 1 × 3
constexpr std::uint16_t Bolts = 0x0807;           // 1 × 1
constexpr std::uint16_t Wings = 0x1806;           // 1 × 1 here: the jewel's last type
constexpr std::uint16_t Orb180f = 0x180f;         // outside the orbs
constexpr std::uint16_t Item1a0b = 0x1a0b;        // 1 × 1
constexpr std::uint16_t Fruit = 0x1a0f;           // 1 × 1
constexpr std::uint16_t Item1a1e = 0x1a1e;        // 1 × 1, the other one that takes the jewel
constexpr std::uint16_t Item1a25 = 0x1a25;        // 1 × 1, the one of dialog 0xa9
constexpr std::uint16_t Potion = 0x1c00;          // 1 × 1
constexpr std::uint16_t JewelOfBless = 0x1c0d;    // 1 × 1
constexpr std::uint16_t JewelOfSoul = 0x1c0e;     // 1 × 1
constexpr std::uint16_t JewelOfLife = 0x1c10;     // 1 × 1
constexpr std::uint16_t JewelOfCreation = 0x1c16; // 1 × 1
// The books and orbs: the edges of the three ranges and the 0x1814 in the middle.
constexpr std::array<std::uint16_t, 7> SkillItems{0x1e00, 0x1fff, 0x1807, 0x180e, 0x1810, 0x1814, 0x1818};
constexpr std::uint16_t RequiredLevel = 50;
constexpr std::size_t RequiredLevelOffset = 0x3a;

Client::Tables::ItemTable MakeItems()
{
    std::vector<Client::Tests::ItemShape> shapes{{Sword, 1, 3, 0},
                                                 {Bolts, 1, 1, 1},
                                                 {Wings, 1, 1, 7},
                                                 {Orb180f, 1, 1, 0xff},
                                                 {Item1a0b, 1, 1, 0xff},
                                                 {Fruit, 1, 1, 0xff},
                                                 {Item1a1e, 1, 1, 7},
                                                 {Item1a25, 1, 1, 8},
                                                 {Potion, 1, 1, 0xff},
                                                 {JewelOfBless, 1, 1, 0xff},
                                                 {JewelOfSoul, 1, 1, 0xff},
                                                 {JewelOfLife, 1, 1, 0xff},
                                                 {JewelOfCreation, 1, 1, 0xff}};
    std::vector<Client::Tests::ItemWord> words;
    for (const std::uint16_t type : SkillItems)
    {
        shapes.push_back({type, 1, 1, 0xff});
        words.push_back({type, RequiredLevelOffset, RequiredLevel});
    }
    return Client::Tests::ItemTableOf(shapes, words);
}

class GridItemUseTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        world.OwnCharacter().ClearEquipmentForEntry();
        world.Inventory().ClearForInventoryReply();
        world.Runtime().ui.field78abb26 = Kind;
        services.items = &items;
        services.texts = &texts;
        services.systemMessage = [this](std::size_t index, std::int32_t type)
        {
            messages.push_back(index);
            messageTypes.push_back(type);
            return Expected<void, std::string>{};
        };
        services.sendUseItem = [this](const UseItem& use)
        {
            sent.push_back(use);
            return Expected<void, std::string>{};
        };
    }

    [[nodiscard]] InventoryMoveContext Moves()
    {
        return InventoryMoveContext{.world = world, .random = random, .services = services};
    }

    // The item `type` from cell (column, row), as 0x5bc740 leaves it: the copy in each cell, with the origin.
    void Put(std::uint16_t type, std::uint8_t column, std::uint8_t row, std::uint32_t scalar1Low = 0)
    {
        const auto footprint = Client::Items::ItemFootprintOf(&items, type, "Put");
        ASSERT_TRUE(footprint.HasValue()) << footprint.Error();
        for (std::int32_t y = 0; y < footprint.Value().height; ++y)
        {
            for (std::int32_t x = 0; x < footprint.Value().width; ++x)
            {
                ItemState& cell = Cell(column + x, row + y);
                cell = ItemState{};
                cell.id = type;
                cell.scalar1Low = scalar1Low;
                cell.durability = 0x2c;
                cell.gridColumn = column;
                cell.gridRow = row;
                cell.occupied = x == 0 && y == 0 ? 1u : 0u;
            }
        }
    }

    ItemState& Cell(std::int32_t column, std::int32_t row)
    {
        return world.Inventory().cells.at(static_cast<std::size_t>(row) * Client::Kernel::InventoryColumns +
                                          static_cast<std::size_t>(column));
    }

    Client::Items::UsedGrid InventoryGrid()
    {
        return Client::Items::UsedGrid{.cells = world.Inventory().cells, .grid = Client::Kernel::CursorGrid::Inventory};
    }

    static std::size_t Index(std::int32_t column, std::int32_t row)
    {
        return static_cast<std::size_t>(row) * Client::Kernel::InventoryColumns + static_cast<std::size_t>(column);
    }

    Expected<void, std::string> RightClick(std::int32_t column, std::int32_t row)
    {
        Client::Hud::GamePanels panels(world, random);
        return Client::Items::UseTheLaterGridItem(Moves(), panels.Registry(), InventoryGrid(), Index(column, row));
    }

    // The C1 06 26 that went out, decrypted.
    [[nodiscard]] std::vector<std::byte> SentBytes(std::size_t which) const
    {
        const auto packet = Client::Protocol::Items::WriteUseItem(sent.at(which));
        const auto decoded = Xor32Decode(packet.packet);
        const std::span<const std::byte> plain = decoded.Bytes();
        return {plain.begin(), plain.end()};
    }

    [[nodiscard]] static CrtRandom Advanced(CrtRandom from, int count)
    {
        from.Discard(static_cast<std::uint32_t>(count));
        return from;
    }

    // The stats the books are used with: the exact level, and the exact energy and strength with what the items add.
    void Qualify()
    {
        Client::Kernel::OwnCharacterRecord& own = world.OwnCharacter();
        own.level = RequiredLevel;
        own.energy = 30;
        own.word30 = 10;
        own.strength = 20;
        own.word2a = 5;
    }

    WorldState world;
    CrtRandom random{0x2b6d1e5u};
    Client::Tables::ItemTable items = MakeItems();
    Client::Tables::TextTable texts = Client::Tests::MakeTextTable({{166, "nieve"},
                                                                    {167, "sol"},
                                                                    {168, "cielo"},
                                                                    {169, "mar"},
                                                                    {1900, "mando"},
                                                                    {376, "usar?"},
                                                                    {474, "no se puede usar ahora"},
                                                                    {749, "desde el nivel diez"},
                                                                    {1865, "joya sobre el cuerno"},
                                                                    {1905, "solo una clase"},
                                                                    {1909, "sin equipo"}});
    InventoryServices services;
    std::vector<std::size_t> messages;
    std::vector<std::int32_t> messageTypes;
    std::vector<UseItem> sent;
};

// 0x5c8fb9..0x5c991b: each book and each orb with Item.bmd's level (+0x3a), the energy (+0x1e and +0x30 against the
// item's +0x22) and the strength (+0x18 and +0x2a against the +0x1e) sends the C1 06 26 with the top left cell + 12, 0
// and [0x78abb26]: the trade's rand(), the two of the send and then the 3848 of the own record's close.
TEST_F(GridItemUseTest, EachBookAndOrbWithItsRequirementsSendsTheUseByteByByte)
{
    Qualify();
    for (const std::uint16_t type : SkillItems)
    {
        SCOPED_TRACE(type);
        sent.clear();
        world.Runtime().ui.field5767954 = 0;
        Put(type, 3, 2);
        Cell(3, 2).word22 = 40;
        Cell(3, 2).word1e = 25;
        const CrtRandom expected = Advanced(random, 1 + SendRandoms + Own);
        const auto used = RightClick(3, 2);
        ASSERT_TRUE(used.HasValue()) << used.Error();
        EXPECT_EQ(random.State(), expected.State());
        ASSERT_EQ(sent.size(), 1u);
        EXPECT_TRUE(SameBytes(SentBytes(0), Bytes({0xc1, 0x06, 0x26, 2 * 8 + 3 + 12, 0x00, Kind})));
        EXPECT_EQ(world.Runtime().ui.field5767954, 10);
        EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
        EXPECT_FALSE(world.Runtime().lastSoundId.has_value());
        EXPECT_TRUE(messages.empty());
    }
}

// 0x5c90ca..0x5c90e3: three JC on WORD. With one point short in level, energy or strength there is no warning or send:
// only the record's close. The 16-bit sum wraps: 0xfff0 of energy with 0x20 from the items gives 0x10.
TEST_F(GridItemUseTest, ABookWithoutItsLevelEnergyOrStrengthDoesNothing)
{
    Put(0x1e00, 0, 0);
    Cell(0, 0).word22 = 40;
    Cell(0, 0).word1e = 25;
    const auto refused = [this](auto&& lower)
    {
        Qualify();
        lower(world.OwnCharacter());
        const CrtRandom expected = Advanced(random, Own);
        const auto used = RightClick(0, 0);
        ASSERT_TRUE(used.HasValue()) << used.Error();
        EXPECT_EQ(random.State(), expected.State());
        EXPECT_TRUE(sent.empty());
        EXPECT_TRUE(messages.empty());
        EXPECT_EQ(world.Runtime().ui.field5767954, 0);
    };
    refused([](Client::Kernel::OwnCharacterRecord& own) { own.level = RequiredLevel - 1; });
    refused([](Client::Kernel::OwnCharacterRecord& own) { own.word30 = 9; });
    refused([](Client::Kernel::OwnCharacterRecord& own) { own.word2a = 4; });
    refused(
        [](Client::Kernel::OwnCharacterRecord& own)
        {
            own.energy = 0xfff0;
            own.word30 = 0x20;
        });
}

// The inline queries (0x5c90e9..0x5c912b): with the warehouse open, the warning 474 of type 2 without querying the
// trade; with 0x5767954 above 0, nothing, after the trade's rand().
TEST_F(GridItemUseTest, ABookWithTheWarehouseOpenOrTheWaitWarnsOrWaits)
{
    Qualify();
    Put(0x1810, 5, 5);
    world.Runtime().panels.warehouse = 1;
    CrtRandom expected = Advanced(random, Own);
    ASSERT_TRUE(RightClick(5, 5).HasValue());
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(messages, std::vector<std::size_t>{474});
    EXPECT_EQ(messageTypes, std::vector<std::int32_t>{2});
    EXPECT_TRUE(sent.empty());

    world.Runtime().panels.warehouse = 0;
    world.Runtime().ui.field5767954 = 3;
    expected = Advanced(random, 1 + Own);
    ASSERT_TRUE(RightClick(5, 5).HasValue());
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_TRUE(sent.empty());
    EXPECT_EQ(world.Runtime().ui.field5767954, 3);
}

// 0x5c8e37..0x5c8fb8: a sword (over its middle cell) and the 0x180f belong to no case: the copy to the cursor and,
// without the warehouse or 0x7d368a1, the cursor at zeros. No rand() (IsOpen(0x40) draws none) and the grid as it was.
// With the warehouse open, it stops (milestone 6).
TEST_F(GridItemUseTest, AnyOtherItemLeavesTheCursorEmptyWithoutTheWarehouse)
{
    Put(Sword, 1, 1, 3u << 3);
    Put(Orb180f, 4, 4);
    for (const std::size_t index : {Index(1, 2), Index(4, 4)})
    {
        SCOPED_TRACE(index);
        world.Runtime().cursorItem.id = Potion;
        world.Runtime().cursorGrid = Client::Kernel::CursorGrid::None;
        world.Runtime().ui.field78abb28 = 0;
        const CrtRandom expected = random;
        Client::Hud::GamePanels panels(world, random);
        const auto used = Client::Items::UseTheLaterGridItem(Moves(), panels.Registry(), InventoryGrid(), index);
        ASSERT_TRUE(used.HasValue()) << used.Error();
        EXPECT_EQ(random.State(), expected.State());
        EXPECT_EQ(world.Runtime().cursorItem.id, 0);
        EXPECT_EQ(world.Runtime().cursorItem.occupied, 0u);
        EXPECT_EQ(world.Runtime().cursorItemCount, 0);
        EXPECT_EQ(world.Runtime().cursorGrid, Client::Kernel::CursorGrid::Inventory);
        EXPECT_EQ(world.Runtime().ui.field78abb28, 1);
        EXPECT_TRUE(sent.empty());
    }
    EXPECT_EQ(Cell(1, 1).id, Sword);
    EXPECT_EQ(Cell(1, 3).id, Sword);
    EXPECT_EQ(Cell(4, 4).id, Orb180f);
}

// 0x5c9e10 searches columns and rows in descending order; 0x5c8e8f keeps the copy until the send at 0x5ca250.
TEST_F(GridItemUseTest, ARightClickWithTheWarehouseMovesToTheLastFreeFootprintWithoutALeftClick)
{
    auto& runtime = world.Runtime();
    runtime.panels.warehouse = 1;
    runtime.ui.npcWindowX = 260;
    runtime.ui.npcWindowY = 0;
    for (auto& item : runtime.npcWindows.grid)
        item.id = Client::Kernel::EmptyOwnItemId;
    Put(Sword, 1, 1, 3u << 3);
    std::vector<Client::Protocol::Items::ItemMove> moves;
    services.sendItemMove = [&moves](const Client::Protocol::Items::ItemMove& move)
    {
        moves.push_back(move);
        return Expected<void, std::string>{};
    };
    const auto before = random;
    const auto used = RightClick(1, 2);
    ASSERT_TRUE(used.HasValue()) << used.Error();
    EXPECT_EQ(runtime.cursorItemCount, 1);
    EXPECT_EQ(runtime.cursorItem.id, Sword);
    EXPECT_EQ(runtime.slot78a6758, 21);
    EXPECT_EQ(Cell(1, 1).id, Client::Kernel::EmptyOwnItemId);
    EXPECT_EQ(Cell(1, 3).id, Client::Kernel::EmptyOwnItemId);
    EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{0x1d});
    EXPECT_EQ(random.State(), before.State());
    EXPECT_FALSE(runtime.mouse.pendingClick);
    const auto dropped = Client::Items::DropOnOtherGrid(
        Moves(), {.mouseX = 100, .mouseY = 100, .left = 275, .top = 50, .grid = Client::Kernel::CursorGrid::NpcGrid});
    ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
    EXPECT_TRUE(dropped.Value());
    ASSERT_EQ(moves.size(), 1u);
    EXPECT_EQ(moves[0].sourceSlot, 21);
    EXPECT_EQ(moves[0].targetSlot, 103);
    EXPECT_EQ(runtime.ui.field78abb39, 1);
    EXPECT_EQ(random.State(), Advanced(before, 2).State());
}

TEST_F(GridItemUseTest, AFullWarehouseKeepsTheRightClickedItemInTheInventory)
{
    world.Runtime().panels.warehouse = 1;
    for (auto& item : world.Runtime().npcWindows.grid)
        item.id = Potion;
    Put(Sword, 1, 1);
    const auto used = RightClick(1, 1);
    ASSERT_TRUE(used.HasValue()) << used.Error();
    EXPECT_EQ(world.Runtime().cursorItemCount, 0);
    EXPECT_EQ(world.Runtime().cursorItem.id, 0);
    EXPECT_EQ(Cell(1, 1).id, Sword);
    EXPECT_EQ(Cell(1, 3).id, Sword);
}

// The target goes back to the inventory; the password is asked for after taking the item out, without sending the 24
// before.
TEST_F(GridItemUseTest, ARightClickFromTheWarehouseWithdrawsOrWaitsForItsPassword)
{
    auto& runtime = world.Runtime();
    runtime.panels.warehouse = 1;
    runtime.hud.inventoryX = 450;
    std::vector<Client::Protocol::Items::ItemMove> moves;
    services.sendItemMove = [&moves](const Client::Protocol::Items::ItemMove& move)
    {
        moves.push_back(move);
        return Expected<void, std::string>{};
    };
    for (const bool locked : {false, true})
    {
        world.Inventory().ClearForInventoryReply();
        for (auto& item : runtime.npcWindows.grid)
            item.id = Client::Kernel::EmptyOwnItemId;
        for (const std::size_t index : {9u, 17u, 25u})
        {
            auto& cell = runtime.npcWindows.grid[index];
            cell.id = Sword;
            cell.gridColumn = 1;
            cell.gridRow = 1;
            cell.occupied = index == 9u ? 1u : 0u;
        }
        runtime.ui.field78abb39 = 0;
        runtime.ui.field78abb20 = 0;
        runtime.npcWindows.warehouseLocked = locked ? 1 : 0;
        runtime.npcWindows.field78abb1c = 0;
        runtime.npcWindows.typedNumber[0] = '9';
        moves.clear();
        const auto before = random;
        Client::Hud::GamePanels panels(world, random);
        const auto used = Client::Items::UseTheLaterGridItem(
            Moves(), panels.Registry(), {.cells = runtime.npcWindows.grid, .grid = Client::Kernel::CursorGrid::NpcGrid},
            17);
        ASSERT_TRUE(used.HasValue()) << used.Error();
        EXPECT_EQ(runtime.npcWindows.grid[9].id, Client::Kernel::EmptyOwnItemId);
        const auto dropped = Client::Items::DropOnTheInventoryGrid(
            Moves(), {.mouseX = 0, .mouseY = 0, .left = 465, .top = 200, .inventoryOpen = true});
        ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
        EXPECT_TRUE(dropped.Value());
        EXPECT_FALSE(runtime.ui.automaticMove);
        EXPECT_FALSE(runtime.mouse.pendingClick);
        EXPECT_EQ(random.State(), Advanced(before, locked ? 40 : 2).State());
        if (locked)
        {
            EXPECT_TRUE(moves.empty());
            EXPECT_EQ(runtime.ui.field78abb20, 1);
            EXPECT_EQ(runtime.npcWindows.pendingMove.slot, 2);
            EXPECT_EQ(runtime.npcWindows.pendingMove.second, 9);
            EXPECT_EQ(runtime.npcWindows.pendingMove.third, 0);
            EXPECT_EQ(runtime.npcWindows.pendingMove.fourth, 59);
            EXPECT_EQ(runtime.npcWindows.typedNumber[0], 0);
        }
        else
        {
            ASSERT_EQ(moves.size(), 1u);
            EXPECT_EQ(moves[0].sourceFlag, 2);
            EXPECT_EQ(moves[0].sourceSlot, 9);
            EXPECT_EQ(moves[0].targetFlag, 0);
            EXPECT_EQ(moves[0].targetSlot, 59);
        }
    }
}

// 0x5c7c10..0x5c8447: the fruit opens and closes the own record for the level (3848 rand()) and, below 10, warns with
// the 749 (0x632e30: the 0x8a). From 10 it keeps it at 0x78a59c8 with its cell, opens and closes it again (another
// 3848) for the equipment and the class: with something equipped, the 1909.
TEST_F(GridItemUseTest, TheFruitAsksTheLevelAndThenTheEmptyEquipment)
{
    Put(Fruit, 6, 3, 1u << 3);
    world.OwnCharacter().level = 9;
    CrtRandom expected = Advanced(random, Own);
    ASSERT_TRUE(RightClick(6, 3).HasValue());
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(Client::Ui::DialogRow(world.Runtime().dialogBox, 0), "desde el nivel diez");
    EXPECT_EQ(world.Runtime().dialogs.current, Client::Ui::MessageBoxDialog);
    EXPECT_EQ(world.Runtime().dialogItem.id, 0);

    world.Runtime().dialogs.current = 0;
    world.OwnCharacter().level = 10;
    world.OwnCharacter().equipment[11].id = 0x1a14;
    expected = Advanced(random, 2 * Own);
    ASSERT_TRUE(RightClick(6, 3).HasValue());
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(Client::Ui::DialogRow(world.Runtime().dialogBox, 0), "sin equipo");
    EXPECT_EQ(world.Runtime().dialogItem.id, Fruit);
    EXPECT_EQ(world.Runtime().dialogItem.occupied, 1u);
    EXPECT_EQ(world.Runtime().dialogItemCell, 3 * 8 + 6);
    EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
    EXPECT_TRUE(sent.empty());
}

// 0x5c83ad..0x5c83f3: with no equipment, the level 4 fruit (the whole dword +4 at 0x20) without class 4 warns with the
// 1905; with class 4, or with another bit in the dword (Q-C1244), it goes on to dialog 0x96 with the fruit's name. A
// level 0 too.
TEST_F(GridItemUseTest, TheFourthFruitIsForTheFourthClassAndTheDialogReceivesItsItem)
{
    world.OwnCharacter().level = 200;
    world.OwnCharacter().characterClass = 1;
    Put(Fruit, 0, 7, 4u << 3);
    ASSERT_TRUE(RightClick(0, 7).HasValue());
    EXPECT_EQ(Client::Ui::DialogRow(world.Runtime().dialogBox, 0), "solo una clase");

    const auto dialog = [this](std::uint32_t scalar1Low, std::uint8_t heroClass, std::string_view name)
    {
        world.OwnCharacter().characterClass = heroClass;
        Cell(0, 7).scalar1Low = scalar1Low;
        const CrtRandom expected = Advanced(random, 2 * Own);
        world.Runtime().dialogs = {};
        const auto opened = RightClick(0, 7);
        ASSERT_TRUE(opened.HasValue()) << opened.Error();
        EXPECT_EQ(world.Runtime().dialogs.current, 0x96);
        EXPECT_EQ(Client::Ui::DialogRow(world.Runtime().dialogBox, 0), name);
        EXPECT_EQ(random.State(), expected.State());
        EXPECT_EQ(world.Runtime().ownRecordDepth, 0);
    };
    dialog(4u << 3, 4, "( mando )");
    dialog((4u << 3) | 4u, 1, "( mando )");
    dialog(0, 1, "( cielo )");
}

// 0x5c8245: the fruit compares the whole class byte (+0xb), not its `& 7`: the fourth class with a bit above its three
// low bits is not the fourth class.
TEST_F(GridItemUseTest, TheFourthFruitComparesTheWholeClassByte)
{
    world.OwnCharacter().level = 200;
    world.OwnCharacter().characterClass = 4 | 8;
    Put(Fruit, 0, 7, 4u << 3);
    ASSERT_TRUE(RightClick(0, 7).HasValue());
    EXPECT_EQ(Client::Ui::DialogRow(world.Runtime().dialogBox, 0), "solo una clase");
}

// 0x5c8448..0x5c8473: the level 0 0x1a0b is used; the level 1 one is not with the hero as guild master (+0x368 at
// 0x80); the level 2 one, never. The use is the grid's: two rand(), without the own record.
TEST_F(GridItemUseTest, The1a0bOfLevelZeroOrOneIsUsed)
{
    Put(Item1a0b, 7, 0);
    const auto use = [this](std::uint32_t level, std::uint8_t guild, bool sends)
    {
        SCOPED_TRACE(level);
        sent.clear();
        world.Runtime().ui.field5767954 = 0;
        Cell(7, 0).scalar1Low = level << 3;
        world.Hero().guildStatus = guild;
        const CrtRandom expected = Advanced(random, sends ? 1 + SendRandoms : 0);
        ASSERT_TRUE(RightClick(7, 0).HasValue());
        EXPECT_EQ(random.State(), expected.State());
        ASSERT_EQ(sent.size(), sends ? 1u : 0u);
        if (sends)
        {
            EXPECT_TRUE(SameBytes(SentBytes(0), Bytes({0xc1, 0x06, 0x26, 7 + 12, 0x00, Kind})));
        }
    };
    use(0, 0x80, true);
    use(1, 0, true);
    use(1, 0x80, false);
    use(2, 0, false);
}

// 0x5c67f3..0x5c685f: the 0x1a1d looks at the NPC shop (with its rand()), the warehouse, the trade (with its own),
// 0x1000 and 0x4000, and with the first open it does nothing (0x5c9b0b). With all closed, and always with the 0x1c13
// and the 0x1a12, the 91 stops (milestone 11).
TEST_F(GridItemUseTest, TheTicket1a1dLooksAtTheShopFirst)
{
    Client::Hud::GamePanels panels(world, random);
    Cell(0, 0).id = 0x1a1d;
    std::vector<Client::Protocol::Outgoing> requests;
    services.sendGamePacket = [&requests](Client::Protocol::Outgoing packet) { requests.push_back(std::move(packet)); };
    Client::Ui::PanelState& state = world.Runtime().panels;
    const auto look = [&](std::uint8_t& open, int randoms)
    {
        open = 1;
        const CrtRandom expected = Advanced(random, randoms);
        const auto used = Client::Items::UseTheEventTicket(Moves(), panels.Registry(), InventoryGrid(), 0);
        ASSERT_TRUE(used.HasValue()) << used.Error();
        EXPECT_EQ(random.State(), expected.State());
        open = 0;
    };
    {
        SCOPED_TRACE("tienda");
        look(state.npcShop, 1);
    }
    {
        SCOPED_TRACE("almacén");
        look(state.warehouse, 1);
    }
    {
        SCOPED_TRACE("comercio");
        look(state.trade, 2);
    }
    const CrtRandom expected = Advanced(random, 2);
    const auto used = Client::Items::UseTheEventTicket(Moves(), panels.Registry(), InventoryGrid(), 0);
    ASSERT_TRUE(used.HasValue()) << used.Error();
    ASSERT_EQ(requests.size(), 1u);
    EXPECT_TRUE(SameBytes(Xor32Decode(requests.back().packet).Bytes(), Bytes({0xc1, 4, 0x91, 4})));
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_TRUE(sent.empty());

    state.npcShop = 1;
    for (const auto& [type, kind] : {std::pair{0x1c13, 1}, std::pair{0x1a12, 2}})
    {
        const CrtRandom unchanged = random;
        Cell(0, 0).id = static_cast<std::uint16_t>(type);
        const auto other = Client::Items::UseTheEventTicket(Moves(), panels.Registry(), InventoryGrid(), 0);
        ASSERT_TRUE(other.HasValue()) << other.Error();
        EXPECT_TRUE(SameBytes(Xor32Decode(requests.back().packet).Bytes(),
                              Bytes({0xc1, 4, 0x91, static_cast<std::uint8_t>(kind)})));
        EXPECT_EQ(random.State(), unchanged.State());
        EXPECT_TRUE(Client::Items::IsAnEventTicket(type));
    }
    EXPECT_FALSE(Client::Items::IsAnEventTicket(0x1a1e));
}

// 0x5a3296..0x5a32c9 and 0x5d8c9a: the sound reads the cell slot − 12 after the send. With an equipment slot or past
// the grid, the original reads the memory next to it; the packet already went out and MuRet does not read it: no
// sound and no stop.
TEST_F(GridItemUseTest, AUseFromOutsideTheGridSendsWithoutTheSound)
{
    for (const std::int32_t slot : {3, 12 + static_cast<std::int32_t>(Client::Kernel::InventoryCellCount)})
    {
        SCOPED_TRACE(slot);
        sent.clear();
        world.Runtime().ui.field5767954 = 0;
        world.Runtime().lastSoundId.reset();
        Client::Hud::GamePanels panels(world, random);
        const CrtRandom expected = Advanced(random, 1 + SendRandoms);
        const auto used = Client::Items::UseInventoryItemOn(
            Moves(), panels.Registry(), Client::Items::InventoryUse{.slot = slot, .target = 20, .where = "test"});
        ASSERT_TRUE(used.HasValue()) << used.Error();
        EXPECT_EQ(random.State(), expected.State());
        ASSERT_EQ(sent.size(), 1u);
        EXPECT_TRUE(SameBytes(SentBytes(0), Bytes({0xc1, 0x06, 0x26, static_cast<std::uint8_t>(slot), 20, Kind})));
        EXPECT_FALSE(world.Runtime().lastSoundId.has_value());
    }
}

// The cursor jewel over a grid item (0x5ca250 → 0x5d82a8), with the click and the inventory open.
class JewelTest : public GridItemUseTest
{
protected:
    void SetUp() override
    {
        GridItemUseTest::SetUp();
        world.Runtime().cursorItemCount = 1;
        world.Runtime().cursorGrid = Client::Kernel::CursorGrid::Inventory;
        world.Runtime().slot78a6758 = JewelCell + 12;
    }

    Expected<bool, std::string> Drop(std::uint16_t jewel, std::int32_t column, std::int32_t row,
                                     bool inventoryOpen = true)
    {
        world.Runtime().cursorItem = ItemState{};
        world.Runtime().cursorItem.id = jewel;
        world.Runtime().cursorItem.occupied = 1;
        world.Runtime().mouse.pendingClick = true;
        world.Runtime().ui.field5767954 = 0;
        sent.clear();
        Client::Hud::GamePanels panels(world, random);
        return Client::Items::DropOnTheInventoryGrid(Moves(), Client::Items::GridDrop{.mouseX = Left + column * 20 + 10,
                                                                                      .mouseY = Top + row * 20 + 10,
                                                                                      .left = Left,
                                                                                      .top = Top,
                                                                                      .inventoryOpen = inventoryOpen,
                                                                                      .panels = &panels.Registry()});
    }

    static constexpr std::int32_t JewelCell = 5;
};

// 0x5d82a8..0x5d8cd3: over the middle cell of a level 3 sword, the jewel of bless sends the C1 06 26 with its slot
// ([0x78a6758]), the one of the sword's origin (0x77f3cc8) and [0x78abb26]: two rand(); it returns true. The sound
// reads the jewel's cell, empty.
TEST_F(JewelTest, TheBlessOnASwordSendsItsPlaceAndTheSwordsByteByByte)
{
    Put(Sword, 2, 1, 3u << 3);
    const CrtRandom expected = Advanced(random, 1 + SendRandoms);
    const auto dropped = Drop(JewelOfBless, 2, 2);
    ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
    EXPECT_TRUE(dropped.Value());
    EXPECT_EQ(random.State(), expected.State());
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_TRUE(SameBytes(SentBytes(0), Bytes({0xc1, 0x06, 0x26, JewelCell + 12, 1 * 8 + 2 + 12, Kind})));
    EXPECT_EQ(world.Runtime().itemMoveTarget, 1 * 8 + 2 + 12);
    EXPECT_EQ(world.Runtime().ui.field5767954, 10);
    EXPECT_EQ(world.Runtime().cursorItemCount, 1);
    EXPECT_FALSE(world.Runtime().lastSoundId.has_value());
}

// 0x5d82d4..0x5d8316: up to 0x1806 and the 0x1a1e, without the bolts or the arrows; bless up to level 5, soul up to 8
// and life on any. What does not go returns true without rand() or send.
TEST_F(JewelTest, EachJewelGoesOnItsTypesAndLevels)
{
    struct Case
    {
        std::uint16_t jewel;
        std::uint16_t type;
        std::uint32_t level;
        bool sends;
    };
    for (const Case& tried :
         {Case{JewelOfBless, Sword, 5, true}, Case{JewelOfBless, Sword, 6, false}, Case{JewelOfSoul, Sword, 8, true},
          Case{JewelOfSoul, Sword, 9, false}, Case{JewelOfLife, Sword, 15, true}, Case{JewelOfLife, Wings, 0, true},
          Case{JewelOfLife, Item1a1e, 0, true}, Case{JewelOfLife, Bolts, 0, false}, Case{JewelOfSoul, Potion, 0, false},
          Case{JewelOfBless, Item1a0b, 0, false}})
    {
        SCOPED_TRACE(tried.type);
        SCOPED_TRACE(tried.jewel);
        world.Inventory().ClearForInventoryReply();
        Put(tried.type, 4, 4, tried.level << 3);
        const CrtRandom expected = Advanced(random, tried.sends ? 1 + SendRandoms : 0);
        const auto dropped = Drop(tried.jewel, 4, 4);
        ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
        EXPECT_TRUE(dropped.Value());
        EXPECT_EQ(random.State(), expected.State());
        EXPECT_EQ(sent.size(), tried.sends ? 1u : 0u);
    }
}

// 0x5d8324..0x5d8372: the bless on the 0x1a25 without durability 0xff keeps the jewel's cell and the target (0x7d3a0dc
// and 0x7d3a0e0) and opens dialog 0xa9 with text 1865, without a send, even at level 6 (Q-C1246); with 0xff, like any
// type above 0x1806, nothing.
TEST_F(JewelTest, TheBlessOnThe1a25OpensItsDialog)
{
    Put(Item1a25, 6, 6, 6u << 3);
    const auto dropped = Drop(JewelOfBless, 6, 6);
    ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
    EXPECT_TRUE(dropped.Value());
    EXPECT_TRUE(sent.empty());
    EXPECT_EQ(world.Runtime().dialogs.current, 0xa9);
    EXPECT_EQ(Client::Ui::DialogRow(world.Runtime().dialogBox, 0), "joya sobre el cuerno");
    EXPECT_EQ(world.Runtime().dialogBox.jewelCell, JewelCell);
    EXPECT_EQ(world.Runtime().dialogBox.jewelTarget, 6 * 8 + 6 + 12);

    world.Runtime().dialogs.current = 0;
    Cell(6, 6).durability = 0xff;
    const auto full = Drop(JewelOfBless, 6, 6);
    ASSERT_TRUE(full.HasValue()) << full.Error();
    EXPECT_TRUE(full.Value());
    EXPECT_EQ(world.Runtime().dialogs.current, 0);
    EXPECT_TRUE(sent.empty());
}

// The inline queries (0x5d838c..0x5d83d7): with the warehouse open, the target stays at 0x77f3cc8 and it warns with
// the 474. The jewel of creation, or the closed inventory, do not enter: it returns false.
TEST_F(JewelTest, TheWarehouseWarnsAndCreationOrAClosedInventoryDoNotEnter)
{
    Put(Sword, 0, 0, 0);
    world.Runtime().panels.warehouse = 1;
    const auto warned = Drop(JewelOfLife, 0, 1);
    ASSERT_TRUE(warned.HasValue()) << warned.Error();
    EXPECT_TRUE(warned.Value());
    EXPECT_EQ(messages, std::vector<std::size_t>{474});
    EXPECT_EQ(world.Runtime().itemMoveTarget, 12);
    EXPECT_TRUE(sent.empty());

    world.Runtime().panels.warehouse = 0;
    for (const auto& [jewel, open] : {std::pair{JewelOfCreation, true}, std::pair{JewelOfBless, false}})
    {
        const auto dropped = Drop(jewel, 0, 1, open);
        ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
        EXPECT_FALSE(dropped.Value());
        EXPECT_TRUE(sent.empty());
    }
}
} // namespace
