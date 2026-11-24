#include "Scenes/FaunaJointCreation.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Render/Models/OriginalRotation.h"
#include "Scenes/GameDrawTail.h"
#include "World/IFishStepServices.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Los remolinos 0x6e8/0 y /1 que el 0x114/1 suelta de a dos (0x51c25f, sin dueño): el alta de 0x5345bf..0x5346d3 y el
// paso de 0x53b7f1..0x53b93a, sin el movimiento común ni la cola común.
namespace
{
using Client::Crt::CrtRandom;
using Client::Scenes::JointState;

constexpr std::uint32_t Seed = 0x6e8u;

double D(float value)
{
    return static_cast<double>(value);
}

float F32(double value)
{
    return static_cast<float>(value);
}

struct SwirlWorld
{
    Client::Core::Expected<std::optional<std::size_t>, std::string> Create(std::int32_t subtype)
    {
        glm::vec3 from = start;
        glm::vec3 angles{90.0f, 0.0f, 30.0f};
        const Client::World::FishEffectRequest request{.type = 0x6e8,
                                                       .firstPosition = from,
                                                       .secondPosition = from,
                                                       .angles = angles,
                                                       .subtype = subtype,
                                                       .ownerContext = nullptr,
                                                       .scale = 85.0f,
                                                       .key = -1,
                                                       .skill = 0,
                                                       .skillSerial = 0,
                                                       .target = -1};
        return Client::Scenes::CreateFaunaJoint(joints, request, random);
    }

    Client::Core::Expected<void, std::string> Step()
    {
        return Client::Scenes::StepJoints(joints, random);
    }

    glm::vec3 start{1000.0f, 2000.0f, 300.0f};
    std::vector<JointState> joints = std::vector<JointState>(2);
    CrtRandom random{Seed};
};
} // namespace

// 0x5345bf..0x5346d3: sin la cola común, el ancho, velocidad 20, los ángulos (−90, 0, z), vida 10 y 30 puntos; la /1
// con la textura repetida (+0x9f0). La luz en 1 con rand() impar y (1; 0,8f; 0,6f) con par; +0x24 en la posición. Con
// la tabla llena, nada ni rand().
TEST(SwirlJointsTest, EachSwirlIsBornUprightWithItsLight)
{
    for (const std::int32_t subtype : {0, 1})
    {
        SCOPED_TRACE(subtype);
        SwirlWorld tested;
        const auto created = tested.Create(subtype);
        ASSERT_TRUE(created.HasValue()) << created.Error();
        CrtRandom expected(Seed);
        const bool white = (expected.Next() & 1) != 0;
        EXPECT_EQ(tested.random.State(), expected.State());
        const JointState& joint = tested.joints[0];
        EXPECT_FALSE(joint.tailEnabled);
        EXPECT_EQ(joint.repeatedTexture, subtype == 1);
        EXPECT_EQ(joint.width, 85.0f);
        EXPECT_EQ(joint.speed, 20.0f);
        EXPECT_EQ(joint.angles, glm::vec3(-90.0f, 0.0f, 30.0f));
        EXPECT_EQ(joint.life, 10);
        EXPECT_EQ(joint.maximumCount, 30);
        EXPECT_EQ(joint.velocity, glm::vec3(0.0f));
        EXPECT_EQ(joint.light,
                  white ? glm::vec3(1.0f)
                        : glm::vec3(1.0f, std::bit_cast<float>(0x3f4ccccdu), std::bit_cast<float>(0x3f19999au)));
        EXPECT_EQ(joint.pathIncrement, tested.start);
    }

    SwirlWorld full;
    for (JointState& joint : full.joints)
        joint.alive = true;
    const auto none = full.Create(0);
    ASSERT_TRUE(none.HasValue()) << none.Error();
    EXPECT_FALSE(none.Value().has_value());
    EXPECT_EQ(full.random.State(), CrtRandom(Seed).State());
}

// 0x53b7f1..0x53b8af: con más de 9 de vida, treinta veces la cola con la matriz de los ángulos y la posición más
// (0, 0, −velocidad) girado; siempre la velocidad 2 menos y la luz por 0,7142857f. Sin el movimiento común ni rand().
TEST(SwirlJointsTest, TheSwirlUnwindsOnlyOnItsFirstStep)
{
    SwirlWorld tested;
    ASSERT_TRUE(tested.Create(0).HasValue());
    JointState& joint = tested.joints[0];
    JointState expected = joint;
    const auto state = tested.random.State();
    const double fade = D(std::bit_cast<float>(0x3f36db6eu));

    ASSERT_TRUE(tested.Step().HasValue());

    const auto matrix = Client::Render::Models::OriginalAngleMatrix(expected.angles);
    for (int turn = 0; turn < 30; ++turn)
    {
        const glm::vec3 moved = Client::Render::Models::OriginalVectorRotate({0.0f, 0.0f, -20.0f}, matrix);
        for (int axis = 0; axis < 3; ++axis)
            expected.position[axis] = F32(D(moved[axis]) + D(expected.position[axis]));
    }
    EXPECT_EQ(joint.position, expected.position);
    // 0x535520 deja la cuenta en 30 − 1 con la cola llena.
    EXPECT_EQ(joint.count, 29);
    EXPECT_EQ(joint.speed, 18.0f);
    for (int channel = 0; channel < 3; ++channel)
        expected.light[channel] = F32(D(expected.light[channel]) * fade);
    EXPECT_EQ(joint.light, expected.light);
    EXPECT_EQ(joint.life, 9);
    EXPECT_EQ(tested.random.State(), state);

    ASSERT_TRUE(tested.Step().HasValue());

    EXPECT_EQ(joint.position, expected.position);
    EXPECT_EQ(joint.count, 29);
    EXPECT_EQ(joint.speed, 16.0f);
    for (int channel = 0; channel < 3; ++channel)
        expected.light[channel] = F32(D(expected.light[channel]) * fade);
    EXPECT_EQ(joint.light, expected.light);
    EXPECT_EQ(joint.life, 8);
}
