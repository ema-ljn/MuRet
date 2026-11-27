#include "Terrain/TerrainCulling.h"

#include "Core/PixelSize.h"
#include "Render/Pass3D.h"
#include "Camera/SceneCameras.h"

#include <gtest/gtest.h>

#include <cstdint>

// Client::Terrain::TerrainRangeOf and TerrainPolygonOf with the login camera (06 § El terreno del login): the block
// range of 0x5fbb00 and the trapezoid of 0x5fb570, which cuts nothing at login.
namespace
{
using Client::Terrain::CullingScene;
using Client::Terrain::PolygonContains;
using Client::Terrain::TerrainPolygonOf;
using Client::Terrain::TerrainRangeOf;

constexpr std::uint8_t Login = 55;

TEST(TerrainCullingTest, TheLoginSeesBlocksFrom0To128And120To248)
{
    const auto camera = Client::Camera::LoginCamera();
    const auto pass =
        Client::Render::MakePass3D(Client::Core::PixelSize{640, 480}, Client::Camera::WorldViewport, camera);
    const auto range = TerrainRangeOf(pass.view, camera);
    EXPECT_EQ(range.firstX, 0);
    EXPECT_EQ(range.lastX, 128);
    EXPECT_EQ(range.firstY, 120);
    // DAT_006c9d6c stays at 251, the clamp at 0xfb, which is not a multiple of 4: the loop by 4 from 120 stops at
    // block 248, which reaches row 251.
    EXPECT_EQ(range.lastY, 251);
}

// The login trapezoid, turned 36°, covers the whole map: every block and every cell of the range passes.
TEST(TerrainCullingTest, TheLoginPolygonPassesEveryCell)
{
    const auto polygon = TerrainPolygonOf(CullingScene::Login, Login, Client::Camera::LoginHeroPosition,
                                          Client::Camera::LoginCamera().angles.z);
    EXPECT_FALSE(polygon.everything);
    // The corners from the emulated real code, bit for bit (06 § El terreno del login).
    EXPECT_EQ(polygon.x[0], -1627.8109130859375f);
    EXPECT_EQ(polygon.x[1], 336.15875244140625f);
    EXPECT_EQ(polygon.x[2], 965.2880249023438f);
    EXPECT_EQ(polygon.x[3], 74.074951171875f);
    EXPECT_EQ(polygon.y[0], 478.1072998046875f);
    EXPECT_EQ(polygon.y[1], 1905.0146484375f);
    EXPECT_EQ(polygon.y[2], -88.8705062866211f);
    EXPECT_EQ(polygon.y[3], -736.374755859375f);
    for (int y = 0; y < 256; y += 5)
    {
        for (int x = 0; x < 256; x += 5)
        {
            EXPECT_TRUE(PolygonContains(polygon, x + 0.5, y + 0.5, Client::Terrain::CellMargin)) << x << ", " << y;
        }
    }
}

// In scenes other than 2, 4 and 5, 0x5fba90 lets everything through; in the game, the trapezoid is at 45° and leaves
// out what is far from the hero.
TEST(TerrainCullingTest, OnlyThreeScenesCut)
{
    const auto other = TerrainPolygonOf(CullingScene::Other, 0, {14350.0f, 12050.0f, 165.0f}, -45.0f);
    EXPECT_TRUE(PolygonContains(other, 0.5, 0.5, 0.0));
    const auto game = TerrainPolygonOf(CullingScene::Game, 0, {14350.0f, 12050.0f, 165.0f}, -45.0f);
    EXPECT_TRUE(PolygonContains(game, 143.5, 120.5, 0.0));
    EXPECT_FALSE(PolygonContains(game, 0.5, 0.5, 0.0));
}
} // namespace
