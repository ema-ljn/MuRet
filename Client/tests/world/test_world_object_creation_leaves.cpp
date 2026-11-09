#include "World/WorldObjects.h"

#include "Data/Terrain/TerrainObjects.h"
#include "Render/Models/Cloth.h"
#include "World/IWorldObjectCreationServices.h"
#include "World/WorldObjectRegistrations.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <span>

namespace
{
// Aísla las leafs: acepta sólo el request309 acreditado. No demuestra ModelEffects transitivo.
class CastleEffectBinding final : public Client::World::IWorldObjectCreationServices
{
public:
    bool CreateModelEffect(const Client::World::WorldObjectModelEffectRequest& request,
                           Client::World::WorldObject& source) noexcept override
    {
        return request.type == 309 && request.scale == 0.0f && request.owner == source.identity &&
               request.position == source.transform.position && &request.position == &source.transform.position;
    }
};

// Las dos listas son literales del contrato42b480, no una tabla derivada del port.
TEST(WorldObjectCreationLeavesTest, AllSignedTypesUseTheCorrectCastleGroupForEveryPredicateByte)
{
    constexpr std::array<std::int16_t, 20> GroupA{4,  11, 37, 41, 43, 44, 45, 46, 47, 48,
                                                  67, 68, 69, 71, 72, 73, 74, 75, 76, 88};
    constexpr std::array<std::int16_t, 16> GroupB{16, 38, 39, 40, 50, 58, 59, 78, 80, 81, 82, 83, 84, 85, 86, 87};
    CastleEffectBinding services;
    for (const std::uint8_t flag : std::array<std::uint8_t, 4>{0, 1, 2, 255})
    {
        for (int type = -32768; type <= 32767; ++type)
        {
            const auto signedType = static_cast<std::int16_t>(type);
            const Client::Data::Terrain::TerrainObject record{.type = signedType, .scale = 2.0f};
            Client::World::WorldObjectRegistrations registrations;
            Client::Render::Models::CrtRandom random(1);
            Client::World::WorldObjectCreationContext context{
                30, Client::World::ObjectScene::Other, registrations, &services, &random, &flag};
            const auto result = Client::World::CreateWorldObjects(std::span(&record, 1), context);
            ASSERT_FALSE(result.failure.has_value());
            ASSERT_EQ(result.objects.size(), 1u);
            std::int16_t expectedHidden = -1;
            if (std::find(GroupA.begin(), GroupA.end(), signedType) != GroupA.end())
            {
                expectedHidden = flag == 0 ? -2 : -1;
            }
            if (std::find(GroupB.begin(), GroupB.end(), signedType) != GroupB.end() || type == 415)
            {
                expectedHidden = flag == 0 ? -1 : -2;
            }
            EXPECT_EQ(result.objects[0].state.hiddenMesh, expectedHidden) << type << '/' << static_cast<int>(flag);
            EXPECT_EQ(result.objects[0].transform.scale, type == 19 || type == 415 ? 1.0f : 2.0f) << type;
            EXPECT_EQ(random.State(), type == 41 ? 0x0029E2C0u : 1u) << type;
            EXPECT_EQ(registrations.nextIdentity, 2u);
        }
    }
}

// D43ff70 lee el tipo sólo tras aceptar mapa31; el resto de la cadena no debe gastar RNG por él.
TEST(WorldObjectCreationLeavesTest, AllSignedTypesOnMap31ConsumeRandomOnlyFor27And54)
{
    for (int type = -32768; type <= 32767; ++type)
    {
        const Client::Data::Terrain::TerrainObject record{.type = static_cast<std::int16_t>(type)};
        Client::World::WorldObjectRegistrations registrations;
        Client::Render::Models::CrtRandom random(1);
        Client::World::WorldObjectCreationContext context{31, Client::World::ObjectScene::Other, registrations, nullptr,
                                                          &random};
        const auto result = Client::World::CreateWorldObjects(std::span(&record, 1), context);
        ASSERT_FALSE(result.failure.has_value());
        ASSERT_EQ(result.objects.size(), 1u);
        const bool draws = type == 27 || type == 54;
        EXPECT_EQ(random.State(), draws ? 0x0029E2C0u : 1u) << type;
        EXPECT_EQ(std::bit_cast<std::uint32_t>(result.objects[0].field84), draws ? 0x3ED1EB85u : 0u) << type;
    }
}

TEST(WorldObjectCreationLeavesTest, Map31PredicateDoesNotNarrowOrConfuseTheMapWithTheType)
{
    for (const std::int32_t map :
         std::array<std::int32_t, 10>{0, 30, 32, 54, 55, 287, -225, std::numeric_limits<std::int32_t>::min(),
                                      std::numeric_limits<std::int32_t>::max(), -1})
    {
        for (const std::int16_t type : std::array<std::int16_t, 2>{27, 54})
        {
            const Client::Data::Terrain::TerrainObject record{.type = type};
            Client::World::WorldObjectRegistrations registrations;
            Client::Render::Models::CrtRandom random(1);
            const std::uint8_t flag = 255;
            Client::World::WorldObjectCreationContext context{
                map, Client::World::ObjectScene::Other, registrations, nullptr, &random, &flag};
            const auto result = Client::World::CreateWorldObjects(std::span(&record, 1), context);
            ASSERT_FALSE(result.failure.has_value());
            ASSERT_EQ(result.objects.size(), 1u);
            EXPECT_EQ(random.State(), 1u) << map << '/' << type;
            EXPECT_EQ(result.objects[0].field84, 0.0f);
        }
    }
}

TEST(WorldObjectCreationLeavesTest, MissingLiveStateNeverBecomesAnInventedZero)
{
    const std::array records{Client::Data::Terrain::TerrainObject{.type = 4},
                             Client::Data::Terrain::TerrainObject{.type = 4}};
    Client::World::WorldObjectRegistrations registrations;
    Client::World::WorldObjectCreationContext context{30, Client::World::ObjectScene::Other, registrations};
    const auto result = Client::World::CreateWorldObjects(records, context);
    ASSERT_TRUE(result.failure.has_value());
    EXPECT_EQ(result.failure->error, Client::World::WorldObjectCreationError::UnsupportedMapHandler);
    EXPECT_EQ(result.failure->recordIndex, 0u);
    ASSERT_EQ(result.objects.size(), 1u);
    EXPECT_EQ(result.objects[0].state.hiddenMesh, -1);
    EXPECT_EQ(registrations.nextIdentity, 2u);
}

TEST(WorldObjectCreationLeavesTest, ReusingTheContextReadsTheCurrentPredicateByteEachTime)
{
    const Client::Data::Terrain::TerrainObject record{.type = 4};
    Client::World::WorldObjectRegistrations registrations;
    std::uint8_t flag = 0;
    Client::World::WorldObjectCreationContext context{
        30, Client::World::ObjectScene::Other, registrations, nullptr, nullptr, &flag};
    for (const std::uint8_t next : std::array<std::uint8_t, 5>{0, 255, 2, 1, 0})
    {
        flag = next;
        const auto result = Client::World::CreateWorldObjects(std::span(&record, 1), context);
        ASSERT_FALSE(result.failure.has_value());
        ASSERT_EQ(result.objects.size(), 1u);
        EXPECT_EQ(result.objects[0].state.hiddenMesh, next == 0 ? -2 : -1);
        EXPECT_EQ(flag, next);
    }
}

struct CastleTransition
{
    std::int16_t type;
    std::int32_t map;
    std::uint8_t state;
};

class CastleTransitionServices final : public Client::World::IWorldObjectCreationServices
{
public:
    CastleTransitionServices(Client::World::WorldObjectCreationContext& context, std::uint8_t& state,
                             CastleTransition transition)
        : m_context(context), m_state(state), m_transition(transition)
    {
    }

    bool CreateModelEffect(const Client::World::WorldObjectModelEffectRequest& request,
                           Client::World::WorldObject& source) noexcept override
    {
        if (request.type != 309)
        {
            return false;
        }
        source.type = m_transition.type;
        m_context.map = m_transition.map;
        m_state = m_transition.state;
        return true;
    }

private:
    Client::World::WorldObjectCreationContext& m_context;
    std::uint8_t& m_state;
    CastleTransition m_transition;
};

TEST(WorldObjectCreationLeavesTest, CastleFinalLeafReadsTheTypeAndPredicateAfterTheEffect)
{
    const Client::Data::Terrain::TerrainObject record{.type = 79};
    Client::World::WorldObjectRegistrations registrations;
    std::uint8_t flag = 255;
    Client::World::WorldObjectCreationContext context{
        30, Client::World::ObjectScene::Other, registrations, nullptr, nullptr, &flag};
    CastleTransitionServices services(context, flag, {4, 30, 0});
    context.services = &services;
    const auto result = Client::World::CreateWorldObjects(std::span(&record, 1), context);
    ASSERT_FALSE(result.failure.has_value());
    ASSERT_EQ(result.objects.size(), 1u);
    EXPECT_EQ(result.objects[0].type, 4);
    EXPECT_EQ(result.objects[0].state.hiddenMesh, -2);
    EXPECT_EQ(flag, 0);
}

TEST(WorldObjectCreationLeavesTest, LaterMap31HandlerReadsTheMapAndTypeChangedByTheCastleEffect)
{
    const Client::Data::Terrain::TerrainObject record{.type = 79};
    Client::World::WorldObjectRegistrations registrations;
    Client::Render::Models::CrtRandom random(1);
    std::uint8_t flag = 255;
    Client::World::WorldObjectCreationContext context{
        30, Client::World::ObjectScene::Other, registrations, nullptr, &random, &flag};
    CastleTransitionServices services(context, flag, {27, 31, 2});
    context.services = &services;
    const auto result = Client::World::CreateWorldObjects(std::span(&record, 1), context);
    ASSERT_FALSE(result.failure.has_value());
    ASSERT_EQ(result.objects.size(), 1u);
    EXPECT_EQ(result.objects[0].type, 27);
    EXPECT_EQ(context.map, 31);
    EXPECT_EQ(random.State(), 0x0029E2C0u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(result.objects[0].field84), 0x3ED1EB85u);
}
} // namespace
