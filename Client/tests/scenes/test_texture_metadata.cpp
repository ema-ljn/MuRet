#include "Scenes/TextureMetadata.h"
#include "Scenes/GameModels.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/TextureSets.h"
#include "Core/Expected.h"
#include "Core/Log.h"
#include "Core/ILogSink.h"
#include "Data/Models/Model.h"
#include "Data/Textures/OzjTexture.h"
#include "Data/Textures/OztTexture.h"
#include "Data/Textures/Texture.h"
#include "Data/Textures/TextureSet.h"
#include "Render/ITextureStore.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "support/FakeTextureStore.h"
#include "support/JpegVectors.h"
#include "support/RecordingSink.h"
#include "support/SceneDataFiles.h"
#include "support/SyntheticModels.h"
#include "support/TemporaryFolder.h"
#include "support/TextureFiles.h"

#include <glm/ext/vector_float2.hpp>
#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace
{
// Los nuevos productores usan estos selectores físicos: Fire01, Shiny01, Spark02, lightning2, Spark01 y flareBlue.
constexpr std::array<std::uint16_t, 6> RuntimeEffectSelectors{0x5d9, 0x5fc, 0x5c5, 0x5d5, 0x617, 0x62b};

class MetadataStore final : public Client::Render::ITextureStore
{
public:
    Client::Core::Expected<void, std::string>
    Load(std::span<const Client::Data::Textures::LoadedTexture> textures) override
    {
        for (const auto& texture : textures)
        {
            attempted.push_back(texture.slot);
            if (blocked == texture.slot)
            {
                if (failAfterUpload)
                {
                    const auto uploaded = stored.Load(std::span(&texture, 1));
                    if (!uploaded.HasValue())
                        return uploaded;
                }
                return Client::Core::Unexpected(std::string("fallo de upload solicitado por el test"));
            }
        }
        return stored.Load(textures);
    }
    void Release(Client::Render::TextureSlot slot) override
    {
        stored.Release(slot);
    }
    Client::Tests::FakeTextureStore stored;
    std::optional<std::uint16_t> blocked;
    bool failAfterUpload = false;
    std::vector<std::uint16_t> attempted;
};

class TextureMetadataTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        Client::Data::Models::Model model;
        model.name = "metadata";
        model.version = 0x0a;
        Client::Tests::WriteSceneFile(m_folder.Path(), "Player/Player.bmd",
                                      Client::Tests::PlainBmd(Client::Tests::BmdContent(model), model.version));
        Client::Tests::WriteTextureSet(m_folder.Path(), Client::Scenes::GlowTextureSet);
        Client::Tests::WriteTextureSet(m_folder.Path(), Client::Scenes::EffectTextureSet);
    }
    Client::Tests::TemporaryFolder m_folder{"texture-metadata"};
    MetadataStore m_store;
    Client::Tests::RecordingSink m_sink;
    Client::Core::Log m_log{m_sink, Client::Core::LogLevel::Debug};
};

TEST(TextureMetadataConversionTest, DecodedRgbAndRgbaUseBufferDimensionsAndActualComponents)
{
    const auto rgb =
        Client::Data::Textures::LoadOzjTexture(Client::Tests::OzjOf(Client::Tests::ReadJpegVector("baseline-420.jpg")));
    ASSERT_TRUE(rgb.HasValue());
    EXPECT_EQ(rgb.Value().width, 13);
    EXPECT_EQ(rgb.Value().height, 11);
    const Client::Scenes::TextureMetadata rgbMetadata(rgb.Value());
    EXPECT_EQ(rgbMetadata.size, (glm::vec2{16, 16}));
    EXPECT_EQ(rgbMetadata.components, 3);

    const auto rgba = Client::Data::Textures::LoadOztTexture(Client::Tests::PlainOzt(3, 5));
    ASSERT_TRUE(rgba.HasValue());
    EXPECT_EQ(rgba.Value().width, 3);
    EXPECT_EQ(rgba.Value().height, 5);
    const Client::Scenes::TextureMetadata rgbaMetadata(rgba.Value());
    EXPECT_EQ(rgbaMetadata.size, (glm::vec2{4, 8}));
    EXPECT_EQ(rgbaMetadata.components, 4);
}

class LibraryWithoutTextureMetadata final : public Client::Scenes::IModelLibrary
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

TEST(TextureMetadataConversionTest, ADefaultLibraryAndAnUnloadedOwnerReportAbsence)
{
    LibraryWithoutTextureMetadata library;
    Client::Scenes::GameModels models;
    EXPECT_FALSE(library.TextureMetadataOf(0x622).has_value());
    EXPECT_FALSE(models.TextureMetadataOf(0x622).has_value());
    EXPECT_FALSE(models.TextureMetadataOf(0xffffffffu).has_value());
    EXPECT_FALSE(models.TextureSize({0x622}).has_value());
}

TEST_F(TextureMetadataTest, LoadedEffectsUseExactRawSelectorsAndShareTheLegacySize)
{
    Client::Scenes::GameModels models;
    ASSERT_TRUE(models.Load(m_folder.Path(), m_store, m_log).HasValue());
    const Client::Scenes::IModelLibrary& library = models;
    const auto metadata = library.TextureMetadataOf(0x5ac);
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(metadata->size, (glm::vec2{16, 16}));
    EXPECT_EQ(metadata->components, 3);
    EXPECT_EQ(library.TextureSize({0x5ac}), std::optional(glm::vec2{16, 16}));
    EXPECT_FALSE(library.TextureMetadataOf(0x100005acu).has_value());
    EXPECT_FALSE(library.TextureMetadataOf(0xffff05acu).has_value());
    EXPECT_FALSE(library.TextureMetadataOf(0xffffffffu).has_value());
    for (const std::uint16_t selector : RuntimeEffectSelectors)
    {
        SCOPED_TRACE(selector);
        const auto effect = library.TextureMetadataOf(selector);
        ASSERT_TRUE(effect.has_value());
        EXPECT_EQ(effect->size, (glm::vec2{16, 16}));
        EXPECT_EQ(effect->components, 3);
        EXPECT_EQ(library.TextureSize({selector}), std::optional(glm::vec2{16, 16}));
        const auto physical = m_store.stored.Find({selector});
        ASSERT_TRUE(physical.has_value());
        EXPECT_EQ(physical->texture.textureWidth, 16);
        EXPECT_EQ(physical->texture.textureHeight, 16);
        EXPECT_FALSE(library.TextureMetadataOf(0x10000000u | selector).has_value());
    }
    EXPECT_FALSE(library.TextureMetadataOf(0x5d8).has_value()); // Fuera de las cargas de esta fixture.
}

TEST_F(TextureMetadataTest, AnUploadFailureKeepsThePublishedPrefixAndContinuesAfterTheFailedSlot)
{
    m_store.blocked = std::uint16_t{0x5fd};
    m_store.failAfterUpload = true;
    Client::Scenes::GameModels models;
    ASSERT_TRUE(models.Load(m_folder.Path(), m_store, m_log).HasValue());
    EXPECT_TRUE(models.TextureMetadataOf(0x5ac).has_value());
    EXPECT_FALSE(models.TextureMetadataOf(0x5fd).has_value());
    EXPECT_FALSE(models.TextureSize({0x5fd}).has_value());
    EXPECT_TRUE(m_store.stored.Find({0x5fd}).has_value()); // El upload pudo mutar antes de devolver error.
    EXPECT_TRUE(models.TextureMetadataOf(0x5c6).has_value());
    std::vector<std::uint16_t> expected;
    for (const auto& entry : Client::Scenes::GlowTextureSet)
        expected.push_back(entry.slot);
    for (const auto& entry : Client::Scenes::EffectTextureSet)
        expected.push_back(entry.slot);
    EXPECT_EQ(m_store.attempted, expected);
}

// El binder de las partículas (0x620780) liga el slot que publicó la carga: el de la entrada, que es su
// selector, solo si subió. Un upload que falló no lo publica aunque haya tocado la tabla.
TEST_F(TextureMetadataTest, OnlyUploadedEffectsPublishTheSlotOfTheirSelector)
{
    m_store.blocked = std::uint16_t{0x5fd};
    m_store.failAfterUpload = true;
    Client::Scenes::GameModels models;
    EXPECT_FALSE(models.TextureSlotOf(0x5ac).has_value()) << "antes de cargar";
    ASSERT_TRUE(models.Load(m_folder.Path(), m_store, m_log).HasValue());
    const Client::Scenes::IModelLibrary& library = models;
    EXPECT_EQ(library.TextureSlotOf(0x5ac), std::optional(Client::Render::TextureSlot{0x5ac}));
    EXPECT_EQ(library.TextureSlotOf(0x622), std::optional(Client::Render::TextureSlot{0x622}));
    EXPECT_FALSE(library.TextureSlotOf(0x5fd).has_value());
    EXPECT_TRUE(m_store.stored.Find({0x5fd}).has_value());
    EXPECT_FALSE(library.TextureSlotOf(0x100005acu).has_value());
    EXPECT_FALSE(library.TextureSlotOf(0xffffffffu).has_value());
    for (const std::uint16_t selector : RuntimeEffectSelectors)
    {
        SCOPED_TRACE(selector);
        EXPECT_EQ(library.TextureSlotOf(selector), std::optional(Client::Render::TextureSlot{selector}));
        EXPECT_TRUE(library.TextureMetadataOf(selector).has_value());
        EXPECT_TRUE(m_store.stored.Find({selector}).has_value());
        EXPECT_FALSE(library.TextureSlotOf(0x10000000u | selector).has_value());
    }
    EXPECT_FALSE(library.TextureSlotOf(0x5d8).has_value());
}

TEST_F(TextureMetadataTest, MissingRuntimeImagesPublishNeitherMetadataNorPhysicalHandles)
{
    ASSERT_TRUE(std::filesystem::remove(m_folder.Path() / "Effect/Fire01.OZJ"));
    ASSERT_TRUE(std::filesystem::remove(m_folder.Path() / "Effect/Shiny01.OZJ"));
    Client::Scenes::GameModels models;
    ASSERT_TRUE(models.Load(m_folder.Path(), m_store, m_log).HasValue());
    for (const std::uint16_t selector : {std::uint16_t{0x5d9}, std::uint16_t{0x5fc}})
    {
        EXPECT_FALSE(models.TextureMetadataOf(selector).has_value());
        EXPECT_FALSE(models.TextureSlotOf(selector).has_value());
        EXPECT_FALSE(models.TextureSize({selector}).has_value());
        EXPECT_FALSE(m_store.stored.Find({selector}).has_value());
    }
    EXPECT_TRUE(models.TextureMetadataOf(0x5c5).has_value());
    EXPECT_TRUE(models.TextureSlotOf(0x617).has_value());
}

TEST_F(TextureMetadataTest, ReloadFailureRetainsTheOldRowAndValueSnapshotsOutliveTheOwner)
{
    std::optional<Client::Scenes::TextureMetadata> snapshot;
    {
        Client::Scenes::GameModels models;
        ASSERT_TRUE(models.Load(m_folder.Path(), m_store, m_log).HasValue());
        snapshot = models.TextureMetadataOf(0x5ac);
        ASSERT_TRUE(snapshot.has_value());
        Client::Tests::WriteSceneFile(m_folder.Path(), "Effect/flare01.OZJ",
                                      Client::Tests::OzjOf(Client::Tests::ReadJpegVector("flat-512x8.jpg")));
        m_store.blocked = std::uint16_t{0x5ac};
        m_store.failAfterUpload = true;
        ASSERT_TRUE(models.Load(m_folder.Path(), m_store, m_log).HasValue());
        const auto failedUpload = m_store.stored.Find({0x5ac});
        ASSERT_TRUE(failedUpload.has_value());
        EXPECT_EQ(failedUpload->texture.textureWidth, 512);
        EXPECT_EQ(models.TextureSize({0x5ac}), std::optional(glm::vec2{16, 16}));
        m_store.blocked.reset();
        ASSERT_TRUE(models.Load(m_folder.Path(), m_store, m_log).HasValue());
        EXPECT_EQ(models.TextureSize({0x5ac}), std::optional(glm::vec2{512, 8}));
        const auto refreshed = models.TextureMetadataOf(0x5ac);
        ASSERT_TRUE(refreshed.has_value());
        EXPECT_EQ(refreshed->size, (glm::vec2{512, 8}));
        EXPECT_EQ(refreshed->components, 3);
        m_store.Release({0x5ac}); // GameModels no recibe una notificación: se conserva la política previa.
        EXPECT_EQ(models.TextureSize({0x5ac}), std::optional(glm::vec2{512, 8}));
    }
    EXPECT_EQ(snapshot->size, (glm::vec2{16, 16}));
    EXPECT_EQ(snapshot->components, 3);
}

TEST_F(TextureMetadataTest, ARequiredModelFailureDoesNotClearTheAlreadyPublishedTextureMetadata)
{
    Client::Scenes::GameModels models;
    ASSERT_TRUE(models.Load(m_folder.Path(), m_store, m_log).HasValue());
    const auto attempts = m_store.attempted.size();
    ASSERT_TRUE(std::filesystem::remove(m_folder.Path() / "Player/Player.bmd"));
    EXPECT_FALSE(models.Load(m_folder.Path(), m_store, m_log).HasValue());
    EXPECT_EQ(m_store.attempted.size(), attempts);
    const auto metadata = models.TextureMetadataOf(0x5ac);
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(metadata->size, (glm::vec2{16, 16}));
    EXPECT_EQ(metadata->components, 3);
}
} // namespace
