#include "World/Particles.h"
#include "World/ParticleOwner.h"
#include "World/PortableParticleTrigonometry.h"
#include "Render/Models/Cloth.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <glm/ext/vector_float2.hpp>
#include <cstdint>
#include <functional>

namespace
{
using Client::Render::Models::CrtRandom;
using Client::World::Particles;
struct Creation
{
    std::int32_t type;
    std::int32_t subtype;
    std::int32_t life;
    std::uint32_t randomState;
};
constexpr std::array Cases{Creation{0x5ac, 6, 11, 0xbb6f8e98u}, Creation{0x5ac, 7, 31, 0xbb6f8e98u},
                           Creation{0x6ed, 0, 24, 0xc40e66b9u}, Creation{0x6f0, 0, 30, 0xc40e66b9u},
                           Creation{0x6f2, 0, 20, 0x9974714du}, Creation{0x6f3, 0, 20, 0x9974714du},
                           Creation{0x6f4, 0, 30, 0xc40e66b9u}};
} // namespace

TEST(KalimaParticlesTest, PublicCreatorsKeepLifeRandomOrdinalAndOriginalStart)
{
    for (const Creation& c : Cases)
    {
        SCOPED_TRACE(c.type);
        Particles particles;
        CrtRandom random(7);
        particles.All()[0].field38 = 99;
        particles.All()[0].rotationFactor = 123;
        const auto made =
            particles.CreateChecked(c.type, {100, 200, 300}, {}, {0.3f, 0.8f, 1}, c.subtype, 1, 99, random);
        ASSERT_TRUE(made.HasValue());
        ASSERT_EQ(made.Value(), 0u);
        const auto& p = particles.All()[0];
        EXPECT_EQ(p.life, c.life);
        EXPECT_EQ(p.start, glm::vec3(100, 200, 300));
        EXPECT_EQ(random.State(), c.randomState);
        EXPECT_EQ(p.field38, 99u);
        EXPECT_EQ(p.rotationFactor, 123);
    }
}

TEST(KalimaParticlesTest, AmbientSevenLoadsLiveClockAndStillRunsItsCaseOnTheDyingTick)
{
    Particles particles;
    auto& p = particles.All()[0];
    p.alive = true;
    p.type = p.texture = 0x5ac;
    p.subtype = 7;
    p.life = 1;
    p.scale = 0.4f;
    p.light = {0.3f, 0.8f, 1};
    float clock = 0;
    CrtRandom random(7);
    Client::World::PortableParticleTrigonometry trig;
    const auto result = Client::World::StepParticlesChecked(particles, random, std::cref(clock), nullptr, trig);
    ASSERT_TRUE(result.HasValue());
    EXPECT_FALSE(p.alive);
    EXPECT_EQ(p.life, 0);
    EXPECT_EQ(p.position.z, 10);
    EXPECT_EQ(p.rotation, 30);
    EXPECT_EQ(p.scale, 0.4f);
    EXPECT_FLOAT_EQ(p.light.z, 0.8333333134651184f);
    EXPECT_EQ(random.State(), 0xc40e66b9u);
}

TEST(KalimaParticlesTest, FootstepSubtypeOneKeepsColorAddsFiftyAndUsesOneCreationDraw)
{
    Particles particles;
    CrtRandom random(7);
    const auto made = particles.CreateChecked(0x6f4, {100, 200, 300}, {}, {0.3f, 0.3f, 0.3f}, 1, 9, 0, random);
    ASSERT_TRUE(made.HasValue());
    const auto& p = particles.All()[0];
    EXPECT_EQ(p.life, 20);
    EXPECT_EQ(p.position, glm::vec3(100, 200, 350));
    EXPECT_EQ(p.start, glm::vec3(100, 200, 300));
    EXPECT_EQ(p.scale, 1);
    EXPECT_EQ(p.rotation, 61);
    EXPECT_EQ(p.light, glm::vec3(0.3f));
    EXPECT_EQ(random.State(), 0x003d7aaeu);
}

TEST(KalimaParticlesTest, MistUsesSceneAndSixDrawsThenMovesWithPreviousVelocity)
{
    Particles particles;
    CrtRandom random(7);
    const Client::World::ParticleOwner owner{Client::World::ParticleOwnerKind::Actor, 0x100000042ull};
    const auto made = particles.CreateKalimaChecked(0x6f1, {100, 200, 300}, {}, {}, 0, 99, 0, random, 5, owner);
    ASSERT_TRUE(made.HasValue());
    auto& p = particles.All()[0];
    EXPECT_EQ(p.position, glm::vec3(90, 191, 316));
    EXPECT_EQ(p.start, glm::vec3(100, 200, 300));
    EXPECT_EQ(p.velocity, glm::vec3(0, 0, -5));
    EXPECT_EQ(p.rotation, 61);
    EXPECT_EQ(p.scale, 1);
    EXPECT_EQ(p.light, glm::vec3(0.4f));
    EXPECT_EQ(p.typedOwner, owner);
    EXPECT_EQ(random.State(), 0x9974714du);
    ASSERT_TRUE(Client::World::StepParticlesChecked(particles, random).HasValue());
    EXPECT_EQ(p.position, glm::vec3(90, 191, 311));
    EXPECT_EQ(p.velocity, glm::vec3(-0.3f, -0.6f, -4.9f));
    EXPECT_FLOAT_EQ(p.scale, 1.03f);
    EXPECT_EQ(p.life, 29);
    EXPECT_EQ(random.State(), 0x4f780a21u);
    Particles selection;
    CrtRandom selectionRandom(7);
    ASSERT_TRUE(selection.CreateKalimaChecked(0x6f1, {}, {}, {}, 0, 1, 0, selectionRandom, 4).HasValue());
    EXPECT_EQ(selection.All()[0].light, glm::vec3(0.25f));
}

TEST(KalimaParticlesTest, FullPoolDoesNotConsumeCreatorRandomAndClearRetainsWind)
{
    Particles particles;
    for (auto& p : particles.All())
        p.alive = true;
    particles.Wind().wind = {1, 2};
    CrtRandom random(7);
    const auto made = particles.CreateKalimaChecked(0x6f1, {}, {}, {}, 0, 1, 0, random, 5);
    ASSERT_TRUE(made.HasValue());
    EXPECT_FALSE(made.Value());
    EXPECT_EQ(random.State(), 7u);
    particles.Clear();
    EXPECT_EQ(particles.Wind().wind.x, 1);
    EXPECT_EQ(particles.Wind().wind.y, 2);
    EXPECT_FALSE(particles.Any());
}

TEST(KalimaParticlesTest, AllNineReachableLifecyclesExpireWithTheNativeRandomBudget)
{
    Particles particles;
    CrtRandom random(7);
    for (const Creation& c : Cases)
        ASSERT_TRUE(particles.CreateKalimaChecked(c.type, {}, {}, {1, 1, 1}, c.subtype, 1, 0, random, 5).HasValue());
    ASSERT_TRUE(particles.CreateKalimaChecked(0x6f1, {}, {}, {}, 0, 1, 0, random, 5).HasValue());
    ASSERT_TRUE(particles.CreateKalimaChecked(0x6f4, {}, {}, {0.3f, 0.3f, 0.3f}, 1, 1, 0, random, 5).HasValue());
    float clock = 0;
    Client::World::PortableParticleTrigonometry trigonometry;
    for (int tick = 0; tick < 50; ++tick)
    {
        clock = static_cast<float>(tick);
        ASSERT_TRUE(
            Client::World::StepParticlesChecked(particles, random, std::cref(clock), nullptr, trigonometry).HasValue());
    }
    EXPECT_FALSE(particles.Any());
    // 31 sorteos de creación; 22 de5ac/6,60 de6f1,40 de6f4/1;100 de viento.
    EXPECT_EQ(random.State(), 0x0460ebcau);
}

TEST(KalimaParticlesTest, FlareSixUsesTheNativeDriftBitsAfterWindAndAsymmetricSpills)
{
    Particles particles;
    auto& p = particles.All()[0];
    p.alive = true;
    p.type = p.texture = 0x5ac;
    p.subtype = 6;
    p.life = 20;
    p.scale = 1.0f;
    CrtRandom random(7);
    ASSERT_TRUE(Client::World::StepParticlesChecked(particles, random).HasValue());
    EXPECT_EQ(std::bit_cast<std::uint32_t>(p.position.x), 0x3d2a64c4u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(p.position.y), 0x3bf27bb4u);
    EXPECT_EQ(p.position.z, 2.5f);
    EXPECT_EQ(p.life, 19);
    EXPECT_EQ(random.State(), 0x1b8052fbu);
}

TEST(KalimaParticlesTest, AbsentMistSceneRetainsTheInitializeAndThreeDrawPrefix)
{
    Particles particles;
    auto& p = particles.All()[0];
    p.type = 0x67;
    p.life = -17;
    p.position = {-1, -2, -3};
    p.start = {-4, -5, -6};
    p.light = {9, 8, 7};
    p.scale = 7;
    p.rotation = 123;
    p.velocity = {8, 9, 10};
    p.field38 = 0x1234u;
    p.rotationFactor = 111;
    p.field64 = {123, 456};
    p.typedOwner = {Client::World::ParticleOwnerKind::WorldObject, 0x100000042ull};
    particles.All()[1].life = 211;
    const glm::vec3 position{100, 200, 300}, angles{10, 20, 30}, light{0.3f, 0.8f, 1};
    CrtRandom random(7);
    const auto made = particles.CreateChecked(0x6f1, position, angles, light, 0, 7.0f, 99.0f, random, 0x12345678u);
    ASSERT_FALSE(made.HasValue());
    EXPECT_EQ(made.Error(), Client::World::ParticleError::MissingScene);
    EXPECT_TRUE(p.alive);
    EXPECT_TRUE(p.moves);
    EXPECT_EQ(p.type, 0x6f1);
    EXPECT_EQ(p.texture, 0x6f1);
    EXPECT_EQ(p.subtype, 0);
    EXPECT_EQ(p.life, 30);
    EXPECT_EQ(p.rotation, 61);
    EXPECT_EQ(p.scale, 1);
    EXPECT_EQ(p.velocity, glm::vec3(0)); // 543fd8 aún no se alcanzó.
    EXPECT_EQ(p.position, position);
    EXPECT_EQ(p.start, position);
    EXPECT_EQ(p.angles, angles);
    EXPECT_EQ(p.light, light);
    EXPECT_EQ(p.owner, 0x12345678u);
    EXPECT_EQ(p.typedOwner.kind, Client::World::ParticleOwnerKind::None);
    EXPECT_EQ(p.verticalSpeed, 0);
    EXPECT_EQ(p.field48, 0);
    EXPECT_EQ(p.field38, 0x1234u);
    EXPECT_EQ(p.rotationFactor, 111);
    EXPECT_EQ(p.field64, glm::vec2(123, 456));
    EXPECT_EQ(particles.All()[1].life, 211);
    EXPECT_EQ(random.State(), 0xbb6f8e98u);
}

TEST(KalimaParticlesTest, FullLegacyMistPoolSkipsAbsentSceneAndAllRandom)
{
    Particles particles;
    for (auto& p : particles.All())
        p.alive = true;
    particles.All()[0].position = {1, 2, 3};
    CrtRandom random(7);
    const auto made = particles.CreateChecked(0x6f1, {}, {}, {}, 0, 1.0f, 0.0f, random);
    ASSERT_TRUE(made.HasValue());
    EXPECT_FALSE(made.Value());
    EXPECT_EQ(particles.All()[0].position, glm::vec3(1, 2, 3));
    EXPECT_EQ(random.State(), 7u);
}
