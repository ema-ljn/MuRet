#include "Scenes/HighModelEffects.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalAngles.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldState.h"
#include "World/GameModelEffects.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/TerrainLighting.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

// Las familias de los tipos altos de la CL11 que viven en su propio archivo y entran por CreateHighModelEffect y
// StepHighModelEffect: el 0x114 de Scenes/WaveEffects y el 0x11b de Scenes/RingEffects. 06, "Los efectos con modelo de
// los tipos altos".
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorState;
using Client::Scenes::CreateHighModelEffect;
using Client::Scenes::HighEffectContext;
using Client::Scenes::HighEffectStepContext;
using Client::Scenes::WorldState;
using Client::World::ModelEffect;

constexpr std::uint32_t Seed = 0x2468ace1u;

// Cuántos rand() sacó `random` desde la semilla.
int DrawsSince(std::uint32_t seed, const CrtRandom& random)
{
    CrtRandom probe(seed);
    for (int draws = 0; draws < 100000; ++draws)
    {
        if (probe.State() == random.State())
            return draws;
        static_cast<void>(probe.Next());
    }
    return -1;
}

struct FamilyWorld
{
    HighEffectContext Context()
    {
        return {.world = *world, .random = random, .worldClock = clock};
    }

    HighEffectStepContext StepContext()
    {
        return {.world = *world, .random = random, .services = services, .worldClock = clock};
    }

    ModelEffect& Effect(std::size_t slot)
    {
        return world->Map().effects.Effects()[slot];
    }

    void FillTheEffects()
    {
        for (auto& effect : world->Map().effects.Effects())
            effect.alive = true;
    }

    void FillTheJoints()
    {
        for (auto& joint : world->DrawTail().joints)
            joint.alive = true;
    }

    void FillTheParticles()
    {
        for (auto& particle : world->Particles().All())
            particle.alive = true;
    }

    [[nodiscard]] int Draws() const
    {
        return DrawsSince(Seed, random);
    }

    std::unique_ptr<WorldState> world = std::make_unique<WorldState>();
    std::unique_ptr<Client::World::TerrainLighting> lighting = std::make_unique<Client::World::TerrainLighting>();
    CrtRandom random{Seed};
    float clock = 1234.5f;
    Client::World::ModelEffectStepServices services{.effects = &world->Map().effects, .lighting = lighting.get()};
};

// Los rand() de una partícula: la crea en un pool aparte con `replay`, que avanza como el original.
void ReplayParticle(CrtRandom& replay, std::int32_t type, std::int32_t subtype, float scale)
{
    auto twin = std::make_unique<Client::World::Particles>();
    const auto made =
        twin->CreateChecked(type, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(1.0f), subtype, scale, 0.0f, replay);
    ASSERT_TRUE(made.HasValue());
}

std::ptrdiff_t AliveParticles(WorldState& world, std::int32_t type, std::int32_t subtype)
{
    return std::ranges::count_if(world.Particles().All(), [type, subtype](const Client::World::Particle& particle)
                                 { return particle.alive && particle.type == type && particle.subtype == subtype; });
}

template <typename Result> bool HasError(const Result& result, std::string_view address)
{
    return !result.HasValue() && result.Error().starts_with(address);
}

// 0x51c186: el 0x114/0 saca un rand() (la escala) antes de cada uno de sus sesenta joints 0x5ac/0, que no son de MuRet:
// con lugar, corta en el primero después de ese rand(); con los joints llenos, sesenta rand() y lo común.
TEST(HighEffectFamiliesTest, TheFlareWaveDrawsOneScaleForEachOfItsSixtyJoints)
{
    for (const bool full : {false, true})
    {
        SCOPED_TRACE(full);
        FamilyWorld tested;
        if (full)
            tested.FillTheJoints();
        const auto made = CreateHighModelEffect(
            {.type = 0x114, .position = {100.0f, 200.0f, 300.0f}, .angles = {10.0f, 20.0f, 30.0f}}, tested.Context());
        const ModelEffect& wave = tested.Effect(0);
        EXPECT_TRUE(wave.alive);
        EXPECT_EQ(wave.life, 20);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(wave.turnDirection), 0x3dcccccdu);
        EXPECT_EQ(wave.transform.angles, glm::vec3(0.0f));
        EXPECT_EQ(wave.transform.position, (glm::vec3{100.0f, 200.0f, 350.0f}));
        EXPECT_EQ(wave.start, (glm::vec3{100.0f, 200.0f, 300.0f}));
        EXPECT_EQ(wave.objectState.blendMesh, -2);
        if (full)
        {
            ASSERT_TRUE(made.HasValue()) << made.Error();
            EXPECT_EQ(tested.Draws(), 60);
        }
        else
        {
            EXPECT_TRUE(HasError(made, "0x51c1b8")) << (made.HasValue() ? std::string() : made.Error());
            EXPECT_EQ(tested.Draws(), 1);
        }
    }
}

// 0x51db73: otro subtipo del 0x114 se queda con lo común, sin rand(); con la tabla llena, nada.
TEST(HighEffectFamiliesTest, AnotherWaveSubtypeKeepsOnlyTheCommonPart)
{
    FamilyWorld tested;
    const auto made =
        CreateHighModelEffect({.type = 0x114, .position = {1.0f, 2.0f, 3.0f}, .subtype = 9}, tested.Context());
    ASSERT_TRUE(made.HasValue()) << made.Error();
    EXPECT_EQ(made.Value(), 0u);
    EXPECT_EQ(tested.Effect(0).life, 20);
    EXPECT_EQ(tested.Effect(0).transform.position, (glm::vec3{1.0f, 2.0f, 53.0f}));
    EXPECT_EQ(tested.Draws(), 0);

    FamilyWorld full;
    full.FillTheEffects();
    const auto none = CreateHighModelEffect({.type = 0x114, .subtype = 1}, full.Context());
    ASSERT_TRUE(none.HasValue()) << none.Error();
    EXPECT_EQ(none.Value(), std::nullopt);
    EXPECT_EQ(full.Draws(), 0);
}
// 0x51c7a7..0x51c883: el 0x11b/1 de 0x439b20, sin rand(): vida 49, escala 2, los puntos y la partida en la posición y
// los joints 0x618/0xa y /0xb con el efecto de dueño (ancho 30, cinco puntos); con los joints llenos, sin ellos.
TEST(HighEffectFamiliesTest, TheRingLeavesItsTwoBeamsOnItsOwnPoints)
{
    for (const bool full : {false, true})
    {
        SCOPED_TRACE(full);
        FamilyWorld tested;
        if (full)
            tested.FillTheJoints();
        ActorState owner;
        const auto made = CreateHighModelEffect({.type = 0x11b,
                                                 .position = {100.0f, 200.0f, 300.0f},
                                                 .angles = {0.0f, 0.0f, 120.0f},
                                                 .subtype = 1,
                                                 .owner = &owner},
                                                tested.Context());
        ASSERT_TRUE(made.HasValue()) << made.Error();
        EXPECT_EQ(tested.Draws(), 0);
        const ModelEffect& ring = tested.Effect(0);
        EXPECT_EQ(ring.life, 49);
        EXPECT_EQ(ring.transform.scale, 2.0f);
        EXPECT_EQ(ring.objectState.blendMesh, -2);
        EXPECT_EQ(ring.objectState.light, 0.5f);
        EXPECT_EQ(ring.velocity, (glm::vec3{0.0f, -45.0f, 0.0f}));
        for (const glm::vec3& point : {ring.start, ring.siegeTarget, ring.point40, ring.point4c})
            EXPECT_EQ(point, (glm::vec3{100.0f, 200.0f, 300.0f}));
        const auto& joints = tested.world->DrawTail().joints;
        if (full)
            continue;
        for (std::size_t index = 0; index < 2; ++index)
        {
            const auto& beam = joints[index];
            ASSERT_TRUE(beam.alive);
            EXPECT_EQ(beam.type, 0x618);
            EXPECT_EQ(beam.subtype, index == 0 ? 0xa : 0xb);
            EXPECT_EQ(beam.ownerContext, &ring);
            EXPECT_EQ(beam.ownerKind, Client::Scenes::JointOwnerKind::ModelEffect);
            EXPECT_EQ(beam.width, 30.0f);
            EXPECT_EQ(beam.maximumCount, 5);
            EXPECT_EQ(beam.position, (glm::vec3{100.0f, 200.0f, 300.0f}));
        }
        EXPECT_FALSE(joints[2].alive);
    }
}

// 0x526507..0x5266e5: el paso del 0x11b/1 con dueño. Con 10 o más de vida, a más de 400 sobre el terreno baja (la x en
// 5, la copiada en 0); los dos rand() de la sacudida y, con lugar, los de la partícula 0x5d9/5.
TEST(HighEffectFamiliesTest, TheRingFollowsItsOwnerAndDivesFromHigh)
{
    for (const bool full : {false, true})
    {
        SCOPED_TRACE(full);
        FamilyWorld tested;
        if (full)
            tested.FillTheParticles();
        tested.services.terrainHeight = [](float, float) { return 0.0; };
        ActorState owner;
        owner.position = {1100.0f, 1000.0f, 50.0f};
        tested.FillTheJoints();
        ASSERT_TRUE(CreateHighModelEffect(
                        {.type = 0x11b, .position = {1000.0f, 1000.0f, 500.0f}, .subtype = 1, .owner = &owner},
                        tested.Context())
                        .HasValue());
        ModelEffect& ring = tested.Effect(0);
        ring.life = 20;
        const auto stepped = Client::Scenes::StepHighModelEffect(ring, nullptr, tested.StepContext());
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        CrtRandom replay(Seed);
        static_cast<void>(replay.Next());
        static_cast<void>(replay.Next());
        const int yaw = (replay.Next() & 31) - 16;
        if (!full)
            ReplayParticle(replay, 0x5d9, 5, std::bit_cast<float>(0x3f99999au));
        EXPECT_EQ(tested.random.State(), replay.State());
        EXPECT_EQ(ring.siegeTarget, (glm::vec3{1000.0f, 1000.0f, 500.0f}));
        EXPECT_EQ(ring.start, (glm::vec3{1100.0f, 1000.0f, 250.0f}));
        glm::vec3 angles{};
        static_cast<void>(Client::Render::Models::MoveToward({1000.0f, 1000.0f, 500.0f}, angles, ring.start, 10.0f));
        const float copiedYaw = static_cast<float>(yaw * static_cast<double>(0.8f));
        EXPECT_EQ(ring.transform.angles.x, 5.0f);
        EXPECT_EQ(ring.copiedAngles.x, 0.0f);
        EXPECT_EQ(ring.transform.angles.z, static_cast<float>(static_cast<double>(copiedYaw) + angles.z));
        EXPECT_EQ(ring.copiedAngles.z, static_cast<float>(static_cast<double>(copiedYaw) * static_cast<double>(0.8f)));
        EXPECT_EQ(ring.life, 19);
        EXPECT_EQ(AliveParticles(*tested.world, 0x5d9, 5), full ? 0 : 1);
    }
}

// Sin la altura del terreno, el paso del 0x11b/1 con 10 o más de vida corta después de sus dos rand(); con menos, no la
// pide: el alpha por 0,909f, +0x68 por 0,833f y la x 5 más.
TEST(HighEffectFamiliesTest, TheRingAsksForTheGroundOnlyWhileItIsYoung)
{
    for (const int life : {10, 9})
    {
        SCOPED_TRACE(life);
        FamilyWorld tested;
        tested.FillTheParticles();
        tested.FillTheJoints();
        ActorState owner;
        ASSERT_TRUE(CreateHighModelEffect({.type = 0x11b, .subtype = 1, .owner = &owner}, tested.Context()).HasValue());
        ModelEffect& ring = tested.Effect(0);
        ring.life = life;
        const auto stepped = Client::Scenes::StepHighModelEffect(ring, nullptr, tested.StepContext());
        EXPECT_EQ(tested.Draws(), 3);
        if (life == 10)
        {
            EXPECT_TRUE(HasError(stepped, "0x526650"));
            continue;
        }
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(ring.alpha, std::bit_cast<float>(0x3f68ba2eu));
        EXPECT_EQ(ring.objectState.light,
                  static_cast<float>(0.5 * static_cast<double>(std::bit_cast<float>(0x3f555555u))));
    }
}

// 0x51c789: el 0x11b/0 vive 1 con escala 2,3f; el del joint 0x613/0x13 (CreateTheRingSpark) es el mismo.
TEST(HighEffectFamiliesTest, TheRingSparkLivesOneStep)
{
    FamilyWorld tested;
    ASSERT_TRUE(CreateHighModelEffect({.type = 0x11b, .position = {1.0f, 2.0f, 3.0f}}, tested.Context()).HasValue());
    EXPECT_EQ(tested.Effect(0).life, 1);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(tested.Effect(0).transform.scale), 0x40133333u);
    EXPECT_EQ(tested.Draws(), 0);
}
} // namespace
