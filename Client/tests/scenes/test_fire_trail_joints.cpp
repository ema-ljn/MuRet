#include "Joints/FaunaJointCreation.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Scenes/GameDrawTail.h"
#include "Kernel/JointRequest.h"
#include "Effects/ModelEffects.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// El 0x5f2 (FireTrailJoints.cpp): el alta 0x531229..0x53128f y 0x532073 y el paso 0x5366bd..0x5366ee, con el pedido
// de los meteoros 0xc2/6 de los magos (0x51b098: 0x530860(0x5f2, posición, posición, +0x28, 0, efecto, 100, −1, 0, 0,
// −1)).
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::JointOwnerKind;
using Client::Scenes::JointState;

constexpr std::uint32_t Seed = 0x5f2u;

struct TrailWorld
{
    TrailWorld()
    {
        meteor.alive = true;
        meteor.type = 0xc2;
        meteor.transform.position = start;
    }

    Client::Core::Expected<std::optional<std::size_t>, std::string> Create(std::int32_t subtype, void* owner,
                                                                           JointOwnerKind kind = JointOwnerKind::ByType)
    {
        glm::vec3 from = start;
        glm::vec3 angles{0.0f, 20.0f, 0.0f};
        const Client::Kernel::JointRequest request{.type = 0x5f2,
                                                   .firstPosition = from,
                                                   .secondPosition = from,
                                                   .angles = angles,
                                                   .subtype = subtype,
                                                   .ownerContext = owner,
                                                   .scale = 100.0f,
                                                   .key = -1,
                                                   .skill = 0,
                                                   .skillSerial = 0,
                                                   .target = -1};
        return Client::Joints::CreateFaunaJoint(joints, request, random, kind);
    }

    Client::Core::Expected<void, std::string> Step()
    {
        return Client::Joints::StepJoints(joints, random, nullptr, nullptr, 0.0f, nullptr);
    }

    glm::vec3 start{1000.0f, 2000.0f, 700.0f};
    Client::Effects::ModelEffect meteor;
    std::vector<JointState> joints = std::vector<JointState>(4);
    CrtRandom random{Seed};
};
} // namespace

// 0x531229..0x53128f y 0x532073: el ancho, la luz en 1; la /0 con velocidad 0, 20 puntos, vida 20 y la textura 0x60f,
// la /1 con 15 y 15, y las demás con los puntos y la vida del lugar, los puntos con el tope de 50 (0x534cb6). Los
// únicos que piden un 0x5f2 (0x51b098 y 0x51b268) lo piden con el subtipo 0: la 0x24 de acá no tiene llamador. Pasa
// por el primer tramo (0x530afa), sin rand().
TEST(FireTrailJointsTest, EachSubtypeBornsWithItsPointsAndLife)
{
    struct Probe
    {
        std::int32_t subtype;
        std::uint16_t texture;
        std::int32_t points;
        std::int32_t life;
    };
    for (const Probe& probe : {Probe{0, 0x60f, 20, 20}, Probe{1, 0x5f2, 15, 15}, Probe{0x24, 0x5f2, 50, 77}})
    {
        SCOPED_TRACE(probe.subtype);
        TrailWorld tested;
        JointState& slot = tested.joints[0];
        slot.life = 77;
        slot.maximumCount = 60;
        slot.light = {0.5f, 0.5f, 0.5f};
        slot.points[0].fill(glm::vec3(7.0f, 8.0f, 9.0f));

        const auto created = tested.Create(probe.subtype, &tested.meteor);

        ASSERT_TRUE(created.HasValue()) << created.Error();
        EXPECT_EQ(tested.random.State(), CrtRandom{Seed}.State());
        const JointState& joint = tested.joints[0];
        EXPECT_TRUE(joint.alive);
        EXPECT_EQ(joint.texture, probe.texture);
        EXPECT_EQ(joint.width, 100.0f);
        EXPECT_EQ(joint.speed, 0.0f);
        EXPECT_EQ(joint.maximumCount, probe.points);
        EXPECT_EQ(joint.life, probe.life);
        EXPECT_EQ(joint.light, glm::vec3(1.0f));
        EXPECT_EQ(joint.count, 0);
        EXPECT_EQ(joint.position, tested.start);
        EXPECT_EQ(joint.angles, glm::vec3(0.0f, 20.0f, 0.0f));
        EXPECT_EQ(joint.ownerContext, &tested.meteor);
        // El primer tramo con el ancho que tenía el lugar (0): las cuatro esquinas en la posición.
        for (const glm::vec3& corner : joint.points[0])
            EXPECT_EQ(corner, tested.start);
    }
}

// 0x5366bd..0x5366ee: con el meteoro vivo y de tipo 0xc2, la estela toma su posición; muerto o de otro tipo, se queda
// (pasa por el movimiento común de 0x53590e, pero con la velocidad en 0). Cada paso suma un tramo y resta una vida;
// muere en el paso 21. Sin rand().
TEST(FireTrailJointsTest, TheTrailFollowsItsMeteorWhileItLives)
{
    for (const JointOwnerKind kind : {JointOwnerKind::ByType, JointOwnerKind::ModelEffect})
    {
        SCOPED_TRACE(static_cast<int>(kind));
        TrailWorld tested;
        ASSERT_TRUE(tested.Create(0, &tested.meteor, kind).HasValue());
        const JointState& joint = tested.joints[0];

        tested.meteor.transform.position = {1010.0f, 2000.0f, 650.0f};
        ASSERT_TRUE(tested.Step().HasValue());
        EXPECT_EQ(joint.position, glm::vec3(1010.0f, 2000.0f, 650.0f));
        EXPECT_EQ(joint.count, 1);
        EXPECT_EQ(joint.life, 19);

        tested.meteor.type = 0xc3;
        tested.meteor.transform.position = {1020.0f, 2000.0f, 600.0f};
        ASSERT_TRUE(tested.Step().HasValue());
        EXPECT_EQ(joint.position, glm::vec3(1010.0f, 2000.0f, 650.0f));
        tested.meteor.type = 0xc2;
        tested.meteor.alive = false;
        ASSERT_TRUE(tested.Step().HasValue());
        EXPECT_EQ(joint.position, glm::vec3(1010.0f, 2000.0f, 650.0f));
        EXPECT_EQ(joint.count, 3);
        EXPECT_EQ(joint.life, 17);

        for (int step = 4; step <= 21; ++step)
        {
            SCOPED_TRACE(step);
            ASSERT_TRUE(tested.Step().HasValue());
            EXPECT_EQ(joint.alive, step < 21);
        }
        EXPECT_EQ(joint.life, -1);
        EXPECT_EQ(tested.random.State(), CrtRandom{Seed}.State());
    }
}

// 0x5366c0: sin dueño, la estela no lee nada y sigue con la cola común.
TEST(FireTrailJointsTest, AnOwnerlessTrailStaysInPlace)
{
    TrailWorld tested;
    ASSERT_TRUE(tested.Create(0, nullptr).HasValue());

    ASSERT_TRUE(tested.Step().HasValue());

    const JointState& joint = tested.joints[0];
    EXPECT_EQ(joint.position, tested.start);
    EXPECT_EQ(joint.count, 1);
    EXPECT_EQ(joint.life, 19);
}
