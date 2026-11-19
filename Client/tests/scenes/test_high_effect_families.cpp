#include "Scenes/HighModelEffects.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/OriginalAngles.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"
#include "Scenes/ActorState.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/GameParticles.h"
#include "Scenes/HighEffectEmitters.h"
#include "Scenes/KundunEffects.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldState.h"
#include "World/GameModelEffects.h"
#include "World/ModelEffects.h"
#include "World/ParticleOwner.h"
#include "World/Particles.h"
#include "World/PortableParticleTrigonometry.h"
#include "World/TerrainLighting.h"
#include "World/WorldObjectStepServices.h"

#include "support/SyntheticModels.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Las familias de los tipos altos de la CL11 que viven en su propio archivo y entran por CreateHighModelEffect y
// StepHighModelEffect: el 0x114 de Scenes/WaveEffects, el 0x11b de Scenes/RingEffects y el 0x13d..0x13f de
// Scenes/MeteorEffects. 06, "Los efectos con modelo de los tipos altos".
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

// La partícula que pide un paso, repetida en un pool gemelo con `replay` (el mismo CrtRandom del original).
Client::World::Particle TwinParticle(CrtRandom& replay, std::int32_t type, std::int32_t subtype,
                                     const glm::vec3& position, const glm::vec3& angles, const glm::vec3& light,
                                     float scale)
{
    auto twin = std::make_unique<Client::World::Particles>();
    const auto made = twin->CreateChecked(type, position, angles, light, subtype, scale, 0.0f, replay);
    EXPECT_TRUE(made.HasValue() && made.Value().has_value());
    return twin->All()[0];
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

// 0x51c186: el 0x114/0 saca un rand() (la escala) antes de cada uno de sus sesenta joints 0x5ac/0 (FlareJoints.cpp),
// que sacan tres más al nacer (0x530cbc..0x530cfe); con los joints llenos, sesenta rand() y lo común.
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
        ASSERT_TRUE(made.HasValue()) << made.Error();
        EXPECT_EQ(tested.Draws(), full ? 60 : 240);
        if (!full)
        {
            const auto& joints = tested.world->DrawTail().joints;
            EXPECT_EQ(std::ranges::count_if(joints, [](const Client::Scenes::JointState& joint)
                                            { return joint.alive && joint.type == 0x5ac && joint.subtype == 0; }),
                      60);
            CrtRandom replay{Seed};
            const auto width = static_cast<float>(replay.Next() % 40 + 70);
            EXPECT_EQ(joints[0].width, width);
            EXPECT_EQ(joints[0].position, (glm::vec3{100.0f, 200.0f, 350.0f}));
            EXPECT_EQ(joints[0].ownerContext, nullptr);
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
        // El primer rand() del paso es la luz local; la sacudida, (8845 & 31) − 16 = −3 de cabeceo y (11882 & 31) − 16
        // = −6 de giro. 0x4e9810 gira también el cabeceo hacia la partida, (0, 0, 200) desde el origen: 0x4e93f0(z, 0,
        // 200, 0) da 90, 360 − 90 = 270 y 0x4e9560 lo lleva de 0 a 350 de a 10 (0x4e9647); la z, de 0 a 350 hacia 180.
        // La copiada x en −3 · 0,2f se suma (349,4f, por encima de 80: sin los 5 de 0x52663d) y se frena por 0,6f; la
        // copiada z en −6 · 0,8f se suma (345,2f) y se frena por 0,8f.
        EXPECT_EQ(std::bit_cast<std::uint32_t>(ring.transform.angles.x), 0x43aeb333u);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(ring.transform.angles.z), 0x43ac999au);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(ring.copiedAngles.x), 0xbeb851ecu);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(ring.copiedAngles.z), 0xc075c290u);
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

// 0x525f6a..0x52603d: con vida 8, el 0x13f suelta dieciséis joints 0x60f/0x18 sin dueño, de ancho 90, en su posición
// con la z 100 más, la x más rand() % 600 − 200 y la y más rand() % 600 − 400, con los ángulos (−10, 0, i · 10); con
// los joints llenos, sólo esos treinta y dos rand().
TEST(HighEffectFamiliesTest, TheMeteorCallRainsSixteenDropsAtLifeEight)
{
    for (const bool full : {false, true})
    {
        SCOPED_TRACE(full);
        FamilyWorld tested;
        if (full)
            tested.FillTheJoints();
        const glm::vec3 origin{1000.0f, 2000.0f, 300.0f};
        ASSERT_TRUE(CreateHighModelEffect({.type = 0x13f, .position = origin}, tested.Context()).HasValue());
        ModelEffect& call = tested.Effect(0);
        EXPECT_EQ(call.life, 20);
        call.life = 8;
        const auto stepped = Client::Scenes::StepHighModelEffect(call, nullptr, tested.StepContext());
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        FamilyWorld twin;
        static_cast<void>(twin.random.Next()); // la luz local (0x5208c5)
        const auto& joints = tested.world->DrawTail().joints;
        for (int drop = 0; drop < 16; ++drop)
        {
            const int x = twin.random.Next() % 600 - 200;
            const int y = twin.random.Next() % 600 - 400;
            const glm::vec3 point{static_cast<float>(static_cast<double>(x) + 1000.0),
                                  static_cast<float>(static_cast<double>(y) + 2000.0), 400.0f};
            const glm::vec3 angles{-10.0f, 0.0f, static_cast<float>(drop * 10.0)};
            if (full)
                continue;
            ASSERT_TRUE(Client::Scenes::CreateHighJoint(*twin.world, twin.random, 0x60f, point, point, angles, 0x18,
                                                        nullptr, 90.0f, 0)
                            .HasValue());
            const auto& rain = joints[static_cast<std::size_t>(drop)];
            ASSERT_TRUE(rain.alive);
            EXPECT_EQ(rain.type, 0x60f);
            EXPECT_EQ(rain.subtype, 0x18);
            EXPECT_EQ(rain.width, 90.0f);
            EXPECT_EQ(rain.targetPosition, point);
            EXPECT_EQ(rain.ownerContext, nullptr);
        }
        EXPECT_EQ(tested.random.State(), twin.random.State());
        EXPECT_EQ(call.life, 7);
        EXPECT_TRUE(call.alive);
    }
}

// 0x526042..0x5260b8: con vida 2, el 0x13f saca el giro (rand() % 360) y deja el 0x13d (0x526090) y el 0x13e (0x5260ad)
// en su posición, con su luz y él de dueño (vida 120, escala 0,8f), y se apaga; con la tabla llena, sólo el giro.
TEST(HighEffectFamiliesTest, TheMeteorCallLeavesTheRockAndItsShellAtLifeTwo)
{
    for (const bool full : {false, true})
    {
        SCOPED_TRACE(full);
        FamilyWorld tested;
        const glm::vec3 origin{1000.0f, 2000.0f, 300.0f};
        const glm::vec3 light{0.25f, 0.5f, 0.75f};
        ASSERT_TRUE(
            CreateHighModelEffect({.type = 0x13f, .position = origin, .light = light}, tested.Context()).HasValue());
        if (full)
            tested.FillTheEffects();
        ModelEffect& call = tested.Effect(0);
        call.life = 2;
        const auto stepped = Client::Scenes::StepHighModelEffect(call, nullptr, tested.StepContext());
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        CrtRandom replay(Seed);
        static_cast<void>(replay.Next());
        const int turn = replay.Next() % 360;
        EXPECT_EQ(tested.random.State(), replay.State());
        EXPECT_FALSE(call.alive);
        EXPECT_EQ(call.life, 1);
        if (full)
            continue;
        for (const auto& [slot, type] :
             {std::pair{std::size_t{1}, std::int16_t{0x13d}}, std::pair{std::size_t{2}, std::int16_t{0x13e}}})
        {
            const ModelEffect& left = tested.Effect(slot);
            ASSERT_TRUE(left.alive);
            EXPECT_EQ(left.type, type);
            EXPECT_EQ(left.life, 120);
            EXPECT_EQ(std::bit_cast<std::uint32_t>(left.transform.scale), 0x3f4ccccdu);
            EXPECT_EQ(left.transform.position, origin);
            EXPECT_EQ(left.transform.angles, (glm::vec3{0.0f, 0.0f, static_cast<float>(turn)}));
            EXPECT_EQ(left.light, light);
            EXPECT_EQ(left.ownerContext, &call);
        }
    }
}

// Una semilla que da `coin` en el tercer rand() (0x52612c): la luz local y el temblor van antes.
std::uint32_t SeedWithCoin(int coin)
{
    for (std::uint32_t seed = 1;; ++seed)
    {
        CrtRandom probe(seed);
        static_cast<void>(probe.Next());
        static_cast<void>(probe.Next());
        if ((probe.Next() & 1) == coin)
            return seed;
    }
}

// 0x5260bd..0x526207, el 0x13d sin BMD: la luz (v, v, v + 0,1) de radio 10 con v = FSIN(vida · π/120) · −0,5, el
// temblor ((rand() & 3) − 2) · 0,1f y, con rand() & 1 en 0, la piedrita 0xc8 o 0xc9 a rand() % 150 + 300 girada
// rand() % 360 en z, con sus cinco rand().
TEST(HighEffectFamiliesTest, TheMeteorPulsesShakesAndDropsAStone)
{
    for (const int coin : {0, 1})
    {
        SCOPED_TRACE(coin);
        const std::uint32_t seed = SeedWithCoin(coin);
        FamilyWorld tested;
        tested.random = CrtRandom(seed);
        float shake = 99.0f;
        tested.services.cameraShake = &shake;
        const glm::vec3 origin{12800.0f, 12800.0f, 100.0f};
        const glm::vec3 angles{0.0f, 0.0f, 30.0f};
        const glm::vec3 light{0.5f, 0.5f, 0.5f};
        ASSERT_TRUE(CreateHighModelEffect({.type = 0x13d, .position = origin, .angles = angles, .light = light},
                                          tested.Context())
                        .HasValue());
        ModelEffect& rock = tested.Effect(0);
        rock.life = 40;
        const auto stepped = Client::Scenes::StepHighModelEffect(rock, nullptr, tested.StepContext());
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        CrtRandom replay(seed);
        static_cast<void>(replay.Next());
        const int shaken = (replay.Next() & 3) - 2;
        EXPECT_EQ(shake, static_cast<float>(shaken * static_cast<double>(std::bit_cast<float>(0x3dcccccdu))));
        ASSERT_EQ(replay.Next() & 1, coin);
        if (coin == 0)
        {
            const int reach = replay.Next() % 150;
            const int turn = replay.Next() % 360;
            const auto type = static_cast<std::int16_t>(0xc8 + (replay.Next() & 1));
            const glm::vec3 offset = Client::Render::Models::OriginalVectorRotate(
                {0.0f, static_cast<float>(reach + 300.0), 0.0f},
                Client::Render::Models::OriginalAngleMatrix({0.0f, 0.0f, static_cast<float>(turn)}));
            const glm::vec3 point{static_cast<float>(static_cast<double>(offset.x) + origin.x),
                                  static_cast<float>(static_cast<double>(offset.y) + origin.y),
                                  static_cast<float>(static_cast<double>(offset.z) + origin.z)};
            auto twin = std::make_unique<Client::World::ModelEffects>();
            ASSERT_TRUE(twin->CreateStoneChip(type, point, angles, light, replay).has_value());
            const ModelEffect& stone = tested.Effect(1);
            ASSERT_TRUE(stone.alive);
            EXPECT_EQ(stone.type, type);
            EXPECT_EQ(stone.transform.position, point);
        }
        else
        {
            EXPECT_FALSE(tested.Effect(1).alive);
        }
        EXPECT_EQ(tested.random.State(), replay.State());
        const auto wave = Client::Render::Models::Detail::SineDoubleProductPC53(40.0 * 0.02617993950843811, -0.5);
        ASSERT_TRUE(wave.has_value());
        const glm::vec3 color{static_cast<float>(*wave), static_cast<float>(*wave),
                              static_cast<float>(*wave + 0.10000000149011612)};
        auto lit = std::make_unique<Client::World::TerrainLighting>();
        ASSERT_TRUE(Client::World::AddDynamicLight(lit.get(), origin, color, 10, "prueba").HasValue());
        EXPECT_TRUE(lit->cells == tested.lighting->cells);
        EXPECT_EQ(rock.life, 39);
    }
}

// Sin la luz dinámica el 0x13d corta antes del temblor (0x5260fe); sin el temblor, después de su rand() (0x526126).
TEST(HighEffectFamiliesTest, TheMeteorAsksForTheLightAndTheShake)
{
    for (const bool lit : {false, true})
    {
        SCOPED_TRACE(lit);
        FamilyWorld tested;
        if (!lit)
            tested.services.lighting = nullptr;
        ASSERT_TRUE(CreateHighModelEffect({.type = 0x13d}, tested.Context()).HasValue());
        const auto stepped = Client::Scenes::StepHighModelEffect(tested.Effect(0), nullptr, tested.StepContext());
        EXPECT_TRUE(HasError(stepped, lit ? "0x526126" : "0x5260fe"));
        EXPECT_EQ(tested.Draws(), lit ? 2 : 1);
    }
}

// Los rand() del 0x13d antes de sus huesos: la luz local, el temblor, la moneda y, con 0, los tres de la piedrita y
// sus cinco.
void ReplayTheMeteorPrefix(CrtRandom& replay)
{
    static_cast<void>(replay.Next());
    static_cast<void>(replay.Next());
    if ((replay.Next() & 1) != 0)
        return;
    for (int draw = 0; draw < 8; ++draw)
        static_cast<void>(replay.Next());
}

// 0x52620f..0x526331: con el BMD, la pose del 0x13d en el buffer global (la acción publicada que se pasa de las del
// modelo vuelve a 0) y un rand() % 12 por cada hueso que no es dummy; con 0, la 0x6ec/3 con el efecto de dueño
// (0x5262e8) y la 0x5f2/0x15 sin dueño (0x526307) en el punto del hueso; con las partículas llenas, sólo los rand().
TEST(HighEffectFamiliesTest, TheMeteorSparksOverItsPosedBones)
{
    auto loaded = Client::Render::Models::MakeSkeleton(Client::Tests::SampleModel());
    ASSERT_TRUE(loaded.HasValue());
    const Client::Render::Models::Skeleton skeleton = loaded.Value();
    ASSERT_EQ(skeleton.bones.size(), 3u);
    std::uint32_t hit = 1;
    for (;; ++hit)
    {
        CrtRandom probe(hit);
        ReplayTheMeteorPrefix(probe);
        if (probe.Next() % 12 == 0)
            break;
    }
    {
        FamilyWorld tested;
        tested.random = CrtRandom(hit);
        float shake = 0.0f;
        tested.services.cameraShake = &shake;
        ASSERT_TRUE(CreateHighModelEffect({.type = 0x13d, .position = {12800.0f, 12800.0f, 0.0f}}, tested.Context())
                        .HasValue());
        const auto sparked = Client::Scenes::StepHighModelEffect(tested.Effect(0), &skeleton, tested.StepContext());
        ASSERT_TRUE(sparked.HasValue()) << sparked.Error();
        // Los rand() de las dos partículas por cada hueso que sale, en un pool aparte.
        CrtRandom replay(hit);
        ReplayTheMeteorPrefix(replay);
        auto twin = std::make_unique<Client::World::Particles>();
        const Client::World::ParticleOwner rockOwner{Client::World::ParticleOwnerKind::ModelEffect, 0};
        std::ptrdiff_t sparks = 0;
        for (const auto& bone : skeleton.bones)
        {
            if (bone.dummy || replay.Next() % 12 != 0)
                continue;
            ++sparks;
            ASSERT_TRUE(twin->CreateOwnedChecked(0x6ec, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(1.0f), 3, 4.0f,
                                                 tested.clock, replay, 0, rockOwner)
                            .HasValue());
            ASSERT_TRUE(twin->CreateChecked(0x5f2, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(1.0f), 0x15, 1.0f,
                                            tested.clock, replay, 0)
                            .HasValue());
        }
        EXPECT_EQ(tested.random.State(), replay.State());
        ASSERT_GE(sparks, 1);
        EXPECT_EQ(AliveParticles(*tested.world, 0x6ec, 3), sparks);
        EXPECT_EQ(AliveParticles(*tested.world, 0x5f2, 0x15), sparks);
        const auto all = tested.world->Particles().All();
        const auto shard = std::ranges::find_if(all, [](const Client::World::Particle& particle)
                                                { return particle.alive && particle.type == 0x6ec; });
        const auto spark = std::ranges::find_if(all, [](const Client::World::Particle& particle)
                                                { return particle.alive && particle.type == 0x5f2; });
        ASSERT_NE(shard, all.end());
        ASSERT_NE(spark, all.end());
        EXPECT_EQ(shard->typedOwner, rockOwner);
        EXPECT_EQ(spark->typedOwner.kind, Client::World::ParticleOwnerKind::None);
        EXPECT_EQ(shard->position, spark->position);
    }
    FamilyWorld tested;
    tested.FillTheParticles();
    float shake = 0.0f;
    tested.services.cameraShake = &shake;
    tested.world->PlaceModel(0x13d, {.action = 7});
    const glm::vec3 angles{0.0f, 0.0f, 60.0f};
    ASSERT_TRUE(CreateHighModelEffect({.type = 0x13d, .position = {12800.0f, 12800.0f, 0.0f}, .angles = angles},
                                      tested.Context())
                    .HasValue());
    ModelEffect& rock = tested.Effect(0);
    rock.animation.frame = 1.5f;
    const auto stepped = Client::Scenes::StepHighModelEffect(rock, &skeleton, tested.StepContext());
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    CrtRandom replay(Seed);
    ReplayTheMeteorPrefix(replay);
    static_cast<void>(replay.Next());
    static_cast<void>(replay.Next());
    EXPECT_EQ(tested.random.State(), replay.State());
    EXPECT_EQ(tested.world->ModelPlacement(0x13d).action, 0);
    std::vector<Client::Render::Models::BoneTransform> expected(3, Client::Render::Models::BoneTransform(0.0f));
    ASSERT_TRUE(
        Client::Render::Models::PoseSkeleton(skeleton, {.action = 0, .frame = 1.5f}, {.angles = angles}, {}, expected));
    const auto& carry = tested.world->PoseCarry().bones;
    ASSERT_GE(carry.size(), 3u);
    EXPECT_EQ(carry[0], expected[0]);
    EXPECT_EQ(carry[1], expected[1]);
}

// Un dueño del 0x140 en el lugar 5: el hueso k sin giro y en (0, 0, k), su modelo con escala 1 y el origen en su
// posición; el héroe (el lugar 0) en otro lado.
ActorState& MarkOwner(FamilyWorld& tested)
{
    ActorState& owner = tested.world->Actors()[5];
    owner.alive = true;
    owner.type = 0x20a;
    owner.position = {1000.0f, 2000.0f, 100.0f};
    owner.bodyPose.assign(30, Client::Render::Models::BoneTransform(1.0f));
    for (std::size_t bone = 0; bone < owner.bodyPose.size(); ++bone)
        owner.bodyPose[bone][3] = {0.0f, 0.0f, static_cast<float>(bone)};
    tested.world->PlaceModel(owner.type, {.scale = 1.0f, .origin = owner.position});
    tested.world->Actors()[0].position = {900.0f, 1900.0f, 0.0f};
    return owner;
}

// 0x51e0cb..0x51e233: el 0x140 nace en el hueso 20 de su dueño, 60 más arriba, con el alpha en 0, la z de los ángulos
// en 45, vida 130 y escala 3,2f, y suelta diez chispas 0x5c6/4 hacia él con tres rand() cada una; con las partículas
// llenas, los mismos treinta rand() y ninguna. Sin dueño, corta.
TEST(HighEffectFamiliesTest, TheGuildMarkRisesOverItsOwnerWithTenSparks)
{
    for (const bool full : {false, true})
    {
        SCOPED_TRACE(full);
        FamilyWorld tested;
        if (full)
            tested.FillTheParticles();
        ActorState& owner = MarkOwner(tested);
        const auto made =
            CreateHighModelEffect({.type = 0x140, .position = owner.position, .owner = &owner}, tested.Context());
        ASSERT_TRUE(made.HasValue()) << made.Error();
        const ModelEffect& mark = tested.Effect(0);
        EXPECT_EQ(mark.alpha, 0.0f);
        EXPECT_EQ(mark.transform.angles.z, 45.0f);
        EXPECT_EQ(mark.transform.position, (glm::vec3{1000.0f, 2000.0f, 180.0f}));
        EXPECT_EQ(mark.start, mark.transform.position);
        EXPECT_EQ(mark.life, 130);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(mark.transform.scale), 0x404ccccdu);
        EXPECT_EQ(mark.light, glm::vec3(1.0f));
        EXPECT_EQ(tested.Draws(), 30);
        EXPECT_EQ(AliveParticles(*tested.world, 0x5c6, 4), full ? 0 : 10);
        if (full)
            continue;
        CrtRandom replay(Seed);
        const auto turn = static_cast<float>(replay.Next() % 360);
        const auto side = static_cast<float>(replay.Next() % 20 + 15);
        const auto sine = Client::Render::Models::Detail::SineProductPC53(turn, side);
        ASSERT_TRUE(sine.has_value());
        const auto& first = tested.world->Particles().All()[0];
        EXPECT_EQ(first.position.x, static_cast<float>(*sine + 1000.0));
        // Referencia aparte (FSIN nativo con el perfil PC53): la x y la y de la primera chispa, en bits.
        EXPECT_EQ(std::bit_cast<std::uint32_t>(first.position.x), 0x44778b75u);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(first.position.y), 0x44f8261bu);
        EXPECT_EQ(first.position.z, 180.0f);
        EXPECT_EQ(first.typedOwner, (Client::World::ParticleOwner{Client::World::ParticleOwnerKind::ModelEffect, 0}));
    }
    FamilyWorld ownerless;
    EXPECT_TRUE(HasError(CreateHighModelEffect({.type = 0x140}, ownerless.Context()), "0x51e0d8"));
}

// 0x529f0c..0x52a0aa: el paso del 0x140 sigue al hueso 20 del dueño (x e y en dueño − héroe + punto, la z a la mitad
// hacia la meta si está debajo); entre 96 y 120 de vida el alpha sube 0,4f, sin girar mientras no llega a 1; con el
// alpha en 1, la z de los ángulos 5 más. Sin dueño, la vida en 0 y se apaga en la cola.
TEST(HighEffectFamiliesTest, TheGuildMarkFollowsItsOwnerAndFadesIn)
{
    FamilyWorld tested;
    ActorState& owner = MarkOwner(tested);
    ASSERT_TRUE(CreateHighModelEffect({.type = 0x140, .owner = &owner}, tested.Context()).HasValue());
    ModelEffect& mark = tested.Effect(0);
    mark.life = 100;
    mark.transform.position.z = 0.0f;
    const auto before = tested.Draws();
    ASSERT_TRUE(Client::Scenes::StepHighModelEffect(mark, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(tested.Draws(), before + 1);
    EXPECT_EQ(mark.alpha, static_cast<float>(static_cast<double>(std::bit_cast<float>(0x3ecccccdu))));
    EXPECT_EQ(mark.transform.angles.z, 45.0f);
    EXPECT_EQ(mark.transform.position, (glm::vec3{1100.0f, 2100.0f, 90.0f}));
    EXPECT_EQ(mark.life, 99);

    mark.alpha = 1.0f;
    mark.life = 70;
    ASSERT_TRUE(Client::Scenes::StepHighModelEffect(mark, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(mark.alpha, 1.0f);
    EXPECT_EQ(mark.transform.angles.z, 50.0f);
    EXPECT_EQ(mark.transform.position.z, static_cast<float>((180.0 - 90.0) * 0.5 + 90.0));

    mark.ownerContext = nullptr;
    ASSERT_TRUE(Client::Scenes::StepHighModelEffect(mark, nullptr, tested.StepContext()).HasValue());
    EXPECT_FALSE(mark.alive);
    EXPECT_EQ(mark.life, -1);
}

// Las chispas del paso del 0x140: con la vida entre 121 y 124, cinco 0x5c5/6 con él de dueño (0x529f71, escala 1,5f),
// que el paso de las partículas resuelve; entre 40 y 59, el giro, dos radios y la altura (cuatro rand()) y una 0x5c5/5
// (0x52a16e, escala 2) también con él de dueño (el ebx de 0x52a134), que al nacer lee su posición para su partida
// (0x5403ad) y saca dos rand(). Réplica en un pool gemelo con el mismo CrtRandom; el punto, desde el ASM (FSIN y FCOS
// del giro por los radios enteros, retenidos, y ((z + 48) − vida) + altura). Con las partículas llenas, sólo los rand()
// propios; por debajo de 50 el alpha baja 0,1f.
TEST(HighEffectFamiliesTest, TheGuildMarkSparksAtBirthAndBeforeFading)
{
    for (const bool full : {false, true})
    {
        SCOPED_TRACE(full);
        for (const int life : {122, 45})
        {
            SCOPED_TRACE(life);
            FamilyWorld tested;
            ActorState& owner = MarkOwner(tested);
            tested.FillTheParticles();
            ASSERT_TRUE(CreateHighModelEffect({.type = 0x140, .owner = &owner}, tested.Context()).HasValue());
            if (!full)
                for (auto& particle : tested.world->Particles().All())
                    particle.alive = false;
            ModelEffect& mark = tested.Effect(0);
            mark.life = life;
            mark.alpha = 0.5f;
            // Las 0x5c5/6 nacen donde estaba el 0x140 (0x529f5b, antes de seguir al hueso); la 0x5c5/5, después.
            const glm::vec3 before = mark.transform.position;
            CrtRandom replay = tested.random;
            const auto stepped = Client::Scenes::StepHighModelEffect(mark, nullptr, tested.StepContext());
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            static_cast<void>(replay.Next()); // 0x5208c5, la luz del paso.
            const Client::World::ParticleOwner markOwner{Client::World::ParticleOwnerKind::ModelEffect, 0};
            auto twin = std::make_unique<Client::World::Particles>();
            if (full)
                for (auto& particle : twin->All())
                    particle.alive = true;
            const glm::vec3& at = mark.transform.position;
            for (int burst = 0; life == 122 && burst < 5; ++burst)
                ASSERT_TRUE(twin->CreateOwnedChecked(0x5c5, before, mark.transform.angles, mark.light, 6, 1.5f,
                                                     tested.clock, replay, 0, markOwner)
                                .HasValue());
            if (life == 45)
            {
                const auto turn = static_cast<float>(replay.Next() % 360);
                const int side = replay.Next() % 26 - 13;
                const auto sine = Client::Render::Models::Detail::SineDoubleProductPC53(turn, side);
                const int depth = replay.Next() % 26 - 13;
                const auto cosine = Client::Render::Models::Detail::CosineDoubleProductPC53(turn, depth);
                const int lift = replay.Next() % 5;
                ASSERT_TRUE(sine.has_value() && cosine.has_value());
                const glm::vec3 point{static_cast<float>(*sine + static_cast<double>(at.x)),
                                      static_cast<float>(*cosine + static_cast<double>(at.y)),
                                      static_cast<float>(((static_cast<double>(at.z) + 48.0) - 45.0) + lift)};
                // Con la semilla (treinta rand() del alta y el de la luz antes): giro 132, radios −4 y 6 y altura 1,
                // con el 0x140 en (1100, 2100, 180). Los bits, calculados aparte con el emulador de la x87 (sin y cos
                // exactos redondeados a 64 bits, producto por el radio y suma con RN a 53, fstp a 24): x =
                // FSIN(132) · −4 + 1100, y = FCOS(132) · 6 + 2100 y z = ((180 + 48) − 45) + 1.
                EXPECT_EQ(turn, 132.0f);
                EXPECT_EQ(side, -4);
                EXPECT_EQ(depth, 6);
                EXPECT_EQ(lift, 1);
                EXPECT_EQ(at, (glm::vec3{1100.0f, 2100.0f, 180.0f}));
                EXPECT_EQ(std::bit_cast<std::uint32_t>(point.x), 0x44897935u);
                EXPECT_EQ(std::bit_cast<std::uint32_t>(point.y), 0x45039fddu);
                EXPECT_EQ(std::bit_cast<std::uint32_t>(point.z), 0x43380000u);
                Client::World::PortableParticleTrigonometry trigonometry;
                ASSERT_TRUE(twin->CreateTowardChecked(0x5c5, point, mark.transform.angles, mark.light, 5, 2.0f,
                                                      std::cref(tested.clock), replay, trigonometry, markOwner, at)
                                .HasValue());
            }
            EXPECT_EQ(tested.random.State(), replay.State());
            const int subtype = life == 122 ? 6 : 5;
            const auto resolved = Client::Scenes::ParticleOwnerStatesOf(*tested.world)(markOwner);
            ASSERT_TRUE(resolved.has_value());
            EXPECT_EQ(resolved->position, at);
            EXPECT_EQ(AliveParticles(*tested.world, 0x5c5, subtype), full ? 0 : (life == 122 ? 5 : 1));
            const auto all = tested.world->Particles().All();
            for (std::size_t slot = 0; !full && slot < all.size(); ++slot)
            {
                const auto& particle = all[slot];
                if (!particle.alive || particle.type != 0x5c5)
                    continue;
                const auto& expected = twin->All()[slot];
                ASSERT_TRUE(expected.alive) << slot;
                EXPECT_EQ(particle.typedOwner, markOwner);
                EXPECT_EQ(particle.subtype, expected.subtype);
                EXPECT_EQ(particle.position, expected.position);
                EXPECT_EQ(particle.start, expected.start);
                EXPECT_EQ(particle.velocity, expected.velocity);
                EXPECT_EQ(particle.life, expected.life);
                EXPECT_EQ(particle.scale, expected.scale);
                EXPECT_EQ(particle.texture, 0x5c6);
                if (subtype == 5)
                {
                    EXPECT_EQ(particle.start, at); // 0x5403ad: la posición del 0x140 al nacer.
                    EXPECT_EQ(particle.scale, 2.0f);
                }
            }
            EXPECT_EQ(mark.alpha, life < 50 ? static_cast<float>(0.5 - static_cast<double>(0.1f)) : 0.5f);
        }
    }
}

// 0x51cdc9..0x51cebe: el 0x125 sube 100, sortea el giro y el radio y deja el joint 0x6ee/0x12 con él de dueño y la
// clave 0 en (FSIN(giro) · radio + x, FCOS(giro) · radio + y, 0); después los ángulos en (−30, 0, rand() % 360). El
// 0x6ee/0x12 (SpiritJoints.cpp, 0x533539) no saca rand(): con lugar o con los joints llenos, tres rand().
TEST(HighEffectFamiliesTest, TheKundunSpiritLeavesItsOrbitingJoint)
{
    for (const bool full : {false, true})
    {
        SCOPED_TRACE(full);
        FamilyWorld tested;
        if (full)
            tested.FillTheJoints();
        const glm::vec3 origin{1000.0f, 2000.0f, 300.0f};
        const glm::vec3 angles{0.0f, 0.0f, 15.0f};
        const auto made =
            CreateHighModelEffect({.type = 0x125, .position = origin, .angles = angles}, tested.Context());
        const ModelEffect& spirit = tested.Effect(0);
        EXPECT_EQ(spirit.life, 30);
        EXPECT_EQ(spirit.objectState.blendMesh, -2);
        EXPECT_EQ(spirit.transform.scale, 1.0f);
        EXPECT_EQ(spirit.light, glm::vec3(0.5f));
        EXPECT_EQ(spirit.alpha, 0.0f);
        EXPECT_EQ(spirit.transform.position, (glm::vec3{1000.0f, 2000.0f, 400.0f}));
        ASSERT_TRUE(made.HasValue()) << made.Error();
        if (!full)
        {
            const auto& joint = tested.world->DrawTail().joints[0];
            EXPECT_TRUE(joint.alive);
            EXPECT_EQ(joint.type, 0x6ee);
            EXPECT_EQ(joint.subtype, 0x12);
            EXPECT_EQ(joint.ownerContext, &spirit);
            EXPECT_EQ(joint.key, 0);
            EXPECT_EQ(joint.width, 100.0f);
            EXPECT_EQ(joint.angles, angles);
        }
        CrtRandom replay(Seed);
        static_cast<void>(replay.Next());
        static_cast<void>(replay.Next());
        const auto yaw = static_cast<float>(replay.Next() % 360);
        EXPECT_EQ(spirit.transform.angles, (glm::vec3{-30.0f, 0.0f, yaw}));
        EXPECT_EQ(tested.random.State(), replay.State());
    }
}

// 0x51ce4f..0x51ce7d: con la semilla, el giro 211 y el radio 645. FSIN y FCOS de 211 (radianes, no grados) con la
// precisión de la x87, por el radio y más la x y la y en doble, guardados en float: los bits que da otra corrida
// (Fraction exacta y el seno con 80 dígitos), no los de SineProductPC53. La z es 0.
TEST(HighEffectFamiliesTest, TheKundunJointLandsOnTheBitsOfTheOrbit)
{
    FamilyWorld tested;
    const auto made = CreateHighModelEffect({.type = 0x125, .position = {1000.0f, 2000.0f, 300.0f}}, tested.Context());
    ASSERT_TRUE(made.HasValue()) << made.Error();
    const auto& joint = tested.world->DrawTail().joints[0];
    ASSERT_TRUE(joint.alive);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(joint.position.x), 0x442ad18bu);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(joint.position.y), 0x44b3c3d3u);
    EXPECT_EQ(joint.position.z, 0.0f);
}

// 0x539494..0x5394bb: el paso del 0x6ee/0x12 (después del movimiento común de 50 por sus ángulos y de su giro) le pone
// al 0x125 el alpha en 1 con vida de más de 5 (con 5, no) y le copia su posición; después (0x5394be..0x5394fb) deja la
// partícula 0x5f2/0x13 de escala 7 con la luz 1 en su posición y sigue a la cola (la vida, de 39 a 38). La partícula se
// repite en un pool gemelo con el mismo CrtRandom.
TEST(HighEffectFamiliesTest, TheKundunJointShowsAndCarriesItsSpirit)
{
    for (const std::int32_t life : {6, 5})
    {
        SCOPED_TRACE(life);
        FamilyWorld tested;
        ASSERT_TRUE(CreateHighModelEffect({.type = 0x125, .position = {1000.0f, 2000.0f, 300.0f}}, tested.Context())
                        .HasValue());
        ModelEffect& spirit = tested.Effect(0);
        spirit.life = life;
        auto& joints = tested.world->DrawTail().joints;
        const glm::vec3 born = joints[0].position;
        CrtRandom replay = tested.random;
        const auto stepped =
            Client::Scenes::StepJoints(joints, tested.random, nullptr, &tested.world->Particles(), 0.0f, nullptr);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(spirit.alpha, life > 5 ? 1.0f : 0.0f);
        EXPECT_EQ(spirit.transform.position, joints[0].position);
        // Con los ángulos del pedido en 0, el movimiento común es (0, −50, 0).
        EXPECT_EQ(joints[0].position, (glm::vec3{born.x, born.y - 50.0f, 0.0f}));
        EXPECT_EQ(joints[0].speed, 53.0f);
        EXPECT_EQ(joints[0].life, 38);
        const auto twin =
            TwinParticle(replay, 0x5f2, 0x13, joints[0].position, joints[0].angles, glm::vec3(1.0f), 7.0f);
        const auto& flame = tested.world->Particles().All()[0];
        ASSERT_TRUE(flame.alive);
        EXPECT_EQ(flame.type, 0x5f2);
        EXPECT_EQ(flame.subtype, 0x13);
        // La escala del pedido es 7; la de la partícula la sortea su alta (el gemelo la repite).
        EXPECT_EQ(flame.scale, twin.scale);
        EXPECT_EQ(flame.light, glm::vec3(1.0f));
        EXPECT_EQ(flame.position, twin.position);
        EXPECT_EQ(flame.angles, twin.angles);
        EXPECT_EQ(flame.life, twin.life);
        EXPECT_EQ(flame.velocity, twin.velocity);
        EXPECT_EQ(flame.rotation, twin.rotation);
        EXPECT_EQ(AliveParticles(*tested.world, 0x5f2, 0x13), 1);
        EXPECT_EQ(tested.random.State(), replay.State());
    }
}

// 0x539249..0x5392e6: el 0x613/0xf del 0x126 (vida 20, menos de 35) deja la 0x5d9/7 y, con rand() & 1 en 1, no suelta
// nada; con dueño le pone al 0x126 la x y la y de los ángulos en 0, el alpha en 1 (vida de más de 5) y la posición del
// joint más (FSIN(z) · k, −FCOS(z) · k, 0) con k = 30 · 0,1f + 30 en float (0x53929a); después el joint avanza con k
// retenido. En (10, 20) y con la z de los ángulos del 0x126 en 29, z = 29 · 0,01745329f (0x3f0192c0) y el k en float
// separa los dos: el 0x126 queda en x 0x41cffd5f y el joint en 0x41cffd60 (con el k retenido el 0x126 daría el del
// joint); la y es 0xc10dcc99 en los dos. Los bits salen de una cuenta aparte (seno y coseno exactos a 64 bits).
TEST(HighEffectFamiliesTest, TheKundunPhoenixJointCarriesItsPhoenix)
{
    std::uint32_t seed = 0;
    for (std::uint32_t candidate = 1; candidate < 100000 && seed == 0; ++candidate)
    {
        CrtRandom probe(candidate);
        static_cast<void>(TwinParticle(probe, 0x5d9, 7, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(1.0f), 1.0f));
        if ((probe.Next() & 1) != 0)
            seed = candidate;
    }
    ASSERT_NE(seed, 0U);
    FamilyWorld tested;
    ASSERT_TRUE(
        CreateHighModelEffect({.type = 0x126, .position = {10.0f, 20.0f, 300.0f}, .angles = {12.0f, 34.0f, 29.0f}},
                              tested.Context())
            .HasValue());
    ModelEffect& phoenix = tested.Effect(0);
    auto& joints = tested.world->DrawTail().joints;
    ASSERT_TRUE(joints[0].alive);
    EXPECT_EQ(joints[0].type, 0x613);
    EXPECT_EQ(joints[0].subtype, 0xf);
    EXPECT_EQ(joints[0].ownerContext, &phoenix);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(joints[0].angles.z), 0x3f0192c0u);
    tested.random = CrtRandom(seed);

    const auto stepped =
        Client::Scenes::StepJoints(joints, tested.random, nullptr, &tested.world->Particles(), 0.0f, nullptr);

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(joints[0].key, 30);
    EXPECT_EQ(phoenix.transform.angles, (glm::vec3{0.0f, 0.0f, 29.0f}));
    EXPECT_EQ(phoenix.alpha, 1.0f);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(phoenix.transform.position.x), 0x41cffd5fu);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(phoenix.transform.position.y), 0xc10dcc99u);
    EXPECT_EQ(phoenix.transform.position.z, 300.0f);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(joints[0].position.x), 0x41cffd60u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(joints[0].position.y), 0xc10dcc99u);
    EXPECT_FALSE(joints[1].alive);
    EXPECT_EQ(AliveParticles(*tested.world, 0x5d9, 7), 1);
    CrtRandom replay(seed);
    static_cast<void>(TwinParticle(replay, 0x5d9, 7, {10.0f, 20.0f, 300.0f}, joints[0].angles, glm::vec3(1.0f), 1.0f));
    static_cast<void>(replay.Next());
    EXPECT_EQ(tested.random.State(), replay.State());

    // Con la vida del 0x126 en 5, el alpha no cambia.
    phoenix.life = 5;
    phoenix.alpha = 0.25f;
    ASSERT_TRUE(Client::Scenes::StepJoints(joints, tested.random, nullptr, &tested.world->Particles(), 0.0f, nullptr)
                    .HasValue());
    EXPECT_EQ(phoenix.alpha, 0.25f);
}

// 0x5276b7..0x527787: el paso del 0x125 gira rand() % 10 + 10; entre 300 y 600 de z, con rand() & 1 en 0, el joint
// 0x6ee/0xe sin dueño, de ancho 100 y clave 0, en (x, y, 350) con el giro (rand() % 24) · 30. Fuera de esa franja, un
// solo rand(). Con la semilla, el alta saca tres rand() (con los joints llenos) y el paso empieza con el de la luz
// local; con cuatro más antes del paso, el giro es el noveno (2629: 19), la moneda el décimo (22786, par) y el rayo el
// undécimo (11018 % 24 = 2: 60 grados).
TEST(HighEffectFamiliesTest, TheKundunSpiritFansOutBetweenThreeAndSixHundred)
{
    for (const float height : {300.0f, 450.0f, 600.0f})
    {
        SCOPED_TRACE(height);
        FamilyWorld tested;
        tested.FillTheJoints();
        ASSERT_TRUE(CreateHighModelEffect({.type = 0x125}, tested.Context()).HasValue());
        ASSERT_EQ(tested.Draws(), 3);
        for (auto& joint : tested.world->DrawTail().joints)
            joint.alive = false;
        for (int draw = 0; draw < 4; ++draw)
            static_cast<void>(tested.random.Next());
        ModelEffect& spirit = tested.Effect(0);
        spirit.transform.position = {1000.0f, 2000.0f, height};
        const float yaw = spirit.transform.angles.z;
        ASSERT_TRUE(Client::Scenes::StepHighModelEffect(spirit, nullptr, tested.StepContext()).HasValue());
        EXPECT_EQ(spirit.transform.angles.z, static_cast<float>(19.0 + static_cast<double>(yaw)));
        const auto& fan = tested.world->DrawTail().joints[0];
        if (height != 450.0f)
        {
            EXPECT_EQ(tested.Draws(), 9);
            EXPECT_FALSE(fan.alive);
            continue;
        }
        EXPECT_EQ(tested.Draws(), 11);
        ASSERT_TRUE(fan.alive);
        EXPECT_EQ(fan.type, 0x6ee);
        EXPECT_EQ(fan.subtype, 0xe);
        EXPECT_EQ(fan.ownerContext, nullptr);
        EXPECT_EQ(fan.position, (glm::vec3{1000.0f, 2000.0f, 350.0f}));
        // Pide los ángulos (0, 0, 60); el alta del 0x6ee/0xe pone la x en 20 (0x5334bb).
        EXPECT_EQ(fan.angles, (glm::vec3{20.0f, 0.0f, 60.0f}));
        EXPECT_EQ(fan.width, 100.0f);
        EXPECT_EQ(fan.key, 0);
    }
}

// 0x518f38..0x518f90: el 0x128/0 y el /1 viven 30, sin rand(); el /2 deja la vida en 40 y la clave en 0 y corta en
// 0x518f90 (el 0x127 que crea con él de dueño); otro subtipo, sólo lo común.
TEST(HighEffectFamiliesTest, TheKundunCallLivesThirtyStepsOrCallsItsEcho)
{
    for (const std::int32_t subtype : {0, 1, 2, 3})
    {
        SCOPED_TRACE(subtype);
        FamilyWorld tested;
        const auto made = CreateHighModelEffect({.type = 0x128, .subtype = subtype}, tested.Context());
        const ModelEffect& call = tested.Effect(0);
        EXPECT_EQ(tested.Draws(), 0);
        EXPECT_TRUE(call.alive);
        if (subtype == 2)
        {
            EXPECT_TRUE(HasError(made, "0x518f90")) << (made.HasValue() ? std::string() : made.Error());
            EXPECT_EQ(call.life, 40);
            EXPECT_EQ(call.disturbanceState, 0);
            continue;
        }
        ASSERT_TRUE(made.HasValue()) << made.Error();
        EXPECT_EQ(call.disturbanceState, 0xffff);
        if (subtype != 3)
        {
            EXPECT_EQ(call.life, 30);
        }
    }

    // El alta de 0x43d051 y 0x43d0fd, sin el mundo: la misma; con la tabla llena, nada.
    FamilyWorld tested;
    auto& effects = tested.world->Map().effects;
    const auto made = Client::Scenes::CreateTheKundunCall(effects, {.type = 0x128, .subtype = 1});
    ASSERT_TRUE(made.HasValue()) << made.Error();
    EXPECT_EQ(made.Value(), std::optional<std::size_t>{std::size_t{0}});
    EXPECT_EQ(tested.Effect(0).life, 30);
    tested.FillTheEffects();
    const auto full = Client::Scenes::CreateTheKundunCall(effects, {.type = 0x128});
    ASSERT_TRUE(full.HasValue()) << full.Error();
    EXPECT_FALSE(full.Value().has_value());
    EXPECT_FALSE(Client::Scenes::CreateTheKundunCall(effects, {.type = 0x125}).HasValue());
}

// 0x527ad6..0x527b4a: el paso del 0x128/0 con la vida en 30 suelta diez 0x126 (i = 0, 8, …, 72) en su posición, de
// ángulos (0, 0, i + z + 320) con un solo fstp (con z = 15,3f, de 0x43a7a666 para i = 0 a 0x43cba666 para i = 72,
// calculados aparte); cada 0x126 (0x51cebf) pone sus campos y, con los joints llenos, saca rand() % 5 para el frame.
// Once rand() con el de la luz local. Con los joints libres, cada uno deja antes del frame su 0x613/0xf con él de
// dueño (0x51cf45), sin rand(). Con la vida en 29, nada.
TEST(HighEffectFamiliesTest, TheKundunCallSendsTenPhoenixesAtLifeThirty)
{
    for (const bool full : {true, false})
    {
        SCOPED_TRACE(full);
        FamilyWorld tested;
        if (full)
            tested.FillTheJoints();
        const glm::vec3 origin{1000.0f, 2000.0f, 149.0f};
        const glm::vec3 angles{0.0f, 0.0f, std::bit_cast<float>(0x4174cccdu)}; // 15,3f
        const glm::vec3 light{0.25f, 0.5f, 0.75f};
        ASSERT_TRUE(CreateHighModelEffect({.type = 0x128, .position = origin, .angles = angles, .light = light},
                                          tested.Context())
                        .HasValue());
        ModelEffect& call = tested.Effect(0);
        const auto stepped = Client::Scenes::StepHighModelEffect(call, nullptr, tested.StepContext());
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        if (!full)
        {
            // Cada 0x126 deja su 0x613/0xf con él de dueño, sin rand() (0x5334c7), antes del rand() del frame.
            for (std::size_t phoenix = 0; phoenix < 10; ++phoenix)
            {
                SCOPED_TRACE(phoenix);
                const auto& trail = tested.world->DrawTail().joints[phoenix];
                ASSERT_TRUE(trail.alive);
                EXPECT_EQ(trail.type, 0x613);
                EXPECT_EQ(trail.subtype, 0xf);
                EXPECT_EQ(trail.ownerContext, &tested.Effect(phoenix + 1));
                EXPECT_EQ(trail.life, 20);
                EXPECT_EQ(trail.key, 0);
                EXPECT_EQ(trail.width, 100.0f);
            }
            EXPECT_FALSE(tested.world->DrawTail().joints[10].alive);
        }
        EXPECT_EQ(tested.Draws(), 11);
        EXPECT_EQ(call.life, 29);
        CrtRandom replay(Seed);
        static_cast<void>(replay.Next());
        constexpr std::array<std::uint32_t, 10> Turns{0x43a7a666u, 0x43aba666u, 0x43afa666u, 0x43b3a666u, 0x43b7a666u,
                                                      0x43bba666u, 0x43bfa666u, 0x43c3a666u, 0x43c7a666u, 0x43cba666u};
        for (int phoenix = 0; phoenix < 10; ++phoenix)
        {
            SCOPED_TRACE(phoenix);
            const ModelEffect& made = tested.Effect(static_cast<std::size_t>(phoenix) + 1);
            ASSERT_TRUE(made.alive);
            EXPECT_EQ(made.type, 0x126);
            EXPECT_EQ(made.subtype, 0);
            EXPECT_FALSE(made.hasOwner);
            EXPECT_EQ(made.transform.position, origin);
            const float turn = std::bit_cast<float>(Turns[static_cast<std::size_t>(phoenix)]);
            EXPECT_EQ(made.transform.angles, (glm::vec3{0.0f, 0.0f, turn}));
            EXPECT_EQ(made.speed, std::bit_cast<float>(0x3eae147bu));
            EXPECT_EQ(made.life, 20);
            EXPECT_EQ(made.objectState.blendMesh, -2);
            EXPECT_EQ(made.transform.scale, std::bit_cast<float>(0x3f333333u));
            EXPECT_EQ(made.light, glm::vec3(0.5f));
            EXPECT_EQ(made.alpha, 0.0f);
            EXPECT_EQ(made.animation.frame, static_cast<float>(replay.Next() % 5));
        }
        EXPECT_FALSE(tested.Effect(11).alive);
        ASSERT_TRUE(Client::Scenes::StepHighModelEffect(call, nullptr, tested.StepContext()).HasValue());
        EXPECT_EQ(tested.Draws(), 12);
        EXPECT_FALSE(tested.Effect(11).alive);
    }
}

// 0x527b4f..0x527c22: el 0x128/1 con la vida en 30 suelta veinte 0x125 y, con la clave (+0x8a, MOVSX) entre 6 y 9,
// uno más por paso; la clave sube una por paso desde el −1 del alta. Con los joints llenos cada 0x125 saca tres rand();
// en los pasos 8 a 11 (clave 6 a 9), cuatro más. Con los joints libres, los mismos rand() y un 0x6ee/0x12 por 0x125.
TEST(HighEffectFamiliesTest, TheKundunCallSendsTwentySpiritsAndFourMore)
{
    FamilyWorld tested;
    tested.FillTheJoints();
    const glm::vec3 origin{1000.0f, 2000.0f, 400.0f};
    ASSERT_TRUE(CreateHighModelEffect({.type = 0x128, .position = origin, .subtype = 1}, tested.Context()).HasValue());
    ModelEffect& call = tested.Effect(0);
    const auto spirits = [&tested]
    {
        return std::ranges::count_if(tested.world->Map().effects.Effects(),
                                     [](const ModelEffect& effect) { return effect.alive && effect.type == 0x125; });
    };
    ASSERT_TRUE(Client::Scenes::StepHighModelEffect(call, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(spirits(), 20);
    EXPECT_EQ(tested.Draws(), 1 + 20 * 3);
    EXPECT_EQ(call.disturbanceState, 0);
    EXPECT_EQ(tested.Effect(1).transform.position, origin + glm::vec3(0.0f, 0.0f, 100.0f));
    int draws = tested.Draws();
    for (int step = 2; step <= 30; ++step)
    {
        SCOPED_TRACE(step);
        ASSERT_TRUE(Client::Scenes::StepHighModelEffect(call, nullptr, tested.StepContext()).HasValue());
        const bool extra = step >= 8 && step <= 11;
        draws += extra ? 4 : 1;
        EXPECT_EQ(tested.Draws(), draws);
    }
    EXPECT_EQ(spirits(), 24);
    EXPECT_FALSE(call.alive);

    // Con los joints libres, cada 0x125 deja su 0x6ee/0x12 con él de dueño (0x51ce91), sin rand() de más.
    FamilyWorld free;
    ASSERT_TRUE(CreateHighModelEffect({.type = 0x128, .subtype = 1}, free.Context()).HasValue());
    ASSERT_TRUE(Client::Scenes::StepHighModelEffect(free.Effect(0), nullptr, free.StepContext()).HasValue());
    EXPECT_EQ(free.Draws(), 1 + 20 * 3);
    for (std::size_t spirit = 0; spirit < 20; ++spirit)
    {
        SCOPED_TRACE(spirit);
        const auto& orbit = free.world->DrawTail().joints[spirit];
        ASSERT_TRUE(orbit.alive);
        EXPECT_EQ(orbit.type, 0x6ee);
        EXPECT_EQ(orbit.subtype, 0x12);
        EXPECT_EQ(orbit.ownerContext, &free.Effect(spirit + 1));
    }
    EXPECT_FALSE(free.world->DrawTail().joints[20].alive);
}

// 0x527c27..0x527c51: el 0x128/2 con la clave en 0 y la vida debajo de 10 pone la clave en 1.
TEST(HighEffectFamiliesTest, TheKundunEchoRaisesItsKeyBelowLifeTen)
{
    for (const std::int32_t life : {10, 9})
    {
        SCOPED_TRACE(life);
        FamilyWorld tested;
        static_cast<void>(CreateHighModelEffect({.type = 0x128, .subtype = 2}, tested.Context()));
        ModelEffect& echo = tested.Effect(0);
        ASSERT_EQ(echo.disturbanceState, 0);
        echo.life = life;
        ASSERT_TRUE(Client::Scenes::StepHighModelEffect(echo, nullptr, tested.StepContext()).HasValue());
        EXPECT_EQ(echo.disturbanceState, life < 10 ? 1 : 0);
        EXPECT_EQ(tested.Draws(), 1);
    }
}

// 0x52778c..0x5277a9: el 0x126 con la vida debajo de 5 pone el alpha en vida · 0,2f; con 5 o más, nada.
TEST(HighEffectFamiliesTest, ThePhoenixFadesInItsLastFourSteps)
{
    FamilyWorld tested;
    tested.FillTheJoints();
    ASSERT_TRUE(CreateHighModelEffect({.type = 0x126}, tested.Context()).HasValue());
    EXPECT_EQ(tested.Draws(), 1);
    ModelEffect& phoenix = tested.Effect(0);
    phoenix.life = 5;
    ASSERT_TRUE(Client::Scenes::StepHighModelEffect(phoenix, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(phoenix.alpha, 0.0f);
    EXPECT_EQ(phoenix.life, 4);
    ASSERT_TRUE(Client::Scenes::StepHighModelEffect(phoenix, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(phoenix.alpha, static_cast<float>(4.0 * static_cast<double>(std::bit_cast<float>(0x3e4ccccdu))));
    ASSERT_TRUE(Client::Scenes::StepHighModelEffect(phoenix, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(phoenix.alpha, static_cast<float>(3.0 * static_cast<double>(std::bit_cast<float>(0x3e4ccccdu))));
    EXPECT_EQ(tested.Draws(), 4);
}
} // namespace
