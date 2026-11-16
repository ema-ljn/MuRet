#include "Render/Models/Cloth.h"

#include "Data/Models/Model.h"
#include "Render/Models/HalfLifeMath.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <vector>

// La tela sobre la malla (la vtable 0x6b8d04: 0x455ea0, 0x4564c0, 0x4565f0 y 0x4568f0), con vectores literales del
// ASM de c32bbd5f, sin ejecutar el original (08 P-C330).
namespace
{
using Client::Render::Models::BoneTransform;
using Client::Render::Models::Cloth;
using Client::Render::Models::ClothBody;
using Client::Render::Models::ClothSpring;
using Client::Render::Models::ClothWorld;
using Client::Render::Models::CrtRandom;

constexpr std::int32_t Bone = 0x12;
constexpr std::uint32_t StrongGravity = 0x400;

// Un cuadrado de dos triángulos: los vértices 1 y 2 del hueso de la tela, el 0 y el 3 de otro.
Client::Data::Models::Mesh Square()
{
    Client::Data::Models::Mesh mesh;
    mesh.vertices = {{1, {}}, {Bone, {}}, {Bone, {}}, {1, {}}};
    mesh.triangles = {{3, {1, 2, 3, 0}, {}, {}}, {3, {1, 3, 0, 0}, {}, {}}};
    return mesh;
}

const std::vector<glm::vec3> SquareVertices{
    {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 10.0f}, {12.0f, 0.0f, 10.0f}, {12.0f, 0.0f, 0.0f}};

void ExpectSpring(const ClothSpring& spring, std::int16_t a, std::int16_t b, float rest, std::uint8_t flags)
{
    EXPECT_EQ(spring.a, a);
    EXPECT_EQ(spring.b, b);
    EXPECT_FLOAT_EQ(spring.rest, rest);
    EXPECT_FLOAT_EQ(spring.shortest, static_cast<float>(static_cast<double>(rest) * 0.5));
    EXPECT_EQ(spring.flags, flags);
}

// 0x455ea0: una partícula por vértice, anclada la del hueso 0x12, y los resortes en el orden de los lados.
TEST(MeshClothTest, TheBuildTakesOneParticlePerVertexAndTheSidesInOrder)
{
    Cloth cloth;
    cloth.BuildOnTheMesh(Square(), 2, Bone, StrongGravity, SquareVertices);

    EXPECT_EQ(cloth.Mesh(), 2);
    EXPECT_EQ(cloth.Flags(), StrongGravity);
    EXPECT_EQ(cloth.Columns(), 0);
    const auto particles = cloth.Particles();
    ASSERT_EQ(particles.size(), 4u);
    EXPECT_FALSE(particles[0].anchored);
    EXPECT_TRUE(particles[1].anchored);
    EXPECT_TRUE(particles[2].anchored);
    EXPECT_FALSE(particles[3].anchored);
    EXPECT_EQ(particles[2].position, glm::vec3(12.0f, 0.0f, 10.0f));
    // El primer triángulo: (1, 2) con |Δx| = 12 > 10 (3), (2, 3) con 0 (6) y (3, 1); su vecino por (1, 3) es el
    // vértice 0, que 0x456101 (JLE) no liga (Q-C861). El segundo: (1, 3) y su vecino por (3, 1) en el primero, el 2,
    // que da (0, 2) con las banderas del lado; (3, 0) y (0, 1).
    const float diagonal = std::sqrt(244.0f);
    const auto springs = cloth.Springs();
    ASSERT_EQ(springs.size(), 7u);
    ExpectSpring(springs[0], 1, 2, 12.0f, 3);
    ExpectSpring(springs[1], 2, 3, 10.0f, 6);
    ExpectSpring(springs[2], 3, 1, diagonal, 3);
    ExpectSpring(springs[3], 1, 3, diagonal, 3);
    ExpectSpring(springs[4], 0, 2, diagonal, 3);
    ExpectSpring(springs[5], 3, 0, 12.0f, 3);
    ExpectSpring(springs[6], 0, 1, 10.0f, 6);
}

// 0x456470 mira solo (c, d): un vecino que ya tiene su resorte en ese sentido no se repite; y uno que pasa de
// L · 1,2f (0x6b8bb0) no se liga.
TEST(MeshClothTest, ANeighbourIsLinkedOnceAndOnlyWhenClose)
{
    Client::Data::Models::Mesh mesh = Square();
    mesh.triangles.push_back(mesh.triangles[1]);
    Cloth repeated;
    repeated.BuildOnTheMesh(mesh, 2, Bone, StrongGravity, SquareVertices);
    // El tercer triángulo repite los lados del segundo, pero (0, 2) ya está.
    EXPECT_EQ(repeated.Springs().size(), 10u);

    std::vector<glm::vec3> stretched = SquareVertices;
    stretched[0] = {-30.0f, 0.0f, 0.0f};
    Cloth far;
    far.BuildOnTheMesh(Square(), 2, Bone, StrongGravity, stretched);
    EXPECT_EQ(far.Springs().size(), 6u);
}

// Q-C861: sin el vértice 0, los dos triángulos se ligan los vecinos, y 0x456470 no ve el (2, 4) al buscar el (4, 2).
TEST(MeshClothTest, TheReverseNeighbourIsLinkedAgain)
{
    Client::Data::Models::Mesh mesh;
    mesh.vertices = {{1, {}}, {Bone, {}}, {Bone, {}}, {1, {}}, {1, {}}};
    mesh.triangles = {{3, {1, 2, 3, 0}, {}, {}}, {3, {1, 3, 4, 0}, {}, {}}};
    std::vector<glm::vec3> vertices{{50.0f, 50.0f, 50.0f}};
    vertices.insert(vertices.end(), SquareVertices.begin(), SquareVertices.end());
    Cloth cloth;
    cloth.BuildOnTheMesh(mesh, 2, Bone, StrongGravity, vertices);

    const float diagonal = std::sqrt(244.0f);
    const auto springs = cloth.Springs();
    ASSERT_EQ(springs.size(), 8u);
    ExpectSpring(springs[2], 3, 1, diagonal, 3);
    ExpectSpring(springs[3], 2, 4, diagonal, 3);
    ExpectSpring(springs[4], 1, 3, diagonal, 3);
    ExpectSpring(springs[5], 4, 2, diagonal, 3);
}

// Un paso: las 4 de la protección del contador (0x4565f0), la misma fuerza para todas las partículas sueltas con
// |c % 10| (V = 30, N = 4: c = 303 % 4 = 3, el peso 4), la gravedad fuerte (· 180) y el anclaje a 0x5786f9c.
TEST(MeshClothTest, AStepPushesEveryParticleAlikeAndAnchorsToTheMesh)
{
    Cloth cloth;
    cloth.BuildOnTheMesh(Square(), 2, Bone, StrongGravity, SquareVertices);
    ClothWorld world;
    world.windBase = 0.5f;
    world.logicSteps = 30;
    std::vector<glm::vec3> moved = SquareVertices;
    moved[1] += glm::vec3(1.0f, 2.0f, 3.0f);
    moved[2] += glm::vec3(1.0f, 2.0f, 3.0f);
    const std::vector<BoneTransform> bones(Bone + 1, BoneTransform(1.0f));
    const ClothBody body{.bones = bones, .angles = {0.0f, 0.0f, 90.0f}, .meshVertices = moved};

    ASSERT_TRUE(cloth.Step(0.005f, body, world, 0));

    CrtRandom expected(1);
    for (int i = 0; i < 4; ++i)
        static_cast<void>(expected.Next());
    EXPECT_EQ(world.random.State(), expected.State());
    const auto particles = cloth.Particles();
    EXPECT_EQ(particles[1].position, moved[1]);
    EXPECT_EQ(particles[2].position, moved[2]);
    // Con el ángulo 90, el viento va todo en x (−0,5). 400 · (4 · viento) · 0,005 = 8 · viento, en las dos sueltas
    // por igual.
    EXPECT_FLOAT_EQ(world.wind.x, -0.5f);
    EXPECT_NEAR(particles[0].velocity.x, 8.0f * world.wind.x, 1e-5f);
    EXPECT_FLOAT_EQ(particles[0].velocity.x, particles[3].velocity.x);
    EXPECT_FLOAT_EQ(particles[0].velocity.y, particles[3].velocity.y);
    const double gravity = static_cast<double>(0.0025f) * static_cast<double>(9.8f) * 180.0;
    EXPECT_FLOAT_EQ(particles[0].velocity.z, static_cast<float>(400.0 * -gravity * 0.005));
}

// 0x4568f0: cada partícula a su vértice; con menos vértices, nada.
TEST(MeshClothTest, TheDrawWritesTheParticlesOverTheMesh)
{
    Cloth cloth;
    cloth.BuildOnTheMesh(Square(), 2, Bone, StrongGravity, SquareVertices);
    std::vector<glm::vec3> vertices(4, glm::vec3(-1.0f));
    cloth.WriteTheMesh(vertices);
    EXPECT_EQ(vertices, SquareVertices);
    std::vector<glm::vec3> shorter(3, glm::vec3(-1.0f));
    cloth.WriteTheMesh(shorter);
    EXPECT_EQ(shorter[0], glm::vec3(-1.0f));
}

// La de la grilla no cambia con la de la malla: Mesh() es −1.
TEST(MeshClothTest, TheGridClothIsNotOnAMesh)
{
    const Cloth cloth;
    EXPECT_EQ(cloth.Mesh(), -1);
}
} // namespace
