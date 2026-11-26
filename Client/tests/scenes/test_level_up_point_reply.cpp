#include "Scenes/LevelUpPointReply.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Tables/ItemSetTables.h"
#include "Tables/ItemTable.h"
#include "Protocol/Character/LevelUpPoint.h"
#include "Models/Cloth.h"
#include "Models/LogicalAccess.h"
#include "Scenes/LoginModels.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnDerivedStats.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/OwnStatHelpers.h"
#include "Scenes/SelectionState.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"
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

// La respuesta del F3 06 (0x4d3200), el punto de subir de nivel que el servidor aceptó o no: lo que escribe en el
// registro propio y 0x550d10, con los rand() de cada cierre.
namespace
{
using Client::Crt::CrtRandom;
using Client::Protocol::Character::LevelUpPointResult;
using Client::Scenes::OwnCharacterRecord;
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

    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection{world};
    Client::World::GameCamera camera;
    Client::World::GameCameraInput cameraInput;
    Client::Models::ClothWorld cloth{.random = CrtRandom(Seed)};
    Client::Scenes::SingleModelLibrary models;
    Client::Tables::ItemTable items = Client::Tests::ItemTableOf({});
    Client::Tables::ItemSetTables sets = Client::Tests::ItemSetTablesFrom(
        std::vector<std::byte>(Client::Tables::ItemSetTypeTableSize, std::byte{0xff}),
        std::vector<std::byte>(Client::Tables::ItemSetOptionTableSize, std::byte{0xff}));
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
};

// 0x4d32d8..0x4d334d: los puntos menos 1 y la estadística del nibble bajo más 1 (la jump table de 0x4d34f4); la
// vitalidad pone la vida máxima de pkt[6..7] (0x4d332d) y la energía el maná máximo (0x4d333b), sin tocar la vida ni
// el maná actuales; las cinco, el BP máximo de pkt[8..9] (0x4d3356).
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
        const auto applied = Client::Scenes::ApplyLevelUpPointResult(
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

// 0x4d32df: un nibble bajo de más de 4 salta el switch (JA a 0x4d334d), pero los puntos ya bajaron (0x4d32d8, antes
// del switch) y el BP máximo se escribe igual. Los puntos son un word: de 0 dan la vuelta a 0xffff.
TEST(LevelUpPointReplyTest, ANibbleAboveFourOnlyTakesThePointAndTheBp)
{
    auto stage = std::make_unique<Stage>();
    OwnCharacterRecord& own = stage->world.OwnCharacter();
    own.levelUpPoints = 0;
    ASSERT_TRUE(
        Client::Scenes::ApplyLevelUpPointResult({.result = 0x1f, .maxLifeOrMana = 777, .maxBp = 88}, stage->Resources())
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

// La secuencia de 0x4d341a..0x4d34ed sobre otro Stage igual: abrir el registro, 0x550d10 y cerrarlo. 0x550d10 saca sus
// propios rand() (las otras tablas protegidas que lee), así que la referencia es correrla, no contarlos.
void OpenRecalculateAndClose(Stage& stage)
{
    Client::Scenes::WorldRuntime& runtime = stage.world.Runtime();
    const Client::Models::LogicalAccess access(runtime.ownRecordDepth, stage.cloth.random, Own);
    const auto recalculated = Client::Scenes::RecalculateOwnStats(
        stage.world.OwnCharacter(), Client::Scenes::OwnStatsContext{.itemSetTables = &stage.sets,
                                                                    .itemSets = &stage.world.ItemSets(),
                                                                    .items = &stage.items,
                                                                    .publishedHero = stage.world.Actors()[runtime.hero],
                                                                    .ownRecordAccesses = runtime.ownRecordDepth,
                                                                    .random = stage.cloth.random,
                                                                    .held = Client::Scenes::HeldItemOf(runtime),
                                                                    .scan505820 = runtime.fullSetScan});
    ASSERT_TRUE(recalculated.HasValue()) << recalculated.Error();
}

// 0x4d320c: sin el nibble alto (el servidor no sumó el punto) no toca el registro, pero 0x4d341a..0x4d34ed lo abre,
// corre 0x550d10 y lo cierra igual. Con el punto sumado, antes, la apertura y el cierre de 0x4d3216..0x4d3415: sin otro
// acceso abierto, 3.848 rand() más.
TEST(LevelUpPointReplyTest, TheStatsAreRecalculatedEvenWithoutThePoint)
{
    auto stage = std::make_unique<Stage>();
    auto reference = std::make_unique<Stage>();
    OwnCharacterRecord& own = stage->world.OwnCharacter();
    ASSERT_TRUE(
        Client::Scenes::ApplyLevelUpPointResult({.result = 0x00, .maxLifeOrMana = 777, .maxBp = 88}, stage->Resources())
            .HasValue());
    ASSERT_NO_FATAL_FAILURE(OpenRecalculateAndClose(*reference));
    EXPECT_EQ(stage->cloth.random.State(), reference->cloth.random.State());
    EXPECT_EQ(own.levelUpPoints, 10);
    EXPECT_EQ(own.strength, 20);
    EXPECT_EQ(own.maxBp, 30);
    EXPECT_EQ(stage->world.Runtime().ownRecordDepth, 0);

    ASSERT_TRUE(Client::Scenes::ApplyLevelUpPointResult({.result = 0x10, .maxBp = 31}, stage->Resources()).HasValue());
    reference->cloth.random.Discard(static_cast<std::uint32_t>(Own));
    ASSERT_NO_FATAL_FAILURE(OpenRecalculateAndClose(*reference));
    EXPECT_EQ(stage->cloth.random.State(), reference->cloth.random.State());
    EXPECT_EQ(own.strength, 21);
}
} // namespace
