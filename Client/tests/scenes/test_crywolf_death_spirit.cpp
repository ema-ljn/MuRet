#include "Kernel/JointRequest.h"
#include "Kernel/ModelEffectArguments.h"
#include "Effects/CrywolfDeathSpiritEffects.h"
#include "Kernel/ActorState.h"
#include "Effects/CharacterEffectSteps.h"
#include "Joints/FaunaJointCreation.h"
#include "Kernel/DrawTailState.h"
#include "Session/CharacterDrawHooks.h"
#include "Particles/Sprites.h"
#include "Audio/SoundId.h"
#include <vector>
#include <cstddef>
#include <string>
#include "Crt/CrtRandom.h"
#include "Effects/ModelEffects.h"
#include "Effects/GameModelEffects.h"
#include "Particles/Particles.h"
#include <gtest/gtest.h>
#include <glm/ext/vector_float3.hpp>
#include <memory>
#include <optional>
namespace
{
TEST(CrywolfDeathSpiritTest, TheMainSpiritRetainsItsTargetOffsetAndCopiesTheInputAngles)
{
    auto effects = std::make_unique<Client::Effects::ModelEffects>();
    auto particles = std::make_unique<Client::Particles::Particles>();
    Client::Kernel::ActorState owner;
    Client::Crt::CrtRandom random(7);
    const Client::Kernel::ModelEffectArguments args{
        .type = 0x147, .position = {100, 200, 300}, .angles = {11, 22, 33}, .light = {0, 0, 80}, .owner = &owner};
    const auto result = Client::Effects::CreateCrywolfDeathSpirit(args, *effects, particles.get(), random, 0);
    ASSERT_TRUE(result.HasValue());
    ASSERT_TRUE(result.Value());
    const auto& effect = effects->Effects()[*result.Value()];
    EXPECT_EQ(effect.life, 30);
    EXPECT_FLOAT_EQ(effect.turnDirection, 1);
    EXPECT_FLOAT_EQ(effect.speed, 10);
    EXPECT_FLOAT_EQ(effect.transform.scale, .3f);
    EXPECT_FLOAT_EQ(effect.alpha, .8f);
    EXPECT_EQ(effect.velocity, (glm::vec3{0, -26, 0}));
    EXPECT_EQ(effect.start, args.light);
    EXPECT_EQ(effect.copiedAngles, args.angles);
    EXPECT_EQ(effect.transform.angles, (glm::vec3{0, 0, 33}));
    EXPECT_EQ(effect.light, glm::vec3(1));
    EXPECT_EQ(effect.objectState.hiddenMesh, 1);
    EXPECT_EQ(random.State(), 7u);
}
TEST(CrywolfDeathSpiritTest, TheEchoCopiesTheOwnersLifetimeAndEmitsItsBlueFire)
{
    auto effects = std::make_unique<Client::Effects::ModelEffects>();
    auto particles = std::make_unique<Client::Particles::Particles>();
    Client::Kernel::ActorState owner;
    owner.dword60 = 12;
    Client::Crt::CrtRandom random(7), expected(7);
    expected.Discard(3);
    const auto result = Client::Effects::CreateCrywolfDeathSpirit({.type = 0x147, .subtype = 1, .owner = &owner},
                                                                  *effects, particles.get(), random, 0);
    ASSERT_TRUE(result.HasValue());
    ASSERT_TRUE(result.Value());
    const auto& e = effects->Effects()[*result.Value()];
    EXPECT_EQ(e.life, 12);
    EXPECT_FLOAT_EQ(e.transform.scale, .2f);
    EXPECT_FLOAT_EQ(e.alpha, 1.6f);
    EXPECT_EQ(e.light, (glm::vec3{.3f, .4f, 1}));
    EXPECT_EQ(e.velocity, glm::vec3(0));
    EXPECT_TRUE(particles->All()[0].alive);
    EXPECT_EQ(particles->All()[0].type, 0x5db);
    EXPECT_EQ(random.State(), expected.State());
}
TEST(CrywolfDeathSpiritTest, TheFirstTickOnlyIncrementsTheIterationsAndRequestsTheSound)
{
    Client::Kernel::ActorState owner;
    Client::Effects::ModelEffect e;
    e.type = 0x147;
    e.life = 30;
    e.hasOwner = true;
    e.ownerContext = &owner;
    e.turnDirection = 1;
    e.speed = 10;
    Client::Crt::CrtRandom random(7);
    int sound = 0;
    const auto result = Client::Effects::StepCrywolfDeathSpirit(
        e, random, {.sound = [&](Client::Audio::SoundId id) { sound = id.Value(); }});
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_FLOAT_EQ(e.turnDirection, 1.1f);
    EXPECT_FLOAT_EQ(e.speed, 10);
    EXPECT_EQ(sound, 0x1ca);
    EXPECT_EQ(random.State(), 7u);
}
TEST(CrywolfDeathSpiritTest, AFreeLateDecalRequiresTheNativeStackPointWhileAFullPoolDoesNot)
{
    auto effects = std::make_unique<Client::Effects::ModelEffects>();
    Client::Kernel::ActorState owner;
    Client::Effects::ModelEffect e;
    e.type = 0x147;
    e.life = 8;
    e.hasOwner = true;
    e.ownerContext = &owner;
    e.turnDirection = 1.1f;
    e.speed = 10;
    e.velocity = {0, -26, 0};
    Client::Crt::CrtRandom random(7);
    const auto result = Client::Effects::StepCrywolfDeathSpirit(e, random, {.effects = effects.get()});
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x5271a8"), std::string::npos);
    for (auto& slot : effects->Effects())
        slot.alive = true;
    ASSERT_TRUE(Client::Effects::StepCrywolfDeathSpirit(e, random, {.effects = effects.get()}).HasValue());
}
TEST(CrywolfDeathSpiritTest, TrailLifeAndItsFallingSpeedFollowTheNativeBody)
{
    auto effects = std::make_unique<Client::Effects::ModelEffects>();
    auto result = Client::Effects::CreateSpiritTrail({.type = 0x104, .position = {0, 0, 300}}, *effects);
    ASSERT_TRUE(result);
    auto& e = effects->Effects()[*result];
    EXPECT_EQ(e.life, 6);
    EXPECT_FLOAT_EQ(e.turnDirection, 80);
    EXPECT_EQ(e.transform.angles, (glm::vec3{0, 0, 45}));
    Client::Particles::Particles particles;
    Client::Crt::CrtRandom random(1);
    const auto stepped = Client::Effects::StepGameModelEffect(e, nullptr, particles, 0.0f, random);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(e.life, 5);
    EXPECT_FLOAT_EQ(e.transform.position.z, 220);
    EXPECT_FLOAT_EQ(e.turnDirection, 140);
    EXPECT_FLOAT_EQ(e.objectState.light, .3f);
}
TEST(CrywolfDeathSpiritTest, TheFullModelPoolDoesNotReadTheOwnerOrNeedParticles)
{
    auto effects = std::make_unique<Client::Effects::ModelEffects>();
    for (auto& e : effects->Effects())
        e.alive = true;
    Client::Crt::CrtRandom random(7);
    const auto result =
        Client::Effects::CreateCrywolfDeathSpirit({.type = 0x147, .subtype = 1}, *effects, nullptr, random);
    ASSERT_TRUE(result.HasValue());
    EXPECT_FALSE(result.Value());
    EXPECT_EQ(random.State(), 7u);
}
// The point is synthetic: it checks the propagation, not a capture of the original's stack.
TEST(CrywolfDeathSpiritTest, AnInjectedStackPointKeepsTheDecalAndTrailOwnersTyped)
{
    auto effects = std::make_unique<Client::Effects::ModelEffects>();
    auto& e = effects->Effects()[0];
    e.alive = true;
    e.type = 0x147;
    e.life = 8;
    e.turnDirection = 1.1f;
    e.speed = 10;
    e.velocity = {0, -26, 0};
    Client::Kernel::ActorState owner;
    owner.position = {0, -100, 0};
    e.ownerContext = &owner;
    Client::Crt::CrtRandom random(7), expected(7);
    expected.Discard(1);
    const auto result =
        Client::Effects::StepCrywolfDeathSpirit(e, random, {.effects = effects.get()}, glm::vec3{11, 22, 33});
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(effects->Effects()[1].type, 0x61e);
    EXPECT_EQ(effects->Effects()[1].transform.position, (glm::vec3{11, 22, 33}));
    EXPECT_EQ(effects->Effects()[1].owner.effect, &e);
    EXPECT_EQ(effects->Effects()[2].type, 0x104);
    EXPECT_EQ(effects->Effects()[2].owner.effect, &e);
    EXPECT_FLOAT_EQ(e.speed, 10.5f);
    EXPECT_EQ(random.State(), expected.State());
    const auto position = e.transform.position;
    Client::Effects::StepModelEffect(e, nullptr);
    EXPECT_EQ(e.transform.position, position);
    EXPECT_EQ(e.life, 7);
}
TEST(CrywolfDeathSpiritTest, TheLateDecalJointsFollowTheDurableModelEffectOwner)
{
    Client::Effects::ModelEffect parent;
    parent.transform.position = {100, 200, 300};
    Client::Effects::ModelEffect decal;
    decal.alive = true;
    decal.type = 0x61e;
    decal.subtype = 1;
    decal.life = 20;
    decal.ownerContext = &parent;
    decal.owner.effect = &parent;
    std::vector<Client::Kernel::JointState> joints(3);
    Client::Crt::CrtRandom random(7), expected(7);
    const Client::Session::CharacterDrawHooks hooks;
    expected.Discard(13);
    ASSERT_TRUE(Client::Effects::StepPetSummon(decal, joints, &hooks, random).HasValue());
    for (auto& joint : joints)
    {
        EXPECT_TRUE(joint.alive);
        EXPECT_EQ(joint.ownerKind, Client::Kernel::JointOwnerKind::ModelEffect);
        joint.life = 1;
    }
    parent.transform.position = {400, 500, 600};
    Client::Particles::FrameSprites sprites;
    const auto stepped = Client::Joints::StepJoints(joints, random, nullptr, nullptr, 0, &sprites);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    for (std::size_t i = 0; i < 3; ++i)
    {
        EXPECT_EQ(sprites.Sprites()[i].position, (glm::vec3{400, 500, 720}));
        EXPECT_EQ(joints[i].life, 0);
    }
    EXPECT_EQ(random.State(), expected.State());
}
} // namespace
