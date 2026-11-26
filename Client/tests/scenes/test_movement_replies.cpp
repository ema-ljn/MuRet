#include "Models/IModelLibrary.h"
#include "Scenes/MovementReplies.h"

#include "Crt/CrtRandom.h"
#include "Models/LogicalAccess.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Tables/PlayerModels.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Protocol/Movement.h"
#include "Models/Cloth.h"
#include "Math/OriginalAngles.h"
#include "Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/FaunaModels.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/LoginModels.h"
#include "Scenes/SelectionState.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"
#include "World/ModelEffects.h"

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
    const Client::Data::Models::Model* Find(std::uint16_t) const override
    {
        return nullptr;
    }
    const Client::Models::Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        if (model == 0x186 || model == Client::Data::Models::PlayerSkeletonModel)
            return &skeleton;
        return model == 0x154 ? &transformed : nullptr;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
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
    Client::Models::ClothWorld cloth;
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
// 0x4c6b09 → 0x4eac00 (Q-C1351): con la etapa 4 en el 34 y un altar en 0x6c8e54 (el registro 9, aunque esté muerto),
// el D3 de otro también lleva la bandera: la celda del blanco, con el 0x02 de otro personaje, queda en 0 y el camino
// llega a ella. En Lorencia y Devias la meta queda y el camino va al más cercano.
TEST(MovementRepliesTest, AChosenCrywolfAltarAlsoClearsTheGoalOfAnotherWalk)
{
    for (const std::int32_t map : {0, 2, 34})
    {
        SCOPED_TRACE(map);
        const auto tested = std::make_unique<Replies>();
        tested->world.Map().terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
        tested->world.Runtime().map = map;
        tested->world.Runtime().crywolf.stage = 4;
        tested->world.Runtime().movement.chosenNpc = 9;
        tested->world.Actors()[9].type = 0x206;
        auto& other = tested->world.Actors()[8];
        other = Client::Tests::UnarmedPlayer();
        other.alive = true;
        other.key = 44;
        other.cellX = 20;
        other.cellY = 21;
        other.position = {2050.0f, 2150.0f, 33.0f};
        auto& goal = tested->world.Map().terrain.attributes->cells[Client::Data::Terrain::CellIndex(24, 21)];
        goal = 0x02;
        const auto applied = Client::Scenes::ApplyWalkReply(Client::Protocol::Movement::Walk{44, {24, 21}, 15},
                                                            Replies::PlayerKey, tested->Resources());
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
        tested.cloth.random = Client::Crt::CrtRandom(seed);
        ASSERT_TRUE(
            Client::Scenes::ApplyDamageReply(Client::Protocol::Movement::Damage{Replies::PlayerKey, true, 10, 0, false},
                                             Replies::PlayerKey, tested.Resources())
                .HasValue());
        EXPECT_EQ(tested.cloth.random.State(), Client::Tests::AfterDraws(seed, characterClass == 4 ? 2 : 1).State());
    }
}
// 0x4cb30b..0x4cb3b0: el héroe 0x20a con +0x78 & 0x100 que recibe un golpe sin sacudida, después de restar la vida y
// antes de los números. El que pega es el personaje del lugar [0x5767964] (WorldRuntime::lastAttacker); 0x4e93f0 da el
// rumbo desde (1000, 1000) hasta el héroe en (1300, 1400): atan(4/3) · 57,2957763671875f + 90 = 143,1300994…, a menos
// de 10 de su 140, así que 0x518cf0(0x109, +0x10 del héroe, (0, 0, rumbo + 180), +0xf4, 0, héroe, −1, 0, 0, 0, 0): el
// escudo 0x109/0 con la z de los ángulos en 0x43a190a7 (calculado aparte del port), la vida 20 (0x51921a) y el héroe de
// dueño, que su paso sigue en vivo. Sin rand(): sólo los 0xf08 del cierre del registro propio.
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
        return Client::Scenes::ApplyDamageReply(
            Client::Protocol::Movement::Damage{Replies::PlayerKey, false, 30, 0, false}, Replies::PlayerKey,
            tested.Resources());
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

// 0x4cb369..0x4cb376: a 10 o más de su rumbo (el 154 queda a 10,87) no sale nada, y sigue igual: los números y el
// daño. Sin 0x20a, ni lo mira.
TEST(MovementRepliesTest, TheHeroShieldNeedsTheStrikerFacingItAndTheBodyOfAPlayer)
{
    // En el heap: dos WorldState no entran en la pila de 1 MB de Windows.
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

// Sin atacante: 0x4cb32f no mira nada, lee el lugar que diga [0x5767964]. El 19, el 69 y el 1A sin la fuente dejan el
// 400, el lugar que sigue a los 400 de la tabla (el registro 401 que WinMain deja en cero, 0x4bafa6), y antes del
// primero está el 0 de la .bss. Con el lugar 400 en (0, 0) y rumbo 0, el héroe en (0, −500) queda a rumbo 0 (0x4e93f0
// sin x y con y negativa) y saca el escudo con la z en 180; en (500, 0) queda a 90 y no. Con la tabla de efectos llena,
// nada.
TEST(MovementRepliesTest, WithoutAStrikerTheHeroShieldReadsTheSlotLeftInTheGlobal)
{
    constexpr glm::vec3 Below{0.0f, -500.0f, 0.0f};
    constexpr glm::vec3 Origin{};
    constexpr std::int32_t LooseSlot = 400;
    // En el heap: cuatro WorldState no entran en la pila de 1 MB de Windows.
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
    const auto result = Client::Scenes::ApplyWalkReply(Client::Protocol::Movement::Walk{44, {30, 31}, 15},
                                                       Replies::PlayerKey, resources);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(other.cellX, 30);
    EXPECT_EQ(other.cellY, 31);
    EXPECT_EQ(other.byte399, 1);
    EXPECT_EQ(other.position, glm::vec3(2050.0f, 2150.0f, 33.0f));
    EXPECT_FLOAT_EQ(other.angles.z, static_cast<float>(Client::Math::CreateAngle(2050.0f, 2150.0f, 3000.0f, 3100.0f)));
    EXPECT_EQ(tested.cloth.random.State(), Client::Tests::AfterDraws(seed, 9).State());
}
// El 0x186 es el modelo de un monstruo (el tipo 71, Monster53.bmd): su D3 llega desde gObjMonster (CGMoveRecv del GS
// 0.97) y 0x4f37c0 → 0x4ea030 lee las acciones del modelo que cargó su alta (WorldState::FaunaResources), no las de la
// biblioteca del mapa, que no lo tiene. En Lorencia y en Devias.
TEST(MovementRepliesTest, TheJumpingMonsterTakesTheActionsOfItsLoadedModel)
{
    for (const std::int32_t map : {0, 2})
    {
        SCOPED_TRACE(map);
        const auto tested = std::make_unique<Replies>();
        tested->world.Runtime().map = map;
        MovementModels fauna;
        tested->world.FaunaResources().Bind(&fauna);
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
        const auto result = Client::Scenes::ApplyWalkReply(Client::Protocol::Movement::Walk{44, {30, 31}, 15},
                                                           Replies::PlayerKey, tested->Resources());
        tested->world.FaunaResources().Bind(nullptr);
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

TEST(MovementRepliesTest, AFailedTransformedPathStopsWithItsLoadedModel)
{
    Replies tested;
    MovementModels fauna;
    tested.world.FaunaResources().Bind(&fauna);
    tested.world.Map().terrain.attributes = std::make_unique<Client::Data::Terrain::TerrainAttributes>();
    auto& other = tested.world.Actors()[8];
    other = Client::Tests::UnarmedPlayer();
    other.alive = true;
    other.key = 44;
    other.type = 0x154;
    other.cellX = 0;
    other.cellY = 21;
    other.movement = 1;
    other.animation.action = 2;

    const auto result = Client::Scenes::ApplyWalkReply(Client::Protocol::Movement::Walk{44, {30, 31}, 15},
                                                       Replies::PlayerKey, tested.Resources());
    tested.world.FaunaResources().Bind(nullptr);

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
