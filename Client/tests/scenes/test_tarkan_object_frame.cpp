#include "MapScripts/Tarkan/TarkanObjectFrame.h"

#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Math/HalfLifeMath.h"
#include "Math/OriginalRotation.h"
#include "Math/RoundedMath.h"
#include "Kernel/PoseBuffer.h"
#include "Effects/ModelEffects.h"
#include "Particles/Particles.h"
#include "Particles/PortableParticleTrigonometry.h"
#include "Particles/Sprites.h"
#include "Kernel/WorldObject.h"
#include "Kernel/ObjectPass.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>

// 0x602ed0 with map 8 (0x603a3e, table 0x6045f8): the bone sprites, the smokes of the shared pool and the 0x53's
// pebble (06 § Los objetos de Tarkan).
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::GameObjectPass;
using Client::Kernel::WorldObject;
using Client::MapScripts::Tarkan::StepTarkanObjectFrame;
using Client::Math::RoundedSin;
using Client::Particles::Particle;

constexpr std::uint8_t TarkanMap = 8;

using Client::Math::D;
using Client::Math::F32;

struct Fixture
{
    std::unique_ptr<Client::Particles::Particles> particles = std::make_unique<Client::Particles::Particles>();
    std::unique_ptr<Client::Particles::FrameSprites> sprites = std::make_unique<Client::Particles::FrameSprites>();
    std::unique_ptr<Client::Effects::ModelEffects> effects = std::make_unique<Client::Effects::ModelEffects>();
    Client::Kernel::PoseBuffer pose;
    CrtRandom random{7};
    float clock = 12345.0f;

    Fixture()
    {
        for (int bone = 0; bone < 8; ++bone)
        {
            Client::Math::BoneTransform matrix(1.0f);
            matrix[3] = glm::vec3(static_cast<float>(bone) * 10.0f, 1.0f, 2.0f);
            pose.bones.push_back(matrix);
        }
    }

    [[nodiscard]] GameObjectPass Pass()
    {
        GameObjectPass pass;
        pass.map = TarkanMap;
        pass.random = &random;
        pass.worldClock = clock;
        pass.particles = particles.get();
        pass.sprites = sprites.get();
        pass.retainedPose = &pose;
        pass.effects = effects.get();
        return pass;
    }

    [[nodiscard]] std::vector<const Particle*> Alive() const
    {
        std::vector<const Particle*> alive;
        for (const Particle& particle : particles->All())
        {
            if (particle.alive)
                alive.push_back(&particle);
        }
        return alive;
    }
};

WorldObject ObjectOf(std::int16_t type)
{
    WorldObject object;
    object.type = type;
    object.transform.scale = 1.25f;
    object.transform.position = {500.0f, 600.0f, 70.0f};
    object.transform.angles = {0.0f, 0.0f, 30.0f};
    object.light = {0.25f, 0.5f, 0.75f};
    return object;
}

// s = sin(phase · 0.002f) · 0.3f + 0.7f, with 0x6b89bc, 0x6b89dc and 0x6b8a5c, unrounded.
double Pulse(double phase)
{
    return (RoundedSin(phase * D(std::bit_cast<float>(0x3b03126fu))) * D(std::bit_cast<float>(0x3e99999au))) +
           D(std::bit_cast<float>(0x3f333333u));
}

// What 0x53fe70(0x5f2, …) leaves in a separate pool with the same seed, to compare slot by slot.
struct Reference
{
    std::unique_ptr<Client::Particles::Particles> particles = std::make_unique<Client::Particles::Particles>();
    CrtRandom random;
    float clock;

    Reference(std::uint32_t seed, float worldClock) : random(seed), clock(worldClock) {}

    void Smoke(const WorldObject& object, const glm::vec3& position, std::int32_t subtype, float scale)
    {
        Client::Particles::PortableParticleTrigonometry trigonometry;
        ASSERT_TRUE(particles
                        ->CreateChecked(Client::Particles::BreathParticle, position, object.transform.angles,
                                        object.light, subtype, scale, std::cref(clock), random, 0, trigonometry)
                        .HasValue());
    }
};

void ExpectSameParticles(const Fixture& fixture, const Reference& reference)
{
    const auto all = fixture.particles->All();
    const auto expected = reference.particles->All();
    for (std::size_t slot = 0; slot < all.size(); ++slot)
    {
        SCOPED_TRACE(slot);
        ASSERT_EQ(all[slot].alive, expected[slot].alive);
        if (!all[slot].alive)
            continue;
        EXPECT_EQ(all[slot].type, expected[slot].type);
        EXPECT_EQ(all[slot].subtype, expected[slot].subtype);
        EXPECT_EQ(all[slot].position, expected[slot].position);
        EXPECT_EQ(all[slot].scale, expected[slot].scale);
        EXPECT_EQ(all[slot].light, expected[slot].light);
        EXPECT_EQ(all[slot].life, expected[slot].life);
        EXPECT_EQ(all[slot].rotation, expected[slot].rotation);
    }
    EXPECT_EQ(fixture.random.State(), reference.random.State());
}

glm::vec3 BonePoint(const Fixture& fixture, std::size_t bone)
{
    return Client::Math::OriginalVectorTransform(glm::vec3(0.0f),
                                                 Client::Math::OriginalMatrixOf(fixture.pose.bones[bone]));
}

// 0x603a5a..0x603abd: the 0 leaves a 0x5c6 on bone 6, of scale stored s times 5 and light
// (s · 0.5882353f; s; s), with phase T and no rand().
TEST(TarkanObjectFrameTest, TheZeroSparksOnItsSixthBone)
{
    Fixture fixture;
    WorldObject object = ObjectOf(0);
    ASSERT_TRUE(StepTarkanObjectFrame(object, fixture.Pass()).HasValue());
    const double pulse = Pulse(D(fixture.clock));
    const float stored = F32(pulse);
    const auto& sprite = fixture.sprites->Sprites()[0];
    ASSERT_TRUE(sprite.alive);
    EXPECT_FALSE(fixture.sprites->Sprites()[1].alive);
    EXPECT_EQ(sprite.texture, 0x5c6);
    EXPECT_EQ(sprite.position, BonePoint(fixture, 6));
    EXPECT_EQ(sprite.scale, F32(D(stored) * 5.0));
    EXPECT_EQ(sprite.light, glm::vec3(F32(pulse * D(std::bit_cast<float>(0x3f169696u))), stored, stored));
    EXPECT_EQ(sprite.rotation, 0.0f);
    EXPECT_EQ(sprite.blend, 0);
    EXPECT_EQ(fixture.random.State(), 7u);
}

// 0x603b0c and 0x603b87: the 0x3f and the 0x40 leave a 0x716 (Impack03) on bone 2 with phase z angle · 5 + T and
// scale s · 1.5; the 0x3f with the 0's light, the 0x40 with (s; s · 0.32f; s · 0.32f).
TEST(TarkanObjectFrameTest, TheImpactsGlowOnTheirSecondBone)
{
    for (const std::int16_t type : {std::int16_t{0x3f}, std::int16_t{0x40}})
    {
        SCOPED_TRACE(type);
        Fixture fixture;
        WorldObject object = ObjectOf(type);
        ASSERT_TRUE(StepTarkanObjectFrame(object, fixture.Pass()).HasValue());
        const double pulse = Pulse((30.0 * 5.0) + D(fixture.clock));
        const float stored = F32(pulse);
        const auto& sprite = fixture.sprites->Sprites()[0];
        ASSERT_TRUE(sprite.alive);
        EXPECT_EQ(sprite.texture, 0x716);
        EXPECT_EQ(sprite.position, BonePoint(fixture, 2));
        EXPECT_EQ(sprite.scale, F32(D(stored) * 1.5));
        const float shade = F32(D(stored) * D(std::bit_cast<float>(0x3ea3d70au)));
        EXPECT_EQ(sprite.light, type == 0x3f
                                    ? glm::vec3(F32(pulse * D(std::bit_cast<float>(0x3f169696u))), stored, stored)
                                    : glm::vec3(stored, shade, shade));
    }
}

TEST(TarkanObjectFrameTest, TheSparksNeedThePoseAndTheSprites)
{
    Fixture fixture;
    WorldObject object = ObjectOf(0);
    GameObjectPass pass = fixture.Pass();
    pass.retainedPose = nullptr;
    const auto unposed = StepTarkanObjectFrame(object, pass);
    ASSERT_FALSE(unposed.HasValue());
    EXPECT_NE(unposed.Error().find("0x4ec110"), std::string::npos) << unposed.Error();

    pass = fixture.Pass();
    pass.sprites = nullptr;
    WorldObject impact = ObjectOf(0x40);
    const auto unsprited = StepTarkanObjectFrame(impact, pass);
    ASSERT_FALSE(unsprited.HasValue());
    EXPECT_NE(unsprited.Error().find("0x603bf7"), std::string::npos) << unsprited.Error();
}

// 0x603acc: the 0x3c, the first time (+0x58 other than −2), twenty 0x5f2/6 with the object's position, angles,
// light and scale; then +0x58 = −2 and it no longer smokes.
TEST(TarkanObjectFrameTest, TheSixtyPuffsTwentyTimesOnce)
{
    Fixture fixture;
    Reference reference(7, fixture.clock);
    WorldObject object = ObjectOf(0x3c);
    ASSERT_TRUE(StepTarkanObjectFrame(object, fixture.Pass()).HasValue());
    for (int puff = 0; puff < 20; ++puff)
        reference.Smoke(object, object.transform.position, 6, object.transform.scale);
    ExpectSameParticles(fixture, reference);
    EXPECT_EQ(fixture.Alive().size(), 20u);
    EXPECT_EQ(fixture.Alive().front()->subtype, 6);
    EXPECT_EQ(object.state.hiddenMesh, Client::Kernel::HiddenObject);

    ASSERT_TRUE(StepTarkanObjectFrame(object, fixture.Pass()).HasValue());
    EXPECT_EQ(fixture.Alive().size(), 20u);
}

// 0x603c06: the 0x46 hides and, with rand() % 5 == 0, leaves a 0x5f2/7.
TEST(TarkanObjectFrameTest, TheSeventyPuffsOneTimeInFive)
{
    for (const std::uint32_t seed : {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u})
    {
        SCOPED_TRACE(seed);
        Fixture fixture;
        fixture.random = CrtRandom(seed);
        Reference reference(seed, fixture.clock);
        WorldObject object = ObjectOf(0x46);
        ASSERT_TRUE(StepTarkanObjectFrame(object, fixture.Pass()).HasValue());
        EXPECT_EQ(object.state.hiddenMesh, Client::Kernel::HiddenObject);
        if (reference.random.Next() % 5 == 0)
            reference.Smoke(object, object.transform.position, 7, object.transform.scale);
        ExpectSameParticles(fixture, reference);
    }
}

// 0x603c4c: the 0x4c hides and, with ftol(T) % 5000 above 4500 (signed), leaves a 0x5f2/4.
TEST(TarkanObjectFrameTest, TheSeventySixPuffsInTheLastTenthOfItsPeriod)
{
    for (const float clock : {14500.0f, 14501.0f, 19999.0f, 20000.0f, -14600.0f})
    {
        SCOPED_TRACE(clock);
        Fixture fixture;
        fixture.clock = clock;
        WorldObject object = ObjectOf(0x4c);
        ASSERT_TRUE(StepTarkanObjectFrame(object, fixture.Pass()).HasValue());
        EXPECT_EQ(object.state.hiddenMesh, Client::Kernel::HiddenObject);
        const bool puffs = clock == 14501.0f || clock == 19999.0f;
        ASSERT_EQ(fixture.Alive().size(), puffs ? 1u : 0u);
        if (puffs)
        {
            Reference reference(7, clock);
            reference.Smoke(object, object.transform.position, 4, object.transform.scale);
            ExpectSameParticles(fixture, reference);
        }
    }
    Fixture fixture;
    fixture.clock = std::numeric_limits<float>::infinity();
    WorldObject object = ObjectOf(0x4c);
    const auto cut = StepTarkanObjectFrame(object, fixture.Pass());
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x603c59"), std::string::npos) << cut.Error();
    EXPECT_EQ(object.state.hiddenMesh, Client::Kernel::HiddenObject);
}

// 0x603c9d..0x603dc8: the 0x53 hides and, with ftol(T) % 10000 between ftol(z angle) · 10 + 3500 and + 4000
// (borders excluded), leaves a 0x5f2/8; with rand() % 3 == 0, also a 0x5f2/4 shifted ((rand() & 127) − 64) in x
// and in y, at half scale, and the pebble 200 + (rand() & 1) on the object.
TEST(TarkanObjectFrameTest, TheEightyThreeSmokesInItsWindowAndSometimesThrowsAStone)
{
    // With angle 30, the window goes from 3800 to 4300.
    for (const float clock : {3800.0f, 3801.0f, 4299.0f, 4300.0f, 13900.0f})
    {
        SCOPED_TRACE(clock);
        Fixture fixture;
        fixture.clock = clock;
        WorldObject object = ObjectOf(0x53);
        ASSERT_TRUE(StepTarkanObjectFrame(object, fixture.Pass()).HasValue());
        EXPECT_EQ(object.state.hiddenMesh, Client::Kernel::HiddenObject);
        const bool inside = clock != 3800.0f && clock != 4300.0f;
        EXPECT_EQ(fixture.Alive().empty(), !inside);
        if (!inside)
        {
            EXPECT_EQ(fixture.random.State(), 7u);
        }
    }
    bool stoned = false;
    bool plain = false;
    for (std::uint32_t seed = 1; seed < 40 && !(stoned && plain); ++seed)
    {
        SCOPED_TRACE(seed);
        Fixture fixture;
        fixture.random = CrtRandom(seed);
        fixture.clock = 4000.0f;
        WorldObject object = ObjectOf(0x53);
        ASSERT_TRUE(StepTarkanObjectFrame(object, fixture.Pass()).HasValue());

        Reference reference(seed, fixture.clock);
        auto effects = std::make_unique<Client::Effects::ModelEffects>();
        reference.Smoke(object, object.transform.position, 8, object.transform.scale);
        if (reference.random.Next() % 3 == 0)
        {
            stoned = true;
            const int dx = (reference.random.Next() & 0x7f) - 0x40;
            const float x = F32(static_cast<double>(dx) + D(object.transform.position.x));
            const int dy = (reference.random.Next() & 0x7f) - 0x40;
            const float y = F32(static_cast<double>(dy) + D(object.transform.position.y));
            reference.Smoke(object, {x, y, object.transform.position.z}, 4, F32(D(object.transform.scale) * 0.5));
            const auto chip = static_cast<std::int16_t>(0xc8 + (reference.random.Next() & 1));
            ASSERT_TRUE(effects
                            ->CreateStoneChip(chip, object.transform.position, object.transform.angles, object.light,
                                              reference.random)
                            .has_value());
        }
        else
            plain = true;
        ExpectSameParticles(fixture, reference);
        for (std::size_t slot = 0; slot < effects->Effects().size(); ++slot)
        {
            const auto& got = fixture.effects->Effects()[slot];
            const auto& want = effects->Effects()[slot];
            ASSERT_EQ(got.alive, want.alive) << slot;
            if (!got.alive)
                continue;
            EXPECT_EQ(got.type, want.type);
            EXPECT_EQ(got.transform.position, want.transform.position);
            EXPECT_EQ(got.velocity, want.velocity);
            EXPECT_EQ(got.life, want.life);
        }
    }
    EXPECT_TRUE(stoned && plain);
}

// The pebble without the effect table cuts at 0x603dc8, with both smokes already created; an angle without a
// credited ftol cuts at 0x603ca7 after hiding.
TEST(TarkanObjectFrameTest, TheEightyThreeCutsWithItsAddresses)
{
    for (std::uint32_t seed = 1;; ++seed)
    {
        CrtRandom probe(seed);
        if (probe.Next() % 3 != 0)
            continue;
        Fixture fixture;
        fixture.random = CrtRandom(seed);
        fixture.clock = 4000.0f;
        GameObjectPass pass = fixture.Pass();
        pass.effects = nullptr;
        WorldObject object = ObjectOf(0x53);
        const auto cut = StepTarkanObjectFrame(object, pass);
        ASSERT_FALSE(cut.HasValue());
        EXPECT_NE(cut.Error().find("0x603dc8"), std::string::npos) << cut.Error();
        EXPECT_EQ(fixture.Alive().size(), 2u);
        break;
    }
    Fixture fixture;
    WorldObject object = ObjectOf(0x53);
    object.transform.angles.z = std::numeric_limits<float>::infinity();
    const auto cut = StepTarkanObjectFrame(object, fixture.Pass());
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x603ca7"), std::string::npos) << cut.Error();
    EXPECT_EQ(object.state.hiddenMesh, Client::Kernel::HiddenObject);

    Fixture empty;
    GameObjectPass pass = empty.Pass();
    pass.particles = nullptr;
    WorldObject smoker = ObjectOf(0x3c);
    const auto unpooled = StepTarkanObjectFrame(smoker, pass);
    ASSERT_FALSE(unpooled.HasValue());
    EXPECT_NE(unpooled.Error().find("0x603af3"), std::string::npos) << unpooled.Error();
}

TEST(TarkanObjectFrameTest, TheOtherTypesDoNothing)
{
    Fixture fixture;
    WorldObject object = ObjectOf(0x51);
    ASSERT_TRUE(StepTarkanObjectFrame(object, fixture.Pass()).HasValue());
    EXPECT_TRUE(fixture.Alive().empty());
    EXPECT_FALSE(fixture.sprites->Sprites()[0].alive);
    EXPECT_EQ(fixture.random.State(), 7u);
    EXPECT_EQ(object.state.hiddenMesh, Client::Models::ObjectState{}.hiddenMesh);
}
} // namespace
