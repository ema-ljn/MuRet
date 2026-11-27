#include "Actors/ActorMotion.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Actors/ActorCooldownServices.h"
#include "Kernel/ActorState.h"
#include "Actors/CharacterStep.h"
#include "Kernel/WorldMap.h"
#include "Kernel/WorldState.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "support/PlayerActors.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

TEST(ActorCooldownTest, FullLoopInterleavesBothCreatorsWithTheSharedRandom)
{
    Client::Kernel::ActorState actor;
    actor.movementCooldown = 2;
    actor.position = {100.0f, 200.0f, 300.0f};
    Client::Crt::CrtRandom random(2);
    std::vector<std::int32_t> events;
    glm::vec3 particlePosition{};
    const Client::Actors::ActorCooldownServices services{
        .particle = [&](const glm::vec3& position,
                        const Client::Kernel::ActorState& live) -> Client::Core::Expected<void, std::string>
        {
            events.push_back(0x5f3);
            particlePosition = position;
            EXPECT_EQ(live.movementCooldown, 1);
            EXPECT_EQ(live.light, glm::vec3(1.0f));
            static_cast<void>(random.Next());
            return {};
        },
        .effect = [&](std::int32_t type,
                      const Client::Kernel::ActorState& live) -> Client::Core::Expected<void, std::string>
        {
            events.push_back(type);
            EXPECT_EQ(live.position, actor.position);
            static_cast<void>(random.Next());
            return {};
        }};
    const auto stepped = Client::Actors::StepActorCooldown(actor, random, services);
    ASSERT_TRUE(stepped.HasValue()) << (stepped.HasValue() ? "" : stepped.Error());
    EXPECT_EQ(events, (std::vector<std::int32_t>{0x5f3, 0xc8}));
    EXPECT_EQ(particlePosition, (glm::vec3{78.0f, 196.0f, 305.0f}));
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(2, 103).State());
    EXPECT_EQ(actor.movementCooldown, 1);
}

TEST(ActorCooldownTest, MissingCreatorReturnsAfterTheDecrementLightAndFourRandomPrefix)
{
    Client::Kernel::ActorState actor;
    actor.movementCooldown = 60;
    Client::Crt::CrtRandom random(1);
    const auto stepped = Client::Actors::StepActorCooldown(actor, random);
    EXPECT_FALSE(stepped.HasValue());
    EXPECT_EQ(actor.movementCooldown, 59);
    EXPECT_EQ(actor.light, glm::vec3(1.0f));
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 4).State());
}

// 0x5069ae..0x506ac6 with the step's creators (Actors::CharacterCooldownServices): the 0x5f3/1 in the context's pool
// (0x506a73, with the character's angles and light, scale 1 and no owner) and the stone chip 0xc8 + (rand() & 1) in the
// world's table (0x506ab7, at the character's position, no owner), each with its rand() in the middle of the twenty
// rounds. With both full, the creators draw nothing and the brake goes on with its five or six rand() per round. The
// values come from cooldown.py (0x54271f, 0x51b72b, 0x5fc8f0 and 0x5fcbd0 on a separate x87), not from CreateChecked or
// CreateStoneChip.
struct BrakeSeed
{
    bool full;
    std::uint32_t seed;
    std::uint32_t state; // CrtRandom::State() at the end
    int dusts;
    int chips;
};

TEST(ActorCooldownTest, TheStepCreatorsInterleaveTheBrakeDustAndTheStoneChips)
{
    constexpr std::array<BrakeSeed, 6> Seeds{{
        {false, 3, 0x12eccd0fu, 3, 1},
        {false, 4, 0x3736bf1eu, 2, 3},
        {false, 5, 0x64977127u, 1, 4},
        {true, 3, 0x8d0928c9u, 0, 0},
        {true, 4, 0x3a6e0aacu, 0, 0},
        {true, 5, 0x72575252u, 0, 0},
    }};
    for (const BrakeSeed& expected : Seeds)
    {
        SCOPED_TRACE(testing::Message() << expected.full << ' ' << expected.seed);
        auto world = std::make_unique<Client::Kernel::WorldState>();
        Client::Kernel::ActorState actor;
        actor.movementCooldown = 1;
        actor.position = {12000.0f, 13000.0f, 250.5f};
        actor.angles = {0.0f, 0.0f, 135.0f};
        actor.light = {0.25f, 0.5f, 0.75f};
        Client::Crt::CrtRandom random(expected.seed);
        if (expected.full)
        {
            for (auto& particle : world->Particles().All())
                particle.alive = true;
            for (auto& effect : world->Map().effects.Effects())
                effect.alive = true;
        }
        const Client::Actors::CharacterStepContext context{
            .world = *world, .random = random, .particles = &world->Particles()};
        const auto stepped =
            Client::Actors::StepActorCooldown(actor, random, Client::Actors::CharacterCooldownServices(context));
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(actor.movementCooldown, 0);
        EXPECT_EQ(actor.light, glm::vec3(1.0f));
        EXPECT_EQ(random.State(), expected.state);
        const auto particles = world->Particles().All();
        const auto effects = world->Map().effects.Effects();
        if (expected.full)
        {
            EXPECT_TRUE(std::ranges::all_of(particles, [](const auto& particle) { return particle.type == 0; }));
            EXPECT_TRUE(std::ranges::all_of(effects, [](const auto& effect) { return effect.type == 0; }));
            continue;
        }
        EXPECT_EQ(std::ranges::count_if(particles, [](const auto& particle) { return particle.alive; }),
                  expected.dusts);
        EXPECT_EQ(std::ranges::count_if(effects, [](const auto& effect) { return effect.alive; }), expected.chips);
    }
}

// Seed 3 above, field by field: the stone chip 0xc8 of round 9 (push ((rand() & 255) + 64) · 0.1f turned by (0, 0,
// rand() % 360)) and the 0x5f3/1 dust of rounds 12, 17 and 19 (cooldown.py).
TEST(ActorCooldownTest, TheBrakeOfSeedThreeLeavesItsDustAndChipBitByBit)
{
    auto world = std::make_unique<Client::Kernel::WorldState>();
    Client::Kernel::ActorState actor;
    actor.movementCooldown = 1;
    actor.position = {12000.0f, 13000.0f, 250.5f};
    actor.angles = {0.0f, 0.0f, 135.0f};
    actor.light = {0.25f, 0.5f, 0.75f};
    Client::Crt::CrtRandom random(3);
    const Client::Actors::CharacterStepContext context{
        .world = *world, .random = random, .particles = &world->Particles()};
    ASSERT_TRUE(Client::Actors::StepActorCooldown(actor, random, Client::Actors::CharacterCooldownServices(context))
                    .HasValue());
    struct Dust
    {
        glm::vec3 position;
        std::uint32_t scale;
        glm::vec3 angles;
    };
    constexpr std::array<Dust, 3> Dusts{{
        {{11975.0f, 13014.0f, 237.5f}, 0x3ea3d70au, {104.0f, 0.0f, 109.0f}},
        {{12022.0f, 12987.0f, 256.5f}, 0x3ec7ae14u, {304.0f, 0.0f, 145.0f}},
        {{12017.0f, 12972.0f, 263.5f}, 0x3f2147aeu, {314.0f, 0.0f, 182.0f}},
    }};
    for (std::size_t slot = 0; slot < Dusts.size(); ++slot)
    {
        const auto& made = world->Particles().All()[slot];
        ASSERT_TRUE(made.alive) << slot;
        EXPECT_EQ(made.type, 0x5f3);
        EXPECT_EQ(made.subtype, 1);
        EXPECT_EQ(made.life, 40);
        EXPECT_EQ(made.position, Dusts[slot].position) << slot;
        EXPECT_EQ(std::bit_cast<std::uint32_t>(made.scale), Dusts[slot].scale) << slot;
        EXPECT_EQ(made.angles, Dusts[slot].angles) << slot;
        EXPECT_EQ(made.light, glm::vec3(1.0f));
        EXPECT_EQ(made.velocity, glm::vec3(0.0f));
        EXPECT_EQ(made.owner, 0u);
    }
    EXPECT_FALSE(world->Particles().All()[Dusts.size()].alive);
    const auto& chip = world->Map().effects.Effects()[0];
    ASSERT_TRUE(chip.alive);
    EXPECT_EQ(chip.type, 0xc8);
    EXPECT_EQ(chip.subtype, 0);
    EXPECT_EQ(chip.life, 36);
    EXPECT_EQ(chip.transform.position, actor.position);
    EXPECT_EQ(chip.transform.angles, (glm::vec3{0.0f, 0.0f, 175.0f}));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(chip.transform.scale), 0x3f4ccccdu);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(chip.velocity.x), 0xbf8b7301u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(chip.velocity.y), 0xc1473d2bu);
    EXPECT_EQ(chip.velocity.z, 0.0f);
    EXPECT_EQ(chip.turnDirection, 23.0f);
    EXPECT_FALSE(world->Map().effects.Effects()[1].alive);
}

// Without the particle pool in the context, the 0x5f3/1 stops at 0x506a73 with what came before done.
TEST(ActorCooldownTest, TheStepCreatorsCutWithoutTheParticlePool)
{
    auto world = std::make_unique<Client::Kernel::WorldState>();
    Client::Kernel::ActorState actor;
    Client::Crt::CrtRandom random(1);
    const Client::Actors::CharacterStepContext context{.world = *world, .random = random};
    const auto made = Client::Actors::CharacterCooldownServices(context).particle({1.0f, 2.0f, 3.0f}, actor);
    ASSERT_FALSE(made.HasValue());
    EXPECT_NE(made.Error().find("0x506a73"), std::string::npos) << made.Error();
    EXPECT_EQ(random.State(), Client::Crt::CrtRandom(1).State());
}
