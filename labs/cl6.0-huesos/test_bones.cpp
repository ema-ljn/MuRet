#include "Bones.h"
#include "Quaternion.h"

#include <glm/ext/matrix_float4x3.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <numbers>
#include <optional>
#include <vector>

// Labs::PoseChain, Labs::Transform y lo de alrededor (CL6.0, paso 4): una cadena de huesos y el
// skinning rígido.
namespace
{
using Labs::AngleMatrix;
using Labs::AngleQuaternion;
using Labs::Bone;
using Labs::Matrix34;
using Labs::ObjectMatrix;
using Labs::PoseChain;
using Labs::QuaternionMatrix;
using Labs::Rotate;
using Labs::Transform;
using Labs::Vec3;

constexpr float Pi = std::numbers::pi_v<float>;
constexpr float Tolerance = 1e-5f;

// El objeto quieto en el origen: sin giro, escala 1.
const Matrix34 NoObject = ObjectMatrix(Vec3{0, 0, 0}, 1, Vec3{0, 0, 0});

void ExpectNear(const Vec3& actual, const Vec3& expected, float tolerance = Tolerance)
{
    EXPECT_NEAR(actual[0], expected[0], tolerance);
    EXPECT_NEAR(actual[1], expected[1], tolerance);
    EXPECT_NEAR(actual[2], expected[2], tolerance);
}

// Un brazo de tres huesos: el hombro en el origen girado un cuarto de vuelta alrededor de z, el
// codo a 10 del hombro y la mano a 10 del codo, los dos sin giro propio.
std::vector<Bone> Arm()
{
    return {
        Bone{.parent = -1, .position = {0, 0, 0}, .rotation = AngleQuaternion(Vec3{0, 0, Pi / 2})},
        Bone{.parent = 0, .position = {10, 0, 0}, .rotation = {}},
        Bone{.parent = 1, .position = {10, 0, 0}, .rotation = {}},
    };
}

// El giro del hombro se lleva todo el brazo: lo que estaba sobre x queda sobre y.
TEST(BonesTest, AChildFollowsItsParent)
{
    const auto arm = Arm();
    const std::optional<std::vector<Matrix34>> pose = PoseChain(arm, NoObject);
    ASSERT_TRUE(pose.has_value());
    ASSERT_EQ(pose->size(), 3u);
    ExpectNear(Transform((*pose)[1], Vec3{0, 0, 0}), Vec3{0, 10, 0});
    ExpectNear(Transform((*pose)[2], Vec3{0, 0, 0}), Vec3{0, 20, 0});
    // Un vértice de la mano, guardado a 5 de su hueso: el skinning rígido lo lleva con la mano.
    ExpectNear(Transform((*pose)[2], Vec3{5, 0, 0}), Vec3{0, 25, 0});
}

// La raíz cuelga del objeto: con el objeto en (100, 0, 0), el brazo entero se corre 100 en x; con
// el objeto además girado 90° y con escala 2, el codo y la mano quedan a 20 y a 40 hacia −x.
TEST(BonesTest, TheRootHangsFromTheObject)
{
    const auto arm = Arm();
    const std::optional<std::vector<Matrix34>> moved = PoseChain(arm, ObjectMatrix(Vec3{0, 0, 0}, 1, {100, 0, 0}));
    ASSERT_TRUE(moved.has_value());
    ExpectNear(Transform((*moved)[0], Vec3{0, 0, 0}), Vec3{100, 0, 0});
    ExpectNear(Transform((*moved)[2], Vec3{0, 0, 0}), Vec3{100, 20, 0});

    // Los ángulos del objeto van en grados: 90 en z. Con el del hombro es media vuelta, y con la
    // escala el codo y la mano quedan 20 y 40 hacia −x.
    const std::optional<std::vector<Matrix34>> turned = PoseChain(arm, ObjectMatrix(Vec3{0, 0, 90}, 2, {100, 0, 0}));
    ASSERT_TRUE(turned.has_value());
    ExpectNear(Transform((*turned)[1], Vec3{0, 0, 0}), Vec3{80, 0, 0}, 1e-4f);
    ExpectNear(Transform((*turned)[2], Vec3{0, 0, 0}), Vec3{60, 0, 0}, 1e-4f);
}

// Half-Life guarda m[fila][columna] con la traslación en la columna 3; glm guarda m[columna][fila]
// y la traslación es m[3]. La misma matriz en glm::mat4x3 (4 columnas de 3) da el mismo punto.
TEST(BonesTest, RowsAgainstColumns)
{
    const auto arm = Arm();
    const Matrix34 hand = PoseChain(arm, ObjectMatrix(Vec3{10, 20, 30}, 0.9f, {13, -7, 163})).value()[2];

    glm::mat4x3 columns(1.0f);
    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 3; ++row)
        {
            columns[column][row] = hand[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)];
        }
    }
    EXPECT_EQ(columns[3].x, hand[0][3]);
    EXPECT_EQ(columns[3].y, hand[1][3]);
    EXPECT_EQ(columns[3].z, hand[2][3]);

    const Vec3 vertex{5, -2, 1};
    const glm::vec3 fromGlm = columns * glm::vec4(vertex[0], vertex[1], vertex[2], 1.0f);
    ExpectNear(Transform(hand, vertex), Vec3{fromGlm.x, fromGlm.y, fromGlm.z}, 1e-4f);
}

// Una normal es una dirección: gira con el hueso pero no se corre con él.
TEST(BonesTest, TheNormalOnlyRotates)
{
    const auto arm = Arm();
    const Matrix34 hand = PoseChain(arm, ObjectMatrix(Vec3{0, 0, 0}, 1, {100, 0, 0})).value()[2];
    ExpectNear(Rotate(hand, Vec3{1, 0, 0}), Vec3{0, 1, 0});
    ExpectNear(Transform(hand, Vec3{1, 0, 0}), Vec3{100, 21, 0});
}

// El padre se calcula antes que el hijo. Un padre que viene después, o él mismo, no se puede usar.
TEST(BonesTest, AParentAfterItsChildIsRefused)
{
    const std::vector<Bone> later{Bone{.parent = 1}, Bone{.parent = -1}};
    EXPECT_FALSE(PoseChain(later, NoObject).has_value());
    const std::vector<Bone> itself{Bone{.parent = -1}, Bone{.parent = 1}};
    EXPECT_FALSE(PoseChain(itself, NoObject).has_value());
    const std::vector<Bone> negative{Bone{.parent = -2}};
    EXPECT_FALSE(PoseChain(negative, NoObject).has_value());
    EXPECT_TRUE(PoseChain(std::vector<Bone>{}, NoObject).has_value());
}

// AngleMatrix, en grados, es el giro de AngleQuaternion con los mismos ángulos en radianes.
TEST(BonesTest, AngleMatrixIsTheQuaternionRotation)
{
    for (int i = -4; i <= 4; ++i)
    {
        for (int j = -4; j <= 4; ++j)
        {
            for (int k = -4; k <= 4; ++k)
            {
                const Vec3 degrees{45.0f * static_cast<float>(i), 45.0f * static_cast<float>(j),
                                   45.0f * static_cast<float>(k)};
                const Vec3 radians{degrees[0] * Pi / 180, degrees[1] * Pi / 180, degrees[2] * Pi / 180};
                const Matrix34 fromDegrees = AngleMatrix(degrees);
                const Matrix34 fromQuaternion = QuaternionMatrix(AngleQuaternion(radians));
                for (std::size_t row = 0; row < 3; ++row)
                {
                    for (std::size_t column = 0; column < 4; ++column)
                    {
                        EXPECT_NEAR(fromDegrees[row][column], fromQuaternion[row][column], 1e-6f);
                    }
                }
            }
        }
    }
}
} // namespace
