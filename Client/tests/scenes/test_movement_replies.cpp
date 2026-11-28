#include "Models/IModelLibrary.h"
#include "Actors/MovementReplies.h"

#include "Crt/CrtRandom.h"
#include "Models/LogicalAccess.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Tables/PlayerModels.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Kernel/PlayerKey.h"
#include "Protocol/Movement/Movement.h"
#include "Models/Cloth.h"
#include "Math/OriginalAngles.h"
#include "Models/Skeleton.h"
#include "Models/ModelId.h"
#include "Render/TextureSlot.h"
#include "Kernel/ActorState.h"
#include "Assets/FaunaModels.h"
#include "Kernel/DrawTailState.h"
#include "FrontEnd/LoginModels.h"
#include "FrontEnd/SelectionState.h"
#include "Session/ModuleStates.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Camera/GameCamera.h"
#include "Effects/ModelEffects.h"

#include "support/PlayerActors.h"
#include "support/RecordingSink.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <limits>
#include <span>
#include <string>

namespace
{
class MovementModels final : public Client::Models::IModelLibrary
{
public:
    const Client::Data::Models::Model* Find(Client::Models::ModelId) const override
    {
        return nullptr;
    }
    const Client::Models::Skeleton* SkeletonOf(Client::Models::ModelId model) const override
    {
        if (model.Value() == 0x186 || model.Value() == Client::Tables::PlayerSkeletonModel)
            return &skeleton;
        return model.Value() == 0x154 ? &transformed : nullptr;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(Client::Models::ModelId) const override
    {
        return {};
    }
    Client::Models::Skeleton skeleton = Client::Tests::PlayerActionsSkeleton();
    Client::Models::Skeleton transformed = []
    {
        Client::Models::Skeleton value;
        value.actions.resize(7);
        for (auto& action : value.actions)
            action.keyCount = 10;
        return value;
    }();
};
struct Replies
{
    Replies()
    {
        models.Set(Client::Tables::PlayerSkeletonModel, {}, Client::Tests::PlayerActionsSkeleton(), {});
        world.Runtime().hero = 7;
        Hero().alive = true;
        Hero().key = PlayerKey;
        Hero().type = Client::Tables::PlayerSkeletonModel;
        Hero().pet.model = Client::Kernel::NoItemModel;
    }
    Client::Kernel::ActorState& Hero()
    {
        return world.Hero();
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
                .log = log,
                .states = &states};
    }
    static constexpr std::uint16_t PlayerKey = 33;
    Client::Kernel::WorldState world;
    Client::FrontEnd::SelectionState selection{world};
    Client::Session::ModuleStates states;
    Client::Camera::GameCamera camera;
    Client::Camera::GameCameraInput input;
    Client::Models::ClothWorld cloth;
    Client::FrontEnd::SingleModelLibrary models;
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
    ASSERT_TRUE(Client::Actors::ApplyWalkReply(Client::Protocol::Movement::Walk{Replies::PlayerKey, {30, 31}, 15},
                                               Client::Kernel::PlayerKey{Replies::PlayerKey}, tested.Resources())
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
    ASSERT_TRUE(Client::Actors::ApplyWalkReply(Client::Protocol::Movement::Walk{Replies::PlayerKey, {30, 31}, 15},
                                               Client::Kernel::PlayerKey{Replies::PlayerKey}, tested.Resources())
                    .HasValue());
    EXPECT_EQ(hero.cellX, 20);
    EXPECT_EQ(hero.direction, 15);
    hero.dying = 1;
    ASSERT_TRUE(Client::Actors::ApplyWalkReply(Client::Protocol::Movement::Walk{Replies::PlayerKey, {30, 31}, 4},
                                               Client::Kernel::PlayerKey{Replies::PlayerKey}, tested.Resources())
                    .HasValue());
    EXPECT_EQ(hero.direction, 15);
    EXPECT_EQ(tested.cloth.random.State(), 1u);
}
TEST(MovementRepliesTest, UnknownIdsLeaveTheSentinelUntouched)
{
    Replies tested;
    auto& sentinel = tested.world.Actors()[400];
    sentinel.direction = 7;
    ASSERT_TRUE(Client::Actors::ApplyWalkReply(Client::Protocol::Movement::Walk{666, {30, 31}, 15},
                                               Client::Kernel::PlayerKey{Replies::PlayerKey}, tested.Resources())
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
        Client::Actors::ApplyDamageReply(Client::Protocol::Movement::Damage{Replies::PlayerKey, true, 50, 0, false},
                                         Client::Kernel::PlayerKey{Replies::PlayerKey}, tested.Resources())
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
    tested.states.terrain.current.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    auto& other = tested.world.Actors()[8];
    other = Client::Tests::UnarmedPlayer();
    other.alive = true;
    other.key = 44;
    other.cellX = 20;
    other.cellY = 21;
    other.position = {2050.0f, 2150.0f, 33.0f};
    tested.states.terrain.current.attributes->cells[Client::Data::Terrain::CellIndex(21, 21)] = 4;
    const auto applied =
        Client::Actors::ApplyWalkReply(Client::Protocol::Movement::Walk{44, {23, 21}, 15},
                                       Client::Kernel::PlayerKey{Replies::PlayerKey}, tested.Resources());
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
// 0x4c6b09 → 0x4eac00 (Q-C1351): with stage 4 on the 34 and an altar at 0x6c8e54 (record 9, even if dead), another's
// D3 also carries the flag: the target's cell, with another character's 0x02, ends up at 0 and the path reaches it.
// In Lorencia and Devias the goal stays and the path goes to the nearest.
TEST(MovementRepliesTest, AChosenCrywolfAltarAlsoClearsTheGoalOfAnotherWalk)
{
    for (const std::int32_t map : {0, 2, 34})
    {
        SCOPED_TRACE(map);
        const auto tested = std::make_unique<Replies>();
        tested->states.terrain.current.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        tested->world.Runtime().map = map;
        tested->world.Runtime().crywolf.stage = 4;
        tested->world.Runtime().chosenNpc.index = 9;
        tested->world.Actors()[9].type = 0x206;
        auto& other = tested->world.Actors()[8];
        other = Client::Tests::UnarmedPlayer();
        other.alive = true;
        other.key = 44;
        other.cellX = 20;
        other.cellY = 21;
        other.position = {2050.0f, 2150.0f, 33.0f};
        auto& goal = tested->states.terrain.current.attributes->cells[Client::Data::Terrain::CellIndex(24, 21)];
        goal = 0x02;
        const auto applied =
            Client::Actors::ApplyWalkReply(Client::Protocol::Movement::Walk{44, {24, 21}, 15},
                                           Client::Kernel::PlayerKey{Replies::PlayerKey}, tested->Resources());
        ASSERT_TRUE(applied.HasValue()) << applied.Error();
        EXPECT_EQ(other.movement, 1);
        ASSERT_GT(other.path.count, 1);
        const auto& last = other.path.cells[other.path.count - 1U];
        EXPECT_EQ(goal, map == 34 ? 0 : 0x02);
        EXPECT_EQ(last.x, map == 34 ? 24 : 23);
        EXPECT_EQ(last.y, map == 34 ? 21 : 20);
        EXPECT_EQ(tested->cloth.random.State(), Client::Tests::AfterDraws(1, 8).State());
    }
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
    ASSERT_TRUE(Client::Actors::ApplyWalkReply(Client::Protocol::Movement::Walk{44, {30, 31}, 15},
                                               Client::Kernel::PlayerKey{Replies::PlayerKey}, tested.Resources())
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
    ASSERT_TRUE(Client::Actors::ApplyWalkReply(Client::Protocol::Movement::Walk{Replies::PlayerKey, {30, 31}, 15},
                                               Client::Kernel::PlayerKey{Replies::PlayerKey}, tested.Resources())
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
        Client::Actors::ApplyDamageReply(Client::Protocol::Movement::Damage{Replies::PlayerKey, false, 30, 0, false},
                                         Client::Kernel::PlayerKey{Replies::PlayerKey}, tested.Resources())
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
    ASSERT_TRUE(Client::Actors::ApplyDamageReply(Client::Protocol::Movement::Damage{44, false, 30, 0, false},
                                                 Client::Kernel::PlayerKey{Replies::PlayerKey}, tested.Resources())
                    .HasValue());
    EXPECT_EQ(other.movement, 1);
    EXPECT_EQ(tested.cloth.random.State(), Client::Tests::AfterDraws(1, 1).State());
    other.word376 = 0x113;
    ASSERT_TRUE(Client::Actors::ApplyDamageReply(Client::Protocol::Movement::Damage{44, false, 30, 0, false},
                                                 Client::Kernel::PlayerKey{Replies::PlayerKey}, tested.Resources())
                    .HasValue());
    EXPECT_EQ(tested.cloth.random.State(), Client::Tests::AfterDraws(1, 1).State());
}
TEST(MovementRepliesTest, DamageFlagsCreateCopiesInPhysicalSlotsWithoutAdditionalRandom)
{
    Replies tested;
    tested.world.Runtime().ownRecordDepth = 1;
    ASSERT_TRUE(Client::Actors::ApplyDamageReply(
                    Client::Protocol::Movement::Damage{Replies::PlayerKey, false, 30, 2, true, true},
                    Client::Kernel::PlayerKey{Replies::PlayerKey}, tested.Resources())
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
        tested.cloth.random = Client::Crt::CrtRandom(seed);
        ASSERT_TRUE(
            Client::Actors::ApplyDamageReply(Client::Protocol::Movement::Damage{Replies::PlayerKey, true, 10, 0, false},
                                             Client::Kernel::PlayerKey{Replies::PlayerKey}, tested.Resources())
                .HasValue());
        EXPECT_EQ(tested.cloth.random.State(), Client::Tests::AfterDraws(seed, characterClass == 4 ? 2 : 1).State());
    }
}
// 0x4cb30b..0x4cb3b0: the 0x20a hero with +0x78 & 0x100 that takes a hit without a shake, after subtracting the life
// and before the numbers. The striker is the character in slot [0x5767964] (WorldRuntime::lastAttacker); 0x4e93f0
// gives the heading from (1000, 1000) to the hero at (1300, 1400): atan(4/3) · 57.2957763671875f + 90 =
// 143.1300994…, less than 10 from its 140, so 0x518cf0(0x109, the hero's +0x10, (0, 0, heading + 180), +0xf4, 0,
// hero, −1, 0, 0, 0, 0): the shield 0x109/0 with the angles' z at 0x43a190a7 (computed apart from the port), life 20
// (0x51921a) and the hero as owner, which its step follows live. No rand(): only the own record close's 0xf08.
namespace
{
constexpr std::uint32_t ShieldFlag = 0x100;
constexpr std::int16_t ShieldEffect = 0x109;

struct ShieldReplies
{
    ShieldReplies(const glm::vec3& heroAt, const glm::vec3& strikerAt, float strikerYaw, std::int32_t source = 3)
    {
        tested.Hero().flags78 = ShieldFlag;
        tested.Hero().position = heroAt;
        tested.Hero().light = {0.25f, 0.5f, 0.75f};
        tested.world.OwnCharacter().life = 40;
        auto& striker = tested.world.Actors()[static_cast<std::size_t>(source)];
        striker.position = strikerAt;
        striker.angles.z = strikerYaw;
        tested.world.Runtime().lastAttacker = source;
    }
    Client::Core::Expected<void, std::string> Hit()
    {
        return Client::Actors::ApplyDamageReply(
            Client::Protocol::Movement::Damage{Replies::PlayerKey, false, 30, 0, false},
            Client::Kernel::PlayerKey{Replies::PlayerKey}, tested.Resources());
    }
    std::ptrdiff_t Shields() const
    {
        return std::ranges::count_if(tested.world.Map().effects.Effects(),
                                     [](const auto& effect) { return effect.alive; });
    }
    Replies tested;
};
} // namespace

TEST(MovementRepliesTest, TheHeroShieldFacesTheStrikerAfterLifeAndBeforeNumbers)
{
    ShieldReplies shield({1300.0f, 1400.0f, 80.0f}, {1000.0f, 1000.0f, 0.0f}, 140.0f);

    const auto result = shield.Hit();

    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_EQ(shield.Shields(), 1);
    const auto& effect = shield.tested.world.Map().effects.Effects()[0];
    EXPECT_EQ(effect.type, ShieldEffect);
    EXPECT_EQ(effect.subtype, 0);
    EXPECT_EQ(effect.transform.position, glm::vec3(1300.0f, 1400.0f, 80.0f));
    EXPECT_EQ(effect.transform.angles, glm::vec3(0.0f, 0.0f, std::bit_cast<float>(0x43a190a7u)));
    EXPECT_EQ(effect.light, glm::vec3(0.25f, 0.5f, 0.75f));
    EXPECT_EQ(effect.life, 20);
    EXPECT_EQ(effect.objectState.blendMesh, 0);
    EXPECT_EQ(effect.objectState.light, 1.0f);
    EXPECT_EQ(effect.owner.position, &shield.tested.Hero().position);
    EXPECT_EQ(shield.tested.world.OwnCharacter().life, 10);
    EXPECT_TRUE(shield.tested.world.DrawTail().damage[0].alive);
    EXPECT_EQ(shield.tested.Hero().damage, 30);
    EXPECT_EQ(shield.tested.cloth.random.State(), Client::Tests::AfterDraws(1, 0xf08).State());
}

// 0x4cb369..0x4cb376: at 10 or more from its heading (154 is 10.87 off) nothing comes out, and it goes on the same:
// the numbers and the damage. Without 0x20a, it does not even look.
TEST(MovementRepliesTest, TheHeroShieldNeedsTheStrikerFacingItAndTheBodyOfAPlayer)
{
    // On the heap: two WorldState do not fit in Windows' 1 MB stack.
    const auto away =
        std::make_unique<ShieldReplies>(glm::vec3{1300.0f, 1400.0f, 80.0f}, glm::vec3{1000.0f, 1000.0f, 0.0f}, 154.0f);
    const auto missed = away->Hit();
    ASSERT_TRUE(missed.HasValue()) << missed.Error();
    EXPECT_EQ(away->Shields(), 0);
    EXPECT_TRUE(away->tested.world.DrawTail().damage[0].alive);
    EXPECT_EQ(away->tested.Hero().damage, 30);

    const auto transformed =
        std::make_unique<ShieldReplies>(glm::vec3{1300.0f, 1400.0f, 80.0f}, glm::vec3{1000.0f, 1000.0f, 0.0f}, 140.0f);
    transformed->tested.Hero().type = 0x154;
    ASSERT_TRUE(transformed->Hit().HasValue());
    EXPECT_EQ(transformed->Shields(), 0);
}

// Without an attacker: 0x4cb32f looks at nothing, it reads the slot [0x5767964] says. The 19, the 69 and the 1A
// without the source leave 400, the slot that follows the table's 400 (record 401, which WinMain leaves at zero,
// 0x4bafa6), and before the first there is the .bss 0. With slot 400 at (0, 0) and heading 0, the hero at (0, −500)
// is at heading 0 (0x4e93f0 without x and with negative y) and brings out the shield with z at 180; at (500, 0) it is
// at 90 and does not. With the effects table full, nothing.
TEST(MovementRepliesTest, WithoutAStrikerTheHeroShieldReadsTheSlotLeftInTheGlobal)
{
    constexpr glm::vec3 Below{0.0f, -500.0f, 0.0f};
    constexpr glm::vec3 Origin{};
    constexpr std::int32_t LooseSlot = 400;
    // On the heap: four WorldState do not fit in Windows' 1 MB stack.
    const auto loose = std::make_unique<ShieldReplies>(Below, Origin, 0.0f, LooseSlot);
    ASSERT_TRUE(loose->Hit().HasValue());
    ASSERT_EQ(loose->Shields(), 1);
    EXPECT_EQ(loose->tested.world.Map().effects.Effects()[0].transform.angles, glm::vec3(0.0f, 0.0f, 180.0f));

    const auto aside = std::make_unique<ShieldReplies>(glm::vec3{500.0f, 0.0f, 0.0f}, Origin, 0.0f, LooseSlot);
    ASSERT_TRUE(aside->Hit().HasValue());
    EXPECT_EQ(aside->Shields(), 0);

    const auto first = std::make_unique<ShieldReplies>(Below, Origin, 0.0f, 0);
    ASSERT_TRUE(first->Hit().HasValue());
    EXPECT_EQ(first->Shields(), 1);

    const auto full = std::make_unique<ShieldReplies>(Below, Origin, 0.0f, LooseSlot);
    for (auto& effect : full->tested.world.Map().effects.Effects())
        effect.alive = true;
    const auto crowded = full->Hit();
    ASSERT_TRUE(crowded.HasValue()) << crowded.Error();
    EXPECT_EQ(full->tested.Hero().damage, 30);
    EXPECT_EQ(full->tested.cloth.random.State(), Client::Tests::AfterDraws(1, 0xf08).State());
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
    tested.cloth.random = Client::Crt::CrtRandom(seed);
    const Client::Scenes::WorldEntryResources resources{tested.world,  tested.selection, {},     tested.cloth,
                                                        tested.camera, tested.input,     models, nullptr,
                                                        nullptr,       tested.log};
    const auto result = Client::Actors::ApplyWalkReply(Client::Protocol::Movement::Walk{44, {30, 31}, 15},
                                                       Client::Kernel::PlayerKey{Replies::PlayerKey}, resources);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(other.cellX, 30);
    EXPECT_EQ(other.cellY, 31);
    EXPECT_EQ(other.byte399, 1);
    EXPECT_EQ(other.position, glm::vec3(2050.0f, 2150.0f, 33.0f));
    EXPECT_FLOAT_EQ(other.angles.z, static_cast<float>(Client::Math::CreateAngle(2050.0f, 2150.0f, 3000.0f, 3100.0f)));
    EXPECT_EQ(tested.cloth.random.State(), Client::Tests::AfterDraws(seed, 9).State());
}
// The 0x186 is a monster's model (type 71, Monster53.bmd): its D3 comes from gObjMonster (the 0.97 GS's CGMoveRecv)
// and 0x4f37c0 → 0x4ea030 reads the actions of the model its creation loaded (Assets::AssetsState::fauna), not those
// of the map's library, which does not have it. In Lorencia and Devias.
TEST(MovementRepliesTest, TheJumpingMonsterTakesTheActionsOfItsLoadedModel)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = std::make_unique<Replies>();
        tested->world.Runtime().map = map;
        MovementModels fauna;
        tested->states.assets.fauna.Bind(&fauna);
        auto& other = tested->world.Actors()[8];
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
        tested->cloth.random = Client::Crt::CrtRandom(seed);
        const auto result =
            Client::Actors::ApplyWalkReply(Client::Protocol::Movement::Walk{44, {30, 31}, 15},
                                           Client::Kernel::PlayerKey{Replies::PlayerKey}, tested->Resources());
        tested->states.assets.fauna.Bind(nullptr);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(other.cellX, 30);
        EXPECT_EQ(other.cellY, 31);
        EXPECT_EQ(other.byte399, 1);
        EXPECT_EQ(other.animation.action, 2);
        EXPECT_EQ(tested->cloth.random.State(), Client::Tests::AfterDraws(seed, 9).State());
    }
}
TEST(MovementRepliesTest, AFailedOtherPathRunsStopAfterClosingItsLogicalCoordinates)
{
    Replies tested;
    tested.states.terrain.current.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    auto& other = tested.world.Actors()[8];
    other = Client::Tests::UnarmedPlayer();
    other.alive = true;
    other.key = 44;
    other.cellX = 0;
    other.cellY = 21;
    other.movement = 1;
    other.animation.action = 13;
    const auto result =
        Client::Actors::ApplyWalkReply(Client::Protocol::Movement::Walk{44, {30, 31}, 15},
                                       Client::Kernel::PlayerKey{Replies::PlayerKey}, tested.Resources());
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(other.movement, 0);
    EXPECT_EQ(other.animation.action, 1);
    EXPECT_EQ(tested.cloth.random.State(), Client::Tests::AfterDraws(1, 9).State());
}

TEST(MovementRepliesTest, AFailedTransformedPathStopsWithItsLoadedModel)
{
    Replies tested;
    MovementModels fauna;
    tested.states.assets.fauna.Bind(&fauna);
    tested.states.terrain.current.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    auto& other = tested.world.Actors()[8];
    other = Client::Tests::UnarmedPlayer();
    other.alive = true;
    other.key = 44;
    other.type = 0x154;
    other.cellX = 0;
    other.cellY = 21;
    other.movement = 1;
    other.animation.action = 2;

    const auto result =
        Client::Actors::ApplyWalkReply(Client::Protocol::Movement::Walk{44, {30, 31}, 15},
                                       Client::Kernel::PlayerKey{Replies::PlayerKey}, tested.Resources());
    tested.states.assets.fauna.Bind(nullptr);

    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(other.movement, 0);
    EXPECT_EQ(other.animation.action, 0);
    EXPECT_EQ(tested.cloth.random.State(),
              Client::Tests::AfterDraws(1, 4 * Client::Models::SmallLogicalCloseRandoms + 1).State());
}
TEST(MovementRepliesTest, BitSixProducesTwoDimCopiesBeforeTheNormalNumberAtTheOriginalHeight)
{
    Replies tested;
    tested.world.Runtime().ownRecordDepth = 1;
    const auto result = Client::Actors::ApplyDamageReply(
        Client::Protocol::Movement::Damage{Replies::PlayerKey, false, 30, 0, false, true},
        Client::Kernel::PlayerKey{Replies::PlayerKey}, tested.Resources());
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
    const auto result = Client::Actors::ApplyDamageReply(
        Client::Protocol::Movement::Damage{Replies::PlayerKey, false, 30, 5, true, false},
        Client::Kernel::PlayerKey{Replies::PlayerKey}, tested.Resources());
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
        Client::Actors::ApplyDamageReply(Client::Protocol::Movement::Damage{Replies::PlayerKey, true, 30, 0, false},
                                         Client::Kernel::PlayerKey{Replies::PlayerKey}, tested.Resources());
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_TRUE(tested.world.Runtime().lastSoundId.has_value());
    EXPECT_EQ(tested.cloth.random.State(), Client::Tests::AfterDraws(1, 1).State());
}
} // namespace
