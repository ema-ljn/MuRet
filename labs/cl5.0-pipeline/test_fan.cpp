#include "Fan.h"

#include <gtest/gtest.h>

#include <vector>

// Labs::FanToTriangles: de un GL_TRIANGLE_FAN a sus triángulos (CL5.0, paso 4).
namespace
{
using Labs::FanToTriangles;
using Labs::Triangle;

// El quad de 0x621b60: (0, 1, 2) y (0, 2, 3), los dos con el vértice 0 y la diagonal 0-2.
TEST(FanTest, FourVerticesAreTwoTriangles)
{
    EXPECT_EQ(FanToTriangles(4), (std::vector<Triangle>{{0, 1, 2}, {0, 2, 3}}));
}

TEST(FanTest, ThreeVerticesAreOne)
{
    EXPECT_EQ(FanToTriangles(3), (std::vector<Triangle>{{0, 1, 2}}));
}

// Con 0, 1 o 2 vértices no hay triángulo: GL no dibuja nada, y el lote tampoco.
TEST(FanTest, LessThanThreeAreNone)
{
    EXPECT_TRUE(FanToTriangles(0).empty());
    EXPECT_TRUE(FanToTriangles(1).empty());
    EXPECT_TRUE(FanToTriangles(2).empty());
}

// n vértices son n − 2 triángulos, todos con el vértice 0 y en el orden del fan.
TEST(FanTest, EachTriangleSharesTheFirstVertex)
{
    EXPECT_EQ(FanToTriangles(6), (std::vector<Triangle>{{0, 1, 2}, {0, 2, 3}, {0, 3, 4}, {0, 4, 5}}));
}
} // namespace
