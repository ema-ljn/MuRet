#include "Pets/PetOrders.h"

#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Crt/CrtRandom.h"
#include "Protocol/Pets/PetOrders.h"
#include "Kernel/ActorSpirit.h"
#include "Models/Cloth.h"
#include "Kernel/ActorState.h"
#include "Pets/DarkSpirit.h"
#include "FrontEnd/LoginModels.h"
#include "FrontEnd/SelectionState.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Camera/GameCamera.h"
#include "Audio/SoundId.h"

#include "support/PlayerActors.h"
#include "support/RecordingSink.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

// The raven's orders (A7 and A8, CL9 milestone 4, 06 § Las órdenes del cuervo), with invented keys.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Kernel::DarkSpirit;
using Client::Protocol::Pets::PetAttack;
using Client::Protocol::Pets::PetOrder;

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
        Client::Pets::ApplyPetAttack(PetAttack{kind, LordKey, MonsterKey}, Resources());
    }

    Client::Kernel::WorldState world;
    Client::FrontEnd::SelectionState selection{world};
    Client::Camera::GameCamera camera;
    Client::Camera::GameCameraInput input;
    Client::Models::ClothWorld cloth;
    Client::FrontEnd::SingleModelLibrary models;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
};

// 0x416510: order 3 follows the key's target.
TEST(RavenOrdersTest, TheFollowOrderTakesTheTarget)
{
    auto tested = std::make_unique<Ravens>();
    Client::Pets::ApplyPetOrder(PetOrder{3, MonsterKey}, tested->world);
    EXPECT_EQ(tested->Raven().order, 3);
    EXPECT_EQ(tested->Raven().byte1ad, 1);
    EXPECT_EQ(tested->Raven().target, MonsterIndex);
    // Without the target, 400.
    Client::Pets::ApplyPetOrder(PetOrder{3, 0x0777}, tested->world);
    EXPECT_EQ(tested->Raven().target, 400u);
}

// The other orders: +0x1ad at 0 except in states 5 and 6, and the target stays.
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
            Client::Pets::ApplyPetOrder(PetOrder{order, LordKey}, tested->world);
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
    Client::Pets::ApplyPetOrder(PetOrder{3, MonsterKey}, tested->world);
    Client::Pets::ApplyPetAttack(PetAttack{0, LordKey, MonsterKey}, tested->Resources());
    EXPECT_FALSE(tested->world.Actors()[LordIndex].darkSpirit.has_value());
    EXPECT_EQ(tested->cloth.random.State(), 1u);
}

// 0x416570 with type 0: state 5, the charge, the speed of rand() % 10 + 20 and sound 0x1d2.
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
    EXPECT_EQ(tested->world.Runtime().lastSoundId, Client::Audio::SoundId{0x1d2});
    EXPECT_EQ(tested->cloth.random.State(), expected.State());
}

// Type 1: state 6 and sound 0x1d0, without rand().
TEST(RavenOrdersTest, TheAttackOfKindOneShouts)
{
    auto tested = std::make_unique<Ravens>();
    tested->Attack(1);
    EXPECT_EQ(tested->Raven().state, 6);
    EXPECT_EQ(tested->Raven().byte1ad, 0);
    EXPECT_EQ(tested->world.Runtime().lastSoundId, Client::Audio::SoundId{0x1d0});
    EXPECT_EQ(tested->cloth.random.State(), 1u);
}

// Type + 5 wraps in the byte: 0xfd is state 2, without sound.
TEST(RavenOrdersTest, TheKindWrapsIntoTheState)
{
    auto tested = std::make_unique<Ravens>();
    tested->Attack(0xfd);
    EXPECT_EQ(tested->Raven().state, 2);
    EXPECT_FALSE(tested->world.Runtime().lastSoundId.has_value());
    EXPECT_EQ(tested->cloth.random.State(), 1u);
}

// With 0x1000 on the owner, only the target; with 0x4000, the target, +0x3b0 and the state, without the charge.
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

TEST(RavenOrdersTest, TheAttackReachesTheRavenOfAnotherLord)
{
    auto tested = std::make_unique<Ravens>();
    tested->world.Runtime().hero = 0;
    tested->Attack(1);
    EXPECT_EQ(tested->Raven().state, 6);
}
} // namespace
