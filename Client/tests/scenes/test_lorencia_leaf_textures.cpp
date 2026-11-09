#include "Scenes/LorenciaLeafTextureSet.h"

#include "Core/Expected.h"
#include "Core/ILogSink.h"
#include "Core/Log.h"
#include "Data/Textures/Sampling.h"
#include "Data/Textures/TextureSet.h"
#include "Render/ITextureStore.h"
#include "Render/TextureSlot.h"
#include "Scenes/GameModels.h"
#include "Scenes/TextureMetadata.h"
#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include "support/RecordingSink.h"

#include <glm/ext/vector_float2.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
class LorenciaStore final : public Client::Render::ITextureStore
{
public:
    Client::Core::Expected<void, std::string>
    Load(std::span<const Client::Data::Textures::LoadedTexture> textures) override
    {
        for (const auto& texture : textures)
        {
            slots.push_back(texture.slot);
            if (failUpload)
                return Client::Core::Unexpected(std::string("fallo de upload solicitado por el test"));
        }
        return stored.Load(textures);
    }
    void Release(Client::Render::TextureSlot slot) override
    {
        stored.Release(slot);
    }
    Client::Tests::FakeTextureStore stored;
    std::vector<std::uint16_t> slots;
    bool failUpload = false;
};

TEST(LorenciaLeafTextureTest, ExactThreeRequestsIncludeTheTwoMissingFilesWithTheirNativeOrder)
{
    using namespace Client::Scenes;
    constexpr std::array<std::string_view, 3> paths{"World1\\leaf01.tga", "World1\\leaf01.jpg", "World1\\leaf02.jpg"};
    constexpr std::array<std::uint16_t, 3> slots{100, 100, 101};
    ASSERT_EQ(LorenciaLeafTextureSet.size(), paths.size());
    for (std::size_t i = 0; i < paths.size(); ++i)
    {
        EXPECT_EQ(LorenciaLeafTextureSet[i].path, paths[i]);
        EXPECT_EQ(LorenciaLeafTextureSet[i].slot, slots[i]);
        EXPECT_EQ(LorenciaLeafTextureSet[i].sampling.filter, Client::Data::Textures::Filter::Nearest);
        EXPECT_EQ(LorenciaLeafTextureSet[i].sampling.wrap, Client::Data::Textures::Wrap::Clamp);
    }
    EXPECT_EQ(Client::Data::Textures::TextureLoaderOf(paths[0]), Client::Data::Textures::TextureLoader::Ozt);
    EXPECT_EQ(Client::Data::Textures::TextureLoaderOf(paths[1]), Client::Data::Textures::TextureLoader::Ozj);
}

TEST(LorenciaLeafTextureTest, RealWorld1PublishesTheOztAndReportsBothLaterMissingRequestsInOrder)
{
    Client::Scenes::GameModels models;
    LorenciaStore store;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
    const auto report =
        models.LoadTextures(Client::Tests::RepoDataDir(), Client::Scenes::LorenciaLeafTextureSet, store, log);
    EXPECT_EQ(report.uploaded, 1u);
    EXPECT_EQ(report.skipped, 2u);
    EXPECT_EQ(store.slots, (std::vector<std::uint16_t>{100}));
    const auto metadata = models.TextureMetadataOf(100);
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(metadata->components, 4);
    EXPECT_EQ(metadata->size, glm::vec2(16, 16));
    EXPECT_EQ(models.TextureSlotOf(100), Client::Render::TextureSlot{100});
    EXPECT_FALSE(models.TextureSlotOf(101).has_value());
    std::vector<std::string> warnings;
    for (std::size_t i = 0; i < sink.lines.size(); ++i)
        if (sink.levels[i] == Client::Core::LogLevel::Warning)
            warnings.push_back(sink.lines[i]);
    ASSERT_EQ(warnings.size(), 2u);
    EXPECT_NE(warnings[0].find("World1\\leaf01.jpg"), std::string::npos);
    EXPECT_NE(warnings[1].find("World1\\leaf02.jpg"), std::string::npos);
}

TEST(LorenciaLeafTextureTest, AFailedReloadKeepsThePublishedRowAndDoesNotOmitTheLaterRequests)
{
    Client::Scenes::GameModels models;
    LorenciaStore store;
    Client::Tests::RecordingSink sink;
    Client::Core::Log log{sink, Client::Core::LogLevel::Debug};
    ASSERT_EQ(
        models.LoadTextures(Client::Tests::RepoDataDir(), Client::Scenes::LorenciaLeafTextureSet, store, log).uploaded,
        1u);
    store.failUpload = true;
    const auto failed =
        models.LoadTextures(Client::Tests::RepoDataDir(), Client::Scenes::LorenciaLeafTextureSet, store, log);
    EXPECT_EQ(failed.uploaded, 0u);
    EXPECT_EQ(failed.skipped, 3u);
    EXPECT_EQ(models.TextureSlotOf(100), Client::Render::TextureSlot{100});
    ASSERT_TRUE(models.TextureMetadataOf(100).has_value());
    EXPECT_EQ(models.TextureMetadataOf(100)->components, 4);
    EXPECT_EQ(store.slots, (std::vector<std::uint16_t>{100, 100}));
    EXPECT_FALSE(models.TextureMetadataOf(101).has_value());
}
} // namespace
