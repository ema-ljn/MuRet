#include "World/Leaves.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "World/ILeafWaveEvaluator.h"
#include "World/LeafWeather.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <system_error>
#include <vector>

namespace
{
using Client::Core::Expected;
using Client::World::Leaf;
using Client::World::LeafMap31Predicate;
using Client::World::LeafMap34Predicate;
using Client::World::LeafMapIsNot35;
using Client::World::LeafPoolHandlers;
using Client::World::LeafWeather;
using Client::World::Leaves;
using Client::World::StepWorldLeaves;
using Client::World::VisitLeafPool;

TEST(LeafPoolTest, Map31PredicateReadsTheFullMapDomain)
{
    for (std::int32_t map = 0; map <= 36; ++map)
        EXPECT_EQ(LeafMap31Predicate(map), map == 31) << map;
    for (const std::int32_t map : {-1, 37, 53, 54, 55, INT32_MIN, INT32_MAX})
        EXPECT_FALSE(LeafMap31Predicate(map)) << map;
}

TEST(LeafPoolTest, Map35PredicateMatchesTheComplementAcrossSignedMapDomain)
{
    for (std::int32_t map = 0; map <= 36; ++map)
        EXPECT_EQ(LeafMapIsNot35(map), map != 35) << map;
    for (const std::int32_t map : {-1, 37, 53, 54, 55, INT32_MIN, INT32_MAX})
        EXPECT_TRUE(LeafMapIsNot35(map)) << map;
}

TEST(LeafPoolTest, Map34PredicateMatchesOnlyTheExactRawMap)
{
    for (std::int32_t map = 0; map <= 36; ++map)
        EXPECT_EQ(LeafMap34Predicate(map), map == 34) << map;
    for (const std::int32_t map : {-1, 37, 53, 54, 55, INT32_MIN, INT32_MAX})
        EXPECT_FALSE(LeafMap34Predicate(map)) << map;
}

// Spies de composición530490, no bindings productivos de los handlers ni oráculo numéricoFSIN.
class NumericSpy final : public Client::World::ILeafWaveEvaluator
{
public:
    explicit NumericSpy(std::int32_t& map) : m_map(map) {}
    Expected<void, std::error_code> Evaluate(Client::World::LeafWave wave, float) override
    {
        if (changeMap && wave == Client::World::LeafWave::Second)
            m_map = 9;
        return {};
    }
    Expected<std::int32_t, std::error_code> Truncate() override
    {
        return std::int32_t{0};
    }
    bool changeMap = false;

private:
    std::int32_t& m_map;
};

TEST(LeafPoolTest, DefaultsPrecedeTheFirstCreatorAndAnyNonzeroAlStopsEvenAfterDeath)
{
    Leaves leaves;
    Leaf& leaf = leaves.All()[0];
    leaf.light = {7, 8, 9};
    leaves.All()[199].alive = 0xff;
    const std::int32_t selector = 0;
    std::vector<int> events;
    LeafPoolHandlers handlers;
    handlers.creators[0] = [&](Leaf& slot, std::int32_t index) -> Expected<std::uint8_t, std::error_code>
    {
        events.push_back(1);
        EXPECT_EQ(&slot, &leaf);
        EXPECT_EQ(index, 0);
        EXPECT_EQ(slot.alive, 1);
        EXPECT_EQ(slot.light, glm::vec3(1));
        slot.alive = 0;
        leaves.All()[1].alive = 0x80; // el slot siguiente debe leer el flag recién mutado.
        return std::uint8_t{0x80};
    };
    handlers.updaters[0] = [&](Leaf& slot) -> Expected<std::uint8_t, std::error_code>
    {
        events.push_back(2);
        EXPECT_EQ(&slot, &leaves.All()[1]);
        EXPECT_EQ(slot.alive, 0x80);
        return std::uint8_t{1};
    };
    ASSERT_TRUE(VisitLeafPool(leaves, 2, selector, handlers).HasValue());
    EXPECT_EQ(events, (std::vector<int>{1, 2}));
    EXPECT_EQ(leaf.alive, 0);
    EXPECT_EQ(leaves.All()[199].alive, 0xff);
}

TEST(LeafPoolTest, ZeroAlPreservesStoresAndContinuesWithoutRecheckingAlive)
{
    Leaves leaves;
    const std::int32_t selector = 0;
    LeafPoolHandlers handlers;
    handlers.creators[0] = [](Leaf& leaf, std::int32_t) -> Expected<std::uint8_t, std::error_code>
    {
        leaf.alive = 0;
        leaf.light.x = 7;
        return std::uint8_t{0};
    };
    handlers.creators[1] = [](Leaf& leaf, std::int32_t) -> Expected<std::uint8_t, std::error_code>
    {
        EXPECT_EQ(leaf.alive, 0);
        EXPECT_EQ(leaf.light.x, 7);
        return std::uint8_t{1};
    };
    ASSERT_TRUE(VisitLeafPool(leaves, 1, selector, handlers).HasValue());
    EXPECT_EQ(leaves.All()[0].alive, 0);
}

TEST(LeafPoolTest, NineCreatorsRunInOrderBeforeTheLateSelectorAndIgnoredFallbackAl)
{
    Leaves leaves;
    std::int32_t selector = 0;
    std::vector<int> events;
    LeafPoolHandlers handlers;
    for (std::size_t i = 0; i < handlers.creators.size(); ++i)
        handlers.creators[i] = [&, i](Leaf& leaf, std::int32_t index) -> Expected<std::uint8_t, std::error_code>
        {
            events.push_back(static_cast<int>(i));
            EXPECT_EQ(index, 0);
            if (i == 8)
            {
                selector = 2;
                leaf.alive = 0;
            }
            return std::uint8_t{0};
        };
    handlers.fallback = [&](Leaf& leaf, std::int32_t index) -> Expected<std::uint8_t, std::error_code>
    {
        events.push_back(9);
        EXPECT_EQ(index, 0);
        EXPECT_EQ(leaf.alive, 0);
        return std::uint8_t{0};
    };
    ASSERT_TRUE(VisitLeafPool(leaves, 1, selector, handlers).HasValue());
    EXPECT_EQ(events, (std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9}));
}

TEST(LeafPoolTest, NoFallbackLeavesTheUnrecognizedSlotAliveWithItsDefaults)
{
    Leaves leaves;
    const std::int32_t selector = 1;
    LeafPoolHandlers handlers;
    for (auto& creator : handlers.creators)
        creator = [](Leaf&, std::int32_t) -> Expected<std::uint8_t, std::error_code> { return std::uint8_t{0}; };
    ASSERT_TRUE(VisitLeafPool(leaves, 1, selector, handlers).HasValue());
    EXPECT_EQ(leaves.All()[0].alive, 1);
    EXPECT_EQ(leaves.All()[0].light, glm::vec3(1));
}

TEST(LeafPoolTest, ActiveZeroAlChainAndFallContinueAfterAnEarlierHandlerKillsTheSlot)
{
    Leaves leaves;
    leaves.All()[0].alive = 0x80;
    const std::int32_t selector = 0;
    std::vector<int> events;
    LeafPoolHandlers handlers;
    for (std::size_t i = 0; i < handlers.updaters.size(); ++i)
        handlers.updaters[i] = [&, i](Leaf& leaf) -> Expected<std::uint8_t, std::error_code>
        {
            events.push_back(static_cast<int>(i));
            if (i == 0)
                leaf.alive = 0;
            else
                EXPECT_EQ(leaf.alive, 0);
            return std::uint8_t{0};
        };
    handlers.fall = [&](Leaf& leaf) -> Expected<void, std::error_code>
    {
        events.push_back(3);
        EXPECT_EQ(leaf.alive, 0);
        return {};
    };
    ASSERT_TRUE(VisitLeafPool(leaves, 1, selector, handlers).HasValue());
    EXPECT_EQ(events, (std::vector<int>{0, 1, 2, 3}));
}

TEST(LeafPoolTest, NonzeroUpdaterStopsEvenWhenItKillsTheSlot)
{
    Leaves leaves;
    leaves.All()[0].alive = 0x80;
    const std::int32_t selector = 0;
    LeafPoolHandlers handlers;
    handlers.updaters[0] = [](Leaf& leaf) -> Expected<std::uint8_t, std::error_code>
    {
        leaf.alive = 0;
        return std::uint8_t{0xff};
    };
    ASSERT_TRUE(VisitLeafPool(leaves, 1, selector, handlers).HasValue());
    EXPECT_EQ(leaves.All()[0].alive, 0);
}

TEST(LeafPoolTest, MissingReachedHandlerErrorsAfterDefaultsWithoutTouchingLaterSlots)
{
    Leaves leaves;
    const std::int32_t selector = 0;
    const LeafPoolHandlers handlers;
    const auto result = VisitLeafPool(leaves, 2, selector, handlers);
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error(), std::make_error_code(std::errc::function_not_supported));
    EXPECT_EQ(leaves.All()[0].alive, 1);
    EXPECT_EQ(leaves.All()[0].light, glm::vec3(1));
    EXPECT_EQ(leaves.All()[1].alive, 0);
    EXPECT_EQ(leaves.All()[1].light, glm::vec3(0));
}

TEST(LeafPoolTest, InvalidPrefixErrorsBeforeStoresAndEmptyPrefixNeedsNoHandlers)
{
    Leaves leaves;
    const std::int32_t selector = 0;
    const LeafPoolHandlers handlers;
    const auto invalid = VisitLeafPool(leaves, 201, selector, handlers);
    ASSERT_FALSE(invalid.HasValue());
    EXPECT_EQ(invalid.Error(), std::make_error_code(std::errc::invalid_argument));
    EXPECT_EQ(leaves.All()[0].alive, 0);
    ASSERT_TRUE(VisitLeafPool(leaves, 0, selector, handlers).HasValue());
}

TEST(LeafPoolTest, FrameStoresWavesAndPhaseBeforePoolAndKeepsItsCapturedMapPrefix)
{
    Leaves leaves;
    LeafWeather weather{.target = 17, .current = 15, .phase = 1990};
    std::int32_t map = 30;
    const std::int32_t selector = 0;
    const float time = 0;
    NumericSpy numeric(map);
    numeric.changeMap = true;
    std::int32_t calls = 0;
    LeafPoolHandlers handlers;
    handlers.creators[0] = [&](Leaf&, std::int32_t index) -> Expected<std::uint8_t, std::error_code>
    {
        EXPECT_EQ(weather.current, 16);
        EXPECT_EQ(weather.firstWave, 30);
        EXPECT_EQ(weather.secondWave, 0);
        EXPECT_EQ(weather.phase, 10);
        EXPECT_EQ(map, 9);
        EXPECT_EQ(index, calls);
        ++calls;
        return std::uint8_t{1};
    };
    ASSERT_TRUE(StepWorldLeaves(leaves, weather, map, selector, time, &numeric, handlers).HasValue());
    EXPECT_EQ(calls, 40);
    EXPECT_EQ(leaves.All()[39].alive, 1);
    EXPECT_EQ(leaves.All()[40].alive, 0);
}

TEST(LeafPoolTest, EveryMapFamilyProcessesOnlyItsPreparedPrefix)
{
    constexpr std::array<std::size_t, 37> Counts{80, 80, 80, 80, 80, 80, 80, 80, 80, 200, 80, 80, 80,
                                                 80, 80, 80, 80, 80, 80, 80, 80, 80, 80,  80, 80, 80,
                                                 80, 80, 80, 80, 40, 80, 80, 80, 80, 80,  80};
    const auto run = [](std::int32_t map, std::int32_t selector, std::size_t expected)
    {
        Leaves leaves;
        for (Leaf& leaf : leaves.All())
            leaf.alive = 0x80;
        LeafWeather weather;
        const float time = 0;
        NumericSpy numeric(map);
        std::size_t calls = 0;
        LeafPoolHandlers handlers;
        handlers.updaters[0] = [&](Leaf&) -> Expected<std::uint8_t, std::error_code>
        {
            ++calls;
            return std::uint8_t{0x80};
        };
        ASSERT_TRUE(StepWorldLeaves(leaves, weather, map, selector, time, &numeric, handlers).HasValue());
        EXPECT_EQ(calls, expected) << map << ',' << selector;
        EXPECT_EQ(leaves.All()[199].alive, 0x80);
    };
    for (std::size_t map = 0; map < Counts.size(); ++map)
        run(static_cast<std::int32_t>(map), 0, Counts[map]);
    run(34, 1, 60);
    run(34, 2, 50);
    run(54, 0, 80);
    run(55, 0, 80);
}

TEST(LeafPoolTest, CallbacksConsumeTheSharedRandomInSlotAndHandlerOrder)
{
    Leaves leaves;
    const std::int32_t selector = 0;
    Client::Render::Models::CrtRandom random(74);
    std::vector<int> draws;
    LeafPoolHandlers handlers;
    handlers.creators[0] = [&](Leaf&, std::int32_t) -> Expected<std::uint8_t, std::error_code>
    {
        draws.push_back(random.Next());
        return std::uint8_t{0};
    };
    handlers.creators[1] = [&](Leaf&, std::int32_t) -> Expected<std::uint8_t, std::error_code>
    {
        draws.push_back(random.Next());
        return std::uint8_t{1};
    };
    ASSERT_TRUE(VisitLeafPool(leaves, 2, selector, handlers).HasValue());
    EXPECT_EQ(draws, (std::vector<int>{280, 16669, 32467, 13650}));
    EXPECT_EQ(random.State(), 0x3552582eu);
}

TEST(LeafPoolTest, ErrorsFromReachedCreatorUpdaterFallbackAndFallKeepTheExecutedPrefix)
{
    const auto fail = [](Leaf& leaf) -> Expected<std::uint8_t, std::error_code>
    {
        leaf.light.x = 7;
        return Client::Core::Unexpected(std::make_error_code(std::errc::io_error));
    };
    for (int mode = 0; mode < 4; ++mode)
    {
        Leaves leaves;
        if (mode == 1 || mode == 3)
            leaves.All()[0].alive = 0x80;
        const std::int32_t selector = mode == 2 ? 2 : 0;
        LeafPoolHandlers handlers;
        for (auto& creator : handlers.creators)
            creator = [](Leaf&, std::int32_t) -> Expected<std::uint8_t, std::error_code> { return std::uint8_t{0}; };
        for (auto& updater : handlers.updaters)
            updater = [](Leaf&) -> Expected<std::uint8_t, std::error_code> { return std::uint8_t{0}; };
        if (mode == 0)
            handlers.creators[1] = [&](Leaf& leaf, std::int32_t) { return fail(leaf); };
        if (mode == 1)
            handlers.updaters[1] = fail;
        handlers.fallback = [&](Leaf& leaf, std::int32_t) { return fail(leaf); };
        handlers.fall = [](Leaf& leaf) -> Expected<void, std::error_code>
        {
            leaf.light.x = 7;
            return Client::Core::Unexpected(std::make_error_code(std::errc::io_error));
        };
        const auto result = VisitLeafPool(leaves, 2, selector, handlers);
        ASSERT_FALSE(result.HasValue()) << mode;
        EXPECT_EQ(result.Error(), std::make_error_code(std::errc::io_error));
        EXPECT_EQ(leaves.All()[0].light.x, 7);
        EXPECT_EQ(leaves.All()[1].alive, 0);
    }
}

TEST(LeafPoolTest, MissingNumericBindingKeepsPreparationButDoesNotVisitPool)
{
    Leaves leaves;
    LeafWeather weather{.target = 17, .current = 15, .phase = 1990};
    const std::int32_t map = 10;
    const std::int32_t selector = 0;
    const float time = 0;
    const LeafPoolHandlers handlers;
    const auto result = StepWorldLeaves(leaves, weather, map, selector, time, nullptr, handlers);
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error(), std::make_error_code(std::errc::function_not_supported));
    EXPECT_EQ(weather.target, 100);
    EXPECT_EQ(weather.current, 16);
    EXPECT_EQ(weather.phase, 1990);
    EXPECT_EQ(leaves.All()[0].alive, 0);
}
} // namespace
