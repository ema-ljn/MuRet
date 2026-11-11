#include "Scenes/KalimaObjectStep.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorState.h"
#include "Scenes/FaunaState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "Scenes/WorldMap.h"
#include "World/Particles.h"
#include "World/ModelEffects.h"
#include "World/ParticleOwner.h"

#include <gtest/gtest.h>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstdint>
#include <memory>

TEST(KalimaObjectStepTest, TheNativeOverlappingLevelTableKeepsItsFirstMatchAndUnmatchedSeven)
{
    EXPECT_EQ(Client::Scenes::KalimaLevelOf(0, 39), 7);
    EXPECT_EQ(Client::Scenes::KalimaLevelOf(0, 40), 1);
    EXPECT_EQ(Client::Scenes::KalimaLevelOf(0, 350), 1);
    EXPECT_EQ(Client::Scenes::KalimaLevelOf(0, 999), 1);
    EXPECT_EQ(Client::Scenes::KalimaLevelOf(0, 1000), 7);
    for (const std::uint8_t characterClass : std::array<std::uint8_t, 4>{3, 4, 11, 12})
    {
        EXPECT_EQ(Client::Scenes::KalimaLevelOf(characterClass, 19), 7);
        EXPECT_EQ(Client::Scenes::KalimaLevelOf(characterClass, 20), 1);
    }
}

TEST(KalimaObjectStepTest, OwnRecordAndSeparateCellsCloseBeforeTheSelectorAndNativeAmbientCreation)
{
    bool reachedParticle = false;
    bool returned = false;
    for (std::uint32_t seed = 1; seed < 20; ++seed)
    {
        auto world = std::make_unique<Client::Scenes::WorldState>();
        auto& hero = world->Actors()[0];
        hero.cellX = 40;
        hero.cellY = 64;
        world->OwnCharacter().level = 0;
        Client::Render::Models::ClothWorld cloth{.random = Client::Render::Models::CrtRandom{seed}};
        auto expected = cloth.random;
        const int ambient = 0x1d4 + expected.Next() % 2;
        for (int cellCloses = 0; cellCloses < 8; ++cellCloses)
            static_cast<void>(expected.Next());
        const int region = 0x1d9 + expected.Next() % 2;
        for (int close = 0; close < 3848; ++close)
            static_cast<void>(expected.Next());
        static_cast<void>(expected.Next()); // gate de selección; count0 no consume otro.
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
        const auto result = Client::Scenes::StepKalimaObjectPrelude(*world, cloth);
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
            EXPECT_EQ(p.typedOwner.kind, Client::World::ParticleOwnerKind::Actor);
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
    auto world = std::make_unique<Client::Scenes::WorldState>();
    world->OwnCharacter().level = 40;
    world->Runtime().ownRecordDepth = 1;
    world->Runtime().objectSelectionCount = 3;
    bool selected = false;
    for (std::uint32_t seed = 1; seed < 200 && !selected; ++seed)
    {
        Client::Render::Models::ClothWorld cloth{.random = Client::Render::Models::CrtRandom{seed}, .logicSteps = 1};
        auto expected = cloth.random;
        if (expected.Next() % 10 != 0)
            continue;
        const int selector = expected.Next() % 3;
        const auto result = Client::Scenes::StepKalimaObjectPrelude(*world, cloth);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(result.Value(), selector);
        EXPECT_EQ(cloth.random.State(), expected.State());
        EXPECT_EQ(world->Runtime().objectSelectionCount, 3);
        EXPECT_EQ(world->Runtime().ownRecordDepth, 1);
        selected = true;
    }
    EXPECT_TRUE(selected);
}
