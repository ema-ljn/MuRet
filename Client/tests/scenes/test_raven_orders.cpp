#include "Scenes/PetOrders.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Protocol/Pet/PetOrders.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorState.h"
#include "Scenes/DarkSpirit.h"
#include "Scenes/LoginModels.h"
#include "Scenes/SelectionState.h"
#include "Scenes/WorldEntry.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/GameCamera.h"

#include "support/PlayerActors.h"
#include "support/RecordingSink.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

// Las órdenes del cuervo (A7 y A8, CL9 hito 4, 06 § Las órdenes del cuervo), con claves inventadas.
namespace
{
using Client::Protocol::Pet::PetAttack;
using Client::Protocol::Pet::PetOrder;
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorState;
using Client::Scenes::DarkSpirit;

constexpr std::uint16_t LordKey = 0x0101;
constexpr std::uint16_t MonsterKey = 0x0202;
constexpr std::size_t LordIndex = 4;
constexpr std::size_t MonsterIndex = 9;

struct Ravens
{
    Ravens()
    {
        world.Runtime().hero = LordIndex;
        ActorState lord = Client::Tests::UnarmedPlayer(4);
        lord.alive = true;
        lord.key = LordKey;
        lord.darkSpirit.emplace();
        world.Actors()[LordIndex] = lord;
        ActorState monster;
        monster.alive = true;
        monster.key = MonsterKey;
        world.Actors()[MonsterIndex] = monster;
    }
    DarkSpirit& Raven()
    {
        return *world.Actors()[LordIndex].darkSpirit;
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
    void Attack(std::uint8_t kind)
    {
        Client::Scenes::ApplyPetAttack(PetAttack{kind, LordKey, MonsterKey}, Resources());
    }

    Client::Scenes::WorldState world;
    Client::Scenes::SelectionState selection{world};
    Client::World::GameCamera camera;
    Client::World::GameCameraInput input;
    Client::Render::Models::ClothWorld cloth;
    Client::Scenes::SingleModelLibrary models;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
};

// 0x416510: la orden 3 sigue al blanco de la clave.
TEST(RavenOrdersTest, TheFollowOrderTakesTheTarget)
{
    auto tested = std::make_unique<Ravens>();
    Client::Scenes::ApplyPetOrder(PetOrder{3, MonsterKey}, tested->world);
    EXPECT_EQ(tested->Raven().order, 3);
    EXPECT_EQ(tested->Raven().byte1ad, 1);
    EXPECT_EQ(tested->Raven().target, MonsterIndex);
    // Sin el blanco, el 400.
    Client::Scenes::ApplyPetOrder(PetOrder{3, 0x0777}, tested->world);
    EXPECT_EQ(tested->Raven().target, 400u);
}

// Las demás órdenes: +0x1ad en 0 salvo en los estados 5 y 6, y el blanco queda.
TEST(RavenOrdersTest, TheOtherOrdersDropTheChargeOutsideTheAttackStates)
{
    for (const std::uint8_t order : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{2}})
    {
        for (const auto& [state, kept] : {std::pair{std::uint8_t{0}, false}, std::pair{std::uint8_t{5}, true},
                                          std::pair{std::uint8_t{6}, true}, std::pair{std::uint8_t{2}, false}})
        {
            auto tested = std::make_unique<Ravens>();
            tested->Raven().state = state;
            tested->Raven().byte1ad = 1;
            tested->Raven().target = MonsterIndex;
            Client::Scenes::ApplyPetOrder(PetOrder{order, LordKey}, tested->world);
            EXPECT_EQ(tested->Raven().order, order);
            EXPECT_EQ(tested->Raven().byte1ad, kept ? 1 : 0) << int{order} << " " << int{state};
            EXPECT_EQ(tested->Raven().target, MonsterIndex);
        }
    }
}

TEST(RavenOrdersTest, WithoutTheRavenTheOrderDoesNothing)
{
    auto tested = std::make_unique<Ravens>();
    tested->world.Actors()[LordIndex].darkSpirit.reset();
    Client::Scenes::ApplyPetOrder(PetOrder{3, MonsterKey}, tested->world);
    Client::Scenes::ApplyPetAttack(PetAttack{0, LordKey, MonsterKey}, tested->Resources());
    EXPECT_FALSE(tested->world.Actors()[LordIndex].darkSpirit.has_value());
    EXPECT_EQ(tested->cloth.random.State(), 1u);
}

// 0x416570 con el tipo 0: el estado 5, la carga, la velocidad de rand() % 10 + 20 y el sonido 0x1d2.
TEST(RavenOrdersTest, TheAttackOfKindZeroCharges)
{
    auto tested = std::make_unique<Ravens>();
    DarkSpirit& raven = tested->Raven();
    raven.far = 40;
    raven.byte38d = 1;
    CrtRandom expected = tested->cloth.random;
    const int roll = expected.Next() % 10;
    tested->Attack(0);
    EXPECT_EQ(raven.state, 5);
    EXPECT_EQ(raven.far, 0);
    EXPECT_EQ(raven.byte38d, 0);
    EXPECT_EQ(raven.byte1ad, 1);
    EXPECT_EQ(raven.floatF0, 0.5f);
    EXPECT_EQ(raven.speed, static_cast<float>(roll) + 20.0f);
    EXPECT_EQ(raven.target, MonsterIndex);
    EXPECT_EQ(raven.target3b0, MonsterIndex);
    EXPECT_EQ(tested->world.Runtime().lastSoundId, 0x1d2);
    EXPECT_EQ(tested->cloth.random.State(), expected.State());
}

// El tipo 1: el estado 6 y el sonido 0x1d0, sin rand().
TEST(RavenOrdersTest, TheAttackOfKindOneShouts)
{
    auto tested = std::make_unique<Ravens>();
    tested->Attack(1);
    EXPECT_EQ(tested->Raven().state, 6);
    EXPECT_EQ(tested->Raven().byte1ad, 0);
    EXPECT_EQ(tested->world.Runtime().lastSoundId, 0x1d0);
    EXPECT_EQ(tested->cloth.random.State(), 1u);
}

// El tipo + 5 envuelve en el byte: 0xfd es el estado 2, sin sonido.
TEST(RavenOrdersTest, TheKindWrapsIntoTheState)
{
    auto tested = std::make_unique<Ravens>();
    tested->Attack(0xfd);
    EXPECT_EQ(tested->Raven().state, 2);
    EXPECT_FALSE(tested->world.Runtime().lastSoundId.has_value());
    EXPECT_EQ(tested->cloth.random.State(), 1u);
}

// Con 0x1000 en el dueño, solo el blanco; con 0x4000, el blanco, +0x3b0 y el estado, sin la carga.
TEST(RavenOrdersTest, TheOwnerFlagsCutTheAttackShort)
{
    auto stopped = std::make_unique<Ravens>();
    stopped->world.Actors()[LordIndex].flags78 = 0x1000;
    stopped->Raven().target3b0 = 7;
    stopped->Attack(0);
    EXPECT_EQ(stopped->Raven().target, MonsterIndex);
    EXPECT_EQ(stopped->Raven().target3b0, 7);
    EXPECT_EQ(stopped->Raven().state, 0);

    auto hidden = std::make_unique<Ravens>();
    hidden->world.Actors()[LordIndex].flags78 = 0x4000;
    hidden->Raven().byte38d = 1;
    hidden->Attack(0);
    EXPECT_EQ(hidden->Raven().target3b0, MonsterIndex);
    EXPECT_EQ(hidden->Raven().state, 5);
    EXPECT_EQ(hidden->Raven().byte38d, 1);
    EXPECT_EQ(hidden->Raven().byte1ad, 0);
    EXPECT_EQ(hidden->cloth.random.State(), 1u);
}

// El cuervo de otro Dark Lord también obedece el A8.
TEST(RavenOrdersTest, TheAttackReachesTheRavenOfAnotherLord)
{
    auto tested = std::make_unique<Ravens>();
    tested->world.Runtime().hero = 0;
    tested->Attack(1);
    EXPECT_EQ(tested->Raven().state, 6);
}
} // namespace
