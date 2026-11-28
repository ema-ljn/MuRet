#include "Models/IModelLibrary.h"
#include "Effects/HighModelEffects.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Models/Model.h"
#include "Kernel/ModelEffectArguments.h"
#include "Protocol/Outgoing.h"
#include "Math/HalfLifeMath.h"
#include "Models/LogicalAccess.h"
#include "Math/OriginalAngles.h"
#include "Math/OriginalRotation.h"
#include "Math/OriginalRetainedMath.h"
#include "Models/Skeleton.h"
#include "Models/ModelId.h"
#include "Render/TextureSlot.h"
#include "Kernel/ActorState.h"
#include "Actors/Drawing/CharacterDrawing.h"
#include "Kernel/DrawTailState.h"
#include "Kernel/WorldMap.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Effects/GameModelEffects.h"
#include "Kernel/JointRequest.h"
#include "Effects/ModelEffects.h"
#include "Particles/Particles.h"
#include "Particles/Sprites.h"
#include "Terrain/TerrainLighting.h"
#include "Assets/FaunaModels.h"
#include "Audio/SoundId.h"

#include "support/LentWorld.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Client::Effects::CreateHighModelEffect and StepHighModelEffect: 0x518cf0's creations and 0x5208a0's steps of the
// CL11 high types (0x134, 0x13b, 0x141, 0x19f, 0x61c and 0xc1a). 06, "Los efectos con modelo de los tipos altos".
namespace
{
using Client::Crt::CrtRandom;
using Client::Effects::CreateHighModelEffect;
using Client::Effects::HighEffectContext;
using Client::Effects::HighEffectStepContext;
using Client::Effects::ModelEffect;
using Client::Effects::StepHighModelEffect;
using Client::Kernel::ActorState;
using Client::Kernel::ModelEffectArguments;
using Client::Kernel::WorldState;

constexpr std::uint32_t Seed = 0x13579bdfu;

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

// A BMD with `actions` actions of 10 keys, for 0x4ea030 and 0x4ec200.
class ActionModels final : public Client::Models::IModelLibrary
{
public:
    explicit ActionModels(std::size_t actions)
    {
        skeleton.actions.assign(actions, Client::Models::ActionInfo{.repeats = true, .keyCount = 10});
    }
    const Client::Data::Models::Model* Find(Client::Models::ModelId) const override
    {
        return nullptr;
    }
    const Client::Models::Skeleton* SkeletonOf(Client::Models::ModelId) const override
    {
        return &skeleton;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(Client::Models::ModelId) const override
    {
        return {};
    }
    Client::Models::Skeleton skeleton;
};

struct HighWorld
{
    HighWorld()
    {
        world->Runtime().map = 30;
        world->Runtime().hero = HeroSlot;
        world->Actors()[HeroSlot].alive = true;
        world->Actors()[HeroSlot].position = {5000, 5000, 0};
    }

    HighEffectContext Context(const Client::Models::IModelLibrary* models = nullptr)
    {
        return {.world = *world, .random = random, .worldClock = clock, .models = models, .faunaModels = &faunaModels};
    }

    HighEffectStepContext StepContext(const Client::Models::IModelLibrary* models = nullptr)
    {
        return {.world = *world,
                .random = random,
                .services = services,
                .worldClock = clock,
                .models = models,
                .faunaModels = &faunaModels};
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

    static constexpr std::size_t HeroSlot = 12;
    std::unique_ptr<WorldState> world = Client::Tests::LentWorld();
    Client::Assets::FaunaModels faunaModels;
    std::unique_ptr<Client::Terrain::TerrainLighting> lighting = std::make_unique<Client::Terrain::TerrainLighting>();
    CrtRandom random{Seed};
    float clock = 1234.5f;
    Client::Effects::ModelEffectStepServices services{.lighting = lighting.get()};
};

bool HasError(const Client::Core::Expected<void, std::string>& result, std::string_view address)
{
    return !result.HasValue() && result.Error().starts_with(address);
}

bool HasError(const Client::Core::Expected<std::optional<std::size_t>, std::string>& result, std::string_view address)
{
    return !result.HasValue() && result.Error().starts_with(address);
}

// A particle's rand(): it creates it in a separate pool with `replay`, which advances like the original.
void ReplayParticle(CrtRandom& replay, std::int32_t type, std::int32_t subtype, float scale = 1.0f)
{
    auto twin = std::make_unique<Client::Particles::Particles>();
    const auto made =
        twin->CreateChecked(type, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(1.0f), subtype, scale, 0.0f, replay);
    ASSERT_TRUE(made.HasValue());
}

void Skip(CrtRandom& random, int draws)
{
    for (int draw = 0; draw < draws; ++draw)
        static_cast<void>(random.Next());
}

std::ptrdiff_t AliveParticles(WorldState& world, std::int32_t type, std::int32_t subtype)
{
    return std::ranges::count_if(world.Particles().All(), [type, subtype](const Client::Particles::Particle& particle)
                                 { return particle.alive && particle.type == type && particle.subtype == subtype; });
}

// 0x51d714: the siege arrival's 0x134 aura, with life 100, the scale from the key (MOVSX) times 0.01f and +0x64 and
// +0x68 at 0, no rand(); with the table full, nothing.
TEST(HighModelEffectsTest, TheArrivalAuraTakesItsScaleFromTheKeyWithoutRandoms)
{
    HighWorld tested;
    ActorState& owner = tested.world->Actors()[3];
    const ModelEffectArguments arguments{.type = 0x134,
                                         .position = {100, 200, 300},
                                         .angles = {1, 2, 3},
                                         .light = {0.5f, 0.6f, 0.7f},
                                         .owner = &owner,
                                         .key = 120};
    const auto made = CreateHighModelEffect(arguments, tested.Context());
    ASSERT_TRUE(made.HasValue()) << made.Error();
    ASSERT_EQ(made.Value(), std::optional<std::size_t>{0});
    const ModelEffect& aura = tested.Effect(0);
    EXPECT_TRUE(aura.alive);
    EXPECT_EQ(aura.type, 0x134);
    EXPECT_EQ(aura.life, 100);
    EXPECT_EQ(aura.transform.scale, static_cast<float>(120.0 * static_cast<double>(0.01f)));
    EXPECT_EQ(aura.objectState.blendMesh, 0);
    EXPECT_EQ(aura.objectState.light, 0.0f);
    EXPECT_EQ(aura.ownerContext, &owner);
    EXPECT_EQ(tested.Draws(), 0);

    // The key is read signed: 0xff9c is −100.
    const auto negative = CreateHighModelEffect({.type = 0x134, .key = 0xff9c}, tested.Context());
    ASSERT_TRUE(negative.HasValue()) << negative.Error();
    EXPECT_EQ(tested.Effect(1).transform.scale, static_cast<float>(-100.0 * static_cast<double>(0.01f)));

    tested.FillTheEffects();
    const auto full = CreateHighModelEffect(arguments, tested.Context());
    ASSERT_TRUE(full.HasValue()) << full.Error();
    EXPECT_FALSE(full.Value().has_value());
    EXPECT_EQ(tested.Draws(), 0);
}

// 0x52952d: with the owner alive, +0x58 at −1 with the siege or −2 without it, life at 2 (and the tail leaves it at 1),
// +0x68 at FSIN(T · 0.001f) · 0.1f + 0.2f and +0x6c at T · 0.0005f; with the owner dead, the slot free and only +0x6c.
// The local light's rand() always comes out.
TEST(HighModelEffectsTest, TheArrivalAuraHoldsWhileItsActorLivesAndHidesWithoutTheSiege)
{
    for (const bool siege : {false, true})
    {
        SCOPED_TRACE(siege);
        HighWorld tested;
        tested.world->Runtime().castleSiege.siegeActive = siege;
        ActorState& owner = tested.world->Actors()[3];
        owner.alive = true;
        ASSERT_TRUE(CreateHighModelEffect({.type = 0x134, .position = {100, 200, 300}, .owner = &owner, .key = 120},
                                          tested.Context())
                        .HasValue());
        ModelEffect& aura = tested.Effect(0);
        auto stepped = StepHighModelEffect(aura, nullptr, tested.StepContext());
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_TRUE(aura.alive);
        EXPECT_EQ(aura.life, 1);
        EXPECT_EQ(aura.objectState.hiddenMesh, siege ? -1 : -2);
        const double argument = static_cast<double>(tested.clock) * static_cast<double>(0.001f);
        const auto wave = Client::Math::Detail::SineProductPC53(argument, 0.1f);
        ASSERT_TRUE(wave.has_value());
        EXPECT_EQ(aura.objectState.light, static_cast<float>(*wave + static_cast<double>(0.2f)));
        EXPECT_EQ(aura.objectState.uv.x,
                  static_cast<float>(static_cast<double>(tested.clock) * static_cast<double>(0.0005f)));
        EXPECT_EQ(aura.transform.position, (glm::vec3{100, 200, 300}));
        EXPECT_EQ(tested.Draws(), 1);

        owner.alive = false;
        tested.clock = 2000.0f;
        stepped = StepHighModelEffect(aura, nullptr, tested.StepContext());
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_FALSE(aura.alive);
        EXPECT_EQ(aura.life, 0);
        EXPECT_EQ(aura.objectState.uv.x, static_cast<float>(2000.0 * static_cast<double>(0.0005f)));
        EXPECT_EQ(tested.Draws(), 2);
    }
}

// 0x51e4ba: the 0x19f/0 spearman with life 20, +0x58 at 3, +0xd8 at 0.33f, action 2 (0x4ea030, if the BMD has it:
// AssetsState::fauna's or, failing that, the library's) and zero angles. Without the BMD, no action. With another
// subtype, only the prefix.
TEST(HighModelEffectsTest, TheSiegeSpearmanTakesActionTwoWhenItsModelHasIt)
{
    for (const std::size_t actions : {std::size_t{0}, std::size_t{2}, std::size_t{3}})
    {
        SCOPED_TRACE(actions);
        HighWorld tested;
        const ActionModels models(actions);
        const auto made = CreateHighModelEffect({.type = 0x19f, .position = {10, 20, 30}, .angles = {4, 5, 6}},
                                                tested.Context(actions == 0 ? nullptr : &models));
        ASSERT_TRUE(made.HasValue()) << made.Error();
        const ModelEffect& spearman = tested.Effect(0);
        EXPECT_EQ(spearman.life, 20);
        EXPECT_EQ(spearman.objectState.hiddenMesh, 3);
        EXPECT_EQ(spearman.speed, std::bit_cast<float>(0x3ea8f5c3u));
        EXPECT_EQ(spearman.animation.action, actions > 2 ? 2 : 0);
        EXPECT_EQ(spearman.transform.angles, glm::vec3(0.0f));
        EXPECT_EQ(tested.Draws(), 0);
    }
    HighWorld tested;
    const auto other = CreateHighModelEffect({.type = 0x19f, .angles = {4, 5, 6}, .subtype = 1}, tested.Context());
    ASSERT_TRUE(other.HasValue()) << other.Error();
    EXPECT_EQ(tested.Effect(0).transform.angles, (glm::vec3{4, 5, 6}));
}

// 0x52aa6a: with life a multiple of 5, two rand() for the point and the 0x12b/2 fragment (which with the table full
// draws no more); under 5, alpha times 0.769f. The tail moves it with its speed (0) and takes one life.
TEST(HighModelEffectsTest, TheSiegeSpearmanDropsAStoneEveryFiveSteps)
{
    HighWorld tested;
    tested.FillTheEffects();
    ModelEffect& spearman = tested.Effect(0);
    spearman = ModelEffect{};
    spearman.alive = true;
    spearman.type = 0x19f;
    spearman.life = 10;
    spearman.transform.position = {1000, 2000, 300};
    auto stepped = StepHighModelEffect(spearman, nullptr, tested.StepContext());
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(spearman.life, 9);
    EXPECT_EQ(tested.Draws(), 3);

    spearman.life = 4;
    spearman.alpha = 1.0f;
    stepped = StepHighModelEffect(spearman, nullptr, tested.StepContext());
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(spearman.alpha, std::bit_cast<float>(0x3f44ec4fu));
    EXPECT_EQ(spearman.life, 3);
    EXPECT_EQ(tested.Draws(), 4);

    // With a slot: the 0x12b/2 at the shifted position.
    HighWorld roomy;
    ModelEffect& dropper = roomy.Effect(0);
    dropper.alive = true;
    dropper.type = 0x19f;
    dropper.life = 5;
    dropper.transform.position = {1000, 2000, 300};
    CrtRandom expected(Seed);
    static_cast<void>(expected.Next());
    const auto side = static_cast<float>(static_cast<double>(expected.Next() % 100) - 50.0 + 1000.0);
    const auto fall = static_cast<float>(2000.0 - (static_cast<double>(expected.Next() % 30) + 15.0));
    stepped = StepHighModelEffect(dropper, nullptr, roomy.StepContext());
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    const ModelEffect& stone = roomy.Effect(1);
    EXPECT_TRUE(stone.alive);
    EXPECT_EQ(stone.type, 0x12b);
    EXPECT_EQ(stone.subtype, 2);
    EXPECT_EQ(stone.transform.position.x, side);
    EXPECT_EQ(stone.transform.position.y, fall);
}

// 0x51f154: the 0x61c/0 pulse with life 20, speed (0, −550, 0), angles (−90, 0, z), +0x28 and +0x2c at 0 and +0x30 at
// rand() % 360; then its joint 0x61c/3 (0x51f1c5, creation 0x534494 without rand()) with scale 80 at the position, and
// the start. The /1, (0, −250, 0) without a joint. Another subtype, only the prefix and no rand().
TEST(HighModelEffectsTest, TheSiegePulseDrawsItsPhaseAndRaisesItsJoint)
{
    {
        HighWorld tested;
        const ModelEffectArguments arguments{.type = 0x61c, .position = {10, 20, 30}, .angles = {4, 5, 6}};
        const auto raised = CreateHighModelEffect(arguments, tested.Context());
        ASSERT_TRUE(raised.HasValue()) << raised.Error();
        const ModelEffect& pulse = tested.Effect(0);
        EXPECT_EQ(pulse.life, 20);
        EXPECT_EQ(pulse.velocity, (glm::vec3{0, -550, 0}));
        EXPECT_EQ(pulse.transform.angles, (glm::vec3{-90, 0, 6}));
        EXPECT_EQ(pulse.start, (glm::vec3{10, 20, 30}));
        EXPECT_EQ(tested.Draws(), 1);
        const auto& joint = tested.world->DrawTail().joints.front();
        ASSERT_TRUE(joint.alive);
        EXPECT_EQ(joint.type, 0x61c);
        EXPECT_EQ(joint.subtype, 3);
        EXPECT_EQ(joint.texture, 0x60f);
        EXPECT_EQ(joint.width, 80.0f);
        EXPECT_EQ(joint.position, (glm::vec3{10, 20, 30}));
        EXPECT_EQ(joint.bendAngles, (glm::vec3{0, 0, 6}));
        EXPECT_EQ(joint.speed, 10.0f);
        EXPECT_EQ(joint.velocity.x, 10.5f);
        EXPECT_EQ(joint.velocity.z, 2.0f);
        EXPECT_EQ(joint.life, 20);
        EXPECT_FALSE(tested.world->DrawTail().joints[1].alive);

        tested.FillTheJoints();
        const auto made = CreateHighModelEffect(arguments, tested.Context());
        ASSERT_TRUE(made.HasValue()) << made.Error();
        const ModelEffect& joined = tested.Effect(1);
        CrtRandom expected(Seed);
        static_cast<void>(expected.Next());
        EXPECT_EQ(joined.copiedAngles, (glm::vec3{0, 0, static_cast<float>(expected.Next() % 360)}));
        EXPECT_EQ(joined.start, (glm::vec3{10, 20, 30}));
        EXPECT_EQ(tested.Draws(), 2);
    }
    HighWorld tested;
    const auto second = CreateHighModelEffect({.type = 0x61c, .position = {1, 2, 3}, .subtype = 1}, tested.Context());
    ASSERT_TRUE(second.HasValue()) << second.Error();
    EXPECT_EQ(tested.Effect(0).velocity, (glm::vec3{0, -250, 0}));
    EXPECT_EQ(tested.Effect(0).start, (glm::vec3{1, 2, 3}));
    EXPECT_EQ(tested.Draws(), 1);
    const auto other = CreateHighModelEffect({.type = 0x61c, .angles = {7, 8, 9}, .subtype = 2}, tested.Context());
    ASSERT_TRUE(other.HasValue()) << other.Error();
    EXPECT_EQ(tested.Effect(1).transform.angles, (glm::vec3{7, 8, 9}));
    EXPECT_EQ(tested.Draws(), 1);
}

// 0x52c19a: with life 10 or less and even, the pulse turns +0x30 by 72 more, sets the angles to (90, 0, 0) and, with
// the /0, draws rand() % 400 for joint 0x632/5 (0x52c266, StormJoints.cpp: two more rand(); with the table full it is
// not created); then it goes on through the 0x13b/0's step (0x52c34e): at 12 or less, four 0x617/0 sparks (two rand()
// each) and 0x5d9/2 with its rand() % 60.
TEST(HighModelEffectsTest, TheSiegePulseTurnsAndFallsIntoTheFlareStep)
{
    HighWorld tested;
    ModelEffect& pulse = tested.Effect(0);
    pulse.alive = true;
    pulse.type = 0x61c;
    pulse.life = 10;
    pulse.transform.position = {1000, 2000, 300};
    // The rotated speed's z and the start's are not integers: the sum is stored as float before the rand() (0x52c228).
    // With −300.1f, storing it or not changes the last bit of the joint's z (with this seed's rand() % 400, 178).
    pulse.velocity = {0, -550, 0.3f};
    pulse.copiedAngles = {0, 0, 10};
    pulse.start = {1000, 2000, -300.1f};
    pulse.transform.scale = 1.0f;
    const ModelEffect initial = pulse;
    const auto raised = StepHighModelEffect(pulse, nullptr, tested.StepContext());
    ASSERT_TRUE(raised.HasValue()) << raised.Error();
    EXPECT_EQ(pulse.transform.angles, (glm::vec3{90, 0, 0}));
    EXPECT_EQ(pulse.copiedAngles.z, 82.0f);
    // The light, the rand() % 400, the 0x632/5's two (rand() & 1 and rand() % 20), the point's two and, per spark, its
    // angle, the 0x617/0's two and the fire's two.
    EXPECT_EQ(tested.Draws(), 1 + 1 + 2 + 2 + 4 * 5);
    CrtRandom expected(Seed);
    static_cast<void>(expected.Next());
    const int rise = expected.Next() % 400;
    static_cast<void>(expected.Next());
    const int speed = expected.Next() % 20;
    const glm::vec3 reach =
        Client::Math::OriginalVectorRotate({0, -550, 0.3f}, Client::Math::OriginalAngleMatrix({0, 0, 82}));
    ASSERT_EQ(reach.z, 0.3f);
    const glm::vec3 point{static_cast<float>(static_cast<double>(reach.x) + 1000.0),
                          static_cast<float>(static_cast<double>(reach.y) + 2000.0),
                          static_cast<float>(static_cast<double>(static_cast<float>(static_cast<double>(reach.z) +
                                                                                    static_cast<double>(-300.1f))) +
                                             static_cast<double>(rise) + 700.0)};
    const auto& star = tested.world->DrawTail().joints.front();
    ASSERT_TRUE(star.alive);
    EXPECT_EQ(star.type, 0x632);
    EXPECT_EQ(star.subtype, 5);
    EXPECT_EQ(star.ownerContext, &pulse);
    EXPECT_EQ(star.ownerKind, Client::Kernel::JointOwnerKind::ModelEffect);
    EXPECT_EQ(star.position, point);
    EXPECT_EQ(star.angles, (glm::vec3{90, 0, 0}));
    EXPECT_EQ(star.bendAngles, (glm::vec3{90, 0, 0}));
    EXPECT_EQ(star.texture, 0x60f);
    EXPECT_EQ(star.width, 110.0f);
    EXPECT_EQ(star.maximumCount, 15);
    EXPECT_EQ(star.life, 40);
    EXPECT_EQ(star.speed, static_cast<float>(speed + 10));
    EXPECT_EQ(star.orientation, 2);
    EXPECT_EQ(star.key, 0);
    EXPECT_EQ(star.field9ec, 150);
    EXPECT_EQ(tested.world->DrawTail().joints[1].type, 0x617);

    HighWorld full;
    full.FillTheJoints();
    ModelEffect& joined = full.Effect(0);
    joined = initial;
    const auto stepped = StepHighModelEffect(joined, nullptr, full.StepContext());
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(joined.life, 9);
    EXPECT_EQ(joined.transform.scale, static_cast<float>(1.0 - static_cast<double>(0.05f)));
    const float light = static_cast<float>(10.0 * static_cast<double>(std::bit_cast<float>(0x3d638e39u)));
    EXPECT_EQ(joined.alpha, light);
    EXPECT_EQ(joined.light, glm::vec3(light));
    EXPECT_EQ(joined.velocity.y, -552.0f);
    // The light, the rand() % 400, the point's two and, per spark, its angle and the 0x5d9/2 fire's two.
    EXPECT_EQ(full.Draws(), 1 + 1 + 2 + 4 * 3);
    EXPECT_EQ(std::ranges::count_if(full.world->Particles().All(), [](const auto& particle)
                                    { return particle.alive && particle.type == Client::Particles::FireParticle; }),
              4);
}

// 0x52c2df..0x52c34a: with the /1, the two 0x615/6 (0x52c31e and 0x52c342, scale 80, owned by the pulse) at the point
// 100 higher, two rand() each (x and y shifted, 0x531b0c); then the /1 of the 0x13b's step without rand().
TEST(HighModelEffectsTest, TheSecondSiegePulseDropsTwoBolts)
{
    HighWorld tested;
    ModelEffect& pulse = tested.Effect(0);
    pulse.alive = true;
    pulse.type = 0x61c;
    pulse.subtype = 1;
    pulse.life = 8;
    // The start's z and the rotated speed's are not integers: the 100 are added in the x87, without storing (0x52c307);
    // with −300.1f, storing the sum would change the last bit.
    pulse.velocity = {0, -250, 0.3f};
    pulse.copiedAngles = {0, 0, 10};
    pulse.start = {1000, 2000, -300.1f};

    const auto stepped = StepHighModelEffect(pulse, nullptr, tested.StepContext());

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(tested.Draws(), 1 + 2 + 2);
    const glm::vec3 reach =
        Client::Math::OriginalVectorRotate({0, -250, 0.3f}, Client::Math::OriginalAngleMatrix({0, 0, 82}));
    ASSERT_EQ(reach.z, 0.3f);
    const glm::vec3 point{static_cast<float>(static_cast<double>(reach.x) + 1000.0),
                          static_cast<float>(static_cast<double>(reach.y) + 2000.0),
                          static_cast<float>(static_cast<double>(reach.z) + static_cast<double>(-300.1f) + 100.0)};
    for (std::size_t slot = 0; slot < 2; ++slot)
    {
        SCOPED_TRACE(slot);
        const auto& bolt = tested.world->DrawTail().joints[slot];
        ASSERT_TRUE(bolt.alive);
        EXPECT_EQ(bolt.type, 0x615);
        EXPECT_EQ(bolt.subtype, 6);
        EXPECT_EQ(bolt.ownerContext, &pulse);
        EXPECT_EQ(bolt.width, 80.0f);
        EXPECT_EQ(bolt.targetPosition, point);
        EXPECT_EQ(bolt.speed, std::bit_cast<float>(0x3e99999au));
    }
    EXPECT_FALSE(tested.world->DrawTail().joints[2].alive);
}

// 0x51e059 and 0x52c34e: the 0x13b/0 with life 15, scale at 0, z 100 more, speed (0, −10, 0) and +0xd8 at 0.25f; with
// life above 12 it grows 0.9f, lowers the speed's y and releases the 0x13b/1 owned by it (life 5, scale 3.5), which in
// its step sets the light and alpha to life · 0.1f. Of the two, only the /0 has the dynamic light.
TEST(HighModelEffectsTest, TheSiegeFlareGrowsAndLeavesSparksThatFade)
{
    HighWorld tested;
    const auto made =
        CreateHighModelEffect({.type = 0x13b, .position = {1000, 2000, 300}, .light = {1, 1, 1}}, tested.Context());
    ASSERT_TRUE(made.HasValue()) << made.Error();
    ModelEffect& flare = tested.Effect(0);
    EXPECT_EQ(flare.life, 15);
    EXPECT_EQ(flare.transform.scale, 0.0f);
    EXPECT_EQ(flare.transform.position.z, 400.0f);
    EXPECT_EQ(flare.velocity, (glm::vec3{0, -10, 0}));
    EXPECT_EQ(flare.speed, 0.25f);
    EXPECT_EQ(flare.objectState.blendMesh, 0);
    EXPECT_EQ(flare.objectState.light, 1.0f);

    const auto stepped = StepHighModelEffect(flare, nullptr, tested.StepContext());
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(flare.life, 14);
    EXPECT_EQ(flare.transform.scale, std::bit_cast<float>(0x3f666666u));
    EXPECT_EQ(flare.velocity.y, -12.0f);
    EXPECT_EQ(flare.transform.position, (glm::vec3{1000, 1988, 400}));
    const ModelEffect& spark = tested.Effect(1);
    ASSERT_TRUE(spark.alive);
    EXPECT_EQ(spark.type, 0x13b);
    EXPECT_EQ(spark.subtype, 1);
    EXPECT_EQ(spark.ownerContext, &flare);
    EXPECT_EQ(spark.life, 5);
    EXPECT_EQ(spark.transform.scale, 3.5f);
    EXPECT_EQ(spark.transform.position, (glm::vec3{1000, 2000, 400}));
    EXPECT_EQ(tested.Draws(), 1);
    EXPECT_TRUE(std::ranges::any_of(tested.lighting->cells, [](const auto& cell) { return cell[0] > 0; }));

    ModelEffect& owned = tested.Effect(1);
    const auto faded = StepHighModelEffect(owned, nullptr, tested.StepContext());
    ASSERT_TRUE(faded.HasValue()) << faded.Error();
    const float light = static_cast<float>(5.0 * static_cast<double>(0.1f));
    EXPECT_EQ(owned.alpha, light);
    EXPECT_EQ(owned.objectState.light, light);
    EXPECT_EQ(owned.light, glm::vec3(light));
    EXPECT_EQ(owned.life, 4);
    EXPECT_EQ(tested.Draws(), 2);
}

// 0x52cd41..0x52cd84: the 0x13b animates twice per step; when the first ends the action (0xff), the second reads the
// BMD's action 255, outside its table, and stops.
TEST(HighModelEffectsTest, TheSiegeFlareCutsWhenItsSecondAnimationReadsAFinishedAction)
{
    HighWorld tested;
    tested.FillTheEffects();
    ModelEffect& flare = tested.Effect(0);
    flare = ModelEffect{};
    flare.alive = true;
    flare.type = 0x13b;
    flare.subtype = 1;
    flare.life = 5;
    flare.speed = 0.25f;
    const ActionModels models(1);
    const auto first = StepHighModelEffect(flare, &models.skeleton, tested.StepContext());
    ASSERT_TRUE(first.HasValue()) << first.Error();
    EXPECT_EQ(flare.animation.frame, 0.5f);
    flare.animation.action = 0xff;
    EXPECT_TRUE(HasError(StepHighModelEffect(flare, &models.skeleton, tested.StepContext()), "0x52cd84"));
}

// 0x51c410 and 0x525f1a: the 0x141 is born 20 higher with life 20, +0xe8 at 0 and scale 0.05f; each step it grows
// 0.1f and, from 1, stays at 1 and loses 0.07f of alpha.
TEST(HighModelEffectsTest, TheGrowingFlashStopsAtOneAndFades)
{
    HighWorld tested;
    const auto made =
        CreateHighModelEffect({.type = 0x141, .position = {10, 20, 30}, .light = {1, 1, 1}}, tested.Context());
    ASSERT_TRUE(made.HasValue()) << made.Error();
    ModelEffect& flash = tested.Effect(0);
    EXPECT_EQ(flash.transform.position, (glm::vec3{10, 20, 50}));
    EXPECT_EQ(flash.life, 20);
    EXPECT_FALSE(flash.vertexLight);
    EXPECT_EQ(flash.transform.scale, std::bit_cast<float>(0x3d4ccccdu));
    float scale = flash.transform.scale;
    float alpha = 1.0f;
    for (int step = 0; step < 15; ++step)
    {
        const auto stepped = StepHighModelEffect(flash, nullptr, tested.StepContext());
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        const double grown = static_cast<double>(scale) + static_cast<double>(0.1f);
        scale = static_cast<float>(grown);
        if (grown >= 1.0)
        {
            alpha = static_cast<float>(static_cast<double>(alpha) - static_cast<double>(0.07f));
            scale = 1.0f;
        }
        EXPECT_EQ(flash.transform.scale, scale);
        EXPECT_EQ(flash.alpha, alpha);
    }
    EXPECT_EQ(flash.life, 5);
    EXPECT_LT(flash.alpha, 1.0f);
    EXPECT_EQ(tested.Draws(), 15);
}

// 0x51f85a and 0x52c9fd: the 0xc1a fragment is born 280 higher with life 30, +0x64 at −2 and speed (0, −80, −10); with
// the terrain above its z, the 0x5ed/0 explosion (and its sound 0x40), six small stones 0xc8 or 0xc9 (rand() & 1) and
// the slot freed, though the tail moves it anyway; always, the dynamic light.
TEST(HighModelEffectsTest, TheRadialShardBurstsIntoChipsBelowTheTerrain)
{
    HighWorld tested;
    tested.services.terrainHeight = [](float, float) { return 300.0; };
    const auto made = CreateHighModelEffect(
        {.type = 0xc1a, .position = {1000, 2000, 40}, .angles = {3, 0, 0}, .light = {1, 1, 1}}, tested.Context());
    ASSERT_TRUE(made.HasValue()) << made.Error();
    ModelEffect& shard = tested.Effect(0);
    EXPECT_EQ(shard.life, 30);
    EXPECT_EQ(shard.objectState.blendMesh, -2);
    EXPECT_EQ(shard.transform.position.z, 320.0f);
    EXPECT_EQ(shard.transform.angles.x, 23.0f);
    EXPECT_EQ(shard.velocity, (glm::vec3{0, -80, -10}));
    EXPECT_EQ(tested.Draws(), 0);

    auto stepped = StepHighModelEffect(shard, nullptr, tested.StepContext());
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_TRUE(shard.alive);
    EXPECT_EQ(shard.life, 29);
    EXPECT_EQ(tested.Draws(), 1);

    shard.transform.position.z = 200.0f;
    stepped = StepHighModelEffect(shard, nullptr, tested.StepContext());
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_FALSE(shard.alive);
    EXPECT_EQ(shard.life, 28);
    const auto chips = std::ranges::count_if(tested.world->Map().effects.Effects(), [](const ModelEffect& effect)
                                             { return effect.alive && (effect.type == 0xc8 || effect.type == 0xc9); });
    EXPECT_EQ(chips, 6);
    EXPECT_EQ(tested.world->Runtime().lastSoundId, Client::Audio::SoundId{0x40});
    EXPECT_TRUE(
        std::ranges::any_of(tested.world->Particles().All(), [](const auto& particle)
                            { return particle.alive && particle.type == Client::Particles::ExplosionParticle; }));

    // With the tables full, only the light and the six rand() & 1.
    HighWorld full;
    full.services.terrainHeight = [](float, float) { return 300.0; };
    full.FillTheEffects();
    full.FillTheParticles();
    ModelEffect& lone = full.Effect(0);
    lone = ModelEffect{};
    lone.alive = true;
    lone.type = 0xc1a;
    lone.life = 30;
    stepped = StepHighModelEffect(lone, nullptr, full.StepContext());
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_FALSE(lone.alive);
    EXPECT_EQ(full.Draws(), 1 + 6);
    EXPECT_NE(full.world->Runtime().lastSoundId, Client::Audio::SoundId{0x40});

    HighWorld blind;
    ModelEffect& unseen = blind.Effect(0);
    unseen.alive = true;
    unseen.type = 0xc1a;
    unseen.life = 30;
    EXPECT_TRUE(HasError(StepHighModelEffect(unseen, nullptr, blind.StepContext()), "0x52ca08"));
}
} // namespace

namespace
{
// 0x51eebb..0x51efa4: the 0x61e case in the creation, with its rand(), and the full table without any.
TEST(HighModelEffectsTest, TheColoredDecalTakesItsLifeAndScaleBySubtype)
{
    constexpr double ScaleStep = static_cast<double>(std::bit_cast<float>(0x3d23d70au)); // 0.04f
    struct Probe
    {
        std::int32_t subtype;
        std::int32_t life;
        int draws;
    };
    for (const Probe& probe : {Probe{2, 20, 0}, Probe{4, 40, 1}, Probe{5, 20, 0}, Probe{6, 60, 1}, Probe{7, 40, 1},
                               Probe{8, 20, 0}, Probe{9, 10, 0}, Probe{0xa, 40, 1}})
    {
        SCOPED_TRACE(probe.subtype);
        HighWorld tested;
        ActorState& owner = tested.world->Actors()[3];
        const ModelEffectArguments arguments{.type = 0x61e,
                                             .position = {700, 800, 90},
                                             .angles = {0, 0, 45},
                                             .light = glm::vec3(0.5f),
                                             .subtype = probe.subtype,
                                             .owner = &owner};
        const auto made = CreateHighModelEffect(arguments, tested.Context());
        ASSERT_TRUE(made.HasValue()) << made.Error();
        ASSERT_EQ(made.Value(), std::optional<std::size_t>{0});
        const ModelEffect& decal = tested.Effect(0);
        EXPECT_EQ(decal.life, probe.life);
        EXPECT_EQ(decal.ownerContext, &owner);
        EXPECT_EQ(tested.Draws(), probe.draws);
        CrtRandom expected{Seed};
        if (probe.subtype == 4 || probe.subtype == 6 || probe.subtype == 0xa)
        {
            const int grown = expected.Next() % 50 + 50;
            EXPECT_EQ(decal.transform.scale, static_cast<float>(static_cast<double>(grown) * ScaleStep));
        }
        if (probe.subtype == 6)
        {
            EXPECT_EQ(decal.start, arguments.position);
        }
        if (probe.subtype == 7)
        {
            EXPECT_EQ(decal.transform.angles.z, static_cast<float>(expected.Next() % 360));
        }
        if (probe.subtype == 9)
        {
            EXPECT_EQ(decal.transform.scale, std::bit_cast<float>(0x3dcccccdu));
        }
    }
    HighWorld full;
    full.FillTheEffects();
    const auto none = CreateHighModelEffect({.type = 0x61e, .subtype = 6}, full.Context());
    ASSERT_TRUE(none.HasValue()) << none.Error();
    EXPECT_FALSE(none.Value().has_value());
    EXPECT_EQ(full.Draws(), 0);
}

// 0x52b9c1..0x52bb24: the 0x61e/6 sets its start as its owner's model origin, draws +0x68 and, per life, a bone;
// dummies draw no more; the others, the shift's three rand() and particle 0x5ac/8 (0x52baf5) with the owner's angles,
// light 1 and scale 1, which draws three more; full, without them. Then the owner's alpha at life · 0.0125f, and the
// tail: one life less without moving.
TEST(HighModelEffectsTest, TheBindingDecalSparksOverTheBonesOfItsOwner)
{
    HighWorld tested;
    ActionModels models(1);
    models.skeleton.bones.resize(2);
    models.skeleton.bones[1].dummy = true;
    tested.faunaModels.Bind(&models);
    ActorState& owner = tested.world->Actors()[3];
    owner.alive = true;
    owner.type = 0x20a;
    owner.position = {1000, 1000, 0};
    owner.angles = {0, 0, 90};
    owner.alpha = 1.0f;
    const ModelEffectArguments arguments{
        .type = 0x61e, .position = {1000, 1000, 0}, .light = glm::vec3(1.0f), .subtype = 6, .owner = &owner};
    ASSERT_TRUE(CreateHighModelEffect(arguments, tested.Context()).HasValue());
    ModelEffect& decal = tested.Effect(0);
    decal.life = 3;
    const int before = tested.Draws();
    const ModelEffect kept = decal;
    const auto sparked = StepHighModelEffect(decal, nullptr, tested.StepContext());
    ASSERT_TRUE(sparked.HasValue()) << sparked.Error();
    {
        // The local light (0x5208c5), +0x68 (0x52ba0f) and, per turn, the bone (0x52ba3d); a non-dummy draws the
        // shift's three (0x52ba66..0x52ba98) and the 0x5ac/8 its own, replayed in a twin pool.
        CrtRandom expected{Seed};
        for (int skipped = 0; skipped < before + 2; ++skipped)
            static_cast<void>(expected.Next());
        auto twin = std::make_unique<Client::Particles::Particles>();
        const auto& all = tested.world->Particles().All();
        std::size_t made = 0;
        for (int turn = 0; turn < 3; ++turn)
        {
            if (expected.Next() % 2 == 1)
                continue;
            for (int axis = 0; axis < 3; ++axis)
                static_cast<void>(expected.Next());
            const Client::Particles::Particle& spark = all[made];
            ASSERT_TRUE(spark.alive);
            EXPECT_EQ(spark.type, 0x5ac);
            EXPECT_EQ(spark.subtype, 8);
            const auto replayed =
                twin->CreateChecked(0x5ac, spark.start, owner.angles, glm::vec3(1.0f), 8, 1.0f, 0.0f, expected);
            ASSERT_TRUE(replayed.HasValue() && replayed.Value().has_value());
            const Client::Particles::Particle& twinSpark = twin->All()[*replayed.Value()];
            EXPECT_EQ(spark.life, twinSpark.life);
            EXPECT_EQ(spark.verticalSpeed, twinSpark.verticalSpeed);
            EXPECT_EQ(spark.scale, twinSpark.scale);
            EXPECT_EQ(spark.angles, owner.angles);
            EXPECT_EQ(spark.light, glm::vec3(1.0f));
            ++made;
        }
        EXPECT_GT(made, 0U);
        EXPECT_FALSE(all[made].alive);
        EXPECT_EQ(tested.random.State(), expected.State());
    }
    decal = kept;
    tested.random = CrtRandom{Seed};
    for (int skipped = 0; skipped < before; ++skipped)
        static_cast<void>(tested.random.Next());
    tested.FillTheParticles();

    const auto stepped = StepHighModelEffect(decal, nullptr, tested.StepContext());

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    CrtRandom expected{Seed};
    for (int skipped = 0; skipped < before; ++skipped)
        static_cast<void>(expected.Next());
    static_cast<void>(expected.Next()); // the local light (0x5208c5)
    const int glow = expected.Next() % 10;
    int sparks = 0;
    int draws = before + 2;
    for (int turn = 0; turn < 3; ++turn)
    {
        const int bone = expected.Next() % 2;
        ++draws;
        if (bone == 1)
            continue;
        for (int axis = 0; axis < 3; ++axis)
            static_cast<void>(expected.Next());
        draws += 3;
        ++sparks;
    }
    EXPECT_EQ(decal.objectState.light,
              static_cast<float>(static_cast<double>(glow) * static_cast<double>(std::bit_cast<float>(0x3dcccccdu))));
    EXPECT_EQ(owner.alpha, static_cast<float>(3.0 * static_cast<double>(std::bit_cast<float>(0x3c4ccccdu))));
    EXPECT_EQ(tested.world->ModelPlacement(0x20a).origin, decal.start);
    EXPECT_EQ(decal.life, 2);
    EXPECT_EQ(decal.transform.position, arguments.position);
    EXPECT_GT(sparks, 0);
    EXPECT_EQ(tested.Draws(), draws);
    tested.faunaModels.Bind(nullptr);
}

// The 0x61e/6 without bones in its owner's model: the original divides by zero (0x52ba47); MuRet stops, after the
// rand() of +0x68.
TEST(HighModelEffectsTest, TheBindingDecalCutsWhenItsOwnerHasNoBones)
{
    HighWorld tested;
    ActorState& owner = tested.world->Actors()[3];
    owner.type = 0x20a;
    ASSERT_TRUE(CreateHighModelEffect({.type = 0x61e, .subtype = 6, .owner = &owner}, tested.Context()).HasValue());
    const auto stepped = StepHighModelEffect(tested.Effect(0), nullptr, tested.StepContext());
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x52ba47"), std::string::npos) << stepped.Error();
    EXPECT_EQ(tested.Draws(), 1 + 2);
}

// 0x52ba1d: the owner's BMD comes from the global table, as in the 0x19f's creation: without AssetsState::fauna's (a
// player), `models`'s. With a single dummy bone, each life draws only the bone and the particle does not run.
TEST(HighModelEffectsTest, TheBindingDecalFindsItsOwnerModelLikeTheSpearman)
{
    HighWorld tested;
    ActionModels models(1);
    models.skeleton.bones.resize(1);
    models.skeleton.bones[0].dummy = true;
    ActorState& owner = tested.world->Actors()[3];
    owner.alive = true;
    owner.type = 0x20a;
    ASSERT_TRUE(CreateHighModelEffect({.type = 0x61e, .subtype = 6, .owner = &owner}, tested.Context()).HasValue());
    ModelEffect& decal = tested.Effect(0);
    decal.life = 3;
    const int before = tested.Draws();

    const auto stepped = StepHighModelEffect(decal, nullptr, tested.StepContext(&models));

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(tested.Draws(), before + 2 + 3);
    EXPECT_EQ(decal.life, 2);
}

// 0x51c884 and 0x5266ea: the seamanFX 0x11a is born with +0x64 at −2, +0x68 at 0.2f, scale 0.4f, life 20, +0xe4 at 120
// and speed (0, −40, 0) in +0xd8, with light 1 and the start at its place. Each step it grows 0.02f, rises to the start
// plus +0xe4 (which drops by 20 down to 60) and speeds up 10; with the particle table full, without the 0x6f4/1 or
// extra rand().
TEST(HighModelEffectsTest, TheSeamanEffectRisesAndSpeedsUp)
{
    HighWorld tested;
    tested.FillTheParticles();
    ASSERT_TRUE(
        CreateHighModelEffect({.type = 0x11a, .position = {100, 200, 300}, .light = glm::vec3(0.5f)}, tested.Context())
            .HasValue());
    ModelEffect& seaman = tested.Effect(0);
    EXPECT_EQ(seaman.objectState.blendMesh, -2);
    EXPECT_EQ(seaman.objectState.light, std::bit_cast<float>(0x3e4ccccdu));
    EXPECT_EQ(seaman.transform.scale, std::bit_cast<float>(0x3ecccccdu));
    EXPECT_EQ(seaman.life, 20);
    EXPECT_EQ(seaman.turnDirection, 120.0f);
    EXPECT_EQ(seaman.velocity, glm::vec3(0, -40, 0));
    EXPECT_EQ(seaman.light, glm::vec3(1.0f));
    EXPECT_EQ(seaman.start, glm::vec3(100, 200, 300));
    float scale = seaman.transform.scale;
    float before = seaman.turnDirection;
    for (const float lift : {100.0f, 80.0f, 60.0f, 60.0f})
    {
        ASSERT_TRUE(StepHighModelEffect(seaman, nullptr, tested.StepContext()).HasValue());
        scale = static_cast<float>(static_cast<double>(scale) + static_cast<double>(std::bit_cast<float>(0x3ca3d70au)));
        EXPECT_EQ(seaman.transform.scale, scale);
        EXPECT_EQ(seaman.turnDirection, lift);
        EXPECT_EQ(seaman.transform.position.z, 300.0f + before);
        before = lift;
    }
    EXPECT_EQ(seaman.speed, -80.0f);
    EXPECT_EQ(seaman.velocity, glm::vec3(0, -80, 0));
    EXPECT_EQ(seaman.life, 16);
    EXPECT_EQ(tested.Draws(), 4);

    // With a slot, the four 0x6f4/1 (Kalima's waterfall ones) at its place, with the local light (0x5208c5, without the
    // last 5 lives' subtraction) times (0.1f; 0.3f; 0.6f) (0x526740..0x526783) and one rand() each (0x543d30); then the
    // dynamic light with radius 3 and that same light (0x5267ba).
    HighWorld roomy;
    ASSERT_TRUE(CreateHighModelEffect({.type = 0x11a, .position = {100, 200, 300}}, roomy.Context()).HasValue());
    const auto stepped = StepHighModelEffect(roomy.Effect(0), nullptr, roomy.StepContext());
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    CrtRandom expected{Seed};
    const auto luminosity =
        static_cast<float>(static_cast<double>((expected.Next() & 3) + 7) * static_cast<double>(0.1f));
    const glm::vec3 color{static_cast<float>(static_cast<double>(luminosity) * static_cast<double>(0.1f)),
                          static_cast<float>(static_cast<double>(luminosity) * static_cast<double>(0.3f)),
                          static_cast<float>(static_cast<double>(luminosity) * static_cast<double>(0.6f))};
    int sprays = 0;
    for (const auto& particle : roomy.world->Particles().All())
    {
        if (!particle.alive || particle.type != 0x6f4 || particle.subtype != 1)
            continue;
        ++sprays;
        EXPECT_EQ(particle.light, color);
    }
    EXPECT_EQ(sprays, 4);
    EXPECT_EQ(roomy.Draws(), 1 + 4);
    EXPECT_TRUE(std::ranges::any_of(roomy.lighting->cells, [](const auto& cell) { return cell[0] > 0; }));
}

// 0x52bb29..0x52bb74: the 0x61e/9 grows 0.3f and its light drops to 0.909f; the /4, /7, /8 and /0xa only lose a life.
TEST(HighModelEffectsTest, TheGrowingDecalWidensAndDims)
{
    HighWorld tested;
    ASSERT_TRUE(
        CreateHighModelEffect({.type = 0x61e, .light = glm::vec3(0.8f), .subtype = 9}, tested.Context()).HasValue());
    ModelEffect& decal = tested.Effect(0);
    ASSERT_TRUE(StepHighModelEffect(decal, nullptr, tested.StepContext()).HasValue());
    const double decay = static_cast<double>(std::bit_cast<float>(0x3f68ba2eu));
    EXPECT_EQ(decal.transform.scale, static_cast<float>(static_cast<double>(std::bit_cast<float>(0x3dcccccdu)) +
                                                        static_cast<double>(std::bit_cast<float>(0x3e99999au))));
    EXPECT_EQ(decal.light.x, static_cast<float>(static_cast<double>(0.8f) * decay));
    EXPECT_EQ(decal.life, 9);
    EXPECT_EQ(tested.Draws(), 1);
    for (const std::int32_t subtype : {4, 7, 8, 0xa})
    {
        HighWorld plain;
        ASSERT_TRUE(CreateHighModelEffect({.type = 0x61e, .subtype = subtype}, plain.Context()).HasValue());
        const int life = plain.Effect(0).life;
        const float scale = plain.Effect(0).transform.scale;
        const int before = plain.Draws();
        ASSERT_TRUE(StepHighModelEffect(plain.Effect(0), nullptr, plain.StepContext()).HasValue());
        EXPECT_EQ(plain.Effect(0).life, life - 1);
        EXPECT_EQ(plain.Effect(0).transform.scale, scale);
        EXPECT_EQ(plain.Draws(), before + 1);
    }
}
} // namespace

namespace
{
// 0x51e596..0x51e69e: rays 0x5e8 (blue; the /1 white with scale 3), 0x5e9 and 0x5ea (orange): life 20, the light, the
// scale and the rotated speed. No rand() without the hero as owner; the table full, nothing. The step only takes their
// life: they do not move (0x52cd1d).
TEST(HighModelEffectsTest, TheMovingLightsTakeTheirColorScaleAndRush)
{
    struct Probe
    {
        std::int16_t type;
        std::int32_t subtype;
        glm::vec3 light;
        float scale;
        float rush;
    };
    const float orange = std::bit_cast<float>(0x3ecccccdu), ember = std::bit_cast<float>(0x3e4ccccdu);
    for (const Probe& probe :
         {Probe{0x5e8, 0, {0.5f, std::bit_cast<float>(0x3f333333u), 1.0f}, 16.0f, -50.0f},
          Probe{0x5e8, 1, glm::vec3(1.0f), 3.0f, -50.0f}, Probe{0x5e9, 0, {1.0f, orange, ember}, 16.0f, -50.0f},
          Probe{0x5ea, 1, {1.0f, orange, ember}, 5.0f, -15.0f}})
    {
        SCOPED_TRACE(probe.type);
        HighWorld tested;
        const ModelEffectArguments arguments{.type = probe.type,
                                             .position = {300, 400, 50},
                                             .angles = {0, 0, 90},
                                             .light = glm::vec3(0.25f),
                                             .subtype = probe.subtype,
                                             .owner = &tested.world->Actors()[2]};
        ASSERT_TRUE(CreateHighModelEffect(arguments, tested.Context()).HasValue());
        ModelEffect& ray = tested.Effect(0);
        EXPECT_EQ(ray.life, 20);
        EXPECT_EQ(ray.light, probe.light);
        EXPECT_EQ(ray.transform.scale, probe.scale);
        EXPECT_EQ(ray.velocity, Client::Math::OriginalVectorRotate(
                                    {0.0f, probe.rush, 0.0f}, Client::Math::OriginalAngleMatrix(arguments.angles)));
        EXPECT_EQ(tested.Draws(), 0);
        ASSERT_TRUE(StepHighModelEffect(ray, nullptr, tested.StepContext()).HasValue());
        EXPECT_EQ(ray.transform.position, arguments.position);
        EXPECT_EQ(ray.life, 19);
        EXPECT_EQ(tested.Draws(), 1);
    }
    HighWorld full;
    full.FillTheEffects();
    const auto none =
        CreateHighModelEffect({.type = 0x5e8, .owner = &full.world->Actors()[HighWorld::HeroSlot]}, full.Context());
    ASSERT_TRUE(none.HasValue()) << none.Error();
    EXPECT_FALSE(none.Value().has_value());
    EXPECT_EQ(full.Draws(), 0);
}

// 0x51e6bf..0x51e76a: with the hero as owner, four 0x517a20 at 150, 300, 450 and 600 ahead of the ray; each opens and
// closes the own record (its rand()) and, without targets, sends nothing.
TEST(HighModelEffectsTest, TheHerosMovingLightLooksForTargetsFourTimes)
{
    HighWorld tested;
    int sent = 0;
    HighEffectContext context = tested.Context();
    context.send = [&sent](Client::Protocol::Outgoing) { ++sent; };
    ASSERT_TRUE(
        CreateHighModelEffect(
            {.type = 0x5e8, .position = {300, 400, 50}, .owner = &tested.world->Actors()[HighWorld::HeroSlot]}, context)
            .HasValue());
    EXPECT_EQ(tested.Draws(), 4 * Client::Models::OwnRecordCloseRandoms);
    EXPECT_EQ(sent, 0);
}
// 0x51f048..0x51f153: the 0x625 with its life, +0xe4, +0xd8, light at 0, the scale and the subtype's spread, z 500
// more, x 100 more, the start at the position and sound 0x42; with the table full, nothing and no rand().
TEST(HighModelEffectsTest, TheFallingStarSpreadsAboveItsTarget)
{
    for (const std::int32_t subtype : {0, 1})
    {
        SCOPED_TRACE(subtype);
        HighWorld tested;
        const ModelEffectArguments arguments{.type = 0x625,
                                             .position = {1000, 2000, 30},
                                             .angles = {0, 0, 45},
                                             .light = glm::vec3(1.0f),
                                             .subtype = subtype};
        const auto made = CreateHighModelEffect(arguments, tested.Context());
        ASSERT_TRUE(made.HasValue()) << made.Error();
        ASSERT_EQ(made.Value(), std::optional<std::size_t>{0});
        CrtRandom expected(Seed);
        const int life = expected.Next() % 15 + 15;
        const auto speed = static_cast<float>(expected.Next() % 360);
        float rise = -20.0f;
        const int spread = subtype == 1 ? 300 : 200;
        const double half = subtype == 1 ? 150.0 : 100.0;
        if (subtype == 1)
            rise = static_cast<float>(-20.0 - static_cast<double>(expected.Next() % 20 + 10));
        const auto x = static_cast<float>(static_cast<double>(expected.Next() % spread) + 1000.0 - half);
        const auto y = static_cast<float>(static_cast<double>(expected.Next() % spread) + 2000.0 - half);
        const glm::vec3 position{static_cast<float>(static_cast<double>(x) + 100.0), y, 530.0f};
        const ModelEffect& star = tested.Effect(0);
        EXPECT_EQ(star.type, 0x625);
        EXPECT_EQ(star.life, life);
        EXPECT_EQ(star.speed, speed);
        EXPECT_EQ(star.turnDirection, rise);
        EXPECT_EQ(star.light, glm::vec3(0.0f));
        EXPECT_EQ(star.transform.scale, subtype == 1 ? 0.5f : 0.0f);
        EXPECT_EQ(star.transform.position, position);
        EXPECT_EQ(star.start, position);
        EXPECT_EQ(tested.Draws(), subtype == 1 ? 5 : 4);
        EXPECT_EQ(tested.world->Runtime().lastSoundId, Client::Audio::SoundId{0x42});
    }
    HighWorld full;
    full.FillTheEffects();
    const auto none = CreateHighModelEffect({.type = 0x625}, full.Context());
    ASSERT_TRUE(none.HasValue()) << none.Error();
    EXPECT_FALSE(none.Value().has_value());
    EXPECT_EQ(full.Draws(), 0);
    EXPECT_NE(full.world->Runtime().lastSoundId, Client::Audio::SoundId{0x42});
}

// A live 0x625 in slot 0, with everything else as freshly constructed.
ModelEffect& FallingStarAt(HighWorld& tested, std::int32_t subtype, std::int32_t life)
{
    ModelEffect& star = tested.Effect(0);
    star = ModelEffect{};
    star.alive = true;
    star.type = 0x625;
    star.subtype = subtype;
    star.life = life;
    return star;
}

// 0x52be78: with life above 15, the 0x625 only draws the local light and loses a life.
TEST(HighModelEffectsTest, TheFallingStarWaitsUntilLifeFifteen)
{
    HighWorld tested;
    ModelEffect& star = FallingStarAt(tested, 0, 16);
    star.transform.position = {10, 20, 30};
    star.turnDirection = -20.0f;
    ASSERT_TRUE(StepHighModelEffect(star, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(star.life, 15);
    EXPECT_EQ(star.transform.position, glm::vec3(10, 20, 30));
    EXPECT_EQ(star.turnDirection, -20.0f);
    EXPECT_EQ(tested.Draws(), 1);
}

// 0x52be8d..0x52bfce: the /0 wobbles around the start, lowers +0xe4, shifts the start, releases the 0x5db/7 fire (with
// the particle table full, nothing and no rand()), raises the light 0.1f and leaves the 0x5fd and 0x5ac sprites; with a
// slot for the fire, it stops at 0x52bf29 after the wobble's two rand().
TEST(HighModelEffectsTest, TheWobblyFallingStarLeavesItsFireAndTwoSprites)
{
    HighWorld tested;
    auto sprites = std::make_unique<Client::Particles::FrameSprites>();
    tested.services.sprites = sprites.get();
    tested.FillTheParticles();
    ModelEffect& star = FallingStarAt(tested, 0, 15);
    star.transform.position = {10, 20, 30};
    star.transform.scale = 0.0f;
    star.start = {500, 600, 700};
    star.turnDirection = -20.0f;
    star.light = glm::vec3(0.0f);
    ASSERT_TRUE(StepHighModelEffect(star, nullptr, tested.StepContext()).HasValue());
    CrtRandom expected(Seed);
    static_cast<void>(expected.Next());
    const double rate = static_cast<double>(std::bit_cast<float>(0x3c23d70au));
    glm::vec3 position{0.0f, 0.0f, 10.0f};
    for (int axis = 0; axis < 2; ++axis)
    {
        const auto wobble =
            Client::Math::Detail::SineProductPC53(static_cast<double>(expected.Next() % 1000) * rate, 10.0f);
        ASSERT_TRUE(wobble.has_value());
        position[axis] = static_cast<float>(*wobble + (axis == 0 ? 500.0 : 600.0));
    }
    const auto shineTurn = static_cast<float>(expected.Next() % 360);
    const auto shineScale = static_cast<float>(static_cast<double>((expected.Next() & 3) + 4) *
                                               static_cast<double>(std::bit_cast<float>(0x3e4ccccdu)));
    const auto flareTurn = static_cast<float>(expected.Next() % 360);
    EXPECT_EQ(star.transform.position, position);
    EXPECT_EQ(star.turnDirection, -22.0f);
    EXPECT_EQ(star.start, glm::vec3(490, 600, 700));
    const float glow = std::bit_cast<float>(0x3dcccccdu);
    EXPECT_EQ(star.light, glm::vec3(glow));
    EXPECT_EQ(star.life, 14);
    EXPECT_EQ(tested.Draws(), 6);
    const auto made = sprites->Sprites();
    ASSERT_TRUE(made[0].alive);
    EXPECT_EQ(made[0].texture, 0x5fd);
    EXPECT_EQ(made[0].position, position);
    EXPECT_EQ(made[0].scale, shineScale);
    EXPECT_EQ(made[0].rotation, shineTurn);
    EXPECT_EQ(made[0].light, glm::vec3(glow));
    ASSERT_TRUE(made[1].alive);
    EXPECT_EQ(made[1].texture, 0x5ac);
    EXPECT_EQ(made[1].scale, 1.0f);
    EXPECT_EQ(made[1].rotation, flareTurn);
    EXPECT_FALSE(made[2].alive);

    // With a slot, the 0x5db/7 fire (0x52bf29) draws its rand() between the wobble and the sprites.
    HighWorld room;
    room.services.sprites = sprites.get();
    ModelEffect& next = FallingStarAt(room, 0, 3);
    const auto lit = StepHighModelEffect(next, nullptr, room.StepContext());
    ASSERT_TRUE(lit.HasValue()) << lit.Error();
    CrtRandom replay(Seed);
    Skip(replay, 3);
    ReplayParticle(replay, Client::Particles::DeathSpiritFireParticle, 7);
    Skip(replay, 3);
    EXPECT_EQ(room.random.State(), replay.State());
    EXPECT_EQ(AliveParticles(*room.world, Client::Particles::DeathSpiritFireParticle, 7), 1);
}

// 0x52bfdc..0x52c195: the /1 stays at the start and, with the terrain above, draws the scale of the 0x5f2/0xb smoke
// and the rand() % 5 that, at 0, releases the 0xca ice piece 50 higher; with the terrain below, none of that. With a
// slot, the 0x5db/0xb fire (0x52c036) draws its rand() before the sprites.
TEST(HighModelEffectsTest, TheStraightFallingStarSmokesAndDropsIceBelowTheTerrain)
{
    for (const double ground : {-1000.0, 1000.0})
    {
        SCOPED_TRACE(ground);
        HighWorld tested;
        auto sprites = std::make_unique<Client::Particles::FrameSprites>();
        tested.services.sprites = sprites.get();
        tested.services.terrainHeight = [ground](float, float) { return ground; };
        tested.FillTheParticles();
        ModelEffect& star = FallingStarAt(tested, 1, 10);
        star.transform.scale = 0.5f;
        star.start = {500, 600, 700};
        star.transform.position = {1, 2, 100};
        star.turnDirection = -30.0f;
        ASSERT_TRUE(StepHighModelEffect(star, nullptr, tested.StepContext()).HasValue());
        EXPECT_EQ(star.transform.position, glm::vec3(500, 600, 70));
        EXPECT_EQ(star.turnDirection, -32.0f);
        EXPECT_EQ(star.start.x, 490.0f);
        CrtRandom expected(Seed);
        for (int draw = 0; draw < 4; ++draw)
            static_cast<void>(expected.Next());
        if (ground < 0)
        {
            EXPECT_EQ(tested.Draws(), 4);
            EXPECT_FALSE(tested.Effect(1).alive);
            continue;
        }
        static_cast<void>(expected.Next());
        const bool shard = expected.Next() % 5 == 0;
        const ModelEffect& ice = tested.Effect(1);
        EXPECT_EQ(ice.alive, shard);
        if (shard)
        {
            EXPECT_EQ(ice.type, 0xca);
            EXPECT_EQ(ice.transform.position.x, 500.0f);
            EXPECT_EQ(ice.transform.position.y, 600.0f);
        }
        EXPECT_EQ(tested.Draws(), DrawsSince(Seed, expected) + (shard ? 5 : 0));
    }
    HighWorld room;
    ModelEffect& star = FallingStarAt(room, 1, 10);
    const auto cut = StepHighModelEffect(star, nullptr, room.StepContext());
    EXPECT_TRUE(HasError(cut, "0x52c0a5")) << (cut.HasValue() ? std::string("sin corte") : cut.Error());
    CrtRandom replay(Seed);
    Skip(replay, 1);
    ReplayParticle(replay, Client::Particles::DeathSpiritFireParticle, 0xb);
    Skip(replay, 2);
    EXPECT_EQ(room.random.State(), replay.State());
    EXPECT_EQ(AliveParticles(*room.world, Client::Particles::DeathSpiritFireParticle, 0xb), 1);
}
// 0x51ca31: the 0x116/0 stores the light it receives as the start and sets its own to 1; the /2 drops 20; the /1 takes
// its owner's life (another effect) and releases flame 0x5da/0 (0x51cb38) with its rand(); with the particle table
// full, it goes on without rand().
TEST(HighModelEffectsTest, ThePierPartsTakeTheirAimLifeAndFlame)
{
    HighWorld tested;
    const ModelEffectArguments homing{.type = 0x116,
                                      .position = {10, 20, 30},
                                      .angles = {5, 6, 70},
                                      .light = {0, 0, 150},
                                      .owner = &tested.world->Actors()[3]};
    ASSERT_TRUE(CreateHighModelEffect(homing, tested.Context()).HasValue());
    const ModelEffect& part = tested.Effect(0);
    EXPECT_EQ(part.life, 20);
    EXPECT_EQ(part.turnDirection, 2.0f);
    EXPECT_EQ(part.speed, 10.0f);
    EXPECT_EQ(part.objectState.hiddenMesh, 1);
    EXPECT_EQ(part.transform.scale, std::bit_cast<float>(0x3f99999au));
    EXPECT_EQ(part.light, glm::vec3(1.0f));
    EXPECT_EQ(part.velocity, (glm::vec3{0, -26, 0}));
    EXPECT_EQ(part.start, (glm::vec3{0, 0, 150}));
    EXPECT_EQ(part.copiedAngles, (glm::vec3{5, 6, 70}));
    EXPECT_EQ(part.transform.angles, (glm::vec3{0, 0, 70}));

    ModelEffectArguments diving = homing;
    diving.subtype = 2;
    ASSERT_TRUE(CreateHighModelEffect(diving, tested.Context()).HasValue());
    const ModelEffect& dive = tested.Effect(1);
    EXPECT_EQ(dive.life, 20);
    EXPECT_EQ(dive.speed, 50.0f);
    EXPECT_EQ(dive.objectState.hiddenMesh, -2);
    EXPECT_EQ(dive.transform.position, (glm::vec3{10, 20, 10}));
    EXPECT_EQ(dive.velocity, (glm::vec3{0, -40, 0}));
    EXPECT_EQ(dive.transform.angles, (glm::vec3{0, 0, 70}));

    tested.Effect(0).life = 12;
    const ModelEffectArguments trail{.type = 0x116, .position = {1, 2, 3}, .subtype = 1, .owner = &tested.Effect(0)};
    ASSERT_TRUE(CreateHighModelEffect(trail, tested.Context()).HasValue());
    EXPECT_EQ(AliveParticles(*tested.world, Client::Particles::MissileFireParticle, 0), 1);
    CrtRandom replay(Seed);
    ReplayParticle(replay, Client::Particles::MissileFireParticle, 0);
    EXPECT_EQ(tested.random.State(), replay.State());
    tested.random = CrtRandom(Seed);
    tested.FillTheParticles();
    tested.Effect(2).alive = false;
    ASSERT_TRUE(CreateHighModelEffect(trail, tested.Context()).HasValue());
    const ModelEffect& flame = tested.Effect(2);
    EXPECT_EQ(flame.life, 12);
    EXPECT_EQ(flame.objectState.hiddenMesh, 0);
    EXPECT_EQ(flame.transform.scale, 0.5f);
    EXPECT_EQ(flame.alpha, static_cast<float>(8.0 * static_cast<double>(std::bit_cast<float>(0x3e4ccccdu))));
    EXPECT_EQ(flame.velocity, glm::vec3(0.0f));
    EXPECT_EQ(tested.Draws(), 0);
}

// 0x52727b..0x527447: the 0x116/0 with +0xe4 at 2 does one turn (one rand(), the turn toward its owner plus the start,
// +0xd8 0.4f more, the advance and the 0x116/1, which with a slot releases its 0x5da/0 flame), and then +0xe4 0.1f more
// and sound 0x1ca; without an owner, only the local light.
TEST(HighModelEffectsTest, TheHomingPartTurnsTowardItsOwnerAndLeavesItsTrail)
{
    HighWorld tested;
    ActorState& owner = tested.world->Actors()[3];
    owner.position = {1000, 0, 0};
    const ModelEffectArguments homing{.type = 0x116, .position = {0, 0, 0}, .light = {0, 0, 100}, .owner = &owner};
    ASSERT_TRUE(CreateHighModelEffect(homing, tested.Context()).HasValue());
    ModelEffect& part = tested.Effect(0);
    tested.FillTheParticles();
    ASSERT_TRUE(StepHighModelEffect(part, nullptr, tested.StepContext()).HasValue());
    CrtRandom expected(Seed);
    static_cast<void>(expected.Next());
    glm::vec3 angles{0, 0, 0};
    if ((expected.Next() & 1) == 0)
        angles.x = -20.0f;
    static_cast<void>(Client::Math::MoveToward({0, 0, 0}, angles, {1000, 0, 100}, 10.0f));
    const glm::vec3 moved = Client::Math::OriginalVectorRotate({0, -26, 0}, Client::Math::OriginalAngleMatrix(angles));
    EXPECT_EQ(part.transform.angles, angles);
    EXPECT_EQ(part.transform.position, moved);
    EXPECT_EQ(part.speed, static_cast<float>(10.0 + static_cast<double>(std::bit_cast<float>(0x3ecccccdu))));
    EXPECT_EQ(part.turnDirection, static_cast<float>(2.0 + static_cast<double>(std::bit_cast<float>(0x3dcccccdu))));
    EXPECT_EQ(part.life, 19);
    EXPECT_EQ(tested.world->Runtime().lastSoundId, Client::Audio::SoundId{0x1ca});
    const ModelEffect& trail = tested.Effect(1);
    ASSERT_TRUE(trail.alive);
    EXPECT_EQ(trail.subtype, 1);
    EXPECT_EQ(trail.ownerContext, &part);
    EXPECT_EQ(trail.transform.position, moved);
    EXPECT_EQ(trail.life, 20);
    EXPECT_EQ(tested.Draws(), 2);

    HighWorld room;
    ASSERT_TRUE(CreateHighModelEffect({.type = 0x116, .owner = &room.world->Actors()[3]}, room.Context()).HasValue());
    ASSERT_TRUE(StepHighModelEffect(room.Effect(0), nullptr, room.StepContext()).HasValue());
    CrtRandom replay(Seed);
    Skip(replay, 2);
    ReplayParticle(replay, Client::Particles::MissileFireParticle, 0);
    EXPECT_EQ(room.random.State(), replay.State());
    EXPECT_EQ(AliveParticles(*room.world, Client::Particles::MissileFireParticle, 0), 1);

    HighWorld alone;
    ASSERT_TRUE(CreateHighModelEffect({.type = 0x116}, alone.Context()).HasValue());
    ASSERT_TRUE(StepHighModelEffect(alone.Effect(0), nullptr, alone.StepContext()).HasValue());
    EXPECT_EQ(alone.Effect(0).turnDirection, 2.0f);
    EXPECT_EQ(alone.Draws(), 1);
}

// 0x527455..0x527534 and 0x527252: the 0x116/2 turns toward its owner, +0xd8 2.4f more and advances; with life a
// multiple of 3, joint 0x61c/2 (creation 0x534578, no rand()) with scale 150, 10 higher; full, it goes on the same. The
// /1, with life under 5, alpha times 0.7692308f.
TEST(HighModelEffectsTest, TheDivingPartDropsAJointEveryThreeSteps)
{
    HighWorld tested;
    ActorState& owner = tested.world->Actors()[3];
    owner.position = {0, 1000, 0};
    ASSERT_TRUE(
        CreateHighModelEffect({.type = 0x116, .angles = {0, 0, 0}, .subtype = 2, .owner = &owner}, tested.Context())
            .HasValue());
    ModelEffect& dive = tested.Effect(0);
    dive.life = 10;
    ASSERT_TRUE(StepHighModelEffect(dive, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(dive.speed, static_cast<float>(50.0 + static_cast<double>(std::bit_cast<float>(0x4019999au))));
    EXPECT_EQ(dive.life, 9);
    EXPECT_FALSE(tested.world->DrawTail().joints.front().alive);
    const auto dropped = StepHighModelEffect(dive, nullptr, tested.StepContext());
    ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
    const auto& joint = tested.world->DrawTail().joints.front();
    ASSERT_TRUE(joint.alive);
    EXPECT_EQ(joint.type, 0x61c);
    EXPECT_EQ(joint.subtype, 2);
    EXPECT_EQ(joint.texture, 0x633);
    EXPECT_EQ(joint.width, 150.0f);
    EXPECT_EQ(joint.life, 15);
    EXPECT_EQ(joint.position.x, dive.transform.position.x);
    EXPECT_EQ(joint.position.y, dive.transform.position.y);
    EXPECT_EQ(joint.position.z, static_cast<float>(static_cast<double>(dive.transform.position.z) + 10.0));
    EXPECT_EQ(joint.bendAngles, (glm::vec3{0, 0, dive.transform.angles.z}));
    EXPECT_FALSE(tested.world->DrawTail().joints[1].alive);
    tested.FillTheJoints();
    dive.life = 9;
    ASSERT_TRUE(StepHighModelEffect(dive, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(tested.Draws(), 3);

    ModelEffect& trail = tested.Effect(1);
    trail = ModelEffect{};
    trail.alive = true;
    trail.type = 0x116;
    trail.subtype = 1;
    trail.life = 5;
    trail.alpha = 0.5f;
    ASSERT_TRUE(StepHighModelEffect(trail, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(trail.alpha, 0.5f);
    ASSERT_TRUE(StepHighModelEffect(trail, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(trail.alpha, static_cast<float>(0.5 * static_cast<double>(std::bit_cast<float>(0x3f44ec4fu))));
    EXPECT_EQ(trail.life, 3);
}

// 0x51cbb2 and 0x527539: the 0x117 is born with life 10, scale 0.2f and +0xd8 0.1f; with subtype 1 or less, a rand() %
// 2 picks between the tilt subtype · −90 + 45 and the fixed one; the /2, life 12 and +0xd8 0.4f. Its step grows and
// fades +0x68.
TEST(HighModelEffectsTest, TheDarkLordSkillTiltsByItsSubtypeAndGrows)
{
    for (const std::int32_t subtype : {-1, 0, 1, 2, 3})
    {
        SCOPED_TRACE(subtype);
        HighWorld tested;
        ASSERT_TRUE(CreateHighModelEffect({.type = 0x117, .angles = {1, 2, 3}, .subtype = subtype}, tested.Context())
                        .HasValue());
        ModelEffect& hand = tested.Effect(0);
        CrtRandom expected(Seed);
        if (subtype <= 1)
        {
            const float yaw =
                expected.Next() % 2 != 0 ? static_cast<float>(subtype * -90.0 + 45.0) : (subtype == 0 ? 45.0f : -45.0f);
            EXPECT_EQ(hand.transform.angles, (glm::vec3{45, yaw, 0}));
            EXPECT_EQ(tested.Draws(), 1);
        }
        else
        {
            EXPECT_EQ(hand.transform.angles, (glm::vec3{1, 2, 3}));
            EXPECT_EQ(tested.Draws(), 0);
        }
        EXPECT_EQ(hand.life, subtype == 2 ? 12 : 10);
        EXPECT_EQ(hand.transform.scale, std::bit_cast<float>(0x3e4ccccdu));
        const float growth = std::bit_cast<float>(subtype == 2 ? 0x3ecccccdu : 0x3dcccccdu);
        EXPECT_EQ(hand.speed, growth);
        hand.life = 7;
        hand.objectState.light = 0.9f;
        ASSERT_TRUE(StepHighModelEffect(hand, nullptr, tested.StepContext()).HasValue());
        EXPECT_EQ(hand.transform.scale,
                  static_cast<float>(static_cast<double>(std::bit_cast<float>(0x3e4ccccdu)) + growth));
        const std::uint32_t change = subtype <= 1 ? 0x3ca3d70au : subtype == 2 ? 0x3d75c28fu : 0x3d23d70au;
        const double sign = subtype == 2 ? -1.0 : 1.0;
        EXPECT_EQ(hand.speed, static_cast<float>(static_cast<double>(growth) +
                                                 sign * static_cast<double>(std::bit_cast<float>(change))));
        const std::uint32_t fade = subtype == 2 ? 0x3f2aaaabu : 0x3f0e38e4u;
        EXPECT_EQ(hand.objectState.light,
                  subtype <= 1 ? 0.9f : static_cast<float>(0.9 * static_cast<double>(std::bit_cast<float>(fade))));
        EXPECT_EQ(hand.life, 6);
    }
}

// 0x51da46 and 0x529a4f: the 0x138/0 is born 300 higher, hidden until life 40, grows from 40 and, past 1, releases the
// 0x138/1 with its light; below 15 it shrinks, fades with its life and sinks. The /1 shrinks down to 1.
TEST(HighModelEffectsTest, TheManaRuneGrowsFlashesAndSinks)
{
    HighWorld tested;
    ASSERT_TRUE(
        CreateHighModelEffect({.type = 0x138, .position = {10, 20, 30}, .light = glm::vec3(0.5f)}, tested.Context())
            .HasValue());
    ModelEffect& rune = tested.Effect(0);
    EXPECT_EQ(rune.life, 50);
    EXPECT_EQ(rune.transform.scale, 0.0f);
    EXPECT_EQ(rune.alpha, std::bit_cast<float>(0x3e99999au));
    EXPECT_EQ(rune.transform.position, (glm::vec3{10, 20, 330}));
    EXPECT_EQ(rune.transform.angles, (glm::vec3{0, 0, 45}));
    ASSERT_TRUE(StepHighModelEffect(rune, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(rune.objectState.hiddenMesh, -2);
    rune.life = 41;
    ASSERT_TRUE(StepHighModelEffect(rune, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(rune.objectState.hiddenMesh, 0);
    int steps = 0;
    while (!tested.Effect(1).alive && steps < 10)
    {
        ASSERT_TRUE(StepHighModelEffect(rune, nullptr, tested.StepContext()).HasValue());
        ++steps;
    }
    ASSERT_TRUE(tested.Effect(1).alive);
    EXPECT_EQ(rune.transform.scale, 1.0f);
    EXPECT_EQ(rune.turnDirection, std::bit_cast<float>(0x3c23d70au));
    const ModelEffect& flash = tested.Effect(1);
    EXPECT_EQ(flash.subtype, 1);
    EXPECT_EQ(flash.life, 10);
    EXPECT_EQ(flash.transform.scale, std::bit_cast<float>(0x3f8ccccdu));
    EXPECT_EQ(flash.objectState.blendMesh, -2);
    EXPECT_EQ(flash.objectState.light, std::bit_cast<float>(0x3ecccccdu));
    EXPECT_EQ(flash.light, glm::vec3(0.5f));
    rune.life = 14;
    ASSERT_TRUE(StepHighModelEffect(rune, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(rune.alpha, static_cast<float>(14.0 * static_cast<double>(std::bit_cast<float>(0x3d4ccccdu))));
    EXPECT_EQ(rune.transform.position.z, 320.0f);
    EXPECT_EQ(tested.Draws(), steps + 3);

    ModelEffect& shrinking = tested.Effect(1);
    shrinking.transform.scale = 1.01f;
    ASSERT_TRUE(StepHighModelEffect(shrinking, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(shrinking.transform.scale, 1.0f);
}
// 0x52c981 and 0x52caa5: each step the 0x812 releases joint 0x60f/0xc (the /1, scale 100) or 0x60f/4 (the /0, scale
// 50) owned by it (HelixJoints.cpp: one rand(), vx; the /4 takes its light), and the 0x813 three 0x619/6 from the
// point 100 behind to its position, with two rand() each and no owner. With the table full, they go on without the
// joints. Both lose a life and do not move.
TEST(HighModelEffectsTest, TheEmittersLeaveJointsEveryStep)
{
    for (const std::int32_t subtype : {0, 1})
    {
        SCOPED_TRACE(subtype);
        HighWorld tested;
        ASSERT_TRUE(CreateHighModelEffect({.type = 0x812, .position = {1, 2, 3}, .subtype = subtype}, tested.Context())
                        .HasValue());
        ModelEffect& spark = tested.Effect(0);
        spark.light = {0.25f, 0.5f, 0.75f};
        EXPECT_EQ(spark.life, 10);
        const auto stepped = StepHighModelEffect(spark, nullptr, tested.StepContext());
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(tested.Draws(), 2);
        CrtRandom expected(Seed);
        static_cast<void>(expected.Next());
        const auto& helix = tested.world->DrawTail().joints.front();
        ASSERT_TRUE(helix.alive);
        EXPECT_EQ(helix.type, 0x60f);
        EXPECT_EQ(helix.subtype, subtype == 1 ? 0xc : 4);
        EXPECT_EQ(helix.ownerContext, &spark);
        EXPECT_EQ(helix.width, subtype == 1 ? 100.0f : 50.0f);
        EXPECT_EQ(helix.life, subtype == 1 ? 70 : 110);
        EXPECT_EQ(helix.maximumCount, 50);
        EXPECT_EQ(helix.light,
                  subtype == 1 ? glm::vec3(std::bit_cast<float>(0x3dcccccdu), std::bit_cast<float>(0x3dcccccdu), 1.0f)
                               : spark.light);
        EXPECT_EQ(helix.velocity.x, static_cast<float>(expected.Next() % 360));
        EXPECT_EQ(helix.targetPosition, (glm::vec3{1, 2, 3}));
        tested.FillTheJoints();
        ASSERT_TRUE(StepHighModelEffect(spark, nullptr, tested.StepContext()).HasValue());
        EXPECT_EQ(spark.life, 8);
        EXPECT_EQ(spark.transform.position, (glm::vec3{1, 2, 3}));
        EXPECT_EQ(tested.Draws(), 3);
    }
    HighWorld tested;
    ASSERT_TRUE(
        CreateHighModelEffect({.type = 0x813, .angles = {4, 5, 6}, .subtype = 0x812}, tested.Context()).HasValue());
    ModelEffect& bolt = tested.Effect(0);
    EXPECT_EQ(bolt.life, 5);
    EXPECT_EQ(bolt.transform.angles, glm::vec3(0.0f));
    const auto bolted = StepHighModelEffect(bolt, nullptr, tested.StepContext());
    ASSERT_TRUE(bolted.HasValue()) << bolted.Error();
    EXPECT_EQ(tested.Draws(), 7);
    CrtRandom expected(Seed);
    static_cast<void>(expected.Next());
    for (std::size_t made = 0; made < 3; ++made)
    {
        SCOPED_TRACE(made);
        const auto& energy = tested.world->DrawTail().joints[made];
        glm::vec3 angles{0.0f};
        angles.x = static_cast<float>(expected.Next() % 90);
        angles.z = static_cast<float>(expected.Next() % 360);
        const glm::vec3 back =
            Client::Math::OriginalVectorRotate({0.0f, -100.0f, 0.0f}, Client::Math::OriginalAngleMatrix(angles));
        const glm::vec3& at = bolt.transform.position;
        ASSERT_TRUE(energy.alive);
        EXPECT_EQ(energy.type, 0x619);
        EXPECT_EQ(energy.subtype, 6);
        EXPECT_EQ(energy.angles, angles);
        EXPECT_EQ(energy.position,
                  glm::vec3(static_cast<float>(static_cast<double>(at.x) - static_cast<double>(back.x)),
                            static_cast<float>(static_cast<double>(at.y) - static_cast<double>(back.y)),
                            static_cast<float>(static_cast<double>(at.z) - static_cast<double>(back.z))));
        EXPECT_EQ(energy.targetPosition, at);
        EXPECT_EQ(energy.width, 5.0f);
        EXPECT_EQ(energy.ownerContext, nullptr);
    }
    tested.FillTheJoints();
    ASSERT_TRUE(StepHighModelEffect(bolt, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(bolt.life, 3);
    EXPECT_EQ(tested.Draws(), 7 + 7);
}
// Each axis's fld/fadd/fstp, like HighModelEffects's Sum.
glm::vec3 SumOf(const glm::vec3& a, const glm::vec3& b)
{
    return {static_cast<float>(static_cast<double>(a.x) + static_cast<double>(b.x)),
            static_cast<float>(static_cast<double>(a.y) + static_cast<double>(b.y)),
            static_cast<float>(static_cast<double>(a.z) + static_cast<double>(b.z))};
}

glm::vec3 TurnedBy(const glm::vec3& vector, const glm::vec3& angles)
{
    return Client::Math::OriginalVectorRotate(vector, Client::Math::OriginalAngleMatrix(angles));
}

// 0x51f5fe..0x51f7f8: the 0x6ac lives 10, with +0xd8 at 1 and speed (0, −30, 0), shifted by (0, −20, 50) rotated. Its
// two mouths, (−20, −20, 60) and (30, −20, 60) rotated from the position it receives (not the shifted one), release
// twenty 0x617/1 each: rand() % 20 + x + 5 in the angles' x, and the y that adds the turns of 18; two more rand() per
// joint (0x5320a4). With the joints full, only the creation's forty rand(); with the effect table full, nothing.
TEST(HighModelEffectsTest, TheFireBreathRaisesFortyJointsFromTwoMouths)
{
    const glm::vec3 origin{1000, 2000, 300};
    const glm::vec3 angles{10, 20, 30};
    HighWorld tested;
    const auto made = CreateHighModelEffect({.type = 0x6ac, .position = origin, .angles = angles}, tested.Context());
    ASSERT_TRUE(made.HasValue()) << made.Error();
    ASSERT_EQ(made.Value(), std::optional<std::size_t>{0});
    const ModelEffect& breath = tested.Effect(0);
    EXPECT_EQ(breath.life, 10);
    EXPECT_EQ(breath.speed, 1.0f);
    EXPECT_EQ(breath.velocity, (glm::vec3{0, -30, 0}));
    EXPECT_EQ(breath.transform.position, SumOf(TurnedBy({0, -20, 50}, angles), origin));
    CrtRandom expected{Seed};
    const std::array<glm::vec3, 2> mouths{SumOf(TurnedBy({-20, -20, 60}, angles), origin),
                                          SumOf(TurnedBy({30, -20, 60}, angles), origin)};
    std::size_t index = 0;
    for (const glm::vec3& mouth : mouths)
    {
        float yaw = angles.y;
        for (int turn = 0; turn < 360; turn += 18, ++index)
        {
            SCOPED_TRACE(index);
            const int tilt = expected.Next() % 20;
            static_cast<void>(expected.Next());
            static_cast<void>(expected.Next());
            yaw = static_cast<float>(static_cast<double>(turn) + static_cast<double>(yaw));
            const auto& joint = tested.world->DrawTail().joints[index];
            ASSERT_TRUE(joint.alive);
            EXPECT_EQ(joint.type, 0x617);
            EXPECT_EQ(joint.subtype, 1);
            EXPECT_EQ(joint.position, mouth);
            EXPECT_EQ(joint.angles,
                      (glm::vec3{static_cast<float>(static_cast<double>(tilt) + static_cast<double>(angles.x) + 5.0),
                                 yaw, angles.z}));
            EXPECT_EQ(joint.width, 2.0f);
        }
    }
    EXPECT_FALSE(tested.world->DrawTail().joints[40].alive);
    EXPECT_EQ(tested.random.State(), expected.State());
    EXPECT_EQ(tested.Draws(), 40 * 3);

    HighWorld crowded;
    crowded.FillTheJoints();
    ASSERT_TRUE(
        CreateHighModelEffect({.type = 0x6ac, .position = origin, .angles = angles}, crowded.Context()).HasValue());
    EXPECT_EQ(crowded.Effect(0).transform.position, breath.transform.position);
    EXPECT_EQ(crowded.Draws(), 40);

    HighWorld full;
    full.FillTheEffects();
    const auto none = CreateHighModelEffect({.type = 0x6ac}, full.Context());
    ASSERT_TRUE(none.HasValue());
    EXPECT_FALSE(none.Value().has_value());
    EXPECT_EQ(full.Draws(), 0);
}

// 0x52c7f0..0x52c97c: the 0x6ac's step turns on the dynamic light with radius 2, draws rand() % 3 + 2 for the scale (·
// (15 − life) · 0.05f) and puts the start 20 to each side (the angles with z 90 more) with the 0x5db/0xa fire
// (0x52c8f7, 0x52c95d; particle batch), which with a slot draws its rand(); with the particles full, it goes on without
// them, moves (0, −30, 0) rotated and, at life 1, 0x51fef0 draws the forty rand() of its twenty 0x5c5/2 sparks
// (without a slot, neither they nor the 0x5ee/0) and dies.
TEST(HighModelEffectsTest, TheFireBreathBurnsAtItsSidesAndSparksAtTheEnd)
{
    const glm::vec3 angles{10, 20, 30};
    HighWorld tested;
    tested.FillTheJoints();
    ASSERT_TRUE(
        CreateHighModelEffect({.type = 0x6ac, .position = {1000, 2000, 300}, .angles = angles}, tested.Context())
            .HasValue());
    ModelEffect& breath = tested.Effect(0);
    ASSERT_EQ(tested.Draws(), 40);
    const auto stepped = StepHighModelEffect(breath, nullptr, tested.StepContext());
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    CrtRandom replay{Seed};
    Skip(replay, 40 + 2);
    ReplayParticle(replay, Client::Particles::DeathSpiritFireParticle, 0xa, breath.transform.scale);
    ReplayParticle(replay, Client::Particles::DeathSpiritFireParticle, 0xa, breath.transform.scale);
    EXPECT_EQ(tested.random.State(), replay.State());
    EXPECT_EQ(AliveParticles(*tested.world, Client::Particles::DeathSpiritFireParticle, 0xa), 2);
    EXPECT_TRUE(std::ranges::any_of(tested.lighting->cells, [](const auto& cell) { return cell[0] > 0; }));

    HighWorld full;
    full.FillTheJoints();
    full.FillTheParticles();
    ASSERT_TRUE(CreateHighModelEffect({.type = 0x6ac, .position = {1000, 2000, 300}, .angles = angles}, full.Context())
                    .HasValue());
    ModelEffect& burning = full.Effect(0);
    CrtRandom expected{Seed};
    for (int draw = 0; draw < 40; ++draw)
        static_cast<void>(expected.Next());
    const glm::vec3 turned{angles.x, angles.y, 120.0f};
    for (std::int32_t life = 10; life >= 1; --life)
    {
        SCOPED_TRACE(life);
        const glm::vec3 before = burning.transform.position;
        ASSERT_TRUE(StepHighModelEffect(burning, nullptr, full.StepContext()).HasValue());
        static_cast<void>(expected.Next());
        const int size = expected.Next() % 3 + 2;
        EXPECT_EQ(burning.transform.scale,
                  static_cast<float>(static_cast<double>(size) *
                                     (static_cast<double>(15 - life) * static_cast<double>(0.05f))));
        EXPECT_EQ(burning.start, SumOf(TurnedBy({0, -20, 0}, turned), before));
        EXPECT_EQ(burning.transform.position, SumOf(TurnedBy({0, -30, 0}, angles), before));
        if (life == 1)
        {
            for (int draw = 0; draw < 40; ++draw)
                static_cast<void>(expected.Next());
        }
        EXPECT_EQ(full.random.State(), expected.State());
    }
    EXPECT_FALSE(burning.alive);
    EXPECT_EQ(full.Draws(), 40 + 10 * 2 + 40);
}
// 0x51fef0(point, 1) with a slot: the twenty 0x5c5/2 (0x51ff7e) and the 0x5ee/0 blaze (0x51ffd5) at the same point,
// with zero angles, light (1, 1, 1) and scale 4, and its creation's sound 0x40 (0x542aa7). With the table full, neither
// the blaze nor the sound.
TEST(HighModelEffectsTest, TheFireBreathEndsInABlaze)
{
    const glm::vec3 angles{10, 20, 30};
    for (const bool full : {false, true})
    {
        SCOPED_TRACE(full);
        HighWorld tested;
        tested.FillTheJoints();
        ASSERT_TRUE(
            CreateHighModelEffect({.type = 0x6ac, .position = {1000, 2000, 300}, .angles = angles}, tested.Context())
                .HasValue());
        if (full)
            tested.FillTheParticles();
        ModelEffect& breath = tested.Effect(0);
        for (int step = 0; step < 10 && breath.alive; ++step)
        {
            const auto stepped = StepHighModelEffect(breath, nullptr, tested.StepContext());
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        }
        EXPECT_FALSE(breath.alive);
        if (full)
        {
            EXPECT_NE(tested.world->Runtime().lastSoundId, Client::Audio::SoundId{0x40});
            continue;
        }
        EXPECT_EQ(AliveParticles(*tested.world, Client::Particles::FairySparkParticle, 2), 20);
        ASSERT_EQ(AliveParticles(*tested.world, 0x5ee, 0), 1);
        const auto& all = tested.world->Particles().All();
        const auto blaze = std::ranges::find_if(all, [](const Client::Particles::Particle& particle)
                                                { return particle.alive && particle.type == 0x5ee; });
        const auto spark = std::ranges::find_if(all, [](const Client::Particles::Particle& particle)
                                                { return particle.alive && particle.type == 0x5c5; });
        ASSERT_NE(blaze, all.end());
        ASSERT_NE(spark, all.end());
        EXPECT_EQ(blaze->position, spark->start);
        EXPECT_EQ(blaze->angles, glm::vec3(0.0f));
        EXPECT_EQ(blaze->light, glm::vec3(1.0f));
        EXPECT_EQ(blaze->scale, 4.0f);
        EXPECT_EQ(blaze->life, 12);
        EXPECT_EQ(tested.world->Runtime().lastSoundId, Client::Audio::SoundId{0x40});
    }
}

// 0x51dacb..0x51db6e: the 0x139 lives 35, with +0xe4 at 2, +0xd8 at 10, scale 1.2f and speed (0, −5, 0); it copies its
// angles to +0x28, takes the start from its owner's position and rises 150. A rand() % 80 + 10 (r) moves +0x28's z by
// subtype · r − r. Without an owner it stops (the original reads address 0x10); with the table full, nothing.
TEST(HighModelEffectsTest, TheJavelinStartsOverItsThrowerAimingAtItsOwner)
{
    for (const std::int32_t subtype : {0, 1, 2})
    {
        SCOPED_TRACE(subtype);
        HighWorld tested;
        ActorState& owner = tested.world->Actors()[3];
        owner.position = {500, 600, 700};
        const auto made = CreateHighModelEffect({.type = 0x139,
                                                 .position = {100, 200, 300},
                                                 .angles = {1, 2, 3},
                                                 .light = {0.5f, 0.6f, 0.7f},
                                                 .subtype = subtype,
                                                 .owner = &owner},
                                                tested.Context());
        ASSERT_TRUE(made.HasValue()) << made.Error();
        const ModelEffect& javelin = tested.Effect(0);
        CrtRandom expected{Seed};
        const double spread = static_cast<double>(expected.Next() % 80 + 10);
        EXPECT_EQ(javelin.life, 35);
        EXPECT_EQ(javelin.turnDirection, 2.0f);
        EXPECT_EQ(javelin.speed, 10.0f);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(javelin.transform.scale), 0x3f99999au);
        EXPECT_EQ(javelin.velocity, (glm::vec3{0, -5, 0}));
        EXPECT_EQ(javelin.start, owner.position);
        EXPECT_EQ(javelin.transform.position, (glm::vec3{100, 200, 450}));
        EXPECT_EQ(javelin.copiedAngles,
                  (glm::vec3{1, 2, static_cast<float>(static_cast<double>(subtype) * spread - spread + 3.0)}));
        EXPECT_EQ(tested.Draws(), 1);
    }
    HighWorld lonely;
    EXPECT_TRUE(HasError(CreateHighModelEffect({.type = 0x139}, lonely.Context()), "0x51db1b"));
    HighWorld full;
    full.FillTheEffects();
    const auto none = CreateHighModelEffect({.type = 0x139}, full.Context());
    ASSERT_TRUE(none.HasValue());
    EXPECT_FALSE(none.Value().has_value());
    EXPECT_EQ(full.Draws(), 0);
}

// 0x529b86..0x529de9 and the tail (0x52ccb0 → 0x52cd94, without moving): with life 25 or more the 0x139 raises +0xe4 by
// 10 and advances with +0x28; from 24 to 20, +0xd8 1.5 more, advances, +0xe4 at rand() % 30 + 30 and the speed's y 8
// less. Always the start over its owner, scale 0.015f more, the angles' z plus +0xe4, +0x68 and alpha at life · 0.1f,
// the /1's z at the start plus FSIN(life · 0.1f) · 30 and the dynamic light.
TEST(HighModelEffectsTest, TheJavelinClimbsSpinsAndBrakes)
{
    HighWorld tested;
    ActorState& owner = tested.world->Actors()[3];
    owner.position = {500, 600, 700};
    ASSERT_TRUE(CreateHighModelEffect(
                    {.type = 0x139, .position = {100, 200, 300}, .angles = {1, 2, 3}, .subtype = 1, .owner = &owner},
                    tested.Context())
                    .HasValue());
    ModelEffect& javelin = tested.Effect(0);
    const glm::vec3 aim = javelin.copiedAngles;
    ASSERT_TRUE(StepHighModelEffect(javelin, nullptr, tested.StepContext()).HasValue());
    const glm::vec3 start{500, 600, 850};
    EXPECT_EQ(javelin.start, start);
    EXPECT_EQ(javelin.transform.scale,
              static_cast<float>(static_cast<double>(std::bit_cast<float>(0x3f99999au)) + static_cast<double>(0.015f)));
    EXPECT_EQ(javelin.turnDirection, 12.0f);
    const glm::vec3 advanced = SumOf(TurnedBy({0, -5, 0}, aim), {100, 200, 450});
    EXPECT_EQ(javelin.transform.position.x, advanced.x);
    EXPECT_EQ(javelin.transform.position.y, advanced.y);
    const auto wave = Client::Math::Detail::SineProductPC53(35.0 * static_cast<double>(0.1f), 30.0f);
    ASSERT_TRUE(wave.has_value());
    EXPECT_EQ(javelin.transform.position.z, static_cast<float>(*wave + 850.0));
    EXPECT_EQ(javelin.transform.angles.z, 15.0f);
    EXPECT_EQ(javelin.objectState.light, static_cast<float>(35.0 * static_cast<double>(0.1f)));
    EXPECT_EQ(javelin.alpha, javelin.objectState.light);
    EXPECT_EQ(javelin.life, 34);
    EXPECT_TRUE(std::ranges::any_of(tested.lighting->cells, [](const auto& cell) { return cell[0] > 0; }));
    EXPECT_EQ(tested.Draws(), 1 + 1);

    javelin.life = 22;
    ASSERT_TRUE(StepHighModelEffect(javelin, nullptr, tested.StepContext()).HasValue());
    CrtRandom expected{Seed};
    for (int draw = 0; draw < 3; ++draw)
        static_cast<void>(expected.Next());
    EXPECT_EQ(javelin.speed, 11.5f);
    EXPECT_EQ(javelin.turnDirection, static_cast<float>(expected.Next() % 30 + 30));
    EXPECT_EQ(javelin.velocity, (glm::vec3{0, -13, 0}));
    EXPECT_EQ(javelin.life, 21);
    EXPECT_EQ(tested.random.State(), expected.State());
}

// 0x529bde..0x529c69: below 20, 0x4e9810 turns +0x28 toward the start. Far (100 or more), the 0x6ad/1 ember at the
// rotated position, with the effect's angles and light (the creation overwrites the light with 0.5) and its three
// rand(), before the turn. With the particles full it goes on without it; near, it goes to the start and grows 0.04f
// more.
TEST(HighModelEffectsTest, TheJavelinHuntsItsOwnerAndTrailsFire)
{
    HighWorld tested;
    ActorState& owner = tested.world->Actors()[3];
    owner.position = {500, 600, 700};
    ASSERT_TRUE(CreateHighModelEffect({.type = 0x139, .position = {100, 200, 300}, .owner = &owner}, tested.Context())
                    .HasValue());
    ModelEffect& javelin = tested.Effect(0);
    javelin.life = 19;
    const glm::vec3 angles = javelin.transform.angles;
    const auto stepped = StepHighModelEffect(javelin, nullptr, tested.StepContext());
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(tested.Draws(), 1 + 1 + 3 + 1);
    EXPECT_EQ(AliveParticles(*tested.world, Client::Particles::CinderParticle, 1), 1);
    const auto& all = tested.world->Particles().All();
    const auto ember = std::ranges::find_if(all, [](const Client::Particles::Particle& particle)
                                            { return particle.alive && particle.type == 0x6ad; });
    ASSERT_NE(ember, all.end());
    CrtRandom expected{Seed};
    expected.Discard(2);
    Client::Particles::Particles twin;
    const auto made = twin.CreateChecked(0x6ad, ember->start, angles, javelin.light, 1, 1.0f, 0.0f, expected);
    ASSERT_TRUE(made.HasValue() && made.Value().has_value());
    const Client::Particles::Particle& replayed = twin.All()[*made.Value()];
    EXPECT_EQ(ember->angles, angles);
    EXPECT_EQ(ember->life, 15);
    EXPECT_EQ(ember->velocity, replayed.velocity);
    EXPECT_EQ(ember->scale, replayed.scale);
    EXPECT_EQ(ember->rotation, replayed.rotation);
    EXPECT_EQ(ember->light, glm::vec3(0.5f));
    EXPECT_EQ(javelin.turnDirection, static_cast<float>(expected.Next() % 30 + 30));
    EXPECT_EQ(tested.random.State(), expected.State());

    tested.FillTheParticles();
    javelin.life = 18;
    javelin.transform.position = {500, 600, 860};
    const float scale = javelin.transform.scale;
    ASSERT_TRUE(StepHighModelEffect(javelin, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(javelin.transform.scale,
              static_cast<float>(
                  static_cast<double>(static_cast<float>(static_cast<double>(scale) + static_cast<double>(0.015f))) +
                  static_cast<double>(0.04f)));
    EXPECT_EQ(javelin.life, 17);
    EXPECT_EQ(tested.Draws(), 6 + 2);
}

// 0x51ed79 and 0x52b4a7: the 0x615 lives 10 and, with an owner and subtype 0, shifts (−25, −80, 0) rotated by the
// owner's angles. Its step does nothing until fewer than 5 steps remain; with even life, three rand() (x and y from
// rand() % 64 − 32 and the scale rand() % 10 + 50) and joint 0x615/5 (0x52b560, with two more rand(): x and y shifted
// rand() % 10 − 5, 800 higher, light (1; 0.5; 0.2f)); with the joints full, without it. The tail neither moves it (no
// speed) nor draws it.
TEST(HighModelEffectsTest, TheThunderShiftsOverItsOwnerAndDropsBoltsAtTheEnd)
{
    HighWorld tested;
    ActorState& owner = tested.world->Actors()[3];
    owner.angles = {0, 0, 90};
    const glm::vec3 origin{100, 200, 300};
    ASSERT_TRUE(
        CreateHighModelEffect({.type = 0x615, .position = origin, .owner = &owner}, tested.Context()).HasValue());
    ModelEffect& thunder = tested.Effect(0);
    const glm::vec3 shifted = SumOf(TurnedBy({-25, -80, 0}, owner.angles), origin);
    EXPECT_EQ(thunder.life, 10);
    EXPECT_EQ(thunder.transform.position, shifted);
    EXPECT_EQ(tested.Draws(), 0);
    for (const std::int32_t subtype : {1, 2})
    {
        HighWorld other;
        ASSERT_TRUE(CreateHighModelEffect({.type = 0x615, .position = origin, .subtype = subtype, .owner = &owner},
                                          other.Context())
                        .HasValue());
        EXPECT_EQ(other.Effect(0).transform.position, origin);
    }
    HighWorld lonely;
    ASSERT_TRUE(CreateHighModelEffect({.type = 0x615, .position = origin}, lonely.Context()).HasValue());
    EXPECT_EQ(lonely.Effect(0).transform.position, origin);
    EXPECT_EQ(lonely.Effect(0).life, 10);

    for (int step = 0; step < 6; ++step)
        ASSERT_TRUE(StepHighModelEffect(thunder, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(thunder.life, 4);
    EXPECT_EQ(thunder.transform.position, shifted);
    EXPECT_EQ(tested.Draws(), 6);
    const auto dropped = StepHighModelEffect(thunder, nullptr, tested.StepContext());
    ASSERT_TRUE(dropped.HasValue()) << dropped.Error();
    EXPECT_EQ(tested.Draws(), 6 + 1 + 3 + 2);
    CrtRandom expected(Seed);
    for (int draw = 0; draw < 6 + 1; ++draw)
        static_cast<void>(expected.Next());
    const int x = expected.Next() % 64;
    const int y = expected.Next() % 64;
    const int scale = expected.Next() % 10;
    const int jitterX = expected.Next() % 10;
    const int jitterY = expected.Next() % 10;
    const glm::vec3 point{static_cast<float>(static_cast<double>(x) - 32.0 + static_cast<double>(shifted.x)),
                          static_cast<float>(static_cast<double>(y) - 32.0 + static_cast<double>(shifted.y)),
                          shifted.z};
    const auto& bolt = tested.world->DrawTail().joints.front();
    ASSERT_TRUE(bolt.alive);
    EXPECT_EQ(bolt.type, 0x615);
    EXPECT_EQ(bolt.subtype, 5);
    EXPECT_EQ(bolt.ownerContext, nullptr);
    EXPECT_EQ(bolt.texture, 0x614);
    EXPECT_EQ(bolt.width, static_cast<float>(scale + 50));
    EXPECT_EQ(bolt.maximumCount, 50);
    EXPECT_EQ(bolt.life, 20);
    EXPECT_EQ(bolt.angles, glm::vec3(0));
    EXPECT_EQ(bolt.targetPosition, point);
    EXPECT_EQ(bolt.position,
              (glm::vec3{static_cast<float>(static_cast<double>(jitterX - 5) + static_cast<double>(point.x)),
                         static_cast<float>(static_cast<double>(jitterY - 5) + static_cast<double>(point.y)),
                         static_cast<float>(static_cast<double>(point.z) + 800.0)}));
    EXPECT_EQ(bolt.pathIncrement, bolt.position);
    EXPECT_EQ(bolt.light, (glm::vec3{1, 0.5f, std::bit_cast<float>(0x3e4ccccdu)}));
    tested.FillTheJoints();
    ASSERT_TRUE(StepHighModelEffect(thunder, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(tested.Draws(), 6 + 6 + 1);
    ASSERT_TRUE(StepHighModelEffect(thunder, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(tested.Draws(), 6 + 6 + 1 + 4);
    EXPECT_EQ(thunder.life, 1);
}

// 0x51efa5 and 0x52bb79: the 0x621/1 stores the position as the start and lives 20; its step sets its owner's model
// origin at the start, goes to point (0, 0, 10) of the owner's bone 33 and does its three emissions (0x52bbf6): with
// even life (20, subtracted later), the angles' two rand(), joint 0x614/3 (scale 10, key 10) from the point to the
// position and the 0x5fd sprite with scale ((rand() & 7) + 8) · 0.2f, without the 0x5c6/2 spark; with odd life, the
// spark before the sprite. Without the joint service it stops at 0x52bcc4. The /0 is born in Effects/ModelEffects
// (life 10) and steps in Effects/GameModelEffects.
// A 0x614/3 joint of 0x530860 as the 0x621 glow requested it (a copy: the request only lends its vectors).
struct GlowJoint
{
    std::int32_t type = 0;
    std::int32_t subtype = 0;
    float scale = 0.0f;
    std::int32_t key = 0;
    std::int32_t skill = 0;
    std::int32_t target = 0;
    glm::vec3 first{};
    glm::vec3 second{};
    glm::vec3 angles{};
};

// The 0x621 of `subtype` with its owner in slot 3 (bone 33 at (1, 2, 3) unrotated), the sprites and the joints by
// hand. With the model origin at (100, 200, 300), the bone's point (0, 0, 10) lands at (101, 202, 313).
struct GlowWorld
{
    explicit GlowWorld(std::int32_t subtype)
    {
        tested.services.sprites = sprites.get();
        tested.services.effects = &tested.world->Map().effects;
        tested.services.joint =
            [this](const Client::Kernel::JointRequest& request) -> Client::Core::Expected<void, std::string>
        {
            joints.push_back({request.type, request.subtype, request.scale, request.key, request.skill, request.target,
                              request.firstPosition, request.secondPosition, request.angles});
            return {};
        };
        owner.type = 0x4a2;
        owner.bodyPose.assign(34, Client::Math::BoneTransform(1.0f));
        owner.bodyPose[33][3] = {1.0f, 2.0f, 3.0f};
        tested.world->PlaceModel(0x4a2, {.scale = 1.0f, .origin = {7, 8, 9}});
        created =
            CreateHighModelEffect({.type = 0x621, .position = {100, 200, 300}, .subtype = subtype, .owner = &owner},
                                  tested.Context())
                .HasValue();
    }

    ModelEffect& Glow()
    {
        return tested.Effect(0);
    }

    HighWorld tested;
    std::unique_ptr<Client::Particles::FrameSprites> sprites = std::make_unique<Client::Particles::FrameSprites>();
    std::vector<GlowJoint> joints;
    ActorState& owner = tested.world->Actors()[3];
    bool created = false;
};

// The angles (rand() % 360, 0, rand() % 360) and point (0, 120, 0) rotated plus the position (0x52bbfe..0x52bc84) of
// the three emissions, from glow621.py (separate x87, seed 0x13579bdf) and not from OriginalAngleMatrix.
std::array<std::uint32_t, 3> BitsOf(const glm::vec3& point)
{
    return {std::bit_cast<std::uint32_t>(point.x), std::bit_cast<std::uint32_t>(point.y),
            std::bit_cast<std::uint32_t>(point.z)};
}

struct GlowEmission
{
    glm::vec3 angles;
    std::array<std::uint32_t, 3> point;
};

void ExpectTheJoints(const std::vector<GlowJoint>& joints, std::span<const GlowEmission> emissions)
{
    ASSERT_EQ(joints.size(), emissions.size());
    for (std::size_t index = 0; index < joints.size(); ++index)
    {
        const GlowJoint& joint = joints[index];
        EXPECT_EQ(joint.type, 0x614);
        EXPECT_EQ(joint.subtype, 3);
        EXPECT_EQ(joint.scale, 10.0f);
        EXPECT_EQ(joint.key, 10);
        EXPECT_EQ(joint.skill, 10);
        EXPECT_EQ(joint.target, -1);
        EXPECT_EQ(joint.second, (glm::vec3{101, 202, 313}));
        EXPECT_EQ(joint.angles, emissions[index].angles) << index;
        EXPECT_EQ(BitsOf(joint.first), emissions[index].point) << index;
    }
}

// The 0x5fd sprites from `first`: rotation rand() % 360 and scale ((rand() & 7) + 8) · 0.2f (0x6b8854), in bits.
void ExpectTheSprites(std::span<const Client::Particles::Sprite> sprites, std::size_t first,
                      std::span<const std::pair<float, std::uint32_t>> expected)
{
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        const Client::Particles::Sprite& sprite = sprites[first + index];
        ASSERT_TRUE(sprite.alive) << index;
        EXPECT_EQ(sprite.texture, 0x5fd);
        EXPECT_EQ(sprite.position, (glm::vec3{101, 202, 313}));
        EXPECT_EQ(sprite.rotation, expected[index].first) << index;
        EXPECT_EQ(std::bit_cast<std::uint32_t>(sprite.scale), expected[index].second) << index;
    }
    EXPECT_FALSE(sprites[first + expected.size()].alive);
}

TEST(HighModelEffectsTest, TheHeldPiercingGlowFollowsTheOwnersBone)
{
    GlowWorld glowing(1);
    HighWorld& tested = glowing.tested;
    ASSERT_TRUE(glowing.created);
    ModelEffect& glow = glowing.Glow();
    EXPECT_EQ(glow.life, 20);
    EXPECT_EQ(glow.start, (glm::vec3{100, 200, 300}));
    EXPECT_TRUE(Client::Effects::StepsAsHighModelEffect(glow));
    ASSERT_TRUE(StepHighModelEffect(glow, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(tested.world->ModelPlacement(0x4a2).origin, (glm::vec3{100, 200, 300}));
    EXPECT_EQ(glow.transform.position, (glm::vec3{101, 202, 313}));
    EXPECT_EQ(glow.life, 19);
    // The light (0x5208c5) and, per emission, the two angles and the sprite's rotation and scale.
    EXPECT_EQ(tested.Draws(), 1 + 3 * 4);
    constexpr std::array<GlowEmission, 3> EvenLife{{
        {{58, 0, 123}, {0x423eacbau, 0x43275dc2u, 0x43cf6205u}},
        {{290, 0, 317}, {0x4300fda9u, 0x4368043bu, 0x43483ca4u}},
        {{232, 0, 100}, {0x432dc1cau, 0x4356d43bu, 0x435a7050u}},
    }};
    ExpectTheJoints(glowing.joints, EvenLife);
    constexpr std::array<std::pair<float, std::uint32_t>, 3> EvenSprites{
        {{84.0f, 0x3fe66667u}, {18.0f, 0x3fcccccdu}, {13.0f, 0x40333333u}}};
    ExpectTheSprites(glowing.sprites->Sprites(), 0, EvenSprites);
    EXPECT_FALSE(std::ranges::any_of(tested.world->Particles().All(),
                                     [](const Client::Particles::Particle& particle) { return particle.alive; }));
    // With odd life, the 0x5c6/2 spark with scale (rand() % 50 + 10) · 0.01f (0x6b86c0) before the sprite: one more
    // rand() per emission.
    const int before = tested.Draws();
    ASSERT_TRUE(StepHighModelEffect(glow, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(tested.Draws() - before, 1 + 3 * 5);
    EXPECT_EQ(glowing.joints.size(), 3u);
    constexpr std::array<std::pair<std::array<std::uint32_t, 3>, std::uint32_t>, 3> Sparks{{
        {{0x4329fe2eu, 0x433b55cau, 0x43cd0a80u}, 0x3e9eb852u},
        {{0x42d05244u, 0x42defae3u, 0x43c3dd11u}, 0x3e8f5c29u},
        {{0x4253125eu, 0x4359abeau, 0x434c3e3au}, 0x3f0f5c29u},
    }};
    const auto particles = tested.world->Particles().All();
    for (std::size_t index = 0; index < Sparks.size(); ++index)
    {
        const Client::Particles::Particle& spark = particles[index];
        ASSERT_TRUE(spark.alive) << index;
        EXPECT_EQ(spark.type, 0x5c6);
        EXPECT_EQ(spark.subtype, 2);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(spark.scale), Sparks[index].second) << index;
        EXPECT_EQ(BitsOf(spark.position), Sparks[index].first) << index;
    }
    EXPECT_FALSE(particles[Sparks.size()].alive);
    constexpr std::array<std::pair<float, std::uint32_t>, 3> OddSprites{
        {{60.0f, 0x3fcccccdu}, {219.0f, 0x3fe66667u}, {160.0f, 0x400ccccdu}}};
    ExpectTheSprites(glowing.sprites->Sprites(), 3, OddSprites);
    tested.services.joint = {};
    ASSERT_FALSE(StepHighModelEffect(glow, nullptr, tested.StepContext()).HasValue());

    ASSERT_TRUE(CreateHighModelEffect({.type = 0x621}, tested.Context()).HasValue());
    EXPECT_EQ(tested.Effect(1).life, 10);
    EXPECT_FALSE(Client::Effects::StepsAsHighModelEffect(tested.Effect(1)));
}

// 0x52bd47..0x52bdd3: the /2 creates joint 0x614/3 only with even life (0x52bd61) and always the sprite; never the
// spark. The angles' two rand() come out the same.
TEST(HighModelEffectsTest, TheThrownPiercingGlowJoinsOnlyOnEvenLives)
{
    GlowWorld glowing(2);
    HighWorld& tested = glowing.tested;
    ASSERT_TRUE(glowing.created);
    ModelEffect& glow = glowing.Glow();
    EXPECT_EQ(glow.life, 20);
    ASSERT_TRUE(StepHighModelEffect(glow, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(glow.transform.position, (glm::vec3{101, 202, 313}));
    EXPECT_EQ(tested.Draws(), 1 + 3 * 4);
    constexpr std::array<GlowEmission, 3> EvenLife{{
        {{58, 0, 123}, {0x423eacbau, 0x43275dc2u, 0x43cf6205u}},
        {{290, 0, 317}, {0x4300fda9u, 0x4368043bu, 0x43483ca4u}},
        {{232, 0, 100}, {0x432dc1cau, 0x4356d43bu, 0x435a7050u}},
    }};
    ExpectTheJoints(glowing.joints, EvenLife);
    constexpr std::array<std::pair<float, std::uint32_t>, 3> EvenSprites{
        {{84.0f, 0x3fe66667u}, {18.0f, 0x3fcccccdu}, {13.0f, 0x40333333u}}};
    ExpectTheSprites(glowing.sprites->Sprites(), 0, EvenSprites);

    // Life 19: no joint or spark, only the sprite. Without the joint service it does not stop: it does not ask for it.
    tested.services.joint = {};
    ASSERT_TRUE(StepHighModelEffect(glow, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(tested.Draws(), 2 * (1 + 3 * 4));
    EXPECT_EQ(glowing.joints.size(), 3u);
    constexpr std::array<std::pair<float, std::uint32_t>, 3> OddSprites{
        {{71.0f, 0x4019999au}, {182.0f, 0x40000000u}, {295.0f, 0x3fcccccdu}}};
    ExpectTheSprites(glowing.sprites->Sprites(), 3, OddSprites);
    EXPECT_FALSE(std::ranges::any_of(tested.world->Particles().All(),
                                     [](const Client::Particles::Particle& particle) { return particle.alive; }));
    EXPECT_EQ(glow.life, 18);
}

// With the pools full, 0x53fe70 and 0x5491e0 do not create but their arguments were already drawn: the spark's scale
// and the sprite's rotation and scale come out the same, in the same order.
TEST(HighModelEffectsTest, TheHeldPiercingGlowDrawsItsRandomsWithFullPools)
{
    GlowWorld glowing(1);
    HighWorld& tested = glowing.tested;
    ASSERT_TRUE(glowing.created);
    ModelEffect& glow = glowing.Glow();
    glow.life = 19;
    tested.FillTheParticles();
    ASSERT_TRUE(StepHighModelEffect(glow, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(tested.Draws(), 1 + 3 * 5);
    EXPECT_TRUE(glowing.joints.empty());
    // Without 0x52bcce's spark (its rand() % 50 did come out), the second angle and the sprites would not be these.
    constexpr std::array<std::pair<float, std::uint32_t>, 3> OddSprites{
        {{9.0f, 0x40000000u}, {232.0f, 0x4019999au}, {126.0f, 0x40333333u}}};
    ExpectTheSprites(glowing.sprites->Sprites(), 0, OddSprites);

    // Life 18 with the sprites full: the three emissions' joints with their usual angles.
    for (auto& sprite : glowing.sprites->Sprites())
        sprite.alive = true;
    glowing.sprites->Sprites()[0].scale = 0.5f;
    const int before = tested.Draws();
    ASSERT_TRUE(StepHighModelEffect(glow, nullptr, tested.StepContext()).HasValue());
    EXPECT_EQ(tested.Draws() - before, 1 + 3 * 4);
    EXPECT_EQ(glowing.sprites->Sprites()[0].scale, 0.5f);
    constexpr std::array<GlowEmission, 3> SecondEven{{
        {{60, 0, 64}, {0x423c4a18u, 0x43644d62u, 0x43d07626u}},
        {{258, 0, 219}, {0x42aa9900u, 0x435d63abu, 0x43439f4eu}},
        {{72, 0, 176}, {0x42c4d39au, 0x4325021fu, 0x43d5903au}},
    }};
    ExpectTheJoints(glowing.joints, SecondEven);
}

} // namespace
