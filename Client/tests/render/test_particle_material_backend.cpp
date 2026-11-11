#include "Render/ModelBatchParticleBackend.h"

#include "Core/Expected.h"
#include "Data/Textures/Texture.h"
#include "Render/ModelBatch.h"
#include "Render/ModelBatchParticleState.h"
#include "Render/Pass3D.h"
#include "Scenes/CharacterDrawing.h"
#include "Data/Models/Model.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureEnvMode.h"
#include "Render/TextureSlot.h"
#include "Scenes/TextureMetadata.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <array>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <optional>
#include <span>
#include <system_error>
#include <type_traits>

namespace
{
class MaterialModels : public Client::Scenes::IModelLibrary
{
public:
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
};

class SlotLookupModels : public MaterialModels
{
public:
    std::optional<Client::Render::TextureSlot> TextureSlotOf(std::uint32_t selector) const override
    {
        m_slotSelector = selector;
        return Client::Render::TextureSlot{41};
    }
    std::optional<Client::Scenes::TextureMetadata> TextureMetadataOf(std::uint32_t) const override
    {
        ++m_metadataQueries;
        Client::Data::Textures::Texture texture;
        texture.textureWidth = 32;
        texture.textureHeight = 16;
        return Client::Scenes::TextureMetadata(texture);
    }

    [[nodiscard]] const std::optional<std::uint32_t>& SlotSelector() const
    {
        return m_slotSelector;
    }
    [[nodiscard]] int MetadataQueries() const
    {
        return m_metadataQueries;
    }

private:
    mutable std::optional<std::uint32_t> m_slotSelector;
    mutable int m_metadataQueries = 0;
};

TEST(ParticleMaterialBackendTest, PhysicalSlotLookupKeepsPublicUnsignedRaw32Api)
{
    using SlotLookup =
        std::optional<Client::Render::TextureSlot> (Client::Scenes::IModelLibrary::*)(std::uint32_t) const;
    using MetadataLookup =
        std::optional<Client::Scenes::TextureMetadata> (Client::Scenes::IModelLibrary::*)(std::uint32_t) const;
    EXPECT_TRUE((std::is_same_v<decltype(&Client::Scenes::IModelLibrary::TextureSlotOf), SlotLookup>));
    EXPECT_TRUE((std::is_same_v<decltype(&Client::Scenes::IModelLibrary::TextureMetadataOf), MetadataLookup>));
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    SlotLookupModels models;
    Client::Render::ModelBatchParticleBackend backend(std::ref(batch), std::cref(models),
                                                      Client::Render::ModelBatchParticleState::ForModelPass());
    ASSERT_TRUE(backend.SubtractivePrefix().HasValue());
    const auto serial = batch.StateSerial();
    ASSERT_TRUE(backend.BindTexture(0x12345678).HasValue());
    EXPECT_EQ(models.SlotSelector(), 0x12345678U);
    EXPECT_EQ(models.MetadataQueries(), 0);
    EXPECT_EQ(backend.State().textureSelector, 0x12345678);
    EXPECT_EQ(backend.State().texture, Client::Render::TextureSlot{41});
    EXPECT_FALSE(backend.State().rawTextureName.has_value());
    EXPECT_EQ(backend.State().blendMode, 4);
    EXPECT_EQ(backend.State().cullEnabled, 1);
    EXPECT_EQ(backend.State().depthWrite, 1);
    ASSERT_EQ(batch.Commands().size(), 2);
    EXPECT_EQ(batch.Commands().back().operation, Client::Render::ModelStateOperation::Binding);
    EXPECT_EQ(batch.Commands().back().state.texture, Client::Render::TextureSlot{41});
    EXPECT_EQ(batch.StateSerial(), serial + 1);
    ASSERT_TRUE(backend.BindTexture(0x12345678).HasValue());
    EXPECT_EQ(batch.StateSerial(), serial + 1);
    EXPECT_TRUE(batch.Vertices().empty());
}

TEST(ParticleMaterialBackendTest, NegativeHighBitSelectorsKeepRawBindingWithoutPublicLookup)
{
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    SlotLookupModels models;
    Client::Render::ModelBatchParticleBackend backend(std::ref(batch), std::cref(models),
                                                      Client::Render::ModelBatchParticleState::ForModelPass());
    ASSERT_TRUE(backend.SubtractivePrefix().HasValue());
    // NEG32 no consulta la tabla pública: 0xfedcba98 produce nombre físico0x01234568.
    ASSERT_TRUE(backend.BindTexture(-0x01234568).HasValue());
    ASSERT_TRUE(backend.State().textureSelector.has_value());
    EXPECT_EQ(static_cast<std::uint32_t>(*backend.State().textureSelector), 0xfedcba98U);
    EXPECT_EQ(backend.State().rawTextureName, 0x01234568U);
    EXPECT_FALSE(backend.State().texture.has_value());
    const auto serial = batch.StateSerial();
    ASSERT_TRUE(backend.BindTexture(-0x01234568).HasValue());
    EXPECT_EQ(batch.StateSerial(), serial);
    ASSERT_TRUE(backend.BindTexture(INT32_MIN).HasValue());
    EXPECT_EQ(backend.State().rawTextureName, 0x80000000U);
    EXPECT_EQ(backend.State().blendMode, 4);
    EXPECT_EQ(backend.State().cullEnabled, 1);
    EXPECT_EQ(backend.State().depthWrite, 1);
    EXPECT_FALSE(models.SlotSelector().has_value());
    EXPECT_EQ(models.MetadataQueries(), 0);
    EXPECT_TRUE(batch.Vertices().empty());
}

// Detecta forzar620890 en raw0 o rechazar depthWrite0: expectativa del contrato620a00.
TEST(ParticleMaterialBackendTest, RawZeroInheritsDisabledDepthWrite)
{
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    MaterialModels models;
    Client::Render::ModelBatchParticleBackend backend(std::ref(batch), std::cref(models),
                                                      Client::Render::ModelBatchParticleState::ForModelPass());
    ASSERT_TRUE(backend.Additive().HasValue());
    ASSERT_TRUE(backend.AlphaInheritDepth().HasValue());
    EXPECT_EQ(backend.State().depthWrite, 0);
    EXPECT_EQ(backend.State().alphaTest, 1);
    EXPECT_EQ(backend.State().blendMode, 2);
    EXPECT_FALSE(batch.Commands().empty());
}

// Detecta adelantar f0/b0/tail al prefijo o dejarlo como Unsupported.
TEST(ParticleMaterialBackendTest, SubtractivePrefixPreservesSuffixState)
{
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    MaterialModels models;
    Client::Render::ModelBatchParticleBackend backend(std::ref(batch), std::cref(models),
                                                      Client::Render::ModelBatchParticleState::ForModelPass());
    ASSERT_TRUE(backend.SubtractivePrefix().HasValue());
    EXPECT_EQ(backend.State().blendMode, 4);
    EXPECT_EQ(backend.State().cullEnabled, 1);
    EXPECT_EQ(backend.State().depthWrite, 1);
    EXPECT_EQ(backend.State().texture2d, 0);
    ASSERT_TRUE(backend.Helper6208f0().HasValue());
    ASSERT_TRUE(backend.Helper6208b0().HasValue());
    ASSERT_TRUE(backend.SubtractiveTail().HasValue());
    EXPECT_EQ(backend.State().cullEnabled, 0);
    EXPECT_EQ(backend.State().depthWrite, 0);
    EXPECT_EQ(backend.State().texture2d, 1);
    EXPECT_EQ(backend.State().alphaTest, 0);
    EXPECT_TRUE(batch.Vertices().empty());
}

TEST(ParticleMaterialBackendTest, SubtractivePublishesOnlyReachedOperationsInOrder)
{
    using Client::Render::ModelStateOperation;
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    MaterialModels models;
    Client::Render::ModelBatchParticleBackend backend(std::ref(batch), std::cref(models),
                                                      Client::Render::ModelBatchParticleState::ForModelPass());
    ASSERT_TRUE(backend.AlphaWriteDepth().HasValue());
    const auto first = batch.Commands().size();
    ASSERT_TRUE(backend.SubtractivePrefix().HasValue());
    ASSERT_EQ(batch.Commands().size(), first + 1);
    EXPECT_EQ(batch.Commands().back().operation, ModelStateOperation::Blend);
    EXPECT_EQ(batch.Commands().back().state.alphaTest, true);
    EXPECT_EQ(batch.Commands().back().state.depthWrite, true);
    ASSERT_TRUE(backend.Helper6208f0().HasValue());
    ASSERT_TRUE(backend.Helper6208b0().HasValue());
    ASSERT_TRUE(backend.SubtractiveTail().HasValue());
    ASSERT_EQ(batch.Commands().size(), first + 3);
    EXPECT_EQ(batch.Commands()[first + 1].operation, ModelStateOperation::DepthWrite);
    EXPECT_EQ(batch.Commands()[first + 2].operation, ModelStateOperation::AlphaTest);
    const auto serial = batch.StateSerial();
    ASSERT_TRUE(backend.SubtractivePrefix().HasValue());
    ASSERT_TRUE(backend.Helper6208b0().HasValue());
    ASSERT_TRUE(backend.SubtractiveTail().HasValue());
    EXPECT_EQ(batch.StateSerial(), serial);
}

TEST(ParticleMaterialBackendTest, FogFailureRetainsReachedPrefixAndStopsBeforeGeometry)
{
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    MaterialModels models;
    auto initial = Client::Render::ModelBatchParticleState::ForModelPass();
    initial.fogPolicy = 2;
    Client::Render::ModelBatchParticleBackend backend(std::ref(batch), std::cref(models), initial);
    const auto result = backend.AlphaInheritDepth();
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error(), std::make_error_code(std::errc::function_not_supported));
    EXPECT_EQ(backend.State().blendMode, 2);
    EXPECT_EQ(backend.State().alphaTest, 1);
    EXPECT_EQ(backend.State().texture2d, 1);
    EXPECT_EQ(backend.State().fogPolicy, 2);
    ASSERT_EQ(batch.Commands().size(), 4);
    EXPECT_EQ(batch.Commands().back().operation, Client::Render::ModelStateOperation::Texture2d);
    EXPECT_TRUE(batch.Vertices().empty());
}

TEST(ParticleMaterialBackendTest, OtherWriterResynchronizesInheritedAxesWithoutGuessingSelector)
{
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    MaterialModels models;
    Client::Render::ModelBatchParticleBackend backend(std::ref(batch), std::cref(models),
                                                      Client::Render::ModelBatchParticleState::ForModelPass());
    ASSERT_TRUE(backend.AlphaWriteDepth().HasValue());
    ASSERT_TRUE(backend.BindTexture(-7).HasValue());
    auto other = batch.State();
    other.depthWrite = false;
    batch.SetState(other);
    ASSERT_TRUE(backend.AlphaInheritDepth().HasValue());
    EXPECT_EQ(backend.State().depthWrite, 0);
    EXPECT_FALSE(backend.State().textureSelector.has_value());
    EXPECT_EQ(backend.State().rawTextureName, 7);
    EXPECT_EQ(batch.State().alphaTest, true);
}

TEST(ParticleMaterialBackendTest, NegativeSelectorsRemainRawAndMaterialNeverRebindsThem)
{
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    MaterialModels models;
    Client::Render::ModelBatchParticleBackend backend(std::ref(batch), std::cref(models),
                                                      Client::Render::ModelBatchParticleState::ForModelPass());
    ASSERT_TRUE(backend.BindTexture(INT32_MIN).HasValue());
    EXPECT_EQ(batch.State().rawTextureName, 0x80000000u);
    const auto serial = batch.StateSerial();
    ASSERT_TRUE(backend.BindTexture(INT32_MIN).HasValue());
    EXPECT_EQ(batch.StateSerial(), serial);
    ASSERT_TRUE(backend.Additive().HasValue());
    EXPECT_EQ(batch.State().rawTextureName, 0x80000000u);
    ASSERT_TRUE(backend.Begin().HasValue());
    const auto before = batch.StateSerial();
    EXPECT_FALSE(backend.SubtractivePrefix().HasValue());
    EXPECT_FALSE(backend.Helper6208f0().HasValue());
    EXPECT_FALSE(backend.Helper6208b0().HasValue());
    EXPECT_FALSE(backend.SubtractiveTail().HasValue());
    EXPECT_EQ(batch.StateSerial(), before);
    EXPECT_FALSE(backend.Type620850().HasValue());
    EXPECT_FALSE(backend.Type620870().HasValue());
}

TEST(ParticleMaterialBackendTest, RawOneRestoresWriteAndPreservesNoncanonicalTrueBytes)
{
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    MaterialModels models;
    auto initial = Client::Render::ModelBatchParticleState::ForModelPass();
    initial.texture2d = 0x80;
    Client::Render::ModelBatchParticleBackend backend(std::ref(batch), std::cref(models), initial);
    ASSERT_TRUE(backend.Additive().HasValue());
    ASSERT_TRUE(backend.AlphaWriteDepth().HasValue());
    EXPECT_EQ(backend.State().depthWrite, 1);
    EXPECT_EQ(backend.State().alphaTest, 1);
    EXPECT_EQ(backend.State().texture2d, 0x80);
    const auto serial = batch.StateSerial();
    ASSERT_TRUE(backend.AlphaInheritDepth().HasValue());
    EXPECT_EQ(backend.State().depthWrite, 1);
    EXPECT_EQ(batch.StateSerial(), serial);
    EXPECT_EQ(batch.State().depthWrite, true);
}

TEST(ParticleMaterialBackendTest, MissingTextureKeepsCacheAndDoesNotUndoMaterialPrefix)
{
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    MaterialModels models;
    Client::Render::ModelBatchParticleBackend backend(std::ref(batch), std::cref(models),
                                                      Client::Render::ModelBatchParticleState::ForModelPass());
    ASSERT_TRUE(backend.AlphaWriteDepth().HasValue());
    const auto serial = batch.StateSerial();
    const auto missing = backend.BindTexture(123456);
    ASSERT_FALSE(missing.HasValue());
    EXPECT_EQ(missing.Error(), std::make_error_code(std::errc::no_such_file_or_directory));
    EXPECT_EQ(backend.State().textureSelector, 123456);
    EXPECT_EQ(backend.State().blendMode, 2);
    EXPECT_EQ(batch.StateSerial(), serial);
    ASSERT_TRUE(backend.BindTexture(123456).HasValue()); // hit no reintenta lookup tras fallo.
    EXPECT_EQ(batch.StateSerial(), serial);
    EXPECT_TRUE(batch.Vertices().empty());
}

// Cada snapshot representa sólo el prefijo alcanzado; Fog no0 falla incluso si el tail hace hit.
TEST(ParticleMaterialBackendTest, SubtractiveFogMissAndHitKeepExactOperationTraceAndSerial)
{
    using Client::Render::ModelStateOperation;
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    auto draw = batch.State();
    draw.alphaTest = true;
    batch.SetState(draw);
    auto initial = Client::Render::ModelBatchParticleState::ForModelPass();
    initial.alphaTest = 0x80;
    initial.fogPolicy = 7;
    MaterialModels models;
    Client::Render::ModelBatchParticleBackend backend(std::ref(batch), std::cref(models), initial);
    const auto firstSerial = batch.StateSerial();
    ASSERT_TRUE(backend.SubtractivePrefix().HasValue());
    EXPECT_EQ(batch.StateSerial(), firstSerial + 1);
    EXPECT_EQ(backend.State().alphaTest, 0x80);
    ASSERT_TRUE(backend.Helper6208f0().HasValue());
    EXPECT_EQ(batch.StateSerial(), firstSerial + 2);
    ASSERT_TRUE(backend.Helper6208b0().HasValue());
    EXPECT_EQ(batch.StateSerial(), firstSerial + 3);
    const auto result = backend.SubtractiveTail();
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error(), std::make_error_code(std::errc::function_not_supported));
    EXPECT_EQ(batch.StateSerial(), firstSerial + 5);
    const std::array<ModelStateOperation, 5> ops{ModelStateOperation::Blend, ModelStateOperation::Cull,
                                                 ModelStateOperation::DepthWrite, ModelStateOperation::AlphaTest,
                                                 ModelStateOperation::Texture2d};
    const std::array<std::array<bool, 4>, 5> masks{
        std::array{true, true, true, false}, std::array{false, true, true, false},
        std::array{false, false, true, false}, std::array{false, false, false, false},
        std::array{false, false, false, true}};
    ASSERT_EQ(batch.Commands().size(), 5);
    for (std::size_t i = 0; i < ops.size(); ++i)
    {
        const auto& command = batch.Commands()[i];
        EXPECT_EQ(command.operation, ops[i]);
        EXPECT_EQ(command.state.cullEnabled, masks[i][0]);
        EXPECT_EQ(command.state.depthWrite, masks[i][1]);
        EXPECT_EQ(command.state.alphaTest, masks[i][2]);
        EXPECT_EQ(command.state.texture2d, masks[i][3]);
        EXPECT_EQ(command.vertexCount, 0);
        EXPECT_EQ(command.firstVertex, 0);
    }
    EXPECT_EQ(backend.State().fogPolicy, 7);
    EXPECT_TRUE(batch.Vertices().empty());
    const auto again = backend.SubtractiveTail();
    ASSERT_FALSE(again.HasValue());
    EXPECT_EQ(again.Error(), std::make_error_code(std::errc::function_not_supported));
    EXPECT_EQ(batch.StateSerial(), firstSerial + 5);
    EXPECT_EQ(batch.Commands().size(), 5);
}
} // namespace

TEST(ParticleMaterialBackendTest, KalimaSixEnablesDepthAfterMaterialWithoutChangingCullOrBlend)
{
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    auto state = batch.State();
    state.depthTest = false;
    state.cullEnabled = false;
    batch.ApplyState(state);
    MaterialModels models;
    Client::Render::ModelBatchParticleBackend backend(std::ref(batch), std::cref(models),
                                                      Client::Render::ModelBatchParticleState::ForModelPass());
    ASSERT_TRUE(backend.Type620850().HasValue());
    EXPECT_TRUE(batch.State().depthTest);
    EXPECT_EQ(batch.State().cullEnabled, false);
    const auto serial = batch.StateSerial();
    ASSERT_TRUE(backend.Type620850().HasValue());
    EXPECT_EQ(batch.StateSerial(), serial);
}

// 0x5489eb: glTexEnvi va fuera del quad y no es del material: no publica una operación ni cambia el serial, y el quad
// que sigue lleva GL_ADD hasta que vuelve GL_MODULATE. Adentro del quad es un error de GL y no cambia nada.
TEST(ParticleMaterialBackendTest, TextureEnvIsOutsideTheQuadAndTravelsWithIt)
{
    using Client::Render::TextureEnvMode;
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    SlotLookupModels models;
    Client::Render::ModelBatchParticleBackend backend(std::ref(batch), std::cref(models),
                                                      Client::Render::ModelBatchParticleState::ForModelPass());
    const auto commands = batch.Commands().size();
    const auto serial = batch.StateSerial();
    ASSERT_TRUE(backend.TextureEnv(TextureEnvMode::Add).HasValue());
    EXPECT_EQ(batch.TextureEnv(), TextureEnvMode::Add);
    EXPECT_EQ(batch.Commands().size(), commands);
    EXPECT_EQ(batch.StateSerial(), serial);
    ASSERT_TRUE(backend.BlendedPrefix().HasValue());
    ASSERT_TRUE(backend.Helper6208f0().HasValue());
    ASSERT_TRUE(backend.Helper6208b0().HasValue());
    ASSERT_TRUE(backend.BlendedTail().HasValue());
    ASSERT_TRUE(backend.BindTexture(5).HasValue());
    ASSERT_TRUE(backend.Begin().HasValue());
    const auto inside = backend.TextureEnv(TextureEnvMode::Modulate);
    ASSERT_FALSE(inside.HasValue());
    EXPECT_EQ(inside.Error(), std::make_error_code(std::errc::operation_in_progress));
    EXPECT_EQ(batch.TextureEnv(), TextureEnvMode::Add);
    ASSERT_TRUE(backend.Color3({1.0f, 1.0f, 1.0f}).HasValue());
    for (const glm::vec3& corner : {glm::vec3{0, 0, 0}, glm::vec3{1, 0, 0}, glm::vec3{1, 1, 0}, glm::vec3{0, 1, 0}})
    {
        ASSERT_TRUE(backend.TexCoord({0.0f, 0.0f}).HasValue());
        ASSERT_TRUE(backend.Vertex(corner).HasValue());
    }
    ASSERT_TRUE(backend.End().HasValue());
    ASSERT_TRUE(backend.TextureEnv(TextureEnvMode::Modulate).HasValue());
    ASSERT_FALSE(batch.Commands().empty());
    const auto& quad = batch.Commands().back();
    EXPECT_EQ(quad.operation, Client::Render::ModelStateOperation::Draw);
    EXPECT_EQ(quad.vertexCount, 6u);
    EXPECT_EQ(quad.textureEnv, TextureEnvMode::Add);
    EXPECT_EQ(quad.state.blend, Client::Render::ModelBlend::BlendedShadow);
    EXPECT_EQ(batch.TextureEnv(), TextureEnvMode::Modulate);
}
