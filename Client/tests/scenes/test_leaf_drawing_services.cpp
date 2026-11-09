#include "Scenes/LeafDrawingServices.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
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
#include "Scenes/DaviasLeafTextureSet.h"
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
using Client::Core::Expected;
using Client::Render::Color;
using Client::Render::ModelBatch;
using Client::Render::ModelBatchParticleBackend;
using Client::Render::ModelBatchParticleState;
using Client::Render::TextureSlot;
using Client::Render::Models::OriginalMatrix;
using Client::Scenes::LeafBillboardRequest;
using Result = Client::Render::IParticleDrawBackend::Result;
constexpr OriginalMatrix Identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
const auto Failure = std::make_error_code(std::errc::io_error);

// Delega al owner REAL cargado; sólo registra sus lookups y permite perturbaciones síncronas.
class LibraryView final : public Client::Scenes::IModelLibrary
{
public:
    explicit LibraryView(const Client::Scenes::GameModels& owner) : m_owner(owner) {}
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
        if (missingMetadata)
            return std::nullopt;
        auto metadata = m_owner.TextureMetadataOf(static_cast<std::uint32_t>(selector));
        if (metadata && components)
            metadata->components = *components;
        return metadata;
    }
    std::optional<TextureSlot> TextureSlotOf(std::uint32_t selector) const override
    {
        bindingKeys.push_back(std::bit_cast<std::int32_t>(selector));
        return m_owner.TextureSlotOf(static_cast<std::uint32_t>(selector));
    }
    mutable std::vector<std::int32_t> bindingKeys;
    mutable std::vector<std::int32_t> metadataKeys;
    std::optional<std::uint8_t> components;
    bool missingMetadata = false;

private:
    const Client::Scenes::GameModels& m_owner;
};

// Toda frontera exitosa ejecuta el backend productivo; no sustituye su estado ni su envío.
class ObservedBackend final : public ModelBatchParticleBackend
{
public:
    using ModelBatchParticleBackend::ModelBatchParticleBackend;
    Result BindTexture(std::int32_t selector) override
    {
        ++bindCalls;
        const auto result = ModelBatchParticleBackend::BindTexture(selector);
        if (result.HasValue() && afterBind)
            afterBind(bindCalls);
        return result;
    }
    Result Begin() override
    {
        ++beginCalls;
        if (failBegin)
            return Client::Core::Unexpected(Failure);
        const auto result = ModelBatchParticleBackend::Begin();
        if (result.HasValue() && afterBegin)
            afterBegin();
        return result;
    }
    Result Color3(const glm::vec3& color) override
    {
        ++color3Calls;
        if (failColor)
            return Client::Core::Unexpected(Failure);
        return ModelBatchParticleBackend::Color3(color);
    }
    Result Color4(const Color& color) override
    {
        ++color4Calls;
        if (failColor)
            return Client::Core::Unexpected(Failure);
        return ModelBatchParticleBackend::Color4(color);
    }
    Result TexCoord(glm::vec2 uv) override
    {
        ++uvCalls;
        if (uvCalls == failUvAt)
            return Client::Core::Unexpected(Failure);
        return ModelBatchParticleBackend::TexCoord(uv);
    }
    Result Vertex(const glm::vec3& position) override
    {
        ++vertexCalls;
        if (vertexCalls == failVertexAt)
            return Client::Core::Unexpected(Failure);
        const auto result = ModelBatchParticleBackend::Vertex(position);
        if (result.HasValue() && afterVertex)
            afterVertex();
        return result;
    }
    Result End() override
    {
        ++endCalls;
        if (failEnd)
            return Client::Core::Unexpected(Failure);
        return ModelBatchParticleBackend::End();
    }
    std::function<void(int)> afterBind;
    std::function<void()> afterBegin;
    std::function<void()> afterVertex;
    int bindCalls = 0;
    int beginCalls = 0;
    int color3Calls = 0;
    int color4Calls = 0;
    int uvCalls = 0;
    int vertexCalls = 0;
    int endCalls = 0;
    int failUvAt = 0;
    int failVertexAt = 0;
    bool failBegin = false;
    bool failColor = false;
    bool failEnd = false;
};

class LeafDrawingServicesTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_batch.Begin(Client::Render::Pass3D{});
        const auto loaded =
            m_owner.LoadTextures(Client::Tests::GoldenDir(), Client::Scenes::DaviasLeafTextureSet, m_textures, m_log);
        ASSERT_EQ(loaded.uploaded, 3u);
    }
    Client::Scenes::GameModels m_owner;
    Client::Tests::FakeTextureStore m_textures;
    Client::Tests::RecordingSink m_sink;
    Client::Core::Log m_log{m_sink, Client::Core::LogLevel::Debug};
    LibraryView m_library{m_owner};
    ModelBatch m_batch;
    OriginalMatrix m_view = Identity;
    ObservedBackend m_backend{std::ref(m_batch), std::cref(m_library), ModelBatchParticleState::ForModelPass()};
    Client::Scenes::LeafDrawingServices m_services{std::cref(m_library), std::ref(m_backend), std::cref(m_view)};
    Client::World::Leaves m_leaves;
    std::int32_t m_map = 2;
    std::int32_t m_selector = 0;

    void Alive(std::size_t index, std::int32_t type = 100)
    {
        auto& leaf = m_leaves.All()[index];
        leaf.alive = 0x80;
        leaf.type = type;
        leaf.scale = 8;
        leaf.position = {10, 20, -30};
        leaf.light = {0.25f, 0.5f, 0.75f};
    }
    Result Draw()
    {
        return Client::Scenes::DrawWorldLeaves(m_batch, m_leaves, m_map, m_selector, &m_services);
    }
};

// Eliminar el binder interno, compactar el pool o perder el material rompe estas observaciones.
TEST_F(LeafDrawingServicesTest, RealOwnerAndBackendDrawAll200SlotsWithTwoBindersPerAliveSlot)
{
    Alive(0);
    Alive(80);
    Alive(199, 101);
    m_leaves.All()[80].position.x = 100;
    m_leaves.All()[199].position.x = 200;
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(m_backend.bindCalls, 6);
    EXPECT_EQ(m_library.bindingKeys, (std::vector<std::int32_t>{100, 101}));
    EXPECT_EQ(m_library.metadataKeys, (std::vector<std::int32_t>{100, 100, 101}));
    ASSERT_EQ(m_batch.Vertices().size(), 18u);
    using Operation = Client::Render::ModelStateOperation;
    constexpr std::array operations{Operation::Blend,   Operation::Cull, Operation::DepthWrite, Operation::Texture2d,
                                    Operation::Binding, Operation::Draw, Operation::Binding,    Operation::Draw};
    ASSERT_EQ(m_batch.Commands().size(), operations.size());
    for (std::size_t i = 0; i < operations.size(); ++i)
        EXPECT_EQ(m_batch.Commands()[i].operation, operations[i]);
    for (std::size_t i = 0; i < 5; ++i)
        EXPECT_EQ(m_batch.Commands()[i].vertexCount, 0u);
    EXPECT_EQ(m_batch.Commands()[5].vertexCount, 12u);
    EXPECT_EQ(m_batch.Commands()[5].state.texture, TextureSlot{100});
    EXPECT_EQ(m_batch.Commands()[6].vertexCount, 0u);
    EXPECT_EQ(m_batch.Commands()[7].vertexCount, 6u);
    EXPECT_EQ(m_batch.Commands()[7].state.texture, TextureSlot{101});
    // 5306a0 M0=2 llama620a90, confirmado por contrato y corrección del coordinador.
    EXPECT_EQ(m_batch.Commands()[0].state.blend, Client::Render::ModelBlend::Additive);
    EXPECT_EQ(m_batch.Commands()[0].state.space, Client::Render::ModelCoordinateSpace::Eye);
    EXPECT_EQ(m_batch.Vertices()[0].position, glm::vec3(6, 16, -30));
    EXPECT_EQ(m_batch.Vertices()[6].position, glm::vec3(96, 16, -30));
    EXPECT_EQ(m_batch.Vertices()[12].position, glm::vec3(196, 16, -30));
    EXPECT_EQ(m_batch.Vertices()[0].color, (Color{0.25f, 0.5f, 0.75f, 1}));
    EXPECT_EQ(m_backend.State().uv, glm::vec2(0, 0));
    EXPECT_EQ(m_backend.State().color, m_batch.Vertices()[0].color);
    EXPECT_EQ(m_backend.State().textureSelector, 101);
    EXPECT_FALSE(m_backend.IsDrawing());
}

TEST_F(LeafDrawingServicesTest, ScalarCapturePrecedesInternalBinderAndPositionViewFollowIt)
{
    Alive(0);
    LeafBillboardRequest request{100, m_leaves.All()[0].position, 8, 4, m_leaves.All()[0].light};
    m_backend.afterBind = [&](int ordinal)
    {
        EXPECT_EQ(ordinal, 1);
        request.selector = 101;
        request.width = 200;
        request.height = 300;
        request.rotation = 90;
        request.uv = {0.5f, 0.5f};
        m_leaves.All()[0].position = {30, 40, -50};
        m_view[3] = 7;
    };
    ASSERT_TRUE(m_services.Blend(Client::World::LeafBlend::Alpha).HasValue());
    ASSERT_TRUE(m_services.Billboard(m_batch, request).HasValue());
    EXPECT_EQ(m_library.metadataKeys, (std::vector<std::int32_t>{100}));
    ASSERT_EQ(m_batch.Vertices().size(), 6u);
    EXPECT_EQ(m_batch.Vertices()[0].position, glm::vec3(33, 38, -50));
    EXPECT_EQ(m_batch.Vertices()[1].position, glm::vec3(41, 38, -50));
    EXPECT_EQ(m_batch.Vertices()[0].uv, glm::vec2(0, 1));
    EXPECT_EQ(m_batch.Vertices()[2].uv, glm::vec2(1, 0));
}

TEST_F(LeafDrawingServicesTest, AliasedPositionColorAndMetadataAreConsumedAtDifferentBoundaries)
{
    glm::vec3 shared{10, 20, -30};
    const LeafBillboardRequest request{100, shared, 8, 4, shared};
    m_backend.afterBegin = [&]
    {
        EXPECT_TRUE(m_library.metadataKeys.empty());
        shared = {0.125f, 0.5f, 0.75f};
        m_library.components = 2; // El fallback no exige RGBA: es cualquier byte distinto de3.
        m_view[3] = 1000;
    };
    m_backend.afterVertex = [&] { shared = {900, 900, 900}; };
    ASSERT_TRUE(m_services.Blend(Client::World::LeafBlend::Alpha).HasValue());
    ASSERT_TRUE(m_services.Billboard(m_batch, request).HasValue());
    ASSERT_EQ(m_batch.Vertices().size(), 6u);
    const std::array expected{glm::vec3{6, 18, -30}, glm::vec3{14, 18, -30}, glm::vec3{14, 22, -30},
                              glm::vec3{6, 18, -30}, glm::vec3{14, 22, -30}, glm::vec3{6, 22, -30}};
    for (std::size_t k = 0; k < expected.size(); ++k)
    {
        EXPECT_EQ(m_batch.Vertices()[k].position, expected[k]);
        EXPECT_EQ(m_batch.Vertices()[k].color, (Color{0.125f, 0.5f, 0.75f, 0.125f}));
    }
    EXPECT_EQ(m_backend.color3Calls, 0);
    EXPECT_EQ(m_backend.color4Calls, 1);
}

TEST_F(LeafDrawingServicesTest, MissingMetadataAfterBeginKeepsTheOpenPrefixAndDoesNotCallEnd)
{
    Alive(199);
    m_library.missingMetadata = true;
    const auto result = Draw();
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error(), std::errc::no_such_file_or_directory);
    EXPECT_EQ(m_backend.bindCalls, 2);
    EXPECT_EQ(m_backend.beginCalls, 1);
    EXPECT_EQ(m_backend.endCalls, 0);
    EXPECT_TRUE(m_backend.IsDrawing());
    EXPECT_EQ(m_backend.State().textureSelector, 100);
    EXPECT_TRUE(m_batch.Vertices().empty());
}

TEST_F(LeafDrawingServicesTest, AFailedUvLeavesItsPrefixAndNeverPublishesAnIncompleteQuad)
{
    Alive(0);
    Alive(199);
    m_backend.failUvAt = 3;
    const auto result = Draw();
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error(), Failure);
    EXPECT_EQ(m_backend.uvCalls, 3);
    EXPECT_EQ(m_backend.vertexCalls, 2);
    EXPECT_EQ(m_backend.endCalls, 0);
    EXPECT_EQ(m_backend.State().uv, glm::vec2(1, 1));
    EXPECT_TRUE(m_backend.IsDrawing());
    using Operation = Client::Render::ModelStateOperation;
    constexpr std::array operations{Operation::Blend, Operation::Cull, Operation::DepthWrite, Operation::Texture2d,
                                    Operation::Binding};
    ASSERT_EQ(m_batch.Commands().size(), operations.size());
    for (std::size_t i = 0; i < operations.size(); ++i)
        EXPECT_EQ(m_batch.Commands()[i].operation, operations[i]);
    for (const auto& command : m_batch.Commands())
        EXPECT_EQ(command.vertexCount, 0u);
}

TEST_F(LeafDrawingServicesTest, HighSelectorNeverAliasesLow16AndFailedLookupKeepsTheCacheStore)
{
    Alive(0, 0x10064);
    const auto result = Draw();
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error(), std::errc::no_such_file_or_directory);
    EXPECT_EQ(m_library.bindingKeys, (std::vector<std::int32_t>{0x10064}));
    EXPECT_TRUE(m_library.metadataKeys.empty());
    EXPECT_EQ(m_backend.State().textureSelector, 0x10064);
    EXPECT_EQ(m_backend.bindCalls, 1);
    EXPECT_TRUE(m_batch.Vertices().empty());
    // Un hit posterior no vuelve a consultar la tabla, aunque el primer miss falló tras el store.
    EXPECT_TRUE(m_backend.BindTexture(0x10064).HasValue());
    EXPECT_EQ(m_library.bindingKeys.size(), 1u);
}

TEST_F(LeafDrawingServicesTest, NegativeNamesUseModularNegationWithoutAnyLogicalLookup)
{
    const std::array selectors{-1, -65537, std::numeric_limits<std::int32_t>::min()};
    const std::array<std::uint32_t, 3> names{1, 65537, 0x80000000u};
    for (std::size_t k = 0; k < selectors.size(); ++k)
    {
        ASSERT_TRUE(m_backend.BindTexture(selectors[k]).HasValue());
        EXPECT_EQ(m_backend.State().textureSelector, selectors[k]);
        EXPECT_FALSE(m_backend.State().texture.has_value());
        EXPECT_EQ(m_backend.State().rawTextureName, names[k]);
    }
    EXPECT_TRUE(m_library.bindingKeys.empty());
    Alive(0, -1);
    const auto result = Draw();
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error(), std::errc::no_such_file_or_directory);
    EXPECT_EQ(m_library.metadataKeys, (std::vector<std::int32_t>{-1}));
    EXPECT_TRUE(m_backend.IsDrawing());
}

TEST_F(LeafDrawingServicesTest, AMapChangeAfterTheOuterBinderSelectsTheRealGenericRoute)
{
    Alive(0);
    m_backend.afterBind = [&](int) { m_map = 0; };
    const auto result = Draw();
    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(m_backend.bindCalls, 1);
    EXPECT_EQ(m_backend.beginCalls, 0);
    ASSERT_EQ(m_batch.Vertices().size(), 6u);
    EXPECT_EQ(m_batch.Vertices()[0].position, glm::vec3(7, 17, -33));
    EXPECT_EQ(m_batch.Commands().back().state.space, Client::Render::ModelCoordinateSpace::World);
    EXPECT_FALSE(m_backend.HasPushedMatrix());
}

TEST_F(LeafDrawingServicesTest, ColorUvPersistAcrossFramesAndTheNewPassReacquiresBindingKnowledge)
{
    Alive(0);
    ASSERT_TRUE(Draw().HasValue());
    const auto state = m_backend.State();
    m_batch.Begin(Client::Render::Pass3D{});
    EXPECT_EQ(m_backend.State().color, state.color);
    EXPECT_EQ(m_backend.State().uv, state.uv);
    m_leaves.All()[0].scale = 0;
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(m_backend.bindCalls, 4);
    EXPECT_EQ(m_library.bindingKeys, (std::vector<std::int32_t>{100, 100}));
    EXPECT_EQ(m_batch.Vertices().size(), 6u);
    for (const auto& vertex : m_batch.Vertices())
        EXPECT_EQ(vertex.position, m_leaves.All()[0].position);
}

TEST_F(LeafDrawingServicesTest, MapZeroChangedByTheBinderKeepsAlphaAndCapturesScaleOnceForBothSides)
{
    Alive(0);
    m_map = 0;
    m_backend.afterBind = [&](int ordinal)
    {
        if (ordinal == 1)
        {
            EXPECT_EQ(m_backend.State().blendMode, 2);
            EXPECT_EQ(m_backend.State().color, (Color{1, 1, 1, 1}));
            m_map = 2;
        }
        else
        {
            m_leaves.All()[0].scale = 100;
            m_leaves.All()[0].position = {30, 40, -50};
        }
    };
    ASSERT_TRUE(Draw().HasValue());
    ASSERT_EQ(m_batch.Vertices().size(), 6u);
    EXPECT_EQ(m_batch.Commands()[0].state.blend, Client::Render::ModelBlend::Alpha);
    EXPECT_EQ(m_batch.Vertices()[0].position, glm::vec3(26, 36, -50));
    EXPECT_EQ(m_batch.Vertices()[2].position, glm::vec3(34, 44, -50));
    EXPECT_EQ(m_backend.bindCalls, 2);
}

TEST_F(LeafDrawingServicesTest, AdditivePrecedesWhiteAndPreservesNoncanonicalTextureAndFogPolicy)
{
    auto initial = ModelBatchParticleState::ForModelPass();
    initial.cullEnabled = 0x80;
    initial.depthWrite = 0x80;
    initial.alphaTest = 0x80;
    initial.texture2d = 0x80;
    initial.fogPolicy = 0x80;
    ObservedBackend actual{std::ref(m_batch), std::cref(m_library), initial};
    Client::Scenes::LeafDrawingServices provider{std::cref(m_library), std::ref(actual), std::cref(m_view)};
    Alive(0);
    actual.afterBind = [&](int)
    {
        EXPECT_EQ(actual.State().blendMode, 3);
        EXPECT_EQ(actual.State().cullEnabled, 0);
        EXPECT_EQ(actual.State().depthWrite, 0);
        EXPECT_EQ(actual.State().alphaTest, 0);
        EXPECT_EQ(actual.State().texture2d, 0x80);
        EXPECT_EQ(actual.State().fogPolicy, 0x80); // Disable no baja la política persistente.
        EXPECT_TRUE(actual.State().depthTest);
        EXPECT_EQ(actual.State().color, (Color{1, 1, 1, 1}));
    };
    ASSERT_TRUE(Client::Scenes::DrawWorldLeaves(m_batch, m_leaves, m_map, m_selector, &provider).HasValue());
    using Operation = Client::Render::ModelStateOperation;
    constexpr std::array operations{Operation::Blend, Operation::Cull, Operation::DepthWrite, Operation::Binding,
                                    Operation::Draw};
    ASSERT_EQ(m_batch.Commands().size(), operations.size());
    for (std::size_t i = 0; i < operations.size(); ++i)
        EXPECT_EQ(m_batch.Commands()[i].operation, operations[i]);
    EXPECT_EQ(m_batch.Commands().back().state.blend, Client::Render::ModelBlend::Additive);
}

// Sin esta publicación, un pase vacío perdería los efectos de620a90 en el renderer real.
TEST_F(LeafDrawingServicesTest, AnEmptyPoolStillPublishesItsMaterialStateWithoutGeometry)
{
    ASSERT_TRUE(Draw().HasValue());
    using Operation = Client::Render::ModelStateOperation;
    constexpr std::array operations{Operation::Blend, Operation::Cull, Operation::DepthWrite, Operation::Texture2d};
    ASSERT_EQ(m_batch.Commands().size(), operations.size());
    for (std::size_t i = 0; i < operations.size(); ++i)
    {
        EXPECT_EQ(m_batch.Commands()[i].operation, operations[i]);
        EXPECT_EQ(m_batch.Commands()[i].vertexCount, 0u);
    }
    EXPECT_EQ(m_batch.Commands()[0].state.cullEnabled, true);
    EXPECT_EQ(m_batch.Commands()[0].state.depthWrite, true);
    EXPECT_EQ(m_batch.Commands()[1].state.cullEnabled, false);
    EXPECT_EQ(m_batch.Commands()[1].state.depthWrite, true);
    EXPECT_EQ(m_batch.Commands()[2].state.depthWrite, false);
    EXPECT_EQ(m_batch.Commands()[2].state.texture2d, false);
    EXPECT_EQ(m_batch.Commands()[3].state.texture2d, true);
    EXPECT_EQ(m_batch.Commands()[0].state.blend, Client::Render::ModelBlend::Additive);
    EXPECT_EQ(m_backend.State().color, (Color{1, 1, 1, 1}));
    EXPECT_TRUE(m_batch.Vertices().empty());
    EXPECT_EQ(m_backend.bindCalls, 0);
}

TEST_F(LeafDrawingServicesTest, BeginFailurePreservesBindingAndGeometryDoesNotReadColorMetadata)
{
    Alive(0);
    m_backend.failBegin = true;
    const auto result = Draw();
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error(), Failure);
    EXPECT_EQ(m_backend.bindCalls, 2);
    EXPECT_TRUE(m_library.metadataKeys.empty());
    EXPECT_EQ(m_backend.endCalls, 0);
    EXPECT_FALSE(m_backend.IsDrawing());
    EXPECT_EQ(m_backend.State().textureSelector, 100);
    EXPECT_TRUE(m_batch.Vertices().empty());
}

TEST_F(LeafDrawingServicesTest, ColorFailureAfterBeginDoesNotEmitUvOrEnd)
{
    Alive(0);
    m_backend.afterBegin = [&] { m_backend.failColor = true; };
    const auto result = Draw();
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error(), Failure);
    EXPECT_EQ(m_backend.uvCalls, 0);
    EXPECT_EQ(m_backend.endCalls, 0);
    EXPECT_TRUE(m_backend.IsDrawing());
    EXPECT_EQ(m_library.metadataKeys, (std::vector<std::int32_t>{100}));
    EXPECT_EQ(m_backend.State().color, (Color{1, 1, 1, 1}));
}

TEST_F(LeafDrawingServicesTest, VertexAndEndFailuresKeepTheirExactCompletedPrefixes)
{
    Alive(0);
    m_backend.failVertexAt = 4;
    const auto result = Draw();
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error(), Failure);
    EXPECT_EQ(m_backend.uvCalls, 4);
    EXPECT_EQ(m_backend.vertexCalls, 4);
    EXPECT_EQ(m_backend.endCalls, 0);
    EXPECT_EQ(m_backend.State().uv, glm::vec2(0, 0));
    EXPECT_TRUE(m_backend.IsDrawing());
    EXPECT_TRUE(m_batch.Vertices().empty());

    ModelBatch other;
    other.Begin(Client::Render::Pass3D{});
    ObservedBackend failedEnd{std::ref(other), std::cref(m_library), ModelBatchParticleState::ForModelPass()};
    failedEnd.failEnd = true;
    Client::Scenes::LeafDrawingServices provider{std::cref(m_library), std::ref(failedEnd), std::cref(m_view)};
    const auto ended = Client::Scenes::DrawWorldLeaves(other, m_leaves, m_map, m_selector, &provider);
    ASSERT_FALSE(ended.HasValue());
    EXPECT_EQ(ended.Error(), Failure);
    EXPECT_EQ(failedEnd.vertexCalls, 4);
    EXPECT_EQ(failedEnd.endCalls, 1);
    EXPECT_TRUE(failedEnd.IsDrawing());
    EXPECT_TRUE(other.Vertices().empty());
}

TEST_F(LeafDrawingServicesTest, UnsupportedFamiliesAndMismatchedBatchesFailAtTheirReachedBoundary)
{
    ASSERT_TRUE(m_backend.SubtractivePrefix().HasValue());
    ASSERT_EQ(m_batch.Commands().size(), 1u);
    EXPECT_EQ(m_batch.Commands()[0].operation, Client::Render::ModelStateOperation::Blend);
    EXPECT_EQ(m_batch.Commands()[0].state.blend, Client::Render::ModelBlend::Subtractive);
    EXPECT_EQ(m_backend.State().blendMode, 4);
    EXPECT_EQ(m_backend.State().cullEnabled, 1);
    EXPECT_EQ(m_backend.State().depthWrite, 1);
    EXPECT_EQ(m_backend.State().texture2d, 0);
    ASSERT_TRUE(m_backend.SubtractiveTail().HasValue());
    ASSERT_EQ(m_batch.Commands().size(), 2u);
    EXPECT_EQ(m_batch.Commands()[1].operation, Client::Render::ModelStateOperation::Texture2d);
    EXPECT_EQ(m_backend.State().texture2d, 1);
    EXPECT_EQ(m_backend.State().alphaTest, 0);
    EXPECT_EQ(m_backend.State().cullEnabled, 1); // Tail no ejecuta los helpers f0/b0.
    EXPECT_EQ(m_backend.State().depthWrite, 1);
    EXPECT_FALSE(m_backend.Type620850().HasValue());
    EXPECT_FALSE(m_backend.Type620870().HasValue());
    EXPECT_EQ(m_batch.Commands().size(), 2u);
    // Push/Translate/Quad son ahora servicios reales; sus errores se prueban en GenericLeafDrawingTest.
    EXPECT_FALSE(m_services.PopMatrix().HasValue());
    EXPECT_FALSE(m_backend.End().HasValue());
    EXPECT_FALSE(m_backend.Vertex(glm::vec3{}).HasValue());
    ModelBatch other;
    glm::vec3 value{};
    const auto result = m_services.Billboard(other, LeafBillboardRequest{100, value, 1, 1, value});
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error(), std::errc::invalid_argument);
    EXPECT_EQ(m_backend.bindCalls, 0);
}

TEST_F(LeafDrawingServicesTest, AlphaInheritsDepthAndFogStillFailsAfterTheNativePrefix)
{
    auto initial = ModelBatchParticleState::ForModelPass();
    initial.depthWrite = 0;
    m_batch.SetState({std::nullopt, Client::Render::ModelBlend::Shadow, true});
    ModelBatchParticleBackend inherited{std::ref(m_batch), std::cref(m_library), initial};
    const auto alpha = inherited.AlphaInheritDepth();
    ASSERT_TRUE(alpha.HasValue());
    using Operation = Client::Render::ModelStateOperation;
    constexpr std::array operations{Operation::Blend, Operation::Cull, Operation::AlphaTest, Operation::Texture2d};
    ASSERT_EQ(m_batch.Commands().size(), operations.size());
    for (std::size_t i = 0; i < operations.size(); ++i)
    {
        EXPECT_EQ(m_batch.Commands()[i].operation, operations[i]);
        EXPECT_EQ(m_batch.Commands()[i].state.depthWrite, false);
        EXPECT_EQ(m_batch.Commands()[i].vertexCount, 0u);
    }
    EXPECT_EQ(inherited.State().blendMode, 2);
    EXPECT_EQ(inherited.State().cullEnabled, 0);
    EXPECT_EQ(inherited.State().alphaTest, 1);
    EXPECT_EQ(inherited.State().texture2d, 1);
    EXPECT_EQ(inherited.State().depthWrite, 0);
    initial.fogPolicy = 0x80;
    ModelBatchParticleBackend fog{std::ref(m_batch), std::cref(m_library), initial};
    const auto result = fog.AlphaWriteDepth();
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error(), std::errc::function_not_supported);
    EXPECT_EQ(fog.State().depthWrite, 1);
    EXPECT_EQ(fog.State().fogPolicy, 0x80);
    ASSERT_EQ(m_batch.Commands().size(), operations.size() + 1);
    EXPECT_EQ(m_batch.Commands().back().operation, Operation::DepthWrite);
    EXPECT_EQ(m_batch.Commands().back().state.depthWrite, true);
    EXPECT_TRUE(m_batch.Vertices().empty());
}

TEST_F(LeafDrawingServicesTest, ASecondPassWithAnInterleavedDrawerReacquiresBindingAndActualDepthState)
{
    Alive(0);
    ASSERT_TRUE(Draw().HasValue());
    Client::Render::Pass3D second;
    second.view[3][0] = 20;
    m_batch.Begin(second);
    m_view[3] = 20; // El owner conserva la misma matriz durable, con el pase real actualizado.
    m_batch.SetState({TextureSlot{101}, Client::Render::ModelBlend::Alpha, false});
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(m_library.bindingKeys, (std::vector<std::int32_t>{100, 100}));
    EXPECT_EQ(m_backend.bindCalls, 4);
    ASSERT_EQ(m_batch.Vertices().size(), 6u);
    EXPECT_EQ(m_batch.Vertices()[0].position, glm::vec3(26, 16, -30));
    EXPECT_EQ(m_batch.Commands().back().state.texture, TextureSlot{100});
    EXPECT_FALSE(m_batch.Commands().back().state.depthTest);
    EXPECT_FALSE(m_backend.State().depthTest);
}

TEST_F(LeafDrawingServicesTest, OwnUpdatesPreserveHitsButAnEqualForeignSetStateLosesRawKeyKnowledge)
{
    Alive(0);
    ASSERT_TRUE(Draw().HasValue());
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(m_library.bindingKeys, (std::vector<std::int32_t>{100}));
    m_batch.SetState(m_batch.Commands().back().state); // Igual valor, distinto productor.
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(m_library.bindingKeys, (std::vector<std::int32_t>{100, 100}));
    EXPECT_EQ(m_backend.bindCalls, 6);
}

TEST_F(LeafDrawingServicesTest, ResourceInvalidationRejectsPendingWorkAndCoversSuccessfulAndFailedLoads)
{
    Alive(0);
    ASSERT_TRUE(Draw().HasValue());
    const auto pending = m_batch.InvalidateTextureKnowledge();
    ASSERT_FALSE(pending.HasValue());
    EXPECT_EQ(pending.Error(), std::errc::operation_in_progress);
    m_batch.Begin(Client::Render::Pass3D{});
    ASSERT_TRUE(m_batch.InvalidateTextureKnowledge().HasValue());
    const auto onlyOzt = std::span(Client::Scenes::DaviasLeafTextureSet).first(1);
    ASSERT_EQ(m_owner.LoadTextures(Client::Tests::GoldenDir(), onlyOzt, m_textures, m_log).uploaded, 1u);
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(m_library.bindingKeys, (std::vector<std::int32_t>{100, 100}));
    EXPECT_EQ(m_batch.Vertices()[0].color.alpha, 0.25f); // Metadata nueva4 después de Begin.
    m_batch.Begin(Client::Render::Pass3D{});
    ASSERT_TRUE(m_batch.InvalidateTextureKnowledge().HasValue());
    const auto failed =
        m_owner.LoadTextures(Client::Tests::GoldenDir() / "missing-world-leaf-root", onlyOzt, m_textures, m_log);
    EXPECT_EQ(failed.uploaded, 0u);
    EXPECT_EQ(failed.skipped, 1u);
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(m_library.bindingKeys, (std::vector<std::int32_t>{100, 100, 100}));
    EXPECT_EQ(m_batch.Vertices()[0].color.alpha, 0.25f);
}

TEST_F(LeafDrawingServicesTest, ForeignStateInsideAQuadFailsWithoutGeometryOrFictitiousClosure)
{
    Alive(0);
    m_backend.afterBegin = [&] { m_batch.SetState({TextureSlot{101}, Client::Render::ModelBlend::Opaque, false}); };
    const auto result = Draw();
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error(), std::errc::state_not_recoverable);
    EXPECT_TRUE(m_backend.IsDrawing());
    EXPECT_EQ(m_backend.endCalls, 0);
    EXPECT_TRUE(m_batch.Vertices().empty());
    const auto invalidated = m_batch.InvalidateTextureKnowledge();
    ASSERT_FALSE(invalidated.HasValue());
    EXPECT_EQ(invalidated.Error(), std::errc::operation_in_progress);
}

TEST_F(LeafDrawingServicesTest, ACacheHitStillReadsChangedColorMetadataWithoutAnotherPhysicalLookup)
{
    Alive(0);
    ASSERT_TRUE(Draw().HasValue());
    m_library.components = 4; // Perturbación de metadata CPU, sin carga/liberación física.
    ASSERT_TRUE(Draw().HasValue());
    EXPECT_EQ(m_library.bindingKeys, (std::vector<std::int32_t>{100}));
    EXPECT_EQ(m_library.metadataKeys, (std::vector<std::int32_t>{100, 100}));
    ASSERT_EQ(m_batch.Vertices().size(), 12u);
    EXPECT_EQ(m_batch.Vertices()[0].color.alpha, 1);
    EXPECT_EQ(m_batch.Vertices()[6].color.alpha, 0.25f);
}

TEST_F(LeafDrawingServicesTest, InvalidForeignStateStopsBeforeAssumingAnyBindingOrExecutingTheLeafPrefix)
{
    Client::Render::ModelDrawState state{TextureSlot{101}, Client::Render::ModelBlend::Alpha, false};
    state.rawTextureName = 0u;
    m_batch.SetState(state);
    const auto result = Draw();
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error(), std::errc::invalid_argument);
    EXPECT_FALSE(m_backend.State().textureSelector.has_value());
    EXPECT_FALSE(m_backend.State().texture.has_value());
    EXPECT_FALSE(m_backend.State().rawTextureName.has_value());
    EXPECT_EQ(m_backend.bindCalls, 0);
    EXPECT_EQ(m_backend.color3Calls, 0);
    state.rawTextureName.reset();
    state.blend = static_cast<Client::Render::ModelBlend>(0xff);
    m_batch.SetState(state);
    const auto unsupported = Draw();
    ASSERT_FALSE(unsupported.HasValue());
    EXPECT_EQ(unsupported.Error(), std::errc::function_not_supported);
}

TEST_F(LeafDrawingServicesTest, CompatibleImportedCapabilitiesKeepExplicitNoncanonicalCacheBytes)
{
    auto initial = ModelBatchParticleState::ForModelPass();
    initial.cullEnabled = 0;
    initial.depthWrite = 0x80;
    initial.alphaTest = 0x80;
    initial.texture2d = 0x80;
    m_batch.SetState({std::nullopt, Client::Render::ModelBlend::Alpha, false});
    ModelBatchParticleBackend actual{std::ref(m_batch), std::cref(m_library), initial};
    ASSERT_TRUE(actual.AlphaWriteDepth().HasValue());
    EXPECT_EQ(actual.State().depthWrite, 0x80);
    EXPECT_EQ(actual.State().alphaTest, 0x80);
    EXPECT_EQ(actual.State().texture2d, 0x80);
    EXPECT_FALSE(actual.State().depthTest);
}
} // namespace
