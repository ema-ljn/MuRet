#include "Crt/CrtRandom.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
#include "Core/Expected.h"
#include "Render/ModelBatch.h"
#include "Render/Pass3D.h"
#include "World/IParticleTrigonometry.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "Terrain/TerrainLighting.h"
#include "support/ParticleDrawRecorder.h"
#include <gtest/gtest.h>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
namespace
{
struct Trigonometry final : Client::World::IParticleTrigonometry
{
    Client::Core::Expected<double, Client::World::ParticleError> SineProductPC53(double, float) const override
    {
        return 2.0;
    }
    Client::Core::Expected<double, Client::World::ParticleError> CosineProductPC53(double, float) const override
    {
        return 3.0;
    }
};
TEST(CrywolfArrowDependenciesTest, TheJointCopiesItsRequestAndKeepsTheNativeKeyLifeAndColor)
{
    std::vector<Client::Scenes::JointState> joints(1);
    Client::World::ModelEffect owner;
    owner.alive = true;
    Client::Crt::CrtRandom random(7);
    glm::vec3 position{100, 200, 300}, target{400, 500, 600}, angles{};
    const auto result =
        Client::Scenes::CreateFaunaJoint(joints, {0x610, position, target, angles, 13, &owner, 20, 40, 0, 0, -1},
                                         random, Client::Scenes::JointOwnerKind::ModelEffect);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    ASSERT_TRUE(result.Value());
    const auto& joint = joints[0];
    EXPECT_EQ(joint.life, 40);
    EXPECT_EQ(joint.key, 0);
    EXPECT_EQ(joint.maximumCount, 16);
    EXPECT_FLOAT_EQ(joint.speed, 70);
    EXPECT_FLOAT_EQ(joint.width, 20);
    EXPECT_EQ(joint.blend, 4);
    EXPECT_EQ(joint.light, (glm::vec3{.7f, .7f, .3f}));
    EXPECT_EQ(joint.targetPosition, (glm::vec3{400, 500, 600}));
    EXPECT_EQ(random.State(), 7u);
}
// 0x537d2e..0x537d43: la /0xc (la estela de la flecha 0x11c) va al mismo paso que la /13 (0x537ee6).
TEST(CrywolfArrowDependenciesTest, TheJointMovesBeforeCheckingItsOwnerAndStillRunsItsLightAndTail)
{
    for (const std::int32_t subtype : {12, 13})
        for (const bool alive : {true, false})
        {
            SCOPED_TRACE(subtype);
            std::vector<Client::Scenes::JointState> joints(1);
            auto& joint = joints[0];
            Client::World::ModelEffect owner;
            owner.alive = alive;
            joint.alive = true;
            joint.type = 0x610;
            joint.subtype = subtype;
            joint.ownerContext = &owner;
            joint.ownerKind = Client::Scenes::JointOwnerKind::ModelEffect;
            joint.position = {100, 200, 300};
            joint.speed = 70;
            joint.width = 20;
            joint.life = 10;
            joint.maximumCount = 16;
            joint.tailEnabled = true;
            joint.drawEnabled = 3;
            joint.light = {.7f, .7f, .3f};
            auto lighting = std::make_unique<Client::World::TerrainLighting>();
            Client::Crt::CrtRandom random(7), expected(7);
            expected.Discard(1);
            const auto result =
                Client::Scenes::StepJoints(joints, random, nullptr, nullptr, 0, nullptr, lighting.get());
            ASSERT_TRUE(result.HasValue()) << result.Error();
            EXPECT_EQ(joint.alive, alive);
            EXPECT_EQ(joint.life, 9);
            EXPECT_EQ(joint.count, 1);
            EXPECT_EQ(joint.position, (glm::vec3{100, 130, 300}));
            EXPECT_EQ(random.State(), expected.State());
            EXPECT_FLOAT_EQ(joint.light.x,
                            static_cast<float>(static_cast<double>(.7f) * static_cast<double>(0.8333333134651184f)));
            if (alive)
            {
                Client::Render::ModelBatch batch;
                batch.Begin(Client::Render::Pass3D{});
                ASSERT_TRUE(Client::Scenes::DrawJoints(batch, joints, 0).HasValue());
                EXPECT_EQ(batch.Vertices().size(), 12u);
            }
        }
}
TEST(CrywolfArrowDependenciesTest, TheFallingOrbitUsesFourDrawsAndDrawsAtLifeForty)
{
    auto particles = std::make_unique<Client::World::Particles>();
    Client::Crt::CrtRandom random(7), expected(7);
    const int rise = expected.Next() % 100, phase = expected.Next() % 300 - 150, scale = expected.Next() % 6,
              rotation = expected.Next() % 360;
    Trigonometry trig;
    const float clock = 0;
    const auto made =
        particles->CreateChecked(0x60f, {100, 200, 300}, {}, {.4f, 1, .2f}, 5, .2f, std::cref(clock), random, 0, trig);
    ASSERT_TRUE(made.HasValue());
    ASSERT_TRUE(made.Value());
    auto& p = particles->All()[*made.Value()];
    EXPECT_EQ(p.life, 40);
    EXPECT_FLOAT_EQ(p.verticalSpeed,
                    static_cast<float>((static_cast<double>(rise) * static_cast<double>(.01f)) * 5.0 + 1.0));
    EXPECT_FLOAT_EQ(p.velocity.x, static_cast<float>(phase));
    EXPECT_FLOAT_EQ(
        p.scale, static_cast<float>(static_cast<double>(scale) * static_cast<double>(.01f) + static_cast<double>(.2f)));
    EXPECT_FLOAT_EQ(p.rotation, static_cast<float>(rotation));
    EXPECT_EQ(random.State(), expected.State());
    const auto draw = Client::Tests::DrawOneParticle(p, glm::vec2{100, 200}, std::uint8_t{3});
    ASSERT_TRUE(draw.HasValue());
    ASSERT_TRUE(draw.Value());
    EXPECT_EQ(draw.Value()->texture, 0x60f);
}
TEST(CrywolfArrowDependenciesTest, TheFallingOrbitDescendsAndMovesOnlyItsAnchorX)
{
    auto particles = std::make_unique<Client::World::Particles>();
    auto& p = particles->All()[0];
    p.alive = true;
    p.type = 0x60f;
    p.texture = 0x60f;
    p.subtype = 5;
    p.life = 21;
    p.scale = .2f;
    p.position = {100, 200, 300};
    p.start = {100, 200, 300};
    p.verticalSpeed = 3;
    p.light = {.4f, 1, .2f};
    Client::Crt::CrtRandom random(7);
    Trigonometry trig;
    const float clock = 0;
    const std::int32_t map = 34;
    ASSERT_TRUE(Client::World::StepParticlesChecked(*particles, random, std::cref(clock), &map, trig).HasValue());
    EXPECT_EQ(p.life, 20);
    EXPECT_EQ(p.position, (glm::vec3{102, 197, 297}));
    EXPECT_EQ(p.start, (glm::vec3{102.5f, 200, 300}));
    EXPECT_FLOAT_EQ(p.scale, static_cast<float>(static_cast<double>(.2f) - static_cast<double>(.002f)));
    EXPECT_FLOAT_EQ(p.light.y, 0.9090908765792847f);
}
TEST(CrywolfArrowDependenciesTest, TheFullPoolsDoNotReadTheOwnerOrAdvanceTheConstructorRandom)
{
    std::vector<Client::Scenes::JointState> joints(1);
    joints[0].alive = true;
    Client::Crt::CrtRandom random(7);
    glm::vec3 empty{};
    const auto joint =
        Client::Scenes::CreateFaunaJoint(joints, {0x610, empty, empty, empty, 13, nullptr, 20, 40, 0, 0, -1}, random,
                                         Client::Scenes::JointOwnerKind::ModelEffect);
    ASSERT_TRUE(joint.HasValue());
    EXPECT_FALSE(joint.Value());
    auto particles = std::make_unique<Client::World::Particles>();
    for (auto& p : particles->All())
        p.alive = true;
    Trigonometry trig;
    const float clock = 0;
    const auto particle = particles->CreateChecked(0x60f, {}, {}, {}, 5, .2f, std::cref(clock), random, 0, trig);
    ASSERT_TRUE(particle.HasValue());
    EXPECT_FALSE(particle.Value());
    EXPECT_EQ(random.State(), 7u);
}
} // namespace
