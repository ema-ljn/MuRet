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
#include "Scenes/FaunaDrawing.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/HighEffectEmitters.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldState.h"
#include "World/GameModelEffects.h"
#include "World/ModelEffects.h"
#include "World/ParticleOwner.h"
#include "World/Particles.h"
#include "World/TerrainLighting.h"
#include "World/WorldObjectStepServices.h"

#include "support/SyntheticModels.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
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
// modelo vuelve a 0) y un rand() % 12 por cada hueso que no es dummy; con 0, la 0x6ec/3 (de otro lote) corta con lugar
// (0x5262e8); con las partículas llenas, sólo los rand().
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
        const auto cut = Client::Scenes::StepHighModelEffect(tested.Effect(0), &skeleton, tested.StepContext());
        EXPECT_TRUE(HasError(cut, "0x5262e8")) << (cut.HasValue() ? std::string() : cut.Error());
        CrtRandom replay(hit);
        ReplayTheMeteorPrefix(replay);
        static_cast<void>(replay.Next());
        EXPECT_EQ(tested.random.State(), replay.State());
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

// Las chispas del paso del 0x140: con la vida entre 121 y 124, cinco 0x5c5/6 (0x529f71); entre 40 y 59, el giro, dos
// radios y la altura (cuatro rand()) y una 0x5c5/5 (0x52a16e). No son de MuRet: con lugar cortan, con las partículas
// llenas siguen; por debajo de 50 el alpha baja 0,1f.
TEST(HighEffectFamiliesTest, TheGuildMarkSparksAtBirthAndBeforeFading)
{
    for (const bool full : {false, true})
    {
        SCOPED_TRACE(full);
        for (const auto& [life, address, draws] : {std::tuple{122, "0x529f71", 1}, std::tuple{45, "0x52a16e", 5}})
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
            const auto before = tested.Draws();
            const auto stepped = Client::Scenes::StepHighModelEffect(mark, nullptr, tested.StepContext());
            EXPECT_EQ(tested.Draws(), before + draws);
            if (!full)
            {
                EXPECT_TRUE(HasError(stepped, address)) << (stepped.HasValue() ? std::string() : stepped.Error());
                continue;
            }
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            EXPECT_EQ(mark.alpha, life < 50 ? static_cast<float>(0.5 - static_cast<double>(0.1f)) : 0.5f);
        }
    }
}

// 0x51cdc9..0x51cebe: el 0x125 sube 100, sortea el giro y el radio y deja el joint 0x6ee/0x12 con él de dueño y la
// clave 0 en (FSIN(giro) · radio + x, FCOS(giro) · radio + y, 0); después los ángulos en (−30, 0, rand() % 360). El
// 0x6ee/0x12 con un efecto de dueño es del lote de joints: con lugar corta en 0x51ce91, después de los dos rand(). Con
// los joints llenos, sin el joint y con el tercer rand().
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
        if (!full)
        {
            EXPECT_TRUE(HasError(made, "0x51ce91")) << (made.HasValue() ? std::string() : made.Error());
            EXPECT_EQ(tested.Draws(), 2);
            EXPECT_EQ(spirit.transform.angles, angles);
            continue;
        }
        ASSERT_TRUE(made.HasValue()) << made.Error();
        CrtRandom replay(Seed);
        static_cast<void>(replay.Next());
        static_cast<void>(replay.Next());
        const auto yaw = static_cast<float>(replay.Next() % 360);
        EXPECT_EQ(spirit.transform.angles, (glm::vec3{-30.0f, 0.0f, yaw}));
        EXPECT_EQ(tested.random.State(), replay.State());
    }
}

// 0x5276b7..0x527787: el paso del 0x125 gira rand() % 10 + 10; entre 300 y 600 de z, con rand() & 1 en 0, el joint
// 0x6ee/0xe sin dueño en (x, y, 350) con el giro (rand() % 24) · 30. Fuera de esa franja, un solo rand().
TEST(HighEffectFamiliesTest, TheKundunSpiritFansOutBetweenThreeAndSixHundred)
{
    for (const float height : {300.0f, 450.0f, 600.0f})
    {
        SCOPED_TRACE(height);
        FamilyWorld tested;
        tested.FillTheJoints();
        ASSERT_TRUE(CreateHighModelEffect({.type = 0x125}, tested.Context()).HasValue());
        for (auto& joint : tested.world->DrawTail().joints)
            joint.alive = false;
        ModelEffect& spirit = tested.Effect(0);
        spirit.transform.position = {1000.0f, 2000.0f, height};
        const float yaw = spirit.transform.angles.z;
        const int before = tested.Draws();
        ASSERT_TRUE(Client::Scenes::StepHighModelEffect(spirit, nullptr, tested.StepContext()).HasValue());
        CrtRandom replay(Seed);
        for (int draw = 0; draw < before + 1; ++draw)
            static_cast<void>(replay.Next());
        const int spin = replay.Next() % 10 + 10;
        EXPECT_EQ(spirit.transform.angles.z, static_cast<float>(spin + static_cast<double>(yaw)));
        const auto& fan = tested.world->DrawTail().joints[0];
        if (height != 450.0f)
        {
            EXPECT_EQ(tested.random.State(), replay.State());
            EXPECT_FALSE(fan.alive);
            continue;
        }
        if ((replay.Next() & 1) != 0)
        {
            EXPECT_EQ(tested.random.State(), replay.State());
            EXPECT_FALSE(fan.alive);
            continue;
        }
        const int spoke = replay.Next() % 24;
        ASSERT_TRUE(fan.alive);
        EXPECT_EQ(fan.type, 0x6ee);
        EXPECT_EQ(fan.subtype, 0xe);
        EXPECT_EQ(fan.ownerContext, nullptr);
        EXPECT_EQ(fan.position, (glm::vec3{1000.0f, 2000.0f, 350.0f}));
        EXPECT_EQ(fan.angles, (glm::vec3{0.0f, 0.0f, static_cast<float>(spoke * 30)}));
    }
}
} // namespace
