#include "Scenes/ActorMotion.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorCooldownServices.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterStep.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldState.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "support/PlayerActors.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

TEST(ActorCooldownTest, FullLoopInterleavesBothCreatorsWithTheSharedRandom)
{
    Client::Scenes::ActorState actor;
    actor.movementCooldown = 2;
    actor.position = {100.0f, 200.0f, 300.0f};
    Client::Render::Models::CrtRandom random(2);
    std::vector<std::int32_t> events;
    glm::vec3 particlePosition{};
    const Client::Scenes::ActorCooldownServices services{
        .particle = [&](const glm::vec3& position,
                        const Client::Scenes::ActorState& live) -> Client::Core::Expected<void, std::string>
        {
            events.push_back(0x5f3);
            particlePosition = position;
            EXPECT_EQ(live.movementCooldown, 1);
            EXPECT_EQ(live.light, glm::vec3(1.0f));
            static_cast<void>(random.Next());
            return {};
        },
        .effect = [&](std::int32_t type,
                      const Client::Scenes::ActorState& live) -> Client::Core::Expected<void, std::string>
        {
            events.push_back(type);
            EXPECT_EQ(live.position, actor.position);
            static_cast<void>(random.Next());
            return {};
        }};
    const auto stepped = Client::Scenes::StepActorCooldown(actor, random, services);
    ASSERT_TRUE(stepped.HasValue()) << (stepped.HasValue() ? "" : stepped.Error());
    EXPECT_EQ(events, (std::vector<std::int32_t>{0x5f3, 0xc8}));
    EXPECT_EQ(particlePosition, (glm::vec3{78.0f, 196.0f, 305.0f}));
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(2, 103).State());
    EXPECT_EQ(actor.movementCooldown, 1);
}

TEST(ActorCooldownTest, MissingCreatorReturnsAfterTheDecrementLightAndFourRandomPrefix)
{
    Client::Scenes::ActorState actor;
    actor.movementCooldown = 60;
    Client::Render::Models::CrtRandom random(1);
    const auto stepped = Client::Scenes::StepActorCooldown(actor, random);
    EXPECT_FALSE(stepped.HasValue());
    EXPECT_EQ(actor.movementCooldown, 59);
    EXPECT_EQ(actor.light, glm::vec3(1.0f));
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 4).State());
}

// 0x5069ae..0x506ac6 con los creadores del paso (Scenes::CharacterCooldownServices): la 5f3/1 en el pool del contexto
// (0x506a73, con los ángulos y la luz del personaje, escala 1 y sin dueño) y la piedrita 0xc8 + (rand() & 1) en la
// tabla del mundo (0x506ab7, en la posición del personaje, sin dueño), cada uno con sus rand() en medio de las veinte
// vueltas. Réplica en un pool y una tabla gemelos con el mismo CrtRandom; con los dos llenos, los creadores no sacan
// nada y el freno sigue con sus cinco o seis rand() por vuelta.
TEST(ActorCooldownTest, TheStepCreatorsInterleaveTheBrakeDustAndTheStoneChips)
{
    for (const bool full : {false, true})
        for (std::uint32_t seed = 3; seed < 6; ++seed)
        {
            SCOPED_TRACE(testing::Message() << full << ' ' << seed);
            auto world = std::make_unique<Client::Scenes::WorldState>();
            Client::Scenes::ActorState actor;
            actor.movementCooldown = 1;
            actor.position = {12000.0f, 13000.0f, 250.5f};
            actor.angles = {0.0f, 0.0f, 135.0f};
            actor.light = {0.25f, 0.5f, 0.75f};
            Client::Render::Models::CrtRandom random(seed);
            auto twin = std::make_unique<Client::World::Particles>();
            auto twinEffects = std::make_unique<Client::World::ModelEffects>();
            if (full)
            {
                for (auto& particle : world->Particles().All())
                    particle.alive = true;
                for (auto& particle : twin->All())
                    particle.alive = true;
                for (auto& effect : world->Map().effects.Effects())
                    effect.alive = true;
                for (auto& effect : twinEffects->Effects())
                    effect.alive = true;
            }
            Client::Render::Models::CrtRandom oracle = random;
            const glm::vec3 white(1.0f);
            int dust = 0, chips = 0;
            for (int turn = 0; turn < 20; ++turn)
            {
                const auto x = static_cast<float>(static_cast<double>(oracle.Next() % 64 - 32) + 12000.0);
                const auto y = static_cast<float>(static_cast<double>(oracle.Next() % 64 - 32) + 13000.0);
                const auto z = static_cast<float>(static_cast<double>(oracle.Next() % 32 - 16) + 250.5);
                if (oracle.Next() % 10 == 0)
                {
                    ASSERT_TRUE(twin->CreateChecked(0x5f3, {x, y, z}, actor.angles, white, 1, 1.0f, 0.0f, oracle, 0)
                                    .HasValue());
                    ++dust;
                }
                if (oracle.Next() % 10 == 0)
                {
                    const auto type = static_cast<std::int16_t>(0xc8 + (oracle.Next() & 1));
                    static_cast<void>(twinEffects->CreateStoneChip(type, actor.position, actor.angles, white, oracle));
                    ++chips;
                }
            }
            const Client::Scenes::CharacterStepContext context{
                .world = *world, .random = random, .particles = &world->Particles()};
            const auto stepped =
                Client::Scenes::StepActorCooldown(actor, random, Client::Scenes::CharacterCooldownServices(context));
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            EXPECT_EQ(actor.movementCooldown, 0);
            EXPECT_EQ(actor.light, white);
            EXPECT_EQ(random.State(), oracle.State());
            EXPECT_GT(dust, 0);
            EXPECT_GT(chips, 0);
            for (std::size_t slot = 0; slot < Client::World::ParticleCount; ++slot)
            {
                const auto& made = world->Particles().All()[slot];
                const auto& reference = twin->All()[slot];
                ASSERT_EQ(made.alive, reference.alive) << slot;
                if (full || !made.alive)
                    continue;
                EXPECT_EQ(made.type, 0x5f3);
                EXPECT_EQ(made.subtype, 1);
                EXPECT_EQ(made.life, 40);
                EXPECT_EQ(made.position, reference.position);
                EXPECT_EQ(made.angles, reference.angles);
                EXPECT_EQ(made.light, white);
                EXPECT_EQ(made.scale, reference.scale);
                EXPECT_EQ(made.velocity, glm::vec3(0.0f));
                EXPECT_EQ(made.owner, 0u);
            }
            const auto& effects = world->Map().effects.Effects();
            const auto& twinChips = twinEffects->Effects();
            for (std::size_t slot = 0; slot < effects.size(); ++slot)
            {
                ASSERT_EQ(effects[slot].alive, twinChips[slot].alive) << slot;
                if (full || !effects[slot].alive)
                    continue;
                EXPECT_EQ(effects[slot].type, twinChips[slot].type);
                EXPECT_EQ(effects[slot].subtype, 0);
                EXPECT_EQ(effects[slot].transform.position, actor.position);
                EXPECT_EQ(effects[slot].transform.angles, twinChips[slot].transform.angles);
                EXPECT_EQ(effects[slot].velocity, twinChips[slot].velocity);
                EXPECT_EQ(effects[slot].life, twinChips[slot].life);
            }
        }
}

// Sin el pool de partículas en el contexto, la 5f3/1 corta en 0x506a73 con lo de antes hecho.
TEST(ActorCooldownTest, TheStepCreatorsCutWithoutTheParticlePool)
{
    auto world = std::make_unique<Client::Scenes::WorldState>();
    Client::Scenes::ActorState actor;
    Client::Render::Models::CrtRandom random(1);
    const Client::Scenes::CharacterStepContext context{.world = *world, .random = random};
    const auto made = Client::Scenes::CharacterCooldownServices(context).particle({1.0f, 2.0f, 3.0f}, actor);
    ASSERT_FALSE(made.HasValue());
    EXPECT_NE(made.Error().find("0x506a73"), std::string::npos) << made.Error();
    EXPECT_EQ(random.State(), Client::Render::Models::CrtRandom(1).State());
}
