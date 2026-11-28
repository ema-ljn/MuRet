#include "WorldObjects/WorldObjects.h"

#include "Crt/CrtRandom.h"
#include "Data/Terrain/TerrainObjects.h"
#include "WorldObjects/IWorldObjectCreationServices.h"
#include "WorldObjects/WorldObjectRegistrations.h"

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
        Client::WorldObjects::WorldObjectRegistrations registrations;
        Client::Crt::CrtRandom random(1);
        const std::uint8_t state = 0;
        float factor = 0.02f;
        Client::WorldObjects::WorldObjectCreationContext context{
            test.map, Client::WorldObjects::ObjectScene::Other, registrations, nullptr, &random, &state, factor};
        const auto result = Client::WorldObjects::CreateWorldObjects(std::span(&record, 1), context);
        ASSERT_FALSE(result.failure.has_value());
        ASSERT_EQ(result.objects.size(), 1u);
        EXPECT_EQ(random.State(), 0x0029E2C0u);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(result.objects[0].field84), 0x3F51EB85u);
    }
}

TEST(WorldObjectCreationFactorTest, ReusingTheContextReadsTheChangedStorageWithoutReseedingTheCrt)
{
    const Client::Data::Terrain::TerrainObject record{.type = 27};
    Client::WorldObjects::WorldObjectRegistrations registrations;
    Client::Crt::CrtRandom random(1);
    float factor = 0.01f;
    Client::WorldObjects::WorldObjectCreationContext context{
        31, Client::WorldObjects::ObjectScene::Other, registrations, nullptr, &random, nullptr, factor};
    const auto first = Client::WorldObjects::CreateWorldObjects(std::span(&record, 1), context);
    ASSERT_FALSE(first.failure.has_value());
    ASSERT_EQ(first.objects.size(), 1u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(first.objects[0].field84), 0x3ED1EB85u);
    factor = 0.02f;
    const auto second = Client::WorldObjects::CreateWorldObjects(std::span(&record, 1), context);
    ASSERT_FALSE(second.failure.has_value());
    ASSERT_EQ(second.objects.size(), 1u);
    // Segundo rand 18467, resto 467: producto binary32 esperado 9.34, literal independiente del port.
    EXPECT_EQ(random.State(), 0xC823F683u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(second.objects[0].field84), 0x411570A4u);
}

class FactorChangingCastleServices final : public Client::WorldObjects::IWorldObjectCreationServices
{
public:
    FactorChangingCastleServices(Client::WorldObjects::WorldObjectCreationContext& context, float& factor)
        : m_context(context), m_factor(factor)
    {
    }

    bool CreateModelEffect(const Client::WorldObjects::WorldObjectModelEffectRequest& request,
                           Client::WorldObjects::WorldObject& source) noexcept override
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
    Client::WorldObjects::WorldObjectCreationContext& m_context;
    float& m_factor;
};

TEST(WorldObjectCreationFactorTest, Map31ReadsTheFactorChangedByTheEarlier309Emitter)
{
    const Client::Data::Terrain::TerrainObject record{.type = 79};
    Client::WorldObjects::WorldObjectRegistrations registrations;
    Client::Crt::CrtRandom random(1);
    float factor = 0.01f;
    Client::WorldObjects::WorldObjectCreationContext context{
        30, Client::WorldObjects::ObjectScene::Other, registrations, nullptr, &random, nullptr, factor};
    FactorChangingCastleServices services(context, factor);
    context.services = &services;
    const auto result = Client::WorldObjects::CreateWorldObjects(std::span(&record, 1), context);
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
    using Context = Client::WorldObjects::WorldObjectCreationContext;
    using Scene = Client::WorldObjects::ObjectScene;
    using Registry = Client::WorldObjects::WorldObjectRegistrations;
    using Services = Client::WorldObjects::IWorldObjectCreationServices;
    using Random = Client::Crt::CrtRandom;
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
    EXPECT_EQ(storage, &Client::WorldObjects::InitialWorldObjectRandomFactor);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(*storage), 0x3c23d70au);
}
TEST(WorldObjectCreationFactorTest, EmissionProviderContractRequiresNormalReturnWithoutExceptions)
{
    EXPECT_TRUE(noexcept(std::declval<Client::WorldObjects::IWorldObjectCreationServices&>().CreateModelEffect(
        std::declval<const Client::WorldObjects::WorldObjectModelEffectRequest&>(),
        std::declval<Client::WorldObjects::WorldObject&>())));
}
} // namespace
