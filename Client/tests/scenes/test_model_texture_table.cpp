#include "Data/Textures/OztTexture.h"
#include "Data/Textures/TextureSet.h"
#include "Scenes/ModelTextureTable.h"
#include "Data/Models/Model.h"
#include "Render/Models/ModelTextureSlots.h"
#include "Render/TextureSlot.h"
#include "support/FakeTextureStore.h"
#include "support/TextureFiles.h"
#include <glm/ext/vector_float2.hpp>
#include <gtest/gtest.h>
#include <span>
#include <utility>

namespace
{
TEST(ModelTextureTableTest, SharedNamesAndReferencesOutliveAMapAndClearAtTheLastRelease)
{
    Client::Tests::FakeTextureStore store;
    Client::Scenes::ModelTextureTable table;
    table.Bind(store);
    Client::Data::Models::Model model;
    model.meshes.emplace_back().textureName = "fauna.tga";
    auto slots = table.Slots().Assign(model, "Monster\\", {});
    auto decoded = Client::Data::Textures::LoadOztTexture(Client::Tests::PlainOzt(2, 2));
    ASSERT_TRUE(decoded.HasValue());
    Client::Data::Textures::LoadedTexture loaded{slots[0].value, {}, std::move(decoded).Value()};
    ASSERT_TRUE(table.Upload(loaded).HasValue());
    table.Acquire(slots[0]);
    EXPECT_EQ(table.References(slots[0]), 2);
    table.Release(slots[0]);
    EXPECT_EQ(store.Size(), 1U);
    EXPECT_EQ(table.Slots().NameOf(slots[0]), "fauna.tga");
    table.Release(slots[0]);
    EXPECT_EQ(store.Size(), 0U);
    EXPECT_TRUE(table.Slots().NameOf(slots[0]).empty());
}
} // namespace

namespace
{
TEST(ModelTextureTableTest, OverwritingAReferencedSlotChangesThePhysicalMetadataForEveryModel)
{
    Client::Tests::FakeTextureStore store;
    Client::Scenes::ModelTextureTable table;
    table.Bind(store);
    auto decoded = Client::Data::Textures::LoadOztTexture(Client::Tests::PlainOzt(2, 2));
    ASSERT_TRUE(decoded.HasValue());
    Client::Data::Textures::LoadedTexture first{0x41c, {}, std::move(decoded).Value()};
    ASSERT_TRUE(table.Upload(first).HasValue());
    table.Acquire({0x41c});
    table.Slots().SetName({0x41c}, "new.tga");
    auto larger = Client::Data::Textures::LoadOztTexture(Client::Tests::PlainOzt(8, 8));
    ASSERT_TRUE(larger.HasValue());
    Client::Data::Textures::LoadedTexture replacement{0x41c, {}, std::move(larger).Value()};
    ASSERT_TRUE(table.Upload(replacement).HasValue());
    EXPECT_EQ(table.References({0x41c}), 2);
    ASSERT_TRUE(table.Metadata(0x41c));
    EXPECT_EQ(table.Metadata(0x41c)->size, glm::vec2(8));
    table.Release({0x41c});
    EXPECT_TRUE(store.Find({0x41c}));
    table.Release({0x41c});
    EXPECT_FALSE(store.Find({0x41c}));
}
} // namespace
