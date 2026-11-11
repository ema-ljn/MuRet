#include "World/Particles.h"
#include "World/IParticleTrigonometry.h"
#include "World/ParticleDrawing.h"
#include "World/IParticleDrawingServices.h"
#include "Render/Models/Cloth.h"
#include "Render/ParticleBillboardRequest.h"
#include "Core/Expected.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/ParticleDrawServices.h"
#include "Scenes/TextureMetadata.h"
#include "Render/IParticleDrawBackend.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Render/SpriteVertex.h"
#include "Data/Models/Model.h"
#include "Data/Textures/Texture.h"
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>
#include <cstdint>
#include <bit>
#include <cstddef>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <system_error>
#include <vector>
namespace
{
using namespace Client;
constexpr std::int32_t Type = 0x60f;
// Evaluador analítico del fixture: sólo acepta u=0; los errores son explícitos.
class Trigonometry final : public World::IParticleTrigonometry
{
public:
    mutable std::vector<int> calls;
    const World::Particle* observed = nullptr;
    bool failSine = false;
    bool failCosine = false;
    double argument = 0;
    double sineProduct = 0;
    double cosineProduct = 40;
    Core::Expected<double, World::ParticleError> SineProductPC53(double u, float factor) const override
    {
        calls.push_back(1);
        EXPECT_EQ(u, argument);
        EXPECT_EQ(factor, 40);
        if (failSine)
            return Core::Unexpected(World::ParticleError::UnrepresentableTrigonometry);
        return sineProduct;
    }
    Core::Expected<double, World::ParticleError> CosineProductPC53(double u, float factor) const override
    {
        calls.push_back(2);
        EXPECT_EQ(u, argument);
        EXPECT_EQ(factor, 40);
        if (observed)
        {
            EXPECT_EQ(observed->position.x, static_cast<float>(sineProduct + observed->start.x));
        }
        if (failCosine)
            return Core::Unexpected(World::ParticleError::UnrepresentableTrigonometry);
        return cosineProduct;
    }
};
// Observador puro del prefijo: devuelve error en el primer seno sin inventar un valor trigonométrico.
class MovementFailureTrigonometry final : public World::IParticleTrigonometry
{
public:
    explicit MovementFailureTrigonometry(const World::Particle& particle) : m_particle(particle) {}
    mutable int sineCalls = 0;
    Core::Expected<double, World::ParticleError> SineProductPC53(double argument, float multiplier) const override
    {
        ++sineCalls;
        EXPECT_EQ(argument, 0x1.99999ap-3); // (vida0 + Vx2) por f32 bits3dcccccd.
        EXPECT_EQ(multiplier, 40);
        EXPECT_EQ(m_particle.life, 0);
        EXPECT_FALSE(m_particle.alive);
        EXPECT_EQ(m_particle.position, glm::vec3(13, 20, 42));
        return Core::Unexpected(World::ParticleError::UnrepresentableTrigonometry);
    }
    Core::Expected<double, World::ParticleError> CosineProductPC53(double, float) const override
    {
        ADD_FAILURE() << "El error del primer seno debe detener el sufijo";
        return Core::Unexpected(World::ParticleError::UnrepresentableTrigonometry);
    }

private:
    const World::Particle& m_particle;
};
class Services final : public World::IParticleDrawingServices
{
public:
    explicit Services(World::Particle& particle) : p(particle) {}
    World::Particle& p;
    std::vector<int> trace;
    float expectedWidth = 16;
    float expectedHeight = 32;
    bool mutate = false;
    bool fail = false;
    Core::Expected<glm::vec2, World::ParticleError> Dimensions(std::int32_t image) override
    {
        trace.push_back(1);
        EXPECT_EQ(image, Type);
        return glm::vec2(8, 16);
    }
    Core::Expected<std::uint8_t, World::ParticleError> MaterialFlag(std::int32_t type) override
    {
        trace.push_back(2);
        EXPECT_EQ(type, Type);
        return std::uint8_t{3};
    }
    Result Material(std::uint8_t flag) override
    {
        trace.push_back(3);
        EXPECT_EQ(flag, 3);
        if (fail)
            return Core::Unexpected(World::ParticleDrawingError{
                World::ParticleError::MissingMaterialFlag, World::ParticleDrawingPhase::Material, {}});
        if (mutate)
        {
            p.subtype = 4;
            p.scale = 99;
            p.texture = 0x12345678;
            p.alive = false;
        }
        return {};
    }
    Result Subtractive() override
    {
        ADD_FAILURE();
        return {};
    }
    Result Blended() override
    {
        ADD_FAILURE();
        return {};
    }
    Result Type620850() override
    {
        ADD_FAILURE();
        return {};
    }
    Result Type620870() override
    {
        ADD_FAILURE();
        return {};
    }
    Result Billboard(Render::ParticleBillboardRequest request) override
    {
        trace.push_back(4);
        EXPECT_EQ(&request.position.get(), &p.position);
        EXPECT_EQ(&request.light.get(), &p.light);
        EXPECT_EQ(request.width, expectedWidth);
        EXPECT_EQ(request.height, expectedHeight);
        EXPECT_EQ(request.selector, mutate ? 0x12345678 : Type);
        return {};
    }
};
TEST(Particle60f3Test, FourRandomValuesInitializeAndRetainResidualFields)
{
    World::Particles particles;
    auto& p = particles.All()[0];
    p.field38 = 123;
    p.rotationFactor = 7;
    p.field64 = {8, 9};
    Render::Models::CrtRandom random(7);
    const float clock = 0;
    [[maybe_unused]] Trigonometry trig;
    const auto result =
        particles.CreateChecked(Type, {2, 3, 4}, {8, 9, 10}, {1, 2, 3}, 3, 2, std::cref(clock), random, 91, trig);
    ASSERT_TRUE(result.HasValue());
    ASSERT_TRUE(result.Value().has_value());
    EXPECT_EQ(p.life, 60);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(p.verticalSpeed), 0x405c28f6u);
    EXPECT_EQ(p.velocity, glm::vec3(-128, 0, 0));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(p.scale), 0x40033333u);
    EXPECT_EQ(p.rotation, 200);
    EXPECT_EQ(p.owner, 91u);
    EXPECT_EQ(p.angles, glm::vec3(0));
    EXPECT_EQ(p.start, glm::vec3(2, 3, 4));
    EXPECT_EQ(p.field38, 123u);
    EXPECT_EQ(p.rotationFactor, 7);
    EXPECT_EQ(p.field64, glm::vec2(8, 9));
    EXPECT_EQ(random.State(), 0x1b8052fbu);
    EXPECT_TRUE(trig.calls.empty());
}
TEST(Particle60f3Test, FullPoolReturnsBeforeAdmissionOrRandom)
{
    World::Particles particles;
    for (auto& p : particles.All())
        p.alive = true;
    Render::Models::CrtRandom random(7);
    const float clock = 0;
    [[maybe_unused]] Trigonometry trig;
    const auto result = particles.CreateChecked(Type, {}, {}, {}, 3, 2, std::cref(clock), random, 0, trig);
    ASSERT_TRUE(result.HasValue());
    EXPECT_FALSE(result.Value());
    EXPECT_EQ(random.State(), 7u);
}
TEST(Particle60f3Test, MaterialFailureIsNotHiddenByLifeGateAndStopsLaterSlot)
{
    World::Particles particles;
    auto& p = particles.All()[0];
    p.alive = true;
    p.type = Type;
    p.texture = Type;
    p.subtype = 3;
    p.scale = 2;
    p.life = 60;
    particles.All()[1] = p;
    Services services(p);
    services.fail = true;
    const float clock = 0;
    [[maybe_unused]] Trigonometry trig;
    const auto result = World::DrawParticlesChecked(particles, 0, std::cref(clock), services);
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error().slot, 0u);
    EXPECT_EQ(result.Error().error.phase, World::ParticleDrawingPhase::Material);
    EXPECT_EQ(services.trace, (std::vector<int>{1, 2, 3}));
}
TEST(Particle60f3Test, DrawGateRunsAfterMetadataAndMaterial)
{
    World::Particles particles;
    auto& p = particles.All()[0];
    p.alive = true;
    p.type = Type;
    p.texture = Type;
    p.subtype = 3;
    p.scale = 2;
    p.life = 60;
    Services services(p);
    const float clock = 0;
    [[maybe_unused]] Trigonometry trig;
    ASSERT_TRUE(World::DrawParticlesChecked(particles, 0, std::cref(clock), services).HasValue());
    EXPECT_EQ(services.trace, (std::vector<int>{1, 2, 3}));
}
TEST(Particle60f3Test, LateSubtypeFourBypassesGateWithoutRefilterOrRecompute)
{
    World::Particles particles;
    auto& p = particles.All()[0];
    p.alive = true;
    p.type = Type;
    p.texture = Type;
    p.subtype = 3;
    p.scale = 2;
    p.life = 60;
    Services services(p);
    services.mutate = true;
    const float clock = 0;
    [[maybe_unused]] Trigonometry trig;
    ASSERT_TRUE(World::DrawParticlesChecked(particles, 0, std::cref(clock), services).HasValue());
    EXPECT_EQ(services.trace, (std::vector<int>{1, 2, 3, 4}));
}
// El ángulo es exactamente cero: sin(0)=0 y cos(0)=1, sin usar el SUT para derivar esperados.
TEST(Particle60f3Test, StepAtZeroAngleUsesPersistentBasesAndFadesAfterDecrement)
{
    World::Particles particles;
    auto& p = particles.All()[0];
    p.alive = true;
    p.type = Type;
    p.subtype = 3;
    p.life = 21;
    p.moves = false;
    p.velocity = {-20, 0, 0};
    p.start = {7, 13, 999};
    p.position = {101, 102, 3};
    p.verticalSpeed = 2;
    p.scale = 1;
    p.light = {1, 2, 4};
    Render::Models::CrtRandom random(7);
    const float clock = 0;
    [[maybe_unused]] Trigonometry trig;
    const auto result = World::StepParticlesChecked(particles, random, std::cref(clock), nullptr, trig);
    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(p.life, 20);
    EXPECT_TRUE(p.alive);
    EXPECT_EQ(p.position, glm::vec3(7, -27, 5));
    EXPECT_FLOAT_EQ(p.scale, .998f);
    // K=bits3f68ba2e; por 1,2,4 sólo cambia el exponente: productos y stores exactos.
    EXPECT_EQ(std::bit_cast<std::uint32_t>(p.light.x), 0x3f68ba2eu);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(p.light.y), 0x3fe8ba2eu);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(p.light.z), 0x4068ba2eu);
    EXPECT_EQ(p.start, glm::vec3(7, 13, 999));
    EXPECT_EQ(random.State(), 3289278137u);
}
TEST(Particle60f3Test, CommonMovementRemainsVisibleBeforeTrigonometryError)
{
    World::Particles particles;
    auto& p = particles.All()[0];
    p.alive = true;
    p.type = Type;
    p.subtype = 3;
    p.life = 1;
    p.moves = true;
    p.position = {11, 23, 37};
    p.velocity = {2, -3, 5};
    p.angles = {0, 0, 0};
    p.start = {47, 53, 59};
    p.verticalSpeed = 7;
    p.scale = 3;
    p.light = {1, 2, 4};
    particles.All()[1] = p;
    MovementFailureTrigonometry trig(p);
    Render::Models::CrtRandom random(7);
    const float clock = 0;

    const auto result = World::StepParticlesChecked(particles, random, std::cref(clock), nullptr, trig);

    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error(), World::ParticleError::UnrepresentableTrigonometry);
    EXPECT_EQ(trig.sineCalls, 1);
    EXPECT_EQ(random.State(), 3289278137u);
    EXPECT_FALSE(p.alive);
    EXPECT_EQ(p.life, 0);
    // Ángulos cero: rotación identidad, (11,23,37)+(2,-3,5)=(13,20,42), sin usar el SUT como oráculo.
    EXPECT_EQ(p.position, glm::vec3(13, 20, 42));
    EXPECT_EQ(p.start, glm::vec3(47, 53, 59));
    EXPECT_EQ(p.scale, 3);
    EXPECT_EQ(p.light, glm::vec3(1, 2, 4));
    EXPECT_EQ(p.verticalSpeed, 7);
    EXPECT_TRUE(particles.All()[1].alive);
    EXPECT_EQ(particles.All()[1].life, 1);
    EXPECT_EQ(particles.All()[1].position, glm::vec3(11, 23, 37));
}
TEST(Particle60f3Test, ExpirationStillFinishesCaseAndVisitsFollowingSlot)
{
    World::Particles particles;
    for (int i = 0; i < 2; ++i)
    {
        auto& p = particles.All()[static_cast<std::size_t>(i)];
        p.alive = true;
        p.type = Type;
        p.subtype = 3;
        p.life = 1;
        p.moves = false;
        p.start = {7, 13, 999};
        p.position.z = 3;
        p.verticalSpeed = 2;
        p.scale = 0;
    }
    Render::Models::CrtRandom random(7);
    const float clock = 0;
    [[maybe_unused]] Trigonometry trig;
    ASSERT_TRUE(World::StepParticlesChecked(particles, random, std::cref(clock), nullptr, trig).HasValue());
    for (int i = 0; i < 2; ++i)
    {
        const auto& p = particles.All()[static_cast<std::size_t>(i)];
        EXPECT_FALSE(p.alive);
        EXPECT_EQ(p.life, 0);
        EXPECT_EQ(p.position, glm::vec3(7, -27, 5));
        EXPECT_LT(p.scale, 0);
    }
    EXPECT_EQ(random.State(), 3289278137u);
}
TEST(Particle60f3Test, SignedLifeWrapKeepsSlotAlive)
{
    World::Particles particles;
    auto& p = particles.All()[0];
    p.alive = true;
    p.type = Type;
    p.subtype = 3;
    p.life = std::numeric_limits<std::int32_t>::min();
    Render::Models::CrtRandom random(7);
    const float clock = 0;
    [[maybe_unused]] Trigonometry trig;
    p.velocity.x = -2147483648.0f;
    trig.argument = -0x1.99999ap-4;
    ASSERT_TRUE(World::StepParticlesChecked(particles, random, std::cref(clock), nullptr, trig).HasValue());
    EXPECT_TRUE(p.alive);
    EXPECT_EQ(p.life, std::numeric_limits<std::int32_t>::max());
}

TEST(Particle60f3Test, LifeGateIsExactDwordEqualityAndKeepsNegativeLifeDraws)
{
    for (const std::int32_t life : {59, 61, 316, 0, -1})
    {
        World::Particles particles;
        auto& p = particles.All()[0];
        p.alive = true;
        p.type = Type;
        p.texture = Type;
        p.subtype = 3;
        p.scale = 2;
        p.life = life;
        Services services(p);
        const float clock = 0;
        [[maybe_unused]] Trigonometry trig;
        ASSERT_TRUE(World::DrawParticlesChecked(particles, 0, std::cref(clock), services).HasValue());
        EXPECT_EQ(services.trace, (std::vector<int>{1, 2, 3, 4}));
    }
}

TEST(Particle60f3Test, AliasInputsAreReadInPrefixOrderAndResidualSlotCanBeReused)
{
    World::Particles particles;
    auto& p = particles.All()[0];
    p.position = {10, 20, 30};
    p.light = {1, 2, 3};
    p.start = {90, 80, 70};
    p.field38 = 42;
    p.rotationFactor = 13;
    p.field64 = {14, 15};
    Render::Models::CrtRandom random(7);
    const float clock = 0;
    [[maybe_unused]] Trigonometry trig;
    const auto created =
        particles.CreateChecked(Type, p.light, p.start, p.position, 3, 2, std::cref(clock), random, 0, trig);
    ASSERT_TRUE(created.HasValue());
    ASSERT_TRUE(created.Value());
    EXPECT_EQ(p.position, glm::vec3(1, 2, 3));
    EXPECT_EQ(p.light, glm::vec3(1, 2, 3));
    EXPECT_EQ(p.start, glm::vec3(1, 2, 3));
    EXPECT_EQ(p.angles, glm::vec3(0));
    particles.Clear();
    const auto reused = particles.CreateChecked(Type, {4, 5, 6}, {}, {}, 3, 2, std::cref(clock), random, 0, trig);
    ASSERT_TRUE(reused.HasValue());
    ASSERT_TRUE(reused.Value());
    EXPECT_EQ(*reused.Value(), 0u);
    EXPECT_EQ(p.field38, 42u);
    EXPECT_EQ(p.rotationFactor, 13);
    EXPECT_EQ(p.field64, glm::vec2(14, 15));
}
TEST(Particle60f3Test, LegacyAndUnaccreditedCreationSubtypesRemainRejected)
{
    World::Particles particles;
    Render::Models::CrtRandom random(7);
    const float clock = 0;
    [[maybe_unused]] Trigonometry trig;
    const auto legacy = particles.CreateChecked(Type, {}, {}, {}, 3, 1, clock, random, 0);
    ASSERT_FALSE(legacy.HasValue());
    EXPECT_EQ(legacy.Error(), World::ParticleError::UnsupportedType);
    // La /4 de la antorcha de Blood Castle y la /10 del calco de Crywolf ya están acreditadas; la /5 comparte el alta
    // de la /4 pero no su paso, y la /6, que nace como la /3 (0x542eeb), sigue sin acreditar.
    for (const int subtype : {0, 5, 6})
    {
        const auto result = particles.CreateChecked(Type, {}, {}, {}, subtype, 1, std::cref(clock), random, 0, trig);
        ASSERT_FALSE(result.HasValue());
        EXPECT_EQ(result.Error(), World::ParticleError::UnsupportedSubtype);
    }
    EXPECT_EQ(random.State(), 7u);
    EXPECT_FALSE(particles.Any());
}
TEST(Particle60f3Test, LegacyStepDoesNotAcquireNewAdmission)
{
    World::Particles particles;
    auto& p = particles.All()[0];
    p.alive = true;
    p.type = Type;
    p.subtype = 3;
    p.life = 60;
    Render::Models::CrtRandom random(7);
    const auto result = World::StepParticlesChecked(particles, random, nullptr);
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error(), World::ParticleError::UnsupportedType);
    EXPECT_EQ(p.life, 59);
    EXPECT_EQ(random.State(), 3289278137u);
}

constexpr std::int32_t LateSelector = 0x12345678;
class Library final : public Scenes::IModelLibrary
{
public:
    mutable std::vector<std::uint32_t> queries;
    std::uint8_t colorFlag = 3;
    const Data::Models::Model* Find(std::uint16_t) const override
    {
        return nullptr;
    }
    const Render::Models::Skeleton* SkeletonOf(std::uint16_t) const override
    {
        return nullptr;
    }
    std::span<const Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return {};
    }
    std::optional<Scenes::TextureMetadata> TextureMetadataOf(std::uint32_t selector) const override
    {
        queries.push_back(selector);
        if (selector != Type && selector != LateSelector)
            return std::nullopt;
        Scenes::TextureMetadata result(Data::Textures::Texture{});
        result.size = {8, 16};
        result.components = selector == Type ? 3 : colorFlag;
        return result;
    }
};
class Backend final : public Render::IParticleDrawBackend
{
public:
    Backend(World::Particle& particle, Library& library, Render::Models::OriginalMatrix& view)
        : p(particle), lib(library), matrix(view)
    {
    }
    World::Particle& p;
    Library& lib;
    Render::Models::OriginalMatrix& matrix;
    std::vector<int> events;
    std::vector<glm::vec3> vertices;
    std::vector<glm::vec2> uvs;
    Render::Color color{};
    bool failBegin = false;
    Result Additive() override
    {
        events.push_back(1);
        p.texture = LateSelector;
        return {};
    }
    Result AlphaInheritDepth() override
    {
        ADD_FAILURE();
        return {};
    }
    Result SubtractivePrefix() override
    {
        ADD_FAILURE();
        return {};
    }
    Result Helper6208f0() override
    {
        ADD_FAILURE();
        return {};
    }
    Result Helper6208b0() override
    {
        ADD_FAILURE();
        return {};
    }
    Result SubtractiveTail() override
    {
        ADD_FAILURE();
        return {};
    }
    Result BlendedPrefix() override
    {
        ADD_FAILURE();
        return {};
    }
    Result BlendedTail() override
    {
        ADD_FAILURE();
        return {};
    }
    Result Type620850() override
    {
        ADD_FAILURE();
        return {};
    }
    Result Type620870() override
    {
        ADD_FAILURE();
        return {};
    }
    Result BindTexture(std::int32_t selector) override
    {
        events.push_back(2);
        EXPECT_EQ(selector, LateSelector);
        p.position = {100, 200, 300};
        matrix[3] = 10;
        return {};
    }
    Result Begin() override
    {
        events.push_back(3);
        p.light = {.25f, .5f, .75f};
        lib.colorFlag = 4;
        if (failBegin)
            return Core::Unexpected(std::make_error_code(std::errc::io_error));
        return {};
    }
    Result Color3(const glm::vec3&) override
    {
        ADD_FAILURE();
        return {};
    }
    Result Color4(const Render::Color& value) override
    {
        events.push_back(4);
        color = value;
        return {};
    }
    Result TexCoord(glm::vec2 uv) override
    {
        events.push_back(5);
        uvs.push_back(uv);
        return {};
    }
    Result Vertex(const glm::vec3& position) override
    {
        events.push_back(6);
        vertices.push_back(position);
        return {};
    }
    Result End() override
    {
        events.push_back(7);
        return {};
    }
};
TEST(Particle60f3Test, DrawConsumesPositionViewAndColorAfterTheirServiceBoundaries)
{
    World::Particles particles;
    auto& p = particles.All()[0];
    p.alive = true;
    p.type = Type;
    p.texture = Type;
    p.subtype = 3;
    p.life = 59;
    p.scale = 2;
    p.position = {1, 2, 3};
    p.light = {1, 1, 1};
    Render::Models::OriginalMatrix view{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    Library lib;
    Backend backend(p, lib, view);
    Scenes::ParticleDrawServices services(std::cref(lib), std::ref(backend), std::cref(view));
    const float clock = 0;
    [[maybe_unused]] Trigonometry trig;
    ASSERT_TRUE(World::DrawParticlesChecked(particles, 0, std::cref(clock), services).HasValue());
    EXPECT_EQ(lib.queries, (std::vector<std::uint32_t>{Type, Type, LateSelector}));
    EXPECT_EQ(backend.events, (std::vector<int>{1, 2, 3, 4, 5, 6, 5, 6, 5, 6, 5, 6, 7}));
    ASSERT_EQ(backend.vertices.size(), 4u);
    EXPECT_EQ(backend.vertices[0], glm::vec3(102, 184, 300));
    EXPECT_EQ(backend.vertices[2], glm::vec3(118, 216, 300));
    EXPECT_EQ(backend.uvs, (std::vector<glm::vec2>{{0, 1}, {1, 1}, {1, 0}, {0, 0}}));
    EXPECT_EQ(backend.color.red, .25f);
    EXPECT_EQ(backend.color.green, .5f);
    EXPECT_EQ(backend.color.blue, .75f);
    EXPECT_EQ(backend.color.alpha, .25f);
}
TEST(Particle60f3Test, BeginErrorPreservesPrefixWithoutColorQuadOrEnd)
{
    World::Particles particles;
    auto& p = particles.All()[0];
    p.alive = true;
    p.type = Type;
    p.texture = Type;
    p.subtype = 3;
    p.life = 59;
    p.scale = 2;
    Render::Models::OriginalMatrix view{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    Library lib;
    Backend backend(p, lib, view);
    backend.failBegin = true;
    Scenes::ParticleDrawServices services(std::cref(lib), std::ref(backend), std::cref(view));
    const float clock = 0;
    [[maybe_unused]] Trigonometry trig;
    const auto result = World::DrawParticlesChecked(particles, 0, std::cref(clock), services);
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error().error.phase, World::ParticleDrawingPhase::Begin);
    EXPECT_EQ(result.Error().error.backendError, std::make_error_code(std::errc::io_error));
    EXPECT_EQ(lib.queries, (std::vector<std::uint32_t>{Type, Type}));
    EXPECT_EQ(backend.events, (std::vector<int>{1, 2, 3}));
    EXPECT_TRUE(backend.vertices.empty());
    EXPECT_EQ(p.position, glm::vec3(100, 200, 300));
    EXPECT_EQ(p.texture, LateSelector);
}

TEST(Particle60f3Test, TrigErrorsPreserveTheirExactPrefixAndStopThePass)
{
    for (const bool failSine : {true, false})
    {
        World::Particles particles;
        auto& p = particles.All()[0];
        p.alive = true;
        p.type = Type;
        p.subtype = 3;
        p.life = 1;
        p.moves = false;
        p.position = {9, 8, 7};
        p.start = {-20, 100, 0};
        p.scale = 3;
        p.light = {1, 2, 3};
        particles.All()[1] = p;
        Trigonometry trig;
        trig.failSine = failSine;
        trig.failCosine = !failSine;
        trig.sineProduct = 20 + 0x1p-48;
        trig.observed = &p;
        Render::Models::CrtRandom random(7);
        const float clock = 0;
        const auto result = World::StepParticlesChecked(particles, random, std::cref(clock), nullptr, trig);
        ASSERT_FALSE(result.HasValue());
        EXPECT_EQ(result.Error(), World::ParticleError::UnrepresentableTrigonometry);
        EXPECT_FALSE(p.alive);
        EXPECT_EQ(p.life, 0);
        EXPECT_EQ(p.position.x, failSine ? 9.0f : 0x1p-48f);
        EXPECT_EQ(p.position.y, 8);
        EXPECT_EQ(p.position.z, 7);
        EXPECT_EQ(p.scale, 3);
        EXPECT_EQ(p.light, glm::vec3(1, 2, 3));
        EXPECT_EQ(particles.All()[1].life, 1);
        EXPECT_TRUE(particles.All()[1].alive);
        EXPECT_EQ(random.State(), 3289278137u);
        EXPECT_EQ(trig.calls, failSine ? std::vector<int>{1} : (std::vector<int>{1, 2}));
    }
}
TEST(Particle60f3Test, Product53IsAddedBeforeTheSinglePositionStore)
{
    World::Particles particles;
    auto& p = particles.All()[0];
    p.alive = true;
    p.type = Type;
    p.subtype = 3;
    p.life = 2;
    p.moves = false;
    p.position = {9, 8, 7};
    p.start = {-20, 100, 0};
    p.scale = 3;
    Trigonometry trig;
    trig.argument = 0x1.99999ap-4;
    trig.sineProduct = 20 + 0x1p-48;
    trig.observed = &p;
    Render::Models::CrtRandom random(7);
    const float clock = 0;
    ASSERT_TRUE(World::StepParticlesChecked(particles, random, std::cref(clock), nullptr, trig).HasValue());
    EXPECT_EQ(p.position.x, 0x1p-48f);
    EXPECT_EQ(p.position.y, 60);
    EXPECT_EQ(trig.calls, (std::vector<int>{1, 2}));
    EXPECT_EQ(p.start, glm::vec3(-20, 100, 0));
}

TEST(Particle60f3Test, CompleteCreateStepDrawChainKeepsCommonMovementAndEarlyDrawGate)
{
    World::Particles particles;
    Render::Models::CrtRandom random(7);
    const float clock = 0;
    Trigonometry trig;
    trig.argument = -0x1.b9999a08p+2;
    trig.sineProduct = 20 + 0x1p-48;
    const auto created =
        particles.CreateChecked(Type, {-20, 100, 4}, {}, {1, 2, 3}, 3, 2, std::cref(clock), random, 0, trig);
    ASSERT_TRUE(created.HasValue());
    ASSERT_TRUE(created.Value());
    auto& p = particles.All()[*created.Value()];
    Services services(p);
    services.expectedWidth = 16.399999618530273f;
    services.expectedHeight = 32.79999923706055f;
    ASSERT_TRUE(World::DrawParticlesChecked(particles, 0, std::cref(clock), services).HasValue());
    EXPECT_EQ(services.trace, (std::vector<int>{1, 2, 3}));
    ASSERT_TRUE(World::StepParticlesChecked(particles, random, std::cref(clock), nullptr, trig).HasValue());
    EXPECT_EQ(p.life, 59);
    EXPECT_EQ(p.position.x, 0x1p-48f);
    EXPECT_EQ(p.position.y, 60);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(p.position.z), 0x40ee147bu);
    EXPECT_EQ(p.light, glm::vec3(1, 2, 3));
    EXPECT_EQ(random.State(), 2574545229u);
    services.trace.clear();
    services.expectedWidth = 16.38399887084961f;
    services.expectedHeight = 32.76799774169922f;
    ASSERT_TRUE(World::DrawParticlesChecked(particles, 0, std::cref(clock), services).HasValue());
    EXPECT_EQ(services.trace, (std::vector<int>{1, 2, 3, 4}));
    EXPECT_EQ(trig.calls, (std::vector<int>{1, 2}));
}
TEST(Particle60f3Test, TypedOverloadRetainsExistingCreationAndStepWithoutCallingTrig)
{
    World::Particles particles;
    Render::Models::CrtRandom random(7);
    const float clock = 0;
    Trigonometry trig;
    trig.failSine = true;
    trig.failCosine = true;
    const auto created = particles.CreateChecked(0x68, {}, {}, {}, 0, 1, std::cref(clock), random, 0, trig);
    ASSERT_TRUE(created.HasValue());
    ASSERT_TRUE(created.Value());
    const std::int32_t map = 34;
    ASSERT_TRUE(World::StepParticlesChecked(particles, random, std::cref(clock), &map, trig).HasValue());
    EXPECT_EQ(particles.All()[*created.Value()].life, 19);
    EXPECT_TRUE(trig.calls.empty());
}

} // namespace
