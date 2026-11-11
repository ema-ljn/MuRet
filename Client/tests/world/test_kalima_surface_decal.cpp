#include "World/KalimaSurfaceDecal.h"

#include "Render/ModelBatch.h"
#include "Render/Models/Cloth.h"
#include "Render/Pass3D.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "World/KalimaWaves.h"
#include "World/KalimaWorld.h"
#include "World/TerrainDecal.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

namespace
{
using Client::Render::ModelBatch;
using Client::World::DrawKalimaSurfaceDecal;
using Client::World::KalimaWorld;
using Client::World::TerrainDecal;

TerrainDecal Decal()
{
    return {.slot = 0x5fd,
            .x = 5000,
            .y = 5000,
            .width = 1,
            .height = 1,
            .color = {0.2f, 0.3f, 0.4f},
            .angle = 0,
            .alpha = 0.4f,
            .lift = 7};
}
TEST(KalimaSurfaceDecalTest, DrawsNativeFiftyUnitCellsAndKeepsTheCallersMaterial)
{
    KalimaWorld world(24);
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    auto state = batch.State();
    state.blend = Client::Render::ModelBlend::Subtractive;
    state.depthWrite = false;
    state.cullEnabled = true;
    state.texture2d = false;
    batch.SetState(state);
    ASSERT_TRUE(DrawKalimaSurfaceDecal(batch, world, Decal()).HasValue());
    ASSERT_EQ(batch.Vertices().size(), 150u);
    ASSERT_GE(batch.Commands().size(), 2u);
    EXPECT_EQ(batch.Commands()[0].operation, Client::Render::ModelStateOperation::Binding);
    EXPECT_EQ(batch.Vertices()[0].position.x, 4900);
    EXPECT_EQ(batch.Vertices()[0].position.y, 4900);
    EXPECT_EQ(batch.Vertices()[0].position.z, 407);
    EXPECT_EQ(batch.Vertices()[0].uv.x, -1.5f);
    EXPECT_EQ(batch.Vertices()[0].uv.y, -1.5f);
    EXPECT_EQ(batch.Vertices()[1].position.x, 4950);
    EXPECT_EQ(batch.Vertices()[2].position.y, 4950);
    EXPECT_EQ(batch.Vertices()[0].color, (Client::Render::Color{0.2f, 0.3f, 0.4f, 0.4f}));
    EXPECT_EQ(batch.State().blend, state.blend);
    EXPECT_EQ(batch.State().depthWrite, state.depthWrite);
    EXPECT_EQ(batch.State().cullEnabled, state.cullEnabled);
    EXPECT_EQ(batch.State().texture2d, state.texture2d);
    EXPECT_EQ(batch.State().texture, Client::Render::TextureSlot{0x5fd});
}
TEST(KalimaSurfaceDecalTest, UsesPlaneZeroRegardlessOfTheSelectedPlaneAndPeriodicFields)
{
    KalimaWorld world(24);
    Client::Render::Models::CrtRandom random;
    ASSERT_TRUE(world.Disturb(50, 50, 3, 1000000, random).HasValue());
    ASSERT_EQ(world.Waves().planes[0][100 * 256 + 100], 3u);
    ASSERT_TRUE(world.Update({1, 1000, 50, 50, random}).HasValue());
    ASSERT_TRUE(world.Waves().selected);
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    ASSERT_TRUE(DrawKalimaSurfaceDecal(batch, world, Decal()).HasValue());
    ASSERT_EQ(batch.Vertices().size(), 150u);
    EXPECT_EQ(batch.Vertices()[72].position.x, 5000);
    EXPECT_EQ(batch.Vertices()[72].position.y, 5000);
    EXPECT_EQ(batch.Vertices()[72].position.z, 410);
}
TEST(KalimaSurfaceDecalTest, ClipsCellsAtBothEdgesBeforeReadingWaveCorners)
{
    KalimaWorld world(24);
    ModelBatch batch;
    auto decal = Decal();
    decal.x = 0;
    decal.y = 0;
    batch.Begin(Client::Render::Pass3D{});
    ASSERT_TRUE(DrawKalimaSurfaceDecal(batch, world, decal).HasValue());
    ASSERT_EQ(batch.Vertices().size(), 54u);
    EXPECT_EQ(batch.Vertices()[0].position.x, 0);
    EXPECT_EQ(batch.Vertices()[0].position.y, 0);
    decal.x = 12750;
    decal.y = 12750;
    batch.Begin(Client::Render::Pass3D{});
    ASSERT_TRUE(DrawKalimaSurfaceDecal(batch, world, decal).HasValue());
    ASSERT_EQ(batch.Vertices().size(), 24u);
    EXPECT_EQ(batch.Vertices().back().position.y, 12750);
}
TEST(KalimaSurfaceDecalTest, RotatesTextureCoordinatesAndStretchesUWithoutMovingGeometry)
{
    KalimaWorld world(24);
    ModelBatch batch;
    auto decal = Decal();
    decal.angle = 90;
    batch.Begin(Client::Render::Pass3D{});
    ASSERT_TRUE(DrawKalimaSurfaceDecal(batch, world, decal).HasValue());
    ASSERT_EQ(batch.Vertices().size(), 150u);
    EXPECT_EQ(batch.Vertices()[0].position.x, 4900);
    EXPECT_EQ(batch.Vertices()[0].position.y, 4900);
    EXPECT_NEAR(batch.Vertices()[0].uv.x, 2.5f, 0.00001f);
    EXPECT_NEAR(batch.Vertices()[0].uv.y, -1.5f, 0.00001f);
    decal.width = 2;
    decal.angle = 0;
    batch.Begin(Client::Render::Pass3D{});
    ASSERT_TRUE(DrawKalimaSurfaceDecal(batch, world, decal).HasValue());
    ASSERT_EQ(batch.Vertices().size(), 294u);
    EXPECT_EQ(batch.Vertices()[0].uv.x, -2.5f);
    EXPECT_EQ(batch.Vertices()[0].uv.y, -1);
}
TEST(KalimaSurfaceDecalTest, PreservesTheNativeNeg32PhysicalTextureBinding)
{
    KalimaWorld world(24);
    ModelBatch batch;
    auto decal = Decal();
    for (const std::int32_t selector : {-1, std::numeric_limits<std::int32_t>::min()})
    {
        decal.slot = selector;
        batch.Begin(Client::Render::Pass3D{});
        ASSERT_TRUE(DrawKalimaSurfaceDecal(batch, world, decal).HasValue());
        EXPECT_FALSE(batch.State().texture);
        EXPECT_EQ(batch.State().rawTextureName, std::uint32_t{0} - static_cast<std::uint32_t>(selector));
        EXPECT_EQ(batch.Vertices().size(), 150u);
    }
}

TEST(KalimaSurfaceDecalTest, RejectsTheNonProgressingNativeFloatLoopAfterPublishingItsBinding)
{
    KalimaWorld world(24);
    ModelBatch batch;
    auto decal = Decal();
    decal.width = 16777215.0f; // ftol(side)+1:2^24; sumarfloat1 ya no progresa en el extremo.
    batch.Begin(Client::Render::Pass3D{});
    EXPECT_FALSE(DrawKalimaSurfaceDecal(batch, world, decal).HasValue());
    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.Commands()[0].operation, Client::Render::ModelStateOperation::Binding);
    EXPECT_TRUE(batch.Vertices().empty());
}

TEST(KalimaSurfaceDecalTest, RejectsAnUnknownPositiveSlotBeforePublishingAFalseBinding)
{
    KalimaWorld world(24);
    ModelBatch batch;
    auto decal = Decal();
    decal.slot = static_cast<std::int32_t>(Client::Render::OriginalTextureSlotCount);
    batch.Begin(Client::Render::Pass3D{});
    EXPECT_FALSE(DrawKalimaSurfaceDecal(batch, world, decal).HasValue());
    EXPECT_TRUE(batch.Commands().empty());
    EXPECT_TRUE(batch.Vertices().empty());
}

TEST(KalimaSurfaceDecalTest, PublishesTheBindingBeforeAConversionFailureOrAnEmptyRegion)
{
    KalimaWorld world(24);
    ModelBatch batch;
    auto decal = Decal();
    decal.x = std::numeric_limits<float>::infinity();
    batch.Begin(Client::Render::Pass3D{});
    EXPECT_FALSE(DrawKalimaSurfaceDecal(batch, world, decal).HasValue());
    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.Commands()[0].operation, Client::Render::ModelStateOperation::Binding);
    EXPECT_TRUE(batch.Vertices().empty());
    decal.x = 50000;
    batch.Begin(Client::Render::Pass3D{});
    ASSERT_TRUE(DrawKalimaSurfaceDecal(batch, world, decal).HasValue());
    EXPECT_TRUE(batch.Vertices().empty());
    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.State().texture, Client::Render::TextureSlot{0x5fd});
}
} // namespace
