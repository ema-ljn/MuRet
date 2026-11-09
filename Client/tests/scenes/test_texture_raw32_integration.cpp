#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Models/Model.h"
#include "Data/Textures/Texture.h"
#include "Data/Textures/TextureSet.h"
#include "Render/ModelBatch.h"
#include "Render/ModelBatchParticleBackend.h"
#include "Render/ModelBatchParticleState.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/Skeleton.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/GameModels.h"
#include "Scenes/LeafDrawingServices.h"
#include "Scenes/ParticleDrawServices.h"
#include "Scenes/TextureMetadata.h"
#include "Scenes/WorldFaunaDrawing.h"
#include "support/FakeTextureStore.h"
#include "support/RecordingSink.h"
#include "support/SceneDataFiles.h"
#include "support/TemporaryFolder.h"
#include "support/TextureFiles.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

namespace
{
using Client::Render::TextureSlot;
using Client::Scenes::TextureMetadata;
using MetadataLookup = std::optional<TextureMetadata> (Client::Scenes::IModelLibrary::*)(std::uint32_t) const;
using SlotLookup = std::optional<TextureSlot> (Client::Scenes::IModelLibrary::*)(std::uint32_t) const;
static_assert(std::is_same_v<decltype(&Client::Scenes::IModelLibrary::TextureMetadataOf), MetadataLookup>);
static_assert(std::is_same_v<decltype(&Client::Scenes::IModelLibrary::TextureSlotOf), SlotLookup>);

// Observa el raw32 recibido; los selectores que no coinciden siguen ausentes.
class Raw32Library final : public Client::Scenes::IModelLibrary
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
    std::span<const TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return {};
    }
    std::optional<TextureMetadata> TextureMetadataOf(std::uint32_t selector) const override
    {
        metadataKeys.push_back(selector);
        const Client::Data::Textures::Texture texture{.textureWidth = 32, .textureHeight = 16, .pixels = {}};
        return selector == key ? std::optional(TextureMetadata{texture}) : std::nullopt;
    }
    std::optional<TextureSlot> TextureSlotOf(std::uint32_t selector) const override
    {
        slotKeys.push_back(selector);
        return selector == key ? std::optional(TextureSlot{23}) : std::nullopt;
    }
    std::uint32_t key = 0;
    mutable std::vector<std::uint32_t> metadataKeys;
    mutable std::vector<std::uint32_t> slotKeys;
};

constexpr Client::Render::Models::OriginalMatrix Identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};

TEST(TextureRaw32IntegrationTest, LoadedOwnerKeepsMetadataAndPhysicalSlotWithoutLow16Aliases)
{
    Client::Tests::TemporaryFolder folder{"raw32-owner"};
    Client::Tests::WriteSceneFile(folder.Path(), "Effect/raw.OZT", Client::Tests::PlainOzt(3, 5));
    const std::array entries{Client::Data::Textures::TextureSetEntry{"Effect/raw.tga", 100, {}}};
    Client::Tests::FakeTextureStore store;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
    Client::Scenes::GameModels models;
    ASSERT_EQ(models.LoadTextures(folder.Path(), entries, store, log).uploaded, 1u);
    const Client::Scenes::IModelLibrary& library = models;
    const auto metadata = library.TextureMetadataOf(100u);
    ASSERT_TRUE(metadata);
    EXPECT_EQ(metadata->size, (glm::vec2{4, 8}));
    EXPECT_EQ(metadata->components, 4);
    EXPECT_EQ(library.TextureSlotOf(100u), TextureSlot{100});
    for (const std::uint32_t raw : {0x10064u, 0x80000064u, 0xffff0064u, 0xffffffffu})
    {
        EXPECT_FALSE(library.TextureMetadataOf(raw));
        EXPECT_FALSE(library.TextureSlotOf(raw));
        const auto signedSelector = std::bit_cast<std::int32_t>(raw);
        EXPECT_FALSE(library.TextureMetadataOf(static_cast<std::uint32_t>(signedSelector)));
        EXPECT_FALSE(library.TextureSlotOf(static_cast<std::uint32_t>(signedSelector)));
    }
}

TEST(TextureRaw32IntegrationTest, ParticleMetadataPreservesEverySignedRawBit)
{
    Raw32Library library;
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    Client::Render::ModelBatchParticleBackend backend{std::ref(batch), std::cref(library),
                                                      Client::Render::ModelBatchParticleState::ForModelPass()};
    Client::Scenes::ParticleDrawServices drawing{std::cref(library), std::ref(backend), std::cref(Identity)};
    for (const std::uint32_t raw : {0x10064u, 0x80000064u, 0xffff0064u, 0xffffffffu})
    {
        library.key = raw;
        const auto selector = std::bit_cast<std::int32_t>(raw);
        const auto dimensions = drawing.Dimensions(selector);
        ASSERT_TRUE(dimensions.HasValue());
        EXPECT_EQ(dimensions.Value(), (glm::vec2{32, 16}));
        const auto material = drawing.MaterialFlag(selector);
        ASSERT_TRUE(material.HasValue());
        EXPECT_EQ(material.Value(), 4);
        EXPECT_EQ(library.metadataKeys.back(), raw);
    }
}

TEST(TextureRaw32IntegrationTest, PositiveSelectorUsesExactRawKeyAndThePublishedPhysicalSlot)
{
    Raw32Library library;
    library.key = 0x10064u;
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    Client::Render::ModelBatchParticleBackend backend{std::ref(batch), std::cref(library),
                                                      Client::Render::ModelBatchParticleState::ForModelPass()};
    ASSERT_TRUE(backend.BindTexture(static_cast<std::int32_t>(library.key)).HasValue());
    EXPECT_EQ(library.slotKeys, (std::vector<std::uint32_t>{0x10064u}));
    EXPECT_EQ(batch.State().texture, TextureSlot{23});
    EXPECT_FALSE(batch.State().rawTextureName);
}

TEST(TextureRaw32IntegrationTest, NegativeLeafBillboardKeepsRawNameBindingAndUnsignedMetadataLookup)
{
    Raw32Library library;
    library.key = 0x80000064u;
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    Client::Render::ModelBatchParticleBackend backend{std::ref(batch), std::cref(library),
                                                      Client::Render::ModelBatchParticleState::ForModelPass()};
    Client::Scenes::LeafDrawingServices drawing{std::cref(library), std::ref(backend), std::cref(Identity)};
    ASSERT_TRUE(backend.AlphaWriteDepth().HasValue());
    glm::vec3 position{1, 2, 3};
    glm::vec3 light{0.5f, 0.6f, 0.7f};
    const auto selector = std::bit_cast<std::int32_t>(library.key);
    ASSERT_TRUE(drawing.Billboard(batch, {selector, position, 1, 2, light}).HasValue());
    EXPECT_EQ(library.metadataKeys, (std::vector<std::uint32_t>{library.key}));
    EXPECT_TRUE(library.slotKeys.empty());
    EXPECT_FALSE(batch.State().texture);
    EXPECT_EQ(batch.State().rawTextureName, std::uint32_t{0} - library.key);
    EXPECT_EQ(backend.State().textureSelector, selector);
}
} // namespace
