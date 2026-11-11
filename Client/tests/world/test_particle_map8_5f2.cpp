#include "World/Particles.h"
#include "World/ParticleDrawing.h"
#include "World/IParticleDrawingServices.h"
#include "Render/ParticleBillboardRequest.h"
#include "World/Ftol.h"
#include "World/IParticleTrigonometry.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <vector>
#include <utility>
#include <cstddef>
#include <string>
#include <system_error>

namespace
{
using Client::Render::Models::CrtRandom;
using Client::World::Particle;
using Client::World::Particles;
constexpr std::int32_t Smoke = 0x5f2;
std::uint32_t Bits(float value)
{
    return std::bit_cast<std::uint32_t>(value);
}

// En ramas sin trig, cualquier llamada al evaluador es un defecto observable.
struct RejectingTrigonometry final : Client::World::IParticleTrigonometry
{
    Client::Core::Expected<double, Client::World::ParticleError> SineProductPC53(double, float) const override
    {
        ADD_FAILURE() << "seno inesperado";
        return Client::Core::Unexpected(Client::World::ParticleError::UnrepresentableTrigonometry);
    }
    Client::Core::Expected<double, Client::World::ParticleError> CosineProductPC53(double, float) const override
    {
        ADD_FAILURE() << "coseno inesperado";
        return Client::Core::Unexpected(Client::World::ParticleError::UnrepresentableTrigonometry);
    }
};

TEST(ParticleMap8Test, CreateFourLoadsClockAfterItsAliasedAngleStore)
{
    RejectingTrigonometry trigonometry;
    Particles pool;
    Particle& p = pool.All()[0];
    CrtRandom random(7); // R1=61, R2=17422; el store de ángulo142 modifica el reloj prestado.
    const auto result = pool.CreateChecked(Smoke, {100, 200, 300}, {10, 20, 30}, {2, 3, 4}, 4, 3, std::cref(p.angles.x),
                                           random, 123, trigonometry);
    ASSERT_TRUE(result.HasValue());
    ASSERT_EQ(result.Value(), 0u);
    EXPECT_EQ(p.life, 16);
    EXPECT_EQ(p.rotation, 142);
    EXPECT_EQ(p.angles, glm::vec3(142, 20, 30));
    EXPECT_EQ(p.start, glm::vec3(100, 200, 300));
    EXPECT_EQ(p.owner, 123u);
    EXPECT_EQ(random.State(), 0xc40e66b9u);
}
TEST(ParticleMap8Test, CreateSixUsesArgumentScaleForJitterAndRetainsInitialBase)
{
    RejectingTrigonometry trigonometry;
    Particles pool;
    float clock = 99;
    CrtRandom random(7);
    const auto result = pool.CreateChecked(Smoke, {100, 200, 300}, {10, 20, 30}, {2, 3, 4}, 6, 3, std::cref(clock),
                                           random, 0, trigonometry);
    ASSERT_TRUE(result.HasValue());
    const Particle& p = pool.All()[0];
    EXPECT_EQ(p.life, 30);
    EXPECT_EQ(p.verticalSpeed, 61);
    EXPECT_FLOAT_EQ(p.scale, 1.82f);
    EXPECT_EQ(p.position, glm::vec3(-155, 20, 321));
    EXPECT_EQ(p.rotation, 321);
    EXPECT_EQ(p.start, glm::vec3(100, 200, 300));
    EXPECT_EQ(p.velocity, glm::vec3(0));
    EXPECT_EQ(random.State(), 0xbca151d2u);
}
TEST(ParticleMap8Test, CreateSevenAndEightHaveDifferentScaleAndRandomSequences)
{
    RejectingTrigonometry trigonometry;
    Particles pool;
    float clock = 99;
    CrtRandom random(7);
    ASSERT_TRUE(pool.CreateChecked(Smoke, {}, {}, {}, 7, 3, std::cref(clock), random, 0, trigonometry).HasValue());
    const Particle& seven = pool.All()[0];
    EXPECT_EQ(seven.life, 30);
    EXPECT_EQ(seven.velocity, glm::vec3(0, 21, 0));
    EXPECT_FLOAT_EQ(seven.verticalSpeed, 0.11f);
    EXPECT_FLOAT_EQ(seven.scale, 4.05f);
    EXPECT_EQ(seven.rotation, 200);
    random = CrtRandom(7);
    ASSERT_TRUE(pool.CreateChecked(Smoke, {}, {}, {}, 8, 3, std::cref(clock), random, 0, trigonometry).HasValue());
    const Particle& eight = pool.All()[1];
    EXPECT_EQ(eight.life, 24);
    EXPECT_FLOAT_EQ(eight.velocity.y, -11.1f);
    EXPECT_FLOAT_EQ(eight.scale, 2.4f);
    EXPECT_EQ(eight.rotation, 142);
    EXPECT_EQ(random.State(), 0xc40e66b9u);
}
TEST(ParticleMap8Test, FullPoolDoesNotConsumeRandomOrObserveAnyAliasedInputs)
{
    RejectingTrigonometry trigonometry;
    Particles pool;
    for (Particle& p : pool.All())
        p.alive = true;
    CrtRandom random(7);
    const Particle& p = pool.All()[0];
    const auto result =
        pool.CreateChecked(Smoke, p.position, p.angles, p.light, 4, 3, std::cref(p.angles.x), random, 0, trigonometry);
    ASSERT_TRUE(result.HasValue());
    EXPECT_FALSE(result.Value());
    EXPECT_EQ(random.State(), 7u);
}
TEST(ParticleMap8Test, LegacyAdmissionRemainsUnchanged)
{
    Particles pool;
    CrtRandom random(7);
    for (const int subtype : {4, 6, 7, 8})
    {
        const auto result = pool.CreateChecked(Smoke, {}, {}, {}, subtype, 3, 1.0f, random);
        ASSERT_FALSE(result.HasValue());
        EXPECT_EQ(result.Error(), Client::World::ParticleError::UnsupportedSubtype);
    }
    EXPECT_EQ(random.State(), 7u);
}
TEST(ParticleMap8Test, FourKeepsWideAccumulatorAndFinishesAfterExpiration)
{
    RejectingTrigonometry trigonometry;
    Particles pool;
    Particle& p = pool.All()[0];
    p = Particle{.alive = true,
                 .type = Smoke,
                 .subtype = 4,
                 .scale = 1,
                 .position = {0, 0, -16777216},
                 .life = 1,
                 .verticalSpeed = 16777216};
    float clock = 0;
    CrtRandom random(7);
    ASSERT_TRUE(Client::World::StepParticlesChecked(pool, random, std::cref(clock), nullptr, trigonometry).HasValue());
    EXPECT_FALSE(p.alive);
    EXPECT_EQ(p.life, 0);
    EXPECT_EQ(p.light, glm::vec3(0));
    EXPECT_EQ(p.verticalSpeed, 16777216);
    EXPECT_EQ(Bits(p.position.z), 0x3e4ccccdu);
    EXPECT_FLOAT_EQ(p.scale, 1.05f);
    EXPECT_EQ(random.State(), 0xc40e66b9u);
}
TEST(ParticleMap8Test, SevenUsesPostDecrementFiveBoundaryAndWideFall)
{
    RejectingTrigonometry trigonometry;
    Particles pool;
    Particle& p = pool.All()[0];
    p = Particle{.alive = true,
                 .type = Smoke,
                 .subtype = 7,
                 .scale = 1,
                 .position = {0, 0, 16777216},
                 .life = 6,
                 .verticalSpeed = 16777216};
    float clock = 0;
    CrtRandom random(7);
    ASSERT_TRUE(Client::World::StepParticlesChecked(pool, random, std::cref(clock), nullptr, trigonometry).HasValue());
    EXPECT_EQ(p.life, 5);
    EXPECT_EQ(p.position.z, -1);
    EXPECT_FLOAT_EQ(p.scale, 1.03f);
    EXPECT_EQ(Bits(p.light.x), 0x3f39999au);
    EXPECT_EQ(Bits(p.light.y), 0x3f126e98u);
    EXPECT_EQ(Bits(p.light.z), 0x3eaa7efau);
    ASSERT_TRUE(Client::World::StepParticlesChecked(pool, random, std::cref(clock), nullptr, trigonometry).HasValue());
    EXPECT_EQ(p.life, 4);
    EXPECT_FLOAT_EQ(p.scale, 0.96f);
    EXPECT_FLOAT_EQ(p.light.x, 0.3625f);
}
TEST(ParticleMap8Test, EightFadesAtFiveAndContinuesAfterDeath)
{
    RejectingTrigonometry trigonometry;
    Particles pool;
    Particle& p = pool.All()[0];
    p = Particle{.alive = true,
                 .type = Smoke,
                 .subtype = 8,
                 .scale = 3,
                 .position = {0, 0, 10},
                 .light = {2, 4, 8},
                 .life = 6,
                 .velocity = {1, 2, 3}};
    float clock = 0;
    CrtRandom random(7);
    ASSERT_TRUE(Client::World::StepParticlesChecked(pool, random, std::cref(clock), nullptr, trigonometry).HasValue());
    EXPECT_EQ(p.life, 5);
    EXPECT_EQ(p.scale, 3);
    EXPECT_EQ(p.position.z, 10);
    EXPECT_EQ(p.velocity, glm::vec3(0));
    EXPECT_EQ(p.light, glm::vec3(1, 2, 4));
    p.life = 1;
    ASSERT_TRUE(Client::World::StepParticlesChecked(pool, random, std::cref(clock), nullptr, trigonometry).HasValue());
    EXPECT_FALSE(p.alive);
    EXPECT_EQ(p.light, glm::vec3(.5, 1, 2));
}

TEST(ParticleMap8Test, CreateFourUsesSignedLowWordAndMaskedInvalidProfile)
{
    RejectingTrigonometry trigonometry;
    struct Case
    {
        float clock;
        float rotation;
    };
    const Case cases[] = {{4294967296.0f, 0},
                          {-2147483648.0f, -128},
                          {0x1p63f, 0},
                          {-0x1p63f, 0},
                          {std::numeric_limits<float>::infinity(), 0},
                          {-std::numeric_limits<float>::infinity(), 0},
                          {std::numeric_limits<float>::quiet_NaN(), 0}};
    for (const auto& sample : cases)
    {
        Particles pool;
        CrtRandom random(7);
        ASSERT_TRUE(
            pool.CreateChecked(Smoke, {}, {}, {}, 4, 3, std::cref(sample.clock), random, 0, trigonometry).HasValue());
        EXPECT_EQ(pool.All()[0].rotation, sample.rotation);
    }
    EXPECT_EQ(Client::World::FtolLow(4294967297.0), 1);
    EXPECT_EQ(Client::World::FtolLow(static_cast<double>(static_cast<float>(4294967297.0))), 0);
    EXPECT_EQ(Client::World::FtolLow(std::nextafter(0x1p63, 0.0)), -1024);
}

// Observador CPU explícito: los métodos inesperados fallan, ningún servicio opaco se omite.
struct DrawingSpy final : Client::World::IParticleDrawingServices
{
    explicit DrawingSpy(Particle& particle) : p(particle) {}
    Particle& p;
    std::vector<std::string> events;
    bool mutate = false;
    bool failMaterial = false;
    bool failBillboard = false;
    int subtracts = 0;
    float expectedWidth = 16;
    float expectedHeight = 32;
    Client::Core::Expected<glm::vec2, Client::World::ParticleError> Dimensions(std::int32_t image) override
    {
        events.push_back("dimensions");
        EXPECT_EQ(image, Smoke);
        return glm::vec2(8, 16);
    }
    Client::Core::Expected<std::uint8_t, Client::World::ParticleError> MaterialFlag(std::int32_t type) override
    {
        events.push_back("flag");
        EXPECT_EQ(type, Smoke);
        return std::uint8_t{3};
    }
    Result Failure(Client::World::ParticleDrawingPhase phase)
    {
        return Client::Core::Unexpected(Client::World::ParticleDrawingError{
            Client::World::ParticleError::MissingDrawingService, phase, std::make_error_code(std::errc::io_error)});
    }
    Result Material(std::uint8_t flag) override
    {
        events.push_back("material");
        EXPECT_EQ(flag, 3);
        if (failMaterial)
            return Failure(Client::World::ParticleDrawingPhase::Material);
        if (mutate)
        {
            p.subtype = 2;
            p.scale = 99;
            p.alive = false;
            p.position.z = 999;
            p.texture = 0x12345678;
        }
        return {};
    }
    Result Subtractive() override
    {
        events.push_back("subtractive");
        ++subtracts;
        if (subtracts == 1)
            p.subtype = 37;
        else
        {
            p.subtype = 6;
            p.type = 0x60f;
        }
        return {};
    }
    Result Blended() override
    {
        ADD_FAILURE() << "unexpected620c10";
        return Failure(Client::World::ParticleDrawingPhase::BlendedPrefix);
    }
    Result Type620850() override
    {
        ADD_FAILURE() << "unexpected620850";
        return Failure(Client::World::ParticleDrawingPhase::Type620850);
    }
    Result Type620870() override
    {
        ADD_FAILURE() << "unexpected620870";
        return Failure(Client::World::ParticleDrawingPhase::Type620870);
    }
    Result Billboard(Client::Render::ParticleBillboardRequest request) override
    {
        events.push_back("billboard");
        EXPECT_EQ(&request.position.get(), &p.position);
        EXPECT_EQ(&request.light.get(), &p.light);
        EXPECT_EQ(request.width, expectedWidth);
        EXPECT_EQ(request.height, expectedHeight);
        EXPECT_EQ(request.uv, glm::vec2(0));
        EXPECT_EQ(request.uvSize, glm::vec2(1));
        EXPECT_EQ(request.selector, mutate ? 0x12345678 : Smoke);
        EXPECT_EQ(Bits(request.rotation), p.subtype == 6 ? 0u : Bits(p.rotation));
        if (mutate)
        {
            EXPECT_EQ(request.position.get().z, 999);
        }
        if (failBillboard)
            return Failure(Client::World::ParticleDrawingPhase::Bind);
        return {};
    }
};

TEST(ParticleMap8Test, DrawAllFourVariantsBorrowsVectorsAndForcesPositiveZeroOnlyForSix)
{
    for (const int subtype : {4, 6, 7, 8})
    {
        Particles pool;
        Particle& p = pool.All()[0];
        p = Particle{.alive = true,
                     .type = Smoke,
                     .texture = Smoke,
                     .subtype = subtype,
                     .scale = 2,
                     .rotation = std::bit_cast<float>(0x7fc12345u)};
        DrawingSpy spy(p);
        float clock = 0;
        ASSERT_TRUE(Client::World::DrawParticlesChecked(pool, 0, std::cref(clock), spy).HasValue());
        EXPECT_EQ(spy.events, (std::vector<std::string>{"dimensions", "flag", "material", "billboard"}));
    }
}
TEST(ParticleMap8Test, DrawRereadsAllThreeSmokeSelectorsAfterServicesAndKeepsCapturedExtents)
{
    Particles pool;
    Particle& p = pool.All()[0];
    p = Particle{.alive = true, .type = Smoke, .texture = Smoke, .subtype = 4, .scale = 2, .rotation = 7};
    DrawingSpy spy(p);
    spy.mutate = true;
    float clock = 0;
    ASSERT_TRUE(Client::World::DrawParticlesChecked(pool, 1, std::cref(clock), spy).HasValue());
    EXPECT_EQ(spy.events,
              (std::vector<std::string>{"dimensions", "flag", "material", "subtractive", "subtractive", "billboard"}));
    EXPECT_FALSE(p.alive);
    EXPECT_EQ(p.type, 0x60f);
}
TEST(ParticleMap8Test, DrawingErrorPreservesPrefixAndStopsFollowingSlots)
{
    for (const bool material : {true, false})
    {
        Particles pool;
        Particle& p = pool.All()[0];
        p = Particle{.alive = true, .type = Smoke, .texture = Smoke, .subtype = 8, .scale = 2};
        pool.All()[1] = p;
        DrawingSpy spy(p);
        spy.failMaterial = material;
        spy.failBillboard = !material;
        float clock = 0;
        const auto result = Client::World::DrawParticlesChecked(pool, 0, std::cref(clock), spy);
        ASSERT_FALSE(result.HasValue());
        EXPECT_EQ(result.Error().slot, 0u);
        EXPECT_EQ(result.Error().error.backendError, std::make_error_code(std::errc::io_error));
        EXPECT_EQ(spy.events.size(), material ? 3u : 4u);
    }
}
TEST(ParticleMap8Test, EightGrowthUsesStoredPhaseForHeightAndWidePhaseForScale)
{
    RejectingTrigonometry trigonometry;
    Particles pool;
    Particle& p = pool.All()[0];
    p = Particle{.alive = true,
                 .type = Smoke,
                 .subtype = 8,
                 .scale = -16777216,
                 .position = {0, 0, 10},
                 .life = 7,
                 .verticalSpeed = 16777216,
                 .velocity = {2, 4, 8}};
    float clock = 0;
    CrtRandom random(7);
    ASSERT_TRUE(Client::World::StepParticlesChecked(pool, random, std::cref(clock), nullptr, trigonometry).HasValue());
    EXPECT_EQ(p.life, 6);
    EXPECT_EQ(Bits(p.scale), 0x3ca3d70au);
    EXPECT_EQ(p.position.z, 335544320);
    EXPECT_EQ(p.light, glm::vec3(.25f));
    EXPECT_FLOAT_EQ(p.velocity.x, .84f);
    EXPECT_FLOAT_EQ(p.velocity.y, 1.68f);
    EXPECT_FLOAT_EQ(p.velocity.z, 3.36f);
}

TEST(ParticleMap8Test, CreatePrefixReadsAnglesAfterWritingAliasedColorAndRetainsResiduals)
{
    RejectingTrigonometry trigonometry;
    Particles pool;
    Particle& p = pool.All()[0];
    p.light = {9, 8, 7};
    p.field38 = 0x12345678;
    p.rotationFactor = -7;
    p.field64 = {4, 5};
    float clock = 0;
    CrtRandom random(7);
    ASSERT_TRUE(
        pool.CreateChecked(Smoke, {1, 2, 3}, p.light, {4, 5, 6}, 8, 3, std::cref(clock), random, 23, trigonometry)
            .HasValue());
    EXPECT_EQ(p.angles, glm::vec3(4, 5, 6));
    EXPECT_EQ(p.light, glm::vec3(4, 5, 6));
    EXPECT_EQ(p.field38, 0x12345678u);
    EXPECT_EQ(p.rotationFactor, -7);
    EXPECT_EQ(p.field64, glm::vec2(4, 5));
}
TEST(ParticleMap8Test, UnsupportedFollowingSlotStopsOnlyAfterItsCommonPrefix)
{
    RejectingTrigonometry trigonometry;
    Particles pool;
    pool.All()[0] = Particle{.alive = true, .type = Smoke, .subtype = 4, .life = 2};
    pool.All()[1] = Particle{.alive = true, .type = 0x7fffffff, .subtype = 3, .life = 2};
    pool.All()[2] = Particle{.alive = true, .type = Smoke, .subtype = 8, .life = 6};
    float clock = 0;
    CrtRandom random(7);
    const auto result = Client::World::StepParticlesChecked(pool, random, std::cref(clock), nullptr, trigonometry);
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error(), Client::World::ParticleError::UnsupportedType);
    EXPECT_EQ(pool.All()[0].life, 1);
    EXPECT_EQ(pool.All()[1].life, 1);
    EXPECT_EQ(pool.All()[2].life, 6);
    EXPECT_EQ(random.State(), 0xc40e66b9u);
}
TEST(ParticleMap8Test, EmptyPassStillConsumesExactlyTwoWindDraws)
{
    RejectingTrigonometry trigonometry;
    Particles pool;
    float clock = std::numeric_limits<float>::quiet_NaN();
    CrtRandom random(7);
    ASSERT_TRUE(Client::World::StepParticlesChecked(pool, random, std::cref(clock), nullptr, trigonometry).HasValue());
    EXPECT_EQ(random.State(), 0xc40e66b9u);
}

TEST(ParticleMap8Test, SevenReloadsRoundedScaleBeforeItsLateShrink)
{
    RejectingTrigonometry trigonometry;
    Particles pool;
    Particle& p = pool.All()[0];
    p = Particle{.alive = true, .type = Smoke, .subtype = 7, .scale = .1f, .life = 5};
    float clock = 0;
    CrtRandom random(7);
    ASSERT_TRUE(Client::World::StepParticlesChecked(pool, random, std::cref(clock), nullptr, trigonometry).HasValue());
    EXPECT_EQ(Bits(p.scale), 0x3cf5c28cu);
}
TEST(ParticleMap8Test, FirstFreePhysicalSlotKeepsItsUnwrittenResidualFields)
{
    RejectingTrigonometry trigonometry;
    Particles pool;
    for (Particle& p : pool.All())
        p.alive = true;
    Particle& p = pool.All()[2979];
    p.alive = false;
    p.rotationFactor = 17;
    p.field38 = 0xcafebabeu;
    float clock = 0;
    CrtRandom random(7);
    const auto created = pool.CreateChecked(Smoke, {}, {}, {}, 4, 3, std::cref(clock), random, 9, trigonometry);
    ASSERT_TRUE(created.HasValue());
    EXPECT_EQ(created.Value(), 2979u);
    EXPECT_EQ(p.rotationFactor, 17);
    EXPECT_EQ(p.field38, 0xcafebabeu);
    EXPECT_EQ(random.State(), 0xc40e66b9u);
}

struct TrigCall
{
    double argument;
    float multiplier;
    double product;
    bool fail = false;
};
struct TrigObservation
{
    std::int32_t life;
    bool alive;
    float z;
    float scale;
    glm::vec3 light;
};
// Productos prescritos para verificar el caller, no muestras numéricas FSIN del original.
// El spy sólo observa: nunca modifica pool, reloj ni RNG.
struct ObservedTrigonometry final : Client::World::IParticleTrigonometry
{
    ObservedTrigonometry(const Particle& observed, std::vector<TrigCall> prescribed)
        : particle(observed), calls(std::move(prescribed))
    {
    }
    const Particle& particle;
    std::vector<TrigCall> calls;
    mutable std::vector<TrigObservation> observations;
    Client::Core::Expected<double, Client::World::ParticleError> SineProductPC53(double argument,
                                                                                 float multiplier) const override
    {
        const std::size_t index = observations.size();
        observations.push_back({particle.life, particle.alive, particle.position.z, particle.scale, particle.light});
        if (index >= calls.size())
        {
            ADD_FAILURE() << "seno fuera de la secuencia prescrita";
            return Client::Core::Unexpected(Client::World::ParticleError::UnrepresentableTrigonometry);
        }
        const TrigCall& call = calls[index];
        if (std::isnan(call.argument))
        {
            EXPECT_TRUE(std::isnan(argument));
        }
        else
        {
            EXPECT_EQ(std::bit_cast<std::uint64_t>(argument), std::bit_cast<std::uint64_t>(call.argument));
        }
        EXPECT_EQ(Bits(multiplier), Bits(call.multiplier));
        if (call.fail)
            return Client::Core::Unexpected(Client::World::ParticleError::UnrepresentableTrigonometry);
        return call.product;
    }
    Client::Core::Expected<double, Client::World::ParticleError> CosineProductPC53(double, float) const override
    {
        ADD_FAILURE() << "coseno fuera del contrato5f2";
        return Client::Core::Unexpected(Client::World::ParticleError::UnrepresentableTrigonometry);
    }
};

TEST(ParticleMap8Test, SixRetainsWideTimeForBothSignedLowWordRemainders)
{
    for (const int sign : {1, -1})
    {
        Particles pool;
        Particle& p = pool.All()[0];
        p = Particle{.alive = true,
                     .type = Smoke,
                     .subtype = 6,
                     .scale = 7,
                     .life = 2,
                     .rotation = 4,
                     .verticalSpeed = static_cast<float>(sign)};
        float clock = static_cast<float>(sign) * 0x1p32f;
        CrtRandom random(7);
        ObservedTrigonometry trigonometry(p, {{static_cast<double>(sign) * 0x1.a36e2e01a36e3p+19, 20, 0},
                                              {static_cast<double>(sign) * 0x1.c9871p-10, .5f, 0}});
        ASSERT_TRUE(
            Client::World::StepParticlesChecked(pool, random, std::cref(clock), nullptr, trigonometry).HasValue());
        ASSERT_EQ(trigonometry.observations.size(), 2u);
        EXPECT_EQ(p.life, 10);
        EXPECT_TRUE(p.alive);
        EXPECT_EQ(p.position.z, 4);
        EXPECT_EQ(Bits(p.scale), 0x3fe66666u);
        EXPECT_EQ(random.State(), 0xc40e66b9u);
        EXPECT_EQ(Bits(trigonometry.observations[1].light.x), 0x3eb851ecu);
        EXPECT_EQ(Bits(trigonometry.observations[1].light.y), 0x3e99999au);
        EXPECT_EQ(Bits(trigonometry.observations[1].light.z), 0x3e75c290u);
        EXPECT_EQ(trigonometry.observations[1].scale, 7);
    }
}
TEST(ParticleMap8Test, SixPreservesTheProduct53BeforeItsBaseAddition)
{
    Particles pool;
    Particle& p = pool.All()[0];
    p = Particle{.alive = true, .type = Smoke, .subtype = 6, .life = 2, .rotation = -10};
    float clock = 1;
    CrtRandom random(7);
    ObservedTrigonometry trigonometry(p, {{0x1.a36e2ep-13, 20, 10 + 0x1p-49}, {0x1.c9871p-10, .5f, 0}});
    ASSERT_TRUE(Client::World::StepParticlesChecked(pool, random, std::cref(clock), nullptr, trigonometry).HasValue());
    EXPECT_EQ(p.position.z, 0x1p-49f);
}
TEST(ParticleMap8Test, SixFirstFailureKeepsCommonMovementAndLifeTenWithoutResurrecting)
{
    Particles pool;
    Particle& p = pool.All()[0];
    p = Particle{.alive = true,
                 .type = Smoke,
                 .subtype = 6,
                 .scale = 7,
                 .position = {5, 6, 7},
                 .light = {2, 3, 4},
                 .life = 1,
                 .rotation = 11,
                 .moves = true,
                 .velocity = {1, 2, 3}};
    pool.All()[1] = Particle{.alive = true, .type = Smoke, .subtype = 4, .life = 2};
    float clock = 0;
    CrtRandom random(7);
    ObservedTrigonometry trigonometry(p, {{0, 20, 0, true}});
    const auto result = Client::World::StepParticlesChecked(pool, random, std::cref(clock), nullptr, trigonometry);
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error(), Client::World::ParticleError::UnrepresentableTrigonometry);
    EXPECT_EQ(p.life, 10);
    EXPECT_FALSE(p.alive);
    EXPECT_EQ(p.position, glm::vec3(6, 8, 10));
    EXPECT_EQ(p.scale, 7);
    EXPECT_EQ(p.light, glm::vec3(2, 3, 4));
    EXPECT_EQ(pool.All()[1].life, 2);
    EXPECT_EQ(random.State(), 0xc40e66b9u);
    ASSERT_EQ(trigonometry.observations.size(), 1u);
    EXPECT_EQ(trigonometry.observations[0].life, 10);
    EXPECT_FALSE(trigonometry.observations[0].alive);
}
TEST(ParticleMap8Test, SixSecondFailureKeepsZAndRgbAndStopsBeforeScaleAndNextSlot)
{
    Particles pool;
    Particle& p = pool.All()[0];
    p = Particle{.alive = true, .type = Smoke, .subtype = 6, .scale = 7, .light = {2, 3, 4}, .life = 2, .rotation = 4};
    pool.All()[1] = Particle{.alive = true, .type = Smoke, .subtype = 4, .life = 2};
    float clock = 0;
    CrtRandom random(7);
    ObservedTrigonometry trigonometry(p, {{0, 20, .25}, {0, .5f, 0, true}});
    const auto result = Client::World::StepParticlesChecked(pool, random, std::cref(clock), nullptr, trigonometry);
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error(), Client::World::ParticleError::UnrepresentableTrigonometry);
    EXPECT_EQ(p.position.z, 4.25f);
    EXPECT_EQ(p.scale, 7);
    EXPECT_EQ(pool.All()[1].life, 2);
    ASSERT_EQ(trigonometry.observations.size(), 2u);
    EXPECT_EQ(trigonometry.observations[0].light, glm::vec3(2, 3, 4));
    EXPECT_EQ(Bits(p.light.x), 0x3eb851ecu);
    EXPECT_EQ(Bits(p.light.y), 0x3e99999au);
    EXPECT_EQ(Bits(p.light.z), 0x3e75c290u);
    EXPECT_EQ(trigonometry.observations[1].z, 4.25f);
    EXPECT_EQ(trigonometry.observations[1].scale, 7);
}
TEST(ParticleMap8Test, SixDoesNotPrevalidateFtolAtEitherSigned64Limit)
{
    for (const int sign : {1, -1})
    {
        Particles pool;
        Particle& p = pool.All()[0];
        p = Particle{.alive = true, .type = Smoke, .subtype = 6, .life = 2, .rotation = 4};
        float clock = static_cast<float>(sign) * 0x1p63f;
        CrtRandom random(7);
        ObservedTrigonometry trigonometry(p, {{static_cast<double>(sign) * 0x1.a36e2ep+50, 20, 0}, {0, .5f, 0}});
        ASSERT_TRUE(
            Client::World::StepParticlesChecked(pool, random, std::cref(clock), nullptr, trigonometry).HasValue());
        EXPECT_EQ(p.position.z, 4);
        EXPECT_EQ(Bits(p.scale), 0x3fe66666u);
        EXPECT_EQ(trigonometry.observations.size(), 2u);
    }
}
TEST(ParticleMap8Test, SixRejectsNonfiniteAtFirstTrigAfterLifeStore)
{
    for (const float clock : {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::quiet_NaN()})
    {
        Particles pool;
        Particle& p = pool.All()[0];
        p = Particle{.alive = true, .type = Smoke, .subtype = 6, .scale = 7, .life = 2};
        CrtRandom random(7);
        const double argument =
            std::isnan(clock) ? std::numeric_limits<double>::quiet_NaN() : static_cast<double>(clock);
        ObservedTrigonometry trigonometry(p, {{argument, 20, 0, true}});
        const auto result = Client::World::StepParticlesChecked(pool, random, std::cref(clock), nullptr, trigonometry);
        ASSERT_FALSE(result.HasValue());
        EXPECT_EQ(p.life, 10);
        EXPECT_EQ(p.scale, 7);
        EXPECT_EQ(p.position.z, 0);
    }
}

TEST(ParticleMap8Test, SixLoadsClockAfterWindInsteadOfAtPassEntry)
{
    Particles pool;
    Particle& p = pool.All()[0];
    p = Particle{.alive = true, .type = Smoke, .subtype = 6, .life = 2};
    CrtRandom random(7);
    // R1=61: (-939)*f32(.0006) se almacena como bf103afc; producto racional RN53 independiente.
    ObservedTrigonometry trigonometry(p, {{-0x1.d89d3b6dc29p-14, 20, 0}, {0, .5f, 0}});
    ASSERT_TRUE(
        Client::World::StepParticlesChecked(pool, random, std::cref(pool.Wind().change.x), nullptr, trigonometry)
            .HasValue());
    EXPECT_EQ(Bits(pool.Wind().change.x), 0xbf103afcu);
    EXPECT_EQ(trigonometry.observations.size(), 2u);
}
TEST(ParticleMap8Test, SixRetainsTimeAfterZChangesAliasedClockAndReloadsForTheNextSlot)
{
    Particles pool;
    Particle& p = pool.All()[0];
    p = Particle{.alive = true, .type = Smoke, .subtype = 6, .position = {0, 0, 100}, .life = 2, .rotation = 10};
    pool.All()[1] = Particle{.alive = true, .type = Smoke, .subtype = 6, .life = 2, .rotation = 8};
    CrtRandom random(7);
    ObservedTrigonometry trigonometry(
        p, {{0x1.47ae13fp-6, 20, 20}, {0x1.6571848p-3, .5f, 0}, {0x1.89374b2p-8, 20, 0}, {0x1.acee9fp-5, .5f, 0}});
    ASSERT_TRUE(
        Client::World::StepParticlesChecked(pool, random, std::cref(p.position.z), nullptr, trigonometry).HasValue());
    ASSERT_EQ(trigonometry.observations.size(), 4u);
    EXPECT_EQ(p.position.z, 30);
    EXPECT_EQ(pool.All()[1].position.z, 8);
    EXPECT_EQ(random.State(), 0xc40e66b9u);
}
TEST(ParticleMap8Test, ExpiredSixCompletesItsSuffixOnceWithoutReactivation)
{
    Particles pool;
    Particle& p = pool.All()[0];
    p = Particle{.alive = true, .type = Smoke, .subtype = 6, .life = 1, .rotation = 4};
    float clock = 0;
    CrtRandom random(7);
    ObservedTrigonometry trigonometry(p, {{0, 20, 0}, {0, .5f, 0}});
    ASSERT_TRUE(Client::World::StepParticlesChecked(pool, random, std::cref(clock), nullptr, trigonometry).HasValue());
    EXPECT_FALSE(p.alive);
    EXPECT_EQ(p.life, 10);
    EXPECT_EQ(p.position.z, 4);
    EXPECT_EQ(Bits(p.scale), 0x3fe66666u);
    RejectingTrigonometry rejecting;
    ASSERT_TRUE(Client::World::StepParticlesChecked(pool, random, std::cref(clock), nullptr, rejecting).HasValue());
    EXPECT_EQ(p.life, 10);
    EXPECT_FALSE(p.alive);
}
TEST(ParticleMap8Test, AllFourVariantsCompleteCreateStepAndBorrowedDraw)
{
    struct Case
    {
        int subtype;
        std::uint32_t width;
        std::uint32_t height;
    };
    for (const Case sample : {Case{4, 0x40d1eb85u, 0x4151eb85u}, Case{6, 0x41666666u, 0x41e66666u},
                              Case{7, 0x42028f5cu, 0x42828f5cu}, Case{8, 0x419ae148u, 0x421ae148u}})
    {
        Particles pool;
        Particle& p = pool.All()[0];
        float clock = 0;
        CrtRandom random(7);
        RejectingTrigonometry rejecting;
        ASSERT_TRUE(pool.CreateChecked(Smoke, {100, 200, 300}, {}, {2, 3, 4}, sample.subtype, 3, std::cref(clock),
                                       random, 0, rejecting)
                        .HasValue());
        if (sample.subtype == 6)
        {
            ObservedTrigonometry trigonometry(p, {{0x1.8fc503d8p-7, 20, 0}, {0x1.b414bb4p-4, .5f, 0}});
            ASSERT_TRUE(
                Client::World::StepParticlesChecked(pool, random, std::cref(clock), nullptr, trigonometry).HasValue());
            EXPECT_EQ(trigonometry.observations.size(), 2u);
        }
        else
        {
            ASSERT_TRUE(
                Client::World::StepParticlesChecked(pool, random, std::cref(clock), nullptr, rejecting).HasValue());
        }
        DrawingSpy spy(p);
        spy.expectedWidth = std::bit_cast<float>(sample.width);
        spy.expectedHeight = std::bit_cast<float>(sample.height);
        ASSERT_TRUE(Client::World::DrawParticlesChecked(pool, 0, std::cref(clock), spy).HasValue());
        EXPECT_EQ(spy.events, (std::vector<std::string>{"dimensions", "flag", "material", "billboard"}));
    }
}
TEST(ParticleMap8Test, SixUsesNonzeroSecondProductBeforeTheScaleStore)
{
    Particles pool;
    Particle& p = pool.All()[0];
    p = Particle{.alive = true, .type = Smoke, .subtype = 6, .scale = 7, .life = 2};
    float clock = 1;
    CrtRandom random(7);
    // Producto sintético PC53: .25 + 2^-23 - 2^-45, con h64 = 2*producto en [-1,1].
    // Base exacta 3fe66666 + producto queda debajo del punto medio de dos f32.
    // RN32 da40033333; omitir producto da3fe66666; narrow f32 previo empata y da40033334.
    // No es una muestra FSIN ni demuestra sensibilidad h64->double para el factor0.5.
    ObservedTrigonometry trigonometry(p, {{0x1.a36e2ep-13, 20, 0}, {0x1.c9871p-10, .5f, 0x1.000007ffffe00p-2}});
    ASSERT_TRUE(Client::World::StepParticlesChecked(pool, random, std::cref(clock), nullptr, trigonometry).HasValue());
    ASSERT_EQ(trigonometry.observations.size(), 2u);
    EXPECT_EQ(trigonometry.observations[1].scale, 7);
    EXPECT_EQ(Bits(p.scale), 0x40033333u);
}

TEST(ParticleMap8Test, FourComputesNonzeroRgbFromPostDecrementLife)
{
    RejectingTrigonometry trigonometry;
    Particles pool;
    Particle& p = pool.All()[0];
    p = Particle{.alive = true, .type = Smoke, .subtype = 4, .light = {9, 8, 7}, .life = 9};
    float clock = 0;
    CrtRandom random(7);
    // Contrato5446e0 §5f2/4: vida9->8; base ancha 8*.125=1, RGB conserva los tres f32 exactos.
    ASSERT_TRUE(Client::World::StepParticlesChecked(pool, random, std::cref(clock), nullptr, trigonometry).HasValue());
    EXPECT_EQ(p.life, 8);
    EXPECT_EQ(Bits(p.light.x), 0x3ef0f0f1u);
    EXPECT_EQ(Bits(p.light.y), 0x3eca3097u);
    EXPECT_EQ(Bits(p.light.z), 0x3ea0a0a1u);
}

// No se conserva el overload vivo provisional sin evaluador.
using ClockOnlyCreateSignature = Client::Core::Expected<std::optional<std::size_t>, Client::World::ParticleError> (
    Particles::*)(std::int32_t, const glm::vec3&, const glm::vec3&, const glm::vec3&, std::int32_t, float,
                  std::reference_wrapper<const float>, CrtRandom&, std::uint32_t);
template <typename Pool>
concept HasClockOnlyCreate = requires { static_cast<ClockOnlyCreateSignature>(&Pool::CreateChecked); };
template <typename Pool>
concept HasClockOnlyStep = requires(Pool& pool, CrtRandom& random, const float& clock) {
    Client::World::StepParticlesChecked(pool, random, std::cref(clock), nullptr);
};
static_assert(!HasClockOnlyCreate<Particles>);
static_assert(!HasClockOnlyStep<Particles>);
} // namespace
