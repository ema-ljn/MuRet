#include "Render/Models/Pose.h"

#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Skeleton.h"

#include <gtest/gtest.h>

#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cmath>
#include <numbers>
#include <vector>

// Client::Render::Models::PoseSkeleton: 0x4eb860 sobre un esqueleto armado a mano.
namespace
{
using Client::Render::Models::ActionInfo;
using Client::Render::Models::AngleMatrix;
using Client::Render::Models::AngleQuaternion;
using Client::Render::Models::AnimationState;
using Client::Render::Models::BoneKey;
using Client::Render::Models::BoneTransform;
using Client::Render::Models::ObjectTransform;
using Client::Render::Models::PoseSkeleton;
using Client::Render::Models::QuaternionMatrix;
using Client::Render::Models::Skeleton;
using Client::Render::Models::SkeletonBone;
using Client::Render::Models::VectorRotate;
using Client::Render::Models::VectorTransform;

constexpr float Tolerance = 1e-5f;
constexpr glm::vec2 NoHead{0.0f, 0.0f};

float Radians(float degrees)
{
    return degrees * std::numbers::pi_v<float> / 180.0f;
}

BoneKey Key(glm::vec3 position, glm::vec3 degrees)
{
    const glm::vec3 radians{Radians(degrees.x), Radians(degrees.y), Radians(degrees.z)};
    return BoneKey{position, radians, AngleQuaternion(radians)};
}

// Tres huesos: una raíz, su hijo y un dummy. La acción 0 tiene dos keys; la 1, tres y
// lock_positions. La raíz gira 90° en z de una key a la otra de la acción 0; el hijo está a 10 en
// x de la raíz.
Skeleton SampleSkeleton()
{
    Skeleton skeleton;
    skeleton.actions = {ActionInfo{true, 0.0f, 2, false}, ActionInfo{true, 0.0f, 3, true}};
    SkeletonBone root;
    root.actions = {
        {Key({0, 0, 0}, {0, 0, 0}), Key({10, 0, 0}, {0, 0, 90})},
        {Key({1, 2, 3}, {0, 0, 0}), Key({4, 5, 6}, {0, 0, 0}), Key({7, 8, 9}, {0, 0, 0})},
    };
    SkeletonBone child;
    child.parent = 0;
    child.actions = {
        {Key({10, 0, 0}, {0, 0, 0}), Key({10, 0, 0}, {0, 0, 0})},
        {Key({10, 0, 0}, {0, 0, 0}), Key({20, 0, 0}, {0, 0, 0}), Key({30, 0, 0}, {0, 0, 0})},
    };
    SkeletonBone dummy;
    dummy.dummy = true;
    skeleton.bones = {root, child, dummy};
    return skeleton;
}

void ExpectNear(const glm::vec3& actual, const glm::vec3& expected)
{
    EXPECT_NEAR(actual.x, expected.x, Tolerance);
    EXPECT_NEAR(actual.y, expected.y, Tolerance);
    EXPECT_NEAR(actual.z, expected.z, Tolerance);
}

std::vector<BoneTransform> Pose(const Skeleton& skeleton, const AnimationState& animation,
                                const ObjectTransform& object = {}, glm::vec2 headAngles = NoHead)
{
    std::vector<BoneTransform> bones(skeleton.bones.size(), BoneTransform(0.0f));
    EXPECT_TRUE(PoseSkeleton(skeleton, animation, object, headAngles, bones));
    return bones;
}

// El hijo cuelga del padre: su posición relativa gira con la raíz.
TEST(PoseTest, AChildFollowsItsParent)
{
    Skeleton skeleton = SampleSkeleton();
    skeleton.bones[0].actions[0][0] = Key({0, 0, 0}, {0, 0, 90});

    const std::vector<BoneTransform> bones = Pose(skeleton, AnimationState{});

    ExpectNear(bones[1][3], {0, 10, 0});
    ExpectNear(VectorTransform({1, 0, 0}, bones[1]), {0, 11, 0});
}

// La raíz gira con los ángulos del objeto, en grados. La escala y la posición del objeto no entran en
// los huesos: las pone SkinMesh. La posición de la key va en la traslación, m[3].
TEST(PoseTest, TheRootTurnsWithTheObjectButDoesNotMoveWithIt)
{
    Skeleton skeleton = SampleSkeleton();
    skeleton.bones[0].actions[0][0] = Key({5, 0, 0}, {0, 0, 0});
    const ObjectTransform object{{100, 200, 300}, {0, 0, 90}, 2.0f, 0.0f};

    const std::vector<BoneTransform> bones = Pose(skeleton, AnimationState{}, object);

    ExpectNear(bones[0][3], {0, 5, 0});
    ExpectNear(VectorRotate({1, 0, 0}, bones[0]), {0, 1, 0});
}

// Entre la key anterior y la de ahora, con la parte fraccionaria: un cuarto del camino de 0° a 90°
// es 22,5°, y la posición va lineal.
TEST(PoseTest, InterpolatesBetweenThePreviousKeyAndTheCurrent)
{
    const AnimationState animation{0, 1.25f, 0, 0};

    const std::vector<BoneTransform> bones = Pose(SampleSkeleton(), animation);

    ExpectNear(bones[0][3], {2.5f, 0, 0});
    ExpectNear(VectorRotate({1, 0, 0}, bones[0]), {std::cos(Radians(22.5f)), std::sin(Radians(22.5f)), 0});
}

// La key anterior puede ser de otra acción: la que guardó el avance al cambiar.
TEST(PoseTest, ThePreviousKeyCanBeOfAnotherAction)
{
    const AnimationState animation{0, 0.5f, 1, 2};
    Skeleton skeleton = SampleSkeleton();
    skeleton.actions[1].lockPositions = false;

    const std::vector<BoneTransform> bones = Pose(skeleton, animation);

    // De la key 2 de la acción 1 (7, 8, 9) a la key 0 de la 0 (0, 0, 0), en la mitad.
    ExpectNear(bones[0][3], {3.5f, 4.0f, 4.5f});
}

// El hueso de la cabeza resta headAngles.x a su x y headAngles.y a su z (no a la y), en grados.
TEST(PoseTest, TheHeadSubtractsXAndZ)
{
    Skeleton skeleton = SampleSkeleton();
    skeleton.headBone = 1;

    const std::vector<BoneTransform> bones = Pose(skeleton, AnimationState{}, {}, {30.0f, 40.0f});

    const BoneTransform expected = AngleMatrix({-30.0f, 0.0f, -40.0f});
    for (int column = 0; column < 3; ++column)
    {
        ExpectNear(bones[1][column], expected[column]);
    }
    // Los otros huesos no giran con la cabeza.
    ExpectNear(VectorRotate({1, 0, 0}, bones[0]), {1, 0, 0});
}

// La cabeza pasa sus grados a radianes con el float de 0x6ba140 (0x3c8efa36), un ulp más que el más
// cercano a π/180, que es el de la raíz (0x6ba360). Con 3.600° la diferencia se ve.
TEST(PoseTest, TheHeadUsesTheDegreeFloatOfTheOriginal)
{
    Skeleton skeleton = SampleSkeleton();
    skeleton.headBone = 1;
    constexpr float HeadDegreesToRadians = std::bit_cast<float>(0x3c8efa36u);

    const std::vector<BoneTransform> bones = Pose(skeleton, AnimationState{}, {}, {3600.0f, 0.0f});

    const BoneTransform expected =
        QuaternionMatrix(AngleQuaternion({0.0f - 3600.0f * HeadDegreesToRadians, 0.0f, 0.0f}));
    for (int column = 0; column < 3; ++column)
    {
        EXPECT_EQ(bones[1][column], expected[column]) << "columna " << column;
    }
}

// Con lock_positions, el hueso 0 toma x e y de la key 0 de la acción de ahora, y su z es la
// interpolada más la altura del modelo (+0x84).
TEST(PoseTest, LockPositionsTakesXAndYFromKeyZero)
{
    const AnimationState animation{1, 2.5f, 1, 1};
    ObjectTransform object;
    object.lockedRootHeight = 10.0f;

    const std::vector<BoneTransform> bones = Pose(SampleSkeleton(), animation, object);

    // x e y de (1, 2, 3); z de 6 a 9 en la mitad, más 10.
    ExpectNear(bones[0][3], {1.0f, 2.0f, 17.5f});
    // El hijo no: entre (20, 0, 0) y (30, 0, 0).
    ExpectNear(bones[1][3], bones[0][3] + glm::vec3{25.0f, 0.0f, 0.0f});
}

// Alcanza con que la tenga la acción anterior, y es el hueso 0 por su índice, no por ser raíz.
TEST(PoseTest, LockPositionsIsBoneZeroWithEitherAction)
{
    Skeleton skeleton = SampleSkeleton();
    skeleton.bones[1].parent = -1;
    const AnimationState animation{0, 1.5f, 1, 2};

    const std::vector<BoneTransform> bones = Pose(skeleton, animation);

    // x e y de la key 0 de la acción 0 (0, 0); z de 9 a 0 en la mitad.
    ExpectNear(bones[0][3], {0.0f, 0.0f, 4.5f});
    // El hueso 1, también raíz, va lineal: de (30, 0, 0) a (10, 0, 0).
    ExpectNear(bones[1][3], {20.0f, 0.0f, 0.0f});
}

// Un dummy no se posa: su matriz queda como estaba.
TEST(PoseTest, DummyBonesAreNotPosed)
{
    const Skeleton skeleton = SampleSkeleton();
    std::vector<BoneTransform> bones(skeleton.bones.size(), BoneTransform(7.0f));

    ASSERT_TRUE(PoseSkeleton(skeleton, AnimationState{0, 1.5f, 0, 0}, {}, NoHead, bones));

    EXPECT_EQ(bones[2], BoneTransform(7.0f));
    EXPECT_NE(bones[0], BoneTransform(7.0f));
}

// 0x5fc700: con las dos keys a 0,001 o menos, la pose copia la anterior y no hace el slerp.
TEST(PoseTest, AlmostEqualKeysCopyTheFirst)
{
    Skeleton skeleton = SampleSkeleton();
    skeleton.bones[0].actions[0][0].rotation = glm::quat::wxyz(1.0f, 0.0f, 0.0f, 0.0f);
    skeleton.bones[0].actions[0][1].rotation = glm::quat::wxyz(0.9995f, 0.0f, 0.0f, 0.0009f);

    const std::vector<BoneTransform> bones = Pose(skeleton, AnimationState{0, 1.5f, 0, 0});

    EXPECT_EQ(bones[0][0], QuaternionMatrix(glm::quat::wxyz(1.0f, 0.0f, 0.0f, 0.0f))[0]);
    EXPECT_EQ(bones[0][1], QuaternionMatrix(glm::quat::wxyz(1.0f, 0.0f, 0.0f, 0.0f))[1]);
}

// Una acción que el esqueleto no tiene es la 0, y una key de más, la 0. La parte fraccionaria sale
// del frame antes de cortarlo: con 5 en una acción de dos keys es 0.
TEST(PoseTest, AnActionOrAKeyOutOfRangeIsTheFirst)
{
    const std::vector<BoneTransform> outOfRange = Pose(SampleSkeleton(), AnimationState{7, 5.0f, 9, 9});
    const std::vector<BoneTransform> first = Pose(SampleSkeleton(), AnimationState{0, 0.0f, 0, 0});

    EXPECT_EQ(outOfRange, first);
}

// Con un frame que pasa de las keys, la key es la 0 pero la parte fraccionaria es la del frame: de la
// key 1 de antes a la 0, un cuarto del camino.
TEST(PoseTest, AFrameOutOfRangeKeepsItsFraction)
{
    const std::vector<BoneTransform> bones = Pose(SampleSkeleton(), AnimationState{0, 5.25f, 0, 1});

    ExpectNear(bones[0][3], {7.5f, 0, 0});
}

// Sin el tamaño del esqueleto no escribe nada.
TEST(PoseTest, TheBonesMustBeThoseOfTheSkeleton)
{
    const Skeleton skeleton = SampleSkeleton();
    std::vector<BoneTransform> bones(2, BoneTransform(7.0f));

    EXPECT_FALSE(PoseSkeleton(skeleton, AnimationState{}, {}, NoHead, bones));
    EXPECT_EQ(bones[0], BoneTransform(7.0f));
}
} // namespace
