#include "Scenes/TerrainTiles.h"
#include "World/WorldTerrain.h"
#include "Data/Textures/Sampling.h"
#include "Render/TextureSlot.h"
#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"
#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace
{
class CastleMinimapTexturesTest : public Client::Tests::GoldenDataTest
{
};
TEST_F(CastleMinimapTexturesTest, TheMapLoaderUploadsBothOriginalMapImagesAndReleasesThem)
{
    Client::Tests::FakeTextureStore store;
    Client::Scenes::TerrainTiles tiles(store);
    std::vector<std::string> skipped;
    const auto loaded = tiles.Load(m_dataRoot, 30, skipped);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    for (const std::uint16_t slot : std::array<std::uint16_t, 2>{0x69d, 0x69e})
    {
        const auto image = store.Find(Client::Render::TextureSlot{slot});
        ASSERT_TRUE(image.has_value());
        EXPECT_EQ(image->sampling.filter, Client::Data::Textures::Filter::Linear);
        EXPECT_EQ(image->sampling.wrap, Client::Data::Textures::Wrap::Clamp);
        EXPECT_GT(image->texture.pixels.size(), 0u);
    }
    tiles.Release();
    EXPECT_FALSE(store.Find(Client::Render::TextureSlot{0x69d}).has_value());
    EXPECT_FALSE(store.Find(Client::Render::TextureSlot{0x69e}).has_value());
    EXPECT_TRUE(Client::World::CastleSiegeTextures(29).empty());
    EXPECT_TRUE(Client::World::CastleSiegeTextures(31).empty());
}
} // namespace
