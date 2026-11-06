#include "GameLogic/PathFinder.h"

#include "GameLogic/Path.h"
#include "GameLogic/PathCell.h"
#include "World/TerrainWalls.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>

namespace
{
using Client::GameLogic::Path;
using Client::GameLogic::PathCell;
using Client::GameLogic::PathFinder;
using Client::World::TerrainWalls;

void ExpectCells(const std::optional<Path>& path, std::initializer_list<PathCell> expected)
{
    ASSERT_TRUE(path);
    ASSERT_EQ(path->count, expected.size());
    std::size_t i = 0;
    for (const PathCell cell : expected)
    {
        EXPECT_EQ(path->cells[i].x, cell.x) << i;
        EXPECT_EQ(path->cells[i].y, cell.y) << i;
        ++i;
    }
}

// Un pasillo de una sola fila: bloquea los atajos y deja observable la segunda pasada.
void MakeCorridor(TerrainWalls& walls)
{
    walls.cells.fill(4);
    for (std::size_t x = 90; x <= 110; ++x)
    {
        walls.cells[100 * 256 + x] = 0;
    }
    walls.cells[100 * 256 + 100] = 2;
}

TEST(PathFinderTest, CrossesTheDiagonalEvenWhenBothSideCellsAreWalls)
{
    Client::World::TerrainWalls walls;
    walls.cells.fill(4);
    walls.cells[10 * 256 + 10] = 0;
    walls.cells[11 * 256 + 11] = 0;
    Client::GameLogic::PathFinder finder(walls);
    const auto path = finder.Find({10, 10}, {11, 11});
    ASSERT_TRUE(path);
    ASSERT_EQ(path->count, 2);
    EXPECT_EQ(path->cells[0], (Client::GameLogic::PathCell{10, 10}));
    EXPECT_EQ(path->cells[1], (Client::GameLogic::PathCell{11, 11}));
    EXPECT_EQ(path->current, 0);
    EXPECT_EQ(path->progress, 0);
}

TEST(PathFinderTest, TheStartOnRowOrColumnZeroFails)
{
    TerrainWalls walls;
    PathFinder finder(walls);
    EXPECT_FALSE(finder.Find({0, 5}, {3, 5}));
    EXPECT_FALSE(finder.Find({5, 0}, {5, 3}));
    EXPECT_FALSE(finder.Find({0, 0}, {1, 1}));
}

TEST(PathFinderTest, ClickingTheStartDoesNotMakeAWalk)
{
    TerrainWalls walls;
    PathFinder finder(walls);
    EXPECT_FALSE(finder.Find({100, 100}, {100, 100}));
}

TEST(PathFinderTest, EveryAdjacentGoalIsOneStep)
{
    TerrainWalls walls;
    PathFinder finder(walls);
    constexpr std::array<PathCell, 8> goals{
        {{99, 99}, {100, 99}, {101, 99}, {99, 100}, {101, 100}, {99, 101}, {100, 101}, {101, 101}}};
    for (const PathCell goal : goals)
    {
        ExpectCells(finder.Find({100, 100}, goal), {{100, 100}, goal});
    }
}

// 4eb1c5 conserva el primer padre cerrado con costo mínimo, en el orden de 6d5d80.
TEST(PathFinderTest, EqualClosedParentCostsKeepTheFirstNeighbor)
{
    TerrainWalls walls;
    PathFinder finder(walls);
    ExpectCells(finder.Find({100, 100}, {102, 101}), {{100, 100}, {101, 100}, {102, 101}});
    ExpectCells(finder.Find({100, 100}, {101, 102}), {{100, 100}, {100, 101}, {101, 102}});
    ExpectCells(finder.Find({100, 100}, {98, 99}), {{100, 100}, {99, 99}, {98, 99}});
}

// 4eb2b9..4eb2cd y 4eb3ac..4eb3bf: h(1,1)=h(1,0)=4. El primer mínimo es (104,99).
// Al cerrar, los empates de costo vuelven a elegir la fila y=99 por el primer vecino cerrado.
TEST(PathFinderTest, AnOccupiedGoalEndsOnTheDiagonalByTheTieOrder)
{
    TerrainWalls walls;
    walls.cells[100 * 256 + 105] = 2;
    PathFinder finder(walls);
    ExpectCells(finder.Find({100, 100}, {105, 100}), {{100, 100}, {101, 99}, {102, 99}, {103, 99}, {104, 99}});
}

TEST(PathFinderTest, AnAdjacentOccupiedGoalDoesNotDisplaceAnEquallyNearStart)
{
    TerrainWalls walls;
    walls.cells[100 * 256 + 101] = 2;
    PathFinder finder(walls);
    EXPECT_FALSE(finder.Find({100, 100}, {101, 100}));
}

TEST(PathFinderTest, InTheSafeZoneTheSecondTryWalksThroughCharacters)
{
    TerrainWalls walls;
    MakeCorridor(walls);
    walls.cells[100 * 256 + 95] = 1;
    PathFinder finder(walls);
    ExpectCells(finder.Find({95, 100}, {105, 100}), {{95, 100},
                                                     {96, 100},
                                                     {97, 100},
                                                     {98, 100},
                                                     {99, 100},
                                                     {100, 100},
                                                     {101, 100},
                                                     {102, 100},
                                                     {103, 100},
                                                     {104, 100},
                                                     {105, 100}});
    // Basta con que sea segura la meta; el resto del corredor no cambia.
    walls.cells[100 * 256 + 95] = 0;
    walls.cells[100 * 256 + 105] = 1;
    const auto path = finder.Find({95, 100}, {105, 100});
    ASSERT_TRUE(path);
    EXPECT_EQ(path->count, 11);
    EXPECT_EQ(path->cells[10], (PathCell{105, 100}));
}

// 0x4eacbe..0x4eacc9 usa iWall=4; la celda segura ocupada vale 3 y debe poder atravesarse.
TEST(PathFinderTest, TheSafeZoneSecondTryAdmitsTheSafeCharacterWordThree)
{
    TerrainWalls walls;
    MakeCorridor(walls);
    for (std::size_t x = 90; x <= 110; ++x)
    {
        walls.cells[100 * 256 + x] = 1;
    }
    walls.cells[100 * 256 + 100] = 3;
    PathFinder finder(walls);
    ExpectCells(finder.Find({95, 100}, {105, 100}), {{95, 100},
                                                     {96, 100},
                                                     {97, 100},
                                                     {98, 100},
                                                     {99, 100},
                                                     {100, 100},
                                                     {101, 100},
                                                     {102, 100},
                                                     {103, 100},
                                                     {104, 100},
                                                     {105, 100}});
}

TEST(PathFinderTest, OutsideTheSafeZoneCharactersStopTheNearestPath)
{
    TerrainWalls walls;
    MakeCorridor(walls);
    PathFinder finder(walls);
    ExpectCells(finder.Find({95, 100}, {105, 100}), {{95, 100}, {96, 100}, {97, 100}, {98, 100}, {99, 100}});
}

TEST(PathFinderTest, AnOccupiedGoalDisablesWalkingThroughCharactersEvenFromTheSafeZone)
{
    TerrainWalls walls;
    MakeCorridor(walls);
    walls.cells[100 * 256 + 95] = 1;
    walls.cells[100 * 256 + 105] = 3;
    PathFinder finder(walls);
    ExpectCells(finder.Find({95, 100}, {105, 100}), {{95, 100}, {96, 100}, {97, 100}, {98, 100}, {99, 100}});
}

TEST(PathFinderTest, SafeZoneDoesNotAdmitWallFour)
{
    TerrainWalls walls;
    MakeCorridor(walls);
    walls.cells[100 * 256 + 95] = 1;
    walls.cells[100 * 256 + 100] = 4;
    PathFinder finder(walls);
    ExpectCells(finder.Find({95, 100}, {105, 100}), {{95, 100}, {96, 100}, {97, 100}, {98, 100}, {99, 100}});
}

TEST(PathFinderTest, OnlyTheFirstFifteenCellsAreKept)
{
    TerrainWalls walls;
    PathFinder finder(walls);
    const auto path = finder.Find({100, 100}, {120, 100});
    ASSERT_TRUE(path);
    ASSERT_EQ(path->count, 15);
    for (std::uint8_t i = 0; i < 15; ++i)
    {
        EXPECT_EQ(path->cells[i], (PathCell{static_cast<std::uint8_t>(100 + i), 100}));
    }
}

// 06 § Caminar: 30 en recta alcanza en 500 extracciones; 40 agota esa pasada. Las 50 de la
// segunda dejan diez pasos, así que el resultado permite distinguir ambos límites del corte en 15.
TEST(PathFinderTest, TheExactSearchGivesUpAfter500AndTheNearestAfter50)
{
    TerrainWalls walls;
    PathFinder finder(walls);
    const auto exact = finder.Find({100, 100}, {130, 100});
    ASSERT_TRUE(exact);
    ASSERT_EQ(exact->count, 15);
    EXPECT_EQ(exact->cells[14], (PathCell{114, 100}));
    const auto nearest = finder.Find({100, 100}, {140, 100});
    ASSERT_TRUE(nearest);
    ASSERT_EQ(nearest->count, 11);
    EXPECT_EQ(nearest->cells[10], (PathCell{110, 100}));
}

TEST(PathFinderTest, NeighborFlagsIgnoreOnlyTheThreeHighBitsOfTheLowByte)
{
    TerrainWalls walls;
    walls.cells.fill(4);
    walls.cells[100 * 256 + 100] = 0;
    walls.cells[100 * 256 + 101] = 0xe0;
    PathFinder finder(walls);
    ExpectCells(finder.Find({100, 100}, {101, 100}), {{100, 100}, {101, 100}});
    walls.cells[100 * 256 + 101] = 0x120;
    EXPECT_FALSE(finder.Find({100, 100}, {101, 100}));
    walls.cells[100 * 256 + 101] = 0x22;
    EXPECT_FALSE(finder.Find({100, 100}, {101, 100}));
}

TEST(PathFinderTest, TheExactGoalCheckUsesTheRawWordAndOnlyBitTwentyBypassesIt)
{
    TerrainWalls walls;
    PathFinder finder(walls);
    // El 0x40 deja pasar vecinos, pero el chequeo exacto de meta lo rechaza antes de buscar.
    walls.cells[100 * 256 + 130] = 0x40;
    const auto nearest = finder.Find({100, 100}, {130, 100});
    ASSERT_TRUE(nearest);
    EXPECT_EQ(nearest->count, 11);
    walls.cells[100 * 256 + 130] = 0x60;
    const auto exact = finder.Find({100, 100}, {130, 100});
    ASSERT_TRUE(exact);
    EXPECT_EQ(exact->count, 15);
}

// 0x4eb176..0x4eb18a (cierre) y 0x4eb22a..0x4eb247 (apertura) comprueban x/y por separado;
// el índice lineal de la apertura se calcula recién en 0x4eb24d. No hay wrap entre filas.
TEST(PathFinderTest, TheGoalMayBeOnRowOrColumnZeroWithoutWrappingNeighbors)
{
    TerrainWalls walls;
    PathFinder finder(walls);
    ExpectCells(finder.Find({1, 1}, {0, 0}), {{1, 1}, {0, 0}});
    ExpectCells(finder.Find({254, 254}, {255, 255}), {{254, 254}, {255, 255}});
    walls.cells.fill(4);
    walls.cells[100 * 256 + 255] = 0;
    walls.cells[100 * 256] = 0;
    EXPECT_FALSE(finder.Find({255, 100}, {0, 100}));
}

// 0x4eae0a..0x4eae0d borra antes de 0x4eae16..0x4eae38: la primera pasada alcanza la meta.
// Si comprobara primero el WORD 2, caería al más cercano de 50 extracciones y daría sólo 11 celdas.
TEST(PathFinderTest, ClearGoalRunsBeforeTheExactGoalCheck)
{
    TerrainWalls walls;
    walls.cells[100 * 256 + 130] = 2;
    PathFinder finder(walls);
    const auto path = finder.Find({100, 100}, {130, 100}, true);
    ASSERT_TRUE(path);
    ASSERT_EQ(path->count, 15);
    EXPECT_EQ(path->cells[14], (PathCell{114, 100}));
    EXPECT_EQ(walls.cells[100 * 256 + 130], 0);
}

TEST(PathFinderTest, ClearGoalWritesTheWholeWordEvenIfTheWalkFails)
{
    TerrainWalls walls;
    walls.cells.fill(0xffff);
    PathFinder finder(walls);
    EXPECT_FALSE(finder.Find({100, 100}, {105, 100}, true));
    EXPECT_EQ(walls.cells[100 * 256 + 105], 0);
    EXPECT_EQ(walls.cells[100 * 256 + 104], 0xffff);
    // La salida temprana de sx/sy en 0 ocurre antes del clearGoal.
    walls.cells[100 * 256 + 105] = 0x106;
    EXPECT_FALSE(finder.Find({0, 100}, {105, 100}, true));
    EXPECT_EQ(walls.cells[100 * 256 + 105], 0x106);
}

// 4eae0d borra la meta antes de que 4eac7a..4eacce consulte de nuevo la zona segura.
TEST(PathFinderTest, ClearingTheOnlySafeCellBeforeTheSecondTryDoesNotAllowCrossingCharacters)
{
    TerrainWalls walls;
    MakeCorridor(walls);
    walls.cells[100 * 256 + 105] = 1;
    PathFinder finder(walls);
    ExpectCells(finder.Find({95, 100}, {105, 100}, true), {{95, 100}, {96, 100}, {97, 100}, {98, 100}, {99, 100}});
    EXPECT_EQ(walls.cells[100 * 256 + 105], 0);
}

// El original limpia el rango inclusivo al entrar (0x4ead7b..0x4eadbe). Meta, inicio y vecinos
// amplían ese rango: stores 0x4eae5f/0x4eae71, 0x4eb0b9/0x4eb0cb y 0x4eb342/0x4eb35d.
// La meta entra al rango aunque después no se abra: su GoalFlag tampoco queda para otra llamada.
TEST(PathFinderTest, TheStartOccupancyDoesNotPreventWalkingAndCallsDoNotRetainSearchFlags)
{
    TerrainWalls walls;
    walls.cells[100 * 256 + 100] = 2;
    PathFinder finder(walls);
    ExpectCells(finder.Find({100, 100}, {101, 100}), {{100, 100}, {101, 100}});
    EXPECT_FALSE(finder.Find({100, 100}, {100, 100}));
    ExpectCells(finder.Find({100, 100}, {101, 100}), {{100, 100}, {101, 100}});
}

// Camino en U sin atajos: los extremos de la base siguen siendo pared, y sólo el vértice central
// conecta las columnas por dos diagonales. Cada celda abierta tiene un único vecino hacia delante.
void MakeU(TerrainWalls& walls, std::size_t bottom, std::size_t rightTop)
{
    walls.cells.fill(4);
    for (std::size_t y = 5; y < bottom; ++y)
    {
        walls.cells[y * 256 + 10] = 0;
    }
    walls.cells[bottom * 256 + 11] = 0;
    for (std::size_t y = rightTop; y < bottom; ++y)
    {
        walls.cells[y * 256 + 12] = 0;
    }
}

TEST(PathFinderTest, TheFiveHundredthExtractionAndReconstructionCellStillSucceed)
{
    TerrainWalls walls;
    // 249 de la izquierda + vértice + 250 de la derecha = 500, incluida la meta (12,4).
    MakeU(walls, 254, 4);
    PathFinder finder(walls);
    const auto path = finder.Find({10, 5}, {12, 4});
    ASSERT_TRUE(path);
    ASSERT_EQ(path->count, 15);
    EXPECT_EQ(path->cells[14], (PathCell{10, 19}));
    // Un desvío muerto con h=13 sale antes de que se agote el corredor. La meta sería la 501.ª.
    // En la segunda pasada las primeras 50 no mejoran h(inicio)=9, por eso devuelve una sola celda.
    walls.cells[5 * 256 + 9] = 0;
    EXPECT_FALSE(finder.Find({10, 5}, {12, 4}));
}

TEST(PathFinderTest, TheFiftiethExtractionMayImproveTheNearestButTheFiftyFirstCannot)
{
    TerrainWalls walls;
    // Meta ocupada: va directo a la segunda pasada. 25 de izquierda + vértice + 24 de derecha
    // hasta (12,6) = 50. h(12,6)=8 es la primera menor que h(inicio)=9.
    MakeU(walls, 30, 5);
    walls.cells[4 * 256 + 12] = 2;
    PathFinder finder(walls);
    const auto path = finder.Find({10, 5}, {12, 4});
    ASSERT_TRUE(path);
    ASSERT_EQ(path->count, 15);
    EXPECT_EQ(path->cells[14], (PathCell{10, 19}));
    // Con una extracción muerta más, (12,6) sería la 51.ª: ninguna de las 50 mejora el inicio.
    walls.cells[5 * 256 + 9] = 0;
    EXPECT_FALSE(finder.Find({10, 5}, {12, 4}));
}
} // namespace
