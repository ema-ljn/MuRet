#include "MapScripts/Kalima/KalimaObjectStep.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Models/Cloth.h"
#include "Kernel/ActorState.h"
#include "Fauna/FaunaState.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Kernel/WorldMap.h"
#include "Particles/Particles.h"
#include "Effects/ModelEffects.h"
#include "Particles/ParticleOwner.h"

#include <gtest/gtest.h>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstdint>
#include <memory>

TEST(KalimaObjectStepTest, TheNativeOverlappingLevelTableKeepsItsFirstMatchAndUnmatchedSeven)
{
    EXPECT_EQ(Client::MapScripts::Kalima::KalimaLevelOf(0, 39), 7);
    EXPECT_EQ(Client::MapScripts::Kalima::KalimaLevelOf(0, 40), 1);
    EXPECT_EQ(Client::MapScripts::Kalima::KalimaLevelOf(0, 350), 1);
    EXPECT_EQ(Client::MapScripts::Kalima::KalimaLevelOf(0, 999), 1);
    EXPECT_EQ(Client::MapScripts::Kalima::KalimaLevelOf(0, 1000), 7);
    for (const std::uint8_t characterClass : std::array<std::uint8_t, 4>{3, 4, 11, 12})
    {
        EXPECT_EQ(Client::MapScripts::Kalima::KalimaLevelOf(characterClass, 19), 7);
        EXPECT_EQ(Client::MapScripts::Kalima::KalimaLevelOf(characterClass, 20), 1);
    }
}

TEST(KalimaObjectStepTest, OwnRecordAndSeparateCellsCloseBeforeTheSelectorAndNativeAmbientCreation)
{
    bool reachedParticle = false;
    bool returned = false;
    for (std::uint32_t seed = 1; seed < 20; ++seed)
    {
        auto world = std::make_unique<Client::Kernel::WorldState>();
        auto& hero = world->Actors()[0];
        hero.cellX = 40;
        hero.cellY = 64;
        world->OwnCharacter().level = 0;
        Client::Models::ClothWorld cloth{.random = Client::Crt::CrtRandom{seed}};
        auto expected = cloth.random;
        const int ambient = 0x1d4 + expected.Next() % 2;
        for (int cellCloses = 0; cellCloses < 8; ++cellCloses)
            static_cast<void>(expected.Next());
        const int region = 0x1d9 + expected.Next() % 2;
        for (int close = 0; close < 3848; ++close)
            static_cast<void>(expected.Next());
        static_cast<void>(expected.Next()); // selection gate; count 0 does not consume another.
        const bool particle = expected.Next() % 5 == 0;
        if (particle)
        {
            static_cast<void>(expected.Next());
            static_cast<void>(expected.Next());
        }
        bool model = false;
        if (particle)
        {
            for (int creator = 0; creator < 3; ++creator)
                static_cast<void>(expected.Next());
            model = expected.Next() % 15 == 0;
            if (model)
                static_cast<void>(expected.Next());
        }
        const auto result = Client::MapScripts::Kalima::StepKalimaObjectPrelude(*world, cloth);
        EXPECT_EQ(cloth.random.State(), expected.State());
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(world->Runtime().ownRecordDepth, 0);
        EXPECT_EQ(hero.cellXAccesses, 0);
        EXPECT_EQ(hero.cellYAccesses, 0);
        EXPECT_EQ(world->Runtime().objectSelectionCount, 0);
        ASSERT_EQ(world->Fauna().Sounds().size(), model ? 4u : 3u);
        EXPECT_EQ(world->Fauna().Sounds()[0].id, 0x1d3);
        EXPECT_EQ(world->Fauna().Sounds()[1].id, ambient);
        EXPECT_EQ(world->Fauna().Sounds()[2].id, region);
        EXPECT_EQ(world->Fauna().Sounds()[0].owner, nullptr);
        if (particle)
        {
            const auto& p = world->Particles().All()[0];
            EXPECT_TRUE(p.alive);
            EXPECT_EQ(p.type, 0x5ac);
            EXPECT_EQ(p.subtype, 7);
            EXPECT_EQ(p.typedOwner.kind, Client::Particles::ParticleOwnerKind::Actor);
            EXPECT_EQ(p.light, glm::vec3(0.3f, 0.8f, 1));
            if (model)
            {
                EXPECT_TRUE(world->Map().effects.Effects()[0].alive);
                EXPECT_EQ(world->Map().effects.Effects()[0].type, 9);
                EXPECT_EQ(world->Fauna().Sounds()[3].id, 0x1d7);
            }
            reachedParticle = true;
        }
        else
            returned = true;
    }
    EXPECT_TRUE(reachedParticle);
    EXPECT_TRUE(returned);
}

TEST(KalimaObjectStepTest, ASelectedRetainedCountReturnsBeforeTheSpawnGuardAndDoesNotResetItsOwner)
{
    auto world = std::make_unique<Client::Kernel::WorldState>();
    world->OwnCharacter().level = 40;
    world->Runtime().ownRecordDepth = 1;
    world->Runtime().objectSelectionCount = 3;
    bool selected = false;
    for (std::uint32_t seed = 1; seed < 200 && !selected; ++seed)
    {
        Client::Models::ClothWorld cloth{.random = Client::Crt::CrtRandom{seed}, .logicSteps = 1};
        auto expected = cloth.random;
        if (expected.Next() % 10 != 0)
            continue;
        const int selector = expected.Next() % 3;
        const auto result = Client::MapScripts::Kalima::StepKalimaObjectPrelude(*world, cloth);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(result.Value(), selector);
        EXPECT_EQ(cloth.random.State(), expected.State());
        EXPECT_EQ(world->Runtime().objectSelectionCount, 3);
        EXPECT_EQ(world->Runtime().ownRecordDepth, 1);
        selected = true;
    }
    EXPECT_TRUE(selected);
}

// 0x43878a and 0x4387f4: the final close leaves four bytes of rand() in each cell; nested accesses do not write.
TEST(KalimaObjectStepTest, RegionCellClosesStoreTheNativeRandomBytesOnlyAtDepthZero)
{
    constexpr std::uint32_t InitialLeft = 0x12345678;
    for (const std::uint8_t depth : std::array<std::uint8_t, 3>{0, 1, 255})
    {
        SCOPED_TRACE(depth);
        bool selected = false;
        for (std::uint32_t seed = 1; seed < 200 && !selected; ++seed)
        {
            Client::Models::ClothWorld cloth{.random = Client::Crt::CrtRandom{seed}, .logicSteps = 1};
            auto expected = cloth.random;
            const auto close = [&expected, depth]
            {
                if (depth != 0)
                    return InitialLeft;
                std::uint32_t left = 0;
                for (unsigned byte = 0; byte < 4; ++byte)
                    left |= (static_cast<std::uint32_t>(expected.Next()) & 0xffu) << (8u * byte);
                return left;
            };
            const auto expectedX = close();
            const auto expectedY = close();
            if (expected.Next() % 10 != 0)
                continue;
            const int selector = expected.Next() % 3;
            auto world = std::make_unique<Client::Kernel::WorldState>();
            auto& hero = world->Actors()[0];
            hero.cellX = 40;
            hero.cellY = 64;
            hero.cellXAccesses = depth;
            hero.cellYAccesses = depth;
            hero.cellXLeft = InitialLeft;
            hero.cellYLeft = InitialLeft;
            world->OwnCharacter().level = 0;
            world->Runtime().ownRecordDepth = 1;
            world->Runtime().objectSelectionCount = 3;
            const auto result = Client::MapScripts::Kalima::StepKalimaObjectPrelude(*world, cloth);
            ASSERT_TRUE(result.HasValue()) << result.Error();
            EXPECT_EQ(result.Value(), selector);
            EXPECT_EQ(hero.cellXLeft, expectedX);
            EXPECT_EQ(hero.cellYLeft, expectedY);
            EXPECT_EQ(hero.cellXAccesses, depth);
            EXPECT_EQ(hero.cellYAccesses, depth);
            EXPECT_EQ(cloth.random.State(), expected.State());
            selected = true;
        }
        EXPECT_TRUE(selected);
    }
}
