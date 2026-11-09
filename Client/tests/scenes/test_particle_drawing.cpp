#include "World/ParticleDrawing.h"
#include "Scenes/ParticleDrawServices.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/TextureMetadata.h"
#include "Render/IParticleDrawBackend.h"
#include "Render/ParticleBillboardRequest.h"
#include "Render/Billboard.h"
#include "Render/SpriteVertex.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/TextureSlot.h"
#include "Render/Models/Skeleton.h"
#include "Data/Models/Model.h"
#include "Data/Textures/Texture.h"
#include "Core/Expected.h"
#include "World/Particles.h"
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <vector>

namespace
{
using Client::Core::Unexpected;
using Client::Render::Color;
using Client::World::DrawParticlesChecked;
using Client::World::Particle;
using Client::World::ParticleDrawingPhase;
using Client::World::ParticleError;
using Request = Client::Render::ParticleBillboardRequest;
using Matrix = Client::Render::Models::OriginalMatrix;
constexpr Matrix Identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
static_assert(!std::is_default_constructible_v<Request>);
static_assert(!std::is_default_constructible_v<Client::Scenes::ParticleDrawServices>);
static_assert(!std::is_constructible_v<Client::Scenes::ParticleDrawServices, const Client::Scenes::IModelLibrary&&,
                                       std::reference_wrapper<Client::Render::IParticleDrawBackend>,
                                       std::reference_wrapper<const Matrix>>);
static_assert(!std::is_constructible_v<Client::Scenes::ParticleDrawServices,
                                       std::reference_wrapper<const Client::Scenes::IModelLibrary>,
                                       Client::Render::IParticleDrawBackend&&, std::reference_wrapper<const Matrix>>);

static_assert(!std::is_constructible_v<std::reference_wrapper<const glm::vec3>, glm::vec3&&>);
static_assert(!std::is_constructible_v<std::reference_wrapper<const Matrix>, Matrix&&>);

struct Library final : Client::Scenes::IModelLibrary
{
    explicit Library(std::vector<std::string>& log) : events(log) {}
    std::vector<std::string>& events;
    std::map<std::uint32_t, Client::Scenes::TextureMetadata> rows;
    void Put(std::int32_t selector, glm::vec2 size, std::uint8_t flag)
    {
        Client::Scenes::TextureMetadata value(Client::Data::Textures::Texture{});
        value.size = size;
        value.components = flag;
        rows.insert_or_assign(static_cast<std::uint32_t>(selector), value);
    }
    const Client::Data::Models::Model* Find(std::uint16_t) const override
    {
        return nullptr;
    }
    const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t) const override
    {
        return nullptr;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return {};
    }
    std::optional<Client::Scenes::TextureMetadata> TextureMetadataOf(std::uint32_t selector) const override
    {
        events.push_back("metadata:" + std::to_string(selector));
        const auto found = rows.find(selector);
        return found == rows.end() ? std::nullopt : std::optional(found->second);
    }
};
struct Backend final : Client::Render::IParticleDrawBackend
{
    explicit Backend(std::vector<std::string>& log) : events(log) {}
    std::vector<std::string>& events;
    std::function<void(std::string_view)> hook;
    std::string fail;
    std::int32_t selector = 0;
    std::vector<std::int32_t> selectors;
    Color color;
    glm::vec2 currentUv{};
    std::vector<Client::Render::BillboardCorner> corners;
    std::size_t completed = 0;
    Result Record(std::string_view name)
    {
        events.emplace_back(name);
        if (hook)
            hook(name);
        if (name == fail)
            return Unexpected(std::make_error_code(std::errc::io_error));
        return {};
    }
    Result Additive() override
    {
        return Record("additive");
    }
    Result AlphaInheritDepth() override
    {
        return Record("alpha0");
    }
    Result SubtractivePrefix() override
    {
        return Record("sub-prefix");
    }
    Result Helper6208f0() override
    {
        return Record("helper-f0");
    }
    Result Helper6208b0() override
    {
        return Record("helper-b0");
    }
    Result SubtractiveTail() override
    {
        return Record("sub-tail");
    }
    Result Type620850() override
    {
        return Record("type-850");
    }
    Result Type620870() override
    {
        return Record("type-870");
    }
    Result BindTexture(std::int32_t value) override
    {
        selector = value;
        selectors.push_back(value);
        return Record("bind");
    }
    Result Begin() override
    {
        return Record("begin");
    }
    Result Color3(const glm::vec3& light) override
    {
        auto result = Record("color3");
        color = {light.x, light.y, light.z, 1};
        return result;
    }
    Result Color4(const Color& value) override
    {
        auto result = Record("color4");
        color = value;
        return result;
    }
    Result TexCoord(glm::vec2 uv) override
    {
        auto result = Record("uv");
        currentUv = uv;
        return result;
    }
    Result Vertex(const glm::vec3& position) override
    {
        auto result = Record("vertex");
        corners.push_back({position, currentUv});
        return result;
    }
    Result End() override
    {
        auto result = Record("end");
        if (result.HasValue())
            ++completed;
        return result;
    }
};
class ParticleDrawingTest : public ::testing::Test
{
protected:
    std::vector<std::string> m_events;
    Library m_library{m_events};
    Backend m_backend{m_events};
    Matrix m_view = Identity;
    float m_clock = 100;
    Client::World::Particles m_particles;
    Client::Scenes::ParticleDrawServices m_services{std::cref(m_library), std::ref(m_backend), std::cref(m_view)};
    void Seed(std::size_t index = 0, std::int32_t type = 0x67, std::int32_t subtype = 0)
    {
        m_particles.All()[index] =
            Particle{.alive = true, .type = type, .texture = 17, .subtype = subtype, .scale = 1, .light = {2, 3, 4}};
        m_library.Put(17, {8, 4}, std::uint8_t{4});
        m_library.Put(type, {99, 77}, std::uint8_t{3});
    }
    auto Draw(std::uint8_t mode = 0)
    {
        return DrawParticlesChecked(m_particles, mode, std::cref(m_clock), m_services);
    }
};
TEST_F(ParticleDrawingTest, PublicUnsignedMetadataKeepsHighBitSelectorsDistinctFromSlot16)
{
    using MetadataQuery =
        std::optional<Client::Scenes::TextureMetadata> (Client::Scenes::IModelLibrary::*)(std::uint32_t) const;
    EXPECT_TRUE((std::is_same_v<decltype(&Client::Scenes::IModelLibrary::TextureMetadataOf), MetadataQuery>));
    m_library.Put(-1, {8, 16}, std::uint8_t{3});
    m_library.Put(std::numeric_limits<std::int32_t>::min(), {32, 64}, std::uint8_t{255});
    m_library.Put(0xffff, {1, 2}, std::uint8_t{1});
    m_library.Put(0, {3, 4}, std::uint8_t{2});

    const auto dimensions = m_services.Dimensions(-1);
    ASSERT_TRUE(dimensions.HasValue());
    EXPECT_EQ(dimensions.Value(), glm::vec2(8, 16));
    const auto material = m_services.MaterialFlag(std::numeric_limits<std::int32_t>::min());
    ASSERT_TRUE(material.HasValue());
    EXPECT_EQ(material.Value(), 255);
    EXPECT_EQ(m_events, (std::vector<std::string>{"metadata:4294967295", "metadata:2147483648"}));
    EXPECT_TRUE(m_backend.selectors.empty());
    EXPECT_EQ(m_backend.completed, 0u);
}
TEST_F(ParticleDrawingTest, IneligibleSlotsPerformNoLookupOrService)
{
    Seed();
    m_particles.All()[0].position.z = 351;
    ASSERT_TRUE(Draw(std::uint8_t{1}).HasValue());
    EXPECT_TRUE(m_events.empty());
    m_particles.All()[0].position.z = std::numeric_limits<float>::quiet_NaN();
    ASSERT_TRUE(Draw(std::uint8_t{2}).HasValue());
    EXPECT_TRUE(m_events.empty());
    m_particles.All()[0].alive = false;
    ASSERT_TRUE(Draw(std::uint8_t{255}).HasValue());
    EXPECT_TRUE(m_events.empty());
}
TEST_F(ParticleDrawingTest, MetadataStagesAndStoredDimensionsSurviveMaterialMutations)
{
    Seed(0, 0x5d9);
    auto& p = m_particles.All()[0];
    p.scale = 1.5f;
    m_library.Put(17, {128, 64}, std::uint8_t{4});
    m_library.Put(42, {1, 1}, std::uint8_t{0});
    m_backend.hook = [&](std::string_view phase)
    {
        if (phase == "additive")
        {
            p.texture = 42;
            p.scale = 9;
            p.alive = false;
            p.position.z = 999;
        }
    };
    ASSERT_TRUE(Draw(std::uint8_t{1}).HasValue());
    ASSERT_EQ(m_events.size(), 16u);
    EXPECT_EQ((std::vector<std::string>(m_events.begin(), m_events.begin() + 6)),
              (std::vector<std::string>{"metadata:17", "metadata:1497", "additive", "bind", "begin", "metadata:42"}));
    ASSERT_EQ(m_backend.corners.size(), 4u);
    EXPECT_EQ(m_backend.corners[0].position, glm::vec3(-24, -48, 999));
    EXPECT_EQ(m_backend.corners[2].position, glm::vec3(24, 48, 999));
    EXPECT_EQ(m_backend.color, (Color{2, 3, 4, 2}));
    EXPECT_EQ(m_backend.selector, 42);
}
TEST_F(ParticleDrawingTest, AliasedPositionAndColorAreConsumedAfterBinderAndBeginWithoutRecomputingGeometry)
{
    glm::vec3 shared{1, 2, 3};
    m_library.Put(0x105e4, {1, 1}, std::uint8_t{4});
    m_backend.hook = [&](std::string_view phase)
    {
        if (phase == "bind")
        {
            shared = {20, 30, 40};
            m_view[3] = 5;
        }
        if (phase == "begin")
            shared = {50, 60, 70};
    };
    const Request request{0x105e4, std::cref(shared), 2, 2, std::cref(shared), 0, {0.25f, 0.5f}, {0.125f, 0.25f}};
    ASSERT_TRUE(m_services.Billboard(request).HasValue());
    ASSERT_EQ(m_backend.corners.size(), 4u);
    EXPECT_EQ(m_backend.corners[0].position, glm::vec3(24, 29, 40));
    EXPECT_EQ(m_backend.corners[2].position, glm::vec3(26, 31, 40));
    EXPECT_EQ(m_backend.corners[0].uv, glm::vec2(0.25f, 0.75f));
    EXPECT_EQ(m_backend.currentUv, glm::vec2(0.25f, 0.5f));
    EXPECT_EQ(m_backend.color, (Color{50, 60, 70, 50}));
}
TEST_F(ParticleDrawingTest, MissingLateColorKeepsBeginAndStopsBeforeColorVerticesEndOrNextSlot)
{
    Seed();
    Seed(1);
    m_backend.hook = [&](std::string_view phase)
    {
        if (phase == "begin")
            m_library.rows.erase(17);
    };
    const auto result = Draw();
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error().slot, 0u);
    EXPECT_EQ(result.Error().error.reason, ParticleError::MissingColorFlag);
    EXPECT_EQ(result.Error().error.phase, ParticleDrawingPhase::ColorFlag);
    EXPECT_EQ(m_events,
              (std::vector<std::string>{"metadata:17", "metadata:103", "additive", "bind", "begin", "metadata:17"}));
    EXPECT_TRUE(m_backend.corners.empty());
    EXPECT_EQ(m_backend.completed, 0u);
}
TEST_F(ParticleDrawingTest, SmokeRechecksSubtypeAfterBothOpaqueSubtractiveServices)
{
    Seed(0, 0x5f2, 2);
    auto& p = m_particles.All()[0];
    p.rotation = 90;
    unsigned tails = 0;
    m_backend.hook = [&](std::string_view phase)
    {
        if (phase == "sub-tail")
        {
            p.subtype = ++tails == 1 ? 37 : 6;
            p.type = 0x5d9;
        }
    };
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(tails, 2u);
    EXPECT_EQ(m_backend.corners[0].position, glm::vec3(-4, -2, 0));
    EXPECT_EQ(std::count(m_events.begin(), m_events.end(), "helper-f0"), 2);
    EXPECT_EQ(std::count(m_events.begin(), m_events.end(), "helper-b0"), 2);
}
TEST_F(ParticleDrawingTest, BothSpecialTypeGuardsReadTheStateLeftByThePreviousService)
{
    Seed();
    auto& p = m_particles.All()[0];
    m_backend.hook = [&](std::string_view phase)
    {
        if (phase == "additive")
        {
            p.type = 0x5ac;
            p.subtype = 6;
        }
        if (phase == "type-850")
        {
            p.type = 0x5ed;
            p.subtype = 5;
        }
        if (phase == "type-870")
        {
            p.type = 0x5d9;
            p.subtype = 0;
        }
    };
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(m_events[3], "type-850");
    EXPECT_EQ(m_events[4], "type-870");
    EXPECT_EQ(m_backend.corners[0].position.x, -1);
}
TEST_F(ParticleDrawingTest, EachBackendFailureRetainsItsPrefixAndStopsThePass)
{
    struct Case
    {
        const char* event;
        ParticleDrawingPhase phase;
    };
    const std::array cases{Case{"additive", ParticleDrawingPhase::Material},
                           Case{"sub-prefix", ParticleDrawingPhase::SubtractivePrefix},
                           Case{"helper-f0", ParticleDrawingPhase::Helper6208f0},
                           Case{"helper-b0", ParticleDrawingPhase::Helper6208b0},
                           Case{"sub-tail", ParticleDrawingPhase::SubtractiveTail},
                           Case{"bind", ParticleDrawingPhase::Bind},
                           Case{"begin", ParticleDrawingPhase::Begin},
                           Case{"color4", ParticleDrawingPhase::Color},
                           Case{"uv", ParticleDrawingPhase::TexCoord},
                           Case{"vertex", ParticleDrawingPhase::Vertex},
                           Case{"end", ParticleDrawingPhase::End}};
    for (const auto& row : cases)
    {
        SCOPED_TRACE(row.event);
        m_particles.Clear();
        m_events.clear();
        m_backend.corners.clear();
        m_backend.selectors.clear();
        Seed(7, 0x5f2, 2);
        Seed(11);
        m_backend.fail = row.event;
        const auto result = Draw();
        ASSERT_FALSE(result.HasValue());
        EXPECT_EQ(result.Error().slot, 7u);
        EXPECT_EQ(result.Error().error.phase, row.phase);
        EXPECT_EQ(result.Error().error.reason, ParticleError::MissingDrawingService);
        EXPECT_EQ(result.Error().error.backendError, std::make_error_code(std::errc::io_error));
        ASSERT_FALSE(m_events.empty());
        EXPECT_EQ(m_events.back(), row.event);
        EXPECT_EQ(std::count(m_events.begin(), m_events.end(), "metadata:17"),
                  row.phase >= ParticleDrawingPhase::Color ? 2 : 1);
        EXPECT_EQ(m_backend.completed, 0u);
        EXPECT_LE(m_backend.selectors.size(), 1u);
    }
}
TEST_F(ParticleDrawingTest, MissingInitialMetadataAndUnknownBranchesFailAtTheirOwnFrontier)
{
    Seed(13);
    m_library.rows.erase(17);
    auto result = Draw();
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error().slot, 13u);
    EXPECT_EQ(result.Error().error.reason, ParticleError::MissingTextureSize);
    EXPECT_EQ(result.Error().error.phase, ParticleDrawingPhase::Dimensions);
    EXPECT_EQ(m_events, (std::vector<std::string>{"metadata:17"}));
    m_events.clear();
    m_library.Put(17, {8, 4}, std::uint8_t{4});
    m_library.rows.erase(0x67);
    result = Draw();
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error().error.reason, ParticleError::MissingMaterialFlag);
    EXPECT_EQ(result.Error().error.phase, ParticleDrawingPhase::MaterialFlag);
    EXPECT_EQ(m_events, (std::vector<std::string>{"metadata:17", "metadata:103"}));
    m_events.clear();
    Seed(13, 0x123456);
    result = Draw();
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error().error.reason, ParticleError::UnsupportedType);
    EXPECT_EQ(result.Error().error.phase, ParticleDrawingPhase::Branch);
    EXPECT_EQ(m_events.back(), "additive");
    EXPECT_TRUE(m_backend.selectors.empty());
    m_events.clear();
    Seed(13, 0x5d9, 5);
    result = Draw();
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error().error.reason, ParticleError::UnsupportedSubtype);
    EXPECT_EQ(m_events.back(), "additive");
}
TEST_F(ParticleDrawingTest, CloudScratchLivesThroughBinderAndUsesTheLiveClockAndPhysicalIndices)
{
    Seed(1, 0x622, 8);
    Seed(3, 0x622, 8);
    for (const auto i : {1u, 3u})
    {
        auto& p = m_particles.All()[i];
        p.rotationFactor = 1;
        p.start.y = 10;
        p.field38 = 0x3f000000u;
        p.light = {2, 4, 6};
    }
    unsigned materials = 0, binds = 0;
    m_backend.hook = [&](std::string_view phase)
    {
        if (phase == "additive")
            m_clock = ++materials == 1 ? 100.0f : 200.0f;
        if (phase == "bind")
            m_particles.All()[++binds == 1 ? 1u : 3u].light = {40, 50, 60};
    };
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(m_particles.All()[1].rotation, 8);
    EXPECT_EQ(m_particles.All()[3].rotation, 6);
    EXPECT_EQ(m_backend.color, (Color{1, 2, 3, 1}));
    EXPECT_EQ(m_backend.completed, 2u);
    EXPECT_EQ(m_particles.All()[1].light, glm::vec3(40, 50, 60));
    EXPECT_EQ(m_particles.All()[3].field38, 0x3f000000u);
    EXPECT_EQ(std::count(m_events.begin(), m_events.end(), "sub-prefix"), 0);
}
TEST_F(ParticleDrawingTest, LoginCloudCallsTheOpaqueServiceBeforeRotationAndKeepsCanonicalColor)
{
    Seed(2, 0x622, 10);
    auto& p = m_particles.All()[2];
    p.rotationFactor = 1;
    p.start.y = 10;
    p.field38 = 0x7f800000u;
    m_backend.hook = [&](std::string_view phase)
    {
        if (phase == "sub-tail")
            m_clock = 200;
    };
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(p.rotation, 14);
    EXPECT_EQ(m_backend.color, (Color{2, 3, 4, 2}));
    EXPECT_EQ((std::vector<std::string>(m_events.begin() + 3, m_events.begin() + 7)),
              (std::vector<std::string>{"sub-prefix", "helper-f0", "helper-b0", "sub-tail"}));
}
TEST_F(ParticleDrawingTest, RawSelectorAndEveryColorFlagRemainIndependentOfMaterialAndSlot16)
{
    struct Case
    {
        std::int32_t selector;
        std::uint8_t flag;
        float alpha;
    };
    constexpr std::array cases{Case{0x5e4, 4, 1}, Case{0x105e4, 4, 2},
                               Case{-1, 0, 2},    Case{std::numeric_limits<std::int32_t>::min(), 255, 2},
                               Case{17, 2, 2},    Case{17, 3, 1}};
    const glm::vec3 p{10, 20, 30}, light{2, -3, 4};
    for (const auto& row : cases)
    {
        SCOPED_TRACE(row.selector);
        m_events.clear();
        m_backend.corners.clear();
        m_library.Put(row.selector, {1, 1}, row.flag);
        const Request request{row.selector, std::cref(p), -8, 4, std::cref(light), 0, {0, 0}, {1, 1}};
        ASSERT_TRUE(m_services.Billboard(request).HasValue());
        EXPECT_EQ(m_backend.selector, row.selector);
        EXPECT_EQ(m_backend.color, (Color{2, -3, 4, row.alpha}));
        ASSERT_EQ(m_backend.corners.size(), 4u);
        EXPECT_EQ(m_backend.corners[0].position, glm::vec3(14, 18, 30));
        EXPECT_EQ(m_events[2], "metadata:" + std::to_string(static_cast<std::uint32_t>(row.selector)));
        EXPECT_EQ(m_events[3], row.flag == 3 ? "color3" : "color4");
    }
}
TEST_F(ParticleDrawingTest, Color3BorrowsTheCanonicalReferenceAndZeroSizeStillEmits)
{
    glm::vec3 p{10, 20, 30}, light{};
    m_library.Put(17, {1, 1}, std::uint8_t{3});
    m_backend.hook = [&](std::string_view phase)
    {
        if (phase == "color3")
            light = {2, -3, 4};
    };
    const Request request{17, std::cref(p), 0, 0, std::cref(light), 0, {0, 0}, {1, 1}};
    ASSERT_TRUE(m_services.Billboard(request).HasValue());
    EXPECT_EQ(m_backend.color, (Color{2, -3, 4, 1}));
    EXPECT_EQ(m_backend.completed, 1u);
    for (const auto& corner : m_backend.corners)
        EXPECT_EQ(corner.position, p);
}
TEST_F(ParticleDrawingTest, EveryHeightModeUsesOnlyItsNativeComparison)
{
    struct Case
    {
        float z;
        std::uint8_t mode;
        bool accepted;
    };
    const std::array cases{Case{300, 1, true},
                           Case{300, 2, false},
                           Case{350, 1, true},
                           Case{350, 2, true},
                           Case{351, 1, false},
                           Case{std::numeric_limits<float>::quiet_NaN(), 1, true},
                           Case{std::numeric_limits<float>::quiet_NaN(), 2, false},
                           Case{999, 255, true},
                           Case{-999, 3, true}};
    for (const auto& row : cases)
    {
        m_events.clear();
        m_backend.corners.clear();
        Seed();
        m_particles.All()[0].position.z = row.z;
        ASSERT_TRUE(Draw(row.mode).HasValue());
        EXPECT_EQ(!m_events.empty(), row.accepted);
    }
}
TEST_F(ParticleDrawingTest, AServiceCanActivateALaterCanonicalSlotWithoutASnapshotOfThePool)
{
    Seed();
    unsigned ends = 0;
    m_backend.hook = [&](std::string_view phase)
    {
        if (phase == "end" && ++ends == 1)
            Seed(29);
    };
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(m_backend.completed, 2u);
    EXPECT_TRUE(m_particles.All()[29].alive);
}
TEST_F(ParticleDrawingTest, FrozenFiniteRotationAlsoPassesThroughTheNewSynchronousAdapter)
{
    // Caso1 ya versionado de billboard300; ningún expected se calcula con el helper.
    constexpr std::array<std::uint32_t, 12> words{0xbf722bf0u, 0xbea5ffb1u, 0,           0x454f7e05u,
                                                  0x3e1b081eu, 0xbee22c08u, 0x3f625fdau, 0xc688eb09u,
                                                  0xbe92c9e2u, 0x3f562577u, 0x3eef1636u, 0x4555da83u};
    for (std::size_t i = 0; i < words.size(); ++i)
        m_view[i] = std::bit_cast<float>(words[i]);
    const glm::vec3 position{std::bit_cast<float>(0x469977a7u), std::bit_cast<float>(0x45b95d3bu),
                             std::bit_cast<float>(0x45569dfbu)};
    const glm::vec3 light{std::bit_cast<float>(0x3f5e6b81u), std::bit_cast<float>(0x3edd9368u),
                          std::bit_cast<float>(0x3eb4edcbu)};
    m_library.Put(1452, {1, 1}, std::uint8_t{3});
    const Request request{1452,
                          std::cref(position),
                          std::bit_cast<float>(0x42c00118u),
                          std::bit_cast<float>(0x4375c1ddu),
                          std::cref(light),
                          std::bit_cast<float>(0xc365e083u),
                          {0, 0},
                          {1, 1}};
    ASSERT_TRUE(m_services.Billboard(request).HasValue());
    constexpr std::array<std::array<std::uint32_t, 3>, 4> expected{{{0xc6854a30u, 0xc65c3313u, 0x458819d1u},
                                                                    {0xc685c5ecu, 0xc65b0d70u, 0x458819d1u},
                                                                    {0xc6873dc4u, 0xc65d86efu, 0x458819d1u},
                                                                    {0xc686c208u, 0xc65eac92u, 0x458819d1u}}};
    ASSERT_EQ(m_backend.corners.size(), 4u);
    for (std::size_t i = 0; i < expected.size(); ++i)
        for (int axis = 0; axis < 3; ++axis)
            EXPECT_EQ(std::bit_cast<std::uint32_t>(m_backend.corners[i].position[axis]),
                      expected[i][static_cast<std::size_t>(axis)]);
    EXPECT_EQ(m_backend.color, (Color{light.x, light.y, light.z, 1}));
}

TEST_F(ParticleDrawingTest, DirectNativeDestinationsDoNotInventASubtypeGuard)
{
    // 548550_ext_67/68 y 5c6_607: estos destinos NO leen +0x0c.
    for (const std::int32_t type : {0x67, 0x68, 0x5c6, 0x607})
    {
        SCOPED_TRACE(type);
        m_particles.Clear();
        m_events.clear();
        m_backend.corners.clear();
        Seed(0, type, std::numeric_limits<std::int32_t>::min());
        ASSERT_TRUE(Draw().HasValue());
        EXPECT_EQ(m_backend.corners.size(), 4u);
    }
}
TEST_F(ParticleDrawingTest, FireDefaultKeepsSignedFramesForEveryNonSpecialSubtype)
{
    // 548550 fire-smoke: sólo5/7/8/11/12/13 salen a ramas aún no implementadas.
    for (const std::int32_t subtype : {4, 6, 9, 10, -1, std::numeric_limits<std::int32_t>::min()})
    {
        SCOPED_TRACE(subtype);
        m_particles.Clear();
        m_events.clear();
        m_backend.corners.clear();
        Seed(0, 0x5d9, subtype);
        m_particles.All()[0].field48 = std::bit_cast<float>(-1);
        ASSERT_TRUE(Draw().HasValue());
        ASSERT_EQ(m_backend.corners.size(), 4u);
        EXPECT_EQ(m_backend.corners[0].position.x, -1);
        EXPECT_EQ(m_backend.corners[0].uv.x, -0.25f);
    }
}

TEST_F(ParticleDrawingTest, InitialMaterialUsesOnlyEqualityWithThreeAndColorMetadataIsQueriedAgain)
{
    for (const std::uint8_t flag :
         {std::uint8_t{0}, std::uint8_t{2}, std::uint8_t{3}, std::uint8_t{4}, std::uint8_t{255}})
    {
        SCOPED_TRACE(static_cast<unsigned>(flag));
        m_events.clear();
        m_backend.corners.clear();
        Seed();
        m_library.Put(0x67, {99, 77}, flag);
        m_library.Put(17, {8, 4}, std::uint8_t{3});
        m_backend.hook = [&](std::string_view phase)
        {
            if (phase == "additive" || phase == "alpha0")
                m_library.Put(17, {999, 999}, std::uint8_t{255});
        };
        ASSERT_TRUE(Draw().HasValue());
        EXPECT_EQ(m_events[2], flag == 3 ? "additive" : "alpha0");
        EXPECT_EQ(m_events[6], "color4");
        EXPECT_EQ(m_backend.color, (Color{2, 3, 4, 2}));
        EXPECT_EQ(m_backend.corners[0].position, glm::vec3(-4, -2, 0));
    }
}
TEST_F(ParticleDrawingTest, SpecialServiceErrorsKeepTheirMutationAndNeverReachTheLaterFrontier)
{
    struct Case
    {
        const char* event;
        ParticleDrawingPhase phase;
    };
    for (const auto& row : std::array{Case{"type-850", ParticleDrawingPhase::Type620850},
                                      Case{"type-870", ParticleDrawingPhase::Type620870}})
    {
        m_events.clear();
        Seed(7);
        Seed(11);
        auto& p = m_particles.All()[7];
        m_backend.fail = row.event;
        m_backend.hook = [&](std::string_view phase)
        {
            if (phase == "additive")
            {
                p.type = 0x5ac;
                p.subtype = 6;
            }
            if (phase == "type-850")
            {
                p.type = 0x5ed;
                p.subtype = 5;
            }
            if (phase == "type-870")
            {
                p.type = 0x5d9;
                p.subtype = 0;
            }
        };
        const auto result = Draw();
        ASSERT_FALSE(result.HasValue());
        EXPECT_EQ(result.Error().slot, 7u);
        EXPECT_EQ(result.Error().error.phase, row.phase);
        EXPECT_EQ(m_events.back(), row.event);
        EXPECT_EQ(p.type, row.phase == ParticleDrawingPhase::Type620850 ? 0x5ed : 0x5d9);
        EXPECT_TRUE(m_backend.selectors.empty());
    }
}
TEST_F(ParticleDrawingTest, CloudPostServiceSubtypeCanSelectScratchWithoutRedispatchingTheType)
{
    Seed(2, 0x622, 10);
    auto& p = m_particles.All()[2];
    p.rotationFactor = 1;
    p.start.y = 10;
    p.field38 = 0x3f000000u;
    p.light = {2, 4, 6};
    m_backend.hook = [&](std::string_view phase)
    {
        if (phase == "sub-tail")
        {
            p.subtype = 8;
            p.type = 0x5d9;
        }
    };
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(p.rotation, 12);
    EXPECT_EQ(m_backend.color, (Color{1, 2, 3, 1}));
    ASSERT_EQ(m_backend.corners.size(), 4u);
    EXPECT_EQ(m_backend.corners[0].uv, glm::vec2(0, 1));
    EXPECT_EQ(std::count(m_events.begin(), m_events.end(), "sub-prefix"), 1);
}

TEST_F(ParticleDrawingTest, UnorderedRotationAndZeroAlphaStillEmitTheUnrotatedQuad)
{
    const glm::vec3 position{10, 20, 30}, light{0, 1, 2};
    m_library.Put(17, {8, 4}, std::uint8_t{0});
    const Request request{17,    std::cref(position), 8, 4, std::cref(light), std::bit_cast<float>(0x7fc12345u), {0, 0},
                          {1, 1}};
    ASSERT_TRUE(m_services.Billboard(request).HasValue());
    ASSERT_EQ(m_backend.corners.size(), 4u);
    EXPECT_EQ(m_backend.corners[0].position, glm::vec3(6, 18, 30));
    EXPECT_EQ(m_backend.corners[2].position, glm::vec3(14, 22, 30));
    EXPECT_EQ(m_backend.color, (Color{0, 1, 2, 0}));
    EXPECT_EQ(m_backend.completed, 1u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(request.rotation), 0x7fc12345u);
}

TEST_F(ParticleDrawingTest, SmokeReadsTheClosedMaterialGuardSetsAfterTheInitialService)
{
    // 548707..749: primer conjunto2/5/12/14/15/20/21/29; segundo37/38; última lectura6.
    struct Case
    {
        std::int32_t subtype;
        int calls;
    };
    constexpr std::array cases{Case{0, 0},  Case{2, 1},  Case{5, 1},  Case{12, 1}, Case{14, 1},
                               Case{15, 1}, Case{20, 1}, Case{21, 1}, Case{29, 1}, Case{37, 1},
                               Case{38, 1}, Case{6, 0},  Case{-1, 0}};
    for (const auto& row : cases)
    {
        SCOPED_TRACE(row.subtype);
        m_particles.Clear();
        m_events.clear();
        m_backend.corners.clear();
        Seed(0, 0x5f2, 2);
        auto& p = m_particles.All()[0];
        p.rotation = 90;
        m_backend.hook = [&](std::string_view phase)
        {
            if (phase == "additive")
                p.subtype = row.subtype;
        };
        ASSERT_TRUE(Draw().HasValue());
        EXPECT_EQ(std::count(m_events.begin(), m_events.end(), "sub-prefix"), row.calls);
        if (row.subtype == 6)
        {
            EXPECT_EQ(m_backend.corners[0].position, glm::vec3(-4, -2, 0));
        }
        EXPECT_EQ(p.rotation, 90);
    }
}

TEST_F(ParticleDrawingTest, BillboardCapturesScalarArgumentsButKeepsPositionAndColorBorrowed)
{
    const glm::vec3 position{10, 20, 30}, light{2, 3, 4};
    m_library.Put(17, {1, 1}, std::uint8_t{4});
    m_library.Put(42, {1, 1}, std::uint8_t{3});
    Request request{17, std::cref(position), 8, 4, std::cref(light), 0, {0.25f, 0.5f}, {0.125f, 0.25f}};
    m_backend.hook = [&](std::string_view phase)
    {
        if (phase == "bind")
        {
            request.selector = 42;
            request.width = 100;
            request.height = 200;
            request.rotation = 90;
            request.uv = {0, 0};
            request.uvSize = {1, 1};
        }
    };
    ASSERT_TRUE(m_services.Billboard(request).HasValue());
    EXPECT_EQ(m_backend.selector, 17);
    EXPECT_EQ(m_events[2], "metadata:17");
    EXPECT_EQ(m_backend.color, (Color{2, 3, 4, 2}));
    ASSERT_EQ(m_backend.corners.size(), 4u);
    EXPECT_EQ(m_backend.corners[0].position, glm::vec3(6, 18, 30));
    EXPECT_EQ(m_backend.corners[0].uv, glm::vec2(0.25f, 0.75f));
}

} // namespace
