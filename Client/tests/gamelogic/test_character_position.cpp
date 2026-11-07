#include "GameLogic/CharacterPosition.h"
#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>
#include <bit>
#include <cstdint>
namespace
{
struct Character
{
    glm::vec3 position{100.0f, 200.0f, 300.0f};
    glm::vec3 angles{0.0f};
    float phase = 0.0f;
    std::uint16_t type = 0;
    std::uint16_t item = 0xffff;
    std::uint8_t safeZone = 0;
    std::int32_t map = -1;
    Client::GameLogic::CharacterPositionState View()
    {
        return {position, angles, phase, type, item, safeZone, map};
    }
};
TEST(CharacterPositionTest, TerrainSeesProvisionalPositionAndReplacesZ)
{
    Character actor;
    int speedCalls = 0;
    int heightCalls = 0;
    Client::GameLogic::UpdateCharacterPosition(
        actor.View(),
        [&]
        {
            ++speedCalls;
            return 12.0;
        },
        [&](float x, float y)
        {
            ++heightCalls;
            EXPECT_EQ(x, 100.0f);
            EXPECT_EQ(y, 188.0f);
            EXPECT_EQ(actor.position.z, 300.0f);
            return 20.0;
        });
    EXPECT_EQ(actor.position, (glm::vec3{100.0f, 188.0f, 20.0f}));
    EXPECT_EQ(speedCalls, 1);
    EXPECT_EQ(heightCalls, 1);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(actor.phase), 0x3e19999au);
}
TEST(CharacterPositionTest, MatrixPrecedesSpeedAndLiveFieldsAreReadAfterIt)
{
    Character actor;
    Client::GameLogic::UpdateCharacterPosition(
        actor.View(),
        [&]
        {
            actor.angles.z = 90.0f;
            actor.position.y = 500.0f;
            actor.item = 0x1c15;
            actor.map = 8;
            return 12.0;
        },
        [](float, float) { return 20.0; });
    EXPECT_EQ(actor.position.x, 100.0f);
    EXPECT_EQ(actor.position.y, 488.0f);
    EXPECT_EQ(actor.position.z, 110.0f);
}
TEST(CharacterPositionTest, HeightOffsetsRespectMapItemAndSafeZonePrecedence)
{
    for (const std::int32_t map : {-1, 0, 8, 10, 18})
        for (const int safeZone : {0, 1, 2})
        {
            Character actor;
            actor.map = map;
            actor.item = 0x1c15;
            actor.safeZone = static_cast<std::uint8_t>(safeZone);
            Client::GameLogic::UpdateCharacterPosition(
                actor.View(), [] { return 0.0; }, [](float, float) { return 20.0; });
            const float expected = map == -1 || safeZone != 0 ? 20.0f : (map == 8 || map == 10 ? 110.0f : 50.0f);
            EXPECT_EQ(actor.position.z, expected);
        }
}
TEST(CharacterPositionTest, TerrainCanChangeTypeAndPhaseBeforeTheSineCorrection)
{
    Character actor;
    Client::GameLogic::UpdateCharacterPosition(
        actor.View(), [] { return 0.0; },
        [&](float, float)
        {
            actor.type = 0x154;
            actor.phase = 0.0f;
            actor.item = 0x1c15;
            actor.map = 8;
            return 20.0;
        });
    EXPECT_EQ(actor.position.z, 90.0f);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(actor.phase), 0x3e19999au);
}
TEST(CharacterPositionTest, SineIsSpilledBeforeAbsoluteAndPhaseNeverWraps)
{
    Character actor;
    actor.type = 0x154;
    actor.phase = -1.0f;
    Client::GameLogic::UpdateCharacterPosition(actor.View(), [] { return 0.0; }, [](float, float) { return 20.0; });
    // sin(-1) se redondea a 0xbf576aa4 antes de abs y de multiplicar por 70.
    EXPECT_EQ(std::bit_cast<std::uint32_t>(actor.position.z), 0x41f8c6b9u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(actor.phase), 0xbf59999au);
}
TEST(CharacterPositionTest, BaseHeightPreservesNegativeZeroWithoutAnExtraAddition)
{
    Character actor;
    Client::GameLogic::UpdateCharacterPosition(actor.View(), [] { return 0.0; }, [](float, float) { return -0.0; });
    EXPECT_EQ(std::bit_cast<std::uint32_t>(actor.position.z), 0x80000000u);
}
TEST(CharacterPositionTest, TerrainObservesTheProvisionalZFromThePitchedMovement)
{
    Character actor;
    actor.angles.x = 30.0f;
    Client::GameLogic::UpdateCharacterPosition(
        actor.View(), [] { return 12.0; },
        [&](float, float)
        {
            EXPECT_EQ(actor.position.z, 294.0f);
            return 20.0;
        });
    EXPECT_EQ(actor.position.z, 20.0f);
}
TEST(CharacterPositionTest, OtherItemsNeverReceiveTheMapHeightOffset)
{
    Character actor;
    actor.item = 0x1c16;
    actor.map = 8;
    Client::GameLogic::UpdateCharacterPosition(actor.View(), [] { return 0.0; }, [](float, float) { return 20.0; });
    EXPECT_EQ(actor.position.z, 20.0f);
}
} // namespace
