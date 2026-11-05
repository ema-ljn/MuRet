#include "World/TerrainCulling.h"

#include "App/WindowState.h"
#include "Render/Pass3D.h"
#include "Scenes/SceneCameras.h"

#include <gtest/gtest.h>

#include <cstdint>

// Client::World::TerrainRangeOf y TerrainPolygonOf con la cámara del login (06 § El terreno del login):
// el rango de bloques de 0x5fbb00 y el trapecio de 0x5fb570, que en el login no recorta nada.
namespace
{
using Client::World::CullingScene;
using Client::World::PolygonContains;
using Client::World::TerrainPolygonOf;
using Client::World::TerrainRangeOf;

constexpr std::uint8_t Login = 55;

TEST(TerrainCullingTest, TheLoginSeesBlocksFrom0To128And120To248)
{
    const auto camera = Client::Scenes::LoginCamera();
    const auto pass =
        Client::Render::MakePass3D(Client::App::PixelSize{640, 480}, Client::Scenes::WorldViewport, camera);
    const auto range = TerrainRangeOf(pass.view, camera);
    EXPECT_EQ(range.firstX, 0);
    EXPECT_EQ(range.lastX, 128);
    EXPECT_EQ(range.firstY, 120);
    // DAT_006c9d6c queda en 251, la pinza a 0xfb, que no es múltiplo de 4: el lazo de a 4 desde el 120
    // para en el bloque 248, que llega a la fila 251.
    EXPECT_EQ(range.lastY, 251);
}

// El trapecio del login, girado 36°, cubre todo el mapa: cada bloque y cada celda del rango pasan.
TEST(TerrainCullingTest, TheLoginPolygonPassesEveryCell)
{
    const auto polygon = TerrainPolygonOf(CullingScene::Login, Login, Client::Scenes::LoginHeroPosition,
                                          Client::Scenes::LoginCamera().angles.z);
    EXPECT_FALSE(polygon.everything);
    // Las esquinas del código real emulado, bit a bit (06 § El terreno del login).
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
            EXPECT_TRUE(PolygonContains(polygon, x + 0.5, y + 0.5, Client::World::CellMargin)) << x << ", " << y;
        }
    }
}

// En las escenas que no son 2, 4 ni 5, 0x5fba90 deja pasar todo; en el juego, el trapecio va a 45° y
// deja afuera lo que está lejos del héroe.
TEST(TerrainCullingTest, OnlyThreeScenesCut)
{
    const auto other = TerrainPolygonOf(CullingScene::Other, 0, {14350.0f, 12050.0f, 165.0f}, -45.0f);
    EXPECT_TRUE(PolygonContains(other, 0.5, 0.5, 0.0));
    const auto game = TerrainPolygonOf(CullingScene::Game, 0, {14350.0f, 12050.0f, 165.0f}, -45.0f);
    EXPECT_TRUE(PolygonContains(game, 143.5, 120.5, 0.0));
    EXPECT_FALSE(PolygonContains(game, 0.5, 0.5, 0.0));
}
} // namespace
