#include "Scenes/WorldFaunaDrawing.h"

#include "Core/Expected.h"
#include "Render/ModelBatch.h"
#include "Scenes/ILeafDrawingServices.h"
#include "World/Leaves.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <system_error>
#include <vector>

namespace
{
using Client::Core::Expected;
using Client::Scenes::LeafBillboardRequest;
using Client::World::LeafBlend;

struct Context
{
    Client::World::Leaves leaves;
    Client::Render::ModelBatch batch;
    std::int32_t map = 0;
    std::int32_t selector = 0;
};

// Spy de servicios/estado, NO implementación productiva del renderer, binder32 o metadata621400.
class Services final : public Client::Scenes::ILeafDrawingServices
{
public:
    explicit Services(Context& context) : m_context(context) {}
    Expected<void, std::error_code> Blend(LeafBlend blend) override
    {
        events.push_back(1);
        observedBlend = blend;
        return {};
    }
    Expected<void, std::error_code> WhiteColor() override
    {
        events.push_back(2);
        currentColor = {1, 1, 1};
        return {};
    }
    Expected<void, std::error_code> BindTexture(std::int32_t selector) override
    {
        events.push_back(3);
        bound.push_back(selector);
        if (lateGenericType)
            Current().type = 102;
        if (flipToBillboard)
        {
            m_context.map = 2;
            Current().type = 102;
            Current().alive = 0;
        }
        if (failBind)
            return Client::Core::Unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
        return {};
    }
    Expected<void, std::error_code> PushMatrix() override
    {
        events.push_back(4);
        if (lateMapTwo)
        {
            m_context.map = 2;
            Current().position = {40, 50, 60};
        }
        return {};
    }
    Expected<void, std::error_code> Translate(float x, float y, float z) override
    {
        events.push_back(5);
        translation = {x, y, z};
        if (lateEvent)
            m_context.map = 18;
        return {};
    }
    Expected<void, std::error_code> Quad(Client::Render::ModelBatch& batch,
                                         const Client::World::LeafQuad& quad) override
    {
        events.push_back(6);
        EXPECT_EQ(&batch, &m_context.batch);
        EXPECT_EQ(quad.translation, translation);
        EXPECT_EQ(quad.corners[0], glm::vec3(-dimensions.x, -dimensions.x, -dimensions.y));
        EXPECT_EQ(quad.corners[1], glm::vec3(dimensions.x, dimensions.x, -dimensions.y));
        EXPECT_EQ(quad.corners[2], glm::vec3(dimensions.x, dimensions.x, dimensions.y));
        EXPECT_EQ(quad.corners[3], glm::vec3(-dimensions.x, -dimensions.x, dimensions.y));
        EXPECT_EQ(quad.uv[0], glm::vec2(0, 1));
        EXPECT_EQ(quad.uv[1], glm::vec2(1, 1));
        EXPECT_EQ(quad.uv[2], glm::vec2(1, 0));
        EXPECT_EQ(quad.uv[3], glm::vec2(0, 0));
        if (mixed)
        {
            EXPECT_EQ(currentColor, glm::vec3(0.25f, 0.5f, 0.75f));
        }
        if (failQuad)
            return Client::Core::Unexpected(std::make_error_code(std::errc::io_error));
        return {};
    }
    Expected<void, std::error_code> Billboard(Client::Render::ModelBatch& batch,
                                              const LeafBillboardRequest& request) override
    {
        events.push_back(8);
        EXPECT_EQ(&batch, &m_context.batch);
        EXPECT_EQ(&request.position, &Current().position);
        EXPECT_EQ(&request.light, &Current().light);
        EXPECT_EQ(request.selector, Current().type);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(request.width), std::bit_cast<std::uint32_t>(Current().scale));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(request.height), std::bit_cast<std::uint32_t>(request.width));
        EXPECT_EQ(request.rotation, 0);
        EXPECT_EQ(request.uv, glm::vec2(0));
        EXPECT_EQ(request.uvSize, glm::vec2(1));
        request.light.x = 9;
        if (mixed)
        {
            currentColor = {0.25f, 0.5f, 0.75f};
            m_context.map = 0;
        }
        return {};
    }
    Expected<void, std::error_code> PopMatrix() override
    {
        events.push_back(7);
        return {};
    }
    Client::World::Leaf& Current()
    {
        return m_context.leaves.All()[indices[bound.size() - 1]];
    }
    LeafBlend observedBlend = LeafBlend::Alpha;
    std::vector<int> events;
    std::vector<std::int32_t> bound;
    std::array<std::size_t, 2> indices{0, 1};
    glm::vec2 dimensions{3, 3};
    glm::vec3 translation{};
    glm::vec3 currentColor{};
    bool flipToBillboard = false;
    bool lateMapTwo = false;
    bool lateEvent = false;
    bool lateGenericType = false;
    bool failBind = false;
    bool failQuad = false;
    bool mixed = false;

private:
    Context& m_context;
};

Expected<void, std::error_code> Draw(Context& context, Client::Scenes::ILeafDrawingServices* services)
{
    return Client::Scenes::DrawWorldLeaves(context.batch, context.leaves, context.map, context.selector, services);
}

TEST(WorldLeavesDrawingTest, InitializesBlendAndWhiteEvenWithNoActiveSlots)
{
    for (const std::int32_t map : {2, 7, 10, 30, 55})
    {
        Context context;
        context.map = map;
        Services services(context);
        ASSERT_TRUE(Draw(context, &services).HasValue());
        EXPECT_EQ(services.observedBlend, LeafBlend::Additive);
        EXPECT_EQ(services.events, (std::vector<int>{1, 2}));
    }
    for (const std::int32_t map : {-1, 0, 18, 23, 34, 54, 258})
    {
        Context context;
        context.map = map;
        Services services(context);
        ASSERT_TRUE(Draw(context, &services).HasValue());
        EXPECT_EQ(services.observedBlend, LeafBlend::Alpha);
        EXPECT_EQ(services.events, (std::vector<int>{1, 2}));
    }
}

TEST(WorldLeavesDrawingTest, AlwaysVisitsSlot199AndUsesTheGenericDefaultQuad)
{
    Context context;
    auto& leaf = context.leaves.All()[199];
    leaf.alive = 0x80;
    leaf.type = 100;
    leaf.position = {10, 20, 30};
    Services services(context);
    services.indices[0] = 199;
    ASSERT_TRUE(Draw(context, &services).HasValue());
    EXPECT_EQ(services.bound, (std::vector<std::int32_t>{100}));
    EXPECT_EQ(services.events, (std::vector<int>{1, 2, 3, 4, 5, 6, 7}));
    EXPECT_EQ(services.translation, glm::vec3(10, 20, 30));
}

TEST(WorldLeavesDrawingTest, BinderSeesSigned32AndBillboardUsesLateTypeAndDuplicatedScaleBits)
{
    Context context;
    auto& leaf = context.leaves.All()[0];
    leaf.alive = 0xff;
    leaf.type = -3;
    leaf.scale = std::bit_cast<float>(0x7fc0ab12u);
    Services services(context);
    services.flipToBillboard = true;
    ASSERT_TRUE(Draw(context, &services).HasValue());
    EXPECT_EQ(services.bound, (std::vector<std::int32_t>{-3}));
    EXPECT_EQ(services.observedBlend, LeafBlend::Alpha);
    EXPECT_EQ(services.events, (std::vector<int>{1, 2, 3, 8}));
    EXPECT_EQ(leaf.alive, 0);
    EXPECT_EQ(leaf.light.x, 9);
}

TEST(WorldLeavesDrawingTest, GenericRouteDoesNotSwitchToBillboardWhenPushChangesTheLateMapToTwo)
{
    Context context;
    auto& leaf = context.leaves.All()[0];
    leaf.alive = 1;
    leaf.type = 102;
    Services services(context);
    services.lateMapTwo = true;
    services.dimensions = {1, 20};
    ASSERT_TRUE(Draw(context, &services).HasValue());
    EXPECT_EQ(services.events, (std::vector<int>{1, 2, 3, 4, 5, 6, 7}));
    EXPECT_EQ(services.translation, glm::vec3(40, 50, 60));
    Context eventContext;
    eventContext.leaves.All()[0].alive = 1;
    eventContext.leaves.All()[0].type = 0x635;
    eventContext.leaves.All()[0].scale = 2;
    eventContext.leaves.All()[0].spin = {1, 37, 0};
    Services eventServices(eventContext);
    eventServices.lateEvent = true;
    eventServices.dimensions = {1, 37};
    ASSERT_TRUE(Draw(eventContext, &eventServices).HasValue());
    EXPECT_EQ(eventServices.events, (std::vector<int>{1, 2, 3, 4, 5, 6, 7}));
    Context typeContext;
    typeContext.leaves.All()[0].alive = 1;
    typeContext.leaves.All()[0].type = 100;
    Services typeServices(typeContext);
    typeServices.lateGenericType = true;
    typeServices.dimensions = {1, 20};
    ASSERT_TRUE(Draw(typeContext, &typeServices).HasValue());
    EXPECT_EQ(typeServices.bound, (std::vector<std::int32_t>{100}));
}

TEST(WorldLeavesDrawingTest, SuppressedRainStillBindsPushesTranslatesAndPops)
{
    Context context;
    context.map = 34;
    context.selector = 2;
    context.leaves.All()[0].alive = 1;
    context.leaves.All()[0].type = 102;
    Services services(context);
    ASSERT_TRUE(Draw(context, &services).HasValue());
    EXPECT_EQ(services.events, (std::vector<int>{1, 2, 3, 4, 5, 7}));
}

TEST(WorldLeavesDrawingTest, EveryEventMapUsesStoredDimensionsBeforeTheType)
{
    for (const std::int32_t map : {18, 19, 20, 21, 22, 23})
    {
        for (const std::int32_t type : {-1, 102, 0x635})
        {
            Context context;
            context.map = map;
            auto& leaf = context.leaves.All()[0];
            leaf.alive = 1;
            leaf.type = type;
            leaf.spin = {1, 37, 0};
            Services services(context);
            services.dimensions = {1, 37};
            ASSERT_TRUE(Draw(context, &services).HasValue());
            EXPECT_EQ(services.events, (std::vector<int>{1, 2, 3, 4, 5, 6, 7}));
        }
    }
}

TEST(WorldLeavesDrawingTest, BillboardColorPersistsIntoTheNextGenericQuadWithoutAnotherWhite)
{
    Context context;
    context.map = 2;
    context.leaves.All()[0].alive = 1;
    context.leaves.All()[0].type = 100;
    context.leaves.All()[1].alive = 1;
    context.leaves.All()[1].type = 100;
    Services services(context);
    services.mixed = true;
    ASSERT_TRUE(Draw(context, &services).HasValue());
    EXPECT_EQ(services.events, (std::vector<int>{1, 2, 3, 8, 3, 4, 5, 6, 7}));
}

TEST(WorldLeavesDrawingTest, MissingOrFailedBackendErrorsWithoutInventedCleanup)
{
    Context context;
    context.leaves.All()[0].alive = 1;
    const auto missing = Draw(context, nullptr);
    ASSERT_FALSE(missing.HasValue());
    EXPECT_EQ(missing.Error(), std::make_error_code(std::errc::function_not_supported));
    Services services(context);
    services.failBind = true;
    const auto failed = Draw(context, &services);
    ASSERT_FALSE(failed.HasValue());
    EXPECT_EQ(failed.Error(), std::make_error_code(std::errc::no_such_file_or_directory));
    EXPECT_EQ(services.events, (std::vector<int>{1, 2, 3}));
    services.events.clear();
    services.bound.clear();
    services.failBind = false;
    services.failQuad = true;
    const auto failedQuad = Draw(context, &services);
    ASSERT_FALSE(failedQuad.HasValue());
    EXPECT_EQ(failedQuad.Error(), std::make_error_code(std::errc::io_error));
    EXPECT_EQ(services.events, (std::vector<int>{1, 2, 3, 4, 5, 6}));
}
} // namespace
