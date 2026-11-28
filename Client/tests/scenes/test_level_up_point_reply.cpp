#include "Player/LevelUpPointReply.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Tables/ItemSetTables.h"
#include "Tables/ItemTable.h"
#include "Protocol/Player/LevelUpPoint.h"
#include "Models/Cloth.h"
#include "Models/LogicalAccess.h"
#include "FrontEnd/LoginModels.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Player/OwnDerivedStats.h"
#include "Kernel/OwnInventory.h"
#include "Player/OwnStatHelpers.h"
#include "FrontEnd/SelectionState.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Camera/GameCamera.h"
#include "support/ItemFiles.h"
#include "support/ItemSetFiles.h"
#include "support/PlayerActors.h"
#include "support/RecordingSink.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

// The F3 06 reply (0x4d3200), the level-up point the server accepted or not: what it writes in the own record and
// 0x550d10, with each closing's rand() calls.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::OwnCharacterRecord;
using Client::Protocol::Player::LevelUpPointResult;
using Client::Scenes::WorldEntryResources;

constexpr std::uint32_t Seed = 0x2468;
constexpr int Own = Client::Models::OwnRecordCloseRandoms;
constexpr std::size_t HeroSlot = 7;

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
        OwnCharacterRecord& own = world.OwnCharacter();
        own.levelUpPoints = 10;
        own.strength = 20;
        own.agility = 21;
        own.vitality = 22;
        own.energy = 23;
        own.leadership = 24;
        own.maxLife = 100;
        own.maxMana = 50;
        own.maxBp = 30;
    }

    [[nodiscard]] WorldEntryResources Resources()
    {
        return WorldEntryResources{.world = world,
                                   .selection = selection,
                                   .dataRoot = std::filesystem::path{},
                                   .cloth = cloth,
                                   .camera = camera,
                                   .cameraInput = cameraInput,
                                   .models = models,
                                   .items = &items,
                                   .itemSetTables = &sets,
                                   .log = log};
    }

    Client::Kernel::WorldState world;
    Client::Scenes::SelectionState selection{world};
    Client::Camera::GameCamera camera;
    Client::Camera::GameCameraInput cameraInput;
    Client::Models::ClothWorld cloth{.random = CrtRandom(Seed)};
    Client::Scenes::SingleModelLibrary models;
    Client::Tables::ItemTable items = Client::Tests::ItemTableOf({});
    Client::Tables::ItemSetTables sets = Client::Tests::ItemSetTablesFrom(
        std::vector<std::byte>(Client::Tables::ItemSetTypeTableSize, std::byte{0xff}),
        std::vector<std::byte>(Client::Tables::ItemSetOptionTableSize, std::byte{0xff}));
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
};

// 0x4d32d8..0x4d334d: the points minus 1 and the low nibble's stat plus 1 (0x4d34f4's jump table); the vitality sets
// the max life from pkt[6..7] (0x4d332d) and the energy the max mana (0x4d333b), without touching the current life or
// mana; all five, the max BP from pkt[8..9] (0x4d3356).
TEST(LevelUpPointReplyTest, EachStatTakesTheDistributedPoint)
{
    struct Case
    {
        std::uint8_t stat = 0;
        std::uint16_t OwnCharacterRecord::*field = nullptr;
    };
    const std::array<Case, 5> cases{{{0, &OwnCharacterRecord::strength},
                                     {1, &OwnCharacterRecord::agility},
                                     {2, &OwnCharacterRecord::vitality},
                                     {3, &OwnCharacterRecord::energy},
                                     {4, &OwnCharacterRecord::leadership}}};
    for (const Case& tested : cases)
    {
        auto stage = std::make_unique<Stage>();
        OwnCharacterRecord& own = stage->world.OwnCharacter();
        own.life = 60;
        own.mana = 40;
        const std::uint16_t before = own.*tested.field;
        const auto applied = Client::Player::ApplyLevelUpPointResult(
            LevelUpPointResult{
                .result = static_cast<std::uint8_t>(0x10 | tested.stat), .maxLifeOrMana = 777, .maxBp = 88},
            stage->Resources());
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(own.levelUpPoints, 9) << int{tested.stat};
        EXPECT_EQ(own.*tested.field, before + 1) << int{tested.stat};
        EXPECT_EQ(own.maxLife, tested.stat == 2 ? 777 : 100) << int{tested.stat};
        EXPECT_EQ(own.maxMana, tested.stat == 3 ? 777 : 50) << int{tested.stat};
        EXPECT_EQ(own.maxBp, 88) << int{tested.stat};
        EXPECT_EQ(own.life, 60);
        EXPECT_EQ(own.mana, 40);
        EXPECT_EQ(stage->world.Runtime().ownRecordDepth, 0);
    }
}

// 0x4d32df: a low nibble above 4 skips the switch (JA to 0x4d334d), but the points already dropped (0x4d32d8, before
// the switch) and the max BP is written anyway. The points are a word: from 0 they wrap to 0xffff.
TEST(LevelUpPointReplyTest, ANibbleAboveFourOnlyTakesThePointAndTheBp)
{
    auto stage = std::make_unique<Stage>();
    OwnCharacterRecord& own = stage->world.OwnCharacter();
    own.levelUpPoints = 0;
    ASSERT_TRUE(
        Client::Player::ApplyLevelUpPointResult({.result = 0x1f, .maxLifeOrMana = 777, .maxBp = 88}, stage->Resources())
            .HasValue());
    EXPECT_EQ(own.levelUpPoints, 0xffff);
    EXPECT_EQ(own.strength, 20);
    EXPECT_EQ(own.agility, 21);
    EXPECT_EQ(own.vitality, 22);
    EXPECT_EQ(own.energy, 23);
    EXPECT_EQ(own.leadership, 24);
    EXPECT_EQ(own.maxLife, 100);
    EXPECT_EQ(own.maxMana, 50);
    EXPECT_EQ(own.maxBp, 88);
}

// 0x4d341a..0x4d34ed's sequence on another equal Stage: open the record, 0x550d10 and close it. 0x550d10 draws its own
// rand() calls (the other protected tables it reads), so the reference is running it, not counting them.
void OpenRecalculateAndClose(Stage& stage)
{
    Client::Kernel::WorldRuntime& runtime = stage.world.Runtime();
    const Client::Models::LogicalAccess access(runtime.ownRecordDepth, stage.cloth.random, Own);
    const auto recalculated = Client::Player::RecalculateOwnStats(
        stage.world.OwnCharacter(), Client::Player::OwnStatsContext{.itemSetTables = &stage.sets,
                                                                    .itemSets = &stage.world.ItemSets(),
                                                                    .items = &stage.items,
                                                                    .publishedHero = stage.world.Hero(),
                                                                    .ownRecordAccesses = runtime.ownRecordDepth,
                                                                    .random = stage.cloth.random,
                                                                    .held = Client::Player::HeldItemOf(runtime),
                                                                    .scan505820 = runtime.fullSetScan});
    ASSERT_TRUE(recalculated.HasValue()) << recalculated.Error();
}

// 0x4d320c: without the high nibble (the server did not add the point) it does not touch the record, but
// 0x4d341a..0x4d34ed opens it, runs 0x550d10 and closes it anyway. With the point added, first 0x4d3216..0x4d3415's
// opening and closing: with no other access open, 3,848 more rand() calls.
TEST(LevelUpPointReplyTest, TheStatsAreRecalculatedEvenWithoutThePoint)
{
    auto stage = std::make_unique<Stage>();
    auto reference = std::make_unique<Stage>();
    OwnCharacterRecord& own = stage->world.OwnCharacter();
    ASSERT_TRUE(
        Client::Player::ApplyLevelUpPointResult({.result = 0x00, .maxLifeOrMana = 777, .maxBp = 88}, stage->Resources())
            .HasValue());
    ASSERT_NO_FATAL_FAILURE(OpenRecalculateAndClose(*reference));
    EXPECT_EQ(stage->cloth.random.State(), reference->cloth.random.State());
    EXPECT_EQ(own.levelUpPoints, 10);
    EXPECT_EQ(own.strength, 20);
    EXPECT_EQ(own.maxBp, 30);
    EXPECT_EQ(stage->world.Runtime().ownRecordDepth, 0);

    ASSERT_TRUE(Client::Player::ApplyLevelUpPointResult({.result = 0x10, .maxBp = 31}, stage->Resources()).HasValue());
    reference->cloth.random.Discard(static_cast<std::uint32_t>(Own));
    ASSERT_NO_FATAL_FAILURE(OpenRecalculateAndClose(*reference));
    EXPECT_EQ(stage->cloth.random.State(), reference->cloth.random.State());
    EXPECT_EQ(own.strength, 21);
}
} // namespace
