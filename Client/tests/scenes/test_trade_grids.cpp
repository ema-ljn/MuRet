#include "Network/Xor32Packet.h"
#include "Kernel/MouseState.h"
#include "Protocol/FrontEnd/CharacterName.h"
#include "Hud/GameUi.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Tables/ItemSetTables.h"
#include "Tables/ItemTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Tables/TextTable.h"
#include "Input/InputState.h"
#include "Network/Delivery.h"
#include "Protocol/Items/Inventory.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Actors/Viewport.h"
#include "Models/Cloth.h"
#include "Kernel/ActorState.h"
#include "Hud/GamePanels.h"
#include "Items/InventoryMoves.h"
#include "FrontEnd/LoginModels.h"
#include "Kernel/OwnCharacterRecord.h"
#include "FrontEnd/SelectionState.h"
#include "Social/SocialEvents.h"
#include "Social/SocialRequests.h"
#include "Social/SocialState.h"
#include "Session/ModuleStates.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameUiState.h"
#include "Ui/PanelRegistry.h"
#include "Camera/GameCamera.h"
#include "Kernel/ItemState.h"
#include "Terrain/WorldTerrain.h"
#include "Audio/SoundId.h"

#include "support/ChatTexts.h"
#include "support/GoldenDir.h"
#include "support/ItemFiles.h"
#include "support/ItemSetFiles.h"
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "support/SceneDataFiles.h"
#include "support/ViewportHandlers.h"
#include "support/ViewportPackets.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The trade grids (CL9 milestone 7, 08 P-C205 a) with the other one that the 0x12 brings next to the hero, in Lorencia
// (0) and Devias (2): picking up from the inventory and dropping in the own grid (0x5c4110, 0x5deb1c → 0x5ca250 with
// 0x78a92a8: the 3C and the 24 to grid 1), the answer (0x4d1ce0), picking up from the own grid (0x5c52b1 and the 3C
// with the OK set) and dropping in the inventory (0x5ce4fe: the 24 from grid 1 and the OK wait), against the ASM. The
// names and the items are made up.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Kernel::EmptyOwnItemId;
using Client::Kernel::ItemState;
using Client::Tables::PlayerSkeletonModel;
using Client::Tests::ViewportPlayerRecord;

constexpr std::uint32_t Seed = 0x7ade;
constexpr std::uint16_t HeroKey = 33;
constexpr std::uint16_t OtherKey = 0x0456;
constexpr std::size_t Chosen = 2;
// A 1 × 3 staff (0x0a05, level 2) and a 1 × 3 sword that the other one puts.
constexpr std::uint16_t StaffType = 0x0a05;
constexpr std::uint16_t SwordType = 0x0001;
// The inventory window at (0x1c2, 0) with its grid at x + 15 and y + 200; the trade window at (0x104, 0), which its
// drawing leaves (0x5f3b40), with the own grid at x + 15 and y + 0x10e.
constexpr std::int32_t InventoryX = 0x1c2;
constexpr std::int32_t InventoryGridLeft = InventoryX + 15;
constexpr std::int32_t InventoryGridTop = 200;
constexpr std::int32_t TradeX = 0x104;
constexpr std::int32_t TradeGridLeft = TradeX + 15;
constexpr std::int32_t TradeGridTop = 0x10e;
constexpr std::int32_t Cell = 20;
// The staff in cell (1, 2) of the inventory: slot 12 + 17.
constexpr std::size_t StaffCell = 2 * 8 + 1;
// The step with the inventory and the trade open and the own record open (without the 3848 of its closes): the 12 of
// test_game_inventory_step.cpp with the window alone (the skill bar, the Q, W and E chain and 0x5e4ce0) minus 3,
// because the open 0x20 cuts three searches on its first query: the one for any panel (0x5a5511, from 0x1 upwards, no
// longer reaches 0x10000), the Q, W and E one (0x5a343e) and the footer buttons' one (0x5e545f). Measured: the trade
// grid and its window draw no more.
constexpr int QuietStepRandoms = 12 - 3;
// IsOpen(0x20) of 0x5c5361 after picking up and the one of the cursor item's step (0x5deb11).
constexpr int TradeQueryRandoms = 1;
// The padding and the serial of a C3.
constexpr int C3Randoms = 2;

std::uint8_t Byte(int value)
{
    return static_cast<std::uint8_t>(value);
}

Client::Protocol::FrontEnd::NameBytes NameOf(std::string_view text)
{
    Client::Protocol::FrontEnd::NameBytes name{};
    for (std::size_t at = 0; at < name.size(); ++at)
        name[at] = at < text.size() ? static_cast<std::byte>(text[at]) : std::byte{0};
    return name;
}

// The seven bytes of an item of the 24 and the 39 (0x5bc740): the type in byte 0, the level in byte 1, the durability,
// bit 8 of the type in the 0x80 of byte 3 and the upper ones in byte 5.
Client::Protocol::Items::ItemData ItemBytes(std::uint16_t type, std::uint8_t level)
{
    return {static_cast<std::byte>(type & 0xff),
            static_cast<std::byte>(level << 3),
            std::byte{30},
            static_cast<std::byte>((type >> 1) & 0x80),
            std::byte{0},
            static_cast<std::byte>((type >> 5) & 0xf0),
            std::byte{0}};
}

CrtRandom Advanced(CrtRandom random, int count)
{
    random.Discard(static_cast<std::uint32_t>(count));
    return random;
}

struct Sent
{
    bool encrypted = false;
    std::vector<std::uint8_t> bytes;
};

Sent PlainOf(const Client::Protocol::Outgoing& outgoing)
{
    const auto plain = Client::Network::Xor32Decode(outgoing.packet);
    Sent sent{.encrypted = outgoing.delivery == Client::Network::Delivery::Encrypted, .bytes = {}};
    for (const std::byte b : plain.Bytes())
        sent.bytes.push_back(std::to_integer<std::uint8_t>(b));
    return sent;
}

// The game's world, on the heap.
struct Game
{
    Game()
    {
        models.Set(PlayerSkeletonModel, {}, Client::Tests::PlayerActionsSkeleton(), {});
        ActorState& hero = world.Actors()[0];
        hero = Client::Tests::UnarmedPlayer();
        hero.alive = true;
        hero.key = HeroKey;
        hero.byte88 = 1;
        world.Runtime().hero = 0;
        world.Runtime().drawingEnabled = true;
        world.OwnCharacter().ClearEquipmentForEntry();
        world.Inventory().ClearForInventoryReply();
        Client::Social::ClearTheTradeForSelection(world.Runtime().social.trade);
    }
    Client::Scenes::WorldEntryResources Resources()
    {
        return {.world = world,
                .selection = selection,
                .dataRoot = {},
                .cloth = cloth,
                .camera = camera,
                .cameraInput = input,
                .models = models,
                .items = &items,
                .itemSetTables = &sets,
                .log = log,
                .texts = &texts,
                .states = &states};
    }
    Client::Kernel::WorldState world;
    Client::Session::ModuleStates states;
    Client::FrontEnd::SelectionState selection{world};
    Client::Camera::GameCamera camera;
    Client::Camera::GameCameraInput input;
    Client::Models::ClothWorld cloth{.random = CrtRandom(Seed)};
    Client::FrontEnd::SingleModelLibrary models;
    Client::Tables::ItemTable items =
        Client::Tests::ItemTableOf({{StaffType, 1, 3, 0}, {SwordType, 1, 3, 0}, {0x1c00, 1, 1, 0xff}});
    Client::Tables::ItemSetTables sets =
        Client::Tests::ItemSetTablesFrom(std::vector<std::byte>(Client::Tables::ItemSetTypeTableSize),
                                         std::vector<std::byte>(Client::Tables::ItemSetOptionTableSize));
    Client::Tables::TextTable texts = Client::Tests::MakeTextTable(Client::Tests::ChatTexts());
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
};

class TradeGridsTest : public Client::Tests::GoldenDataTest, public ::testing::WithParamInterface<std::uint8_t>
{
protected:
    void SetUp() override
    {
        GoldenDataTest::SetUp();
        game = std::make_unique<Game>();
        const std::uint8_t map = GetParam();
        auto loaded = Client::Terrain::LoadWorldTerrain(m_dataRoot, map);
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        game->states.terrain.current = std::move(loaded).Value();
        game->world.Runtime().map = map;
        TheOtherArrives();
        TradeWithTheOther();
        services.items = &game->items;
        services.dressTheHero = [] { return Client::Core::Expected<void, std::string>{}; };
        services.sendItemMove = [this](const Client::Protocol::Items::ItemMove& move)
        {
            sent.push_back(PlainOf(Client::Protocol::Items::WriteItemMove(move)));
            return Client::Core::Expected<void, std::string>{};
        };
        services.sendGamePacket = [this](Client::Protocol::Outgoing packet) { sent.push_back(PlainOf(packet)); };
        // The hero's staff, level 2, in cell (1, 2) of the inventory.
        Client::Kernel::WorldRuntime& runtime = game->world.Runtime();
        ASSERT_TRUE(
            Client::Items::PlaceMovedItem(Byte(12 + StaffCell), ItemBytes(StaffType, 2), game->Resources()).HasValue());
        ASSERT_EQ(game->world.Inventory().cells[StaffCell].id, StaffType);
        // The corners the drawings leave (0x5f3b40 and 0x5f0c30) and the own record open around everything.
        runtime.hud.inventoryX = InventoryX;
        runtime.hud.inventoryY = 0;
        runtime.hud.tradeX = TradeX;
        runtime.hud.tradeY = 0;
        // The chat log's corner outside its window, as the previous step leaves it (0x636f04): the initial one (0xba,
        // 0x1a4) covers the own grid and its step (0x636e51, before) takes the mouse.
        runtime.hud.chatFrame.x = -10;
        runtime.hud.chatFrame.y = 0x65;
        runtime.ownRecordDepth = 1;
    }

    // The first free row of two cells from (100, 100).
    [[nodiscard]] std::array<std::uint8_t, 2> FreeRow() const
    {
        const auto& cells = game->states.terrain.current.attributes->cells;
        for (int y = 100; y < 200; ++y)
            for (int x = 100; x < 200; ++x)
                if (cells[Client::Data::Terrain::CellIndex(Byte(x), Byte(y))] == 0 &&
                    cells[Client::Data::Terrain::CellIndex(Byte(x + 1), Byte(y))] == 0)
                    return {Byte(x), Byte(y)};
        ADD_FAILURE() << "el mapa no tiene una fila libre";
        return {};
    }

    // The hero in the first cell and the 0x12 with Brezal in the one next to it.
    void TheOtherArrives()
    {
        const auto [x, y] = FreeRow();
        ActorState& hero = game->world.Actors()[0];
        hero.cellX = x;
        hero.cellY = y;
        const std::array<ViewportPlayerRecord, 1> records{{{.key = OtherKey,
                                                            .isNew = true,
                                                            .x = Byte(x + 1),
                                                            .y = y,
                                                            .name = "Brezal",
                                                            .targetX = Byte(x + 1),
                                                            .targetY = y}}};
        auto read = Client::Protocol::Actors::ReadPlayers(Client::Tests::ViewportPlayersPacket(records));
        ASSERT_TRUE(read.HasValue());
        const auto applied = Client::Tests::ApplyViewportPlayers(read.Value(), game->Resources());
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        ASSERT_EQ(game->world.Actors()[1].key, OtherKey);
    }

    // /trade to the chosen one and the 37 with the yes, which opens the trade with the inventory.
    void TradeWithTheOther()
    {
        Client::Kernel::WorldRuntime& runtime = game->world.Runtime();
        runtime.selectedTargets[Chosen] = 1;
        std::vector<Client::Protocol::Outgoing> asked;
        const auto requested = Client::Social::RequestATrade(
            {.world = game->world,
             .random = game->cloth.random,
             .texts = game->texts,
             .send = [&asked](Client::Protocol::Outgoing packet) { asked.push_back(std::move(packet)); }});
        ASSERT_TRUE(requested.HasValue()) << requested.Error();
        ASSERT_EQ(asked.size(), 1U);
        const auto opened = Client::Social::ApplyTradeResponse({1, NameOf("Brezal"), 150, 0}, game->Resources());
        ASSERT_TRUE(opened.HasValue()) << opened.Error();
        ASSERT_EQ(runtime.panels.trade, 1);
        ASSERT_EQ(runtime.panels.inventory, 1);
    }

    Client::Core::Expected<void, std::string> Step(std::int32_t x, std::int32_t y, bool click)
    {
        if (click)
            Client::Kernel::LatchTheLeftButton(game->world.Runtime().mouse, true, true);
        return Client::Hud::StepTheGameUi(Client::Ui::GameUiContext{.world = game->world,
                                                                    .random = game->cloth.random,
                                                                    .input = input,
                                                                    .mouseX = x,
                                                                    .mouseY = y,
                                                                    .inventory = &services});
    }

    CrtRandom& Random()
    {
        return game->cloth.random;
    }

    std::unique_ptr<Game> game;
    Client::Input::InputState input;
    Client::Items::InventoryServices services;
    std::vector<Sent> sent;
};

// Picking it up from the inventory: the mark, the cursor from slot 29 and the two trade queries. Dropping it on cell
// (2, 0) of the own grid: the OK at 0 and the C1 04 3C 00, then the 24 from the inventory (0, 29) to grid 1 at slot 2,
// each with its C3. The answer (C3 0C 24 with 1) puts it in cells 2, 10 and 18 with column 2 and row 0, and the sound
// 0x1d. With the OK set again, picking it up from cell 10 picks it up from cell 2 (slot 2, without the 12), empties its
// footprint and sends the 3C with the OK at 0. Dropping it on cell (3, 1) of the inventory sends the 24 from grid 1
// (2) to the inventory (0, 23) and, without the OK, sets the OK wait at 0x96. The answer (with 0) returns it to the
// inventory.
TEST_P(TradeGridsTest, PutAnItemInTheTradeAndTakeItOut)
{
    Client::Kernel::WorldRuntime& runtime = game->world.Runtime();
    Client::Social::TradeState& trade = runtime.social.trade;
    auto& inventory = game->world.Inventory().cells;

    // Picking up the staff from the inventory.
    CrtRandom expected = Advanced(Random(), QuietStepRandoms + 2 * TradeQueryRandoms);
    auto stepped = Step(InventoryGridLeft + Cell + 5, InventoryGridTop + 2 * Cell + 5, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Random().State(), expected.State());
    EXPECT_EQ(runtime.cursorItemCount, 1);
    EXPECT_EQ(runtime.cursorItem.id, StaffType);
    EXPECT_EQ(runtime.cursorGrid, Client::Kernel::CursorGrid::Inventory);
    EXPECT_EQ(runtime.slot78a6758, 12 + static_cast<std::int32_t>(StaffCell));
    EXPECT_EQ(inventory[StaffCell].id, EmptyOwnItemId);
    EXPECT_TRUE(sent.empty());

    // Without the click over the own grid: the footprint marks, 2 if it fits.
    const std::int32_t dropX = TradeGridLeft + 2 * Cell + 5;
    const std::int32_t dropY = TradeGridTop + Cell + 5;
    expected = Advanced(Random(), QuietStepRandoms + TradeQueryRandoms);
    stepped = Step(dropX, dropY, false);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Random().State(), expected.State());
    EXPECT_EQ(trade.ownGrid[2].warning, 2);
    EXPECT_EQ(trade.ownGrid[10].warning, 2);
    EXPECT_EQ(trade.ownGrid[18].warning, 2);
    EXPECT_TRUE(sent.empty());

    // Dropping it: the 3C and the 24, each with the two rand() of the C3.
    trade.ownOk = 1;
    expected = Advanced(Random(), QuietStepRandoms + TradeQueryRandoms + 2 * C3Randoms);
    stepped = Step(dropX, dropY, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Random().State(), expected.State());
    ASSERT_EQ(sent.size(), 2U);
    EXPECT_EQ(sent[0].bytes, (std::vector<std::uint8_t>{0xc1, 0x04, 0x3c, 0x00}));
    EXPECT_TRUE(sent[0].encrypted);
    EXPECT_EQ(sent[1].bytes, (std::vector<std::uint8_t>{0xc1, 0x0e, 0x24, 0x00, 29, 0x05, 2 << 3, 30, 0x00, 0x00, 0x50,
                                                        0x00, 0x01, 0x02}));
    EXPECT_TRUE(sent[1].encrypted);
    EXPECT_EQ(trade.ownOk, 0);
    EXPECT_EQ(runtime.ui.field78abb39, 1);
    EXPECT_EQ(runtime.itemMoveTarget, 2);
    EXPECT_FALSE(runtime.mouse.pendingClick);

    // The answer: 0x5bc740 in the own grid, without rand() with the record open.
    expected = Random();
    auto applied = Client::Items::ApplyItemMoveResult({1, 2, ItemBytes(StaffType, 2)}, game->Resources());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(Random().State(), expected.State());
    EXPECT_EQ(runtime.ui.field78abb39, 0);
    EXPECT_EQ(runtime.cursorItemCount, 0);
    EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{0x1d});
    for (const std::size_t cell : {2U, 10U, 18U})
    {
        SCOPED_TRACE(cell);
        EXPECT_EQ(trade.ownGrid[cell].id, StaffType);
        EXPECT_EQ(trade.ownGrid[cell].gridColumn, 2);
        EXPECT_EQ(trade.ownGrid[cell].gridRow, 0);
    }
    EXPECT_EQ(trade.ownGrid[2].occupied, 1U);
    EXPECT_EQ(trade.ownGrid[10].occupied, 0U);

    // Without the click over cell 10: the footprint's mark, the banner's item and 0x5aed40 with 2 and the cell.
    sent.clear();
    const std::int32_t takeX = TradeGridLeft + 2 * Cell + 5;
    const std::int32_t takeY = TradeGridTop + Cell + 5;
    expected = Advanced(Random(), QuietStepRandoms);
    stepped = Step(takeX, takeY, false);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Random().State(), expected.State());
    EXPECT_EQ(runtime.ui.hoveredItem.place, Client::Ui::HoveredItem::Place::OwnTradeCell);
    EXPECT_EQ(runtime.ui.hoveredItem.index, 10U);
    EXPECT_EQ(runtime.ui.field78abb28, 2);
    EXPECT_EQ(runtime.ui.field78abb2c, 10);
    EXPECT_EQ(runtime.ui.skillTooltipX, TradeGridLeft + 10 + 2 * Cell);
    EXPECT_EQ(runtime.ui.skillTooltipY, TradeGridTop);
    EXPECT_EQ(trade.ownGrid[18].warning, 2);

    // Picking it up with the OK set: the 3C with the OK at 0 after IsOpen(0x20).
    trade.ownOk = 1;
    expected = Advanced(Random(), QuietStepRandoms + 2 * TradeQueryRandoms + C3Randoms);
    stepped = Step(takeX, takeY, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Random().State(), expected.State());
    ASSERT_EQ(sent.size(), 1U);
    EXPECT_EQ(sent[0].bytes, (std::vector<std::uint8_t>{0xc1, 0x04, 0x3c, 0x00}));
    EXPECT_EQ(trade.ownOk, 0);
    EXPECT_EQ(runtime.cursorItemCount, 1);
    EXPECT_EQ(runtime.cursorGrid, Client::Kernel::CursorGrid::OwnTrade);
    EXPECT_EQ(runtime.slot78a6758, 2);
    for (const std::size_t cell : {2U, 10U, 18U})
        EXPECT_EQ(trade.ownGrid[cell].id, EmptyOwnItemId);
    EXPECT_EQ(runtime.ui.hoveredItem.place, Client::Ui::HoveredItem::Place::None);
    EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{0x1d});

    // Dropping it on cell (3, 1) of the inventory: the 24 from grid 1 and the OK wait.
    sent.clear();
    expected = Advanced(Random(), QuietStepRandoms + TradeQueryRandoms + C3Randoms);
    stepped = Step(InventoryGridLeft + 3 * Cell + 5, InventoryGridTop + 2 * Cell + 5, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Random().State(), expected.State());
    ASSERT_EQ(sent.size(), 1U);
    EXPECT_EQ(sent[0].bytes, (std::vector<std::uint8_t>{0xc1, 0x0e, 0x24, 0x01, 0x02, 0x05, 2 << 3, 30, 0x00, 0x00,
                                                        0x50, 0x00, 0x00, 12 + 8 + 3}));
    EXPECT_EQ(trade.okWait, Client::Social::TradeOkWait);
    EXPECT_EQ(runtime.itemMoveTarget, 12 + 8 + 3);

    // The answer, in the inventory.
    applied = Client::Items::ApplyItemMoveResult({0, 12 + 8 + 3, ItemBytes(StaffType, 2)}, game->Resources());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(inventory[8 + 3].id, StaffType);
    EXPECT_EQ(inventory[16 + 3].id, StaffType);
    EXPECT_EQ(inventory[24 + 3].id, StaffType);
    EXPECT_EQ(runtime.cursorItemCount, 0);
}

// The other's grid (0x78a5a20) is only looked at: the mark without overwriting the 99, the banner's item and 0x5aed40
// with 3 and the cell; the click does not pick it up and goes on to the own grid.
TEST_P(TradeGridsTest, TheOtherGridIsOnlyLookedAt)
{
    Client::Kernel::WorldRuntime& runtime = game->world.Runtime();
    Client::Social::TradeState& trade = runtime.social.trade;
    ASSERT_TRUE(Client::Social::ApplyTradeItemAdded({9, ItemBytes(SwordType, 0)}, game->Resources()).HasValue());
    trade.otherGrid[17].warning = Client::Social::ChangedFromTheCopy;
    const CrtRandom expected = Advanced(Random(), QuietStepRandoms);
    const auto stepped = Step(TradeGridLeft + Cell + 5, 0x46 + 2 * Cell + 5, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Random().State(), expected.State());
    EXPECT_EQ(trade.otherGrid[9].warning, 2);
    EXPECT_EQ(trade.otherGrid[17].warning, Client::Social::ChangedFromTheCopy);
    EXPECT_EQ(trade.otherGrid[25].warning, 2);
    EXPECT_EQ(runtime.ui.hoveredItem.place, Client::Ui::HoveredItem::Place::OtherTradeCell);
    EXPECT_EQ(runtime.ui.hoveredItem.index, 17U);
    EXPECT_EQ(runtime.ui.field78abb28, 3);
    EXPECT_EQ(runtime.ui.field78abb2c, 17);
    EXPECT_EQ(runtime.cursorItemCount, 0);
    EXPECT_EQ(trade.otherGrid[9].id, SwordType);
    EXPECT_TRUE(sent.empty());
}

// The right click on the own grid (0x5c4aa5 → 0x5c5895 with 0x78a92a8, 08 P-C235): the inventory branch with this
// grid's cell. The potion reaches UseInventoryItem, which with the trade open (its rand()) only gives the warning 474
// of type 2, without the 26. The sword (of the others, 0x5c8e37) goes to the cursor with the trade grid, without
// 0x78abb28 (0x5c8ef8) and, without the warehouse, the cursor ends up at zeros again.
TEST_P(TradeGridsTest, TheRightClickOnTheOwnGridIsTheInventoryBranch)
{
    Client::Kernel::WorldRuntime& runtime = game->world.Runtime();
    Client::Social::TradeState& trade = runtime.social.trade;
    std::vector<std::pair<std::size_t, std::int32_t>> notices;
    services.systemMessage = [&notices](std::size_t index, std::int32_t type)
    {
        notices.emplace_back(index, type);
        return Client::Core::Expected<void, std::string>{};
    };
    ASSERT_TRUE(Client::Items::ApplyItemMoveResult({1, 4, ItemBytes(SwordType, 0)}, game->Resources()).HasValue());
    ASSERT_TRUE(Client::Items::ApplyItemMoveResult({1, 6, ItemBytes(0x1c00, 0)}, game->Resources()).HasValue());
    ASSERT_EQ(trade.ownGrid[6].id, 0x1c00);
    // The left button released (WM_LBUTTONUP): the earlier click no longer stays.
    Client::Kernel::LatchTheLeftButton(runtime.mouse, false, false);

    runtime.ui.rightClick = true;
    CrtRandom expected = Advanced(Random(), QuietStepRandoms + TradeQueryRandoms);
    auto stepped = Step(TradeGridLeft + 6 * Cell + 5, TradeGridTop + 5, false);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Random().State(), expected.State());
    EXPECT_FALSE(runtime.ui.rightClick);
    ASSERT_EQ(notices.size(), 1U);
    EXPECT_EQ(notices[0], (std::pair<std::size_t, std::int32_t>{474, 2}));
    EXPECT_TRUE(sent.empty());
    EXPECT_EQ(trade.ownGrid[6].id, 0x1c00);

    // The sword, from the bottom cell (12): the top left one is 4.
    runtime.ui.rightClick = true;
    runtime.ui.field78abb28 = 0;
    expected = Advanced(Random(), QuietStepRandoms);
    stepped = Step(TradeGridLeft + 4 * Cell + 5, TradeGridTop + Cell + 5, false);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(Random().State(), expected.State());
    EXPECT_EQ(runtime.cursorGrid, Client::Kernel::CursorGrid::OwnTrade);
    EXPECT_EQ(runtime.ui.field78abb28, 0);
    EXPECT_EQ(runtime.cursorItem.id, 0);
    EXPECT_EQ(runtime.cursorItemCount, 0);
    EXPECT_EQ(trade.ownGrid[4].id, SwordType);
    EXPECT_EQ(notices.size(), 1U);
    EXPECT_TRUE(sent.empty());
}

// What does not go to the trade grid (0x5cf3c9..0x5cf40b) stays in the cursor without sending anything, and the click
// stays in the grid. What does not fit is marked with 3.
TEST_P(TradeGridsTest, WhatIsNotTradedStaysInTheCursor)
{
    Client::Kernel::WorldRuntime& runtime = game->world.Runtime();
    runtime.cursorItem = ItemState{};
    runtime.cursorItem.id = 0x1a14;
    runtime.cursorItemCount = 1;
    runtime.cursorGrid = Client::Kernel::CursorGrid::Inventory;
    game->items = Client::Tests::ItemTableOf({{0x1a14, 1, 1, 0}, {StaffType, 1, 3, 0}});
    const auto stepped = Step(TradeGridLeft + 5, TradeGridTop + 5, true);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_TRUE(sent.empty());
    EXPECT_EQ(runtime.cursorItemCount, 1);
    EXPECT_EQ(runtime.ui.field78abb39, 0);
    EXPECT_FALSE(runtime.mouse.pendingClick);

    // A staff that does not fit from row 2 (3 high): mark 3 in the grid's two cells.
    runtime.cursorItem.id = StaffType;
    const auto low = Step(TradeGridLeft + 5, TradeGridTop + 3 * Cell + 5, false);
    ASSERT_TRUE(low.HasValue()) << low.Error();
    EXPECT_TRUE(sent.empty());
    EXPECT_EQ(runtime.social.trade.ownGrid[2 * 8].warning, 3);
    EXPECT_EQ(runtime.social.trade.ownGrid[3 * 8].warning, 3);
    EXPECT_EQ(runtime.social.trade.ownGrid[1 * 8].warning, 0);
}

INSTANTIATE_TEST_SUITE_P(Maps, TradeGridsTest, ::testing::Values(std::uint8_t{0}, std::uint8_t{2}));

// 0x4d1b76..0x4d1c7d: the price of the origin slot moves to the new one in the map of the shop being looked at; to a
// slot outside the shop it is lost. 0x4d1d70: the 3D that arrived with the lock repeats at the end (1, done: it closes
// the trade).
class ItemMoveReplyTest : public ::testing::TestWithParam<std::int32_t>
{
protected:
    ItemMoveReplyTest()
    {
        game->world.Runtime().map = GetParam();
        game->world.Runtime().ownRecordDepth = 1;
    }
    std::unique_ptr<Game> game = std::make_unique<Game>();
};

TEST_P(ItemMoveReplyTest, ThePriceFollowsTheItem)
{
    Client::Kernel::WorldRuntime& runtime = game->world.Runtime();
    Client::Social::PersonalShopState& shops = runtime.social.personalShop;
    shops.pricesBuilt = true;
    shops.mode = Client::Social::OwnShopView;
    shops.ownPrices = {{0x4c, 500}, {0x50, 700}};
    runtime.slot78a6758 = 0x4c;
    ASSERT_TRUE(Client::Items::ApplyItemMoveResult({0, 0x4d, ItemBytes(0x1c00, 0)}, game->Resources()).HasValue());
    EXPECT_EQ(shops.ownPrices, (std::map<std::int32_t, std::int32_t>{{0x4d, 500}, {0x50, 700}}));
    runtime.slot78a6758 = 0x4d;
    ASSERT_TRUE(Client::Items::ApplyItemMoveResult({0, 0x20, ItemBytes(0x1c00, 0)}, game->Resources()).HasValue());
    EXPECT_EQ(shops.ownPrices, (std::map<std::int32_t, std::int32_t>{{0x50, 700}}));
    // Looking at another shop, that one's; without the view, none.
    shops.mode = Client::Social::OtherShopView;
    shops.otherPrices = {{0x50, 900}};
    runtime.slot78a6758 = 0x50;
    ASSERT_TRUE(Client::Items::ApplyItemMoveResult({0, 0x51, ItemBytes(0x1c00, 0)}, game->Resources()).HasValue());
    EXPECT_EQ(shops.otherPrices, (std::map<std::int32_t, std::int32_t>{{0x51, 900}}));
    EXPECT_EQ(shops.ownPrices, (std::map<std::int32_t, std::int32_t>{{0x50, 700}}));
    shops.mode = Client::Social::NoShopView;
    runtime.slot78a6758 = 0x51;
    ASSERT_TRUE(Client::Items::ApplyItemMoveResult({0, 0x52, ItemBytes(0x1c00, 0)}, game->Resources()).HasValue());
    EXPECT_EQ(shops.otherPrices, (std::map<std::int32_t, std::int32_t>{{0x51, 900}}));
}

TEST_P(ItemMoveReplyTest, TheDeferredTradeResultIsReplayed)
{
    Client::Kernel::WorldRuntime& runtime = game->world.Runtime();
    Client::Hud::GamePanels panels(game->world, game->cloth.random);
    ASSERT_TRUE(panels.Registry().Open(Client::Ui::TradePanel).HasValue());
    runtime.ui.field78abb39 = 1;
    ASSERT_TRUE(Client::Social::ApplyTradeResult({1, {0xc1, 0x04, 0x3d, 0x01}}, game->Resources()).HasValue());
    EXPECT_EQ(runtime.social.trade.resultDeferred, 1);
    EXPECT_EQ(runtime.panels.trade, 1);
    ASSERT_TRUE(Client::Items::ApplyItemMoveResult({0xff, 0, {}}, game->Resources()).HasValue());
    EXPECT_EQ(runtime.social.trade.resultDeferred, 0);
    EXPECT_EQ(runtime.panels.trade, 0);
    EXPECT_EQ(runtime.ui.field78abb39, 0);
}

INSTANTIATE_TEST_SUITE_P(Maps, ItemMoveReplyTest, ::testing::Values(0, 2));
} // namespace
