#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainLight.h"
#include "Data/Terrain/TerrainMapping.h"
#include "Math/X87.h"
#include "Math/OriginalRotation.h"
#include "MapScripts/Crywolf/CrywolfWater.h"
#include "Effects/GameModelEffects.h"
#include "Particles/IParticleTrigonometry.h"
#include "Particles/KalimaParticleKernels.h"
#include "Effects/ModelEffects.h"
#include "Particles/ParticleDrawParameters.h"
#include "Particles/ParticleOwner.h"
#include "Particles/Particles.h"
#include "Particles/PortableParticleTrigonometry.h"
#include "Terrain/TerrainCells.h"
#include "Terrain/TerrainLighting.h"
#include "WorldObjects/WorldObjects.h"

#include "support/ParticleDrawRecorder.h"
#include "support/PlayerActors.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// What Crywolf (map 34) adds to the world: the step of its objects (0x4303e0), its particles (0x5f2/21, 0x5f2/33,
// 0x622/9, 0x698/0, 0x6ec/5 and 0x60f/10), the banner's decal 0x61d/3 and the bubbles of its water (0x5fa695):
// 06 § Los objetos de Crywolf.
namespace
{
using Client::Crt::CrtRandom;
using Client::Particles::Particle;
using Client::Particles::Particles;
using Client::Terrain::TerrainLighting;
using Client::WorldObjects::ObjectStepGlobals;
using Client::WorldObjects::StepWorldObjectChecked;
using Client::WorldObjects::WorldObject;

constexpr std::uint8_t CrywolfMap = 34;
constexpr std::int16_t Hidden = -2;
const std::int16_t Shown = WorldObject{}.state.hiddenMesh;

using Client::Math::D;
using Client::Math::F32;

WorldObject Object(std::int16_t type)
{
    WorldObject object;
    object.type = type;
    object.transform.position = {1000.0f, 1500.0f, 20.0f};
    object.transform.angles = {0.0f, 0.0f, 45.0f};
    object.transform.scale = 1.5f;
    return object;
}

struct Step
{
    std::unique_ptr<TerrainLighting> lighting = std::make_unique<TerrainLighting>();
    ObjectStepGlobals globals;

    Step()
    {
        globals.dynamicLighting = lighting.get();
        globals.worldClock = 4321.5f;
    }
};

// The first seed whose first rand() % `modulus` gives `remainder`, or does not with `match` false.
std::uint32_t SeedWhere(int modulus, int remainder, bool match = true)
{
    for (std::uint32_t seed = 1;; ++seed)
    {
        CrtRandom probe(seed);
        if ((probe.Next() % modulus == remainder) == match)
            return seed;
    }
}

// The portable trigonometry, counting the calls: the 0x60f/10 asks for it at birth and not in the step.
class CountingTrigonometry final : public Client::Particles::IParticleTrigonometry
{
public:
    mutable int calls = 0;
    Client::Core::Expected<double, Client::Particles::ParticleError> SineProductPC53(double argument,
                                                                                     float multiplier) const override
    {
        ++calls;
        return m_inner.SineProductPC53(argument, multiplier);
    }
    Client::Core::Expected<double, Client::Particles::ParticleError> CosineProductPC53(double argument,
                                                                                       float multiplier) const override
    {
        ++calls;
        return m_inner.CosineProductPC53(argument, multiplier);
    }

private:
    Client::Particles::PortableParticleTrigonometry m_inner;
};

// The fields the step leaves; the comparison between twins ignores the type and the subtype.
void ExpectSameStep(const Particle& actual, const Particle& twin)
{
    EXPECT_EQ(actual.alive, twin.alive);
    EXPECT_EQ(actual.life, twin.life);
    EXPECT_EQ(actual.scale, twin.scale);
    EXPECT_EQ(actual.position, twin.position);
    EXPECT_EQ(actual.angles, twin.angles);
    EXPECT_EQ(actual.light, twin.light);
    EXPECT_EQ(actual.rotation, twin.rotation);
    EXPECT_EQ(actual.verticalSpeed, twin.verticalSpeed);
    EXPECT_EQ(actual.velocity, twin.velocity);
    EXPECT_EQ(actual.field38, twin.field38);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(actual.field48), std::bit_cast<std::uint32_t>(twin.field48));
    EXPECT_EQ(actual.rotationFactor, twin.rotationFactor);
    EXPECT_EQ(actual.start, twin.start);
}

// Gives a particle and its twin of another type or subtype `steps` steps, each in its pool and with the same seed,
// and compares what is left.
void ExpectTheSameStepAs(const Particle& particle, std::int32_t twinType, std::int32_t twinSubtype, int steps)
{
    auto own = std::make_unique<Particles>();
    auto twins = std::make_unique<Particles>();
    own->All()[0] = particle;
    Particle twin = particle;
    twin.type = twinType;
    twin.texture = twinType;
    twin.subtype = twinSubtype;
    twins->All()[0] = twin;
    CrtRandom random(31);
    CrtRandom twinRandom(31);
    const float clock = 2500.0f;
    Client::Particles::PortableParticleTrigonometry trigonometry;
    for (int step = 0; step < steps; ++step)
    {
        SCOPED_TRACE(step);
        ASSERT_TRUE(
            Client::Particles::StepParticlesChecked(*own, random, std::cref(clock), nullptr, trigonometry).HasValue());
        ASSERT_TRUE(Client::Particles::StepParticlesChecked(*twins, twinRandom, std::cref(clock), nullptr, trigonometry)
                        .HasValue());
        ExpectSameStep(own->All()[0], twins->All()[0]);
    }
    EXPECT_EQ(random.State(), twinRandom.State());
}
} // namespace

// 0x430416..0x43043e: the 0x29 adds (0.2f; 0.7f; 0.5f) to the dynamic light with radius 2, no rand(); without the
// buffer, it cuts at the call.
TEST(CrywolfObjectsTest, TheGlowLightsTheDynamicBufferWithoutADraw)
{
    Step step;
    auto object = Object(0x29);
    CrtRandom random(7);
    ASSERT_TRUE(StepWorldObjectChecked(object, CrywolfMap, nullptr, random, step.globals).HasValue());
    auto expected = std::make_unique<TerrainLighting>();
    Client::Terrain::AddTerrainLight(*expected, 1000.0f, 1500.0f,
                                     Client::Data::Terrain::TerrainLightColor{std::bit_cast<float>(0x3e4ccccdu),
                                                                              std::bit_cast<float>(0x3f333333u),
                                                                              std::bit_cast<float>(0x3f000000u)},
                                     2);
    EXPECT_EQ(step.lighting->cells, expected->cells);
    EXPECT_EQ(random.State(), CrtRandom(7).State());
    EXPECT_EQ(object.state.hiddenMesh, Shown);

    Step missing;
    missing.globals.dynamicLighting = nullptr;
    auto other = Object(0x29);
    const auto cut = StepWorldObjectChecked(other, CrywolfMap, nullptr, random, missing.globals);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x43043e"), std::string::npos) << cut.Error();
}

// 0x43044d..0x430497: the 0x39 and the 0x47, ((rand() & 3) + 3) · 0.1f on the x87 (fst of the red), times 0.6f
// and 0.2f, with radius 3. Without the buffer, the rand() is already drawn when it cuts.
TEST(CrywolfObjectsTest, TheTorchesLightTheDynamicBufferWithOneDraw)
{
    for (const std::int16_t type : {std::int16_t{0x39}, std::int16_t{0x47}})
    {
        SCOPED_TRACE(type);
        Step step;
        auto object = Object(type);
        CrtRandom random(7);
        CrtRandom draws(7);
        ASSERT_TRUE(StepWorldObjectChecked(object, CrywolfMap, nullptr, random, step.globals).HasValue());
        const double level = static_cast<double>((draws.Next() & 3) + 3) * D(std::bit_cast<float>(0x3dcccccdu));
        auto expected = std::make_unique<TerrainLighting>();
        Client::Terrain::AddTerrainLight(
            *expected, 1000.0f, 1500.0f,
            Client::Data::Terrain::TerrainLightColor{F32(level), F32(level * D(std::bit_cast<float>(0x3f19999au))),
                                                     F32(level * D(std::bit_cast<float>(0x3e4ccccdu)))},
            3);
        EXPECT_EQ(step.lighting->cells, expected->cells);
        EXPECT_EQ(random.State(), draws.State());
        EXPECT_EQ(object.state.hiddenMesh, Shown);

        Step missing;
        missing.globals.dynamicLighting = nullptr;
        auto other = Object(type);
        CrtRandom cutRandom(7);
        const auto cut = StepWorldObjectChecked(other, CrywolfMap, nullptr, cutRandom, missing.globals);
        ASSERT_FALSE(cut.HasValue());
        EXPECT_NE(cut.Error().find("0x430497"), std::string::npos) << cut.Error();
        EXPECT_EQ(cutRandom.State(), Client::Tests::AfterDraws(7, 1).State());
    }
}

// 0x4304a6 and 0x4304b7: the 0x51 is left with alpha 0.2f (+0x174) and the 0x52, the 0x53 and the 0x54 with +0x58
// at −2; the other types of the table (0x29..0x54) and those outside it touch nothing.
TEST(CrywolfObjectsTest, TheFaintAndHiddenObjectsOnlyChangeTheirOwnField)
{
    for (const std::int16_t type :
         {std::int16_t{0x51}, std::int16_t{0x52}, std::int16_t{0x53}, std::int16_t{0x54}, std::int16_t{0x24},
          std::int16_t{0x38}, std::int16_t{0x48}, std::int16_t{0x4d}, std::int16_t{0x55}, std::int16_t{0xfa}})
    {
        SCOPED_TRACE(type);
        Step step;
        auto object = Object(type);
        const float alpha = object.alpha;
        CrtRandom random(7);
        ASSERT_TRUE(StepWorldObjectChecked(object, CrywolfMap, nullptr, random, step.globals).HasValue());
        EXPECT_EQ(object.alpha, type == 0x51 ? std::bit_cast<float>(0x3e4ccccdu) : alpha);
        EXPECT_EQ(object.state.hiddenMesh, type >= 0x52 && type <= 0x54 ? Hidden : Shown);
        EXPECT_EQ(random.State(), CrtRandom(7).State());
        EXPECT_EQ(step.lighting->cells, std::make_unique<TerrainLighting>()->cells);
    }
}

// 0x542370..0x5423d8: the 0x5f2/21 is born with life 80, the scale ((rand() & 63) + 64) · scale · 0.005f, the
// rotation rand() % 360 and the rise ((rand() & 31) + 60) · 0.1f; 0x5466c3 sends it to the step of the 0x5f2/2.
TEST(CrywolfParticlesTest, TheChimneySmokeIsBornWithThreeDrawsAndStepsAsTheWorldSmoke)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(11);
    CrtRandom draws(11);
    const auto created =
        particles->CreateChecked(Client::Particles::BreathParticle, {100.0f, 200.0f, 300.0f}, {0.0f, 0.0f, 30.0f},
                                 glm::vec3(1.0f), Client::Particles::CrywolfSmokeSubtype, 1.25f, 0.0f, random);
    ASSERT_TRUE(created.HasValue());
    ASSERT_TRUE(created.Value().has_value());
    const Particle& smoke = particles->All()[*created.Value()];
    const int size = (draws.Next() & 63) + 64;
    const int turn = draws.Next() % 360;
    const int rise = (draws.Next() & 31) + 60;
    EXPECT_EQ(smoke.life, 80);
    EXPECT_EQ(smoke.scale, F32(static_cast<double>(size) * D(1.25f) * D(std::bit_cast<float>(0x3ba3d70au))));
    EXPECT_EQ(smoke.rotation, static_cast<float>(turn));
    EXPECT_EQ(smoke.verticalSpeed, F32(static_cast<double>(rise) * D(std::bit_cast<float>(0x3dcccccdu))));
    EXPECT_EQ(random.State(), draws.State());
    ExpectTheSameStepAs(smoke, Client::Particles::BreathParticle, 2, 4);
}

// 0x54330a..0x5433f4: the 0x622/9 draws seven rand() in the order of the 0x622/8, but its scale multiplies the
// argument's and +0x60 adds nothing to it; 0x54768f gives it the step of the 0x622/8.
TEST(CrywolfParticlesTest, TheMistCloudScalesItsArgumentAndStepsAsTheCloudEight)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(13);
    CrtRandom draws(13);
    const glm::vec3 position(12550.0f, 4250.0f, 170.0f);
    const auto created = particles->CreateChecked(Client::Particles::CloudParticle, position, {0.0f, 0.0f, 90.0f},
                                                  glm::vec3(0.02f, 0.03f, 0.02f),
                                                  Client::Particles::CrywolfCloudSubtype, 1.25f, 0.0f, random);
    ASSERT_TRUE(created.HasValue());
    ASSERT_TRUE(created.Value().has_value());
    const Particle& cloud = particles->All()[*created.Value()];
    constexpr double Hundredth = static_cast<double>(std::bit_cast<float>(0x3c23d70au));
    const int r1 = draws.Next() % 1000;
    const int r2 = (draws.Next() % 500) - 250;
    const int r3 = (draws.Next() % 500) - 250;
    const int r4 = (draws.Next() % 20) - 20;
    const int r5 = draws.Next() % 100;
    const int r6 = (draws.Next() % 20) + 180;
    const int r7 = draws.Next() % 10;
    EXPECT_EQ(cloud.life, 300);
    EXPECT_EQ(cloud.field38, 0x3dcccccdu);
    EXPECT_EQ(cloud.verticalSpeed, static_cast<float>(r1));
    const glm::vec3 moved(F32(static_cast<double>(r2) + D(position.x)), F32(static_cast<double>(r3) + D(position.y)),
                          F32(static_cast<double>(r4) + D(position.z)));
    EXPECT_EQ(cloud.position, moved);
    EXPECT_EQ(cloud.start.z, moved.z);
    EXPECT_EQ(cloud.start.y, F32(static_cast<double>(r5) * Hundredth));
    EXPECT_EQ(cloud.scale, F32(static_cast<double>(r6) * Hundredth * D(1.25f)));
    EXPECT_EQ(cloud.rotationFactor, F32(static_cast<double>(r7) * Hundredth));
    EXPECT_EQ(cloud.velocity, glm::vec3(0.0f));
    EXPECT_EQ(random.State(), draws.State());
    ExpectTheSameStepAs(cloud, Client::Particles::CloudParticle, 8, 4);
}

// 0x543152..0x543178 and 0x54719f..0x5471b2: the bubble 0x698/0 is born with life 32 and the scale
// (rand() % 170 + 1) · 0.01f, which overrides the argument's; each step leaves at +0x48 the frame (32 − life) / 4,
// and the draw (0x54899a) takes the row frame % 8 of coll.jpg's column, with the height times 0.125 and the
// rotation of +0x20.
TEST(CrywolfParticlesTest, TheBubbleLivesThirtyTwoStepsAcrossEightFrames)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(17);
    CrtRandom draws(17);
    const auto created =
        particles->CreateChecked(Client::Particles::BubbleParticle, {1050.0f, 2075.0f, 310.0f}, {12.0f, 0.0f, 0.0f},
                                 glm::vec3(0.3f, 0.4f, 0.2f), 0, 1.0f, 0.0f, random);
    ASSERT_TRUE(created.HasValue());
    ASSERT_TRUE(created.Value().has_value());
    const Particle& bubble = particles->All()[*created.Value()];
    EXPECT_EQ(bubble.life, 32);
    EXPECT_EQ(bubble.scale, F32(static_cast<double>(draws.Next() % 170 + 1) * D(0.01f)));
    EXPECT_EQ(random.State(), draws.State());
    for (int step = 1; step <= 32; ++step)
    {
        SCOPED_TRACE(step);
        ASSERT_TRUE(Client::Particles::StepParticlesChecked(*particles, random).HasValue());
        EXPECT_EQ(bubble.life, 32 - step);
        EXPECT_EQ(std::bit_cast<std::int32_t>(bubble.field48), step / 4);
        EXPECT_EQ(bubble.alive, step < 32);
    }
    EXPECT_FALSE(
        particles->CreateChecked(Client::Particles::BubbleParticle, {}, {}, {}, 1, 1.0f, 0.0f, random).HasValue());

    Particle drawn = bubble;
    drawn.angles.x = 12.0f;
    for (const std::int32_t frame : {3, 9})
    {
        drawn.field48 = std::bit_cast<float>(frame);
        const auto shape = Client::Particles::ParticleDrawParametersOf(drawn, {64.0f, 32.0f},
                                                                       Client::Particles::ParticleDrawKind::Bubble);
        EXPECT_EQ(shape.width, 64.0f);
        EXPECT_EQ(shape.height, 4.0f);
        EXPECT_EQ(shape.rotation, 12.0f);
        EXPECT_EQ(shape.uv, glm::vec2(0.0f, static_cast<float>(frame % 8) * 0.125f));
        EXPECT_EQ(shape.uvSize, glm::vec2(1.0f, 0.125f));
    }
}

// 0x541a8d..0x541b03: the 0x48's 0x5f2/33 is born with the argument's scale plus 0.1f, life rand() % 150, the tilt
// rand() % 90 − 45 at +0x20, the rotation rand() % 360 at +0x28 and the speed (0, −(rand() & 7), 0).
// 0x545ffc..0x54604c: after the common movement, the light (V · 0.4f; V · 0.4f; V) with V = life · 0.125f on the
// x87, the speed times 0.001f, the scale plus 0.02f (fst) and the z plus that unrounded sum. 0x548550 draws it
// without subtracting (it is not /2, /5, /12, /14, /15, /20, /21, /29, /37 or /38) with the common billboard and
// its rotation (0x548a64).
TEST(CrywolfParticlesTest, TheRisingSmokeDriftsOnceAndClimbsWithItsGrowingScale)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(37);
    CrtRandom draws(37);
    const glm::vec3 position(12550.0f, 4250.0f, 220.0f);
    const glm::vec3 light(0.5f, 0.3f, 0.1f);
    const auto created =
        particles->CreateChecked(Client::Particles::BreathParticle, position, {0.0f, 0.0f, 90.0f}, light,
                                 Client::Particles::CrywolfRisingSmokeSubtype, 1.25f, 0.0f, random);
    ASSERT_TRUE(created.HasValue());
    ASSERT_TRUE(created.Value().has_value());
    const Particle& smoke = particles->All()[*created.Value()];
    const int life = draws.Next() % 150;
    const int tilt = draws.Next() % 90 - 45;
    const int turn = draws.Next() % 360;
    const int drift = draws.Next() & 7;
    EXPECT_EQ(smoke.life, life);
    EXPECT_EQ(smoke.scale, F32(1.25 + D(std::bit_cast<float>(0x3dcccccdu))));
    EXPECT_EQ(smoke.angles, glm::vec3(static_cast<float>(tilt), 0.0f, static_cast<float>(turn)));
    EXPECT_EQ(smoke.velocity, glm::vec3(0.0f, -static_cast<float>(drift), 0.0f));
    EXPECT_EQ(smoke.light, light);
    EXPECT_EQ(smoke.position, position);
    EXPECT_EQ(smoke.rotation, 0.0f);
    EXPECT_TRUE(smoke.moves);
    EXPECT_EQ(random.State(), draws.State());

    Particle expected = smoke;
    expected.life = 12;
    particles->All()[*created.Value()].life = 12;
    const std::uint32_t born = random.State();
    for (int step = 1; step <= 12; ++step)
    {
        SCOPED_TRACE(step);
        ASSERT_TRUE(Client::Particles::StepParticlesChecked(*particles, random).HasValue());
        expected.life -= 1;
        const glm::vec3 turned =
            Client::Math::OriginalVectorRotate(expected.velocity, Client::Math::OriginalAngleMatrix(expected.angles));
        for (int axis = 0; axis < 3; ++axis)
            expected.position[axis] = F32(D(turned[axis]) + D(expected.position[axis]));
        const double glow = static_cast<double>(expected.life) * 0.125;
        const float dim = F32(glow * D(std::bit_cast<float>(0x3ecccccdu)));
        expected.light = glm::vec3(dim, dim, F32(glow));
        for (int axis = 0; axis < 3; ++axis)
            expected.velocity[axis] = F32(D(expected.velocity[axis]) * D(std::bit_cast<float>(0x3a83126fu)));
        const double grown = D(expected.scale) + D(std::bit_cast<float>(0x3ca3d70au));
        expected.scale = F32(grown);
        expected.position.z = F32(grown + D(expected.position.z));
        EXPECT_EQ(smoke.alive, expected.life > 0);
        EXPECT_EQ(smoke.life, expected.life);
        EXPECT_EQ(smoke.position, expected.position);
        EXPECT_EQ(smoke.light, expected.light);
        EXPECT_EQ(smoke.velocity, expected.velocity);
        EXPECT_EQ(smoke.scale, expected.scale);
        EXPECT_EQ(smoke.angles, expected.angles);
    }
    CrtRandom wind(born);
    for (int draw = 0; draw < 2 * 12; ++draw)
        static_cast<void>(wind.Next());
    EXPECT_EQ(random.State(), wind.State());

    Particle drawn = smoke;
    drawn.alive = true;
    drawn.rotation = 33.0f;
    const auto billboard = Client::Tests::DrawOneParticle(drawn, glm::vec2(64.0f, 32.0f), std::uint8_t{3});
    ASSERT_TRUE(billboard.HasValue());
    ASSERT_TRUE(billboard.Value().has_value());
    EXPECT_EQ(billboard.Value()->texture, Client::Particles::BreathParticle);
    EXPECT_EQ(billboard.Value()->blend, Client::Tests::RecordedParticleDraw::Blend::Additive);
    EXPECT_EQ(billboard.Value()->width, F32(64.0 * D(drawn.scale)));
    EXPECT_EQ(billboard.Value()->height, F32(32.0 * D(drawn.scale)));
    EXPECT_EQ(billboard.Value()->rotation, 33.0f);
    EXPECT_EQ(billboard.Value()->light, drawn.light);
}

// 0x543b82..0x543bd5: the 0x6ec of the 0x39 and the 0x47 is born like the 0x6ed (life 24 and the speed's two
// rand()) and, with the /5, is left with life 20; the step is the 0x6ed's (0x547a40).
TEST(CrywolfParticlesTest, TheTorchSparkIsAShortFanta)
{
    for (const std::int32_t subtype : {0, Client::Particles::ShortFantaSubtype})
    {
        SCOPED_TRACE(subtype);
        auto particles = std::make_unique<Particles>();
        CrtRandom random(19);
        CrtRandom draws(19);
        const glm::vec3 position(12550.0f, 4250.0f, 170.0f);
        const auto created = particles->CreateChecked(Client::Particles::CrywolfFanta, position, {0.0f, 0.0f, 90.0f},
                                                      glm::vec3(1.0f), subtype, 1.25f, 0.0f, random);
        ASSERT_TRUE(created.HasValue());
        ASSERT_TRUE(created.Value().has_value());
        const Particle& spark = particles->All()[*created.Value()];
        EXPECT_EQ(spark.life, subtype == Client::Particles::ShortFantaSubtype ? 20 : 24);
        EXPECT_EQ(spark.velocity.x, F32(static_cast<double>(draws.Next() % 10 - 5) * D(0.4f)));
        EXPECT_EQ(spark.velocity.y, 0.0f);
        EXPECT_EQ(spark.velocity.z, F32(static_cast<double>(draws.Next() % 10 + 5) * D(0.2f)));
        EXPECT_EQ(spark.start, position);
        EXPECT_EQ(random.State(), draws.State());
        ExpectTheSameStepAs(spark, Client::Particles::KalimaFanta, 0, 6);
    }
}

// 0x542ed0..0x54304a: the 0x60f/10 is born like the /3 (life 60, four rand()) and, alone among the orbs, shifts its
// start with the sine and cosine of (life + Vx) · 0.1f times 40. 0x547286..0x5472a5: in the step it computes the
// argument and throws it away; goes back to the start in x and y, rises, shrinks 0.002f and, with life at 20 or
// less, fades out.
TEST(CrywolfParticlesTest, TheAnchoredOrbStaysOverItsShiftedStart)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(23);
    CrtRandom draws(23);
    CountingTrigonometry trigonometry;
    const float clock = 2500.0f;
    const glm::vec3 position(12550.0f, 4250.0f, 170.0f);
    const glm::vec3 light(0.25f, 0.5f, 0.75f);
    const float scale = std::bit_cast<float>(0x3e428f5cu);
    const auto created = particles->CreateChecked(Client::Particles::OrbitParticle, position, {0.0f, 0.0f, 90.0f},
                                                  light, Client::Particles::AnchoredOrbitSubtype, scale,
                                                  std::cref(clock), random, 0, trigonometry);
    ASSERT_TRUE(created.HasValue());
    ASSERT_TRUE(created.Value().has_value());
    const Particle& orb = particles->All()[*created.Value()];
    constexpr double Hundredth = static_cast<double>(std::bit_cast<float>(0x3c23d70au));
    const int rise = draws.Next() % 100;
    const int phase = (draws.Next() % 300) - 150;
    const int grow = draws.Next() % 6;
    const int turn = draws.Next() % 360;
    EXPECT_EQ(orb.life, 60);
    EXPECT_EQ(orb.verticalSpeed, F32(static_cast<double>(rise) * Hundredth * 4.0 + 1.0));
    EXPECT_EQ(orb.velocity.x, static_cast<float>(phase));
    EXPECT_EQ(orb.scale, F32(static_cast<double>(grow) * Hundredth + D(scale)));
    EXPECT_EQ(orb.rotation, static_cast<float>(turn));
    EXPECT_EQ(orb.angles, glm::vec3(0.0f));
    EXPECT_EQ(random.State(), draws.State());
    const double argument = (60.0 + static_cast<double>(phase)) * D(std::bit_cast<float>(0x3dcccccdu));
    const Client::Particles::PortableParticleTrigonometry portable;
    const glm::vec3 start(F32(portable.SineProductPC53(argument, 40.0f).Value() + D(position.x)),
                          F32(D(position.y) - portable.CosineProductPC53(argument, 40.0f).Value()), position.z);
    EXPECT_EQ(orb.start, start);
    EXPECT_EQ(orb.position, position);
    EXPECT_EQ(trigonometry.calls, 2);

    trigonometry.calls = 0;
    Particle expected = orb;
    for (int step = 1; step <= 45; ++step)
    {
        SCOPED_TRACE(step);
        ASSERT_TRUE(Client::Particles::StepParticlesChecked(*particles, random, std::cref(clock), nullptr, trigonometry)
                        .HasValue());
        expected.life -= 1;
        expected.position = {start.x, start.y, F32(D(expected.verticalSpeed) + D(expected.position.z))};
        expected.scale = F32(D(expected.scale) - D(std::bit_cast<float>(0x3b03126fu)));
        if (expected.life <= 20)
        {
            for (int axis = 0; axis < 3; ++axis)
                expected.light[axis] = F32(D(expected.light[axis]) * D(std::bit_cast<float>(0x3f68ba2eu)));
        }
        EXPECT_EQ(orb.life, expected.life);
        EXPECT_EQ(orb.position, expected.position);
        EXPECT_EQ(orb.scale, expected.scale);
        EXPECT_EQ(orb.light, expected.light);
    }
    EXPECT_EQ(trigonometry.calls, 0);
}

// 0x51ee96..0x51eeae: the decal 0x61d is born with life 20 (15 with subtype 0) and scale 0.5, no rand().
// 0x52b91f..0x52b954: the /3, with life between 6 and 9 before the common step, releases a 0x60f/10 with its light
// and scale 0.19; the /0, the /2 and the /7 release the 0x60f/0 with the owner effect (0x52b8ff, 0x52b977), and the
// others only have the common step.
TEST(CrywolfDecalTest, TheBannerDecalReleasesFourOrbsBeforeItFades)
{
    auto effects = std::make_unique<Client::Effects::ModelEffects>();
    const glm::vec3 position(12550.0f, 4250.0f, 170.0f);
    const glm::vec3 angles(0.0f, 0.0f, 90.0f);
    const glm::vec3 light(0.37f);
    const auto slot = effects->CreateFadingDecal(position, angles, light, Client::Effects::CrywolfDecalSubtype, 4);
    ASSERT_TRUE(slot.has_value());
    Client::Effects::ModelEffect& decal = effects->Effects()[*slot];
    EXPECT_TRUE(decal.alive);
    EXPECT_EQ(decal.type, Client::Effects::FadingDecalEffect);
    EXPECT_EQ(decal.subtype, Client::Effects::CrywolfDecalSubtype);
    EXPECT_EQ(decal.life, 20);
    EXPECT_EQ(decal.transform.scale, 0.5f);
    EXPECT_EQ(decal.transform.position, position);
    EXPECT_EQ(decal.transform.angles, angles);
    EXPECT_EQ(decal.light, light);
    EXPECT_EQ(decal.disturbanceState, 4);
    const auto first = effects->CreateFadingDecal(position, angles, light, 0, 4);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(effects->Effects()[*first].life, 15);

    auto particles = std::make_unique<Particles>();
    CrtRandom random(29);
    CrtRandom draws(29);
    std::vector<int> released;
    for (int guard = 0; decal.alive && guard < 30; ++guard)
    {
        const int life = decal.life;
        SCOPED_TRACE(life);
        ASSERT_TRUE(Client::Effects::StepGameModelEffect(decal, nullptr, *particles, 2500.0f, random).HasValue());
        static_cast<void>(draws.Next());
        if (life > 5 && life < 10)
        {
            released.push_back(life);
            for (int draw = 0; draw < 4; ++draw)
                static_cast<void>(draws.Next());
        }
        EXPECT_EQ(random.State(), draws.State());
    }
    EXPECT_FALSE(decal.alive);
    EXPECT_EQ(released, (std::vector<int>{9, 8, 7, 6}));
    int orbs = 0;
    for (const Particle& particle : particles->All())
    {
        if (!particle.alive)
            continue;
        ++orbs;
        EXPECT_EQ(particle.type, Client::Particles::OrbitParticle);
        EXPECT_EQ(particle.subtype, Client::Particles::AnchoredOrbitSubtype);
        EXPECT_EQ(particle.light, light);
        EXPECT_EQ(particle.start.z, position.z);
    }
    EXPECT_EQ(orbs, 4);

    // 0x52b8ff and 0x52b977: the /0, the /2 and the /7 release the orb 0x60f/0 with its position, angles, light, scale
    // 0.19 and the owner effect (its slot in the table), after the rand() of the step's light; full, nothing. Without
    // the effect table (where the owner's slot comes from) they cut with their address if the orb has room.
    for (const std::int32_t subtype : {0, 2, 7})
    {
        SCOPED_TRACE(subtype);
        auto table = std::make_unique<Client::Effects::ModelEffects>();
        static_cast<void>(table->CreateFadingDecal(position, angles, light, 4, 4));
        const auto made = table->CreateFadingDecal(position, angles, light, subtype, 4);
        ASSERT_EQ(made, std::optional<std::size_t>(1));
        Client::Effects::ModelEffect& owner = table->Effects()[*made];
        owner.life = 8;
        const Client::Effects::ModelEffect before = owner;
        const Client::Effects::ModelEffectStepServices services{.effects = table.get()};
        auto spawned = std::make_unique<Particles>();
        CrtRandom orbRandom(29);
        ASSERT_TRUE(
            Client::Effects::StepGameModelEffect(owner, nullptr, *spawned, 2500.0f, orbRandom, nullptr, services)
                .HasValue());
        constexpr Client::Particles::ParticleOwner DecalOwner{Client::Particles::ParticleOwnerKind::ModelEffect, 1};
        // The light's rand() and the four of 0x542f83, 0x542fab, 0x542fc6 and 0x542fe5; the values come from orbit.py
        // (0x542ed0..0x54304a on a separate x87) and not from CreateChecked.
        EXPECT_EQ(orbRandom.State(), 0x3309f4f0u);
        EXPECT_EQ(orbRandom.State(), Client::Tests::AfterDraws(29, 5).State());
        const Particle& orb = spawned->All()[0];
        ASSERT_TRUE(orb.alive);
        EXPECT_EQ(orb.type, Client::Particles::OrbitParticle);
        EXPECT_EQ(orb.subtype, 0);
        EXPECT_EQ(orb.typedOwner, DecalOwner);
        EXPECT_EQ(orb.light, light);
        EXPECT_EQ(orb.position, position);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(orb.scale), 0x3e4ccccdu);         // 0.19f + (rand() % 6) · 0.01f
        EXPECT_EQ(orb.rotation, 105.0f);                                         // rand() % 360
        EXPECT_EQ(std::bit_cast<std::uint32_t>(orb.verticalSpeed), 0x3fb851ebu); // (rand() % 100) · 0.01f · 4 + 1
        EXPECT_EQ(orb.velocity, (glm::vec3{-139.0f, 0.0f, 0.0f}));               // rand() % 300 − 150
        EXPECT_EQ(orb.angles, glm::vec3(0.0f));
        EXPECT_EQ(orb.start, position);
        EXPECT_EQ(orb.life, 60);
        EXPECT_FALSE(spawned->All()[1].alive);

        auto full = std::make_unique<Particles>();
        for (Particle& particle : full->All())
            particle.alive = true;
        Client::Effects::ModelEffect again = before;
        CrtRandom fullRandom(29);
        auto fullTable = std::make_unique<Client::Effects::ModelEffects>();
        static_cast<void>(fullTable->CreateFadingDecal(position, angles, light, 4, 4));
        const auto fullMade = fullTable->CreateFadingDecal(position, angles, light, subtype, 4);
        ASSERT_TRUE(fullMade.has_value());
        fullTable->Effects()[*fullMade].life = 8;
        const Client::Effects::ModelEffectStepServices fullServices{.effects = fullTable.get()};
        ASSERT_TRUE(Client::Effects::StepGameModelEffect(fullTable->Effects()[*fullMade], nullptr, *full, 2500.0f,
                                                         fullRandom, nullptr, fullServices)
                        .HasValue());
        EXPECT_EQ(fullRandom.State(), Client::Tests::AfterDraws(29, 1).State());

        if (subtype == 0)
            continue;
        // With the particle table full the orb is not born and the owner is not needed: without the effect table the /2
        // and the /7 go on like the original, with only the light's rand().
        Client::Effects::ModelEffect unowned = before;
        CrtRandom unownedRandom(29);
        ASSERT_TRUE(Client::Effects::StepGameModelEffect(unowned, nullptr, *full, 2500.0f, unownedRandom).HasValue());
        EXPECT_EQ(unownedRandom.State(), 0x0085526cu); // 29 · 214013 + 2531011
        EXPECT_EQ(unownedRandom.State(), Client::Tests::AfterDraws(29, 1).State());
        CrtRandom loneRandom(29);
        const auto lone = Client::Effects::StepGameModelEffect(again, nullptr, *spawned, 2500.0f, loneRandom);
        ASSERT_FALSE(lone.HasValue());
        EXPECT_NE(lone.Error().find("0x52b977"), std::string::npos) << lone.Error();
    }
    for (const std::int32_t subtype : {1, 4})
    {
        Client::Effects::ModelEffect quiet = effects->Effects()[*first];
        quiet.subtype = subtype;
        quiet.life = 8;
        CrtRandom quietRandom(29);
        auto none = std::make_unique<Particles>();
        ASSERT_TRUE(Client::Effects::StepGameModelEffect(quiet, nullptr, *none, 2500.0f, quietRandom).HasValue());
        EXPECT_EQ(quietRandom.State(), Client::Tests::AfterDraws(29, 1).State());
        EXPECT_FALSE(none->All()[0].alive);
    }
}

// 0x52b8ad..0x52b8cf: the 0x61d/0 leaves on each step the /1 at its position, with its angles and light, life 20
// and scale 0.5, no rand(); full, nothing. Without the effect table it cuts. Outside life 6..9 it does not release
// the orb.
TEST(CrywolfDecalTest, TheTrailDecalLeavesAFadingDecalEachStep)
{
    auto effects = std::make_unique<Client::Effects::ModelEffects>();
    const glm::vec3 position(12550.0f, 4250.0f, 170.0f);
    const glm::vec3 angles(0.0f, 0.0f, 90.0f);
    const glm::vec3 light(0.37f);
    const auto slot = effects->CreateFadingDecal(position, angles, light, 0, 4);
    ASSERT_TRUE(slot.has_value());
    auto particles = std::make_unique<Particles>();
    CrtRandom random(29);
    const Client::Effects::ModelEffectStepServices services{.effects = effects.get()};
    Client::Effects::ModelEffect& trail = effects->Effects()[*slot];
    ASSERT_TRUE(Client::Effects::StepGameModelEffect(trail, nullptr, *particles, 2500.0f, random, nullptr, services)
                    .HasValue());
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(29, 1).State());
    const Client::Effects::ModelEffect& faded = effects->Effects()[1];
    ASSERT_TRUE(faded.alive);
    EXPECT_EQ(faded.type, Client::Effects::FadingDecalEffect);
    EXPECT_EQ(faded.subtype, 1);
    EXPECT_EQ(faded.life, 20);
    EXPECT_EQ(faded.transform.scale, 0.5f);
    EXPECT_EQ(faded.transform.position, position);
    EXPECT_EQ(faded.transform.angles, angles);
    EXPECT_EQ(faded.light, light);
    EXPECT_EQ(faded.disturbanceState, 0xffff);
    EXPECT_FALSE(particles->All()[0].alive);
    for (auto& effect : effects->Effects())
        effect.alive = true;
    ASSERT_TRUE(Client::Effects::StepGameModelEffect(trail, nullptr, *particles, 2500.0f, random, nullptr, services)
                    .HasValue());
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(29, 2).State());
    const auto cut = Client::Effects::StepGameModelEffect(trail, nullptr, *particles, 2500.0f, random);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x52b8cf"), std::string::npos) << cut.Error();
}

namespace
{
struct Water
{
    std::unique_ptr<Client::Data::Terrain::TerrainMapping> mapping =
        std::make_unique<Client::Data::Terrain::TerrainMapping>();
    std::unique_ptr<Particles> particles = std::make_unique<Particles>();
    Client::Terrain::TerrainView view;
    float clock = 2500.0f;
    CrtRandom random{1};
    Client::MapScripts::Crywolf::CrywolfWaterContext context{.heroZ = 245.5f,
                                                             .heroAngles = {0.0f, 0.0f, 135.0f},
                                                             .random = nullptr,
                                                             .particles = nullptr,
                                                             .worldClock = std::cref(clock)};

    explicit Water(std::uint32_t seed) : random(seed)
    {
        view.mapping = mapping.get();
        view.map = CrywolfMap;
        context.random = &random;
        context.particles = particles.get();
    }

    // The four corners of (x, y), in the order of Terrain::DrawTerrainCell.
    void Cell(std::uint8_t x, std::uint8_t y, std::uint8_t layer1, std::uint8_t layer2, float alpha)
    {
        const std::size_t first = Client::Data::Terrain::CellIndex(x, y);
        mapping->layer1[first] = layer1;
        mapping->layer2[first] = layer2;
        for (const std::size_t corner : {first, first + 1, first + 256 + 1, first + 256})
            mapping->alpha[corner] = alpha;
    }
};

// A request for the water UVs (0x5fa6ab..0x5fa731), on a copy of the randomness: the bubble, if it comes out.
std::optional<glm::vec3> Request(CrtRandom& draws, std::uint8_t x, std::uint8_t y, float heroZ, float& scale)
{
    if (draws.Next() % 50 != 0)
        return std::nullopt;
    const int across = draws.Next() % 100 + 1;
    const int down = draws.Next() % 100 + 1;
    const glm::vec3 position(F32(static_cast<double>(across) + D(static_cast<float>(x)) * 100.0),
                             F32(static_cast<double>(down) + D(static_cast<float>(y)) * 100.0), F32(D(heroZ) + 10.0));
    scale = F32(static_cast<double>(draws.Next() % 170 + 1) * D(0.01f));
    return position;
}
} // namespace

// 0x5fa8d0..0x5fa8f6 and 0x5fa9b2..0x5fa9dc: the bottom layer requests the water UVs with layer 1 at 5 and some
// alpha corner below 1; the alpha one, also with layer 2 at 5 and some corner above 0. Each request draws a rand()
// and, with 0 modulo 50, the bubble 0x698/0 in the cell, above the hero, with its angles and the light
// (0.3; 0.4; 0.2).
TEST(CrywolfWaterTest, EachWaterRequestDrawsOnceAndSometimesReleasesABubble)
{
    struct Case
    {
        std::uint8_t layer1;
        std::uint8_t layer2;
        float alpha;
        int requests;
    };
    for (const Case c : {Case{5, 0, 0.0f, 1}, Case{5, 5, 0.0f, 1}, Case{5, 5, 0.5f, 2}, Case{5, 4, 0.5f, 1},
                         Case{5, 5, 1.0f, 0}, Case{4, 5, 0.5f, 0}, Case{4, 4, 0.0f, 0}})
    {
        SCOPED_TRACE(testing::Message() << int{c.layer1} << "/" << int{c.layer2} << " " << c.alpha);
        for (const bool bubble : {false, true})
        {
            SCOPED_TRACE(bubble);
            const std::uint32_t seed = SeedWhere(50, 0, bubble);
            Water water(seed);
            water.Cell(40, 120, c.layer1, c.layer2, c.alpha);
            ASSERT_TRUE(
                Client::MapScripts::Crywolf::StepCrywolfWaterCell(water.view, 40, 120, water.context).HasValue());
            CrtRandom draws(seed);
            std::vector<std::pair<glm::vec3, float>> expected;
            for (int request = 0; request < c.requests; ++request)
            {
                float scale = 0.0f;
                if (const auto position = Request(draws, 40, 120, water.context.heroZ, scale))
                    expected.emplace_back(*position, scale);
            }
            EXPECT_EQ(water.random.State(), draws.State());
            std::size_t alive = 0;
            for (const Particle& particle : water.particles->All())
            {
                if (!particle.alive)
                    continue;
                ASSERT_LT(alive, expected.size());
                EXPECT_EQ(particle.type, Client::Particles::BubbleParticle);
                EXPECT_EQ(particle.subtype, 0);
                EXPECT_EQ(particle.position, expected[alive].first);
                EXPECT_EQ(particle.scale, expected[alive].second);
                EXPECT_EQ(particle.angles, water.context.heroAngles);
                EXPECT_EQ(particle.light,
                          glm::vec3(std::bit_cast<float>(0x3e99999au), std::bit_cast<float>(0x3ecccccdu),
                                    std::bit_cast<float>(0x3e4ccccdu)));
                EXPECT_EQ(particle.life, 32);
                ++alive;
            }
            EXPECT_EQ(alive, expected.size());
            if (bubble && c.requests > 0)
            {
                EXPECT_FALSE(expected.empty());
            }
        }
    }
}

// Without the mapping, the randomness or the pool, the request cuts where it reads them: the pool, after the three
// rand() of the position.
TEST(CrywolfWaterTest, AMissingBindingCutsWhereTheRequestReadsIt)
{
    const std::uint32_t seed = SeedWhere(50, 0);
    {
        Water water(seed);
        water.view.mapping = nullptr;
        const auto cut = Client::MapScripts::Crywolf::StepCrywolfWaterCell(water.view, 40, 120, water.context);
        ASSERT_FALSE(cut.HasValue());
        EXPECT_NE(cut.Error().find("0x5fa830"), std::string::npos) << cut.Error();
    }
    {
        Water water(seed);
        water.Cell(40, 120, 5, 0, 0.0f);
        water.context.random = nullptr;
        const auto cut = Client::MapScripts::Crywolf::StepCrywolfWaterCell(water.view, 40, 120, water.context);
        ASSERT_FALSE(cut.HasValue());
        EXPECT_NE(cut.Error().find("0x5fa6ab"), std::string::npos) << cut.Error();
    }
    {
        Water water(seed);
        water.Cell(40, 120, 5, 0, 0.0f);
        water.context.particles = nullptr;
        const auto cut = Client::MapScripts::Crywolf::StepCrywolfWaterCell(water.view, 40, 120, water.context);
        ASSERT_FALSE(cut.HasValue());
        EXPECT_NE(cut.Error().find("0x5fa731"), std::string::npos) << cut.Error();
        EXPECT_EQ(water.random.State(), Client::Tests::AfterDraws(seed, 3).State());
    }
    {
        // Without a bubble, the pool is not read.
        Water water(SeedWhere(50, 0, false));
        water.Cell(40, 120, 5, 0, 0.0f);
        water.context.particles = nullptr;
        EXPECT_TRUE(Client::MapScripts::Crywolf::StepCrywolfWaterCell(water.view, 40, 120, water.context).HasValue());
    }
}
