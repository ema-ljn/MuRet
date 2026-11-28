#include "Network/Xor32Packet.h"
#include "Npc/NpcWindows.h"

#include "Audio/Playback.h"
#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Tables/ItemSetTables.h"
#include "Tables/ItemTable.h"
#include "Tables/TextTable.h"
#include "Network/Delivery.h"
#include "Network/Packet.h"
#include "Protocol/Npc/NpcWindows.h"
#include "Protocol/Outgoing.h"
#include "Models/Cloth.h"
#include "Models/LogicalAccess.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Hud/GamePanels.h"
#include "Items/InventoryMoves.h"
#include "FrontEnd/LoginModels.h"
#include "Npc/NpcWindowState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/OwnInventory.h"
#include "FrontEnd/SelectionState.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/Dialogs.h"
#include "Ui/PanelRegistry.h"
#include "Camera/GameCamera.h"
#include "Kernel/ItemState.h"
#include "Audio/SoundId.h"
#include "Sound/SoundRequests.h"

#include "support/Bytes.h"
#include "support/ItemFiles.h"
#include "support/ItemSetFiles.h"
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "support/SameBytes.h"
#include "support/SceneDataFiles.h"
#include "Chat/ChatRuntime.h"
#include "Ui/GameUiState.h"
#include "CastleSiege/CastleSiegeState.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The NPC windows on the scene side (CL9 milestone 6; 04 § Las ventanas de los NPC), without NPC actors: the 30 sets
// the open state as 0x4d1e00 does, and the outgoing packets are checked byte by byte. The items and texts are made
// up.
namespace
{
using Client::Crt::CrtRandom;
using Client::Network::Delivery;
using Client::Network::Xor32Decode;
using Client::Npc::NpcWindowState;
using Client::Scenes::WorldEntryResources;
using Client::Tests::Bytes;
using Client::Tests::SameBytes;
namespace Npc = Client::Protocol::Npc;

class NpcPlayback final : public Client::Audio::IPlayback
{
public:
    void PlaySound(std::int32_t id, std::optional<glm::vec3> origin, bool loop) override
    {
        EXPECT_FALSE(origin.has_value());
        EXPECT_FALSE(loop);
        sounds.push_back(id);
    }
    void StopSound(std::int32_t, bool) override {}
    void PlayMusic(std::string_view, bool) override {}
    void StopMusic() override {}
    void SetEffectsVolume(std::uint8_t) override {}
    void SetListener(glm::vec3) override {}

    std::vector<std::int32_t> sounds;
};

constexpr std::uint32_t Seed = 0x1357;
constexpr int Own = Client::Models::OwnRecordCloseRandoms;
constexpr std::size_t HeroSlot = 7;
// A 1 by 3 weapon, a 2 by 2 shield and a 1 by 1 jewel (synthetic Item.bmd).
constexpr std::uint16_t Sword = 0x0001;
constexpr std::uint16_t Shield = 0x0c00;
constexpr std::uint16_t Jewel = 0x1c0d;
constexpr std::size_t ChaosBoxNotEmptyText = 593;
constexpr std::size_t MixSucceededText = 595;
constexpr std::size_t TrainerListText = 1208;

class LetterMeasure final : public Client::Render::Text::ITextMeasure
{
public:
    [[nodiscard]] Client::Render::Text::TextExtent Measure(Client::Render::Text::FontKind,
                                                           std::string_view text) const override
    {
        return {static_cast<int>(text.size()) * 6, 13};
    }
};

// The seven bytes of an item for `type`, as the GameServer builds them (0x54b3a0 reversed).
Npc::ItemData ItemBytes(std::uint16_t type, std::uint8_t durability = 20)
{
    return {static_cast<std::byte>(type & 0xff),
            std::byte{0},
            std::byte{durability},
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

struct Stage
{
    Stage()
    {
        models.Set(Client::Tables::PlayerSkeletonModel, {}, Client::Tests::PlayerActionsSkeleton(), {});
        world.OwnCharacter().ClearEquipmentForEntry();
        world.Inventory().ClearForInventoryReply();
        world.Runtime().hero = HeroSlot;
        world.Actors()[HeroSlot] = Client::Tests::UnarmedPlayer();
        world.Actors()[HeroSlot].alive = true;
    }

    [[nodiscard]] WorldEntryResources Resources()
    {
        WorldEntryResources resources{.world = world,
                                      .selection = selection,
                                      .dataRoot = std::filesystem::path{},
                                      .cloth = cloth,
                                      .camera = camera,
                                      .cameraInput = cameraInput,
                                      .models = models,
                                      .items = &items,
                                      .itemSetTables = &sets,
                                      .log = log};
        resources.textMeasure = &measure;
        resources.texts = &texts;
        resources.sendNpcWindowPacket = [this](Client::Protocol::Outgoing packet)
        {
            sent.push_back(std::move(packet));
            return Client::Core::Expected<void, std::string>{};
        };
        return resources;
    }

    // The C1 of packet `index` without the XOR32, to compare it byte by byte.
    [[nodiscard]] Client::Network::Packet Plain(std::size_t index) const
    {
        return Xor32Decode(sent.at(index).packet);
    }

    Client::Kernel::WorldState world;
    Client::FrontEnd::SelectionState selection{world};
    Client::Camera::GameCamera camera;
    Client::Camera::GameCameraInput cameraInput;
    Client::Models::ClothWorld cloth{.random = CrtRandom(Seed)};
    Client::FrontEnd::SingleModelLibrary models;
    Client::Tables::ItemTable items =
        Client::Tests::ItemTableOf({{Sword, 1, 3, 0}, {Shield, 2, 2, 1}, {Jewel, 1, 1, 0xff}});
    Client::Tables::ItemSetTables sets = Client::Tests::ItemSetTablesFrom(
        std::vector<std::byte>(Client::Tables::ItemSetTypeTableSize, std::byte{0xff}),
        std::vector<std::byte>(Client::Tables::ItemSetOptionTableSize, std::byte{0xff}));
    Client::Tables::TextTable texts = Client::Tests::MakeTextTable({{ChaosBoxNotEmptyText, "la caja tiene algo"},
                                                                    {594, "no salio"},
                                                                    {MixSucceededText, "salio"},
                                                                    {596, "no"},
                                                                    {TrainerListText, "pone la mascota"}});
    LetterMeasure measure;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
    std::vector<Client::Protocol::Outgoing> sent;
};

class NpcWindowRepliesTest : public ::testing::Test
{
protected:
    [[nodiscard]] Client::Core::Expected<void, std::string> Talk(std::uint8_t result)
    {
        return Client::Npc::ApplyTalkResult(Npc::TalkResult{.result = result, .rates = {10, 20, 30, 40, 0, 0}},
                                            stage->Resources());
    }

    // The registry as the UI step builds it, with the windows' wiring.
    template <typename Body> void WithThePanels(Body body)
    {
        const WorldEntryResources resources = stage->Resources();
        Client::Hud::GamePanels panels(stage->world, stage->cloth.random);
        Client::Npc::LendTheNpcWindows(panels, resources);
        body(panels.Registry());
    }

    [[nodiscard]] NpcWindowState& Npc()
    {
        return stage->world.Runtime().npcWindows;
    }

    // On the heap: WorldState weighs about 350 KB (the MSVC stack is 1 MB).
    std::unique_ptr<Stage> stage = std::make_unique<Stage>();
};

// 0x4d1e00 with 0 (the default of 0x4d2881): closing everything draws the rand() of the trade and the shop (2);
// 0x6599a0(0x10000) draws 9: its IsOpen, CanOpen (the shop and the trade), CloseByMask (the trade), the inventory
// inside (IsOpen, CanOpen with 2 and CloseByMask with 2) and the write of the protected byte. Then each query of the
// open shop draws one and gives 1.
TEST_F(NpcWindowRepliesTest, TheDefaultOpensTheNpcShopWithTheRandomOfItsProtectedByte)
{
    const CrtRandom expected = Advanced(stage->cloth.random, 2 + 9);
    const auto talked = Talk(0);
    ASSERT_TRUE(talked.HasValue()) << talked.Error();
    EXPECT_EQ(stage->cloth.random.State(), expected.State());
    const Client::Ui::PanelState& state = stage->world.Runtime().panels;
    EXPECT_EQ(state.npcShop, 1);
    EXPECT_EQ(state.npcShopDepth, 0);
    EXPECT_EQ(state.inventory, 1);
    EXPECT_EQ(stage->world.Runtime().lastSoundId, Client::Audio::SoundId{0x1c});
    EXPECT_TRUE(Npc().cursorWarpRequested);
    EXPECT_TRUE(stage->sent.empty());

    const CrtRandom query = Advanced(stage->cloth.random, 1);
    WithThePanels([](Client::Ui::PanelRegistry& panels) { EXPECT_TRUE(panels.IsOpen(Client::Ui::NpcShopPanel)); });
    EXPECT_EQ(stage->cloth.random.State(), query.State());
}

// 0x4d25bc..0x4d25dd: close mode before the open, then Senior's three queries.
TEST_F(NpcWindowRepliesTest, SeniorTalkWritesTheCloseModeBeforeOpeningAndRequestingItsData)
{
    for (const std::uint8_t mode : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{255}})
    {
        stage = std::make_unique<Stage>();
        bool checkedBeforeSend = false;
        auto resources = stage->Resources();
        resources.sendNpcWindowPacket = [&](Client::Protocol::Outgoing packet)
        {
            EXPECT_EQ(stage->world.Runtime().castleSiege.senior.closeWith87, mode == 1);
            EXPECT_EQ(stage->world.Runtime().panels.delegated.castleSiegeList, 1);
            checkedBeforeSend = true;
            stage->sent.push_back(std::move(packet));
            return Client::Core::Expected<void, std::string>{};
        };
        const auto result = Client::Npc::ApplyTalkResult(Npc::TalkResult{.result = 12, .rates = {mode}}, resources);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_TRUE(checkedBeforeSend);
        ASSERT_EQ(stage->sent.size(), 3u);
        EXPECT_TRUE(SameBytes(stage->Plain(0).Bytes(), Bytes({0xc1, 4, 0xb3, 1})));
        EXPECT_TRUE(SameBytes(stage->Plain(1).Bytes(), Bytes({0xc1, 4, 0xb3, 2})));
        EXPECT_TRUE(SameBytes(stage->Plain(2).Bytes(), Bytes({0xc1, 4, 0xb2, 8})));
        EXPECT_TRUE(Npc().cursorWarpRequested);
    }
}

TEST_F(NpcWindowRepliesTest, CastleGuardTalkClosesTheCurrentNpcBeforeRequestingCastleState)
{
    ASSERT_TRUE(Talk(0).HasValue());
    const auto result = Talk(13);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(stage->sent.size(), 2u);
    EXPECT_TRUE(SameBytes(stage->Plain(0).Bytes(), Bytes({0xc1, 3, 0x31})));
    EXPECT_TRUE(SameBytes(stage->Plain(1).Bytes(), Bytes({0xc1, 4, 0xb2, 0})));
    EXPECT_EQ(stage->world.Runtime().panels.npcShop, 0);
    EXPECT_EQ(stage->world.Runtime().lastSoundId, Client::Audio::SoundId{0x1c});
}

// 0x4d2532 and 0x4d253b: the warehouse and the chaos box, each with the inventory. The box sends the C1 04 BD 09 (with
// the XOR32, in the clear), opens dialog 0x8e and copies the rates.
TEST_F(NpcWindowRepliesTest, TheWarehouseAndTheChaosBoxOpenWithTheInventory)
{
    stage->world.Runtime().ui.field78abb20 = 5;
    Npc().warehouseLocked = 1;
    ASSERT_TRUE(Talk(2).HasValue());
    EXPECT_EQ(stage->world.Runtime().panels.warehouse, 1);
    EXPECT_EQ(stage->world.Runtime().panels.inventory, 1);
    EXPECT_EQ(Npc().warehouseLocked, 0);
    EXPECT_EQ(stage->world.Runtime().ui.field78abb20, 0);

    Npc().chaosMixState = 2;
    const auto chaos = Talk(3);
    ASSERT_TRUE(chaos.HasValue()) << chaos.Error();
    // Closing everything closed the warehouse with its C1 03 82; then, the BD 09.
    ASSERT_EQ(stage->sent.size(), 2u);
    EXPECT_TRUE(SameBytes(stage->sent[0].packet.Bytes(), Bytes({0xc1, 0x03, 0x82})));
    EXPECT_EQ(stage->sent[1].delivery, Delivery::Clear);
    EXPECT_TRUE(SameBytes(stage->Plain(1).Bytes(), Bytes({0xc1, 0x04, 0xbd, 0x09})));
    EXPECT_EQ(stage->world.Runtime().panels.warehouse, 0);
    EXPECT_EQ(stage->world.Runtime().panels.chaosBox, 1);
    EXPECT_EQ(stage->world.Runtime().dialogs.current, 0x8e);
    EXPECT_EQ(Npc().chaosMixState, 0);
    EXPECT_EQ(Npc().chaosRates, (std::array<std::int32_t, 6>{10, 20, 30, 40, 0, 0}));
}

// 0x4d2591: the 7 opens the trainer (0x8000): closing everything (2 rand()) and 0x6599a0(0x8000) with CanOpen and
// CloseByMask (the shop and the trade, 4), 0x449940(1) with the box empty and without the inventory.
TEST_F(NpcWindowRepliesTest, TheTrainerOpensItsMenuWithoutTheInventory)
{
    auto& runtime = stage->world.Runtime();
    runtime.npcWindows.trainerBox[4].id = Jewel;
    const CrtRandom expected = Advanced(stage->cloth.random, 2 + 4);
    const auto talked = Talk(7);
    ASSERT_TRUE(talked.HasValue()) << talked.Error();
    EXPECT_EQ(stage->cloth.random.State(), expected.State());
    EXPECT_EQ(runtime.panels.delegated.trainer, 1);
    EXPECT_EQ(runtime.npcWindows.trainerBox[4].id, 0xffff);
    EXPECT_EQ(runtime.panels.inventory, 0);
    EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{0x1c});
    EXPECT_TRUE(Npc().cursorWarpRequested);
    EXPECT_TRUE(stage->sent.empty());
}

// 0x4d257c, 0x65b240: result 5 opens 0x2000 and lets the next step handle its checkbox.
TEST_F(NpcWindowRepliesTest, TheFifthTalkResultOpensPanel2000WithoutTheInventory)
{
    for (const std::int32_t map : {0, 2, 3})
    {
        stage = std::make_unique<Stage>();
        stage->world.Runtime().map = map;
        const auto talked = Talk(5);
        ASSERT_TRUE(talked.HasValue()) << talked.Error();
        EXPECT_EQ(stage->world.Runtime().panels.panel2000, 1);
        EXPECT_EQ(stage->world.Runtime().panels.inventory, 0);
        EXPECT_EQ(stage->world.Runtime().panels.field78abb08, 0);
        EXPECT_EQ(stage->world.Runtime().lastSoundId, Client::Audio::SoundId{0x1c});
        EXPECT_TRUE(Npc().cursorWarpRequested);
        EXPECT_TRUE(stage->sent.empty());
        EXPECT_EQ(stage->cloth.random.State(), Advanced(CrtRandom(Seed), 6).State());
    }
}

// With each window open (and the inventory), the world is 260 wide (0x5bb8e3..0x5bb9a3).
TEST_F(NpcWindowRepliesTest, TheWorldIs260WideWithEachWindowOpen)
{
    for (const std::uint8_t result : {std::uint8_t{0}, std::uint8_t{2}, std::uint8_t{3}})
    {
        stage = std::make_unique<Stage>();
        ASSERT_TRUE(Talk(result).HasValue());
        WithThePanels([](Client::Ui::PanelRegistry& panels) { EXPECT_EQ(panels.WorldWidth(), 260); });
    }
}

// 0x657ab0(0x10000): the byte at 0 (one rand()), the C1 03 31 in the clear and the grid empty; then, the inventory.
TEST_F(NpcWindowRepliesTest, ClosingTheShopSends31AndEmptiesTheGrid)
{
    ASSERT_TRUE(Talk(0).HasValue());
    Npc().grid[5].id = Jewel;
    Npc().grid[5].occupied = 1;
    // Close: IsOpen (1), CloseByMask with its IsOpen (1) and the write (1); the inventory draws none.
    const CrtRandom expected = Advanced(stage->cloth.random, 3);
    WithThePanels([](Client::Ui::PanelRegistry& panels)
                  { ASSERT_TRUE(panels.Close(Client::Ui::NpcShopPanel).HasValue()); });
    EXPECT_EQ(stage->cloth.random.State(), expected.State());
    EXPECT_EQ(stage->world.Runtime().panels.npcShop, 0);
    EXPECT_EQ(stage->world.Runtime().panels.inventory, 0);
    ASSERT_EQ(stage->sent.size(), 1u);
    EXPECT_TRUE(SameBytes(stage->sent[0].packet.Bytes(), Bytes({0xc1, 0x03, 0x31})));
    EXPECT_EQ(Npc().grid[5].id, Client::Kernel::EmptyOwnItemId);
    EXPECT_EQ(Npc().grid[5].occupied, 0);
}

// 0x65852b: with the 24's lock, the warehouse does not close and CloseByMask gives 0, but it closes the rest;
// 0x658741: with something in the chaos box, text 593 and 0.
TEST_F(NpcWindowRepliesTest, TheLockAndAFullChaosBoxKeepTheirWindowsOpen)
{
    ASSERT_TRUE(Talk(2).HasValue());
    stage->world.Runtime().ui.field78abb39 = 1;
    stage->world.Runtime().panels.party = 1;
    WithThePanels(
        [](Client::Ui::PanelRegistry& panels)
        {
            const auto closed = panels.CloseByMask(Client::Ui::WarehousePanel | Client::Ui::PartyPanel);
            ASSERT_TRUE(closed.HasValue());
            EXPECT_FALSE(closed.Value());
        });
    EXPECT_EQ(stage->world.Runtime().panels.warehouse, 1);
    EXPECT_EQ(stage->world.Runtime().panels.party, 0);
    EXPECT_TRUE(stage->sent.empty());

    stage = std::make_unique<Stage>();
    ASSERT_TRUE(Talk(3).HasValue());
    stage->sent.clear();
    Npc().chaosBox[3].id = Jewel;
    Npc().chaosBox[3].occupied = 1;
    WithThePanels([](Client::Ui::PanelRegistry& panels)
                  { ASSERT_TRUE(panels.Close(Client::Ui::ChaosBoxPanel).HasValue()); });
    EXPECT_EQ(stage->world.Runtime().panels.chaosBox, 1);
    EXPECT_TRUE(stage->sent.empty());
    ASSERT_FALSE(stage->world.Runtime().chat.log.entries.empty());
    EXPECT_EQ(stage->world.Runtime().chat.log.entries.back().text, "la caja tiene algo");

    Npc().chaosBox[3].id = Client::Kernel::EmptyOwnItemId;
    WithThePanels([](Client::Ui::PanelRegistry& panels)
                  { ASSERT_TRUE(panels.Close(Client::Ui::ChaosBoxPanel).HasValue()); });
    EXPECT_EQ(stage->world.Runtime().panels.chaosBox, 0);
    EXPECT_EQ(stage->world.Runtime().dialogs.current, 0);
    ASSERT_EQ(stage->sent.size(), 1u);
    EXPECT_TRUE(SameBytes(stage->sent[0].packet.Bytes(), Bytes({0xc1, 0x03, 0x87})));
}

// 0x4c5f60: the shop's list goes to 0x78a7070 with 0x5bc150 (8 per row from the slot) and the chaos box's to
// 0x7d2d380 with 0x5bc740(.., 8, 4, ..), with 0x7d2dc80 at 2.
TEST_F(NpcWindowRepliesTest, TheListFillsTheShopGridAndTheChaosBox)
{
    Npc().grid[40].id = Jewel;
    const Npc::ItemList shop{.grid = 0,
                             .items = {{.slot = 2, .item = ItemBytes(Sword)}, {.slot = 4, .item = ItemBytes(Shield)}}};
    const auto listed = Client::Npc::ApplyItemList(shop, stage->Resources());
    ASSERT_TRUE(listed.HasValue()) << listed.Error();
    const auto& grid = Npc().grid;
    EXPECT_EQ(grid[40].id, Client::Kernel::EmptyOwnItemId);
    for (const std::size_t cell : {2u, 10u, 18u})
    {
        EXPECT_EQ(grid[cell].id, Sword) << cell;
        EXPECT_EQ(grid[cell].gridColumn, 2) << cell;
        EXPECT_EQ(grid[cell].gridRow, 0) << cell;
    }
    EXPECT_EQ(grid[2].occupied, 1);
    EXPECT_EQ(grid[10].occupied, 0);
    for (const std::size_t cell : {4u, 5u, 12u, 13u})
        EXPECT_EQ(grid[cell].id, Shield) << cell;

    const Npc::ItemList chaos{.grid = 3, .items = {{.slot = 9, .item = ItemBytes(Jewel)}}};
    ASSERT_TRUE(Client::Npc::ApplyItemList(chaos, stage->Resources()).HasValue());
    EXPECT_EQ(Npc().chaosMixState, 2);
    EXPECT_EQ(Npc().chaosBox[9].id, Jewel);
    EXPECT_EQ(Npc().chaosBox[9].gridColumn, 1);
    EXPECT_EQ(Npc().chaosBox[9].gridRow, 1);
    EXPECT_EQ(stage->world.Runtime().lastSoundId, Client::Audio::SoundId{0x44});
}

// The 31 with grid 5 (0x4c5fb4..0x4c6042): warning 1208 of type 2, the sounds, the trainer box empty and each item
// with 0x44ad70(slot, data, 0), 0x5bc740's tail with the own record open (3848 rand() per item). 0x7d2dc80 stays as
// it was.
TEST_F(NpcWindowRepliesTest, TheListFillsTheTrainerBoxWithTheTailOfEachItem)
{
    Npc().trainerBox[30].id = Jewel;
    Npc().chaosMixState = 1;
    const CrtRandom expected = Advanced(stage->cloth.random, 2 * Own);
    const Npc::ItemList trainer{
        .grid = 5, .items = {{.slot = 3, .item = ItemBytes(Sword)}, {.slot = 12, .item = ItemBytes(Shield)}}};
    const auto listed = Client::Npc::ApplyItemList(trainer, stage->Resources());
    ASSERT_TRUE(listed.HasValue()) << listed.Error();
    EXPECT_EQ(stage->cloth.random.State(), expected.State());
    const auto& box = Npc().trainerBox;
    EXPECT_EQ(box[30].id, Client::Kernel::EmptyOwnItemId);
    for (const std::size_t cell : {3u, 11u, 19u})
        EXPECT_EQ(box[cell].id, Sword) << cell;
    for (const std::size_t cell : {12u, 13u, 20u, 21u})
    {
        EXPECT_EQ(box[cell].id, Shield) << cell;
        EXPECT_EQ(box[cell].gridColumn, 4) << cell;
        EXPECT_EQ(box[cell].gridRow, 1) << cell;
    }
    EXPECT_EQ(box[12].occupied, 1);
    EXPECT_EQ(box[21].occupied, 0);
    EXPECT_EQ(Npc().chaosMixState, 1);
    EXPECT_EQ(stage->world.Runtime().lastSoundId, Client::Audio::SoundId{0x44});
    const auto& entry = stage->world.Runtime().chat.log.entries.back();
    EXPECT_EQ(entry.text, "pone la mascota");
    EXPECT_EQ(entry.type, 2);
    EXPECT_TRUE(entry.name.empty());
}

// The 32: the bought item in its inventory slot, sound 0x1d and 0x5767950 at 0; with 0xff, only 0x5767950.
TEST_F(NpcWindowRepliesTest, ABoughtItemGoesToItsInventoryPlace)
{
    Npc().buyLock = 1;
    ASSERT_TRUE(Client::Npc::ApplyBuyResult({.slot = 0xff, .item = {}}, stage->Resources()).HasValue());
    EXPECT_EQ(Npc().buyLock, 0);
    Npc().buyLock = 1;
    const auto bought = Client::Npc::ApplyBuyResult({.slot = 12 + 9, .item = ItemBytes(Jewel)}, stage->Resources());
    ASSERT_TRUE(bought.HasValue()) << bought.Error();
    EXPECT_EQ(stage->world.Inventory().cells[9].id, Jewel);
    EXPECT_EQ(stage->world.Runtime().lastSoundId, Client::Audio::SoundId{0x1d});
    EXPECT_EQ(Npc().buyLock, 0);
}

// The 33 and the 34: the new zen in +0xecc with the own record open; the 34, also 0x550d10 inside; the 81, both zen
// (+0xed0 and +0xecc).
TEST_F(NpcWindowRepliesTest, SellRepairAndTheWarehouseMoneyWriteTheZen)
{
    stage->world.Runtime().cursorItemCount = 1;
    const CrtRandom sold = Advanced(stage->cloth.random, Own);
    ASSERT_TRUE(Client::Npc::ApplySellResult({.result = 1, .money = 1234}, stage->Resources()).HasValue());
    EXPECT_EQ(stage->cloth.random.State(), sold.State());
    EXPECT_EQ(stage->world.OwnCharacter().zen, 1234u);
    EXPECT_EQ(stage->world.Runtime().cursorItemCount, 0);
    EXPECT_EQ(stage->world.Runtime().lastSoundId, Client::Audio::SoundId{0x1d});

    const auto repaired = Client::Npc::ApplyRepairResult({.money = 1000}, stage->Resources());
    ASSERT_TRUE(repaired.HasValue()) << repaired.Error();
    EXPECT_EQ(stage->world.OwnCharacter().zen, 1000u);
    EXPECT_EQ(stage->world.Runtime().lastSoundId, Client::Audio::SoundId{0x25});
    ASSERT_TRUE(Client::Npc::ApplyRepairResult({.money = 0}, stage->Resources()).HasValue());
    EXPECT_EQ(stage->world.OwnCharacter().zen, 1000u);

    const CrtRandom moved = Advanced(stage->cloth.random, Own);
    ASSERT_TRUE(Client::Npc::ApplyWarehouseMoney({.result = 1, .warehouseMoney = 500, .inventoryMoney = 700},
                                                 stage->Resources())
                    .HasValue());
    EXPECT_EQ(stage->cloth.random.State(), moved.State());
    EXPECT_EQ(stage->world.OwnCharacter().warehouseZen, 500u);
    EXPECT_EQ(stage->world.OwnCharacter().zen, 700u);
}

// Storing and taking zen from the warehouse: the 83 with 0xc, with the lock and the pending one at −1, sends
// 0x4e6f80(type, zen), C1 08 81 with the XOR32 and in the clear, and leaves the lock and 0x78abb1c at 1.
TEST_F(NpcWindowRepliesTest, StoringAndTakingZenSends81WhenThePasswordIsAccepted)
{
    for (const std::uint8_t type : {std::uint8_t{0}, std::uint8_t{1}})
    {
        stage = std::make_unique<Stage>();
        ASSERT_TRUE(Client::Npc::ApplyWarehouseState({.state = 1}, stage->Resources()).HasValue());
        Npc().pendingMove = {.slot = -1, .second = type, .third = 0x12345, .fourth = 0};
        ASSERT_TRUE(Client::Npc::ApplyWarehouseState({.state = 0xc}, stage->Resources()).HasValue());
        ASSERT_EQ(stage->sent.size(), 1u);
        EXPECT_EQ(stage->sent[0].delivery, Delivery::Clear);
        EXPECT_TRUE(SameBytes(stage->Plain(0).Bytes(), Bytes({0xc1, 0x08, 0x81, type, 0x45, 0x23, 0x01, 0x00})));
        EXPECT_EQ(Npc().warehouseLocked, 1);
        EXPECT_EQ(Npc().field78abb1c, 1);
        // Again, with 0x78abb1c at 1, it sends nothing.
        ASSERT_TRUE(Client::Npc::ApplyWarehouseState({.state = 0xc}, stage->Resources()).HasValue());
        EXPECT_EQ(stage->sent.size(), 1u);
    }
    ASSERT_TRUE(Client::Npc::ApplyWarehouseState({.state = 0xb}, stage->Resources()).HasValue());
    EXPECT_EQ(stage->world.Runtime().dialogs.current, 0x86);
}

// The 83 with 0xc and an item waiting (0x4e47b0..0x4e47c6): 0x449d80 with the four stored values, the cursor item's
// C1 0E 24 with the XOR32 and in C3 (two rand()), and the 24's lock (0x78abb39) on. With the lock already on,
// 0x449d80 sends nothing, but the 83 still leaves 0x6c932f and 0x78abb1c at 1.
TEST_F(NpcWindowRepliesTest, TakingAnItemSends24WhenThePasswordIsAccepted)
{
    for (const bool alreadyMoving : {false, true})
    {
        SCOPED_TRACE(alreadyMoving);
        stage = std::make_unique<Stage>();
        auto& runtime = stage->world.Runtime();
        ASSERT_TRUE(Client::Npc::ApplyWarehouseState({.state = 1}, stage->Resources()).HasValue());
        Npc().pendingMove = {.slot = 2, .second = 17, .third = 0, .fourth = 30};
        runtime.cursorItem = Client::Kernel::ItemState{.id = Shield, .scalar1Low = 2u << 3, .durability = 9};
        runtime.cursorItemCount = 1;
        runtime.ui.field78abb39 = alreadyMoving ? 1 : 0;
        const CrtRandom expected = Advanced(stage->cloth.random, alreadyMoving ? 0 : 2);
        ASSERT_TRUE(Client::Npc::ApplyWarehouseState({.state = 0xc}, stage->Resources()).HasValue());
        EXPECT_EQ(stage->cloth.random.State(), expected.State());
        EXPECT_EQ(runtime.ui.field78abb39, 1);
        EXPECT_EQ(Npc().warehouseLocked, 1);
        EXPECT_EQ(Npc().field78abb1c, 1);
        if (alreadyMoving)
        {
            EXPECT_TRUE(stage->sent.empty());
            continue;
        }
        ASSERT_EQ(stage->sent.size(), 1u);
        EXPECT_EQ(stage->sent[0].delivery, Delivery::Encrypted);
        EXPECT_TRUE(SameBytes(stage->Plain(0).Bytes(),
                              Bytes({0xc1, 0x0e, 0x24, 0x02, 17, 0x00, 0x10, 9, 0x00, 0x00, 0x60, 0x00, 0x00, 30})));
    }
}

// Q-C200: the network replies neither write partially nor close the session over a footprint outside the grid.
TEST_F(NpcWindowRepliesTest, ListsDiscardOnlyTheEntryPastTheGrid)
{
    for (const std::uint8_t grid : std::array<std::uint8_t, 3>{0, 3, 5})
    {
        SCOPED_TRACE(grid);
        const std::uint8_t bad = grid == 0 ? 119 : 31;
        const auto applied = Client::Npc::ApplyItemList(
            {.grid = grid, .items = {{.slot = bad, .item = ItemBytes(Shield)}, {.slot = 0, .item = ItemBytes(Jewel)}}},
            stage->Resources());
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        const std::span<const Client::Kernel::ItemState> cells =
            grid == 0   ? std::span<const Client::Kernel::ItemState>(Npc().grid)
            : grid == 3 ? std::span<const Client::Kernel::ItemState>(Npc().chaosBox)
                        : std::span<const Client::Kernel::ItemState>(Npc().trainerBox);
        EXPECT_EQ(cells[bad].id, Client::Kernel::EmptyOwnItemId);
        EXPECT_EQ(cells[0].id, Jewel);
        ASSERT_FALSE(stage->sink.lines.empty());
        EXPECT_NE(stage->sink.lines.back().find("Q-C200"), std::string::npos);
    }
}

// Q-C201 keeps the native linear index: going past column 7 continues on the row below, as long as it stays inside
// the array.
TEST_F(NpcWindowRepliesTest, ListsPreserveTheNativeCarryAcrossRows)
{
    for (const std::uint8_t grid : std::array<std::uint8_t, 3>{0, 3, 5})
    {
        SCOPED_TRACE(grid);
        const auto applied = Client::Npc::ApplyItemList(
            {.grid = grid, .items = {{.slot = 7, .item = ItemBytes(Shield)}}}, stage->Resources());
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        const std::span<const Client::Kernel::ItemState> cells =
            grid == 0   ? std::span<const Client::Kernel::ItemState>(Npc().grid)
            : grid == 3 ? std::span<const Client::Kernel::ItemState>(Npc().chaosBox)
                        : std::span<const Client::Kernel::ItemState>(Npc().trainerBox);
        for (const std::size_t cell : {7u, 8u, 15u, 16u})
        {
            EXPECT_EQ(cells[cell].id, Shield);
            EXPECT_EQ(cells[cell].gridColumn, 7);
            EXPECT_EQ(cells[cell].gridRow, 0);
        }
    }
}

TEST_F(NpcWindowRepliesTest, BuyingAnItemPastTheGridDoesNotWriteAPartialFootprint)
{
    Npc().buyLock = 1;
    const auto applied = Client::Npc::ApplyBuyResult({.slot = 75, .item = ItemBytes(Sword)}, stage->Resources());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(stage->world.Inventory().cells[63].id, Client::Kernel::EmptyOwnItemId);
    EXPECT_EQ(Npc().buyLock, 1);
    ASSERT_FALSE(stage->sink.lines.empty());
    EXPECT_NE(stage->sink.lines.back().find("Q-C200"), std::string::npos);
}

TEST_F(NpcWindowRepliesTest, MovingAnItemPastAnyNpcGridDiscardsItsWholeFootprint)
{
    for (const std::uint8_t grid : std::array<std::uint8_t, 3>{2, 3, 5})
    {
        SCOPED_TRACE(grid);
        stage->world.Runtime().cursorItemCount = 1;
        stage->world.Runtime().ui.field78abb39 = 1;
        const std::uint8_t bad = grid == 2 ? 119 : 31;
        auto& retained = grid == 2 ? Npc().grid[bad] : grid == 3 ? Npc().chaosBox[bad] : Npc().trainerBox[bad];
        retained.id = Jewel;
        const auto applied = Client::Items::ApplyItemMoveResult(
            {.result = grid, .slot = bad, .item = ItemBytes(Shield)}, stage->Resources());
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        const std::span<const Client::Kernel::ItemState> cells =
            grid == 2   ? std::span<const Client::Kernel::ItemState>(Npc().grid)
            : grid == 3 ? std::span<const Client::Kernel::ItemState>(Npc().chaosBox)
                        : std::span<const Client::Kernel::ItemState>(Npc().trainerBox);
        EXPECT_EQ(cells[bad].id, Jewel);
        EXPECT_EQ(stage->world.Runtime().cursorItemCount, 1);
        EXPECT_EQ(stage->world.Runtime().ui.field78abb39, 1);
        ASSERT_FALSE(stage->sink.lines.empty());
        EXPECT_NE(stage->sink.lines.back().find("Q-C200"), std::string::npos);
    }
}

TEST_F(NpcWindowRepliesTest, MixResultsDiscardAnItemWhoseHeightExceedsTheBox)
{
    stage->items = Client::Tests::ItemTableOf({{Sword, 1, 5, 0}});
    for (const std::uint8_t result : std::array<std::uint8_t, 2>{1, 100})
    {
        SCOPED_TRACE(result);
        Npc().chaosBox[0].id = Jewel;
        Npc().trainerBox[0].id = Jewel;
        Npc().chaosMixState = 1;
        const auto applied =
            Client::Npc::ApplyChaosMixResult({.result = result, .item = ItemBytes(Sword)}, stage->Resources());
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        const auto& cells = result == 1 ? Npc().chaosBox : Npc().trainerBox;
        EXPECT_EQ(cells[0].id, Jewel);
        EXPECT_EQ(Npc().chaosMixState, 1);
        ASSERT_FALSE(stage->sink.lines.empty());
        EXPECT_NE(stage->sink.lines.back().find("Q-C200"), std::string::npos);
    }
}

// The 86 with 1 (0x4da2c5): text 595, the box empty and the new item in slot 0, with 0x7d2dc80 at 2.
TEST_F(NpcWindowRepliesTest, ASuccessfulMixLeavesTheNewItemInTheChaosBox)
{
    Npc().chaosBox[20].id = Jewel;
    Npc().chaosMixState = 1;
    const auto mixed = Client::Npc::ApplyChaosMixResult({.result = 1, .item = ItemBytes(Sword)}, stage->Resources());
    ASSERT_TRUE(mixed.HasValue()) << mixed.Error();
    EXPECT_EQ(Npc().chaosMixState, 2);
    EXPECT_EQ(Npc().chaosBox[20].id, Client::Kernel::EmptyOwnItemId);
    for (const std::size_t cell : {0u, 8u, 16u})
        EXPECT_EQ(Npc().chaosBox[cell].id, Sword) << cell;
    EXPECT_EQ(stage->world.Runtime().chat.log.entries.back().text, "salio");

    ASSERT_TRUE(Client::Npc::ApplyChaosMixResult({.result = 2, .item = {}}, stage->Resources()).HasValue());
    EXPECT_EQ(Npc().chaosMixState, 0);
}

// 0x4d2899/0x4d28a4, 0x4c5f7f/0x4c5f8a, 0x4c5fcb/0x4c5fd6 and 0x4da31b/0x4da326.
TEST_F(NpcWindowRepliesTest, NativeSoundPairsReachPlaybackInOrderWithoutAnOriginOrLoop)
{
    NpcPlayback playback;
    stage->world.Runtime().audio = &playback;
    Client::Sound::SoundRequests playbackRequests(&playback);
    stage->world.Runtime().sounds = &playbackRequests;
    ASSERT_TRUE(Talk(13).HasValue());
    EXPECT_EQ(playback.sounds, (std::vector<std::int32_t>{0x19, 0x1c}));
    playback.sounds.clear();

    for (const std::uint8_t grid : {std::uint8_t{3}, std::uint8_t{5}})
    {
        ASSERT_TRUE(Client::Npc::ApplyItemList({.grid = grid, .items = {}}, stage->Resources()).HasValue());
        EXPECT_EQ(playback.sounds, (std::vector<std::int32_t>{0x57, 0x44}));
        playback.sounds.clear();
    }
    ASSERT_TRUE(
        Client::Npc::ApplyChaosMixResult({.result = 1, .item = ItemBytes(Sword)}, stage->Resources()).HasValue());
    EXPECT_EQ(playback.sounds, (std::vector<std::int32_t>{0x57, 0x45}));
    stage->world.Runtime().audio = nullptr;
    stage->world.Runtime().sounds = nullptr;
}

// The 86 with 100 (0x4da373): the trainer box empty and the item in its slot 0 with 0x44ad70(0, data, 1), without the
// tail (no rand()), without sound and with 0x7d2dc80 as it was.
TEST_F(NpcWindowRepliesTest, TheTrainerMixLeavesItsItemInTheTrainerBox)
{
    Npc().trainerBox[20].id = Jewel;
    Npc().chaosMixState = 1;
    stage->world.Runtime().lastSoundId = Client::Audio::SoundId{0};
    const CrtRandom before = stage->cloth.random;
    const auto mixed = Client::Npc::ApplyChaosMixResult({.result = 100, .item = ItemBytes(Sword)}, stage->Resources());
    ASSERT_TRUE(mixed.HasValue()) << mixed.Error();
    EXPECT_EQ(stage->cloth.random.State(), before.State());
    EXPECT_EQ(Npc().trainerBox[20].id, Client::Kernel::EmptyOwnItemId);
    for (const std::size_t cell : {0u, 8u, 16u})
        EXPECT_EQ(Npc().trainerBox[cell].id, Sword) << cell;
    EXPECT_EQ(Npc().trainerBox[0].occupied, 1);
    EXPECT_EQ(Npc().chaosMixState, 1);
    EXPECT_EQ(stage->world.Runtime().lastSoundId, Client::Audio::SoundId{0});
}

// The 24 with 2, 3 and 5 (0x4d1cf6.., 0x4d1d20.. and 0x4d1d4a..): 0x5bc740 with the last argument at 0 in the
// warehouse grid (8 per row), in the chaos box and in the trainer's, and 0x5bd405's tail with the own record open;
// then, sound 0x1d.
TEST_F(NpcWindowRepliesTest, TheItemMoveResultPlacesInTheOtherGrids)
{
    stage->world.Runtime().cursorItemCount = 1;
    const CrtRandom stored = Advanced(stage->cloth.random, Own);
    const auto warehouse =
        Client::Items::ApplyItemMoveResult({.result = 2, .slot = 10, .item = ItemBytes(Shield)}, stage->Resources());
    ASSERT_TRUE(warehouse.HasValue()) << warehouse.Error();
    EXPECT_EQ(stage->cloth.random.State(), stored.State());
    EXPECT_EQ(stage->world.Runtime().cursorItemCount, 0);
    for (const std::size_t cell : {10u, 11u, 18u, 19u})
    {
        EXPECT_EQ(Npc().grid[cell].id, Shield) << cell;
        EXPECT_EQ(Npc().grid[cell].gridColumn, 2) << cell;
        EXPECT_EQ(Npc().grid[cell].gridRow, 1) << cell;
    }
    EXPECT_EQ(Npc().grid[10].occupied, 1);
    EXPECT_EQ(Npc().grid[19].occupied, 0);
    EXPECT_EQ(stage->world.Runtime().lastSoundId, Client::Audio::SoundId{0x1d});

    stage->world.Runtime().lastSoundId = Client::Audio::SoundId{0};
    const auto chaos =
        Client::Items::ApplyItemMoveResult({.result = 3, .slot = 1, .item = ItemBytes(Sword)}, stage->Resources());
    ASSERT_TRUE(chaos.HasValue()) << chaos.Error();
    for (const std::size_t cell : {1u, 9u, 17u})
        EXPECT_EQ(Npc().chaosBox[cell].id, Sword) << cell;
    EXPECT_EQ(Npc().chaosBox[1].occupied, 1);
    EXPECT_EQ(stage->world.Runtime().lastSoundId, Client::Audio::SoundId{0x1d});

    // The 24 with 5 (0x4d1d4a..0x4d1d55): 0x44ad70(slot, data, 0) in the trainer box, with the tail.
    stage->world.Runtime().lastSoundId = Client::Audio::SoundId{0};
    const CrtRandom placed = Advanced(stage->cloth.random, Own);
    const auto trainer =
        Client::Items::ApplyItemMoveResult({.result = 5, .slot = 6, .item = ItemBytes(Sword)}, stage->Resources());
    ASSERT_TRUE(trainer.HasValue()) << trainer.Error();
    EXPECT_EQ(stage->cloth.random.State(), placed.State());
    for (const std::size_t cell : {6u, 14u, 22u})
        EXPECT_EQ(Npc().trainerBox[cell].id, Sword) << cell;
    EXPECT_EQ(Npc().trainerBox[6].occupied, 1);
    EXPECT_EQ(stage->world.Runtime().lastSoundId, Client::Audio::SoundId{0x1d});
}

// 0x4d28a9..0x4d28f6: x = (260 · mx / 640) · width / 640, y = height · my / 480.
TEST(NpcTalkCursorWarpTest, MovesTheCursorIntoTheWorldOf260)
{
    const auto warp = Client::Npc::NpcTalkCursorWarp(320, 240, 1280, 960);
    EXPECT_EQ(warp.x, 260);
    EXPECT_EQ(warp.y, 480);
    const auto small = Client::Npc::NpcTalkCursorWarp(639, 479, 800, 600);
    EXPECT_EQ(small.x, (259 * 800) / 640);
    EXPECT_EQ(small.y, (600 * 479) / 480);
}
// 0x4d2577/0x4d258e, TalkResult 4/6: both event NPCs have a window and the inventory, with CloseAll's two rand() and
// Open's eight, without sending a new packet on entering.
TEST_F(NpcWindowRepliesTest, BothEventTalkResultsOpenTheirNativeWindowWithTheInventory)
{
    for (const std::uint8_t result : {std::uint8_t{4}, std::uint8_t{6}})
    {
        stage = std::make_unique<Stage>();
        const CrtRandom expected = Advanced(stage->cloth.random, 10);
        const auto talked = Talk(result);
        ASSERT_TRUE(talked.HasValue()) << talked.Error();
        const auto& runtime = stage->world.Runtime();
        EXPECT_EQ(runtime.panels.eventNpc, 1);
        EXPECT_EQ(runtime.panels.eventNpcKind, result == 4 ? 0 : 1);
        EXPECT_EQ(runtime.panels.inventory, 1);
        EXPECT_EQ(stage->cloth.random.State(), expected.State());
        EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{0x1c});
        EXPECT_TRUE(Npc().cursorWarpRequested);
        EXPECT_TRUE(stage->sent.empty());
    }
}

} // namespace
