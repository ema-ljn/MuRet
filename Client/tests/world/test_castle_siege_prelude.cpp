#include "CastleSiege/CastleSiegeObjectStep.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Kernel/ModelEffectArguments.h"
#include "Models/LogicalAccess.h"
#include "Effects/ModelEffects.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ModelEffectArguments;

class CastleSiegePreludeTest : public ::testing::Test
{
protected:
    Client::World::CastleSiegePrelude Prelude(std::int32_t frame)
    {
        return {.siegeActive = true,
                .musicTime = &m_music,
                .systemTime = [] { return 7000u; },
                .sound = [this](Client::Audio::SoundId sound) { m_sounds.push_back(sound.Value()); },
                .random = &m_random,
                .logicSteps = frame,
                .heroPosition = {10000.0f, 10000.0f, 270.0f},
                .heroY =
                    [this]
                {
                    ++m_reads;
                    const Client::Models::LogicalAccess access(m_depth, m_random,
                                                               Client::Models::SmallLogicalCloseRandoms);
                    return m_y;
                },
                .emit = [this](const ModelEffectArguments& args) -> Client::Core::Expected<void, std::string>
                {
                    m_emitted.push_back(args);
                    return {};
                }};
    }

    float m_music = 0.0f;
    CrtRandom m_random{1};
    std::uint8_t m_depth = 0;
    std::int32_t m_y = 0;
    std::int32_t m_reads = 0;
    std::vector<std::int32_t> m_sounds;
    std::vector<ModelEffectArguments> m_emitted;
};

// Frame1: rand%10 (41), un cierre de4bytes delY; sin rangos, no hay emisiones.
TEST_F(CastleSiegePreludeTest, WarContinuesAfterTheSoundAndReadsTheProtectedHeroY)
{
    const auto result = Client::World::StepCastleSiegePrelude(Prelude(1));
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_reads, 1);
    EXPECT_EQ(m_depth, 0);
    CrtRandom expected(1);
    expected.Discard(5);
    EXPECT_EQ(m_random.State(), expected.State());
    EXPECT_EQ(m_sounds, (std::vector<std::int32_t>{0x1e2}));
    EXPECT_TRUE(m_emitted.empty());
}

// Frame20: dos accesos independientes alY y rand%3 antes de comprobar su rango.
TEST_F(CastleSiegePreludeTest, TheRockFrameReadsYTwiceEvenOutsideEveryEmissionRange)
{
    const auto result = Client::World::StepCastleSiegePrelude(Prelude(20));
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_reads, 2);
    CrtRandom expected(1);
    expected.Discard(10);
    EXPECT_EQ(m_random.State(), expected.State());
    EXPECT_TRUE(m_emitted.empty());
}

TEST_F(CastleSiegePreludeTest, AnOuterProtectedAccessPreventsBothInnerReencryptions)
{
    m_depth = 1;
    const auto result = Client::World::StepCastleSiegePrelude(Prelude(20));
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(m_reads, 2);
    EXPECT_EQ(m_depth, 1);
    CrtRandom expected(1);
    expected.Discard(2);
    EXPECT_EQ(m_random.State(), expected.State());
}
// 42b613..42b693: aun cuando el cuarto sorteo suena, el quinto se consume antes del cierreY.
TEST_F(CastleSiegePreludeTest, AllFiveBattleSoundDrawsRunIndependently)
{
    m_random = CrtRandom(420);
    ASSERT_TRUE(Client::World::StepCastleSiegePrelude(Prelude(1)).HasValue());
    EXPECT_EQ(m_sounds, (std::vector<std::int32_t>{0x1e2, 0x1e6}));
    CrtRandom expected(420);
    expected.Discard(10);
    EXPECT_EQ(m_random.State(), expected.State());
}

// Traza estática con delegado sinRNG: 1piedra,4flechasdefila y1individual. No representa elRNGdelcallee518cf0.
TEST_F(CastleSiegePreludeTest, TheSixtiethFrameEmitsTheNativeArgumentsInOrder)
{
    m_y = 100;
    ASSERT_TRUE(Client::World::StepCastleSiegePrelude(Prelude(60)).HasValue());
    ASSERT_EQ(m_emitted.size(), 6u);
    EXPECT_EQ(m_emitted[0].type, 0x12b);
    EXPECT_EQ(m_emitted[0].position, (glm::vec3{9860.0f, 4482.0f, 500.0f}));
    for (std::size_t index = 1; index <= 4; ++index)
    {
        EXPECT_EQ(m_emitted[index].type, 0xdb);
        EXPECT_EQ(m_emitted[index].subtype, 4);
        EXPECT_EQ(m_emitted[index].position,
                  (glm::vec3{9401.0f + static_cast<float>(index - 1) * 60.0f, 9000.0f, 350.0f}));
        EXPECT_EQ(m_emitted[index].angles, (glm::vec3{0.0f, 0.0f, 180.0f}));
    }
    EXPECT_EQ(m_emitted[5].type, 0xdb);
    EXPECT_EQ(m_emitted[5].subtype, 3);
    EXPECT_EQ(m_emitted[5].position, (glm::vec3{9453.0f, 10974.0f, 350.0f}));
    EXPECT_EQ(m_random.State(), 0x491c86cdu);
}
TEST_F(CastleSiegePreludeTest, BothRockFamiliesKeepTheirStrictLogicalYBounds)
{
    for (const auto& [y, emits] :
         std::array<std::pair<std::int32_t, bool>, 4>{{{50, false}, {51, true}, {130, true}, {131, false}}})
    {
        m_y = y;
        m_random = CrtRandom(1); // El sexto randelige12b.
        m_emitted.clear();
        ASSERT_TRUE(Client::World::StepCastleSiegePrelude(Prelude(20)).HasValue());
        EXPECT_EQ(std::ranges::any_of(m_emitted, [](const auto& args) { return args.type == 0x12b; }), emits) << y;
    }
    for (const auto& [y, emits] :
         std::array<std::pair<std::int32_t, bool>, 4>{{{70, false}, {71, true}, {176, true}, {177, false}}})
    {
        m_y = y;
        m_random = CrtRandom(2); // El sexto randelige12c.
        m_emitted.clear();
        ASSERT_TRUE(Client::World::StepCastleSiegePrelude(Prelude(20)).HasValue());
        EXPECT_EQ(std::ranges::any_of(m_emitted, [](const auto& args) { return args.type == 0x12c; }), emits) << y;
    }
}
} // namespace
