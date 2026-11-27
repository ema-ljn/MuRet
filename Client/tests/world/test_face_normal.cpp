#include "Terrain/FaceNormal.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cstdint>

// Client::Terrain::FaceNormal, 0x5fd000 with the ASM's PC53 profile, the normal the terrain light and the picking use.
// The expected bits come from Client/docs/research/scripts/game/maps/terrain_light.py, a separate computation of the
// same ASM.
namespace
{
using Client::Terrain::FaceNormal;

using ComponentBits = std::array<std::uint32_t, 3>;

ComponentBits BitsOf(const glm::vec3& normal)
{
    return {std::bit_cast<std::uint32_t>(normal.x), std::bit_cast<std::uint32_t>(normal.y),
            std::bit_cast<std::uint32_t>(normal.z)};
}

// (p2 − p1) × (p3 − p1): with the last two points swapped, the normal flips.
TEST(FaceNormalTest, TheNormalIsTheCrossProductOfTheTwoEdgesFromTheFirstPoint)
{
    const glm::vec3 origin{0.0f, 0.0f, 0.0f};
    const glm::vec3 alongX{100.0f, 0.0f, 0.0f};
    const glm::vec3 alongY{0.0f, 100.0f, 0.0f};
    EXPECT_EQ(FaceNormal(origin, alongX, alongY), glm::vec3(0.0f, 0.0f, 1.0f));
    EXPECT_EQ(FaceNormal(origin, alongY, alongX), glm::vec3(0.0f, 0.0f, -1.0f));
}

// Cell (10, 20) of the light, as 0x5f8b10 builds it: nz and the length stay in 53 bits, and each component is divided
// in 53 bits. With glm::cross, glm::length and the division in float, all three would be one ULP more (0x3f3487c1,
// 0xbecb18b9 and 0x3f167121).
TEST(FaceNormalTest, ACellOfTheLightKeepsNzAndTheLengthInTheRegister)
{
    const glm::vec3 normal =
        FaceNormal({1100.0f, 2000.0f, 34.5f}, {1100.0f, 2100.0f, 102.0f}, {1000.0f, 2100.0f, 222.0f});
    EXPECT_EQ(BitsOf(normal), (ComponentBits{0x3f3487c0U, 0xbecb18b8U, 0x3f167120U}));
}

// With fractional coordinates the differences and products do not fit a float, and the spills of vy, nx, vx, ux and
// ny change the result. All in float would give 0x3f008dc7, 0x3f49b0ae and 0x3eb68c6e; all in 53 bits, without the
// spills, 0x3f008dc8, 0x3f49b0ae and 0x3eb68c6e.
TEST(FaceNormalTest, TheFloatSpillsOfTheAsmChangeTheResult)
{
    const glm::vec3 normal = FaceNormal({-822.3125f, 829.9375f, -389.8125f}, {599.0625f, -460.6875f, 460.1875f},
                                        {580.8125f, -318.75f, 172.25f});
    EXPECT_EQ(BitsOf(normal), (ComponentBits{0x3f008dc9U, 0x3f49b0adU, 0x3eb68c6fU}));
}
} // namespace
