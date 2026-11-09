#include "World/FaceNormal.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cstdint>

// Client::World::FaceNormal, 0x5fd000 con el perfil PC53 del ASM, la normal que usan la luz del terreno
// y el picking. Los bits esperados salen de Client/docs/research/scripts/game/maps/terrain_light.py, un
// cálculo aparte del mismo ASM.
namespace
{
using Client::World::FaceNormal;

using ComponentBits = std::array<std::uint32_t, 3>;

ComponentBits BitsOf(const glm::vec3& normal)
{
    return {std::bit_cast<std::uint32_t>(normal.x), std::bit_cast<std::uint32_t>(normal.y),
            std::bit_cast<std::uint32_t>(normal.z)};
}

// (p2 − p1) × (p3 − p1): con los dos últimos puntos cambiados, la normal se da vuelta.
TEST(FaceNormalTest, TheNormalIsTheCrossProductOfTheTwoEdgesFromTheFirstPoint)
{
    const glm::vec3 origin{0.0f, 0.0f, 0.0f};
    const glm::vec3 alongX{100.0f, 0.0f, 0.0f};
    const glm::vec3 alongY{0.0f, 100.0f, 0.0f};
    EXPECT_EQ(FaceNormal(origin, alongX, alongY), glm::vec3(0.0f, 0.0f, 1.0f));
    EXPECT_EQ(FaceNormal(origin, alongY, alongX), glm::vec3(0.0f, 0.0f, -1.0f));
}

// La celda (10, 20) de la luz, como la arma 0x5f8b10: nz y el largo quedan en 53 bits, y cada componente
// se divide en 53 bits. Con glm::cross, glm::length y la división en float, los tres darían un ULP más
// (0x3f3487c1, 0xbecb18b9 y 0x3f167121).
TEST(FaceNormalTest, ACellOfTheLightKeepsNzAndTheLengthInTheRegister)
{
    const glm::vec3 normal =
        FaceNormal({1100.0f, 2000.0f, 34.5f}, {1100.0f, 2100.0f, 102.0f}, {1000.0f, 2100.0f, 222.0f});
    EXPECT_EQ(BitsOf(normal), (ComponentBits{0x3f3487c0U, 0xbecb18b8U, 0x3f167120U}));
}

// Con coordenadas con fracción, las restas y los productos no entran en un float, y los spills de vy,
// nx, vx, ux y ny cambian el resultado. Todo en float daría 0x3f008dc7, 0x3f49b0ae y 0x3eb68c6e; todo
// en 53 bits, sin los spills, 0x3f008dc8, 0x3f49b0ae y 0x3eb68c6e.
TEST(FaceNormalTest, TheFloatSpillsOfTheAsmChangeTheResult)
{
    const glm::vec3 normal = FaceNormal({-822.3125f, 829.9375f, -389.8125f}, {599.0625f, -460.6875f, 460.1875f},
                                        {580.8125f, -318.75f, 172.25f});
    EXPECT_EQ(BitsOf(normal), (ComponentBits{0x3f008dc9U, 0x3f49b0adU, 0x3eb68c6fU}));
}
} // namespace
