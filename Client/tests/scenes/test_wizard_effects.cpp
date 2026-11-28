#include "Models/IModelLibrary.h"
#include "Effects/WizardEffects.h"

#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Kernel/ModelEffectArguments.h"
#include "Tables/PlayerModels.h"
#include "Models/Skeleton.h"
#include "Models/ModelId.h"
#include "Render/TextureSlot.h"
#include "Kernel/ActorState.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Joints/FaunaJointCreation.h"
#include "Kernel/DrawTailState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldMap.h"
#include "Kernel/GameContext.h"
#include "Kernel/WorldState.h"
#include "Kernel/JointRequest.h"
#include "Effects/ModelEffects.h"
#include "Particles/Sprites.h"
#include "Effects/WizardModelEffects.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace
{
using Client::Kernel::ActorState;

// The game's skeletons (0x5769ad8 + type · 0xe0): Player.bmd with 0xc3 actions and a monster with 6; the rest is
// missing (the record without actions).
class PushedModels final : public Client::Models::IModelLibrary
{
public:
    static constexpr std::uint16_t Monster = 0x10;
    PushedModels()
    {
        player.actions.resize(0xc3);
        monster.actions.resize(6);
    }
    const Client::Data::Models::Model* Find(Client::Models::ModelId) const override
    {
        return nullptr;
    }
    const Client::Models::Skeleton* SkeletonOf(Client::Models::ModelId model) const override
    {
        if (model.Value() == Client::Tables::PlayerSkeletonModel)
            return &player;
        return model.Value() == Monster ? &monster : nullptr;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(Client::Models::ModelId) const override
    {
        return {};
    }
    Client::Models::Skeleton player;
    Client::Models::Skeleton monster;
};

// 0x524a8d..0x524b21: the 0xc2/4 that touches the ground walks the pool's first 400 characters and pushes the live,
// seen (+0x16c) ones that are not the hero ([0x743a170]), not dying (+0x389) and within 200 in x and y: the 0x20a with
// Fenrir (+0x318 at 0x1c37) with 0x4e9cd0 (0x68 with only the right), the 0x20a without it with 0x4ea030(0xc2) and the
// others with 0x4ea030(5), which does not set an action the model lacks. It goes on with all and draws no rand().
TEST(WizardEffectsTest, TheFallingFirePushesWhoIsClose)
{
    constexpr std::size_t Hero = 3;
    constexpr std::uint8_t Standing = 1;
    constexpr std::uint16_t Missing = 0x11;
    constexpr std::uint16_t Sword = 0;
    auto world = std::make_unique<Client::Kernel::WorldState>();
    world->Runtime().hero = Hero;
    const PushedModels models;
    Client::Crt::CrtRandom random(1);
    const Client::Effects::WizardEffectServices services =
        Client::Effects::WizardEffectServicesOf(*world, random, Client::Effects::WizardSceneParts{.models = &models});
    ASSERT_TRUE(services.pushAround);
    const auto place = [&world](std::size_t slot, std::uint16_t type, float x, float y) -> ActorState&
    {
        ActorState& actor = world->Actors()[slot];
        actor.alive = true;
        actor.seen = true;
        actor.dying = 0;
        actor.type = type;
        actor.position = {x, y, 0.0f};
        actor.animation.action = Standing;
        actor.animation.frame = 2.0f;
        return actor;
    };
    const std::uint16_t player = Client::Tables::PlayerSkeletonModel;
    ActorState& justThere = place(7, player, 1000.0f, 1200.0f); // a 200 justo
    ActorState& rider = place(8, player, 1120.0f, 1160.0f);     // the square root of 120² + 160²
    rider.pet.model = 0x1c37;
    rider.hands[0].model = Sword;
    rider.hands[1].model = Client::Kernel::NoItemModel;
    ActorState& monster = place(9, PushedModels::Monster, 900.0f, 900.0f);
    ActorState& unknown = place(10, Missing, 1000.0f, 1000.0f);
    ActorState& far = place(11, player, 1000.0f, 1201.0f);
    ActorState& unseen = place(12, player, 1000.0f, 1000.0f);
    unseen.seen = false;
    ActorState& dying = place(13, player, 1000.0f, 1000.0f);
    dying.dying = 1;
    ActorState& dead = place(14, player, 1000.0f, 1000.0f);
    dead.alive = false;
    ActorState& hero = place(Hero, player, 1000.0f, 1000.0f);
    ActorState& outside = place(Client::Kernel::ActorPoolSize - 1, player, 1000.0f, 1000.0f);

    const auto pushed = services.pushAround(1000.0f, 1000.0f);
    ASSERT_TRUE(pushed.HasValue()) << pushed.Error();
    EXPECT_EQ(justThere.animation.action, 0xc2);
    EXPECT_EQ(justThere.animation.previousAction, Standing);
    EXPECT_EQ(justThere.animation.frame, 0.0f);
    EXPECT_EQ(rider.animation.action, 0x68);
    EXPECT_EQ(monster.animation.action, 5);
    for (const ActorState* untouched : {&unknown, &far, &unseen, &dying, &dead, &hero, &outside})
    {
        EXPECT_EQ(untouched->animation.action, Standing);
        EXPECT_EQ(untouched->animation.frame, 2.0f);
    }
    EXPECT_EQ(random.State(), Client::Crt::CrtRandom(1).State());

    // Without the game's models, the pushing 0x20a has no Player.bmd: it stops at 0x524afc.
    const Client::Effects::WizardEffectServices bare =
        Client::Effects::WizardEffectServicesOf(*world, random, Client::Effects::WizardSceneParts{});
    const auto cut = bare.pushAround(1000.0f, 1000.0f);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x524afc"), std::string::npos) << cut.Error();
}

// 0x524d38: the falling meteor's 0xf6/2 goes through Effects::CreateBlastModelEffect to the world's table, owned by
// the meteor (+0x108, which the step reads live), with key 30 in +0xec and in the scale (30 · 0.01f), life 12 and no
// rand(). An owner that is not a slot of the table cannot be read: it stops.
TEST(WizardEffectsTest, TheFallingMeteorDropsItsBurstIntoTheWorld)
{
    auto world = std::make_unique<Client::Kernel::WorldState>();
    Client::Crt::CrtRandom random(5);
    const Client::Effects::WizardEffectServices services =
        Client::Effects::WizardEffectServicesOf(*world, random, Client::Effects::WizardSceneParts{});
    ASSERT_TRUE(services.otherEffect);
    Client::Effects::ModelEffect& meteor = world->Map().effects.Effects()[3];
    meteor.alive = true;
    meteor.type = Client::Effects::FireEffect;
    meteor.subtype = 7;
    const Client::Kernel::ModelEffectArguments burst{.type = 0xf6,
                                                     .position = {100.0f, 200.0f, 300.0f},
                                                     .angles = {},
                                                     .light = {0.0f, 0.5f, 0.0f},
                                                     .subtype = 2,
                                                     .owner = &meteor,
                                                     .key = 30};
    const auto created = services.otherEffect(burst);
    ASSERT_TRUE(created.HasValue()) << created.Error();
    EXPECT_EQ(random.State(), 5u);
    const Client::Effects::ModelEffect& made = world->Map().effects.Effects()[0];
    ASSERT_TRUE(made.alive);
    EXPECT_EQ(made.type, 0xf6);
    EXPECT_EQ(made.subtype, 2);
    EXPECT_EQ(made.owner.effect, &meteor);
    EXPECT_EQ(made.ownerContext, &meteor);
    EXPECT_EQ(made.life, 12);
    EXPECT_EQ(made.flagEc, 30.0f);
    EXPECT_EQ(made.transform.scale, static_cast<float>(30.0 * static_cast<double>(0.01f)));
    EXPECT_EQ(made.transform.position, glm::vec3(100.0f, 200.0f, 300.0f));
    EXPECT_EQ(made.light, glm::vec3(0.0f, 0.5f, 0.0f));

    Client::Effects::ModelEffect loose = meteor;
    auto outside = burst;
    outside.owner = &loose;
    const auto cut = services.otherEffect(outside);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x518cf0"), std::string::npos) << cut.Error();
    EXPECT_FALSE(world->Map().effects.Effects()[1].alive);
}

// 0x5231f6..0x5232f8 with the world's joints: the 0xcb/1 on its first step (life 45 with +0x1bc at 0) releases 36
// 0x613/6 and 36 0x613/7 joints into 0x530860's table (WorldState::DrawTail), owned by the effect; each rand() % 10 of
// the fan goes before its 0x613/6's creation, which draws its own (the key, rand() % 5). Then the joint step advances
// them without stopping.
TEST(WizardEffectsTest, TheHiddenCircleFansItsRaysIntoTheWorld)
{
    auto world = std::make_unique<Client::Kernel::WorldState>();
    ActorState& caster = world->Actors()[5];
    caster.alive = true;
    caster.byte1bc = 0;
    Client::Particles::FrameSprites sprites;
    Client::Crt::CrtRandom random(21);
    const Client::Effects::WizardEffectServices services = Client::Effects::WizardEffectServicesOf(
        *world, random,
        Client::Effects::WizardSceneParts{
            .particles = &world->Particles(), .worldClock = 1000.0f, .sprites = &sprites});
    const Client::Kernel::ModelEffectArguments arguments = Client::Effects::WizardArguments(
        0xcb, {1000.0f, 2000.0f, 300.0f}, {0.0f, 0.0f, 90.0f}, glm::vec3(1.0f), 1, &caster);
    const auto created = Client::Effects::CreateWizardModelEffect(world->Map().effects, arguments, random, services);
    ASSERT_TRUE(created.HasValue() && created.Value().has_value()) << (created.HasValue() ? "" : created.Error());
    Client::Effects::ModelEffect& circle = world->Map().effects.Effects()[*created.Value()];
    ASSERT_EQ(circle.life, 45);

    auto twin = std::make_unique<std::array<Client::Kernel::JointState, 500>>();
    Client::Crt::CrtRandom oracle = random;
    static_cast<void>(oracle.Next()); // 0x5208c5
    glm::vec3 point{1000.0f, 2000.0f, 400.0f};
    for (int ray = 0; ray < 36; ++ray)
    {
        glm::vec3 angles{-10.0f, 0.0f, static_cast<float>(oracle.Next() % 10 + 10) * static_cast<float>(ray)};
        glm::vec3 second = point;
        for (const std::int32_t subtype : {6, 7})
        {
            if (subtype == 7)
                angles.z = 10.0f * static_cast<float>(ray);
            const Client::Kernel::JointRequest request{.type = 0x613,
                                                       .firstPosition = point,
                                                       .secondPosition = second,
                                                       .angles = angles,
                                                       .subtype = subtype,
                                                       .ownerContext = &circle,
                                                       .scale = 60.0f,
                                                       .key = 0,
                                                       .skill = 0,
                                                       .skillSerial = 0,
                                                       .target = -1};
            ASSERT_TRUE(Client::Joints::CreateFaunaJoint(*twin, request, oracle).HasValue());
        }
    }
    const auto stepped = Client::Effects::StepWizardModelEffect(circle, nullptr, random, services);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(random.State(), oracle.State());
    const auto& joints = world->DrawTail().joints;
    for (std::size_t slot = 0; slot < joints.size(); ++slot)
    {
        SCOPED_TRACE(slot);
        const Client::Kernel::JointState& joint = joints[slot];
        const Client::Kernel::JointState& expected = (*twin)[slot];
        ASSERT_EQ(joint.alive, expected.alive);
        if (!joint.alive)
            continue;
        EXPECT_EQ(joint.type, 0x613);
        EXPECT_EQ(joint.subtype, expected.subtype);
        EXPECT_EQ(joint.key, expected.key);
        EXPECT_EQ(joint.position, expected.position);
        EXPECT_EQ(joint.angles, expected.angles);
        EXPECT_EQ(joint.light, expected.light);
        EXPECT_EQ(joint.width, 60.0f);
        EXPECT_EQ(joint.ownerContext, &circle);
    }
    EXPECT_EQ(std::ranges::count_if(joints, [](const Client::Kernel::JointState& joint) { return joint.alive; }), 72);

    const auto advanced =
        Client::Joints::StepJoints(world->DrawTail().joints, random, nullptr, &world->Particles(), 1000.0f, &sprites);
    ASSERT_TRUE(advanced.HasValue()) << advanced.Error();
}
} // namespace
