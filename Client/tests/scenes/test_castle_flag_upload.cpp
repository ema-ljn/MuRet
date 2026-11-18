#include "Scenes/CastleFlagUpload.h"

#include "Data/Textures/Sampling.h"
#include "Data/Textures/Texture.h"
#include "Data/Textures/TextureSet.h"
#include "Render/TextureSlot.h"
#include "Scenes/ModelTextureTable.h"

#include "support/FakeTextureStore.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace
{
TEST(CastleFlagUploadTest, IdenticalMarksBothReplaceTheRgbImageAndPreserveCpuMetadata)
{
    Client::Tests::FakeTextureStore store;
    Client::Scenes::ModelTextureTable table;
    table.Bind(store);
    const Client::Data::Textures::LoadedTexture original{
        .slot = 0x69c,
        .sampling = {.filter = Client::Data::Textures::Filter::Linear},
        .texture = {.width = 64,
                    .height = 64,
                    .textureWidth = 64,
                    .textureHeight = 64,
                    .format = Client::Data::Textures::PixelFormat::Rgba,
                    .pixels = std::vector<std::uint8_t>(64 * 64 * 4)}};
    ASSERT_TRUE(table.Upload(original).HasValue());
    std::array<std::uint8_t, 64> mark{};
    mark[0] = 4;
    for (int repeat = 0; repeat < 2; ++repeat)
    {
        const auto uploaded = Client::Scenes::UploadTheCastleFlag(mark, table);
        ASSERT_TRUE(uploaded.HasValue()) << uploaded.Error();
    }
    EXPECT_EQ(store.loadCalls, 3);
    EXPECT_EQ(table.References(Client::Render::TextureSlot{0x69c}), 1);
    ASSERT_TRUE(table.Metadata(0x69c).has_value());
    EXPECT_EQ(table.Metadata(0x69c)->components, 4);
    const auto image = store.Find(Client::Render::TextureSlot{0x69c});
    ASSERT_TRUE(image.has_value());
    EXPECT_EQ(image->texture.format, Client::Data::Textures::PixelFormat::Rgb);
    ASSERT_EQ(image->texture.pixels.size(), 64u * 64u * 3u);
    EXPECT_EQ(image->texture.pixels[(16u * 64u + 16u) * 3u], 255);
    EXPECT_EQ(image->sampling.filter, Client::Data::Textures::Filter::Linear);
    EXPECT_EQ(image->sampling.wrap, Client::Data::Textures::Wrap::Clamp);
}
} // namespace
