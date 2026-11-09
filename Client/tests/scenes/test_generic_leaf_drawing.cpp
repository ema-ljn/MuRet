#include "Scenes/LeafDrawingServices.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Data/Textures/Sampling.h"
#include "Data/Textures/TextureSet.h"
#include "Render/Billboard.h"
#include "Render/IParticleDrawBackend.h"
#include "Render/ModelBatch.h"
#include "Render/ModelBatchParticleBackend.h"
#include "Render/ModelBatchParticleState.h"
#include "Render/ModelVertex.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/Skeleton.h"
#include "Render/Pass3D.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/GameModels.h"
#include "Scenes/TextureMetadata.h"
#include "Scenes/WorldFaunaDrawing.h"
#include "World/Leaves.h"
#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/RecordingSink.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <bit>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <system_error>
#include <vector>

namespace
{
using Client::Render::Color;
using Client::Render::ModelBatch;
using Client::Render::ModelBatchParticleBackend;
using Client::Render::ModelBatchParticleState;
using Client::Render::TextureSlot;
using Client::Render::Models::OriginalMatrix;
using Result = Client::Render::IParticleDrawBackend::Result;

enum class Event
{
    Blend,
    White,
    Bind,
    Push,
    Translate,
    Quad,
    Billboard,
    Pop
};

// Cada método cruza el provider REAL; el registro no reemplaza transformación, estado ni envío.
class TracingServices final : public Client::Scenes::ILeafDrawingServices
{
public:
    explicit TracingServices(Client::Scenes::LeafDrawingServices& real) : m_real(real) {}
    Result Blend(Client::World::LeafBlend blend) override
    {
        events.push_back(Event::Blend);
        return m_real.Blend(blend);
    }
    Result WhiteColor() override
    {
        events.push_back(Event::White);
        return m_real.WhiteColor();
    }
    Result BindTexture(std::int32_t selector) override
    {
        events.push_back(Event::Bind);
        const auto result = m_real.BindTexture(selector);
        if (result.HasValue() && onBind)
            onBind();
        return result;
    }
    Result PushMatrix() override
    {
        events.push_back(Event::Push);
        const auto result = m_real.PushMatrix();
        if (result.HasValue() && onPush)
            onPush();
        return result;
    }
    Result Translate(float x, float y, float z) override
    {
        events.push_back(Event::Translate);
        const auto result = m_real.Translate(x, y, z);
        if (result.HasValue() && onTranslate)
            onTranslate();
        return result;
    }
    Result Quad(ModelBatch& batch, const Client::World::LeafQuad& quad) override
    {
        events.push_back(Event::Quad);
        return m_real.Quad(batch, quad);
    }
    Result Billboard(ModelBatch& batch, const Client::Scenes::LeafBillboardRequest& request) override
    {
        events.push_back(Event::Billboard);
        return m_real.Billboard(batch, request);
    }
    Result PopMatrix() override
    {
        events.push_back(Event::Pop);
        return m_real.PopMatrix();
    }
    std::vector<Event> events;
    std::function<void()> onBind;
    std::function<void()> onPush;
    std::function<void()> onTranslate;

private:
    Client::Scenes::LeafDrawingServices& m_real;
};

class GenericBackend final : public ModelBatchParticleBackend
{
public:
    using ModelBatchParticleBackend::ModelBatchParticleBackend;
    Result TexCoord(glm::vec2 uv) override
    {
        ++uvCalls;
        if (uvCalls == failUvAt)
            return Client::Core::Unexpected(std::make_error_code(std::errc::io_error));
        if (onUv)
            onUv();
        return ModelBatchParticleBackend::TexCoord(uv);
    }
    Result End() override
    {
        ++endCalls;
        return ModelBatchParticleBackend::End();
    }
    int uvCalls = 0;
    int failUvAt = 0;
    int endCalls = 0;
    std::function<void()> onUv;
};

class GenericLibrary final : public Client::Scenes::IModelLibrary
{
public:
    explicit GenericLibrary(const Client::Scenes::GameModels& owner) : m_owner(owner) {}
    const Client::Data::Models::Model* Find(std::uint16_t model) const override
    {
        return m_owner.Find(model);
    }
    const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t model) const override
    {
        return m_owner.SkeletonOf(model);
    }
    std::span<const TextureSlot> MeshTextures(std::uint16_t model) const override
    {
        return m_owner.MeshTextures(model);
    }
    std::optional<Client::Scenes::TextureMetadata> TextureMetadataOf(std::uint32_t selector) const override
    {
        metadataKeys.push_back(std::bit_cast<std::int32_t>(selector));
        return m_owner.TextureMetadataOf(static_cast<std::uint32_t>(selector));
    }
    std::optional<TextureSlot> TextureSlotOf(std::uint32_t selector) const override
    {
        bindingKeys.push_back(std::bit_cast<std::int32_t>(selector));
        return m_owner.TextureSlotOf(static_cast<std::uint32_t>(selector));
    }
    mutable std::vector<std::int32_t> metadataKeys;
    mutable std::vector<std::int32_t> bindingKeys;

private:
    const Client::Scenes::GameModels& m_owner;
};

class GenericLeafDrawingTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Única fila disponible de las tres solicitudes cerradas61a560 de World1.
        const Client::Data::Textures::TextureSetEntry entry{
            "World1\\leaf01.tga", 100, {Client::Data::Textures::Filter::Nearest, Client::Data::Textures::Wrap::Clamp}};
        ASSERT_EQ(m_models.LoadTextures(Client::Tests::RepoDataDir(), std::span(&entry, 1), m_textures, m_log).uploaded,
                  1u);
        m_batch.Begin(Client::Render::Pass3D{});
    }
    Client::Scenes::GameModels m_models;
    Client::Tests::FakeTextureStore m_textures;
    Client::Tests::RecordingSink m_sink;
    Client::Core::Log m_log{m_sink, Client::Core::LogLevel::Debug};
    GenericLibrary m_library{m_models};
    ModelBatch m_batch;
    OriginalMatrix m_view = Client::Render::ViewRows(Client::Render::Pass3D{}.view);
    ModelBatchParticleBackend m_backend{std::ref(m_batch), std::cref(m_library),
                                        ModelBatchParticleState::ForModelPass()};
    Client::Scenes::LeafDrawingServices m_services{std::cref(m_library), std::ref(m_backend), std::cref(m_view)};
    Client::World::Leaves m_leaves;
    std::int32_t m_map = 0;
    std::int32_t m_selector = 0;

    Client::World::Leaf& Alive()
    {
        auto& leaf = m_leaves.All()[199];
        leaf.alive = 0x80;
        leaf.type = 100;
        leaf.position = {10, 20, 30};
        return leaf;
    }
};

// Push/Translate/Quad/Pop vacíos no pueden producir estos vértices World en el mismo lote real.
TEST_F(GenericLeafDrawingTest, MapZeroDrawsPhysicalSlot199WithCurrentMaterialAndNoMetadataLookup)
{
    auto& leaf = m_leaves.All()[199];
    leaf.alive = 0x80;
    leaf.type = 100;
    leaf.position = {10, 20, 30};
    leaf.light = {0.25f, 0.5f, 0.75f};
    ASSERT_TRUE(Client::Scenes::DrawWorldLeaves(m_batch, m_leaves, m_map, m_selector, &m_services).HasValue());
    ASSERT_EQ(m_batch.Vertices().size(), 6u);
    const std::array expected{glm::vec3(7, 17, 27), glm::vec3(13, 23, 27), glm::vec3(13, 23, 33),
                              glm::vec3(7, 17, 27), glm::vec3(13, 23, 33), glm::vec3(7, 17, 33)};
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        EXPECT_EQ(m_batch.Vertices()[i].position, expected[i]);
        EXPECT_EQ(m_batch.Vertices()[i].color, (Color{1, 1, 1, 1}));
    }
    EXPECT_EQ(m_batch.Commands().back().state.space, Client::Render::ModelCoordinateSpace::World);
    EXPECT_EQ(m_batch.Commands().back().state.texture, TextureSlot{100});
    EXPECT_EQ(m_batch.Commands().back().state.blend, Client::Render::ModelBlend::Alpha);
    EXPECT_EQ(m_library.bindingKeys, (std::vector<std::int32_t>{100}));
    EXPECT_TRUE(m_library.metadataKeys.empty());
    EXPECT_EQ(m_backend.State().uv, glm::vec2(0));
}

TEST_F(GenericLeafDrawingTest, PushTranslatePopReallySaveComposeAndRestoreTheModelWithoutDoubleTranslation)
{
    ASSERT_TRUE(m_services.Blend(Client::World::LeafBlend::Alpha).HasValue());
    ASSERT_TRUE(m_services.BindTexture(100).HasValue());
    const Color inherited{0.25f, 0.5f, 0.75f, 0.5f};
    ASSERT_TRUE(m_backend.Color4(inherited).HasValue());
    ASSERT_TRUE(m_services.Translate(5, 0, 0).HasValue());
    ASSERT_TRUE(m_services.PushMatrix().HasValue());
    ASSERT_TRUE(m_services.Translate(10, 20, 30).HasValue());
    // Rotación90° exacta como INPUT del kernel L5, sin generar expected con ese kernel.
    constexpr OriginalMatrix turn{0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0};
    const auto quad = Client::World::LeafQuadOf({10, 20, 30}, turn, {1, 2});
    ASSERT_TRUE(m_services.Quad(m_batch, quad).HasValue());
    ASSERT_EQ(m_batch.Vertices().size(), 6u);
    EXPECT_EQ(m_batch.Vertices()[0].position, glm::vec3(16, 19, 28));
    EXPECT_EQ(m_batch.Vertices()[2].position, glm::vec3(14, 21, 32));
    ASSERT_TRUE(m_services.PopMatrix().HasValue());
    EXPECT_FALSE(m_backend.HasPushedMatrix());
    EXPECT_EQ(m_backend.ModelMatrix()[3], 5);
    ASSERT_TRUE(m_services.Quad(m_batch, quad).HasValue());
    ASSERT_EQ(m_batch.Vertices().size(), 12u);
    EXPECT_EQ(m_batch.Vertices()[6].position, glm::vec3(6, -1, -2));
    EXPECT_EQ(m_batch.Vertices()[8].position, glm::vec3(4, 1, 2));
    for (const auto& vertex : m_batch.Vertices())
        EXPECT_EQ(vertex.color, inherited);
    EXPECT_TRUE(m_library.metadataKeys.empty());
    EXPECT_EQ(m_backend.State().uv, glm::vec2(0));
}

TEST_F(GenericLeafDrawingTest, SuppressionStillExecutesRealPushTranslatePopAndLeavesUvAndMaterial)
{
    auto& leaf = Alive();
    m_map = 34;
    m_selector = 1;
    ASSERT_TRUE(m_backend.TexCoord({0.25f, 0.75f}).HasValue());
    TracingServices services{m_services};
    services.onBind = [&] { leaf.type = 102; }; // K tardío, sin fingir otra fila física cargada.
    services.onTranslate = [&]
    {
        EXPECT_TRUE(m_backend.HasPushedMatrix());
        EXPECT_EQ(m_backend.ModelMatrix()[3], 10);
        EXPECT_EQ(m_backend.ModelMatrix()[7], 20);
        EXPECT_EQ(m_backend.ModelMatrix()[11], 30);
        m_selector = 2;
    };
    ASSERT_TRUE(Client::Scenes::DrawWorldLeaves(m_batch, m_leaves, m_map, m_selector, &services).HasValue());
    EXPECT_EQ(services.events,
              (std::vector{Event::Blend, Event::White, Event::Bind, Event::Push, Event::Translate, Event::Pop}));
    EXPECT_TRUE(m_batch.Vertices().empty());
    EXPECT_FALSE(m_backend.HasPushedMatrix());
    EXPECT_EQ(m_backend.ModelMatrix()[3], 0);
    EXPECT_EQ(m_backend.State().uv, glm::vec2(0.25f, 0.75f));
    EXPECT_EQ(m_backend.State().color, (Color{1, 1, 1, 1}));
    EXPECT_EQ(m_backend.State().textureSelector, 100);
    EXPECT_EQ(leaf.alive, 0x80);
    EXPECT_TRUE(m_library.metadataKeys.empty());
}

TEST_F(GenericLeafDrawingTest, PositionIsReadAfterPushAndLateMapAndDimensionsFollowTranslate)
{
    auto& leaf = Alive();
    TracingServices services{m_services};
    services.onPush = [&] { leaf.position = {40, 50, 60}; };
    services.onTranslate = [&]
    {
        leaf.position = {900, 900, 900};
        m_map = 18;
        leaf.spin = {2, 7, 99};
    };
    ASSERT_TRUE(Client::Scenes::DrawWorldLeaves(m_batch, m_leaves, m_map, m_selector, &services).HasValue());
    ASSERT_EQ(m_batch.Vertices().size(), 6u);
    EXPECT_EQ(m_batch.Vertices()[0].position, glm::vec3(38, 48, 53));
    EXPECT_EQ(m_batch.Vertices()[2].position, glm::vec3(42, 52, 67));
    EXPECT_EQ(m_batch.Commands().back().state.space, Client::Render::ModelCoordinateSpace::World);
    EXPECT_FALSE(m_backend.HasPushedMatrix());
}

TEST_F(GenericLeafDrawingTest, AMapChangedToTwoAfterTranslateStaysGeneric)
{
    Alive();
    TracingServices services{m_services};
    services.onTranslate = [&] { m_map = 2; };
    ASSERT_TRUE(Client::Scenes::DrawWorldLeaves(m_batch, m_leaves, m_map, m_selector, &services).HasValue());
    ASSERT_EQ(m_batch.Vertices().size(), 6u);
    EXPECT_EQ(m_batch.Vertices()[0].position, glm::vec3(7, 17, 27));
    EXPECT_EQ(m_batch.Commands().back().state.space, Client::Render::ModelCoordinateSpace::World);
    EXPECT_EQ(services.events.back(), Event::Pop);
    EXPECT_TRUE(m_library.metadataKeys.empty());
}

TEST_F(GenericLeafDrawingTest, LateTypeAndMapPartitionsUseTheirIndependentDimensionsAndInitialMaterial)
{
    struct Case
    {
        std::int32_t map;
        std::int32_t type;
        std::int32_t selector;
        glm::vec2 spin;
        float scale;
        glm::vec3 first;
        glm::vec3 opposite;
        Client::Render::ModelBlend blend;
    };
    using Client::Render::ModelBlend;
    const std::array cases{Case{0, 100, 0, {9, 9}, 2.5f, {7, 17, 27}, {13, 23, 33}, ModelBlend::Alpha},
                           Case{0, 102, 0, {9, 9}, 2.5f, {9, 19, 10}, {11, 21, 50}, ModelBlend::Alpha},
                           Case{34, 102, 1, {9, 9}, 2.5f, {9, 19, 10}, {11, 21, 50}, ModelBlend::Alpha},
                           Case{0, 1589, 0, {9, 9}, 2.5f, {5, 15, 20}, {15, 25, 40}, ModelBlend::Alpha},
                           Case{18, 1589, 0, {2, 7}, 2.5f, {8, 18, 23}, {12, 22, 37}, ModelBlend::Alpha},
                           Case{23, 102, 0, {-2, 0}, 2.5f, {12, 22, 30}, {8, 18, 30}, ModelBlend::Alpha},
                           Case{17, 100, 0, {2, 7}, 2.5f, {7, 17, 27}, {13, 23, 33}, ModelBlend::Alpha},
                           Case{24, 100, 0, {2, 7}, 2.5f, {7, 17, 27}, {13, 23, 33}, ModelBlend::Alpha},
                           Case{7, 100, 0, {2, 7}, 2.5f, {7, 17, 27}, {13, 23, 33}, ModelBlend::Additive},
                           Case{55, 1589, 0, {2, 7}, 2.5f, {5, 15, 20}, {15, 25, 40}, ModelBlend::Additive}};
    for (const auto& value : cases)
    {
        SCOPED_TRACE(value.map);
        m_batch.Begin(Client::Render::Pass3D{});
        auto& leaf = Alive();
        m_map = value.map;
        m_selector = value.selector;
        leaf.spin = {value.spin.x, value.spin.y, 0};
        leaf.scale = value.scale;
        TracingServices services{m_services};
        services.onBind = [&] { leaf.type = value.type; };
        ASSERT_TRUE(Client::Scenes::DrawWorldLeaves(m_batch, m_leaves, m_map, m_selector, &services).HasValue());
        ASSERT_EQ(m_batch.Vertices().size(), 6u);
        EXPECT_EQ(m_batch.Vertices()[0].position, value.first);
        EXPECT_EQ(m_batch.Vertices()[2].position, value.opposite);
        EXPECT_EQ(m_batch.Commands().back().state.blend, value.blend);
    }
    EXPECT_TRUE(m_library.metadataKeys.empty());
}

TEST_F(GenericLeafDrawingTest, BillboardColorPersistsIntoWorldQuadAndForeignRasterIsImported)
{
    glm::vec3 position{0, 0, -10};
    glm::vec3 light{0.25f, 0.5f, 0.75f};
    ASSERT_TRUE(m_services.Blend(Client::World::LeafBlend::Alpha).HasValue());
    ASSERT_TRUE(m_services.BindTexture(100).HasValue());
    ASSERT_TRUE(m_services.Billboard(m_batch, {100, position, 4, 4, light}).HasValue());
    ASSERT_TRUE(m_services.PushMatrix().HasValue());
    ASSERT_TRUE(m_services.Translate(1, 2, 3).HasValue());
    constexpr OriginalMatrix identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    const auto quad = Client::World::LeafQuadOf({1, 2, 3}, identity, {1, 2});
    ASSERT_TRUE(m_services.Quad(m_batch, quad).HasValue());
    ASSERT_TRUE(m_services.PopMatrix().HasValue());
    ASSERT_EQ(m_batch.Vertices().size(), 12u);
    EXPECT_EQ(m_batch.Vertices()[6].position, glm::vec3(0, 1, 1));
    EXPECT_EQ(m_batch.Vertices()[6].color, (Color{0.25f, 0.5f, 0.75f, 0.25f}));
    m_batch.SetState({TextureSlot{100}, Client::Render::ModelBlend::Shadow, false});
    ASSERT_TRUE(m_services.Quad(m_batch, quad).HasValue());
    EXPECT_EQ(m_batch.Commands().back().state.blend, Client::Render::ModelBlend::Opaque);
    EXPECT_EQ(m_batch.Commands().back().state.cullEnabled, true);
    EXPECT_EQ(m_batch.Commands().back().state.depthWrite, false);
    EXPECT_EQ(m_batch.Commands().back().state.alphaTest, false);
    EXPECT_EQ(m_batch.Commands().back().state.texture2d, true);
    EXPECT_FALSE(m_batch.Commands().back().state.depthTest);
    EXPECT_EQ(m_batch.Vertices().back().color, (Color{0.25f, 0.5f, 0.75f, 0.25f}));
    EXPECT_EQ(m_library.metadataKeys, (std::vector<std::int32_t>{100}));
    EXPECT_EQ(m_backend.State().uv, glm::vec2(0));
}

TEST_F(GenericLeafDrawingTest, StackErrorsAndMatrixOperationsDuringAPrimitiveHaveNoFictitiousCleanup)
{
    const auto underflow = m_services.PopMatrix();
    ASSERT_FALSE(underflow.HasValue());
    EXPECT_EQ(underflow.Error(), std::errc::invalid_argument);
    ASSERT_TRUE(m_services.PushMatrix().HasValue());
    const auto nested = m_services.PushMatrix();
    ASSERT_FALSE(nested.HasValue());
    EXPECT_EQ(nested.Error(), std::errc::function_not_supported);
    ASSERT_TRUE(m_services.Translate(1, 2, 3).HasValue());
    const auto eye = m_backend.Begin();
    ASSERT_FALSE(eye.HasValue());
    EXPECT_EQ(eye.Error(), std::errc::operation_in_progress);
    EXPECT_FALSE(m_backend.IsDrawing());
    EXPECT_TRUE(m_backend.HasPushedMatrix());
    EXPECT_EQ(m_backend.ModelMatrix()[3], 1);
    ASSERT_TRUE(m_services.PopMatrix().HasValue());
    ASSERT_TRUE(m_backend.Begin().HasValue());
    EXPECT_FALSE(m_services.PushMatrix().HasValue());
    EXPECT_FALSE(m_services.Translate(5, 6, 7).HasValue());
    EXPECT_FALSE(m_services.PopMatrix().HasValue());
    EXPECT_TRUE(m_backend.IsDrawing());
    EXPECT_EQ(m_backend.ModelMatrix()[3], 0);
}

TEST_F(GenericLeafDrawingTest, AFailedQuadKeepsTranslationAndPushWithoutEndOrPop)
{
    Alive();
    GenericBackend backend{std::ref(m_batch), std::cref(m_library), ModelBatchParticleState::ForModelPass()};
    backend.failUvAt = 2;
    Client::Scenes::LeafDrawingServices provider{std::cref(m_library), std::ref(backend), std::cref(m_view)};
    TracingServices services{provider};
    const auto result = Client::Scenes::DrawWorldLeaves(m_batch, m_leaves, m_map, m_selector, &services);
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error(), std::errc::io_error);
    EXPECT_EQ(services.events.back(), Event::Quad);
    EXPECT_EQ(backend.endCalls, 0);
    EXPECT_TRUE(backend.IsDrawing());
    EXPECT_TRUE(backend.HasPushedMatrix());
    EXPECT_EQ(backend.ModelMatrix()[3], 10);
    EXPECT_EQ(backend.ModelMatrix()[7], 20);
    EXPECT_EQ(backend.ModelMatrix()[11], 30);
    EXPECT_EQ(backend.State().uv, glm::vec2(0, 1));
    EXPECT_TRUE(m_batch.Vertices().empty());
    EXPECT_FALSE(m_batch.InvalidateTextureKnowledge().HasValue());
}

TEST_F(GenericLeafDrawingTest, CornersAndUvArePreparedBeforeBeginAndNotReevaluatedDuringEmission)
{
    constexpr OriginalMatrix identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    auto quad = Client::World::LeafQuadOf({10, 20, 30}, identity, {1, 2});
    GenericBackend backend{std::ref(m_batch), std::cref(m_library), ModelBatchParticleState::ForModelPass()};
    Client::Scenes::LeafDrawingServices provider{std::cref(m_library), std::ref(backend), std::cref(m_view)};
    ASSERT_TRUE(provider.Blend(Client::World::LeafBlend::Alpha).HasValue());
    ASSERT_TRUE(provider.BindTexture(100).HasValue());
    ASSERT_TRUE(provider.PushMatrix().HasValue());
    ASSERT_TRUE(provider.Translate(10, 20, 30).HasValue());
    backend.onUv = [&]
    {
        for (auto& corner : quad.corners)
            corner = {900, 900, 900};
        for (auto& uv : quad.uv)
            uv = {9, 9};
    };
    ASSERT_TRUE(provider.Quad(m_batch, quad).HasValue());
    ASSERT_TRUE(provider.PopMatrix().HasValue());
    ASSERT_EQ(m_batch.Vertices().size(), 6u);
    EXPECT_EQ(m_batch.Vertices()[0].position, glm::vec3(9, 19, 28));
    EXPECT_EQ(m_batch.Vertices()[2].position, glm::vec3(11, 21, 32));
    EXPECT_EQ(backend.State().uv, glm::vec2(0));
}

TEST_F(GenericLeafDrawingTest, NegativeFull32SelectorNeedsNoColorMetadataIn621320)
{
    auto& leaf = Alive();
    leaf.type = std::numeric_limits<std::int32_t>::min();
    ASSERT_TRUE(Client::Scenes::DrawWorldLeaves(m_batch, m_leaves, m_map, m_selector, &m_services).HasValue());
    ASSERT_EQ(m_batch.Vertices().size(), 6u);
    EXPECT_EQ(m_batch.Vertices()[0].position, glm::vec3(7, 17, 27));
    EXPECT_EQ(m_batch.Commands().back().state.rawTextureName, 0x80000000u);
    EXPECT_TRUE(m_library.bindingKeys.empty());
    EXPECT_TRUE(m_library.metadataKeys.empty());
}

TEST_F(GenericLeafDrawingTest, MissingWorld1Slot101FailsBeforeAnyMatrixOperationInsteadOfDrawingNothingSuccessfully)
{
    auto& leaf = Alive();
    leaf.type = 101;
    TracingServices services{m_services};
    const auto result = Client::Scenes::DrawWorldLeaves(m_batch, m_leaves, m_map, m_selector, &services);
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error(), std::errc::no_such_file_or_directory);
    EXPECT_EQ(services.events.back(), Event::Bind);
    EXPECT_FALSE(m_backend.HasPushedMatrix());
    EXPECT_EQ(m_backend.ModelMatrix()[3], 0);
    EXPECT_TRUE(m_batch.Vertices().empty());
    EXPECT_TRUE(m_library.metadataKeys.empty());
}

TEST_F(GenericLeafDrawingTest, InvalidPreparedPacketAndMismatchedBatchKeepTheMatrixPrefix)
{
    ASSERT_TRUE(m_services.PushMatrix().HasValue());
    ASSERT_TRUE(m_services.Translate(10, 20, 30).HasValue());
    constexpr OriginalMatrix identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    auto quad = Client::World::LeafQuadOf({10, 20, 30}, identity, {1, 2});
    ModelBatch other;
    const auto mismatched = m_services.Quad(other, quad);
    ASSERT_FALSE(mismatched.HasValue());
    EXPECT_EQ(mismatched.Error(), std::errc::invalid_argument);
    quad.uv[3] = {9, 9}; // No es un paquete del kernel621320: el original emite UV constantes.
    const auto malformed = m_services.Quad(m_batch, quad);
    ASSERT_FALSE(malformed.HasValue());
    EXPECT_EQ(malformed.Error(), std::errc::invalid_argument);
    EXPECT_TRUE(m_backend.HasPushedMatrix());
    EXPECT_EQ(m_backend.ModelMatrix()[3], 10);
    EXPECT_FALSE(m_backend.IsDrawing());
    EXPECT_TRUE(m_batch.Vertices().empty());
}

TEST_F(GenericLeafDrawingTest, ForeignStateDuringAWorldPrimitiveRetainsTheOpenPrefixWithoutPop)
{
    Alive();
    GenericBackend backend{std::ref(m_batch), std::cref(m_library), ModelBatchParticleState::ForModelPass()};
    Client::Scenes::LeafDrawingServices provider{std::cref(m_library), std::ref(backend), std::cref(m_view)};
    backend.onUv = [&] { m_batch.SetState({TextureSlot{100}, Client::Render::ModelBlend::Opaque, false}); };
    const auto result = Client::Scenes::DrawWorldLeaves(m_batch, m_leaves, m_map, m_selector, &provider);
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error(), std::errc::state_not_recoverable);
    EXPECT_TRUE(backend.HasPushedMatrix());
    EXPECT_TRUE(backend.IsDrawing());
    EXPECT_EQ(backend.endCalls, 0);
    EXPECT_TRUE(m_batch.Vertices().empty());
}
} // namespace
