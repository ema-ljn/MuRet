#include "Effects/HighModelEffects.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Kernel/JointRequest.h"
#include "Math/HalfLifeMath.h"
#include "Math/OriginalAngles.h"
#include "Math/OriginalRetainedMath.h"
#include "Math/OriginalRotation.h"
#include "Models/Pose.h"
#include "Models/Skeleton.h"
#include "Kernel/ActorState.h"
#include "Joints/FaunaJointCreation.h"
#include "Kernel/DrawTailState.h"
#include "Effects/GameParticles.h"
#include "Effects/HighEffectEmitters.h"
#include "Effects/KundunEffects.h"
#include "Effects/KundunFallEffects.h"
#include "Effects/SkillBurstEffects.h"
#include "Effects/WizardEffects.h"
#include "Kernel/WorldMap.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Effects/GameModelEffects.h"
#include "Effects/ModelEffects.h"
#include "Particles/ParticleOwner.h"
#include "Particles/Particles.h"
#include "Particles/PortableParticleTrigonometry.h"
#include "Terrain/TerrainLighting.h"
#include "Effects/WizardModelEffects.h"
#include "Terrain/DynamicWorldLight.h"
#include "Terrain/WorldTerrain.h"
#include "Assets/FaunaModels.h"

#include "support/LentWorld.h"
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
#include <string_view>
#include <utility>
#include <vector>

// The CL11 high-type families that live in their own file and enter through CreateHighModelEffect and
// StepHighModelEffect: the 0x114 of Effects/WaveEffects, the 0x11b of Effects/RingEffects and the 0x13d..0x13f of
// Effects/MeteorEffects. 06, "Los efectos con modelo de los tipos altos".
namespace
{
using Client::Crt::CrtRandom;
using Client::Effects::CreateHighModelEffect;
using Client::Effects::HighEffectContext;
using Client::Effects::HighEffectStepContext;
using Client::Effects::ModelEffect;
using Client::Kernel::ActorState;
using Client::Kernel::WorldState;

constexpr std::uint32_t Seed = 0x2468ace1u;

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
        return {.world = *world, .random = random, .worldClock = clock, .faunaModels = &faunaModels};
    }

    HighEffectStepContext StepContext()
    {
        return {
            .world = *world, .random = random, .services = services, .worldClock = clock, .faunaModels = &faunaModels};
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

    std::unique_ptr<WorldState> world = Client::Tests::LentWorld();
    std::unique_ptr<Client::Terrain::TerrainLighting> lighting = std::make_unique<Client::Terrain::TerrainLighting>();
    CrtRandom random{Seed};
    float clock = 1234.5f;
    Client::Effects::ModelEffectStepServices services{.effects = &world->Map().effects, .lighting = lighting.get()};
    Client::Assets::FaunaModels faunaModels;
};

// A particle's rand(): it creates it in a separate pool with `replay`, which advances like the original.
void ReplayParticle(CrtRandom& replay, std::int32_t type, std::int32_t subtype, float scale)
{
    auto twin = std::make_unique<Client::Particles::Particles>();
    const auto made =
        twin->CreateChecked(type, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(1.0f), subtype, scale, 0.0f, replay);
    ASSERT_TRUE(made.HasValue());
}

// The particle a step asks for, repeated in a twin pool with `replay` (the same CrtRandom as the original).
Client::Particles::Particle TwinParticle(CrtRandom& replay, std::int32_t type, std::int32_t subtype,
                                         const glm::vec3& position, const glm::vec3& angles, const glm::vec3& light,
                                         float scale)
{
    auto twin = std::make_unique<Client::Particles::Particles>();
    const auto made = twin->CreateChecked(type, position, angles, light, subtype, scale, 0.0f, replay);
    EXPECT_TRUE(made.HasValue() && made.Value().has_value());
    return twin->All()[0];
}

std::ptrdiff_t AliveParticles(WorldState& world, std::int32_t type, std::int32_t subtype)
{
    return std::ranges::count_if(world.Particles().All(), [type, subtype](const Client::Particles::Particle& particle)
                                 { return particle.alive && particle.type == type && particle.subtype == subtype; });
}

template <typename Result> bool HasError(const Result& result, std::string_view address)
{
    return !result.HasValue() && result.Error().starts_with(address);
}

// 0x51c186: the 0x114/0 draws one rand() (the scale) before each of its sixty 0x5ac/0 joints (FlareJoints.cpp), which
// draw three more at birth (0x530cbc..0x530cfe); with the joints full, sixty rand() and the common part.
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
            EXPECT_EQ(std::ranges::count_if(joints, [](const Client::Kernel::JointState& joint)
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

// 0x51db73: another 0x114 subtype keeps only the common part, no rand(); with the table full, nothing.
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
// 0x51c7a7..0x51c883: 0x439b20's 0x11b/1, no rand(): life 49, scale 2, the points and the start at the position, and
// joints 0x618/0xa and /0xb owned by the effect (width 30, five points); with the joints full, without them.
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
            EXPECT_EQ(beam.ownerKind, Client::Kernel::JointOwnerKind::ModelEffect);
            EXPECT_EQ(beam.width, 30.0f);
            EXPECT_EQ(beam.maximumCount, 5);
            EXPECT_EQ(beam.position, (glm::vec3{100.0f, 200.0f, 300.0f}));
        }
        EXPECT_FALSE(joints[2].alive);
    }
}

// 0x526507..0x5266e5: the step of the 0x11b/1 with an owner. With life 10 or more, above 400 over the terrain it dives
// (x at 5, the copied one at 0); the shake's two rand() and, with a slot, those of particle 0x5d9/5.
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
        const auto stepped = Client::Effects::StepHighModelEffect(ring, nullptr, tested.StepContext());
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
        static_cast<void>(Client::Math::MoveToward({1000.0f, 1000.0f, 500.0f}, angles, ring.start, 10.0f));
        const float copiedYaw = static_cast<float>(yaw * static_cast<double>(0.8f));
        EXPECT_EQ(ring.transform.angles.x, 5.0f);
        EXPECT_EQ(ring.copiedAngles.x, 0.0f);
        EXPECT_EQ(ring.transform.angles.z, static_cast<float>(static_cast<double>(copiedYaw) + angles.z));
        EXPECT_EQ(ring.copiedAngles.z, static_cast<float>(static_cast<double>(copiedYaw) * static_cast<double>(0.8f)));
        EXPECT_EQ(ring.life, 19);
        EXPECT_EQ(AliveParticles(*tested.world, 0x5d9, 5), full ? 0 : 1);
    }
}

// Without the terrain height, the 0x11b/1's step with life 10 or more stops after its two rand(); with less, it does
// not ask for it: alpha times 0.909f, +0x68 times 0.833f and x 5 more.
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
        const auto stepped = Client::Effects::StepHighModelEffect(ring, nullptr, tested.StepContext());
        EXPECT_EQ(tested.Draws(), 3);
        if (life == 10)
        {
            EXPECT_TRUE(HasError(stepped, "0x526650"));
            continue;
        }
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        // The step's first rand() is the local light; the shake, (8845 & 31) − 16 = −3 of pitch and (11882 & 31) − 16 =
        // −6 of turn. 0x4e9810 also turns the pitch toward the start, (0, 0, 200) from the origin: 0x4e93f0(z, 0, 200,
        // 0) gives 90, 360 − 90 = 270 and 0x4e9560 takes it from 0 to 350 in steps of 10 (0x4e9647); z, from 0 to 350
        // toward 180. The copied x at −3 · 0.2f is added (349.4f, above 80: without 0x52663d's 5) and slowed by 0.6f;
        // the copied z at −6 · 0.8f is added (345.2f) and slowed by 0.8f.
        EXPECT_EQ(std::bit_cast<std::uint32_t>(ring.transform.angles.x), 0x43aeb333u);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(ring.transform.angles.z), 0x43ac999au);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(ring.copiedAngles.x), 0xbeb851ecu);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(ring.copiedAngles.z), 0xc075c290u);
        EXPECT_EQ(ring.alpha, std::bit_cast<float>(0x3f68ba2eu));
        EXPECT_EQ(ring.objectState.light,
                  static_cast<float>(0.5 * static_cast<double>(std::bit_cast<float>(0x3f555555u))));
    }
}

// 0x51c789: the 0x11b/0 lives 1 with scale 2.3f; the one of joint 0x613/0x13 (CreateTheRingSpark) is the same.
TEST(HighEffectFamiliesTest, TheRingSparkLivesOneStep)
{
    FamilyWorld tested;
    ASSERT_TRUE(CreateHighModelEffect({.type = 0x11b, .position = {1.0f, 2.0f, 3.0f}}, tested.Context()).HasValue());
    EXPECT_EQ(tested.Effect(0).life, 1);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(tested.Effect(0).transform.scale), 0x40133333u);
    EXPECT_EQ(tested.Draws(), 0);
}

// 0x525f6a..0x52603d: at life 8, the 0x13f releases sixteen ownerless 0x60f/0x18 joints, width 90, at its position
// with z 100 more, x plus rand() % 600 − 200 and y plus rand() % 600 − 400, with angles (−10, 0, i · 10); with the
// joints full, only those thirty-two rand().
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
        const auto stepped = Client::Effects::StepHighModelEffect(call, nullptr, tested.StepContext());
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        FamilyWorld twin;
        static_cast<void>(twin.random.Next()); // the local light (0x5208c5)
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
            ASSERT_TRUE(Client::Effects::CreateHighJoint(*twin.world, twin.random, 0x60f, point, point, angles, 0x18,
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

// 0x526042..0x5260b8: at life 2, the 0x13f draws the turn (rand() % 360) and leaves the 0x13d (0x526090) and the 0x13e
// (0x5260ad) at its position, with its light and itself as owner (life 120, scale 0.8f), and dies; with the table
// full, only the turn.
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
        const auto stepped = Client::Effects::StepHighModelEffect(call, nullptr, tested.StepContext());
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

// A seed that gives `coin` on the third rand() (0x52612c): the local light and the shake come first.
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

// 0x5260bd..0x526207, the 0x13d without BMD: the light (v, v, v + 0.1) with radius 10 and v = FSIN(life · π/120) ·
// −0.5, the shake ((rand() & 3) − 2) · 0.1f and, with rand() & 1 at 0, the small stone 0xc8 or 0xc9 at rand() % 150 +
// 300 turned rand() % 360 in z, with its five rand().
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
        const auto stepped = Client::Effects::StepHighModelEffect(rock, nullptr, tested.StepContext());
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
            const glm::vec3 offset = Client::Math::OriginalVectorRotate(
                {0.0f, static_cast<float>(reach + 300.0), 0.0f},
                Client::Math::OriginalAngleMatrix({0.0f, 0.0f, static_cast<float>(turn)}));
            const glm::vec3 point{static_cast<float>(static_cast<double>(offset.x) + origin.x),
                                  static_cast<float>(static_cast<double>(offset.y) + origin.y),
                                  static_cast<float>(static_cast<double>(offset.z) + origin.z)};
            auto twin = std::make_unique<Client::Effects::ModelEffects>();
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
        const auto wave = Client::Math::Detail::SineDoubleProductPC53(40.0 * 0.02617993950843811, -0.5);
        ASSERT_TRUE(wave.has_value());
        const glm::vec3 color{static_cast<float>(*wave), static_cast<float>(*wave),
                              static_cast<float>(*wave + 0.10000000149011612)};
        auto lit = std::make_unique<Client::Terrain::TerrainLighting>();
        ASSERT_TRUE(Client::Terrain::AddDynamicLight(lit.get(), origin, color, 10, "prueba").HasValue());
        EXPECT_TRUE(lit->cells == tested.lighting->cells);
        EXPECT_EQ(rock.life, 39);
    }
}

// Without the dynamic light the 0x13d stops before the shake (0x5260fe); without the shake, after its rand()
// (0x526126).
TEST(HighEffectFamiliesTest, TheMeteorAsksForTheLightAndTheShake)
{
    for (const bool lit : {false, true})
    {
        SCOPED_TRACE(lit);
        FamilyWorld tested;
        if (!lit)
            tested.services.lighting = nullptr;
        ASSERT_TRUE(CreateHighModelEffect({.type = 0x13d}, tested.Context()).HasValue());
        const auto stepped = Client::Effects::StepHighModelEffect(tested.Effect(0), nullptr, tested.StepContext());
        EXPECT_TRUE(HasError(stepped, lit ? "0x526126" : "0x5260fe"));
        EXPECT_EQ(tested.Draws(), lit ? 2 : 1);
    }
}

// The 0x13d's rand() before its bones: the local light, the shake, the coin and, with 0, the small stone's three and
// its five.
void ReplayTheMeteorPrefix(CrtRandom& replay)
{
    static_cast<void>(replay.Next());
    static_cast<void>(replay.Next());
    if ((replay.Next() & 1) != 0)
        return;
    for (int draw = 0; draw < 8; ++draw)
        static_cast<void>(replay.Next());
}

// 0x52620f..0x526331: with the BMD, the 0x13d's pose in the global buffer (a published action past the model's goes
// back to 0) and one rand() % 12 per non-dummy bone; with 0, the 0x6ec/3 owned by the effect (0x5262e8) and the
// ownerless 0x5f2/0x15 (0x526307) at the bone's point; with the particles full, only the rand().
TEST(HighEffectFamiliesTest, TheMeteorSparksOverItsPosedBones)
{
    auto loaded = Client::Models::MakeSkeleton(Client::Tests::SampleModel());
    ASSERT_TRUE(loaded.HasValue());
    const Client::Models::Skeleton skeleton = loaded.Value();
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
        const auto sparked = Client::Effects::StepHighModelEffect(tested.Effect(0), &skeleton, tested.StepContext());
        ASSERT_TRUE(sparked.HasValue()) << sparked.Error();
        // The two particles' rand() for each bone that comes out, in a separate pool.
        CrtRandom replay(hit);
        ReplayTheMeteorPrefix(replay);
        auto twin = std::make_unique<Client::Particles::Particles>();
        const Client::Particles::ParticleOwner rockOwner{Client::Particles::ParticleOwnerKind::ModelEffect, 0};
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
        const auto shard = std::ranges::find_if(all, [](const Client::Particles::Particle& particle)
                                                { return particle.alive && particle.type == 0x6ec; });
        const auto spark = std::ranges::find_if(all, [](const Client::Particles::Particle& particle)
                                                { return particle.alive && particle.type == 0x5f2; });
        ASSERT_NE(shard, all.end());
        ASSERT_NE(spark, all.end());
        EXPECT_EQ(shard->typedOwner, rockOwner);
        EXPECT_EQ(spark->typedOwner.kind, Client::Particles::ParticleOwnerKind::None);
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
    const auto stepped = Client::Effects::StepHighModelEffect(rock, &skeleton, tested.StepContext());
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    CrtRandom replay(Seed);
    ReplayTheMeteorPrefix(replay);
    static_cast<void>(replay.Next());
    static_cast<void>(replay.Next());
    EXPECT_EQ(tested.random.State(), replay.State());
    EXPECT_EQ(tested.world->ModelPlacement(0x13d).action, 0);
    std::vector<Client::Math::BoneTransform> expected(3, Client::Math::BoneTransform(0.0f));
    ASSERT_TRUE(Client::Models::PoseSkeleton(skeleton, {.action = 0, .frame = 1.5f}, {.angles = angles}, {}, expected));
    const auto& carry = tested.world->PoseCarry().bones;
    ASSERT_GE(carry.size(), 3u);
    EXPECT_EQ(carry[0], expected[0]);
    EXPECT_EQ(carry[1], expected[1]);
}

// An owner of the 0x140 in slot 5: bone k unrotated and at (0, 0, k), its model with scale 1 and the origin at its
// position; the hero (slot 0) elsewhere.
ActorState& MarkOwner(FamilyWorld& tested)
{
    ActorState& owner = tested.world->Actors()[5];
    owner.alive = true;
    owner.type = 0x20a;
    owner.position = {1000.0f, 2000.0f, 100.0f};
    owner.bodyPose.assign(30, Client::Math::BoneTransform(1.0f));
    for (std::size_t bone = 0; bone < owner.bodyPose.size(); ++bone)
        owner.bodyPose[bone][3] = {0.0f, 0.0f, static_cast<float>(bone)};
    tested.world->PlaceModel(owner.type, {.scale = 1.0f, .origin = owner.position});
    tested.world->Actors()[0].position = {900.0f, 1900.0f, 0.0f};
    return owner;
}

// 0x51e0cb..0x51e233: the 0x140 is born at its owner's bone 20, 60 higher, with alpha at 0, the angles' z at 45, life
// 130 and scale 3.2f, and releases ten 0x5c6/4 sparks toward it with three rand() each; with the particles full, the
// same thirty rand() and none. Without an owner, it stops.
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
        const auto sine = Client::Math::Detail::SineProductPC53(turn, side);
        ASSERT_TRUE(sine.has_value());
        const auto& first = tested.world->Particles().All()[0];
        EXPECT_EQ(first.position.x, static_cast<float>(*sine + 1000.0));
        // Separate reference (native FSIN with the PC53 profile): the first spark's x and y, in bits.
        EXPECT_EQ(std::bit_cast<std::uint32_t>(first.position.x), 0x44778b75u);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(first.position.y), 0x44f8261bu);
        EXPECT_EQ(first.position.z, 180.0f);
        EXPECT_EQ(first.typedOwner,
                  (Client::Particles::ParticleOwner{Client::Particles::ParticleOwnerKind::ModelEffect, 0}));
    }
    FamilyWorld ownerless;
    EXPECT_TRUE(HasError(CreateHighModelEffect({.type = 0x140}, ownerless.Context()), "0x51e0d8"));
}

// 0x529f0c..0x52a0aa: the 0x140's step follows the owner's bone 20 (x and y at owner − hero + point, z halfway toward
// the goal if below); between life 96 and 120 alpha rises 0.4f, without turning until it reaches 1; with alpha at 1,
// the angles' z 5 more. Without an owner, life at 0 and it dies in the tail.
TEST(HighEffectFamiliesTest, TheGuildMarkFollowsItsOwnerAndFadesIn)
{
    FamilyWorld tested;
    ActorState& owner = MarkOwner(tested);
    ASSERT_TRUE(CreateHighModelEffect({.type = 0x140, .owner = &owner}, tested.Context()).HasValue());
    ModelEffect& mark = tested.Effect(0);
    mark.life = 100;
    mark.transform.position.z = 0.0f;
    const auto before = tested.Draws();
    ASSERT_TRUE(Client::Effects::StepHighModelEffect(mark, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(tested.Draws(), before + 1);
    EXPECT_EQ(mark.alpha, static_cast<float>(static_cast<double>(std::bit_cast<float>(0x3ecccccdu))));
    EXPECT_EQ(mark.transform.angles.z, 45.0f);
    EXPECT_EQ(mark.transform.position, (glm::vec3{1100.0f, 2100.0f, 90.0f}));
    EXPECT_EQ(mark.life, 99);

    mark.alpha = 1.0f;
    mark.life = 70;
    ASSERT_TRUE(Client::Effects::StepHighModelEffect(mark, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(mark.alpha, 1.0f);
    EXPECT_EQ(mark.transform.angles.z, 50.0f);
    EXPECT_EQ(mark.transform.position.z, static_cast<float>((180.0 - 90.0) * 0.5 + 90.0));

    mark.ownerContext = nullptr;
    ASSERT_TRUE(Client::Effects::StepHighModelEffect(mark, nullptr, tested.StepContext()).HasValue());
    EXPECT_FALSE(mark.alive);
    EXPECT_EQ(mark.life, -1);
}

// The 0x140 step's sparks: with life between 121 and 124, five 0x5c5/6 owned by it (0x529f71, scale 1.5f), which the
// particle step resolves; between 40 and 59, the turn, two radii and the height (four rand()) and one 0x5c5/5
// (0x52a16e, scale 2) also owned by it (0x52a134's ebx), which at birth reads its position for its start (0x5403ad)
// and draws two rand(). Replayed in a twin pool with the same CrtRandom; the point, from the ASM (FSIN and FCOS of the
// turn times the integer radii, retained, and ((z + 48) − life) + height). With the particles full, only its own
// rand(); below 50 the alpha drops 0.1f.
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
            // The 0x5c5/6 are born where the 0x140 was (0x529f5b, before following the bone); the 0x5c5/5, after.
            const glm::vec3 before = mark.transform.position;
            CrtRandom replay = tested.random;
            const auto stepped = Client::Effects::StepHighModelEffect(mark, nullptr, tested.StepContext());
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            static_cast<void>(replay.Next()); // 0x5208c5, the step's light.
            const Client::Particles::ParticleOwner markOwner{Client::Particles::ParticleOwnerKind::ModelEffect, 0};
            auto twin = std::make_unique<Client::Particles::Particles>();
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
                const auto sine = Client::Math::Detail::SineDoubleProductPC53(turn, side);
                const int depth = replay.Next() % 26 - 13;
                const auto cosine = Client::Math::Detail::CosineDoubleProductPC53(turn, depth);
                const int lift = replay.Next() % 5;
                ASSERT_TRUE(sine.has_value() && cosine.has_value());
                const glm::vec3 point{static_cast<float>(*sine + static_cast<double>(at.x)),
                                      static_cast<float>(*cosine + static_cast<double>(at.y)),
                                      static_cast<float>(((static_cast<double>(at.z) + 48.0) - 45.0) + lift)};
                // With the seed (thirty creation rand() and the light's before): turn 132, radii −4 and 6 and height 1,
                // with the 0x140 at (1100, 2100, 180). The bits, computed separately with the x87 emulator (exact sin
                // and cos rounded to 64 bits, product by the radius and sum with RN at 53, fstp at 24): x = FSIN(132) ·
                // −4 + 1100, y = FCOS(132) · 6 + 2100 and z = ((180 + 48) − 45) + 1.
                EXPECT_EQ(turn, 132.0f);
                EXPECT_EQ(side, -4);
                EXPECT_EQ(depth, 6);
                EXPECT_EQ(lift, 1);
                EXPECT_EQ(at, (glm::vec3{1100.0f, 2100.0f, 180.0f}));
                EXPECT_EQ(std::bit_cast<std::uint32_t>(point.x), 0x44897935u);
                EXPECT_EQ(std::bit_cast<std::uint32_t>(point.y), 0x45039fddu);
                EXPECT_EQ(std::bit_cast<std::uint32_t>(point.z), 0x43380000u);
                Client::Particles::PortableParticleTrigonometry trigonometry;
                ASSERT_TRUE(twin->CreateTowardChecked(0x5c5, point, mark.transform.angles, mark.light, 5, 2.0f,
                                                      std::cref(tested.clock), replay, trigonometry, markOwner, at)
                                .HasValue());
            }
            EXPECT_EQ(tested.random.State(), replay.State());
            const int subtype = life == 122 ? 6 : 5;
            const auto resolved = Client::Effects::ParticleOwnerStatesOf(*tested.world)(markOwner);
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
                    EXPECT_EQ(particle.start, at); // 0x5403ad: the 0x140's position at birth.
                    EXPECT_EQ(particle.scale, 2.0f);
                }
            }
            EXPECT_EQ(mark.alpha, life < 50 ? static_cast<float>(0.5 - static_cast<double>(0.1f)) : 0.5f);
        }
    }
}

// 0x51cdc9..0x51cebe: the 0x125 rises 100, draws the turn and the radius and leaves joint 0x6ee/0x12 owned by it with
// key 0 at (FSIN(turn) · radius + x, FCOS(turn) · radius + y, 0); then the angles at (−30, 0, rand() % 360). The
// 0x6ee/0x12 (SpiritJoints.cpp, 0x533539) draws no rand(): with a slot or with the joints full, three rand().
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

// 0x51ce4f..0x51ce7d: with the seed, turn 211 and radius 645. FSIN and FCOS of 211 (radians, not degrees) at x87
// precision, times the radius plus x and y in double, stored as float: the bits another run gives (exact Fraction and
// the sine with 80 digits), not SineProductPC53's. z is 0.
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

// 0x539494..0x5394bb: the 0x6ee/0x12's step (after the common move of 50 along its angles and its turn) sets the
// 0x125's alpha to 1 with life above 5 (with 5, no) and copies its position to it; then (0x5394be..0x5394fb) it leaves
// particle 0x5f2/0x13 with scale 7 and light 1 at its position and goes on to the tail (life, from 39 to 38). The
// particle is repeated in a twin pool with the same CrtRandom.
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
            Client::Joints::StepJoints(joints, tested.random, nullptr, &tested.world->Particles(), 0.0f, nullptr);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(spirit.alpha, life > 5 ? 1.0f : 0.0f);
        EXPECT_EQ(spirit.transform.position, joints[0].position);
        // With the request's angles at 0, the common move is (0, −50, 0).
        EXPECT_EQ(joints[0].position, (glm::vec3{born.x, born.y - 50.0f, 0.0f}));
        EXPECT_EQ(joints[0].speed, 53.0f);
        EXPECT_EQ(joints[0].life, 38);
        const auto twin =
            TwinParticle(replay, 0x5f2, 0x13, joints[0].position, joints[0].angles, glm::vec3(1.0f), 7.0f);
        const auto& flame = tested.world->Particles().All()[0];
        ASSERT_TRUE(flame.alive);
        EXPECT_EQ(flame.type, 0x5f2);
        EXPECT_EQ(flame.subtype, 0x13);
        // The request's scale is 7; the particle's is drawn by its creation (the twin repeats it).
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

// 0x539249..0x5392e6: the 0x126's 0x613/0xf (life 20, under 35) leaves the 0x5d9/7 and, with rand() & 1 at 1, releases
// nothing; with an owner it sets the 0x126's angles' x and y to 0, alpha to 1 (life above 5) and the position to the
// joint's plus (FSIN(z) · k, −FCOS(z) · k, 0) with k = 30 · 0.1f + 30 in float (0x53929a); then the joint advances
// with k retained. At (10, 20) and with the 0x126's angles' z at 29, z = 29 · 0.01745329f (0x3f0192c0) and the float k
// separates the two: the 0x126 ends at x 0x41cffd5f and the joint at 0x41cffd60 (with the retained k the 0x126 would
// give the joint's); y is 0xc10dcc99 in both. The bits come from a separate computation (exact sine and cosine at 64
// bits).
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
        Client::Joints::StepJoints(joints, tested.random, nullptr, &tested.world->Particles(), 0.0f, nullptr);

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

    // With the 0x126's life at 5, the alpha does not change.
    phoenix.life = 5;
    phoenix.alpha = 0.25f;
    ASSERT_TRUE(Client::Joints::StepJoints(joints, tested.random, nullptr, &tested.world->Particles(), 0.0f, nullptr)
                    .HasValue());
    EXPECT_EQ(phoenix.alpha, 0.25f);
}

// 0x5276b7..0x527787: the 0x125's step turns rand() % 10 + 10; between z 300 and 600, with rand() & 1 at 0, the
// ownerless joint 0x6ee/0xe, width 100 and key 0, at (x, y, 350) with turn (rand() % 24) · 30. Outside that band, a
// single rand(). With the seed, the creation draws three rand() (with the joints full) and the step starts with the
// local light's; with four more before the step, the turn is the ninth (2629: 19), the coin the tenth (22786, even)
// and the ray the eleventh (11018 % 24 = 2: 60 degrees).
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
        ASSERT_TRUE(Client::Effects::StepHighModelEffect(spirit, nullptr, tested.StepContext()).HasValue());
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
        // It asks for angles (0, 0, 60); the 0x6ee/0xe's creation sets x to 20 (0x5334bb).
        EXPECT_EQ(fan.angles, (glm::vec3{20.0f, 0.0f, 60.0f}));
        EXPECT_EQ(fan.width, 100.0f);
        EXPECT_EQ(fan.key, 0);
    }
}

// 0x518f38..0x518f90: the 0x128/0 and the /1 live 30, no rand(); the /2 sets life to 40 and the key to 0 and re-enters
// 0x518cf0 as the 0x127/0 (0x51cf6d) owned by it, with its position, angles and light, the key at −1 and the case's
// scale (1.8f); with the table full after the 0x128, no 0x127. Another subtype, only the common part.
TEST(HighEffectFamiliesTest, TheKundunCallLivesThirtyStepsOrCallsItsEcho)
{
    for (const std::int32_t subtype : {0, 1, 2, 3})
    {
        SCOPED_TRACE(subtype);
        FamilyWorld tested;
        const auto made = CreateHighModelEffect({.type = 0x128,
                                                 .position = {1000.0f, 2000.0f, 300.0f},
                                                 .angles = {0.0f, 0.0f, 90.0f},
                                                 .light = {0.2f, 0.4f, 0.6f},
                                                 .subtype = subtype},
                                                tested.Context());
        ASSERT_TRUE(made.HasValue()) << made.Error();
        const ModelEffect& call = tested.Effect(0);
        EXPECT_EQ(tested.Draws(), 0);
        EXPECT_TRUE(call.alive);
        if (subtype == 2)
        {
            EXPECT_EQ(call.life, 40);
            EXPECT_EQ(call.disturbanceState, 0);
            const ModelEffect& echo = tested.Effect(1);
            ASSERT_TRUE(echo.alive);
            EXPECT_EQ(echo.type, 0x127);
            EXPECT_EQ(echo.subtype, 0);
            EXPECT_EQ(echo.ownerContext, &call);
            EXPECT_EQ(echo.owner.effect, &call);
            EXPECT_EQ(echo.transform.position, (glm::vec3{1000.0f, 2000.0f, 300.0f}));
            EXPECT_EQ(echo.transform.angles, (glm::vec3{0.0f, 0.0f, 90.0f}));
            EXPECT_EQ(echo.light, glm::vec3(0.5f));
            EXPECT_EQ(echo.life, 200);
            EXPECT_EQ(std::bit_cast<std::uint32_t>(echo.speed), 0x3da3d70au);
            EXPECT_EQ(std::bit_cast<std::uint32_t>(echo.transform.scale), 0x3fe66666u);
            EXPECT_EQ(echo.objectState.blendMesh, -2);
            EXPECT_EQ(echo.disturbanceState, 0xffff);
            EXPECT_FALSE(tested.Effect(2).alive);
            continue;
        }
        EXPECT_EQ(call.disturbanceState, 0xffff);
        EXPECT_FALSE(tested.Effect(1).alive);
        if (subtype != 3)
        {
            EXPECT_EQ(call.life, 30);
        }
    }
    {
        // The 0x128/2 in the last free slot: the 0x127 finds no other and is not born.
        FamilyWorld tested;
        tested.FillTheEffects();
        tested.Effect(9).alive = false;
        ASSERT_TRUE(CreateHighModelEffect({.type = 0x128, .subtype = 2}, tested.Context()).HasValue());
        EXPECT_EQ(tested.Effect(9).type, 0x128);
        EXPECT_EQ(tested.Effect(9).life, 40);
        EXPECT_EQ(std::ranges::count_if(tested.world->Map().effects.Effects(),
                                        [](const ModelEffect& effect) { return effect.type == 0x127; }),
                  0);
    }

    // The creation of 0x43d051 and 0x43d0fd, without the world: the same; with the table full, nothing.
    FamilyWorld tested;
    auto& effects = tested.world->Map().effects;
    const auto made = Client::Effects::CreateTheKundunCall(effects, {.type = 0x128, .subtype = 1});
    ASSERT_TRUE(made.HasValue()) << made.Error();
    EXPECT_EQ(made.Value(), std::optional<std::size_t>{std::size_t{0}});
    EXPECT_EQ(tested.Effect(0).life, 30);
    tested.FillTheEffects();
    const auto full = Client::Effects::CreateTheKundunCall(effects, {.type = 0x128});
    ASSERT_TRUE(full.HasValue()) << full.Error();
    EXPECT_FALSE(full.Value().has_value());
    EXPECT_FALSE(Client::Effects::CreateTheKundunCall(effects, {.type = 0x125}).HasValue());
}

// 0x527ad6..0x527b4a: the 0x128/0's step at life 30 releases ten 0x126 (i = 0, 8, …, 72) at its position, with angles
// (0, 0, i + z + 320) in a single fstp (with z = 15.3f, from 0x43a7a666 for i = 0 to 0x43cba666 for i = 72, computed
// separately); each 0x126 (0x51cebf) sets its fields and, with the joints full, draws rand() % 5 for the frame. Eleven
// rand() with the local light's. With the joints free, each leaves its 0x613/0xf owned by it before the frame
// (0x51cf45), no rand(). At life 29, nothing.
TEST(HighEffectFamiliesTest, TheKundunCallSendsTenPhoenixesAtLifeThirty)
{
    for (const bool full : {true, false})
    {
        SCOPED_TRACE(full);
        FamilyWorld tested;
        if (full)
            tested.FillTheJoints();
        const glm::vec3 origin{1000.0f, 2000.0f, 149.0f};
        const glm::vec3 angles{0.0f, 0.0f, std::bit_cast<float>(0x4174cccdu)}; // 15.3f
        const glm::vec3 light{0.25f, 0.5f, 0.75f};
        ASSERT_TRUE(CreateHighModelEffect({.type = 0x128, .position = origin, .angles = angles, .light = light},
                                          tested.Context())
                        .HasValue());
        ModelEffect& call = tested.Effect(0);
        const auto stepped = Client::Effects::StepHighModelEffect(call, nullptr, tested.StepContext());
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        if (!full)
        {
            // Each 0x126 leaves its 0x613/0xf owned by it, no rand() (0x5334c7), before the frame's rand().
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
        ASSERT_TRUE(Client::Effects::StepHighModelEffect(call, nullptr, tested.StepContext()).HasValue());
        EXPECT_EQ(tested.Draws(), 12);
        EXPECT_FALSE(tested.Effect(11).alive);
    }
}

// 0x527b4f..0x527c22: the 0x128/1 at life 30 releases twenty 0x125 and, with the key (+0x8a, MOVSX) between 6 and 9,
// one more per step; the key rises one per step from the creation's −1. With the joints full each 0x125 draws three
// rand(); in steps 8 to 11 (key 6 to 9), four more. With the joints free, the same rand() and one 0x6ee/0x12 per 0x125.
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
    ASSERT_TRUE(Client::Effects::StepHighModelEffect(call, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(spirits(), 20);
    EXPECT_EQ(tested.Draws(), 1 + 20 * 3);
    EXPECT_EQ(call.disturbanceState, 0);
    EXPECT_EQ(tested.Effect(1).transform.position, origin + glm::vec3(0.0f, 0.0f, 100.0f));
    int draws = tested.Draws();
    for (int step = 2; step <= 30; ++step)
    {
        SCOPED_TRACE(step);
        ASSERT_TRUE(Client::Effects::StepHighModelEffect(call, nullptr, tested.StepContext()).HasValue());
        const bool extra = step >= 8 && step <= 11;
        draws += extra ? 4 : 1;
        EXPECT_EQ(tested.Draws(), draws);
    }
    EXPECT_EQ(spirits(), 24);
    EXPECT_FALSE(call.alive);

    // With the joints free, each 0x125 leaves its 0x6ee/0x12 owned by it (0x51ce91), with no extra rand().
    FamilyWorld free;
    ASSERT_TRUE(CreateHighModelEffect({.type = 0x128, .subtype = 1}, free.Context()).HasValue());
    ASSERT_TRUE(Client::Effects::StepHighModelEffect(free.Effect(0), nullptr, free.StepContext()).HasValue());
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

// 0x527c27..0x527c51: the 0x128/2 with the key at 0 and life below 10 sets the key to 1.
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
        ASSERT_TRUE(Client::Effects::StepHighModelEffect(echo, nullptr, tested.StepContext()).HasValue());
        EXPECT_EQ(echo.disturbanceState, life < 10 ? 1 : 0);
        EXPECT_EQ(tested.Draws(), 1);
    }
}

// 0x52778c..0x5277a9: the 0x126 with life below 5 sets alpha to life · 0.2f; with 5 or more, nothing.
TEST(HighEffectFamiliesTest, ThePhoenixFadesInItsLastFourSteps)
{
    FamilyWorld tested;
    tested.FillTheJoints();
    ASSERT_TRUE(CreateHighModelEffect({.type = 0x126}, tested.Context()).HasValue());
    EXPECT_EQ(tested.Draws(), 1);
    ModelEffect& phoenix = tested.Effect(0);
    phoenix.life = 5;
    ASSERT_TRUE(Client::Effects::StepHighModelEffect(phoenix, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(phoenix.alpha, 0.0f);
    EXPECT_EQ(phoenix.life, 4);
    ASSERT_TRUE(Client::Effects::StepHighModelEffect(phoenix, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(phoenix.alpha, static_cast<float>(4.0 * static_cast<double>(std::bit_cast<float>(0x3e4ccccdu))));
    ASSERT_TRUE(Client::Effects::StepHighModelEffect(phoenix, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(phoenix.alpha, static_cast<float>(3.0 * static_cast<double>(std::bit_cast<float>(0x3e4ccccdu))));
    EXPECT_EQ(tested.Draws(), 4);
}

// `made` from the world and `expected` from the twin pool, field by field.
void ExpectSameParticle(const Client::Particles::Particle& made, const Client::Particles::Particle& expected)
{
    EXPECT_TRUE(made.alive);
    EXPECT_EQ(made.type, expected.type);
    EXPECT_EQ(made.subtype, expected.subtype);
    EXPECT_EQ(made.position, expected.position);
    EXPECT_EQ(made.angles, expected.angles);
    EXPECT_EQ(made.light, expected.light);
    EXPECT_EQ(made.scale, expected.scale);
}

// 0x51e397..0x51e3eb: skill 0x37's 0x5be is born with life 30, light at 0.8f, the copied angles at (0, 0, z + 45)
// (15.3f + 45 in one fstp: 0x42713333, computed separately) and the start at the position, no rand(). The step
// (0x52a6d6..0x52a732) at life 30 leaves joint 0x61c/0 at (x, y, z + 100) with the copied angles, the effect's owner,
// key and skill and width 150, repeated in a twin pool with the same CrtRandom; at 29, only the local light's rand().
TEST(HighEffectFamiliesTest, TheSweepEmitterLeavesItsJointAtLifeThirty)
{
    FamilyWorld tested;
    ActorState& caster = tested.world->Actors()[3];
    const glm::vec3 origin{100.0f, 200.0f, 300.0f};
    const auto made = CreateHighModelEffect({.type = Client::Effects::SweepEmitterEffect,
                                             .position = origin,
                                             .angles = {1.0f, 2.0f, std::bit_cast<float>(0x4174cccdu)},
                                             .owner = &caster,
                                             .key = 7,
                                             .byte89 = 3},
                                            tested.Context());
    ASSERT_TRUE(made.HasValue()) << made.Error();
    ModelEffect& sweep = tested.Effect(0);
    EXPECT_EQ(sweep.life, 30);
    EXPECT_EQ(sweep.light, glm::vec3(std::bit_cast<float>(0x3f4ccccdu)));
    EXPECT_EQ(sweep.copiedAngles.x, 0.0f);
    EXPECT_EQ(sweep.copiedAngles.y, 0.0f);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(sweep.copiedAngles.z), 0x42713333u);
    EXPECT_EQ(sweep.start, origin);
    EXPECT_EQ(tested.Draws(), 0);

    const auto stepped = Client::Effects::StepHighModelEffect(sweep, nullptr, tested.StepContext());
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    CrtRandom replay(Seed);
    static_cast<void>(replay.Next());
    glm::vec3 point{100.0f, 200.0f, 400.0f};
    glm::vec3 angles{0.0f, 0.0f, std::bit_cast<float>(0x42713333u)};
    std::vector<Client::Kernel::JointState> twin(tested.world->DrawTail().joints.size());
    ASSERT_TRUE(Client::Joints::CreateFaunaJoint(twin,
                                                 {.type = 0x61c,
                                                  .firstPosition = point,
                                                  .secondPosition = point,
                                                  .angles = angles,
                                                  .subtype = 0,
                                                  .ownerContext = &caster,
                                                  .scale = 150.0f,
                                                  .key = 7,
                                                  .skill = 3,
                                                  .skillSerial = 0,
                                                  .target = -1},
                                                 replay)
                    .HasValue());
    EXPECT_EQ(tested.random.State(), replay.State());
    const auto& joint = tested.world->DrawTail().joints[0];
    ASSERT_TRUE(joint.alive);
    EXPECT_EQ(joint.type, 0x61c);
    EXPECT_EQ(joint.subtype, 0);
    EXPECT_EQ(joint.ownerContext, &caster);
    EXPECT_EQ(joint.ownerKind, Client::Kernel::JointOwnerKind::ByType);
    EXPECT_EQ(joint.width, 150.0f);
    EXPECT_EQ(joint.key, 7);
    EXPECT_EQ(joint.skill, 3);
    EXPECT_EQ(joint.position, twin[0].position);
    EXPECT_EQ(joint.angles, twin[0].angles);
    EXPECT_EQ(joint.targetPosition, twin[0].targetPosition);
    EXPECT_FALSE(tested.world->DrawTail().joints[1].alive);
    EXPECT_EQ(sweep.life, 29);

    ASSERT_TRUE(Client::Effects::StepHighModelEffect(sweep, nullptr, tested.StepContext()).HasValue());
    static_cast<void>(replay.Next());
    EXPECT_EQ(tested.random.State(), replay.State());
    EXPECT_FALSE(tested.world->DrawTail().joints[1].alive);
}

// 0x51e529..0x51e569: skill 0x11's 0x5ca is born with life 20, speed (0, −60, 0) and z 100 more; the /1, with scale
// 1.5f. The step (0x52abbe..0x52ac75) at life 17: level 17 · 0.2f (0x4059999a) is the /0's color; particles 0x5ca/0
// (scale 1) and 0x5c6/0 (scale 4), repeated in a twin pool; the light (level · 0.2f; level · 0.4f; level) with radius 2
// (0x3f2e147b and 0x3fae147b, computed separately). With the owner more than 100 away on the plane it goes on; at
// exactly 100, alive and seen (0x518b60), life at 1 (the tail leaves it at 0) and the white 0x5c6/1 with scale 6.
TEST(HighEffectFamiliesTest, TheEnergyBallGlowsAndBurstsOnItsOwner)
{
    for (const bool near : {false, true})
    {
        SCOPED_TRACE(near);
        FamilyWorld tested;
        ActorState& victim = tested.world->Actors()[4];
        victim.alive = true;
        victim.seen = true;
        victim.position = {near ? 1060.0f : 1061.0f, 1920.0f, 0.0f};
        const glm::vec3 angles{0.0f, 0.0f, 30.0f};
        const auto made = CreateHighModelEffect({.type = Client::Effects::EnergyBallEffect,
                                                 .position = {1000.0f, 2000.0f, 50.0f},
                                                 .angles = angles,
                                                 .owner = &victim},
                                                tested.Context());
        ASSERT_TRUE(made.HasValue()) << made.Error();
        ModelEffect& ball = tested.Effect(0);
        EXPECT_EQ(ball.life, 20);
        EXPECT_EQ(ball.velocity, (glm::vec3{0.0f, -60.0f, 0.0f}));
        const glm::vec3 point{1000.0f, 2000.0f, 150.0f};
        EXPECT_EQ(ball.transform.position, point);
        EXPECT_EQ(tested.Draws(), 0);
        ball.life = 17;
        const auto stepped = Client::Effects::StepHighModelEffect(ball, nullptr, tested.StepContext());
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();

        CrtRandom replay(Seed);
        static_cast<void>(replay.Next());
        const glm::vec3 level(std::bit_cast<float>(0x4059999au));
        const auto& particles = tested.world->Particles().All();
        ExpectSameParticle(particles[0], TwinParticle(replay, 0x5ca, 0, point, angles, level, 1.0f));
        ExpectSameParticle(particles[1], TwinParticle(replay, 0x5c6, 0, point, angles, level, 4.0f));
        if (near)
            ExpectSameParticle(particles[2], TwinParticle(replay, 0x5c6, 1, point, angles, glm::vec3(1.0f), 6.0f));
        EXPECT_FALSE(particles[near ? 3 : 2].alive);
        EXPECT_EQ(tested.random.State(), replay.State());
        auto expected = std::make_unique<Client::Terrain::TerrainLighting>();
        ASSERT_TRUE(
            Client::Terrain::AddDynamicLight(expected.get(), point,
                                             {std::bit_cast<float>(0x3f2e147bu), std::bit_cast<float>(0x3fae147bu),
                                              std::bit_cast<float>(0x4059999au)},
                                             2, "test")
                .HasValue());
        EXPECT_EQ(tested.lighting->cells, expected->cells);
        EXPECT_EQ(ball.life, near ? 0 : 16);
        EXPECT_EQ(ball.alive, !near);
    }
}

// The 0x5ca's /1 is born with scale 1.5f and releases its particles at (1; 0.8f; 0.2f); an owner that is not one of
// the world's characters stops at 0x518b60 (0x518b7a), after the particles.
TEST(HighEffectFamiliesTest, TheOrangeEnergyBallAndTheOwnerOutsideTheWorld)
{
    FamilyWorld tested;
    ActorState stranger;
    ASSERT_TRUE(CreateHighModelEffect({.type = Client::Effects::EnergyBallEffect, .subtype = 1, .owner = &stranger},
                                      tested.Context())
                    .HasValue());
    ModelEffect& ball = tested.Effect(0);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(ball.transform.scale), 0x3fc00000u);
    const auto stepped = Client::Effects::StepHighModelEffect(ball, nullptr, tested.StepContext());
    EXPECT_TRUE(HasError(stepped, "0x518b7a"));
    const glm::vec3 orange{1.0f, std::bit_cast<float>(0x3f4ccccdu), std::bit_cast<float>(0x3e4ccccdu)};
    EXPECT_EQ(tested.world->Particles().All()[0].light, orange);
    EXPECT_EQ(tested.world->Particles().All()[1].light, orange);
}

// 0x51e773..0x51e7d7: the 0x33's 0x5da is born with life 10 and (0, −60, 0) rotated by its angles plus the position,
// with z 130 more: with zero angles, (100, 140, 430). The step (0x52acc3..0x52b3b1): the white 0x5da/1 particle with
// scale 1 (repeated in a twin pool) and the light (L; L · 0.6f; L · 0.3f) with radius 2, with L the local light; its
// bits, for each value of rand() & 3, computed separately.
TEST(HighEffectFamiliesTest, TheFlameBurstLeavesItsFlameAndItsLight)
{
    constexpr std::array<std::array<std::uint32_t, 3>, 4> Lights{{{0x3f333333u, 0x3ed70a3eu, 0x3e570a3eu},
                                                                  {0x3f4ccccdu, 0x3ef5c290u, 0x3e75c290u},
                                                                  {0x3f666667u, 0x3f0a3d71u, 0x3e8a3d71u},
                                                                  {0x3f800000u, 0x3f19999au, 0x3e99999au}}};
    FamilyWorld tested;
    ASSERT_TRUE(CreateHighModelEffect({.type = Client::Effects::FlameBurstEffect, .position = {100.0f, 200.0f, 300.0f}},
                                      tested.Context())
                    .HasValue());
    ModelEffect& burst = tested.Effect(0);
    EXPECT_EQ(burst.life, 10);
    const glm::vec3 point{100.0f, 140.0f, 430.0f};
    EXPECT_EQ(burst.transform.position, point);
    EXPECT_EQ(tested.Draws(), 0);
    const auto stepped = Client::Effects::StepHighModelEffect(burst, nullptr, tested.StepContext());
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    CrtRandom replay(Seed);
    const auto& light = Lights[replay.Next() & 3];
    ExpectSameParticle(tested.world->Particles().All()[0],
                       TwinParticle(replay, 0x5da, 1, point, glm::vec3(0.0f), glm::vec3(1.0f), 1.0f));
    EXPECT_EQ(tested.random.State(), replay.State());
    auto expected = std::make_unique<Client::Terrain::TerrainLighting>();
    ASSERT_TRUE(Client::Terrain::AddDynamicLight(
                    expected.get(), point,
                    {std::bit_cast<float>(light[0]), std::bit_cast<float>(light[1]), std::bit_cast<float>(light[2])}, 2,
                    "test")
                    .HasValue());
    EXPECT_EQ(tested.lighting->cells, expected->cells);
    EXPECT_EQ(burst.life, 9);
    // With the angles' z at 45 (0x5fc8f0: sin and cos of 45 · 0.0174533f in the x87, stored as float), (0, −60, 0)
    // rotated gives (60 · sin, −60 · cos, 0): the position at (142.4264…; 157.5735…; 430), bits computed separately.
    FamilyWorld turned;
    ASSERT_TRUE(CreateHighModelEffect({.type = Client::Effects::FlameBurstEffect,
                                       .position = {100.0f, 200.0f, 300.0f},
                                       .angles = {0.0f, 0.0f, 45.0f}},
                                      turned.Context())
                    .HasValue());
    const ModelEffect& aimed = turned.Effect(0);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(aimed.transform.position.x), 0x430e6d29u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(aimed.transform.position.y), 0x431d92d7u);
    EXPECT_EQ(aimed.transform.position.z, 430.0f);
    EXPECT_EQ(aimed.transform.angles, (glm::vec3{0.0f, 0.0f, 45.0f}));
}

// The skill 0x2e's 0x13a in the table, as its creation leaves it (Effects/ArrowEffects).
ModelEffect& ArrowBlast(FamilyWorld& tested, void* target, std::int32_t subtype, std::int32_t life)
{
    const auto slot = tested.world->Map().effects.Begin(
        {.type = Client::Effects::ArrowBlastEffect, .subtype = subtype, .owner = target});
    EXPECT_EQ(slot, std::optional<std::size_t>(0));
    ModelEffect& blast = tested.Effect(0);
    blast.life = life;
    blast.transform.angles.x = -30.0f;
    blast.velocity.y = -30.0f;
    return blast;
}

// 0x529dee..0x529f07: the 0x13a lowers the angles' x by 5 and the speed's y by 8. With life below 2, the /0 and the
// target as owner: the angles' x at 90, the /1 and joints 0x632/2 and /3 over the target (rand() % 100 − 50 in x and
// y); with the joints full, those two rand() and the tail kills it; with a slot, both joints. With the /1, the slot
// free; an owner outside the world's characters stops (0x529e3e).
TEST(HighEffectFamiliesTest, TheArrowBlastFallsAndCallsTheRainOverItsTarget)
{
    {
        FamilyWorld tested;
        ModelEffect& blast = ArrowBlast(tested, nullptr, 0, 5);
        ASSERT_TRUE(Client::Effects::StepHighModelEffect(blast, nullptr, tested.StepContext()).HasValue());
        EXPECT_EQ(blast.transform.angles.x, -35.0f);
        EXPECT_EQ(blast.velocity.y, -38.0f);
        EXPECT_EQ(blast.subtype, 0);
        EXPECT_EQ(blast.life, 4);
        EXPECT_TRUE(blast.alive);
        EXPECT_EQ(tested.Draws(), 1);
    }
    for (const bool room : {false, true})
    {
        SCOPED_TRACE(room);
        FamilyWorld tested;
        if (!room)
            tested.FillTheJoints();
        ActorState& target = tested.world->Actors()[2];
        target.position = {500.0f, 600.0f, 70.0f};
        ModelEffect& blast = ArrowBlast(tested, &target, 0, 1);
        const auto stepped = Client::Effects::StepHighModelEffect(blast, nullptr, tested.StepContext());
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(blast.transform.angles.x, 90.0f);
        EXPECT_EQ(blast.subtype, 1);
        EXPECT_FALSE(blast.alive);
        EXPECT_EQ(blast.life, 0);
        if (!room)
        {
            EXPECT_EQ(tested.Draws(), 3);
            continue;
        }
        // 0x529e62..0x529eed: the local light, rand() % 100 − 50 in x, in y, z 1200 more and the two 0x632 (/2 and /3)
        // owned by the 0x13a, width 50 and key −1, repeated in a twin pool with the same CrtRandom (one rand() each).
        CrtRandom replay(Seed);
        static_cast<void>(replay.Next());
        const auto dx = static_cast<double>(replay.Next() % 100) - 50.0;
        const auto dy = static_cast<double>(replay.Next() % 100) - 50.0;
        const glm::vec3 point{static_cast<float>(dx + 500.0), static_cast<float>(dy + 600.0), 1270.0f};
        std::vector<Client::Kernel::JointState> twin(tested.world->DrawTail().joints.size());
        for (const std::int32_t subtype : {2, 3})
        {
            glm::vec3 first = point, second = point, angles{90.0f, 0.0f, 0.0f};
            ASSERT_TRUE(Client::Joints::CreateFaunaJoint(twin,
                                                         {.type = 0x632,
                                                          .firstPosition = first,
                                                          .secondPosition = second,
                                                          .angles = angles,
                                                          .subtype = subtype,
                                                          .ownerContext = &blast,
                                                          .scale = 50.0f,
                                                          .key = -1,
                                                          .skill = 0,
                                                          .skillSerial = 0,
                                                          .target = -1},
                                                         replay, Client::Kernel::JointOwnerKind::ModelEffect)
                            .HasValue());
        }
        EXPECT_EQ(tested.random.State(), replay.State());
        EXPECT_EQ(tested.Draws(), 5);
        const auto& joints = tested.world->DrawTail().joints;
        for (std::size_t slot = 0; slot < 2; ++slot)
        {
            SCOPED_TRACE(slot);
            const auto& joint = joints[slot];
            ASSERT_TRUE(joint.alive);
            EXPECT_EQ(joint.type, 0x632);
            EXPECT_EQ(joint.subtype, static_cast<std::int32_t>(2 + slot));
            EXPECT_EQ(joint.position, point);
            EXPECT_EQ(joint.position, twin[slot].position);
            EXPECT_EQ(joint.angles, twin[slot].angles);
            EXPECT_EQ(joint.width, 50.0f);
            EXPECT_EQ(joint.texture, twin[slot].texture);
            EXPECT_EQ(joint.speed, 30.0f);
            EXPECT_EQ(joint.orientation, twin[slot].orientation);
            EXPECT_EQ(joint.ownerContext, &blast);
        }
        EXPECT_FALSE(joints[2].alive);
    }
    {
        FamilyWorld tested;
        ActorState stranger;
        ModelEffect& blast = ArrowBlast(tested, &stranger, 0, 1);
        EXPECT_TRUE(HasError(Client::Effects::StepHighModelEffect(blast, nullptr, tested.StepContext()), "0x529e3e"));
        EXPECT_EQ(tested.Draws(), 1);
        // 0x529e31 and 0x529e37 write before reading the owner at 0x529e3e.
        EXPECT_EQ(std::bit_cast<std::uint32_t>(blast.transform.angles.x), 0x42b40000u);
        EXPECT_EQ(blast.subtype, 1);
    }
    {
        FamilyWorld tested;
        ModelEffect& blast = ArrowBlast(tested, nullptr, 1, 10);
        ASSERT_TRUE(Client::Effects::StepHighModelEffect(blast, nullptr, tested.StepContext()).HasValue());
        EXPECT_FALSE(blast.alive);
        EXPECT_EQ(blast.life, 9);
    }
}

double FlatGround(float, float)
{
    return 123.5;
}

// 0x51e234..0x51e348: the Kundun fall's pieces by subtype, with the terrain height at 123.5 and the key at −130 (−10
// for the /4): the /1 lives 100; the /2 and the /3, 500, with the start at (x, y, 123.5 + 130 − 80) and the copied x
// at rand() % 2 (the /2, at 0); the /4, 170 with the start at (x, y, 123.5 + 10) and the action at 0; the /5, 170.
// Scale 2 and the key at 0xffff in all. Without the height (CreateHighModelEffect), the /2 and the /4 stop; another
// type too.
TEST(HighEffectFamiliesTest, TheKundunPiecesSettleByTheirSubtype)
{
    for (std::int32_t subtype = 1; subtype <= 5; ++subtype)
    {
        SCOPED_TRACE(subtype);
        FamilyWorld tested;
        const std::int16_t key = subtype == 4 ? -10 : -130;
        const glm::vec3 origin{1000.0f, 2000.0f, 900.0f};
        const auto made = Client::Effects::CreateTheKundunPiece(
            tested.world->Map().effects,
            {.type = 0x11f, .position = origin, .subtype = subtype, .key = std::bit_cast<std::uint16_t>(key)},
            tested.random, FlatGround);
        ASSERT_TRUE(made.HasValue()) << made.Error();
        ASSERT_EQ(made.Value(), std::optional<std::size_t>(0));
        const ModelEffect& piece = tested.Effect(0);
        EXPECT_EQ(piece.transform.scale, 2.0f);
        EXPECT_EQ(piece.disturbanceState, 0xffff);
        CrtRandom replay(Seed);
        switch (subtype)
        {
        case 1:
            EXPECT_EQ(piece.life, 100);
            break;
        case 2:
        case 3:
            EXPECT_EQ(piece.life, 500);
            EXPECT_EQ(piece.start, (glm::vec3{1000.0f, 2000.0f, 173.5f}));
            EXPECT_EQ(piece.velocity, glm::vec3(0.0f));
            EXPECT_EQ(piece.copiedAngles.x, subtype == 2 ? 0.0f : static_cast<float>(replay.Next() % 2));
            if (subtype == 2)
                static_cast<void>(replay.Next());
            break;
        default:
            EXPECT_EQ(piece.life, 170);
            EXPECT_EQ(piece.animation.action, 0);
            if (subtype == 4)
            {
                EXPECT_EQ(piece.start, (glm::vec3{1000.0f, 2000.0f, 133.5f}));
            }
            break;
        }
        EXPECT_EQ(tested.random.State(), replay.State());
    }
    FamilyWorld ungrounded;
    EXPECT_TRUE(HasError(CreateHighModelEffect({.type = 0x11f, .subtype = 2}, ungrounded.Context()), "0x51e26e"));
    EXPECT_TRUE(HasError(CreateHighModelEffect({.type = 0x122, .subtype = 4}, ungrounded.Context()), "0x51e2ee"));
    EXPECT_TRUE(CreateHighModelEffect({.type = 0x123, .subtype = 5}, ungrounded.Context()).HasValue());
    EXPECT_TRUE(HasError(Client::Effects::CreateTheKundunPiece(ungrounded.world->Map().effects, {.type = 0x125},
                                                               ungrounded.random, FlatGround),
                         "0x518cf0"));
}

// A Kundun piece in the table, no rand(): the fall creates it with the terrain height.
ModelEffect& KundunPiece(FamilyWorld& tested, std::int32_t subtype, std::int32_t life)
{
    const auto made = Client::Effects::CreateTheKundunPiece(
        tested.world->Map().effects, {.type = 0x120, .position = {1000.0f, 2000.0f, 300.0f}, .subtype = 5},
        tested.random, FlatGround);
    EXPECT_TRUE(made.HasValue());
    ModelEffect& piece = tested.Effect(0);
    piece.subtype = subtype;
    piece.life = life;
    return piece;
}

// 0x52a5fe..0x52a683: the /1 lowers alpha by 0.01f (1 − 0.01f: 0x3f7d70a4, computed separately); the /4 with life 140
// or more sets the frame to 0 and, with less, lowers alpha and, above the start, the speed's z by 3.5 (−6 − 3.5:
// 0xc1180000), or sets it to 0; the /5 with 150 or more sets the frame to 0 and, with less, lowers alpha. Only the
// local light.
TEST(HighEffectFamiliesTest, TheKundunShardsFadeAndSink)
{
    const auto step = [](FamilyWorld& tested, ModelEffect& piece)
    {
        ASSERT_TRUE(Client::Effects::StepHighModelEffect(piece, nullptr, tested.StepContext()).HasValue());
        EXPECT_EQ(tested.Draws(), 1);
    };
    {
        FamilyWorld tested;
        ModelEffect& shard = KundunPiece(tested, 1, 100);
        step(tested, shard);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(shard.alpha), 0x3f7d70a4u);
        EXPECT_EQ(shard.life, 99);
    }
    for (const auto& [subtype, still] : {std::pair{4, 140}, std::pair{5, 150}})
    {
        SCOPED_TRACE(subtype);
        FamilyWorld tested;
        ModelEffect& piece = KundunPiece(tested, subtype, still);
        piece.animation.frame = 3.0f;
        step(tested, piece);
        EXPECT_EQ(piece.animation.frame, 0.0f);
        EXPECT_EQ(piece.alpha, 1.0f);

        FamilyWorld fading;
        ModelEffect& faded = KundunPiece(fading, subtype, still - 1);
        faded.animation.frame = 3.0f;
        faded.start.z = 200.0f;
        faded.velocity.z = -6.0f;
        step(fading, faded);
        EXPECT_EQ(faded.animation.frame, 3.0f);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(faded.alpha), 0x3f7d70a4u);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(faded.velocity.z), subtype == 4 ? 0xc1180000u : 0xc0c00000u);
    }
    FamilyWorld landed;
    ModelEffect& sunk = KundunPiece(landed, 4, 100);
    sunk.start.z = 300.0f;
    sunk.velocity = {1.0f, 2.0f, -6.0f};
    step(landed, sunk);
    EXPECT_EQ(sunk.velocity, glm::vec3(0.0f));
}

// The first seed from `from` whose step of a shaking /3 (the local light's rand() first) draws 0 in rand() % 2, then
// the turn, again 0 in rand() % 2 and 0 in rand() % 3: the angles, the joint and the particle.
std::uint32_t ShakingSeed(std::uint32_t from)
{
    for (std::uint32_t seed = from;; ++seed)
    {
        CrtRandom probe(seed);
        static_cast<void>(probe.Next());
        if (probe.Next() % 2 != 0)
            continue;
        static_cast<void>(probe.Next());
        if (probe.Next() % 2 == 0 && probe.Next() % 3 == 0)
            return seed;
    }
}

// 0x52a4ab..0x52a5f9: the /3 with life from (0x5e − +0x89) · 5 and above 385 shakes: with rand() % 2 at 0, local
// angles at (0, 0, rand() % 360) and, with another rand() % 2 at 0, the ownerless joint 0x6ee/8 with them (width 50,
// key 0); with rand() % 3 at 0, particle 0x5f5/2 with the effect's angles, light 0.5 and scale ((rand() & 31) + 48) ·
// 0.02f. The joint and the particle, repeated in a twin world with the same CrtRandom; with the pools full, the same
// rand(). The /2 does not shake.
TEST(HighEffectFamiliesTest, TheShakingKundunPieceSpitsDustAndSmoke)
{
    const std::uint32_t seed = ShakingSeed(Seed);
    for (const bool full : {false, true})
    {
        SCOPED_TRACE(full);
        FamilyWorld tested;
        tested.random = CrtRandom(seed);
        if (full)
        {
            tested.FillTheJoints();
            tested.FillTheParticles();
        }
        ModelEffect& piece = KundunPiece(tested, 3, 460);
        piece.byte89 = 4;
        const auto stepped = Client::Effects::StepHighModelEffect(piece, nullptr, tested.StepContext());
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        CrtRandom replay(seed);
        static_cast<void>(replay.Next());
        static_cast<void>(replay.Next());
        const glm::vec3 angles{0.0f, 0.0f, static_cast<float>(replay.Next() % 360)};
        static_cast<void>(replay.Next());
        const glm::vec3 point{1000.0f, 2000.0f, 300.0f};
        auto twin = Client::Tests::LentWorld();
        if (full)
            for (auto& joint : twin->DrawTail().joints)
                joint.alive = true;
        ASSERT_TRUE(
            Client::Effects::CreateHighJoint(*twin, replay, 0x6ee, point, point, angles, 8, nullptr, 50.0f, 0x52a57b, 0)
                .HasValue());
        static_cast<void>(replay.Next());
        const auto scale =
            static_cast<float>(static_cast<double>((replay.Next() & 31) + 48) * static_cast<double>(0.02f));
        if (full)
        {
            EXPECT_EQ(tested.random.State(), replay.State());
            continue;
        }
        ExpectSameParticle(tested.world->Particles().All()[0],
                           TwinParticle(replay, 0x5f5, 2, point, glm::vec3(0.0f), glm::vec3(0.5f), scale));
        EXPECT_EQ(tested.random.State(), replay.State());
        EXPECT_EQ(piece.transform.angles, glm::vec3(0.0f));
        const auto& joint = tested.world->DrawTail().joints[0];
        ASSERT_TRUE(joint.alive);
        EXPECT_EQ(joint.type, 0x6ee);
        EXPECT_EQ(joint.subtype, 8);
        EXPECT_EQ(joint.ownerContext, nullptr);
        EXPECT_EQ(joint.width, 50.0f);
        EXPECT_EQ(joint.key, 0);
        EXPECT_EQ(joint.position, twin->DrawTail().joints[0].position);
        EXPECT_EQ(joint.angles, twin->DrawTail().joints[0].angles);
    }
    FamilyWorld rock;
    rock.random = CrtRandom(seed);
    ModelEffect& still = KundunPiece(rock, 2, 460);
    still.byte89 = 4;
    ASSERT_TRUE(Client::Effects::StepHighModelEffect(still, nullptr, rock.StepContext()).HasValue());
    EXPECT_EQ(DrawsSince(seed, rock.random), 1);
}

// 0x52a1ae..0x52a259: below life (0x5e − +0x89) · 5 and above 350, with the speed at 0, x and y at rand() % 2 · 2 − 2
// and z at 0; then z 6 less.
TEST(HighEffectFamiliesTest, TheFallingKundunPieceDrifts)
{
    FamilyWorld tested;
    ModelEffect& piece = KundunPiece(tested, 3, 300);
    piece.byte89 = 4;
    piece.transform.position.z = 500.0f;
    ASSERT_TRUE(Client::Effects::StepHighModelEffect(piece, nullptr, tested.StepContext()).HasValue());
    CrtRandom replay(Seed);
    static_cast<void>(replay.Next());
    const auto x = static_cast<float>((replay.Next() % 2) * 2 - 2);
    const auto y = static_cast<float>((replay.Next() % 2) * 2 - 2);
    EXPECT_EQ(tested.random.State(), replay.State());
    EXPECT_EQ(piece.velocity, (glm::vec3{x, y, -6.0f}));
    piece.velocity.z = -12.0f;
    ASSERT_TRUE(Client::Effects::StepHighModelEffect(piece, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(tested.Draws(), 4);
    EXPECT_EQ(piece.velocity, (glm::vec3{x, y, -18.0f}));
}

// 0x52a26e..0x52a487: at 350 or less, with the speed's z other than −3, the strike: without Kalima, a hundred 0x6f4/2
// particles (0x52a37d: turn, radius and the particle's turn, three rand() each, and its creation's two, 0x543d68) with
// light 0.3f, scale 1 and no owner, the 0x607/4 (x and y, and its creation's two, 0x542d75), the speed at (0, 0, −3)
// and the sway (rand() % 3 + 1 less with the copied x at 0); with the particles full, the loop's three hundred rand()
// and the 0x607/4's two. Already at −3, no strike; with the copied x at 1 and the angles' x at 30, the copied one goes
// back to 0 without rand(); with life under 100, alpha 0.1f less (0x3f666666). The expected values come from kundun.py
// (0x52a26e..0x52a48a, 0x543d68 and 0x542d75 in a separate x87, with the turn's FSIN and FCOS in degrees taken as
// radians).
TEST(HighEffectFamiliesTest, TheKundunPieceStrikesTheGroundOnce)
{
    {
        FamilyWorld tested;
        ModelEffect& piece = KundunPiece(tested, 3, 300);
        piece.byte89 = 4;
        piece.transform.angles = {0.0f, 0.0f, 12.5f};
        piece.copiedAngles.x = 0.0f;
        ASSERT_EQ(piece.transform.position, (glm::vec3{1000.0f, 2000.0f, 300.0f}));
        const auto struck = Client::Effects::StepHighModelEffect(piece, nullptr, tested.StepContext());
        ASSERT_TRUE(struck.HasValue()) << struck.Error();
        // The local light, a hundred of five, the 0x607/4's four and the sway: 506.
        EXPECT_EQ(tested.random.State(), 0xafd45dbbu);
        EXPECT_EQ(tested.Draws(), 506);
        EXPECT_EQ(piece.transform.angles.x, -2.0f);
        EXPECT_EQ(piece.velocity, (glm::vec3{0.0f, 0.0f, -3.0f}));
        struct Debris
        {
            std::size_t slot;
            std::array<std::uint32_t, 2> point; // x and y: sin and cos of the turn times the radius, plus the position
            float tilt;                         // 12.5 + rand() % 180
            float rotation;                     // rand() % 360
            float rise;                         // rand() % 5 + 10
        };
        constexpr std::array<Debris, 4> Expected{{
            {0, {0x447447e4u, 0x44f733fdu}, 101.5f, 351.0f, 12.0f},
            {1, {0x447e787fu, 0x44fe0c87u}, 121.5f, 106.0f, 13.0f},
            {2, {0x4481e7f0u, 0x44f620bbu}, 106.5f, 22.0f, 12.0f},
            {99, {0x4480dc7fu, 0x44fa534au}, 147.5f, 51.0f, 12.0f},
        }};
        const auto particles = tested.world->Particles().All();
        for (std::size_t slot = 0; slot < 100; ++slot)
        {
            SCOPED_TRACE(slot);
            const auto& made = particles[slot];
            ASSERT_TRUE(made.alive);
            EXPECT_EQ(made.type, 0x6f4);
            EXPECT_EQ(made.subtype, 2);
            EXPECT_EQ(made.life, 20);
            EXPECT_EQ(made.scale, 0.5f);
            EXPECT_EQ(made.position.z, 320.0f); // 300 − 30 + 50
            EXPECT_EQ(made.light, glm::vec3(std::bit_cast<float>(0x3e99999au)));
        }
        for (const Debris& debris : Expected)
        {
            SCOPED_TRACE(debris.slot);
            const auto& made = particles[debris.slot];
            EXPECT_EQ(std::bit_cast<std::uint32_t>(made.position.x), debris.point[0]);
            EXPECT_EQ(std::bit_cast<std::uint32_t>(made.position.y), debris.point[1]);
            EXPECT_EQ(made.angles, (glm::vec3{0.0f, 0.0f, debris.tilt}));
            EXPECT_EQ(made.rotation, debris.rotation);
            EXPECT_EQ(made.velocity, (glm::vec3{0.0f, 0.0f, debris.rise}));
        }
        // 0x52a38c..0x52a400: the 0x607/4 at (x + rand() % 40 − 20; y + rand() % 40 − 20; z − 50), with the piece's
        // angles; its creation overwrites the light with 1 and gives it life 33 and scale (1 + 4) · 0.03f (0x3e199999).
        const auto& dust = particles[100];
        ASSERT_TRUE(dust.alive);
        EXPECT_EQ(dust.type, 0x607);
        EXPECT_EQ(dust.subtype, 4);
        EXPECT_EQ(dust.position, (glm::vec3{1001.0f, 1988.0f, 250.0f}));
        EXPECT_EQ(dust.angles, (glm::vec3{0.0f, 0.0f, 12.5f}));
        EXPECT_EQ(dust.life, 33);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(dust.scale), 0x3e199999u);
        EXPECT_EQ(dust.light, glm::vec3(1.0f));
        EXPECT_FALSE(particles[101].alive);
    }
    FamilyWorld tested;
    tested.FillTheParticles();
    ModelEffect& piece = KundunPiece(tested, 3, 300);
    piece.byte89 = 4;
    piece.transform.angles = {0.0f, 0.0f, 12.5f};
    piece.copiedAngles.x = 0.0f;
    ASSERT_TRUE(Client::Effects::StepHighModelEffect(piece, nullptr, tested.StepContext()).HasValue());
    // 1 + 300 + 2 and the sway (kundun.py).
    EXPECT_EQ(tested.random.State(), 0x6076eb51u);
    EXPECT_EQ(tested.Draws(), 1 + 300 + 2 + 1);
    EXPECT_EQ(piece.velocity, (glm::vec3{0.0f, 0.0f, -3.0f}));
    EXPECT_EQ(piece.transform.angles.x, -2.0f);
    EXPECT_EQ(piece.alpha, 1.0f);

    piece.life = 50;
    piece.copiedAngles.x = 1.0f;
    piece.transform.angles.x = 30.0f;
    const int before = tested.Draws();
    ASSERT_TRUE(Client::Effects::StepHighModelEffect(piece, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(tested.Draws(), before + 3);
    EXPECT_EQ(piece.copiedAngles.x, 0.0f);
    EXPECT_EQ(piece.transform.angles.x, 30.0f);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(piece.alpha), 0x3f666666u);

    // 0x52a37d..0x52a400 already on the ground (the speed's z at −3), with a slot in the pool: after the local light,
    // rand() % 40 − 20 in x, then in y, z 50 less and the 0x607/4 with its angles (life 39, scale 0x3e570a3d and light
    // 1, from its creation); then the sway, 3 (kundun.py).
    FamilyWorld landed;
    ModelEffect& rock = KundunPiece(landed, 3, 300);
    rock.byte89 = 4;
    rock.velocity = {0.0f, 0.0f, -3.0f};
    rock.transform.position = {1000.25f, 2000.5f, 300.0f};
    rock.transform.angles = {5.0f, 6.0f, 7.0f};
    rock.light = {0.5f, 0.25f, 1.0f};
    rock.copiedAngles.x = 0.0f;
    ASSERT_EQ(landed.Draws(), 0);
    ASSERT_TRUE(Client::Effects::StepHighModelEffect(rock, nullptr, landed.StepContext()).HasValue());
    EXPECT_EQ(landed.random.State(), 0x5b015a17u);
    const Client::Particles::Particle& made = landed.world->Particles().All()[0];
    ASSERT_TRUE(made.alive);
    EXPECT_EQ(made.type, 0x607);
    EXPECT_EQ(made.subtype, 4);
    EXPECT_EQ(made.position, (glm::vec3{985.25f, 1982.5f, 250.0f}));
    EXPECT_EQ(made.angles, (glm::vec3{5.0f, 6.0f, 7.0f}));
    EXPECT_EQ(made.life, 39);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(made.scale), 0x3e570a3du);
    EXPECT_EQ(made.light, glm::vec3(1.0f));
    EXPECT_FALSE(landed.world->Particles().All()[1].alive);
    EXPECT_EQ(rock.velocity, (glm::vec3{0.0f, 0.0f, -3.0f}));
    EXPECT_EQ(rock.transform.angles.x, 2.0f);
}

// 0x5277ae..0x5277d6: the 0x127 with its 0x128/2 as owner: with the 0x128's key at 0, the frame at 0; otherwise it is
// left without an owner. Only the local light.
TEST(HighEffectFamiliesTest, TheKundunEchoWaitsForItsCall)
{
    FamilyWorld tested;
    ASSERT_TRUE(CreateHighModelEffect({.type = 0x128, .subtype = 2}, tested.Context()).HasValue());
    ModelEffect& call = tested.Effect(0);
    ModelEffect& echo = tested.Effect(1);
    ASSERT_EQ(echo.type, Client::Effects::KundunEchoEffect);
    echo.animation.frame = 4.0f;
    ASSERT_TRUE(Client::Effects::StepHighModelEffect(echo, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(echo.animation.frame, 0.0f);
    EXPECT_EQ(echo.ownerContext, &call);
    call.disturbanceState = 1;
    echo.animation.frame = 4.0f;
    ASSERT_TRUE(Client::Effects::StepHighModelEffect(echo, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(echo.animation.frame, 4.0f);
    EXPECT_EQ(echo.ownerContext, nullptr);
    EXPECT_FALSE(echo.hasOwner);
    EXPECT_EQ(echo.owner.effect, nullptr);
    EXPECT_EQ(echo.life, 198);
    EXPECT_EQ(tested.Draws(), 2);
}

// The ownerless 0x127, as the return of its 0x128/2 leaves it.
ModelEffect& LooseEcho(FamilyWorld& tested, const glm::vec3& position, float frame)
{
    const auto slot = tested.world->Map().effects.Begin(
        {.type = Client::Effects::KundunEchoEffect, .position = position, .angles = {0.0f, 0.0f, 0.0f}});
    EXPECT_EQ(slot, std::optional<std::size_t>(0));
    ModelEffect& echo = tested.Effect(0);
    Client::Effects::BeginTheKundunEcho(echo);
    echo.animation.frame = frame;
    return echo;
}

// 0x527808..0x5278ae: the ownerless 0x127 with the frame past 6 raises the key and z by key · 0.8f (with the key from
// 4 to 5 and z at 100: 0x42d00000, computed separately) and leaves three white 0x5f2/0x14 with scale 10 at the
// position with rand() % 120 − 60 in x and y and rand() % 60 in z, repeated in a twin pool.
TEST(HighEffectFamiliesTest, TheLooseKundunEchoRisesInSmoke)
{
    FamilyWorld tested;
    ModelEffect& echo = LooseEcho(tested, {1000.0f, 2000.0f, 100.0f}, 7.0f);
    echo.disturbanceState = 4;
    ASSERT_TRUE(Client::Effects::StepHighModelEffect(echo, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(echo.disturbanceState, 5);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(echo.transform.position.z), 0x42d00000u);
    CrtRandom replay(Seed);
    static_cast<void>(replay.Next());
    const auto dx = static_cast<float>(replay.Next() % 120 - 60);
    const auto dy = static_cast<float>(replay.Next() % 120 - 60);
    const auto dz = static_cast<float>(replay.Next() % 60);
    const glm::vec3 point{1000.0f + dx, 2000.0f + dy, 104.0f + dz};
    for (std::size_t puff = 0; puff < 3; ++puff)
        ExpectSameParticle(tested.world->Particles().All()[puff],
                           TwinParticle(replay, 0x5f2, 0x14, point, glm::vec3(0.0f), glm::vec3(1.0f), 10.0f));
    EXPECT_FALSE(tested.world->Particles().All()[3].alive);
    EXPECT_EQ(tested.random.State(), replay.State());
}

// 0x5278b3..0x527ad1: with the frame up to 6, +0x89 from 9 to 10; the angles' z from 44.9f to 47.9f and back
// (0x423f999a and 0x4233999a); toward the hero at (2000, 2000), x and y 2 more (0x44964000 and 0x44c84000); the 0xc2/0
// with key 0 at ±600, repeated in a twin world; scale 1.8f + 0.02f (0x3fe8f5c2) and the shake (rand() % 8 − 8) ·
// 0.1f, with its eight values computed separately. Without the shake or without the hero in the pool, it stops.
TEST(HighEffectFamiliesTest, TheLooseKundunEchoCallsFireAroundTheHero)
{
    constexpr std::array<std::uint32_t, 8> Shakes{0xbf4ccccdu, 0xbf333333u, 0xbf19999au, 0xbf000000u,
                                                  0xbecccccdu, 0xbe99999au, 0xbe4ccccdu, 0xbdcccccdu};
    FamilyWorld tested;
    float shake = 0.0f;
    tested.services.cameraShake = &shake;
    tested.world->Actors()[0].position = {2000.0f, 2000.0f, 0.0f};
    ModelEffect& echo = LooseEcho(tested, {1200.0f, 1600.0f, 100.0f}, 6.0f);
    echo.byte89 = 9;
    echo.transform.angles.z = std::bit_cast<float>(0x4233999au);
    const auto stepped = Client::Effects::StepHighModelEffect(echo, nullptr, tested.StepContext());
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(echo.byte89, 10);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(echo.transform.angles.z), 0x4233999au);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(echo.transform.position.x), 0x44964000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(echo.transform.position.y), 0x44c84000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(echo.transform.scale), 0x3fe8f5c2u);

    CrtRandom replay(Seed);
    static_cast<void>(replay.Next());
    const auto dx = static_cast<double>(replay.Next() % 1200 - 600);
    const auto dy = static_cast<double>(replay.Next() % 1200 - 600);
    const glm::vec3 point{static_cast<float>(dx + 1202.0), static_cast<float>(dy + 1602.0), 100.0f};
    const glm::vec3 angles{0.0f, 0.0f, static_cast<float>(replay.Next() % 10 * 20)};
    auto twin = Client::Tests::LentWorld();
    const Client::Terrain::WorldTerrain twinTerrain;
    const auto fire = Client::Effects::CreateWizardModelEffect(
        twin->Map().effects, Client::Effects::WizardArguments(0xc2, point, angles, glm::vec3(1.0f), 0, nullptr, 0),
        replay,
        Client::Effects::WizardEffectServicesOf(
            *twin, replay, {.particles = &twin->Particles(), .worldClock = tested.clock, .terrain = &twinTerrain}));
    ASSERT_TRUE(fire.HasValue()) << fire.Error();
    EXPECT_EQ(std::bit_cast<std::uint32_t>(shake), Shakes[static_cast<std::size_t>(replay.Next() % 8)]);
    EXPECT_EQ(tested.random.State(), replay.State());
    const ModelEffect& made = tested.Effect(1);
    const ModelEffect& expected = twin->Map().effects.Effects()[0];
    ASSERT_TRUE(made.alive);
    EXPECT_EQ(made.type, 0xc2);
    EXPECT_EQ(made.transform.position, expected.transform.position);
    EXPECT_EQ(made.transform.angles, expected.transform.angles);
    EXPECT_EQ(made.transform.scale, expected.transform.scale);
    EXPECT_EQ(made.life, expected.life);
    EXPECT_EQ(made.disturbanceState, 0);

    FamilyWorld shakeless;
    ModelEffect& calm = LooseEcho(shakeless, {}, 0.0f);
    EXPECT_TRUE(HasError(Client::Effects::StepHighModelEffect(calm, nullptr, shakeless.StepContext()), "0x527acb"));
    FamilyWorld heroless;
    heroless.world->Runtime().hero = heroless.world->Actors().size();
    ModelEffect& lost = LooseEcho(heroless, {}, 0.0f);
    EXPECT_TRUE(HasError(Client::Effects::StepHighModelEffect(lost, nullptr, heroless.StepContext()), "0x527919"));
}
} // namespace
