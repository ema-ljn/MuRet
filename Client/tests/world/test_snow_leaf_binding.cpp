#include "World/Leaves.h"

#include "Render/Models/Cloth.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

namespace
{
// Detecta cámara inventada/requerida, dibujos extra y lectura de Z posterior al store de ángulos.
TEST(SnowLeafBindingTest, BorrowsMapAndHeroWithoutCameraAndKeepsTheFiveDrawContract)
{
    Client::World::Leaf leaf;
    leaf.type = 2;                    // Alias deliberado: el guard se evalúa antes de sobrescribir type.
    leaf.angles = {1000, 2000, 3000}; // Hero Z se consume antes de que el creador escriba angles.
    leaf.alive = 0x80;
    leaf.light = {3, 4, 5};
    leaf.spin = {6, 7, 8};
    leaf.start = {9, 10, 11};
    Client::Render::Models::CrtRandom random(74);
    ASSERT_TRUE(Client::World::CreateSnowLeaf(leaf, leaf.type, leaf.angles, random));
    EXPECT_EQ(random.State(), 0x4825ce39u);
    EXPECT_EQ(leaf.type, 101);
    EXPECT_EQ(leaf.scale, 10);
    EXPECT_EQ(leaf.position, glm::vec3(869, 1767, 3250));
    EXPECT_EQ(leaf.angles, glm::vec3(-30, 0, 0));
    EXPECT_EQ(leaf.alive, 0x80);
    EXPECT_EQ(leaf.light, glm::vec3(3, 4, 5));
    EXPECT_EQ(leaf.spin, glm::vec3(6, 7, 8));
    EXPECT_EQ(leaf.start, glm::vec3(9, 10, 11));
}
TEST(SnowLeafBindingTest, LegacyDelegatesWithoutUsingTheUnusedCamera)
{
    Client::World::Leaf leaf;
    const std::int32_t map = 2;
    const glm::vec3 hero{1000, 2000, 3000};
    const float camera = std::numeric_limits<float>::quiet_NaN();
    Client::Render::Models::CrtRandom random(74);
    ASSERT_TRUE(Client::World::CreateSnowLeaf(leaf, {map, hero, camera}, random));
    EXPECT_EQ(random.State(), 0x4825ce39u);
    EXPECT_EQ(leaf.position, glm::vec3(869, 1767, 3250));
    EXPECT_EQ(leaf.type, 101);
    EXPECT_EQ(leaf.scale, 10);
}
TEST(SnowLeafBindingTest, RawMap258RejectsWithoutWritesOrDraws)
{
    Client::World::Leaf leaf;
    leaf.type = -3;
    const std::int32_t map = 258;
    const glm::vec3 hero{1000, 2000, 3000};
    Client::Render::Models::CrtRandom random(74);
    EXPECT_FALSE(Client::World::CreateSnowLeaf(leaf, map, hero, random));
    EXPECT_EQ(random.State(), 74u);
    EXPECT_EQ(leaf.type, -3);
}
} // namespace
