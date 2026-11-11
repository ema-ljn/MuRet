#include "Scenes/ActorMotion.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorCooldownServices.h"
#include "Scenes/ActorState.h"
#include "support/PlayerActors.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <cstdint>
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
