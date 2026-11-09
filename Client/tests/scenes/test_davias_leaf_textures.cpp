#include "Scenes/DaviasLeafTextureSet.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Textures/Texture.h"
#include "Data/Textures/TextureSet.h"
#include "Render/ITextureStore.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/GameModels.h"
#include "Scenes/TextureMetadata.h"
#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/RecordingSink.h"

#include <glm/ext/vector_float2.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace
{
class DaviasStore final : public Client::Render::ITextureStore
{
public:
    Client::Core::Expected<void, std::string>
    Load(std::span<const Client::Data::Textures::LoadedTexture> textures) override
    {
        for (const auto& texture : textures)
        {
            ++calls;
            formats.push_back(texture.texture.format);
            slots.push_back(texture.slot);
            if (failAt == calls)
                return Client::Core::Unexpected(std::string("fallo de upload del test"));
        }
        return stored.Load(textures);
    }
    void Release(Client::Render::TextureSlot slot) override
    {
        stored.Release(slot);
    }
    Client::Tests::FakeTextureStore stored;
    std::size_t calls = 0;
    std::size_t failAt = 0;
    std::vector<Client::Data::Textures::PixelFormat> formats;
    std::vector<std::uint16_t> slots;
};

class DaviasLeafTextureTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_root = Client::Tests::GoldenDir();
        ASSERT_TRUE(std::filesystem::is_directory(m_root));
    }
    std::filesystem::path m_root;
    DaviasStore m_store;
    Client::Tests::RecordingSink m_sink;
    Client::Core::Log m_log{m_sink, Client::Core::LogLevel::Debug};
    Client::Scenes::GameModels m_models;
};

// Suprimir una carga o fabricar metadata por extensión rompe orden, formatos y registro real.
TEST_F(DaviasLeafTextureTest, RealAssetsPublishTheLastSuccessfulUploadAndExactRawKeys)
{
    const auto report = m_models.LoadTextures(m_root, Client::Scenes::DaviasLeafTextureSet, m_store, m_log);
    EXPECT_EQ(report.uploaded, 3u);
    EXPECT_EQ(report.skipped, 0u);
    EXPECT_EQ(m_store.slots, (std::vector<std::uint16_t>{100, 100, 101}));
    using Client::Data::Textures::PixelFormat;
    EXPECT_EQ(m_store.formats, (std::vector{PixelFormat::Rgba, PixelFormat::Rgb, PixelFormat::Rgb}));
    const Client::Scenes::IModelLibrary& library = m_models;
    EXPECT_EQ(library.TextureSlotOf(100), Client::Render::TextureSlot{100});
    EXPECT_EQ(library.TextureSlotOf(101), Client::Render::TextureSlot{101});
    EXPECT_FALSE(library.TextureSlotOf(0x10064).has_value());
    EXPECT_FALSE(library.TextureSlotOf(static_cast<std::uint32_t>(-1)).has_value());
    ASSERT_TRUE(library.TextureMetadataOf(100).has_value());
    ASSERT_TRUE(library.TextureMetadataOf(101).has_value());
    EXPECT_EQ(library.TextureMetadataOf(100)->components, 3);
    EXPECT_EQ(library.TextureMetadataOf(101)->components, 3);
    // SOF de los JPEG existentes: ambos16×16; el OZT previo tiene32×32/32bpp.
    EXPECT_EQ(library.TextureMetadataOf(100)->size, glm::vec2(16, 16));
    EXPECT_EQ(library.TextureMetadataOf(101)->size, glm::vec2(16, 16));
    const auto actual = m_store.stored.Find({100});
    ASSERT_TRUE(actual.has_value());
    EXPECT_EQ(actual->texture.format, PixelFormat::Rgb);
}

TEST_F(DaviasLeafTextureTest, FailedSecondUploadRetainsTheFirstOztAndContinuesTo101)
{
    m_store.failAt = 2;
    const auto report = m_models.LoadTextures(m_root, Client::Scenes::DaviasLeafTextureSet, m_store, m_log);
    EXPECT_EQ(report.uploaded, 2u);
    EXPECT_EQ(report.skipped, 1u);
    ASSERT_TRUE(m_models.TextureMetadataOf(100).has_value());
    EXPECT_EQ(m_models.TextureMetadataOf(100)->components, 4);
    EXPECT_EQ(m_models.TextureMetadataOf(100)->size, glm::vec2(32, 32));
    EXPECT_EQ(m_models.TextureSlotOf(100), Client::Render::TextureSlot{100});
    EXPECT_EQ(m_models.TextureSlotOf(101), Client::Render::TextureSlot{101});
    m_store.failAt = 0;
    EXPECT_EQ(m_models.LoadTextures(m_root, Client::Scenes::DaviasLeafTextureSet, m_store, m_log).uploaded, 3u);
    EXPECT_EQ(m_models.TextureMetadataOf(100)->components, 3);
    // Reload fallido conserva ambos campos; Release no notifica a GameModels en el diseño previo.
    m_store.failAt = m_store.calls + 1;
    EXPECT_EQ(
        m_models.LoadTextures(m_root, std::span(Client::Scenes::DaviasLeafTextureSet).first(1), m_store, m_log).skipped,
        1u);
    EXPECT_EQ(m_models.TextureMetadataOf(100)->components, 3);
    EXPECT_EQ(m_models.TextureSlotOf(100), Client::Render::TextureSlot{100});
    m_store.Release({100});
    EXPECT_EQ(m_models.TextureSlotOf(100), Client::Render::TextureSlot{100});
}
} // namespace
