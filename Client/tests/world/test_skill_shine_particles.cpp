#include "Particles/Particles.h"

#include "Crt/CrtRandom.h"
#include "Math/RoundedMath.h"
#include "Particles/ParticleOwner.h"
#include "Particles/PortableParticleTrigonometry.h"

#include "support/ParticleDrawRecorder.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

// The weapon skills' shine 5fe: 0x542bff (creation), 0x546d54 (step) and 0x548550's common billboard (0x54889a). The
// owner's bone (0x53fdb0) is given by the requester. 06, "Lo que mandan las skills de las armas".
namespace
{
using Client::Crt::CrtRandom;
using Client::Math::RoundedSin;
using Client::Particles::Particle;
using Client::Particles::ParticleOwner;
using Client::Particles::ParticleOwnerKind;
using Client::Particles::Particles;
using Client::Particles::SkillShineParticle;

constexpr std::uint32_t Seed = 0x1357bdfu;
constexpr ParticleOwner HeroOwner{.kind = ParticleOwnerKind::Actor, .identity = 3};

// A bone that moves with the life, to see that the step reads it again.
std::optional<glm::vec3> BonePoint(const Particle& particle)
{
    if (particle.typedOwner != HeroOwner)
        return std::nullopt;
    return glm::vec3{100.0f + static_cast<float>(particle.life), 200.0f, 50.0f};
}

// 0x542bff..0x542c07: the common prefix, life 18 and 0x53fdb0, which overwrites the position with the bone's. Without
// rand(); the requested scale (0) stays. With the pool full, nothing; without the owner, MissingOwner with the slot
// off.
TEST(SkillShineParticlesTest, TheShineIsBornOnTheOwnersBoneWithoutDraws)
{
    Particles particles;
    CrtRandom random(Seed);
    const auto created = particles.CreateFollowingChecked(SkillShineParticle, {1.0f, 2.0f, 3.0f}, {0.0f, 0.0f, 90.0f},
                                                          glm::vec3(1.0f), 0, 0.0f, HeroOwner, BonePoint);
    ASSERT_TRUE(created.HasValue());
    ASSERT_EQ(created.Value(), std::optional<std::size_t>{0});
    EXPECT_EQ(random.State(), CrtRandom(Seed).State());
    const Particle& shine = particles.All()[0];
    EXPECT_TRUE(shine.alive);
    EXPECT_EQ(shine.type, SkillShineParticle);
    EXPECT_EQ(shine.texture, SkillShineParticle);
    EXPECT_EQ(shine.life, 18);
    EXPECT_EQ(shine.scale, 0.0f);
    EXPECT_EQ(shine.position, (glm::vec3{118.0f, 200.0f, 50.0f}));
    EXPECT_EQ(shine.start, (glm::vec3{1.0f, 2.0f, 3.0f}));
    EXPECT_EQ(shine.light, glm::vec3(1.0f));
    EXPECT_EQ(shine.typedOwner, HeroOwner);
    EXPECT_TRUE(shine.moves);

    const auto lost = particles.CreateFollowingChecked(SkillShineParticle, {}, {}, glm::vec3(1.0f), 0, 0.0f,
                                                       ParticleOwner{}, BonePoint);
    ASSERT_FALSE(lost.HasValue());
    EXPECT_EQ(lost.Error(), Client::Particles::ParticleError::MissingOwner);
    EXPECT_FALSE(particles.All()[1].alive);

    const auto other = particles.CreateFollowingChecked(0x5e4, {}, {}, glm::vec3(1.0f), 0, 0.0f, HeroOwner, BonePoint);
    ASSERT_FALSE(other.HasValue());
    EXPECT_EQ(other.Error(), Client::Particles::ParticleError::UnsupportedType);

    for (Particle& particle : particles.All())
        particle.alive = true;
    const auto full =
        particles.CreateFollowingChecked(SkillShineParticle, {}, {}, glm::vec3(1.0f), 0, 0.0f, HeroOwner, BonePoint);
    ASSERT_TRUE(full.HasValue());
    EXPECT_FALSE(full.Value().has_value());
}

// 0x546d54..0x546d6e: after the life (and the common movement, with the velocity at zero), the scale
// sin(life · π/18) · 3 and the bone's position again. It goes off at step 18 with the scale at 0. Each step takes the
// wind's two rand() and nothing else; without the bone, MissingOwner.
TEST(SkillShineParticlesTest, TheShineSwellsWithItsLifeAndFollowsTheBone)
{
    Particles particles;
    CrtRandom random(Seed);
    ASSERT_TRUE(
        particles.CreateFollowingChecked(SkillShineParticle, {}, {}, glm::vec3(1.0f), 0, 0.0f, HeroOwner, BonePoint)
            .HasValue());
    const Particle& shine = particles.All()[0];
    Client::Particles::PortableParticleTrigonometry trigonometry;
    const float clock = 0.0f;
    const std::int32_t map = 2;
    const double frequency = static_cast<double>(std::bit_cast<float>(0x3e32b8c2u));
    for (int step = 1; step <= 18; ++step)
    {
        SCOPED_TRACE(step);
        CrtRandom wind(random.State());
        static_cast<void>(wind.Next());
        static_cast<void>(wind.Next());
        const auto stepped = Client::Particles::StepParticlesChecked(
            particles, random, std::cref(clock), &map, trigonometry, {}, {}, {}, {}, nullptr, {}, BonePoint);
        ASSERT_TRUE(stepped.HasValue());
        const int life = 18 - step;
        EXPECT_EQ(shine.life, life);
        EXPECT_EQ(shine.alive, step < 18);
        EXPECT_EQ(shine.scale, static_cast<float>(RoundedSin(static_cast<double>(life) * frequency) * 3.0));
        EXPECT_EQ(shine.position, (glm::vec3{100.0f + static_cast<float>(life), 200.0f, 50.0f}));
        EXPECT_EQ(random.State(), wind.State());
    }
    EXPECT_EQ(shine.scale, 0.0f);

    Particles lonely;
    ASSERT_TRUE(
        lonely.CreateFollowingChecked(SkillShineParticle, {}, {}, glm::vec3(1.0f), 0, 0.0f, HeroOwner, BonePoint)
            .HasValue());
    const auto lost = Client::Particles::StepParticlesChecked(lonely, random, std::cref(clock), &map, trigonometry);
    ASSERT_FALSE(lost.HasValue());
    EXPECT_EQ(lost.Error(), Client::Particles::ParticleError::MissingOwner);
}

// 0x548550: 5fe goes through 0x548cf0's table (byte 8) to 0x54889a's common billboard, with its material's blend and
// the texture's size times the scale.
TEST(SkillShineParticlesTest, TheShineDrawsWithTheCommonBillboard)
{
    Particle shine;
    shine.alive = true;
    shine.type = shine.texture = SkillShineParticle;
    shine.scale = 1.5f;
    shine.rotation = 0.0f;
    shine.light = glm::vec3(1.0f);
    shine.position = {10.0f, 20.0f, 30.0f};
    const auto drawn = Client::Tests::DrawOneParticle(shine, glm::vec2(32.0f), std::uint8_t{4});
    ASSERT_TRUE(drawn.HasValue());
    ASSERT_TRUE(drawn.Value().has_value());
    EXPECT_EQ(drawn.Value()->texture, SkillShineParticle);
    EXPECT_EQ(drawn.Value()->blend, Client::Tests::RecordedParticleDraw::Blend::Alpha);
    EXPECT_EQ(drawn.Value()->width, 48.0f);
    EXPECT_EQ(drawn.Value()->height, 48.0f);
    EXPECT_EQ(drawn.Value()->position, shine.position);
    EXPECT_EQ(drawn.Value()->light, glm::vec3(1.0f));
    EXPECT_EQ(drawn.Value()->uv, glm::vec2(0.0f));
    EXPECT_EQ(drawn.Value()->uvSize, glm::vec2(1.0f));
}
} // namespace
