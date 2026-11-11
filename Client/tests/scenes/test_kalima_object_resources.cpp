#include "Scenes/ModelTextureTable.h"
#include "Scenes/TerrainTiles.h"

#include "Core/Expected.h"
#include "Data/Textures/Sampling.h"
#include "Render/Models/Cloth.h"
#include "Render/TextureSlot.h"
#include "support/FakeTextureStore.h"
#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

TEST(KalimaObjectResourcesTest, AllThirtyTwoNativeCausticsAreLoadedFromRealDataIntoTheirSharedSlots)
{
    Client::Tests::FakeTextureStore store;
    Client::Scenes::ModelTextureTable table;
    Client::Scenes::TerrainTiles tiles(store);
    tiles.BindTextureTable(table);
    std::vector<std::string> skipped;
    const auto result = tiles.Load(Client::Tests::RepoDataDir(), 24, skipped);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    for (std::uint16_t slot = 65; slot < 97; ++slot)
    {
        const auto loaded = store.Find(Client::Render::TextureSlot{slot});
        ASSERT_TRUE(loaded) << slot;
        EXPECT_EQ(loaded->sampling.filter, Client::Data::Textures::Filter::Linear);
        EXPECT_EQ(loaded->sampling.wrap, Client::Data::Textures::Wrap::Repeat);
        EXPECT_EQ(table.References(Client::Render::TextureSlot{slot}), 1);
    }
    EXPECT_EQ(store.Size(), 35u);
    tiles.Release();
    EXPECT_EQ(store.Size(), 0u);
}

TEST(KalimaObjectResourcesTest, TheSharedBssCycleAdvancesOnceAfterWindWithoutAddingRandomDraws)
{
    Client::Render::Models::ClothWorld world;
    EXPECT_EQ(world.waterTextureFrame, 0u);
    Client::Render::Models::ClothWorld expected = world;
    for (int step = 1; step <= 33; ++step)
    {
        Client::Render::Models::StepTheClothWorld(world);
        for (int walk = 0; walk < 5; ++walk)
            Client::Render::Models::WalkTheWind(expected);
        for (int close = 0; close < 4; ++close)
            static_cast<void>(expected.random.Next());
        EXPECT_EQ(world.waterTextureFrame, static_cast<std::uint32_t>(step % 32));
        EXPECT_EQ(world.random.State(), expected.random.State());
    }
    const auto projected = world;
    Client::Render::Models::StepTheClothWorld(world);
    EXPECT_EQ(projected.waterTextureFrame, 1u);
    EXPECT_EQ(world.waterTextureFrame, 2u);
}
