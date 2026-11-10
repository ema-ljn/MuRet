#include "Scenes/DynamicWorldLight.h"

#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainLight.h"
#include "World/TerrainCells.h"
#include "World/TerrainLighting.h"

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>

// Primer bloque RGB de 0x5fbf60: historia compartida y rango del último dibujo, no una copia
// completa de la base en cada frame. Los valores iniciales se leyeron del PE c32bbd5f, sin ejecutarlo.
namespace
{
using Client::Data::Terrain::CellIndex;
using Client::Data::Terrain::TerrainLightColor;
using Client::Scenes::DynamicWorldLight;
using Client::World::AddTerrainLight;
using Client::World::TerrainLighting;

constexpr TerrainLightColor Base{1.0f, 2.0f, 3.0f};
constexpr TerrainLightColor Retained{9.0f, 8.0f, 7.0f};

std::unique_ptr<TerrainLighting> BaseLight(const TerrainLightColor& color = Base)
{
    auto base = std::make_unique<TerrainLighting>();
    base->cells.fill(color);
    return base;
}

TEST(DynamicWorldLightTest, TheNativeInitialRangeCopiesTheFullBaseIntoTheZeroBssTable)
{
    DynamicWorldLight light;
    const DynamicWorldLight& view = light;
    EXPECT_EQ(view.Range().firstX, 0);
    EXPECT_EQ(view.Range().lastX, 255);
    EXPECT_EQ(view.Range().firstY, 0);
    EXPECT_EQ(view.Range().lastY, 255);
    for (const TerrainLightColor& cell : view.Lighting().cells)
    {
        for (float channel : cell)
        {
            ASSERT_EQ(std::bit_cast<std::uint32_t>(channel), 0u);
        }
    }
    const auto base = BaseLight();
    const TerrainLighting* const address = &view.Lighting();
    light.Update(*base);
    EXPECT_EQ(&view.Lighting(), address);
    EXPECT_EQ(view.Lighting().cells, base->cells);
}

TEST(DynamicWorldLightTest, AnUpdateCopiesOnlyThePublishedWindowAndItsThreeCellTail)
{
    DynamicWorldLight light;
    light.Lighting().cells.fill(Retained);
    const auto base = BaseLight();
    light.PublishRange({.firstX = 10, .lastX = 12, .firstY = 20, .lastY = 22});
    // El dibujo solo publica: todavía conserva la luz de antes, también adentro de la ventana.
    EXPECT_EQ(light.Lighting().cells[CellIndex(10, 20)], Retained);
    EXPECT_EQ(light.Range().firstX, 10);
    EXPECT_EQ(light.Range().lastX, 12);
    EXPECT_EQ(light.Range().firstY, 20);
    EXPECT_EQ(light.Range().lastY, 22);
    light.Update(*base);
    for (std::size_t cell : {CellIndex(10, 20), CellIndex(12, 22), CellIndex(15, 25)})
    {
        EXPECT_EQ(light.Lighting().cells[cell], Base);
    }
    for (std::size_t cell : {CellIndex(9, 20), CellIndex(16, 20), CellIndex(10, 19), CellIndex(10, 26)})
    {
        EXPECT_EQ(light.Lighting().cells[cell], Retained);
    }
    EXPECT_EQ(base->cells[CellIndex(10, 20)], Base);
}

TEST(DynamicWorldLightTest, TheWindowWrapsBothAxesWithoutClearingOutsideCells)
{
    DynamicWorldLight light;
    light.Lighting().cells.fill(Retained);
    const auto base = BaseLight();
    light.PublishRange({.firstX = 254, .lastX = 255, .firstY = -1, .lastY = 0});
    light.Update(*base);
    for (std::size_t cell : {CellIndex(254, 255), CellIndex(255, 0), CellIndex(0, 0), CellIndex(2, 3)})
    {
        EXPECT_EQ(light.Lighting().cells[cell], Base);
    }
    for (std::size_t cell : {CellIndex(253, 0), CellIndex(3, 3), CellIndex(0, 4), CellIndex(254, 254)})
    {
        EXPECT_EQ(light.Lighting().cells[cell], Retained);
    }
}

TEST(DynamicWorldLightTest, ANewSceneBaseUsesTheRangeAndDynamicHistoryLeftByThePreviousDraw)
{
    DynamicWorldLight shared;
    const auto loginBase = BaseLight();
    const auto gameBase = BaseLight({4.0f, 5.0f, 6.0f});
    // Un consumidor copia la ventana del login. El siguiente dibujo publica otra, sin resetear tabla.
    shared.PublishRange({.firstX = 10, .lastX = 10, .firstY = 20, .lastY = 20});
    shared.Update(*loginBase);
    shared.Lighting().cells[CellIndex(100, 100)] = Retained;
    shared.PublishRange({.firstX = 200, .lastX = 200, .firstY = 210, .lastY = 210});
    EXPECT_EQ(shared.Lighting().cells[CellIndex(200, 210)], (TerrainLightColor{}));
    shared.Update(*gameBase);
    EXPECT_EQ(shared.Lighting().cells[CellIndex(200, 210)], (TerrainLightColor{4.0f, 5.0f, 6.0f}));
    EXPECT_EQ(shared.Lighting().cells[CellIndex(203, 213)], (TerrainLightColor{4.0f, 5.0f, 6.0f}));
    EXPECT_EQ(shared.Lighting().cells[CellIndex(10, 20)], Base);
    EXPECT_EQ(shared.Lighting().cells[CellIndex(100, 100)], Retained);
    EXPECT_EQ(shared.Lighting().cells[CellIndex(199, 210)], (TerrainLightColor{}));
    EXPECT_EQ(shared.Range().firstX, 200);
    EXPECT_EQ(shared.Range().lastY, 210);
}

TEST(DynamicWorldLightTest, LightsChangeOnlyTheDynamicTableAndTheNextWindowRefreshesItsOwnCells)
{
    DynamicWorldLight light;
    const auto base = BaseLight(TerrainLightColor{});
    light.PublishRange({.firstX = 10, .lastX = 10, .firstY = 10, .lastY = 10});
    light.Update(*base);
    AddTerrainLight(light.Lighting(), 1000.0f, 1000.0f, {2.0f, 4.0f, 6.0f}, 2);
    EXPECT_EQ(light.Lighting().cells[CellIndex(10, 10)], (TerrainLightColor{2.0f, 4.0f, 6.0f}));
    EXPECT_EQ(light.Lighting().cells[CellIndex(9, 10)], (TerrainLightColor{1.0f, 2.0f, 3.0f}));
    EXPECT_EQ(base->cells[CellIndex(10, 10)], (TerrainLightColor{}));
    light.Update(*base);
    EXPECT_EQ(light.Lighting().cells[CellIndex(10, 10)], (TerrainLightColor{}));
    EXPECT_EQ(light.Lighting().cells[CellIndex(9, 10)], (TerrainLightColor{1.0f, 2.0f, 3.0f}));
}
} // namespace
