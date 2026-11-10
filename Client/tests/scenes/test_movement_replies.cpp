#include "Scenes/MovementReplies.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Protocol/Movement.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalAngles.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/LoginModels.h"
#include "Scenes/SelectionState.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"

#include "support/PlayerActors.h"
#include "support/RecordingSink.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <limits>
#include <span>
#include <string>

namespace
{
class MovementModels final : public Client::Scenes::IModelLibrary
{
public:
    const Client::Data::Models::Model* Find(std::uint16_t) const override
    {
        return nullptr;
    }
    const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        return model == 0x186 || model == Client::Data::Models::PlayerSkeletonModel ? &skeleton : nullptr;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return {};
    }
    Client::Render::Models::Skeleton skeleton = Client::Tests::PlayerActionsSkeleton();
};
struct Replies
{
    Replies()
    {
        models.Set(Client::Data::Models::PlayerSkeletonModel, {}, Client::Tests::PlayerActionsSkeleton(), {});
        world.Runtime().hero = 7;
        Hero().alive = true;
        Hero().key = PlayerKey;
        Hero().type = Client::Data::Models::PlayerSkeletonModel;
        Hero().pet.model = Client::Scenes::NoItemModel;
    }
    Client::Scenes::ActorState& Hero()
    {
        return world.Actors()[world.Runtime().hero];
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
                .log = log};
    }
    static constexpr std::uint16_t PlayerKey = 33;
    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection{world};
    Client::World::GameCamera camera;
    Client::World::GameCameraInput input;
    Client::Render::Models::ClothWorld cloth;
    Client::Scenes::SingleModelLibrary models;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
};

TEST(MovementRepliesTest, AnIdleHeroEchoWritesOnlyTheLogicalCellsAndDirection)
{
    Replies tested;
    auto& hero = tested.Hero();
    hero.cellX = 20;
    hero.cellY = 21;
    hero.targetX = 24;
    hero.targetY = 25;
    hero.position = {2050.0f, 2150.0f, 33.0f};
    ASSERT_TRUE(Client::Scenes::ApplyWalkReply(Client::Protocol::Movement::Walk{Replies::PlayerKey, {30, 31}, 15},
                                               Replies::PlayerKey, tested.Resources())
                    .HasValue());
    EXPECT_EQ(hero.cellX, 30);
    EXPECT_EQ(hero.cellY, 31);
    EXPECT_EQ(hero.direction, 15);
    EXPECT_EQ(hero.position, glm::vec3(2050.0f, 2150.0f, 33.0f));
    EXPECT_EQ(hero.targetX, 24);
    EXPECT_EQ(hero.targetY, 25);
    EXPECT_EQ(tested.cloth.random.State(), Client::Tests::AfterDraws(1, 8).State());
}
TEST(MovementRepliesTest, MovingAndDyingHeroEchoesHaveDistinctPrefixes)
{
    Replies tested;
    auto& hero = tested.Hero();
    hero.movement = 1;
    hero.cellX = 20;
    ASSERT_TRUE(Client::Scenes::ApplyWalkReply(Client::Protocol::Movement::Walk{Replies::PlayerKey, {30, 31}, 15},
                                               Replies::PlayerKey, tested.Resources())
                    .HasValue());
    EXPECT_EQ(hero.cellX, 20);
    EXPECT_EQ(hero.direction, 15);
    hero.dying = 1;
    ASSERT_TRUE(Client::Scenes::ApplyWalkReply(Client::Protocol::Movement::Walk{Replies::PlayerKey, {30, 31}, 4},
                                               Replies::PlayerKey, tested.Resources())
                    .HasValue());
    EXPECT_EQ(hero.direction, 15);
    EXPECT_EQ(tested.cloth.random.State(), 1u);
}
TEST(MovementRepliesTest, UnknownIdsLeaveTheSentinelUntouched)
{
    Replies tested;
    auto& sentinel = tested.world.Actors()[400];
    sentinel.direction = 7;
    ASSERT_TRUE(Client::Scenes::ApplyWalkReply(Client::Protocol::Movement::Walk{666, {30, 31}, 15}, Replies::PlayerKey,
                                               tested.Resources())
                    .HasValue());
    EXPECT_EQ(sentinel.direction, 7);
    EXPECT_EQ(tested.cloth.random.State(), 1u);
}
TEST(MovementRepliesTest, ShockStopsTheHeroBeforeLifeClosesTheOwnRecord)
{
    Replies tested;
    auto& hero = tested.Hero();
    hero.movement = 1;
    hero.cellX = 31;
    hero.animation.action = 13;
    tested.world.OwnCharacter().life = 40;
    ASSERT_TRUE(
        Client::Scenes::ApplyDamageReply(Client::Protocol::Movement::Damage{Replies::PlayerKey, true, 50, 0, false},
                                         Replies::PlayerKey, tested.Resources())
            .HasValue());
    EXPECT_EQ(hero.movement, 0);
    EXPECT_EQ(hero.animation.action, 0xc2);
    EXPECT_EQ(hero.cellX, 31);
    EXPECT_EQ(tested.world.OwnCharacter().life, 0);
    EXPECT_EQ(tested.cloth.random.State(), Client::Tests::AfterDraws(1, 1 + 0xf08).State());
}
TEST(MovementRepliesTest, OtherActorUsesTheLiveWallsAndKeepsLogicalPositionUntilTheNextStep)
{
    Replies tested;
    tested.world.Map().terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    auto& other = tested.world.Actors()[8];
    other = Client::Tests::UnarmedPlayer();
    other.alive = true;
    other.key = 44;
    other.cellX = 20;
    other.cellY = 21;
    other.position = {2050.0f, 2150.0f, 33.0f};
    tested.world.Map().terrain.attributes->cells[Client::Data::Terrain::CellIndex(21, 21)] = 4;
    const auto applied = Client::Scenes::ApplyWalkReply(Client::Protocol::Movement::Walk{44, {23, 21}, 15},
                                                        Replies::PlayerKey, tested.Resources());
    ASSERT_TRUE(applied.HasValue()) << applied.Error();
    EXPECT_EQ(other.targetX, 23);
    EXPECT_EQ(other.targetY, 21);
    EXPECT_EQ(other.movement, 1);
    EXPECT_EQ(other.cellX, 20);
    EXPECT_EQ(other.position.x, 2050.0f);
    EXPECT_EQ(other.direction, 15);
    ASSERT_GT(other.path.count, 1);
    for (std::uint8_t index = 0; index < other.path.count; ++index)
        EXPECT_FALSE(other.path.cells[index].x == 21 && other.path.cells[index].y == 21);
    EXPECT_EQ(tested.cloth.random.State(), Client::Tests::AfterDraws(1, 8).State());
}
TEST(MovementRepliesTest, CooldownPreservesAnExistingPathButStillUpdatesTheTargetAndDirection)
{
    Replies tested;
    auto& other = tested.world.Actors()[8];
    other.alive = true;
    other.key = 44;
    other.movementCooldown = 1;
    other.path.count = 4;
    other.movement = 1;
    ASSERT_TRUE(Client::Scenes::ApplyWalkReply(Client::Protocol::Movement::Walk{44, {30, 31}, 15}, Replies::PlayerKey,
                                               tested.Resources())
                    .HasValue());
    EXPECT_EQ(other.path.count, 4);
    EXPECT_EQ(other.movement, 1);
    EXPECT_EQ(other.targetX, 30);
    EXPECT_EQ(other.direction, 15);
    EXPECT_EQ(tested.cloth.random.State(), 1u);
}
TEST(MovementRepliesTest, NestedLogicalCellsDoNotCloseTheOuterEntries)
{
    Replies tested;
    tested.Hero().cellXAccesses = 1;
    tested.Hero().cellYAccesses = 1;
    ASSERT_TRUE(Client::Scenes::ApplyWalkReply(Client::Protocol::Movement::Walk{Replies::PlayerKey, {30, 31}, 15},
                                               Replies::PlayerKey, tested.Resources())
                    .HasValue());
    EXPECT_EQ(tested.Hero().cellXAccesses, 1);
    EXPECT_EQ(tested.Hero().cellYAccesses, 1);
    EXPECT_EQ(tested.cloth.random.State(), 1u);
}
TEST(MovementRepliesTest, OrdinaryHeroDamageNeverShocksAndItsNumberDoesNotRequestRandom)
{
    Replies tested;
    tested.Hero().movement = 1;
    tested.Hero().animation.action = 13;
    tested.world.OwnCharacter().life = 40;
    ASSERT_TRUE(
        Client::Scenes::ApplyDamageReply(Client::Protocol::Movement::Damage{Replies::PlayerKey, false, 30, 0, false},
                                         Replies::PlayerKey, tested.Resources())
            .HasValue());
    EXPECT_EQ(tested.Hero().movement, 1);
    EXPECT_EQ(tested.Hero().animation.action, 13);
    EXPECT_EQ(tested.world.OwnCharacter().life, 10);
    EXPECT_EQ(tested.Hero().damage, 30);
    const auto& number = tested.world.DrawTail().damage[0];
    EXPECT_TRUE(number.alive);
    EXPECT_EQ(number.number, 30);
    EXPECT_EQ(number.position.z, 140.0f);
    EXPECT_EQ(number.light, glm::vec3(1.0f, 0.0f, 0.0f));
    EXPECT_EQ(number.scale, 15.0f);
    EXPECT_EQ(tested.cloth.random.State(), Client::Tests::AfterDraws(1, 0xf08).State());
}
TEST(MovementRepliesTest, OtherDamageConsumesTheCoinEvenWhenShockIsBlockedByTheMount)
{
    Replies tested;
    auto& other = tested.world.Actors()[8];
    other = Client::Tests::UnarmedPlayer();
    other.alive = true;
    other.key = 44;
    other.movement = 1;
    other.pet.model = 0x1c15;
    ASSERT_TRUE(Client::Scenes::ApplyDamageReply(Client::Protocol::Movement::Damage{44, false, 30, 0, false},
                                                 Replies::PlayerKey, tested.Resources())
                    .HasValue());
    EXPECT_EQ(other.movement, 1);
    EXPECT_EQ(tested.cloth.random.State(), Client::Tests::AfterDraws(1, 1).State());
    other.word376 = 0x113;
    ASSERT_TRUE(Client::Scenes::ApplyDamageReply(Client::Protocol::Movement::Damage{44, false, 30, 0, false},
                                                 Replies::PlayerKey, tested.Resources())
                    .HasValue());
    EXPECT_EQ(tested.cloth.random.State(), Client::Tests::AfterDraws(1, 1).State());
}
TEST(MovementRepliesTest, DamageFlagsCreateCopiesInPhysicalSlotsWithoutAdditionalRandom)
{
    Replies tested;
    tested.world.Runtime().ownRecordDepth = 1;
    ASSERT_TRUE(Client::Scenes::ApplyDamageReply(
                    Client::Protocol::Movement::Damage{Replies::PlayerKey, false, 30, 2, true, true},
                    Replies::PlayerKey, tested.Resources())
                    .HasValue());
    const auto& numbers = tested.world.DrawTail().damage;
    ASSERT_TRUE(numbers[0].alive);
    ASSERT_TRUE(numbers[1].alive);
    ASSERT_TRUE(numbers[2].alive);
    EXPECT_TRUE(numbers[3].alive);
    EXPECT_FALSE(numbers[4].alive);
    EXPECT_EQ(numbers[3].position.z, 140.0f);
    EXPECT_EQ(numbers[3].scale, 50.0f);
    EXPECT_EQ(numbers[0].position.z, 140.0f);
    EXPECT_EQ(numbers[1].position.z, 150.0f);
    EXPECT_EQ(numbers[2].position.z, 160.0f);
    EXPECT_EQ(numbers[0].scale, 50.0f);
    EXPECT_EQ(numbers[1].scale, 55.0f);
    EXPECT_EQ(numbers[2].scale, 60.0f);
    EXPECT_FLOAT_EQ(numbers[0].light.x, -0.4f);
    EXPECT_FLOAT_EQ(numbers[0].light.y, 0.6f);
    EXPECT_FLOAT_EQ(numbers[0].light.z, 0.2f);
    EXPECT_EQ(tested.cloth.random.State(), 1u);
}
TEST(MovementRepliesTest, ShockSoundClassesPreserveTheirDistinctRandomCounts)
{
    for (const std::uint8_t characterClass : std::array<std::uint8_t, 3>{0, 2, 4})
    {
        Replies tested;
        tested.Hero().characterClass = characterClass;
        tested.world.Runtime().ownRecordDepth = 1;
        const auto seed = Client::Tests::FirstSeedWhere([](auto& random) { return random.Next() % 5 == 0; });
        tested.cloth.random = Client::Render::Models::CrtRandom(seed);
        ASSERT_TRUE(
            Client::Scenes::ApplyDamageReply(Client::Protocol::Movement::Damage{Replies::PlayerKey, true, 10, 0, false},
                                             Replies::PlayerKey, tested.Resources())
                .HasValue());
        EXPECT_EQ(tested.cloth.random.State(), Client::Tests::AfterDraws(seed, characterClass == 4 ? 2 : 1).State());
    }
}
TEST(MovementRepliesTest, TheUnportedDefensiveEffectStopsAfterLifeAndBeforeNumbers)
{
    Replies tested;
    tested.Hero().flags78 = 0x100;
    tested.world.OwnCharacter().life = 40;
    const auto result =
        Client::Scenes::ApplyDamageReply(Client::Protocol::Movement::Damage{Replies::PlayerKey, false, 30, 0, false},
                                         Replies::PlayerKey, tested.Resources());
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x4cb32f"), std::string::npos);
    EXPECT_EQ(tested.world.OwnCharacter().life, 10);
    EXPECT_FALSE(tested.world.DrawTail().damage[0].alive);
    EXPECT_EQ(tested.Hero().damage, 0);
    EXPECT_EQ(tested.cloth.random.State(), Client::Tests::AfterDraws(1, 0xf08).State());
}
TEST(MovementRepliesTest, JumpingActorAimsAtTheCellCornerWithoutMovingItsVisiblePosition)
{
    Replies tested;
    MovementModels models;
    auto& other = tested.world.Actors()[8];
    other = Client::Tests::UnarmedPlayer();
    other.alive = true;
    other.key = 44;
    other.type = 0x186;
    other.position = {2050.0f, 2150.0f, 33.0f};
    const auto seed = Client::Tests::FirstSeedWhere(
        [](auto& random)
        {
            for (int index = 0; index < 8; ++index)
                static_cast<void>(random.Next());
            return (random.Next() & 15) != 0;
        });
    tested.cloth.random = Client::Render::Models::CrtRandom(seed);
    const Client::Scenes::WorldEntryResources resources{tested.world,  tested.selection, {},     tested.cloth,
                                                        tested.camera, tested.input,     models, nullptr,
                                                        nullptr,       tested.log};
    const auto result = Client::Scenes::ApplyWalkReply(Client::Protocol::Movement::Walk{44, {30, 31}, 15},
                                                       Replies::PlayerKey, resources);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(other.cellX, 30);
    EXPECT_EQ(other.cellY, 31);
    EXPECT_EQ(other.byte399, 1);
    EXPECT_EQ(other.position, glm::vec3(2050.0f, 2150.0f, 33.0f));
    EXPECT_FLOAT_EQ(other.angles.z,
                    static_cast<float>(Client::Render::Models::CreateAngle(2050.0f, 2150.0f, 3000.0f, 3100.0f)));
    EXPECT_EQ(tested.cloth.random.State(), Client::Tests::AfterDraws(seed, 9).State());
}
TEST(MovementRepliesTest, AFailedOtherPathRunsStopAfterClosingItsLogicalCoordinates)
{
    Replies tested;
    tested.world.Map().terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    auto& other = tested.world.Actors()[8];
    other = Client::Tests::UnarmedPlayer();
    other.alive = true;
    other.key = 44;
    other.cellX = 0;
    other.cellY = 21;
    other.movement = 1;
    other.animation.action = 13;
    const auto result = Client::Scenes::ApplyWalkReply(Client::Protocol::Movement::Walk{44, {30, 31}, 15},
                                                       Replies::PlayerKey, tested.Resources());
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(other.movement, 0);
    EXPECT_EQ(other.animation.action, 1);
    EXPECT_EQ(tested.cloth.random.State(), Client::Tests::AfterDraws(1, 9).State());
}
TEST(MovementRepliesTest, BitSixProducesTwoDimCopiesBeforeTheNormalNumberAtTheOriginalHeight)
{
    Replies tested;
    tested.world.Runtime().ownRecordDepth = 1;
    const auto result = Client::Scenes::ApplyDamageReply(
        Client::Protocol::Movement::Damage{Replies::PlayerKey, false, 30, 0, false, true}, Replies::PlayerKey,
        tested.Resources());
    ASSERT_TRUE(result.HasValue()) << result.Error();
    const auto& numbers = tested.world.DrawTail().damage;
    EXPECT_TRUE(numbers[0].alive);
    EXPECT_TRUE(numbers[1].alive);
    EXPECT_TRUE(numbers[2].alive);
    EXPECT_FALSE(numbers[3].alive);
    EXPECT_EQ(numbers[0].position.z, 140.0f);
    EXPECT_EQ(numbers[1].position.z, 150.0f);
    EXPECT_EQ(numbers[2].position.z, 140.0f);
    EXPECT_EQ(numbers[0].scale, 15.0f);
    EXPECT_EQ(numbers[1].scale, 20.0f);
    EXPECT_EQ(numbers[2].scale, 15.0f);
    EXPECT_EQ(tested.cloth.random.State(), 1u);
}
TEST(MovementRepliesTest, BitSevenFallsThroughFromItsThirdNumberToTheFourthNormalNumber)
{
    Replies tested;
    tested.world.Runtime().ownRecordDepth = 1;
    const auto result = Client::Scenes::ApplyDamageReply(
        Client::Protocol::Movement::Damage{Replies::PlayerKey, false, 30, 5, true, false}, Replies::PlayerKey,
        tested.Resources());
    ASSERT_TRUE(result.HasValue()) << result.Error();
    const auto& numbers = tested.world.DrawTail().damage;
    EXPECT_TRUE(numbers[3].alive);
    EXPECT_FALSE(numbers[4].alive);
    EXPECT_EQ(numbers[2].position.z, 160.0f);
    EXPECT_EQ(numbers[2].scale, 60.0f);
    EXPECT_EQ(numbers[3].position.z, 140.0f);
    EXPECT_EQ(numbers[3].scale, 50.0f);
    EXPECT_EQ(numbers[3].number, 30);
    EXPECT_EQ(numbers[3].light, glm::vec3(0.0f, 1.0f, 0.0f));
    EXPECT_EQ(tested.cloth.random.State(), 1u);
}
TEST(MovementRepliesTest, ShockTreatsAnUnorderedExistingFrameAsEqualForTheHitSound)
{
    Replies tested;
    tested.world.Runtime().ownRecordDepth = 1;
    tested.Hero().animation.action = 0xc2;
    tested.Hero().animation.frame = std::numeric_limits<float>::quiet_NaN();
    const auto result =
        Client::Scenes::ApplyDamageReply(Client::Protocol::Movement::Damage{Replies::PlayerKey, true, 30, 0, false},
                                         Replies::PlayerKey, tested.Resources());
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_TRUE(tested.world.Runtime().lastSoundId.has_value());
    EXPECT_EQ(tested.cloth.random.State(), Client::Tests::AfterDraws(1, 1).State());
}
} // namespace
