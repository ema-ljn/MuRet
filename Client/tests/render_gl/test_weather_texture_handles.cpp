#include "Scenes/TerrainTiles.h"

#include "Render/Gl/GlTexture.h"
#include "Render/Gl/TextureTable.h"
#include "Render/TextureSlot.h"
#include "support/GlTestContext.h"
#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace
{
using WeatherTextureHandlesTest = Client::Tests::GlTest;

TEST_F(WeatherTextureHandlesTest, ReferenceWeatherImagesHaveActualGlHandlesAndAreReleased)
{
    Client::Render::Gl::TextureTable store(Gl());
    Client::Scenes::TerrainTiles tiles(store);
    for (const std::uint8_t map : {std::uint8_t{0}, std::uint8_t{2}, std::uint8_t{3}})
    {
        std::vector<std::string> skipped;
        const auto loaded = tiles.Load(Client::Tests::RepoDataDir(), map, skipped);
        ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
        for (const std::uint16_t slot :
             {std::uint16_t{0x64}, std::uint16_t{0x66}, std::uint16_t{0x67}, std::uint16_t{0x68}})
        {
            const auto* texture = store.Find(Client::Render::TextureSlot{slot});
            ASSERT_NE(texture, nullptr) << slot;
            EXPECT_NE(texture->Name(), 0u);
            EXPECT_GT(texture->Width(), 0);
            EXPECT_GT(texture->Height(), 0);
        }
        if (map == 2)
        {
            const auto* snow = store.Find(Client::Render::TextureSlot{0x65});
            ASSERT_NE(snow, nullptr);
            EXPECT_NE(snow->Name(), 0u);
        }
    }
    tiles.Release();
    EXPECT_EQ(store.Find(Client::Render::TextureSlot{0x64}), nullptr);
    EXPECT_EQ(store.Find(Client::Render::TextureSlot{0x68}), nullptr);
}
} // namespace
