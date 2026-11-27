#include "Assets/ModelTextureTable.h"
#include "Assets/TerrainTiles.h"

#include "Core/Expected.h"
#include "Data/Textures/Sampling.h"
#include "Render/TextureSlot.h"
#include "Terrain/WorldTerrain.h"
#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace
{
class CastleSiegeObjectResourcesTest : public Client::Tests::GoldenDataTest,
                                       public ::testing::WithParamInterface<std::uint8_t>
{
};

// 0x42e626 dibuja NpcBarrier1ff con 0x41..0x60; el epílogo 0x618dda..0x618e29 los carga también en el mapa 30.
TEST_P(CastleSiegeObjectResourcesTest, TheWorldFindsAllThirtyTwoRealCausticsInTheNativeSharedSlots)
{
    const auto map = GetParam();
    ASSERT_EQ(Client::Terrain::CausticTextures(map).size(), 32u);
    auto store = std::make_unique<Client::Tests::FakeTextureStore>();
    Client::Assets::ModelTextureTable table;
    Client::Assets::TerrainTiles tiles(*store);
    tiles.BindTextureTable(table);
    std::vector<std::string> skipped;
    const auto loaded = tiles.Load(m_dataRoot, map, skipped);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error();
    for (std::uint16_t slot = 65; slot < 97; ++slot)
    {
        const Client::Render::TextureSlot textureSlot{slot};
        const auto texture = store->Find(textureSlot);
        ASSERT_TRUE(texture.has_value()) << slot;
        EXPECT_EQ(texture->sampling.filter, Client::Data::Textures::Filter::Linear);
        EXPECT_EQ(texture->sampling.wrap, Client::Data::Textures::Wrap::Repeat);
        EXPECT_GT(texture->texture.width, 0u);
        EXPECT_GT(texture->texture.height, 0u);
        EXPECT_EQ(table.References(textureSlot), 1);
    }
    ASSERT_FALSE(store->calls.empty());
    EXPECT_EQ(store->calls.front().slot, 0x607); // 0x616eeb, antes de la rama propia del mapa.
    const auto firstTile = std::ranges::find(store->calls, std::uint16_t{0x23}, &Client::Tests::StoreCall::slot);
    const auto firstCaustic = std::ranges::find(store->calls, std::uint16_t{0x41}, &Client::Tests::StoreCall::slot);
    const auto lastCaustic = std::ranges::find(store->calls, std::uint16_t{0x60}, &Client::Tests::StoreCall::slot);
    ASSERT_NE(firstTile, store->calls.end());
    ASSERT_NE(firstCaustic, store->calls.end());
    ASSERT_NE(lastCaustic, store->calls.end());
    if (map == 24)
    {
        EXPECT_LT(firstTile, firstCaustic);
    } // Water1 de Kalima está en la rama, antes del epílogo.
    else
    {
        EXPECT_LT(lastCaustic, firstTile);
    } // Tiles comunes de 0x61a560, después de 0x616ec0.
    const auto light = std::ranges::find(store->calls, std::uint16_t{0x68}, &Client::Tests::StoreCall::slot);
    ASSERT_NE(light, store->calls.end());
    EXPECT_LT(lastCaustic, light); // 0x618e40, antes de que el clima de 0x61a560 pueda reemplazar 0x68.
    const auto finalLight = store->Find(Client::Render::TextureSlot{0x68});
    ASSERT_TRUE(finalLight.has_value());
    EXPECT_EQ(finalLight->sampling.filter,
              map == 24 ? Client::Data::Textures::Filter::Linear : Client::Data::Textures::Filter::Nearest);
    EXPECT_EQ(finalLight->sampling.wrap,
              map == 24 ? Client::Data::Textures::Wrap::Repeat : Client::Data::Textures::Wrap::Clamp);
    EXPECT_EQ(table.References(Client::Render::TextureSlot{0x68}), 1);
    std::uint16_t next = 65;
    for (const auto& call : store->calls)
        if (call.kind == Client::Tests::StoreCall::Kind::Load && call.slot >= 65 && call.slot < 97)
        {
            EXPECT_EQ(call.slot, next++);
        }
    EXPECT_EQ(next, 97);
    tiles.Release();
    EXPECT_EQ(store->Size(), 0u);
    for (std::uint16_t slot = 65; slot < 97; ++slot)
        EXPECT_EQ(table.References(Client::Render::TextureSlot{slot}), 0);
}
TEST(CausticTexturesTest, EveryByteMapUsesTheSameNativeThirtyTwoFrameRequestOrder)
{
    for (std::uint16_t map = 0; map < 256; ++map)
    {
        const auto frames = Client::Terrain::CausticTextures(static_cast<std::uint8_t>(map));
        ASSERT_EQ(frames.size(), 32u) << map;
        EXPECT_EQ(frames.front().path, "Object8\\wt00.jpg");
        EXPECT_EQ(frames.back().path, "Object8\\wt31.jpg");
        for (std::uint16_t frame = 0; frame < 32; ++frame)
        {
            EXPECT_EQ(frames[frame].slot, 65 + frame);
            EXPECT_FALSE(frames[frame].required);
            EXPECT_EQ(frames[frame].sampling.filter, Client::Data::Textures::Filter::Linear);
            EXPECT_EQ(frames[frame].sampling.wrap, Client::Data::Textures::Wrap::Repeat);
        }
    }
}

INSTANTIATE_TEST_SUITE_P(Maps, CastleSiegeObjectResourcesTest,
                         ::testing::Values(std::uint8_t{0}, std::uint8_t{7}, std::uint8_t{24}, std::uint8_t{30},
                                           std::uint8_t{55}));
} // namespace
