#include "World/WorldObjects.h"

#include "Data/Terrain/TerrainObjects.h"
#include "Render/Models/Cloth.h"
#include "World/IWorldObjectCreationServices.h"
#include "World/WorldObjectRegistrations.h"

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstdint>
#include <span>
#include <functional>
#include <type_traits>
#include <utility>

namespace
{
// 41 * binary32(0.02) redondeado una vez a float: bits 0x3f51eb85, no 0x3ed1eb85 del factor inicial.
TEST(WorldObjectCreationFactorTest, CastleAndMap31UseTheBorrowedFactorForANonzeroRemainder)
{
    struct Case
    {
        std::int32_t map;
        std::int16_t type;
    };
    for (const auto& test : std::array{Case{30, 41}, Case{31, 27}, Case{31, 54}})
    {
        SCOPED_TRACE(test.map);
        SCOPED_TRACE(test.type);
        const Client::Data::Terrain::TerrainObject record{.type = test.type};
        Client::World::WorldObjectRegistrations registrations;
        Client::Render::Models::CrtRandom random(1);
        const std::uint8_t state = 0;
        float factor = 0.02f;
        Client::World::WorldObjectCreationContext context{
            test.map, Client::World::ObjectScene::Other, registrations, nullptr, &random, &state, factor};
        const auto result = Client::World::CreateWorldObjects(std::span(&record, 1), context);
        ASSERT_FALSE(result.failure.has_value());
        ASSERT_EQ(result.objects.size(), 1u);
        EXPECT_EQ(random.State(), 0x0029E2C0u);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(result.objects[0].field84), 0x3F51EB85u);
    }
}

TEST(WorldObjectCreationFactorTest, ReusingTheContextReadsTheChangedStorageWithoutReseedingTheCrt)
{
    const Client::Data::Terrain::TerrainObject record{.type = 27};
    Client::World::WorldObjectRegistrations registrations;
    Client::Render::Models::CrtRandom random(1);
    float factor = 0.01f;
    Client::World::WorldObjectCreationContext context{
        31, Client::World::ObjectScene::Other, registrations, nullptr, &random, nullptr, factor};
    const auto first = Client::World::CreateWorldObjects(std::span(&record, 1), context);
    ASSERT_FALSE(first.failure.has_value());
    ASSERT_EQ(first.objects.size(), 1u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(first.objects[0].field84), 0x3ED1EB85u);
    factor = 0.02f;
    const auto second = Client::World::CreateWorldObjects(std::span(&record, 1), context);
    ASSERT_FALSE(second.failure.has_value());
    ASSERT_EQ(second.objects.size(), 1u);
    // Segundo rand 18467, resto 467: producto binary32 esperado 9.34, literal independiente del port.
    EXPECT_EQ(random.State(), 0xC823F683u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(second.objects[0].field84), 0x411570A4u);
}

class FactorChangingCastleServices final : public Client::World::IWorldObjectCreationServices
{
public:
    FactorChangingCastleServices(Client::World::WorldObjectCreationContext& context, float& factor)
        : m_context(context), m_factor(factor)
    {
    }

    bool CreateModelEffect(const Client::World::WorldObjectModelEffectRequest& request,
                           Client::World::WorldObject& source) noexcept override
    {
        if (request.type != 309 || request.owner != source.identity)
        {
            return false;
        }
        m_factor = 0.02f;
        m_context.map = 31;
        source.type = 27;
        return true;
    }

private:
    Client::World::WorldObjectCreationContext& m_context;
    float& m_factor;
};

TEST(WorldObjectCreationFactorTest, Map31ReadsTheFactorChangedByTheEarlier309Emitter)
{
    const Client::Data::Terrain::TerrainObject record{.type = 79};
    Client::World::WorldObjectRegistrations registrations;
    Client::Render::Models::CrtRandom random(1);
    float factor = 0.01f;
    Client::World::WorldObjectCreationContext context{
        30, Client::World::ObjectScene::Other, registrations, nullptr, &random, nullptr, factor};
    FactorChangingCastleServices services(context, factor);
    context.services = &services;
    const auto result = Client::World::CreateWorldObjects(std::span(&record, 1), context);
    ASSERT_FALSE(result.failure.has_value());
    ASSERT_EQ(result.objects.size(), 1u);
    EXPECT_EQ(context.map, 31);
    EXPECT_EQ(result.objects[0].type, 27);
    EXPECT_EQ(factor, 0.02f);
    EXPECT_EQ(random.State(), 0x0029E2C0u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(result.objects[0].field84), 0x3F51EB85u);
}

TEST(WorldObjectCreationFactorTest, BorrowedFactorAcceptsLvaluesRejectsRvaluesAndKeepsDurableDefault)
{
    using Context = Client::World::WorldObjectCreationContext;
    using Scene = Client::World::ObjectScene;
    using Registry = Client::World::WorldObjectRegistrations;
    using Services = Client::World::IWorldObjectCreationServices;
    using Random = Client::Render::Models::CrtRandom;
    EXPECT_TRUE((std::is_constructible_v<Context, std::int32_t, Scene, Registry&, Services*, Random*,
                                         const std::uint8_t*, float&>));
    EXPECT_TRUE((std::is_constructible_v<Context, std::int32_t, Scene, Registry&, Services*, Random*,
                                         const std::uint8_t*, const float&>));
    EXPECT_FALSE((std::is_constructible_v<Context, std::int32_t, Scene, Registry&, Services*, Random*,
                                          const std::uint8_t*, float&&>));
    EXPECT_FALSE((std::is_constructible_v<Context, std::int32_t, Scene, Registry&, Services*, Random*,
                                          const std::uint8_t*, const float&&>));
    EXPECT_FALSE((std::is_constructible_v<Context, std::int32_t, Scene, Registry&, Services*, Random*,
                                          const std::uint8_t*, int&>));
    Registry registry;
    Context context{31, Scene::Other, registry};
    EXPECT_TRUE((
        std::is_same_v<std::remove_cvref_t<decltype(context.randomPhaseFactor)>, std::reference_wrapper<const float>>));
    EXPECT_FALSE((std::is_assignable_v<decltype((context.randomPhaseFactor)), std::reference_wrapper<const float>>));
    const auto storage = [](const auto& borrowed) -> const float*
    {
        if constexpr (requires { borrowed.randomPhaseFactor.get(); })
            return &borrowed.randomPhaseFactor.get();
        else
            return &borrowed.randomPhaseFactor;
    }(context);
    EXPECT_EQ(storage, &Client::World::InitialWorldObjectRandomFactor);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(*storage), 0x3c23d70au);
}
TEST(WorldObjectCreationFactorTest, EmissionProviderContractRequiresNormalReturnWithoutExceptions)
{
    EXPECT_TRUE(noexcept(std::declval<Client::World::IWorldObjectCreationServices&>().CreateModelEffect(
        std::declval<const Client::World::WorldObjectModelEffectRequest&>(),
        std::declval<Client::World::WorldObject&>())));
}
} // namespace
