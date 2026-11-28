#include "Effects/SkullEffects.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/OriginalRetainedMath.h"
#include "Kernel/ActorState.h"
#include "Effects/HighModelEffects.h"
#include "Kernel/WorldMap.h"
#include "Kernel/WorldState.h"
#include "Effects/GameModelEffects.h"
#include "Effects/ModelEffects.h"
#include "Particles/Particles.h"
#include "Particles/Sprites.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

// The 0x628, the skull: its creation (0x51ee23), its step (0x52b59b) and PutTheSkullOn (0x4e688c, 0x4cd664).
namespace
{
using Client::Crt::CrtRandom;
using Client::Effects::CreateHighModelEffect;
using Client::Effects::HighEffectStepContext;
using Client::Effects::ModelEffect;
using Client::Effects::StepHighModelEffect;
using Client::Kernel::ActorState;
using Client::Kernel::WorldState;

constexpr std::uint32_t Seed = 0x2468ace1u;
constexpr std::size_t OwnerSlot = 7;

int DrawsSince(const CrtRandom& random)
{
    CrtRandom probe(Seed);
    for (int draws = 0; draws < 1000; ++draws)
    {
        if (probe.State() == random.State())
            return draws;
        static_cast<void>(probe.Next());
    }
    return -1;
}

struct SkullWorld
{
    SkullWorld()
    {
        services.sprites = sprites.get();
        ActorState& owner = Owner();
        owner.alive = true;
        owner.position = {3000, 4000, 250};
        owner.scale = 1.25f;
    }

    ActorState& Owner()
    {
        return world->Actors()[OwnerSlot];
    }

    ModelEffect& Create(std::int32_t subtype, bool owned = true)
    {
        const auto made = CreateHighModelEffect({.type = 0x628,
                                                 .position = {100, 200, 300},
                                                 .angles = {0, 0, 45},
                                                 .light = {0.3f, 0.4f, 0.5f},
                                                 .subtype = subtype,
                                                 .owner = owned ? &Owner() : nullptr},
                                                {.world = *world, .random = random});
        EXPECT_TRUE(made.HasValue() && made.Value().has_value());
        return world->Map().effects.Effects()[made.Value().value_or(0)];
    }

    Client::Core::Expected<void, std::string> Step(ModelEffect& effect)
    {
        const HighEffectStepContext context{
            .world = *world, .random = random, .services = services, .worldClock = clock};
        return StepHighModelEffect(effect, nullptr, context);
    }

    void FillTheParticles()
    {
        for (auto& particle : world->Particles().All())
            particle.alive = true;
    }

    std::unique_ptr<WorldState> world = std::make_unique<WorldState>();
    std::unique_ptr<Client::Particles::FrameSprites> sprites = std::make_unique<Client::Particles::FrameSprites>();
    Client::Effects::ModelEffectStepServices services{};
    CrtRandom random{Seed};
    float clock = 98765.5f;
};

// 0x52b640..0x52b726: the point of turn `turn` of the /0, /1 and /3 with the life before the tail.
glm::vec3 OrbitPoint(int turn, std::int32_t life, bool reversed, float clock, const ActorState& owner)
{
    using Client::Math::Detail::CosineProductPC53;
    using Client::Math::Detail::SineProductPC53;
    const double index = turn;
    double angle = static_cast<double>(life) * static_cast<double>(std::bit_cast<float>(0x3e2e147bu)) +
                   index * static_cast<double>(std::bit_cast<float>(0x40060a92u));
    if (reversed)
        angle = -angle;
    const auto wobble =
        SineProductPC53(index * static_cast<double>(std::bit_cast<float>(0x4175eb85u)) +
                            static_cast<double>(clock) * static_cast<double>(std::bit_cast<float>(0x3b4b295fu)),
                        20.0f);
    const auto radius = static_cast<float>(wobble.value_or(0.0) + 50.0);
    const auto x = SineProductPC53(angle, radius);
    const auto y = CosineProductPC53(angle, radius);
    return {static_cast<float>(x.value_or(0.0) + static_cast<double>(owner.position.x)),
            static_cast<float>(y.value_or(0.0) + static_cast<double>(owner.position.y)),
            static_cast<float>((reversed ? 250.0 : 200.0) * static_cast<double>(owner.scale) +
                               static_cast<double>(owner.position.z))};
}

// 0x51ee23..0x51ee95: life 1000, except the /1 (940), the /2 (8, 150 higher) and the /4 (20, +0xd8 at 2 and speed (0,
// −10, 0)). No rand().
TEST(SkullEffectsTest, TheSkullTakesItsLifeFromItsSubtype)
{
    constexpr std::int32_t Lives[] = {1000, 940, 8, 1000, 20, 1000, 1000};
    for (std::int32_t subtype = 0; subtype < 7; ++subtype)
    {
        SCOPED_TRACE(subtype);
        SkullWorld tested;
        const ModelEffect& skull = tested.Create(subtype);
        EXPECT_EQ(skull.life, Lives[subtype]);
        EXPECT_EQ(skull.transform.position, glm::vec3(100, 200, subtype == 2 ? 450 : 300));
        if (subtype == 4)
        {
            EXPECT_EQ(skull.speed, 2.0f);
        }
        EXPECT_EQ(skull.velocity, subtype == 4 ? glm::vec3(0, -10, 0) : glm::vec3(0.0f));
        EXPECT_EQ(DrawsSince(tested.random), 0);
    }
}

// 0x52b5a3..0x52b5dc: the /4 lowers the speed's y in +0xd8 and its angles' x by 1 and leaves the 0x628 sprite with
// scale 10 and its light where it was; the tail moves it. No owner and no rand() besides the local light's.
TEST(SkullEffectsTest, TheFallingSkullDropsAndLeavesItsSprite)
{
    SkullWorld tested;
    ModelEffect& skull = tested.Create(4, false);
    ASSERT_TRUE(tested.Step(skull).HasValue());
    EXPECT_EQ(skull.velocity, glm::vec3(0, -12, 0));
    EXPECT_EQ(skull.transform.angles.x, -1.0f);
    EXPECT_EQ(skull.life, 19);
    EXPECT_NE(skull.transform.position, glm::vec3(100, 200, 300));
    const auto& sprite = tested.sprites->Sprites()[0];
    ASSERT_TRUE(sprite.alive);
    EXPECT_EQ(sprite.texture, 0x628);
    EXPECT_EQ(sprite.position, glm::vec3(100, 200, 300));
    EXPECT_EQ(sprite.scale, 10.0f);
    EXPECT_EQ(sprite.light, glm::vec3(0.3f, 0.4f, 0.5f));
    EXPECT_EQ(DrawsSince(tested.random), 1);
}

// 0x52b5e9..0x52b5fe: the others die with their owner dead; without an owner, the original reads a null pointer.
TEST(SkullEffectsTest, TheSkullDiesWithItsOwner)
{
    SkullWorld tested;
    ModelEffect& skull = tested.Create(1);
    tested.Owner().alive = false;
    ASSERT_TRUE(tested.Step(skull).HasValue());
    EXPECT_FALSE(skull.alive);
    EXPECT_EQ(skull.life, -1);
    EXPECT_FALSE(tested.sprites->Sprites()[0].alive);
    EXPECT_EQ(DrawsSince(tested.random), 1);

    SkullWorld orphan;
    const auto cut = orphan.Step(orphan.Create(0, false));
    ASSERT_FALSE(cut.HasValue());
    EXPECT_TRUE(cut.Error().starts_with("0x52b5ef")) << cut.Error();
}

// 0x52b640..0x52b782: the /1 and the /3 (reversed, higher) do three turns over their owner; with the particle table
// full, the 0x5de/5 is not born and the 0x628 sprite with scale 1 and local light (1, 1, 1) stays at each point. With
// a slot, each turn releases its 0x5de/5 with scale 0.4f (0x52b74e), with its rand(), before its sprite.
TEST(SkullEffectsTest, TheSkullCirclesOverItsOwner)
{
    for (const std::int32_t subtype : {1, 3})
    {
        SCOPED_TRACE(subtype);
        SkullWorld tested;
        tested.FillTheParticles();
        ModelEffect& skull = tested.Create(subtype);
        const std::int32_t life = skull.life;
        ASSERT_TRUE(tested.Step(skull).HasValue());
        EXPECT_EQ(skull.life, life - 1);
        for (int turn = 0; turn < 3; ++turn)
        {
            const auto& sprite = tested.sprites->Sprites()[static_cast<std::size_t>(turn)];
            ASSERT_TRUE(sprite.alive);
            EXPECT_EQ(sprite.texture, 0x628);
            EXPECT_EQ(sprite.position, OrbitPoint(turn, life, subtype == 3, tested.clock, tested.Owner()));
            EXPECT_EQ(sprite.scale, 1.0f);
            EXPECT_EQ(sprite.light, glm::vec3(1.0f));
        }
        EXPECT_FALSE(tested.sprites->Sprites()[3].alive);
        EXPECT_EQ(DrawsSince(tested.random), 1);

        SkullWorld roomy;
        const auto lit = roomy.Step(roomy.Create(subtype));
        ASSERT_TRUE(lit.HasValue()) << lit.Error();
        CrtRandom replay(Seed);
        static_cast<void>(replay.Next());
        for (int turn = 0; turn < 3; ++turn)
        {
            auto twin = std::make_unique<Client::Particles::Particles>();
            ASSERT_TRUE(twin->CreateChecked(0x5de, glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(1.0f), 5,
                                            std::bit_cast<float>(0x3ecccccdu), 0.0f, replay)
                            .HasValue());
        }
        EXPECT_EQ(roomy.random.State(), replay.State());
        std::size_t orbiting = 0;
        for (const auto& particle : roomy.world->Particles().All())
            orbiting += particle.alive && particle.type == 0x5de && particle.subtype == 5 ? 1 : 0;
        EXPECT_EQ(orbiting, 3u);
        EXPECT_TRUE(roomy.sprites->Sprites()[2].alive);
        EXPECT_FALSE(roomy.sprites->Sprites()[3].alive);
    }
}

// 0x52b612..0x52b63b: the /0 looks at its own +0x78. With 0x40, life at 10 (the tail leaves it at 9) and it does its
// turns; with 0x40 and 0x4000, also +0x16c at 0 and nothing else. Without the bits, it turns like the /1. Nobody writes
// +0x78 on an effect (Q-C1582): the test sets it by hand, in a state the original does not reach.
TEST(SkullEffectsTest, ThePlainSkullReadsItsOwnFlags)
{
    for (const std::uint32_t flags : {0x0u, 0x40u, 0x4040u, 0x4000u})
    {
        SCOPED_TRACE(flags);
        SkullWorld tested;
        tested.FillTheParticles();
        ModelEffect& skull = tested.Create(0);
        skull.flags78 = flags;
        skull.seen = true;
        ASSERT_TRUE(tested.Step(skull).HasValue());
        const bool held = (flags & 0x40u) != 0;
        EXPECT_EQ(skull.life, held ? 9 : 999);
        const bool hidden = flags == 0x4040u;
        EXPECT_EQ(skull.seen, !hidden);
        EXPECT_EQ(tested.sprites->Sprites()[0].alive, !hidden);
        if (!hidden)
        {
            EXPECT_EQ(tested.sprites->Sprites()[0].position,
                      OrbitPoint(0, held ? 10 : 1000, false, tested.clock, tested.Owner()));
        }
    }
}

// 0x52b648..0x52b773, turn 1 of the /3 with life 1000 and T = 98765.5f, computed by hand outside MuRet (exact sin and
// cos, the 64-bit retained, one R53 per product and per sum, one FSTP per axis): the radius FSIN(15.37f + T · 0.0031f)
// · 20 + 50 in float (0x4287a8c9) and the point (0x4538ca82, 0x4576bd33, 562.5). With the particle table full, the
// 0x628 sprite stays there.
TEST(SkullEffectsTest, TheReversedSkullsSecondTurnMatchesTheHandMadePoint)
{
    SkullWorld tested;
    tested.FillTheParticles();
    ModelEffect& skull = tested.Create(3);
    ASSERT_EQ(skull.life, 1000);
    ASSERT_TRUE(tested.Step(skull).HasValue());
    const auto& sprite = tested.sprites->Sprites()[1];
    ASSERT_TRUE(sprite.alive);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(sprite.position.x), 0x4538ca82u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(sprite.position.y), 0x4576bd33u);
    EXPECT_EQ(sprite.position.z, 562.5f);
    EXPECT_EQ(DrawsSince(tested.random), 1);
}

// 0x52b795..0x52b829: the /5's three points with life 1000 and T = 98765.5f, computed by hand like the one above. With
// the particle table full, its 0x5ac/5 are not born and the step goes on with no extra rand().
TEST(SkullEffectsTest, TheStackedSkullPointsMatchTheHandMadeOnes)
{
    SkullWorld tested;
    constexpr std::uint32_t Points[3][2] = {
        {0x453e8aefu, 0x457ab663u}, {0x453a987cu, 0x4577023bu}, {0x45395c95u, 0x457c4762u}};
    constexpr float Heights[3] = {312.5f, 375.0f, 437.5f};
    for (int turn = 0; turn < 3; ++turn)
    {
        SCOPED_TRACE(turn);
        const auto point = Client::Effects::SkullStackPoint(turn, 1000, tested.clock, tested.Owner());
        ASSERT_TRUE(point.HasValue()) << point.Error();
        EXPECT_EQ(std::bit_cast<std::uint32_t>(point.Value().x), Points[turn][0]);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(point.Value().y), Points[turn][1]);
        EXPECT_EQ(point.Value().z, Heights[turn]);
    }
    tested.FillTheParticles();
    ModelEffect& stacked = tested.Create(5);
    ASSERT_TRUE(tested.Step(stacked).HasValue());
    EXPECT_EQ(stacked.life, 999);
    EXPECT_EQ(DrawsSince(tested.random), 1);
}

// 0x52b85e..0x52b899: the /2 leaves the 0x628 sprite with scale 1.5f at its place with light life · 0.1f.
TEST(SkullEffectsTest, TheGlowingSkullFadesWithItsLife)
{
    SkullWorld tested;
    ModelEffect& skull = tested.Create(2);
    ASSERT_TRUE(tested.Step(skull).HasValue());
    const auto& sprite = tested.sprites->Sprites()[0];
    ASSERT_TRUE(sprite.alive);
    EXPECT_EQ(sprite.texture, 0x628);
    EXPECT_EQ(sprite.position, glm::vec3(100, 200, 450));
    EXPECT_EQ(sprite.scale, 1.5f);
    const auto glow = static_cast<float>(8.0 * static_cast<double>(std::bit_cast<float>(0x3dcccccdu)));
    EXPECT_EQ(sprite.light, glm::vec3(glow));
    EXPECT_EQ(skull.life, 7);
}

// 0x52b78d..0x52b853: the /5 releases three 0x5ac/5 (life 25, no rand()) at SkullStackPoint with the life before the
// tail, its angles, light (0.6f; 0.6f; 1) and scale 0.7f; with the table full, nothing. The /6 and the others above 5
// only lose a life.
TEST(SkullEffectsTest, TheStackedSkullAndTheOthers)
{
    SkullWorld full;
    full.FillTheParticles();
    ModelEffect& stacked = full.Create(5);
    ASSERT_TRUE(full.Step(stacked).HasValue());
    EXPECT_EQ(stacked.life, 999);
    EXPECT_FALSE(full.sprites->Sprites()[0].alive);

    SkullWorld roomy;
    ModelEffect& flaring = roomy.Create(5);
    const auto stepped = roomy.Step(flaring);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(DrawsSince(roomy.random), DrawsSince(full.random));
    for (int turn = 0; turn < 3; ++turn)
    {
        SCOPED_TRACE(turn);
        const auto point = Client::Effects::SkullStackPoint(turn, 1000, roomy.clock, roomy.Owner());
        ASSERT_TRUE(point.HasValue());
        const Client::Particles::Particle& flare = roomy.world->Particles().All()[static_cast<std::size_t>(turn)];
        EXPECT_TRUE(flare.alive);
        EXPECT_EQ(flare.type, 0x5ac);
        EXPECT_EQ(flare.subtype, 5);
        EXPECT_EQ(flare.life, 25);
        EXPECT_EQ(flare.position, point.Value());
        EXPECT_EQ(flare.angles, glm::vec3(0, 0, 45));
        EXPECT_EQ(flare.light, glm::vec3(std::bit_cast<float>(0x3f19999au), std::bit_cast<float>(0x3f19999au), 1.0f));
        EXPECT_EQ(flare.scale, std::bit_cast<float>(0x3f333333u));
    }
    EXPECT_FALSE(roomy.world->Particles().All()[3].alive);

    SkullWorld other;
    ModelEffect& sixth = other.Create(6);
    ASSERT_TRUE(other.Step(sixth).HasValue());
    EXPECT_EQ(sixth.life, 999);
    EXPECT_EQ(DrawsSince(other.random), 1);
}

// PutTheSkullOn: kills only the same owner's 0x628/0 and creates another owned by it, with its position, its angles
// and light 1. With the table full, none and no error.
TEST(SkullEffectsTest, PuttingASkullReplacesOnlyTheOwnersPlainOne)
{
    SkullWorld tested;
    ModelEffect& old = tested.Create(0);
    ModelEffect& stacked = tested.Create(5);
    ActorState& owner = tested.Owner();
    owner.angles = {0, 0, 60};
    ASSERT_TRUE(Client::Effects::PutTheSkullOn(owner, *tested.world, tested.random, 0x4cd664).HasValue());
    EXPECT_TRUE(stacked.alive);
    EXPECT_TRUE(old.alive); // slot 0 again, with the new one
    EXPECT_EQ(old.life, 1000);
    EXPECT_EQ(old.transform.position, owner.position);
    EXPECT_EQ(old.transform.angles, owner.angles);
    EXPECT_EQ(old.light, glm::vec3(1.0f));
    EXPECT_EQ(old.ownerContext, &owner);
    EXPECT_EQ(DrawsSince(tested.random), 0);

    for (auto& effect : tested.world->Map().effects.Effects())
        effect.alive = true;
    old.type = 0;
    ASSERT_TRUE(Client::Effects::PutTheSkullOn(owner, *tested.world, tested.random, 0x4cd664).HasValue());
}
} // namespace
