#include "Crt/CrtRandom.h"
#include "Scenes/CastleSiegeAttack.h"
#include "Core/Expected.h"
#include "Kernel/ActorState.h"
#include "World/ModelEffects.h"
#include "World/SiegeVertexEffects.h"
#include <gtest/gtest.h>
#include <glm/ext/vector_float3.hpp>
#include <cstddef>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
namespace
{
TEST(CastleSiegeAttackTest, TheClosedGateEmitsItsWaveBeforeFiveRandomizedFragments)
{
    Client::Scenes::ActorState target;
    target.type = 0x19b;
    target.position = {100, 200, 300};
    Client::Crt::CrtRandom random(1);
    std::vector<Client::World::ModelEffectArguments> emissions;
    ASSERT_TRUE(
        Client::Scenes::EmitTheCastleAttackHit(target, 0, random,
                                               [&](const auto& args) -> Client::Core::Expected<void, std::string>
                                               {
                                                   emissions.push_back(args);
                                                   return {};
                                               })
            .HasValue());
    ASSERT_EQ(emissions.size(), 6u);
    EXPECT_EQ(emissions[0].type, 0x114);
    EXPECT_EQ(emissions[0].subtype, 6);
    EXPECT_EQ(emissions[0].key, 0);
    EXPECT_EQ(emissions[0].position, (glm::vec3{160, 100, 450}));
    for (std::size_t index = 1; index < emissions.size(); ++index)
    {
        EXPECT_GE(emissions[index].type, 0x131);
        EXPECT_LE(emissions[index].type, 0x133);
        EXPECT_GE(emissions[index].subtype, 1);
        EXPECT_LE(emissions[index].subtype, 2);
        EXPECT_GE(emissions[index].position.z, 500);
        EXPECT_LT(emissions[index].position.z, 530);
    }
    Client::Crt::CrtRandom expected(1);
    expected.Discard(15);
    EXPECT_EQ(random.State(), expected.State());
}
TEST(CastleSiegeAttackTest, HeroFlagsSuppressOnlyTheWaveAndAnOpenGateSuppressesEverything)
{
    Client::Scenes::ActorState target;
    target.type = 0x19b;
    Client::Crt::CrtRandom random(1);
    int calls = 0;
    const Client::Scenes::CastleAttackEmitter emit = [&](const auto& args) -> Client::Core::Expected<void, std::string>
    {
        EXPECT_NE(args.type, 0x114);
        ++calls;
        return {};
    };
    ASSERT_TRUE(Client::Scenes::EmitTheCastleAttackHit(target, 0x8000, random, emit).HasValue());
    EXPECT_EQ(calls, 5);
    target.flags78 = 0x20000;
    const auto before = random.State();
    ASSERT_TRUE(Client::Scenes::EmitTheCastleAttackHit(target, 0, random, emit).HasValue());
    EXPECT_EQ(calls, 5);
    EXPECT_EQ(random.State(), before);
}
TEST(CastleSiegeAttackTest, EachStatueAttemptUsesItsGateBeforeTheHeightAndNestedConstructor)
{
    Client::Scenes::ActorState target;
    target.type = 0x19c;
    target.position.z = 300;
    Client::Crt::CrtRandom random(1), expected(1);
    int calls = 0;
    for (int attempt = 0; attempt < 5; ++attempt)
    {
        if ((expected.Next() & 1) != 0)
            continue;
        static_cast<void>(expected.Next());
        expected.Discard(8);
    }
    ASSERT_TRUE(
        Client::Scenes::EmitTheCastleAttackHit(target, 0, random,
                                               [&](const auto& args) -> Client::Core::Expected<void, std::string>
                                               {
                                                   EXPECT_EQ(args.type, 0x10b);
                                                   EXPECT_GE(args.position.z, 500);
                                                   ++calls;
                                                   random.Discard(8);
                                                   return {};
                                               })
            .HasValue());
    EXPECT_GT(calls, 0);
    EXPECT_EQ(random.State(), expected.State());
}
} // namespace

TEST(CastleSiegeAttackTest, RealConstructorsKeepTheirRandomDrawsBetweenTheFiveCallerGroups)
{
    Client::Scenes::ActorState target;
    target.type = 0x19b;
    auto effects = std::make_unique<Client::World::ModelEffects>();
    Client::Crt::CrtRandom random(1);
    ASSERT_TRUE(Client::Scenes::EmitTheCastleAttackHit(
                    target, 0, random,
                    [&](const auto& args) -> Client::Core::Expected<void, std::string>
                    {
                        if (args.type == 0x114)
                            static_cast<void>(effects->CreateWaveEffect(args));
                        else
                            static_cast<void>(Client::World::CreateSiegeVertexEffect(args, *effects, random));
                        return {};
                    })
                    .HasValue());
    EXPECT_EQ(std::ranges::count_if(effects->Effects(), [](const auto& effect) { return effect.alive; }), 6);
    EXPECT_EQ(effects->Effects()[0].type, 0x114);
    EXPECT_EQ(effects->Effects()[0].life, 10);
    EXPECT_EQ(effects->Effects()[0].transform.scale, 0);
    Client::Crt::CrtRandom expected(1);
    expected.Discard(55);
    EXPECT_EQ(random.State(), expected.State());
    for (auto& effect : effects->Effects())
        effect.alive = true;
    const auto before = random.State();
    ASSERT_TRUE(Client::Scenes::EmitTheCastleAttackHit(
                    target, 0, random,
                    [&](const auto& args) -> Client::Core::Expected<void, std::string>
                    {
                        if (args.type == 0x114)
                            static_cast<void>(effects->CreateWaveEffect(args));
                        else
                            static_cast<void>(Client::World::CreateSiegeVertexEffect(args, *effects, random));
                        return {};
                    })
                    .HasValue());
    Client::Crt::CrtRandom fullExpected(before);
    fullExpected.Discard(15);
    EXPECT_EQ(random.State(), fullExpected.State());
}
