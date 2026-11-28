#include "Items/InventoryReply.h"
#include "Kernel/HelperTable.h"

#include "Session/SessionState.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Tables/ItemSetTables.h"
#include "Tables/ItemTable.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Network/PacketSerials.h"
#include "Protocol/Items/InventoryList.h"
#include "Protocol/Items/Inventory.h"
#include "Models/Cloth.h"
#include "Math/HalfLifeMath.h"
#include "Models/LogicalAccess.h"
#include "Kernel/ActorState.h"
#include "Pets/DarkHorse.h"
#include "Pets/DarkSpirit.h"
#include "Pets/HelperTable.h"
#include "Items/InventoryMoves.h"
#include "Actors/Drawing/GameHanging.h"
#include "Kernel/PoseBuffer.h"
#include "Render/ModelBatch.h"
#include "Models/Pose.h"
#include "Particles/Sprites.h"
#include "FrontEnd/LoginModels.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/OwnInventory.h"
#include "Items/OwnItemPlacement.h"
#include "FrontEnd/SelectionState.h"
#include "Session/SessionEventApplier.h"
#include "Session/ModuleStates.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldMap.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Camera/GameCamera.h"
#include "Kernel/ItemState.h"
#include "Effects/ModelEffects.h"

#include "Tables/TextTable.h"
#include "Audio/SoundId.h"
#include "support/FileBytes.h"
#include "support/GoldenDir.h"
#include "support/ItemSetFiles.h"
#include "support/LetterMeasure.h"
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"
#include "support/SceneDataFiles.h"
#include "support/SessionHarness.h"
#include "support/TestGameServer.h"
#include "support/ViewportPackets.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// F3 10 (0x4c55a0) and what it does with each entry (0x5bc740), with the real Item.bmd of the data folder:
// 06, "El F3 10, el inventario". The item bytes are synthetic; only the five slots come from the wire (04, "El F3 10").
namespace
{
using Client::Crt::CrtRandom;
using Client::Items::ApplyInventory;
using Client::Kernel::ActorState;
using Client::Kernel::EmptyOwnItemId;
using Client::Kernel::HelperState;
using Client::Kernel::ItemState;
using Client::Protocol::Items::InventoryEntry;
using Client::Protocol::Items::InventoryList;
using Client::Scenes::WorldEntryResources;
using Client::Tests::AfterDraws;

constexpr std::uint32_t Seed = 0x1234;
constexpr std::size_t PublishedSlot = 42;
// A close of the own record that leaves it with no accesses, the suffix of 0x4f2250 and the tail of 0x550d10.
constexpr int OwnRecordClose = 0xf08;
constexpr int StopRandom = 1;
constexpr int TailRandoms = 3;
// What an equipment slot spends with the record closed: its inline close and 0x512a30 inside.
constexpr int EquipmentEntry = OwnRecordClose + StopRandom + TailRandoms;

// The Item.bmd types the tests use, with their width and height there: Kris (1 by 2), Short Sword (1 by 3), Small
// Shield (2 by 2), Guardian Helm (2 by 2), Apple, Jewel of Bless, Gaurdian Angel, Dark Horse, Dark Raven and Wizard's
// Ring (1 by 1) and Horn of Doom (2 by 2).
constexpr std::uint16_t Kris = 0x000;
constexpr std::uint16_t ShortSword = 0x001;
constexpr std::uint16_t SmallShield = 0xc00;
constexpr std::uint16_t GuardianHelm = 0xe0e;
constexpr std::uint16_t Apple = 0x1c00;
constexpr std::uint16_t JewelOfBless = 0x1c0d;
constexpr std::uint16_t GuardianAngel = 0x1a00;
constexpr std::uint16_t DarkHorse = 0x1a04;
constexpr std::uint16_t DarkRaven = 0x1a05;
constexpr std::uint16_t WizardsRing = 0x1a14;
constexpr std::uint16_t HornOfDoom = 0x1a25;

constexpr std::uint8_t ChaosCastleMap = 18;

// The seven bytes of an F3 10 item for `type` (0x54b3a0 reversed): bit 8 in the 0x80 of byte 3 and bits 9 to 12 at the
// top of byte 5. Nobody reads byte 6 or the four low bits of byte 5.
std::array<std::byte, Client::Protocol::Items::InventoryItemDataSize>
ItemBytes(std::uint16_t type, std::uint8_t byte1 = 0, std::uint8_t durability = 20, std::uint8_t byte3 = 0,
          std::uint8_t byte4 = 0)
{
    return {static_cast<std::byte>(type & 0xff),
            std::byte{byte1},
            std::byte{durability},
            static_cast<std::byte>(byte3 | ((type >> 1) & 0x80)),
            std::byte{byte4},
            static_cast<std::byte>(((type >> 5) & 0xf0) | 0x0f),
            std::byte{0xa5}};
}

InventoryEntry Entry(std::uint8_t slot, std::uint16_t type, std::uint8_t byte1 = 0, std::uint8_t durability = 20,
                     std::uint8_t byte3 = 0, std::uint8_t byte4 = 0)
{
    return InventoryEntry{.slot = slot, .item = ItemBytes(type, byte1, durability, byte3, byte4)};
}

InventoryList ListOf(std::initializer_list<InventoryEntry> entries)
{
    return InventoryList{.items = std::vector<InventoryEntry>(entries)};
}

// The set tables with no set: 0x40e1b0 walks the equipment and applies nothing.
Client::Tables::ItemSetTables NoSets()
{
    return Client::Tests::ItemSetTablesFrom(
        std::vector<std::byte>(Client::Tables::ItemSetTypeTableSize, std::byte{0xff}),
        std::vector<std::byte>(Client::Tables::ItemSetOptionTableSize, std::byte{0xff}));
}

// Lives for the session, with Player.bmd, no sets and the Item.bmd the test passes.
struct Stage
{
private:
    // Two simultaneous worlds exceed the 1 MiB stack of the Windows runner.
    std::unique_ptr<Client::Kernel::WorldState> m_world = std::make_unique<Client::Kernel::WorldState>();

public:
    Stage(std::filesystem::path root, const Client::Tables::ItemTable* itemTable)
        : dataRoot(std::move(root)), cloth{.random = CrtRandom(Seed)}, items(itemTable)
    {
        models.Set(Client::Tables::PlayerSkeletonModel, {}, Client::Tests::PlayerActionsSkeleton(), {});
        world.OwnCharacter().ClearEquipmentForEntry();
    }

    [[nodiscard]] WorldEntryResources Resources()
    {
        return WorldEntryResources{.world = world,
                                   .selection = selection,
                                   .dataRoot = dataRoot,
                                   .cloth = cloth,
                                   .camera = camera,
                                   .cameraInput = cameraInput,
                                   .models = models,
                                   .items = items,
                                   .itemSetTables = &sets,
                                   .log = log,
                                   .textMeasure = &letters,
                                   .texts = &texts,
                                   .states = &states};
    }

    // The hero published at PublishedSlot: an unarmed player on map 0.
    ActorState& PublishedHero()
    {
        world.Runtime().map = 0;
        world.Runtime().hero = PublishedSlot;
        ActorState& hero = world.Actors()[PublishedSlot];
        hero = Client::Tests::UnarmedPlayer();
        hero.alive = true;
        return hero;
    }

    [[nodiscard]] const ItemState& Cell(std::size_t index) const
    {
        return world.Inventory().cells[index];
    }

    std::filesystem::path dataRoot;
    Client::Kernel::WorldState& world = *m_world;
    Client::FrontEnd::SelectionState selection{world};
    Client::Session::ModuleStates states;
    Client::Camera::GameCamera camera;
    Client::Camera::GameCameraInput cameraInput;
    Client::Models::ClothWorld cloth;
    Client::FrontEnd::SingleModelLibrary models;
    const Client::Tables::ItemTable* items = nullptr;
    Client::Tables::ItemSetTables sets = NoSets();
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
    // The welcome line of F3 03 and of the map-changing 1C (0x4c4b60 and 0x4c7c4e).
    Client::Tests::LetterMeasure letters;
    Client::Tables::TextTable texts = Client::Tests::MakeTextTable({});
};

class InventoryReplyTest : public Client::Tests::GoldenDataTest
{
protected:
    void SetUp() override
    {
        GoldenDataTest::SetUp();
        const auto path = DataFile("Local\\Item.bmd");
        ASSERT_TRUE(path.has_value());
        const std::optional<std::vector<char>> file = Client::Tests::ReadFileBytes(*path);
        ASSERT_TRUE(file.has_value());
        auto loaded = Client::Tables::LoadItemTable(std::as_bytes(std::span(*file)));
        ASSERT_TRUE(loaded.HasValue());
        m_items.emplace(std::move(loaded).Value().table);
    }

    [[nodiscard]] Stage Fresh() const
    {
        return Stage(m_dataRoot, &*m_items);
    }

    // On the heap: WorldState weighs over 600 KB and some tests have two stages.
    [[nodiscard]] std::unique_ptr<Stage> FreshOnTheHeap() const
    {
        return std::make_unique<Stage>(m_dataRoot, &*m_items);
    }

    std::optional<Client::Tables::ItemTable> m_items;
};

std::uint16_t ModelOf(std::uint16_t type)
{
    return static_cast<std::uint16_t>(type + Client::Tables::FirstItemModel);
}

// 0x54b3a0: byte 0, bit 0x80 of byte 3 as bit 8 and the top four bits of byte 5 as bits 9 to 12.
TEST(InventoryItemTypeTest, TakesByte0TheTopBitOfByte3AndTheTopNibbleOfByte5)
{
    const std::array<std::byte, 7> bytes{std::byte{0x12}, std::byte{0xff}, std::byte{0xff}, std::byte{0x80},
                                         std::byte{0xff}, std::byte{0xa7}, std::byte{0xff}};
    EXPECT_EQ(Client::Items::InventoryItemType(bytes), 0x12 | 0x100 | 0x1400);
    const std::array<std::byte, 7> low{std::byte{0x34}, std::byte{0xff}, std::byte{0xff}, std::byte{0x7f},
                                       std::byte{0xff}, std::byte{0x0f}, std::byte{0xff}};
    EXPECT_EQ(Client::Items::InventoryItemType(low), 0x34);
    for (const std::uint16_t type : {Kris, SmallShield, GuardianHelm, Apple, JewelOfBless, HornOfDoom})
    {
        EXPECT_EQ(Client::Items::InventoryItemType(ItemBytes(type)), type);
    }
}

// 0x5bc75d: type 0x1fff returns without touching anything, neither the record nor the random.
TEST_F(InventoryReplyTest, TheType1fffTouchesNothing)
{
    Stage tested = Fresh();
    const std::array<std::byte, 7> none{std::byte{0xff}, std::byte{0x01}, std::byte{0x02}, std::byte{0x80},
                                        std::byte{0x04}, std::byte{0xf0}, std::byte{0x06}};
    ASSERT_EQ(Client::Items::InventoryItemType(none), Client::Items::NoInventoryItem);

    for (const int slot : {0, 12, 80})
    {
        ASSERT_TRUE(
            Client::Items::PlaceInventoryItem(static_cast<std::uint8_t>(slot), none, tested.Resources()).HasValue());
    }

    EXPECT_EQ(tested.world.OwnCharacter().equipment[0].id, EmptyOwnItemId);
    EXPECT_EQ(tested.Cell(0).id, 0);
    EXPECT_EQ(tested.world.Inventory().secondCells[4].id, 0);
    EXPECT_EQ(tested.cloth.random.State(), CrtRandom(Seed).State());
}

// 0x4c55a0 with the count at 0: the common initialization still empties the equipment and both grids, with one record
// close. The equipment keeps the dword at +4 and the durability (Q-C203), and the hero does not change.
TEST_F(InventoryReplyTest, ACountOfZeroStillEmpties)
{
    Stage tested = Fresh();
    ActorState& hero = tested.PublishedHero();
    hero.hands[0].model = ModelOf(Kris);
    ItemState& worn = tested.world.OwnCharacter().equipment[0];
    worn.id = Kris;
    worn.occupied = 1;
    worn.flags1b = 0x22;
    worn.scalar1Low = 0x18;
    worn.durability = 30;
    for (std::size_t cell = 0; cell < Client::Kernel::InventoryCellCount; ++cell)
    {
        tested.world.Inventory().cells[cell] = ItemState{.id = Apple, .flags1b = 3, .occupied = 1};
    }
    tested.world.Inventory().secondCells.back() = ItemState{.id = Apple, .flags1b = 3, .occupied = 1};

    const auto applied = ApplyInventory(InventoryList{}, tested.Resources());

    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_TRUE(applied.Value().sounds.empty());
    EXPECT_EQ(worn.id, EmptyOwnItemId);
    EXPECT_EQ(worn.occupied, 0u);
    EXPECT_EQ(worn.flags1b, 0);
    EXPECT_EQ(worn.scalar1Low, 0x18u);
    EXPECT_EQ(worn.durability, 30);
    for (const ItemState& cell : tested.world.Inventory().cells)
    {
        EXPECT_EQ(cell.id, EmptyOwnItemId);
        EXPECT_EQ(cell.occupied, 0u);
        EXPECT_EQ(cell.flags1b, 0);
    }
    EXPECT_EQ(tested.world.Inventory().secondCells.back().id, EmptyOwnItemId);
    EXPECT_EQ(hero.hands[0].model, ModelOf(Kris));
    EXPECT_EQ(tested.world.Runtime().ownRecordDepth, 0);
    EXPECT_EQ(tested.cloth.random.State(), AfterDraws(Seed, OwnRecordClose).State());
}

// 0x5bc840..0x5bc886: an equipment slot with the item bytes, 0x54bf40 and 0x512a30 over the published hero. What is
// inside nests in the inline access: it spends the suffix of 0x4f2250, the tail of 0x550d10 and one close.
TEST_F(InventoryReplyTest, AnEquipmentSlotDressesThePublishedHero)
{
    Stage tested = Fresh();
    const ActorState& hero = tested.PublishedHero();

    const auto applied = ApplyInventory(ListOf({Entry(0, Kris, 0x18, 25, 0x01, 0x02)}), tested.Resources());

    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    const ItemState& worn = tested.world.OwnCharacter().equipment[0];
    EXPECT_EQ(worn.id, Kris);
    EXPECT_EQ(worn.durability, 25);
    EXPECT_EQ(worn.flags1b, 0x01);
    EXPECT_EQ(worn.flags1c, 0x02);
    EXPECT_EQ(worn.occupied, 1u);
    EXPECT_EQ(worn.scalar1Low, 0x18u);
    EXPECT_EQ(hero.hands[0].model, ModelOf(Kris));
    EXPECT_EQ(hero.hands[0].level, 3);
    EXPECT_EQ(hero.hands[0].flags1b, 0x01);
    EXPECT_EQ(tested.world.Runtime().ownRecordDepth, 0);
    EXPECT_EQ(tested.cloth.random.State(), AfterDraws(Seed, OwnRecordClose + EquipmentEntry).State());
}

// Q-C203: the common emptying keeps the durability. With an equipment slot in the same F3 10, the 0x550d10 of 0x512a30
// reads Item.bmd with the emptied slot's type −1 (Q-C184), and MuRet cuts there with the record closed.
TEST_F(InventoryReplyTest, AnEmptiedSlotKeepsItsDurabilityUntilQC184)
{
    Stage tested = Fresh();
    tested.PublishedHero();
    tested.world.OwnCharacter().equipment[5].durability = 10;
    tested.world.OwnCharacter().equipment[5].flags1c = 4; // Non-zero bonus: Q-C184 still needs the prefix.

    const auto applied = ApplyInventory(ListOf({Entry(0, Kris)}), tested.Resources());

    ASSERT_FALSE(applied.HasValue());
    EXPECT_NE(applied.Error().find("Q-C184"), std::string::npos) << applied.Error();
    EXPECT_NE(applied.Error().find("lugar 0"), std::string::npos) << applied.Error();
    EXPECT_EQ(tested.world.OwnCharacter().equipment[5].id, EmptyOwnItemId);
    EXPECT_EQ(tested.world.OwnCharacter().equipment[5].durability, 10);
    EXPECT_EQ(tested.world.Runtime().ownRecordDepth, 0);
}

// 0x4c5e78..0x4c5e9f does not check whether a slot repeats: each entry runs whole, and the last one stays.
TEST_F(InventoryReplyTest, RepeatedSlotsApplyEach)
{
    Stage tested = Fresh();
    const ActorState& hero = tested.PublishedHero();

    const auto applied = ApplyInventory(
        ListOf({Entry(0, Kris), Entry(0, ShortSword), Entry(12, SmallShield), Entry(12, Apple)}), tested.Resources());

    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(tested.world.OwnCharacter().equipment[0].id, ShortSword);
    EXPECT_EQ(hero.hands[0].model, ModelOf(ShortSword));
    // The apple overwrites only the shield's top-left cell; the other three stay.
    EXPECT_EQ(tested.Cell(0).id, Apple);
    EXPECT_EQ(tested.Cell(0).occupied, 1u);
    EXPECT_EQ(tested.Cell(1).id, SmallShield);
    EXPECT_EQ(tested.Cell(8).id, SmallShield);
    EXPECT_EQ(tested.Cell(9).id, SmallShield);
    EXPECT_EQ(tested.cloth.random.State(), AfterDraws(Seed, 3 * OwnRecordClose + 2 * EquipmentEntry).State());
}

// 0x5bcc1a..0x5bcd05: from slot 12, Item.bmd's width and height in cells of 8 columns; all with the origin's column and
// row, only the top-left one occupied. 0x54bf40 runs in each one.
TEST_F(InventoryReplyTest, TheGridTakesTheWidthAndHeightOfItemBmd)
{
    Stage tested = Fresh();

    const auto placed =
        Client::Items::PlaceInventoryItem(12 + 9, ItemBytes(SmallShield, 0x10, 40, 0x01, 0x02), tested.Resources());

    ASSERT_TRUE(placed.HasValue()) << placed.Error();
    for (const std::size_t cell : {9u, 10u, 17u, 18u})
    {
        SCOPED_TRACE(cell);
        const ItemState& item = tested.Cell(cell);
        EXPECT_EQ(item.id, SmallShield);
        EXPECT_EQ(item.durability, 40);
        EXPECT_EQ(item.flags1b, 0x01);
        EXPECT_EQ(item.flags1c, 0x02);
        EXPECT_EQ(item.scalar1Low, 0x10u);
        EXPECT_EQ(item.gridColumn, 1);
        EXPECT_EQ(item.gridRow, 1);
        EXPECT_EQ(item.occupied, cell == 9u ? 1u : 0u);
    }
    EXPECT_EQ(tested.Cell(11).id, 0);
    EXPECT_EQ(tested.Cell(25).id, 0);
}

// Q-C201: with no per-column limit, an item past the last column continues on the row below.
TEST_F(InventoryReplyTest, AnItemPastTheLastColumnContinuesOnTheNextRow)
{
    Stage tested = Fresh();

    ASSERT_TRUE(Client::Items::PlaceInventoryItem(12 + 7, ItemBytes(SmallShield), tested.Resources()).HasValue());

    for (const std::size_t cell : {7u, 8u, 15u, 16u})
    {
        SCOPED_TRACE(cell);
        EXPECT_EQ(tested.Cell(cell).id, SmallShield);
        EXPECT_EQ(tested.Cell(cell).gridColumn, 7);
        EXPECT_EQ(tested.Cell(cell).gridRow, 0);
    }
}

// Q-C200: with no per-cell limit, the original writes past the last cell, outside the grid. MuRet writes the ones
// inside, with their 0x54bf40, and cuts at the first one outside.
TEST_F(InventoryReplyTest, AnItemPastTheLastCellIsAnExplicitFrontier)
{
    const std::unique_ptr<Stage> testedOnHeap = FreshOnTheHeap();
    Stage& tested = *testedOnHeap;

    const auto placed = Client::Items::PlaceInventoryItem(12 + 63, ItemBytes(SmallShield), tested.Resources());

    ASSERT_FALSE(placed.HasValue());
    EXPECT_NE(placed.Error().find("Q-C200"), std::string::npos) << placed.Error();
    EXPECT_NE(placed.Error().find("0x78a9bb0"), std::string::npos) << placed.Error();
    EXPECT_EQ(tested.Cell(63).id, SmallShield);
    EXPECT_EQ(tested.Cell(63).occupied, 1u);
    // It cuts before the record: it neither opens nor closes it.
    EXPECT_EQ(tested.cloth.random.State(), CrtRandom(Seed).State());

    Stage second = Fresh();
    const auto past = Client::Items::PlaceInventoryItem(0x4c + 31, ItemBytes(Apple), second.Resources());
    ASSERT_TRUE(past.HasValue()) << past.Error();
    const auto beyond = Client::Items::PlaceInventoryItem(0x4c + 32, ItemBytes(Apple), second.Resources());
    ASSERT_FALSE(beyond.HasValue());
    EXPECT_NE(beyond.Error().find("0x78ab0b8"), std::string::npos) << beyond.Error();
}

// 0x5bcf99..0x5bd085: the second grid, with the same calculation.
TEST_F(InventoryReplyTest, TheSecondGridStartsAtSlot76)
{
    Stage tested = Fresh();

    ASSERT_TRUE(Client::Items::PlaceInventoryItem(0x4c + 11, ItemBytes(GuardianHelm), tested.Resources()).HasValue());

    const auto& second = tested.world.Inventory().secondCells;
    for (const std::size_t cell : {11u, 12u, 19u, 20u})
    {
        SCOPED_TRACE(cell);
        EXPECT_EQ(second[cell].id, GuardianHelm);
        EXPECT_EQ(second[cell].gridColumn, 3);
        EXPECT_EQ(second[cell].gridRow, 1);
    }
    EXPECT_EQ(tested.Cell(11).id, 0);
}

// 0x5bcd0b: after a grid, with [0x78a6758] below 12 it opens and closes the record (one close), and with 12 or more it
// does not touch it. The WORDs +0x3e and +0x42 at 0 do not call 0x54fe10.
TEST_F(InventoryReplyTest, AfterTheGridTheRecordClosesOnlyWithSlot78a6758BelowTwelve)
{
    const std::unique_ptr<Stage> closingOnHeap = FreshOnTheHeap();
    Stage& closing = *closingOnHeap;
    const std::unique_ptr<Stage> skippingOnHeap = FreshOnTheHeap();
    Stage& skipping = *skippingOnHeap;
    skipping.world.Runtime().slot78a6758 = 12;
    constexpr std::uint16_t Untouched = 0x1234;
    closing.world.OwnCharacter().attackSpeed = Untouched;

    ASSERT_TRUE(Client::Items::PlaceInventoryItem(12, ItemBytes(Apple), closing.Resources()).HasValue());
    ASSERT_TRUE(Client::Items::PlaceInventoryItem(12, ItemBytes(Apple), skipping.Resources()).HasValue());

    EXPECT_EQ(closing.cloth.random.State(), AfterDraws(Seed, OwnRecordClose).State());
    EXPECT_EQ(skipping.cloth.random.State(), CrtRandom(Seed).State());
    EXPECT_EQ(closing.world.OwnCharacter().attackSpeed, Untouched);
}

// 0x5bcdd9 and 0x5bcdf0: 0x54fe10 once for each non-zero WORD, +0x3e and +0x42, with the record open.
TEST_F(InventoryReplyTest, EachNonZeroWord3eOr42RunsTheSpeeds)
{
    Stage tested = Fresh();
    Client::Kernel::OwnCharacterRecord& own = tested.world.OwnCharacter();
    own.word42 = 1;
    own.agility = 100;
    own.speedAdjustBits = 0x01;

    ASSERT_TRUE(Client::Items::PlaceInventoryItem(12, ItemBytes(Apple), tested.Resources()).HasValue());

    // Class 0: agility / 20 and agility / 10, plus 20 for bit 0x01 of +0x3c (0x54fe10).
    EXPECT_EQ(own.attackSpeed, 100 / 20 + 20);
    EXPECT_EQ(own.magicSpeed, 100 / 10 + 20);
    EXPECT_EQ(tested.cloth.random.State(), AfterDraws(Seed, OwnRecordClose).State());
}

// Q-C202: in the grid 0x54bf40 runs with the record closed, and the late weapon family with bit 0x10 of B3 reads its
// level, which the last close left at rand() & 0xff. Open, it is the level.
TEST_F(InventoryReplyTest, ALateWeaponReadsTheLevelOnlyWithTheRecordOpen)
{
    const std::unique_ptr<Stage> closedOnHeap = FreshOnTheHeap();
    Stage& closed = *closedOnHeap;
    const auto cut = Client::Items::PlaceInventoryItem(12, ItemBytes(Kris, 0, 20, 0x10), closed.Resources());
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("Q-C202"), std::string::npos) << cut.Error();
    EXPECT_EQ(closed.Cell(0).id, Kris);
    EXPECT_EQ(closed.Cell(8).id, 0);

    const std::unique_ptr<Stage> heldOnHeap = FreshOnTheHeap();
    Stage& held = *heldOnHeap;
    held.world.Runtime().ownRecordDepth = 1;
    held.world.OwnCharacter().level = 200;
    const auto placed = Client::Items::PlaceInventoryItem(12, ItemBytes(Kris, 0, 20, 0x10), held.Resources());
    ASSERT_TRUE(placed.HasValue()) << placed.Error();
    const ItemState& top = held.Cell(0);
    std::optional<std::uint8_t> levelOption;
    for (std::uint8_t option = 0; option < top.optionCount; ++option)
    {
        if (top.OptionTag(option) == 0x5d)
        {
            levelOption = top.OptionValue(option);
        }
    }
    EXPECT_EQ(levelOption, std::optional<std::uint8_t>(static_cast<std::uint8_t>(200 / 20)));
    EXPECT_EQ(held.world.Runtime().ownRecordDepth, 1);
}

// 0x4225d0 from 0x54cc5c: the Dark Horse adds option 0x96 with 5 + agility / 20 + 2 · the horse's level ([0x72397c]).
// In the grid, 0x54bf40 runs with the record closed: 0x4225d0 opens and closes it (3848 rand()), and then the grid's
// tail opens and closes it again.
TEST_F(InventoryReplyTest, TheDarkHorseInTheGridAddsItsDefenseOption)
{
    Stage grid = Fresh();
    grid.world.OwnCharacter().agility = 205;
    grid.world.Runtime().pets[Client::Kernel::DarkHorsePetStats].level = 3;

    const auto placed = Client::Items::PlaceInventoryItem(12, ItemBytes(DarkHorse), grid.Resources());

    ASSERT_TRUE(placed.HasValue()) << placed.Error();
    const ItemState& horse = grid.Cell(0);
    EXPECT_EQ(horse.id, DarkHorse);
    ASSERT_EQ(horse.optionCount, 1);
    EXPECT_EQ(horse.OptionTag(0), 0x96);
    EXPECT_EQ(horse.OptionValue(0), 205 / 20 + 2 * 3 + 5);
    EXPECT_EQ(grid.world.Runtime().ownRecordDepth, 0);
    EXPECT_EQ(grid.cloth.random.State(), AfterDraws(Seed, 2 * OwnRecordClose).State());
}

// In the equipment, 0x5bc740 already has the record open: 0x4225d0 does not close it and draws no rand(). What follows
// 0x54bf40 (the helper and slot 8's effect) does not change the option.
TEST_F(InventoryReplyTest, TheWornDarkHorseGetsItsOptionWithTheRecordAlreadyOpen)
{
    Stage worn = Fresh();
    worn.PublishedHero();
    worn.world.OwnCharacter().agility = 40;

    const auto placed = Client::Items::PlaceInventoryItem(8, ItemBytes(DarkHorse), worn.Resources());

    if (!placed.HasValue())
    {
        EXPECT_EQ(placed.Error().find("0x4225d0"), std::string::npos) << placed.Error();
    }
    const ItemState& horse = worn.world.OwnCharacter().equipment[8];
    EXPECT_EQ(horse.id, DarkHorse);
    ASSERT_EQ(horse.optionCount, 1);
    EXPECT_EQ(horse.OptionTag(0), 0x96);
    EXPECT_EQ(horse.OptionValue(0), 40 / 20 + 5);
    EXPECT_EQ(worn.world.Runtime().ownRecordDepth, 0);
}

// 0x4225f2..0x422602: a 0x96 from the second option on gets the new value and the count does not change; one in the
// first does not count and another is added at the end (Q-C415). With the record open, no rand(); the value goes in a
// byte: (65535 / 20 = 3276) + 2 · 200 + 5 = 3681, which leaves 97.
TEST_F(InventoryReplyTest, TheDarkHorseOptionSkipsAnEarlierOneInTheFirstPlace)
{
    Stage tested = Fresh();
    tested.world.Runtime().ownRecordDepth = 1;
    tested.world.OwnCharacter().agility = 0xffff;
    tested.world.Runtime().pets[Client::Kernel::DarkHorsePetStats].level = 200;

    ItemState second;
    second.id = DarkHorse;
    second.optionCount = 2;
    second.optionBytes[0] = 0x3e;
    second.optionBytes[1] = 0x96;
    second.optionBytes[Client::Kernel::ItemOptionValueOffset + 1] = 99;
    Client::Items::AddTheDarkHorseOption(second, tested.Resources());
    EXPECT_EQ(second.optionCount, 2);
    EXPECT_EQ(second.OptionTag(1), 0x96);
    EXPECT_EQ(second.OptionValue(1), 97);

    ItemState first;
    first.id = DarkHorse;
    first.optionCount = 1;
    first.optionBytes[0] = 0x96;
    first.optionBytes[Client::Kernel::ItemOptionValueOffset] = 99;
    Client::Items::AddTheDarkHorseOption(first, tested.Resources());
    EXPECT_EQ(first.optionCount, 2);
    EXPECT_EQ(first.OptionValue(0), 99);
    EXPECT_EQ(first.OptionTag(1), 0x96);
    EXPECT_EQ(first.OptionValue(1), 97);

    EXPECT_EQ(tested.world.Runtime().ownRecordDepth, 1);
    EXPECT_EQ(tested.cloth.random.State(), CrtRandom(Seed).State());
}

// 0x5bc8cc: outside maps 18 to 23, slot 8's Guardian Angel is created with 0x443fa0(0x1c12, hero + 0x10, hero, 0, 0)
// in the table at 0x7d335d0, with the three rand() of its slot (0x443f21..0x443f8c) between the tail of 0x512a30 and
// the record close: the seed's rand() 5 to 7 (4116, 25814 and 28931) move it (4116 % 512 − 256; 25814 % 512 − 256;
// 28931 % 128 + 128). Without the effect 0x61e, which is not the 0x1c12's.
TEST_F(InventoryReplyTest, AGuardianAngelOfSlot8IsCreatedWithTheSharedRandom)
{
    Stage tested = Fresh();
    ActorState& hero = tested.PublishedHero();
    hero.position = {1000.0f, 2000.0f, 300.0f};

    const auto placed = Client::Items::PlaceInventoryItem(8, ItemBytes(GuardianAngel), tested.Resources());

    ASSERT_TRUE(placed.HasValue()) << placed.Error();
    EXPECT_EQ(hero.pet.model, ModelOf(GuardianAngel));
    const HelperState& angel = tested.world.Helpers()[0];
    EXPECT_TRUE(angel.alive);
    EXPECT_EQ(angel.type, 0x1c12u);
    EXPECT_EQ(angel.rider, PublishedSlot);
    EXPECT_EQ(angel.position, glm::vec3(764.0f, 1958.0f, 431.0f));
    EXPECT_EQ(tested.world.Runtime().ownRecordDepth, 0);
    EXPECT_EQ(tested.cloth.random.State(), AfterDraws(Seed, EquipmentEntry + 3).State());
}

// On maps 18 to 23 no pet is created, not even the raven.
TEST_F(InventoryReplyTest, InChaosCastleNeitherThePetNorTheRavenIsCreated)
{
    Stage tested = Fresh();
    tested.PublishedHero();
    tested.world.Runtime().map = ChaosCastleMap;

    const auto applied = ApplyInventory(ListOf({Entry(8, GuardianAngel), Entry(1, DarkRaven)}), tested.Resources());

    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(tested.world.OwnCharacter().equipment[8].id, GuardianAngel);
    EXPECT_EQ(tested.world.OwnCharacter().equipment[1].id, DarkRaven);
}

// 0x4c5e4f..0x4c5e61: before the entries, 0x443d40 with the published hero turns off its helpers; another rider's stay.
TEST_F(InventoryReplyTest, TheReplyTurnsOffTheHorseOfThePublishedHero)
{
    Stage tested = Fresh();
    tested.PublishedHero();
    constexpr std::size_t OtherRider = 7;
    auto& helpers = tested.world.Helpers();
    helpers[0] = HelperState{.alive = true, .type = Client::Pets::DarkHorseModel, .rider = PublishedSlot};
    helpers[1] = HelperState{.alive = true, .type = Client::Pets::DarkHorseModel, .rider = OtherRider};

    const auto applied = ApplyInventory(ListOf({Entry(12, Apple)}), tested.Resources());

    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_FALSE(helpers[0].alive);
    EXPECT_TRUE(helpers[1].alive);
}

// 0x5bc9d6..0x5bc9db: Horn of Doom copies +0x1b to +0x31b before creating the helper (the 0x14c with 2). Outside the
// safe zone (the hero's +0x3e2 at 0), 0x5bca46 creates the decal 0x61e/1 over the hero, with him as owner and no
// rand(); inside it, not.
TEST_F(InventoryReplyTest, HornOfDoomCopiesItsFlags1bAndSummonsOutsideTheSafeZone)
{
    for (const std::uint8_t safeZone : {std::uint8_t{0}, std::uint8_t{1}})
    {
        SCOPED_TRACE(static_cast<int>(safeZone));
        Stage tested = Fresh();
        ActorState& hero = tested.PublishedHero();
        hero.safeZone = safeZone;
        hero.position = {1000.0f, 2000.0f, 300.0f};
        hero.angles = {0.0f, 0.0f, 90.0f};
        hero.light = {0.25f, 0.5f, 0.75f};

        const auto placed =
            Client::Items::PlaceInventoryItem(8, ItemBytes(HornOfDoom, 0, 20, 0x02), tested.Resources());

        ASSERT_TRUE(placed.HasValue()) << placed.Error();
        EXPECT_EQ(hero.pet.flags1b, 0x02);
        EXPECT_TRUE(tested.world.Helpers()[0].alive);
        EXPECT_EQ(tested.world.Helpers()[0].type, 0x14cu);
        EXPECT_EQ(tested.cloth.random.State(), AfterDraws(Seed, EquipmentEntry).State());
        const auto& summon = tested.world.Map().effects.Effects()[0];
        EXPECT_EQ(summon.alive, safeZone == 0);
        if (safeZone == 0)
        {
            EXPECT_EQ(summon.type, Client::Effects::ColoredDecalEffect);
            EXPECT_EQ(summon.subtype, Client::Effects::PetSummonSubtype);
            EXPECT_EQ(summon.transform.position, hero.position);
            EXPECT_EQ(summon.transform.angles, hero.angles);
            EXPECT_EQ(summon.light, hero.light);
            EXPECT_EQ(summon.ownerContext, &hero);
            EXPECT_EQ(summon.life, 20);
        }
    }
}

// 0x5bca62: slot 1's Dark Raven calls 0x420cf0 with the published hero. It releases the raven it had and, outside maps
// 18 to 23, creates another (0x4167b0) at the center of its cell, 300 above the terrain, with the 12 rand() of the
// closes of its cells and of the counter. In Chaos Castle it only releases it, with no rand() beyond those of dressing
// with a shield in the same slot.
TEST_F(InventoryReplyTest, TheDarkRavenOfSlot1ReplacesTheRavenAt420cf0)
{
    constexpr int RavenRandoms = 3 * Client::Models::SmallLogicalCloseRandoms;
    const auto testedStage = FreshOnTheHeap();
    Stage& tested = *testedStage;
    tested.states.terrain.current.heights = std::make_unique<Client::Data::Terrain::TerrainHeights>();
    tested.states.terrain.current.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    ActorState& hero = tested.PublishedHero();
    hero.cellX = 133;
    hero.cellY = 127;
    hero.darkSpirit.emplace().state = 4;

    const auto placed = Client::Items::PlaceInventoryItem(1, ItemBytes(DarkRaven), tested.Resources());

    ASSERT_TRUE(placed.HasValue()) << placed.Error();
    EXPECT_EQ(hero.hands[1].model, ModelOf(DarkRaven));
    ASSERT_TRUE(hero.darkSpirit.has_value());
    EXPECT_EQ(hero.darkSpirit->state, 0);
    EXPECT_EQ(hero.darkSpirit->position, glm::vec3(13350.0f, 12750.0f, 300.0f));
    EXPECT_EQ(tested.world.Runtime().ownRecordDepth, 0);
    EXPECT_EQ(tested.cloth.random.State(), AfterDraws(Seed, EquipmentEntry + RavenRandoms).State());

    const auto chaosStage = FreshOnTheHeap();
    Stage& chaos = *chaosStage;
    ActorState& rider = chaos.PublishedHero();
    chaos.world.Runtime().map = ChaosCastleMap;
    rider.darkSpirit.emplace();

    const auto inChaos = Client::Items::PlaceInventoryItem(1, ItemBytes(DarkRaven), chaos.Resources());

    const auto shieldedStage = FreshOnTheHeap();
    Stage& shielded = *shieldedStage;
    shielded.PublishedHero();
    shielded.world.Runtime().map = ChaosCastleMap;
    const auto withShield = Client::Items::PlaceInventoryItem(1, ItemBytes(SmallShield), shielded.Resources());

    ASSERT_TRUE(inChaos.HasValue()) << inChaos.Error();
    ASSERT_TRUE(withShield.HasValue()) << withShield.Error();
    EXPECT_FALSE(rider.darkSpirit.has_value());
    EXPECT_EQ(chaos.cloth.random.State(), shielded.cloth.random.State());
}

// 0x5bca6c..0x5bcaa7: a level 3 Wizard's Ring in slot 11, or in 10 with +0x360 outside 1 to 3, releases the layer
// (0x414340) and leaves +0x360 at 4. At another level, or in 10 with +0x360 from 1 to 3, it does not touch it.
TEST_F(InventoryReplyTest, ALevelThreeWizardsRingLeavesTheLayerAtFour)
{
    for (const int ring : {10, 11})
    {
        SCOPED_TRACE(ring);
        const auto slot = static_cast<std::uint8_t>(ring);
        const auto testedStage = FreshOnTheHeap();
        Stage& tested = *testedStage;
        ActorState& hero = tested.PublishedHero();
        hero.layer.emplace();
        hero.byte360 = 5;
        const auto placed = Client::Items::PlaceInventoryItem(slot, ItemBytes(WizardsRing, 0x18), tested.Resources());
        ASSERT_TRUE(placed.HasValue()) << placed.Error();
        EXPECT_FALSE(hero.layer.has_value());
        EXPECT_EQ(hero.byte360, 4);

        const auto otherStage = FreshOnTheHeap();
        Stage& other = *otherStage;
        ActorState& plain = other.PublishedHero();
        plain.byte360 = 5;
        const auto levelTwo = Client::Items::PlaceInventoryItem(slot, ItemBytes(WizardsRing, 0x10), other.Resources());
        ASSERT_TRUE(levelTwo.HasValue()) << levelTwo.Error();
        EXPECT_EQ(plain.byte360, 5);
    }

    const auto eventStage = FreshOnTheHeap();
    Stage& event = *eventStage;
    ActorState& bloodCastle = event.PublishedHero();
    bloodCastle.byte360 = 2;
    const auto inTen = Client::Items::PlaceInventoryItem(10, ItemBytes(WizardsRing, 0x18), event.Resources());
    ASSERT_TRUE(inTen.HasValue()) << inTen.Error();
    EXPECT_EQ(bloodCastle.byte360, 2);
}

// Without Item.bmd, the grid cuts where it reads the width (0x5bcc33) and the equipment at 0x54bf40, before 0x512a30.
TEST_F(InventoryReplyTest, WithoutItemBmdBothPathsStop)
{
    Stage tested(m_dataRoot, nullptr);
    tested.PublishedHero();

    const auto grid = Client::Items::PlaceInventoryItem(12, ItemBytes(Apple), tested.Resources());
    const auto worn = Client::Items::PlaceInventoryItem(0, ItemBytes(Kris), tested.Resources());

    ASSERT_FALSE(grid.HasValue());
    EXPECT_NE(grid.Error().find("0x7758550"), std::string::npos) << grid.Error();
    ASSERT_FALSE(worn.HasValue());
    EXPECT_NE(worn.Error().find("0x54bf40"), std::string::npos) << worn.Error();
    EXPECT_EQ(tested.world.OwnCharacter().equipment[0].id, Kris);
    EXPECT_EQ(tested.world.Actors()[PublishedSlot].hands[0].model, Client::Kernel::NoItemModel);
}

// Q-C204: 0x5bc740 dresses the published hero (0x743a170), not the one at WorldRuntime::heroSlot. Before the F3 03
// reply it is WinMain's record 0, which is not a player: 0x512a30 does nothing to it.
TEST_F(InventoryReplyTest, BeforeTheEntryTheEquipmentGoesToTheRecordOnly)
{
    Stage tested = Fresh();
    tested.world.Runtime().heroSlot = 7;
    tested.world.Actors()[7] = Client::Tests::UnarmedPlayer();

    const auto applied = ApplyInventory(ListOf({Entry(0, Kris)}), tested.Resources());

    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(tested.world.OwnCharacter().equipment[0].id, Kris);
    EXPECT_EQ(tested.world.Actors()[7].hands[0].model, Client::Kernel::NoItemModel);
    EXPECT_EQ(tested.world.Actors()[0].hands[0].model, 0);
    EXPECT_EQ(tested.cloth.random.State(), AfterDraws(Seed, 2 * OwnRecordClose).State());
}

// The wire's F3 03 reply (cell (133, 127) of map 0) and the F3 10 in the same segment, with five entries in the wire's
// slots (0, 12, 14, 17 and 23; 04, "El F3 10") and synthetic bytes: in a single Poll the entry publishes the hero and
// the F3 10 dresses him (P-C28 a). What a zero-step frame does waits for T8's game frame.
TEST_F(InventoryReplyTest, TheFiveCableSlotsDressTheHeroInTheSamePoll)
{
    Stage tested = Fresh();
    Client::Scenes::SessionEventApplier applier(tested.Resources());
    Client::Tests::LoggedInSession online(&applier);
    ASSERT_NO_FATAL_FAILURE(online.LogIn());
    // The F3 03 reply: the header and the cell; the rest at 0 (map 0 and direction 0).
    std::vector<std::byte> entry(0x34);
    const std::array head{std::byte{0xC1}, std::byte{0x34}, std::byte{0xF3},
                          std::byte{0x03}, std::byte{133},  std::byte{127}};
    std::ranges::copy(head, entry.begin());
    const std::array cable{Entry(0, Kris, 0, 20), Entry(12, SmallShield), Entry(14, Apple), Entry(17, JewelOfBless),
                           Entry(23, GuardianHelm)};
    std::vector<std::byte> inventory{std::byte{0xC2}, std::byte{0x00},
                                     std::byte{0x00}, std::byte{0xF3},
                                     std::byte{0x10}, std::byte{static_cast<std::uint8_t>(cable.size())}};
    for (const InventoryEntry& item : cable)
    {
        inventory.push_back(std::byte{item.slot});
        inventory.insert(inventory.end(), item.item.begin(), item.item.end());
    }
    inventory[2] = static_cast<std::byte>(inventory.size());
    Client::Network::PacketSerials serials;
    std::vector<std::byte> segment = Client::Tests::FromTheGameServer(entry, serials);
    const std::vector<std::byte> second = Client::Tests::FromTheGameServer(inventory, serials);
    segment.insert(segment.end(), second.begin(), second.end());
    online.socket.reads = {segment};

    static_cast<void>(online.session.Poll());

    ASSERT_EQ(online.session.State(), Client::Session::SessionState::InWorld);
    const Client::Kernel::WorldRuntime& runtime = tested.world.Runtime();
    ASSERT_EQ(runtime.hero, static_cast<std::size_t>(runtime.heroSlot));
    const ActorState& hero = tested.world.Hero();
    EXPECT_EQ(hero.hands[0].model, ModelOf(Kris));
    EXPECT_EQ(tested.world.OwnCharacter().equipment[0].id, Kris);
    EXPECT_EQ(tested.world.OwnCharacter().equipment[1].id, EmptyOwnItemId);
    EXPECT_EQ(tested.Cell(0).id, SmallShield);
    EXPECT_EQ(tested.Cell(9).id, SmallShield);
    EXPECT_EQ(tested.Cell(2).id, Apple);
    EXPECT_EQ(tested.Cell(5).id, JewelOfBless);
    EXPECT_EQ(tested.Cell(11).id, GuardianHelm);
    EXPECT_EQ(tested.Cell(20).id, GuardianHelm);
    EXPECT_EQ(tested.Cell(3).id, EmptyOwnItemId);
    EXPECT_EQ(runtime.ownRecordDepth, 0);
    // The entry with 0x512a30 whole (WorldEntryTest), and the F3 10 with everything closed: the common close, slot 0's
    // with what 0x512a30 does inside, and one per grid (P-C42).
    constexpr int GrassRandoms = 65536;
    constexpr int HeroRandom = 1;
    constexpr int ConstructionRandoms = 28;
    constexpr int WaitClose = 4;
    constexpr int WindowRandoms = 2;
    constexpr int TheEntry = 4 * OwnRecordClose + GrassRandoms + HeroRandom + ConstructionRandoms + StopRandom +
                             TailRandoms + WaitClose + WindowRandoms + StopRandom;
    constexpr int TheInventory = OwnRecordClose + EquipmentEntry + 4 * OwnRecordClose;
    EXPECT_EQ(tested.cloth.random.State(), AfterDraws(Seed, TheEntry + TheInventory).State());
}

// 0x4d1400, the 24 encrypted with 0: releases the lock, sets the cursor count to 0, puts the item in the packet's slot
// (0x5bc740 with the last argument at 0) and requests sound 0x1d. With 0xff, 0x5be150 returns the cursor's item to its
// slot.
TEST_F(InventoryReplyTest, TheItemMoveResultPlacesTheItemOrPutsItBack)
{
    const std::unique_ptr<Stage> testedOnHeap = FreshOnTheHeap();
    Stage& tested = *testedOnHeap;
    tested.PublishedHero();
    Client::Kernel::WorldRuntime& runtime = tested.world.Runtime();
    runtime.ui.field78abb39 = 1;
    runtime.cursorItem.id = Apple;
    runtime.cursorItem.occupied = 1;
    runtime.cursorItemCount = 1;
    const auto moved = Client::Items::ApplyItemMoveResult(
        Client::Protocol::Items::ItemMoveResult{.result = 0, .slot = 12 + 9, .item = ItemBytes(Apple)},
        tested.Resources());
    ASSERT_TRUE(moved.HasValue()) << moved.Error();
    EXPECT_EQ(runtime.ui.field78abb39, 0);
    EXPECT_EQ(runtime.cursorItemCount, 0);
    EXPECT_EQ(tested.Cell(9).id, Apple);
    EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{0x1d});

    Stage refused = Fresh();
    refused.PublishedHero();
    Client::Kernel::WorldRuntime& back = refused.world.Runtime();
    back.ui.field78abb39 = 1;
    back.cursorItem.id = Apple;
    back.cursorItem.gridColumn = 1;
    back.cursorItem.occupied = 1;
    back.cursorItemCount = 1;
    back.cursorGrid = Client::Kernel::CursorGrid::Inventory;
    back.slot78a6758 = 12 + 1;
    const auto rejected = Client::Items::ApplyItemMoveResult(
        Client::Protocol::Items::ItemMoveResult{.result = 0xff, .slot = 0, .item = {}}, refused.Resources());
    ASSERT_TRUE(rejected.HasValue()) << rejected.Error();
    EXPECT_EQ(back.ui.field78abb39, 0);
    EXPECT_EQ(back.cursorItemCount, 0);
    EXPECT_EQ(refused.Cell(1).id, Apple);
    EXPECT_EQ(refused.Cell(1).occupied, 1u);
}

// 0x4c5ec0 (28): 0x5bd750 with the slot and, with pkt[4], the end of the wait; from 0x4c, the shop grid.
// 0x4d3cc0 (2A): the durability of the equipment slot, with its record access, or of the grid's whole footprint.
TEST_F(InventoryReplyTest, TheDeleteAndTheDurabilityTouchTheirPlace)
{
    Stage tested = Fresh();
    tested.PublishedHero();
    ASSERT_TRUE(Client::Items::PlaceInventoryItem(12 + 2, ItemBytes(ShortSword), tested.Resources()).HasValue());
    ASSERT_TRUE(Client::Items::PlaceInventoryItem(12 + 6, ItemBytes(Apple), tested.Resources()).HasValue());
    Client::Kernel::WorldRuntime& runtime = tested.world.Runtime();

    const auto worn = Client::Items::ApplyDurability(
        Client::Protocol::Items::Durability{.slot = 12 + 2, .durability = 7, .endsTheWait = 0}, tested.Resources());
    ASSERT_TRUE(worn.HasValue()) << worn.Error();
    for (const std::size_t cell : {2u, 10u, 18u})
        EXPECT_EQ(tested.Cell(cell).durability, 7) << cell;
    EXPECT_NE(tested.Cell(6).durability, 7);

    tested.world.OwnCharacter().equipment[3].id = GuardianHelm;
    const CrtRandom before = tested.cloth.random;
    runtime.ui.field5767954 = 10;
    const auto helm = Client::Items::ApplyDurability(
        Client::Protocol::Items::Durability{.slot = 3, .durability = 9, .endsTheWait = 1}, tested.Resources());
    ASSERT_TRUE(helm.HasValue()) << helm.Error();
    EXPECT_EQ(tested.world.OwnCharacter().equipment[3].durability, 9);
    EXPECT_EQ(runtime.ui.field5767954, 0);
    CrtRandom expected = before;
    expected.Discard(OwnRecordClose);
    EXPECT_EQ(tested.cloth.random.State(), expected.State());

    runtime.ui.field5767954 = 10;
    const auto deleted = Client::Items::ApplyItemDelete(
        Client::Protocol::Items::ItemDelete{.slot = 12 + 6, .endsTheWait = 1}, tested.Resources());
    ASSERT_TRUE(deleted.HasValue()) << deleted.Error();
    EXPECT_EQ(tested.Cell(6).id, EmptyOwnItemId);
    EXPECT_EQ(runtime.ui.field5767954, 0);

    // From 0x4c, without looking at another's shop (0x6c931c at −1): the shop grid (0x78ab0b8).
    ASSERT_TRUE(Client::Items::PlaceInventoryItem(0x4c + 1, ItemBytes(Apple), tested.Resources()).HasValue());
    const auto shop = Client::Items::ApplyItemDelete(
        Client::Protocol::Items::ItemDelete{.slot = 0x4c + 1, .endsTheWait = 0}, tested.Resources());
    ASSERT_TRUE(shop.HasValue()) << shop.Error();
    EXPECT_EQ(tested.world.Inventory().secondCells[1].id, EmptyOwnItemId);
}

// 0x4d3510 (26): each one with the own record open, 3848 rand() on closing it; 0xfe the max life, 0xff the life, 0xfd
// the end of the wait and any other slot the potion: it drops by 1 and at 0 leaves the cell empty. Outside the grid, it
// cuts after opening the record, which closes anyway. With the record already open it spends nothing.
TEST_F(InventoryReplyTest, TheLifeRefillWritesTheRecordOrSpendsThePotion)
{
    const std::unique_ptr<Stage> tested = FreshOnTheHeap();
    tested->PublishedHero();
    ASSERT_TRUE(Client::Items::PlaceInventoryItem(12 + 6, ItemBytes(Apple, 0, 2), tested->Resources()).HasValue());
    Client::Kernel::WorldState& world = tested->world;
    Client::Kernel::WorldRuntime& runtime = world.Runtime();
    CrtRandom& random = tested->cloth.random;
    namespace Inventory = Client::Protocol::Items;
    const auto refill = [&](std::uint8_t place, std::uint16_t life)
    { return Client::Items::ApplyLifeRefill(Inventory::LifeRefill{.place = place, .life = life}, world, random); };

    CrtRandom expected = random;
    ASSERT_TRUE(refill(Inventory::MaximumPlace, 300).HasValue());
    ASSERT_TRUE(refill(Inventory::CurrentPlace, 120).HasValue());
    expected.Discard(2 * OwnRecordClose);
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(world.OwnCharacter().maxLife, 300);
    EXPECT_EQ(world.OwnCharacter().life, 120);

    runtime.ui.field5767954 = 10;
    ASSERT_TRUE(refill(Inventory::EndTheWaitPlace, 0).HasValue());
    EXPECT_EQ(runtime.ui.field5767954, 0);
    EXPECT_EQ(world.OwnCharacter().life, 120);

    ASSERT_TRUE(refill(12 + 6, 0).HasValue());
    EXPECT_EQ(tested->Cell(6).id, Apple);
    EXPECT_EQ(tested->Cell(6).durability, 1);
    ASSERT_TRUE(refill(12 + 6, 0).HasValue());
    EXPECT_EQ(tested->Cell(6).id, EmptyOwnItemId);
    EXPECT_EQ(tested->Cell(6).occupied, 0u);
    EXPECT_EQ(tested->Cell(6).durability, 0);

    expected = random;
    const auto outside = refill(12 + 64, 0);
    ASSERT_FALSE(outside.HasValue());
    EXPECT_TRUE(outside.Error().starts_with("0x4d35f6:")) << outside.Error();
    expected.Discard(OwnRecordClose);
    EXPECT_EQ(random.State(), expected.State());

    expected = random;
    {
        const Client::Models::LogicalAccess open(runtime.ownRecordDepth, random, Client::Models::OwnRecordCloseRandoms);
        ASSERT_TRUE(refill(Inventory::CurrentPlace, 80).HasValue());
        EXPECT_EQ(random.State(), expected.State());
    }
    EXPECT_EQ(world.OwnCharacter().life, 80);
}

// 0x4d3820 (27): 0xfe the max mana and BP, 0xff the current ones and any other slot the mana and the potion; 0xfd is
// one more slot, outside the grid, and cuts after writing the mana.
TEST_F(InventoryReplyTest, TheManaRefillWritesTheManaAndTheBpOrSpendsThePotion)
{
    const std::unique_ptr<Stage> tested = FreshOnTheHeap();
    tested->PublishedHero();
    ASSERT_TRUE(Client::Items::PlaceInventoryItem(12 + 9, ItemBytes(Apple, 0, 1), tested->Resources()).HasValue());
    Client::Kernel::WorldState& world = tested->world;
    CrtRandom& random = tested->cloth.random;
    namespace Inventory = Client::Protocol::Items;
    const auto refill = [&](std::uint8_t place, std::uint16_t mana, std::uint16_t bp)
    {
        return Client::Items::ApplyManaRefill(Inventory::ManaRefill{.place = place, .mana = mana, .bp = bp}, world,
                                              random);
    };
    const Client::Kernel::OwnCharacterRecord& own = world.OwnCharacter();

    CrtRandom expected = random;
    ASSERT_TRUE(refill(Inventory::MaximumPlace, 200, 40).HasValue());
    ASSERT_TRUE(refill(Inventory::CurrentPlace, 150, 30).HasValue());
    expected.Discard(2 * OwnRecordClose);
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(own.maxMana, 200);
    EXPECT_EQ(own.maxBp, 40);
    EXPECT_EQ(own.mana, 150);
    EXPECT_EQ(own.bp, 30);

    ASSERT_TRUE(refill(12 + 9, 190, 7).HasValue());
    EXPECT_EQ(own.mana, 190);
    EXPECT_EQ(own.bp, 30);
    EXPECT_EQ(tested->Cell(9).id, EmptyOwnItemId);
    EXPECT_EQ(tested->Cell(9).occupied, 0u);

    world.Runtime().ui.field5767954 = 10;
    const auto wait = refill(Inventory::EndTheWaitPlace, 170, 0);
    ASSERT_FALSE(wait.HasValue());
    EXPECT_TRUE(wait.Error().starts_with("0x4d3917:")) << wait.Error();
    EXPECT_EQ(own.mana, 170);
    EXPECT_EQ(world.Runtime().ui.field5767954, 10);
}

// 0x4e316e: the 16-bit key with no mask and the 17 bytes reach the same 0x5139f0 as the viewport.
TEST_F(InventoryReplyTest, TheEquipmentChangeUpdatesPlayersAndTheSentinelOnlyWhenTheirTypeMatches)
{
    Stage tested = Fresh();
    ActorState& hero = tested.PublishedHero();
    hero.key = 0x1234;
    hero.byte3e3 = 1;
    Client::Protocol::Items::EquipmentChange change{.key = 0x9234};
    change.equipment[0] = std::byte{7};
    change.equipment[4] = std::byte{3}; // No pet or helper.
    // 0x5110d0 compares the MOVSX of the key's WORD: 0x9234 does not match an actor with key 0x1234.
    ASSERT_TRUE(Client::Items::ApplyEquipmentChange(change, tested.Resources()).HasValue());
    EXPECT_EQ(hero.hands[0].model, Client::Kernel::NoItemModel);
    change.key = 0x1234;
    const auto changed = Client::Items::ApplyEquipmentChange(change, tested.Resources());
    ASSERT_TRUE(changed.HasValue()) << changed.Error();
    EXPECT_EQ(hero.hands[0].model, 0x219);
    EXPECT_EQ(hero.pet.model, Client::Kernel::NoItemModel);
    hero.type = 0;
    hero.hands[0].model = 99;
    ASSERT_TRUE(Client::Items::ApplyEquipmentChange(change, tested.Resources()).HasValue());
    EXPECT_EQ(hero.hands[0].model, 99);
    ActorState& sentinel = tested.world.Actors()[400];
    sentinel = Client::Tests::UnarmedPlayer();
    sentinel.byte3e3 = 1;
    change.key = 0x4321;
    change.equipment[0] = std::byte{9};
    ASSERT_TRUE(Client::Items::ApplyEquipmentChange(change, tested.Resources()).HasValue());
    EXPECT_EQ(sentinel.hands[0].model, 0x21b);
}

// 0x4e31a3 (F3 14): the cursor count at 0, the item in its slot and sound 0x45. 0x4e316e (F3 13): without a player
// with the key, 0x5139f0 does nothing; with one, it applies the 17 bytes like the viewport.
TEST_F(InventoryReplyTest, TheItemPutAndTheEquipmentOfOthers)
{
    Stage tested = Fresh();
    ActorState& hero = tested.PublishedHero();
    Client::Kernel::WorldRuntime& runtime = tested.world.Runtime();
    runtime.cursorItemCount = 1;
    const auto put = Client::Items::ApplyItemPut(
        Client::Protocol::Items::ItemPut{.slot = 12 + 4, .item = ItemBytes(Apple)}, tested.Resources());
    ASSERT_TRUE(put.HasValue()) << put.Error();
    EXPECT_EQ(runtime.cursorItemCount, 0);
    EXPECT_EQ(tested.Cell(4).id, Apple);
    EXPECT_EQ(runtime.lastSoundId, Client::Audio::SoundId{0x45});

    hero.key = 0x1234;
    const auto nobody = Client::Items::ApplyEquipmentChange(
        Client::Protocol::Items::EquipmentChange{.key = 0x4321, .equipment = {}}, tested.Resources());
    EXPECT_TRUE(nobody.HasValue()) << nobody.Error();
    const auto player = Client::Items::ApplyEquipmentChange(
        Client::Protocol::Items::EquipmentChange{.key = 0x1234, .equipment = {}}, tested.Resources());
    ASSERT_TRUE(player.HasValue()) << player.Error();
    EXPECT_EQ(hero.pet.model, 0x1c12);
    EXPECT_TRUE(std::ranges::any_of(tested.world.Helpers(), [](const HelperState& helper) { return helper.alive; }));
}
} // namespace

// Q-C102: the same empty pixels, but two calls to 0x509340 add four rand() that model 0xffff skips.
TEST_F(InventoryReplyTest, EncodedEmptyHandsReachTheHangingConsumerWithTheirNativeRandoms)
{
    for (const bool upgraded : {false, true})
    {
        Stage tested = Fresh();
        ActorState& hero = tested.PublishedHero();
        hero.key = 0x1234;
        Client::Protocol::Items::EquipmentChange change{.key = 0x1234};
        for (std::size_t index = 0; index < change.equipment.size(); ++index)
            change.equipment[index] = static_cast<std::byte>(Client::Tests::NakedEquipment[index]);
        if (upgraded)
        {
            change.equipment[7] = std::byte{0x3f};
            change.equipment[9] = std::byte{6};
            change.equipment[10] = std::byte{6};
        }
        const auto changed = Client::Items::ApplyEquipmentChange(change, tested.Resources());
        ASSERT_TRUE(changed.HasValue()) << changed.Error();
        EXPECT_EQ(hero.hands[0].model, 0x1211);
        EXPECT_EQ(hero.hands[1].model, 0x1211);
        EXPECT_EQ(hero.hands[0].level, upgraded ? 13 : 0);
        ASSERT_EQ(tested.models.Find(Client::Models::ModelId{0x1211}), nullptr);
        Client::Render::ModelBatch batch;
        Client::Kernel::PoseBuffer carry;
        auto sprites = std::make_unique<Client::Particles::FrameSprites>();
        std::array<Client::Math::BoneTransform, 64> bones;
        bones.fill(Client::Math::BoneTransform(1.0f));
        const auto before = tested.cloth.random;
        const auto drawn = Client::Actors::Drawing::DrawGameHanging(hero, {.batch = batch,
                                                                           .models = tested.models,
                                                                           .body = bones,
                                                                           .random = tested.cloth.random,
                                                                           .carry = &carry,
                                                                           .sprites = sprites.get(),
                                                                           .logicSteps = 1});
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_TRUE(batch.Vertices().empty());
        EXPECT_EQ(tested.cloth.random.State(), AfterDraws(before.State(), 7).State());
        EXPECT_FALSE(std::ranges::any_of(sprites->Sprites(), [](const auto& sprite) { return sprite.alive; }));
    }
}
