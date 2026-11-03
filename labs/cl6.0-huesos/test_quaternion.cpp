#include "Quaternion.h"

#include <glm/ext/matrix_float3x3.hpp>
#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/gtc/quaternion.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

// Labs::AngleQuaternion y Labs::QuaternionMatrix (CL6.0, paso 2), contra glm.
namespace
{
using Labs::AngleQuaternion;
using Labs::Matrix34;
using Labs::Quat;
using Labs::QuaternionMatrix;
using Labs::Vec3;

constexpr float Pi = std::numbers::pi_v<float>;

// Lo que separa dos cuentas de float con productos y senos: unos pocos ulp de 1. No sirve
// EXPECT_FLOAT_EQ, que pide 4 ulp del valor: cerca de 0 un ulp es mucho más chico que el error.
constexpr float Tolerance = 1e-6f;

// Los ángulos de −π a π cada π/8, en los tres ejes: 17³ = 4.913 giros.
std::vector<Vec3> AngleGrid()
{
    std::vector<Vec3> grid;
    for (int i = -8; i <= 8; ++i)
    {
        for (int j = -8; j <= 8; ++j)
        {
            for (int k = -8; k <= 8; ++k)
            {
                grid.push_back(Vec3{static_cast<float>(i) * Pi / 8, static_cast<float>(j) * Pi / 8,
                                    static_cast<float>(k) * Pi / 8});
            }
        }
    }
    return grid;
}

// Un cuarto de vuelta alrededor de z: el eje (0, 0, 1) por sin(π/4) y w = cos(π/4).
TEST(QuaternionTest, AQuarterTurnAroundZ)
{
    const Quat q = AngleQuaternion(Vec3{0, 0, Pi / 2});
    EXPECT_NEAR(q.x, 0, Tolerance);
    EXPECT_NEAR(q.y, 0, Tolerance);
    EXPECT_NEAR(q.z, std::sqrt(0.5f), Tolerance);
    EXPECT_NEAR(q.w, std::sqrt(0.5f), Tolerance);
}

// glm::quat(glm::vec3) es el mismo AngleQuaternion: los mismos medios ángulos y los mismos cuatro
// productos (en glm 1.0.3, detail/type_quat.inl). glm los hace en float y el original en la x87.
TEST(QuaternionTest, MatchesGlmForAnyAngles)
{
    for (const Vec3& angles : AngleGrid())
    {
        SCOPED_TRACE(testing::Message() << angles[0] << " " << angles[1] << " " << angles[2]);
        const Quat byHand = AngleQuaternion(angles);
        const glm::quat fromGlm(glm::vec3(angles[0], angles[1], angles[2]));
        EXPECT_NEAR(byHand.x, fromGlm.x, Tolerance);
        EXPECT_NEAR(byHand.y, fromGlm.y, Tolerance);
        EXPECT_NEAR(byHand.z, fromGlm.z, Tolerance);
        EXPECT_NEAR(byHand.w, fromGlm.w, Tolerance);
    }
}

// glm::mat3_cast da la misma matriz guardada por columnas: el m[fila][columna] de Half-Life es el
// [columna][fila] de glm. La traslación de la 3 × 4 queda en 0.
TEST(QuaternionTest, TheMatrixIsTheSameRotation)
{
    for (const Vec3& angles : AngleGrid())
    {
        SCOPED_TRACE(testing::Message() << angles[0] << " " << angles[1] << " " << angles[2]);
        const Matrix34 byHand = QuaternionMatrix(AngleQuaternion(angles));
        const glm::mat3 fromGlm = glm::mat3_cast(glm::quat(glm::vec3(angles[0], angles[1], angles[2])));
        for (int row = 0; row < 3; ++row)
        {
            const auto& byHandRow = byHand[static_cast<std::size_t>(row)];
            for (int column = 0; column < 3; ++column)
            {
                EXPECT_NEAR(byHandRow[static_cast<std::size_t>(column)], fromGlm[column][row], Tolerance);
            }
            EXPECT_EQ(byHandRow[3], 0.0f);
        }
    }
}

// La matriz tiene solo productos de dos componentes: q y −q dan la misma, bit a bit. Son el mismo
// giro (el ángulo θ y θ + 2π alrededor del mismo eje).
TEST(QuaternionTest, QAndMinusQAreTheSameMatrix)
{
    for (const Vec3& angles : AngleGrid())
    {
        const Quat q = AngleQuaternion(angles);
        const Quat minusQ{.x = -q.x, .y = -q.y, .z = -q.z, .w = -q.w};
        EXPECT_EQ(QuaternionMatrix(q), QuaternionMatrix(minusQ));
    }
}

// El orden de las componentes. glm::quat guarda x, y, z y w como el original, pero su constructor
// de cuatro recibe la w primero: glm::quat(x, y, z, w) es otro giro. glm::quat::wxyz lo dice en
// el nombre.
TEST(QuaternionTest, GlmTakesTheWFirst)
{
    const Quat q = AngleQuaternion(Vec3{0, 0, Pi / 2});
    const glm::quat good = glm::quat::wxyz(q.w, q.x, q.y, q.z);
    EXPECT_FLOAT_EQ(good.z, q.z);
    EXPECT_FLOAT_EQ(good.w, q.w);

    // Con el orden del original en el constructor, la w de glm es la x: (0, 0, √½, √½) pasa a ser
    // w = 0, x = 0, y = √½, z = √½, media vuelta alrededor de (0, 1, 1)/√2.
    const glm::quat wrong(q.x, q.y, q.z, q.w);
    EXPECT_FLOAT_EQ(wrong.w, q.x);
    EXPECT_FLOAT_EQ(wrong.y, q.z);
    const glm::mat3 turned = glm::mat3_cast(wrong);
    EXPECT_NEAR(turned[0][0], -1, Tolerance);
}
} // namespace
