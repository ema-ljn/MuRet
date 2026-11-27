#include "Scenes/FaunaJointCreation.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Math/OriginalRotation.h"
#include "Scenes/BlastEffectCalls.h"
#include "Actors/CharacterStep.h"
#include "Scenes/GameDrawTail.h"
#include "Kernel/WorldState.h"
#include "World/IFishStepServices.h"
#include "World/ModelEffects.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

// El 0x5ac (FlareJoints.cpp): el alta 0x530c93..0x530dd8 y 0x53323b..0x533253 y el paso 0x5359d7..0x535b32, con el
// pedido del 0x114/0 (0x51c1b8: 0x530860(0x5ac, posición, posición, ángulos, 0, 0, escala, −1, 0, 0, −1)).
namespace
{
using Client::Crt::CrtRandom;
using Client::Scenes::JointState;

constexpr std::uint32_t Seed = 0x5acu;

struct FlareWorld
{
    Client::Core::Expected<std::optional<std::size_t>, std::string> Create(std::int32_t subtype)
    {
        glm::vec3 from = start;
        glm::vec3 to{1.0f, 2.0f, 3.0f};
        glm::vec3 angles{10.0f, 20.0f, 30.0f};
        const Client::World::FishEffectRequest request{.type = 0x5ac,
                                                       .firstPosition = from,
                                                       .secondPosition = to,
                                                       .angles = angles,
                                                       .subtype = subtype,
                                                       .ownerContext = nullptr,
                                                       .scale = 80.0f,
                                                       .key = -1,
                                                       .skill = 0,
                                                       .skillSerial = 0,
                                                       .target = -1};
        return Client::Scenes::CreateFaunaJoint(joints, request, random);
    }

    Client::Core::Expected<void, std::string> Step()
    {
        return Client::Scenes::StepJoints(joints, random, nullptr, nullptr, 0.0f, nullptr);
    }

    glm::vec3 start{1000.0f, 2000.0f, 300.0f};
    std::vector<JointState> joints = std::vector<JointState>(2);
    CrtRandom random{Seed};
};

using Client::Math::Bits;
} // namespace

// 0x530ca5..0x530d35: la /0 con vida 20, 30 puntos, el ancho, la velocidad rand() % 10 + 10, la x de los ángulos
// 30 − rand() % 40, la z rand() % 360 y la luz (0,1f; 0,5f; 1), sin la cola común; +0x24 guarda la posición. El primer
// tramo (0x530afa) va antes, con los ángulos del pedido y el ancho que tenía el lugar.
TEST(FlareJointsTest, TheFlareBornsWithThreeDraws)
{
    FlareWorld tested;
    tested.joints[0].width = 4.0f;
    CrtRandom replay{Seed};
    const auto speed = static_cast<float>(replay.Next() % 10 + 10);
    const auto tilt = static_cast<float>(30 - replay.Next() % 40);
    const auto turn = static_cast<float>(replay.Next() % 360);

    const auto created = tested.Create(0);

    ASSERT_TRUE(created.HasValue()) << created.Error();
    EXPECT_EQ(tested.random.State(), replay.State());
    const JointState& joint = tested.joints[0];
    EXPECT_TRUE(joint.alive);
    EXPECT_FALSE(joint.tailEnabled);
    EXPECT_EQ(joint.life, 20);
    EXPECT_EQ(joint.maximumCount, 30);
    EXPECT_EQ(joint.width, 80.0f);
    EXPECT_EQ(joint.speed, speed);
    EXPECT_EQ(joint.angles, glm::vec3(tilt, 0.0f, turn));
    EXPECT_EQ(joint.velocity, glm::vec3(0.0f));
    EXPECT_EQ(Bits(joint.light.x), 0x3dcccccdu);
    EXPECT_EQ(joint.light.y, 0.5f);
    EXPECT_EQ(joint.light.z, 1.0f);
    EXPECT_EQ(joint.pathIncrement, tested.start);
    EXPECT_EQ(joint.count, 0);
    const auto matrix = Client::Math::OriginalAngleMatrix({10.0f, 20.0f, 30.0f});
    const auto corner = Client::Math::OriginalVectorRotate({-2.0f, 0.0f, 0.0f}, matrix);
    EXPECT_EQ(joint.points[0][0], (glm::vec3{static_cast<float>(1000.0 + static_cast<double>(corner.x)),
                                             static_cast<float>(2000.0 + static_cast<double>(corner.y)),
                                             static_cast<float>(300.0 + static_cast<double>(corner.z))}));
}

// 0x530d43..0x530dd8: la /1 con vida 10, 20 puntos, el skill rand() % 5, la velocidad rand() % 5 + 1, los ángulos
// (30, 0, rand() % 360) y la luz (1; 0,8f; 0,6f). Otro subtipo (0x530d3d) sólo apaga la cola: sin rand(), con la vida y
// los puntos del lugar, éstos con el tope de 50 (0x534cb6).
TEST(FlareJointsTest, TheSecondFlareDrawsItsSkillAndAnotherSubtypeNothing)
{
    FlareWorld tested;
    CrtRandom replay{Seed};
    const auto skill = static_cast<std::uint8_t>(replay.Next() % 5);
    const auto speed = static_cast<float>(replay.Next() % 5 + 1);
    const auto turn = static_cast<float>(replay.Next() % 360);

    ASSERT_TRUE(tested.Create(1).HasValue());

    EXPECT_EQ(tested.random.State(), replay.State());
    const JointState& joint = tested.joints[0];
    EXPECT_FALSE(joint.tailEnabled);
    EXPECT_EQ(joint.life, 10);
    EXPECT_EQ(joint.maximumCount, 20);
    EXPECT_EQ(joint.skill, skill);
    EXPECT_EQ(joint.speed, speed);
    EXPECT_EQ(joint.angles, glm::vec3(30.0f, 0.0f, turn));
    EXPECT_EQ(Bits(joint.light.y), 0x3f4ccccdu);
    EXPECT_EQ(Bits(joint.light.z), 0x3f19999au);
    EXPECT_EQ(joint.pathIncrement, tested.start);

    FlareWorld other;
    other.joints[0].life = 77;
    other.joints[0].maximumCount = 90;
    ASSERT_TRUE(other.Create(2).HasValue());
    EXPECT_EQ(other.random.State(), CrtRandom{Seed}.State());
    EXPECT_FALSE(other.joints[0].tailEnabled);
    EXPECT_EQ(other.joints[0].life, 77);
    EXPECT_EQ(other.joints[0].maximumCount, 50);
    EXPECT_EQ(other.joints[0].angles, glm::vec3(10.0f, 20.0f, 30.0f));
}

// 0x5359e2..0x535a61: con vida de más de 16, la /0 agrega diez tramos (cada uno antes de avanzar) y avanza diez veces
// (0, −velocidad, 0) girado por sus ángulos; con 16 o menos, se queda. 0x535b0e: la luz por 0,8333333f. Sin la cola
// común: la cuenta es la de los diez tramos, y la vida baja de a una.
TEST(FlareJointsTest, TheFlareShootsTenSegmentsWhileItIsYoung)
{
    FlareWorld tested;
    ASSERT_TRUE(tested.Create(0).HasValue());
    JointState& joint = tested.joints[0];
    joint.angles = glm::vec3(0.0f);
    joint.speed = 12.0f;
    const CrtRandom before = tested.random;

    ASSERT_TRUE(tested.Step().HasValue());

    EXPECT_EQ(tested.random.State(), before.State());
    EXPECT_EQ(joint.position, (glm::vec3{1000.0f, 1880.0f, 300.0f}));
    EXPECT_EQ(joint.count, 10);
    // El último tramo, en la posición de antes del último avance.
    EXPECT_EQ(joint.points[0][0], (glm::vec3{960.0f, 1892.0f, 300.0f}));
    EXPECT_EQ(joint.points[0][3], (glm::vec3{1000.0f, 1892.0f, 340.0f}));
    EXPECT_EQ(joint.points[9][1], (glm::vec3{1040.0f, 2000.0f, 300.0f}));
    EXPECT_EQ(Bits(joint.light.y), Bits(static_cast<float>(0.5 * static_cast<double>(0.8333333f))));
    EXPECT_EQ(joint.life, 19);

    joint.life = 16;
    const glm::vec3 still = joint.position;
    ASSERT_TRUE(tested.Step().HasValue());
    EXPECT_EQ(joint.position, still);
    EXPECT_EQ(joint.count, 10);
    EXPECT_EQ(joint.life, 15);
}

// Con los ángulos del alta (la semilla da (−7, 0, 199) y velocidad 10): cada avance gira (0, −velocidad, 0) con
// 0x5fcbd0 en el mismo vector (0x535a1d..0x535a3f: la y relee la x girada y la z las dos) y lo suma en float. Los bits
// salen de una emulación aparte del original (0x5fc8f0 y 0x5fcbd0 a doble, con cada fstp en float).
TEST(FlareJointsTest, TheFlareAdvancesAlongItsAngles)
{
    FlareWorld tested;
    ASSERT_TRUE(tested.Create(0).HasValue());
    const JointState& joint = tested.joints[0];
    ASSERT_EQ(joint.angles, (glm::vec3{-7.0f, 0.0f, 199.0f}));
    ASSERT_EQ(joint.speed, 10.0f);

    ASSERT_TRUE(tested.Step().HasValue());

    EXPECT_EQ(Bits(joint.position.x), 0x4471ebeau);
    EXPECT_EQ(Bits(joint.position.y), 0x450385e2u);
    EXPECT_EQ(Bits(joint.position.z), 0x438fa3f4u);
}

// 0x535ab6..0x535ad8: la /1 gira igual en el mismo vector. La semilla da el skill 0, la velocidad 3 y los ángulos
// (30, 0, 199): cinco avances con la velocidad en 3; 3,5; 4; 4,5 y 5 (bits de la misma emulación aparte).
TEST(FlareJointsTest, TheSecondFlareAdvancesAlongItsAngles)
{
    FlareWorld tested;
    ASSERT_TRUE(tested.Create(1).HasValue());
    const JointState& joint = tested.joints[0];
    ASSERT_EQ(joint.skill, 0);
    ASSERT_EQ(joint.angles, (glm::vec3{30.0f, 0.0f, 199.0f}));
    ASSERT_EQ(joint.speed, 3.0f);

    ASSERT_TRUE(tested.Step().HasValue());

    EXPECT_EQ(Bits(joint.position.x), 0x4478971bu);
    EXPECT_EQ(Bits(joint.position.y), 0x44fc46d0u);
    EXPECT_EQ(Bits(joint.position.z), 0x439a8d9eu);
    EXPECT_EQ(joint.speed, 5.5f);
    EXPECT_EQ(joint.count, 5);
}

// 0x535a71..0x535b0c: la /1 con el skill en 0 (o la vida de más de 3) agrega cinco tramos y avanza cinco veces, la
// velocidad más 0,5 después de cada uno; con otro skill y vida de 3 o menos, se queda (y sólo se apaga).
TEST(FlareJointsTest, TheSecondFlareSpeedsUpForFiveSegments)
{
    for (const auto& [skill, life, moves] : {std::tuple{0, 2, true}, std::tuple{3, 4, true}, std::tuple{3, 3, false}})
    {
        SCOPED_TRACE(life);
        FlareWorld tested;
        ASSERT_TRUE(tested.Create(1).HasValue());
        JointState& joint = tested.joints[0];
        joint.angles = glm::vec3(0.0f);
        joint.speed = 2.0f;
        joint.skill = static_cast<std::uint8_t>(skill);
        joint.life = life;

        ASSERT_TRUE(tested.Step().HasValue());

        if (moves)
        {
            // 2 + 2,5 + 3 + 3,5 + 4 = 15.
            EXPECT_EQ(joint.position, (glm::vec3{1000.0f, 1985.0f, 300.0f}));
            EXPECT_EQ(joint.speed, 4.5f);
            EXPECT_EQ(joint.count, 5);
        }
        else
        {
            EXPECT_EQ(joint.position, tested.start);
            EXPECT_EQ(joint.speed, 2.0f);
            EXPECT_EQ(joint.count, 0);
        }
        EXPECT_EQ(joint.light.x, static_cast<float>(static_cast<double>(0.8333333f)));
        EXPECT_EQ(joint.life, life - 1);
    }
}

// 0x51bf73..0x51c00d: el combo 0x112/0 (World/BlastModelEffects, por Scenes::CreateBlastEffect) sube 50 y suelta
// sesenta joints 0x5ac/0 sin dueño desde su posición con sus ángulos (en 0), cada uno con el ancho rand() % 40 + 70
// sacado antes de su alta, que saca tres: 240 rand(). Con la tabla de joints llena 0x530860 no encuentra lugar y no
// saca ninguno: sólo los 60 de los anchos.
TEST(FlareJointsTest, TheComboReleasesSixtyFlares)
{
    for (const bool full : {false, true})
    {
        SCOPED_TRACE(full);
        auto world = std::make_unique<Client::Kernel::WorldState>();
        auto& joints = world->DrawTail().joints;
        if (full)
            for (JointState& joint : joints)
                joint.alive = true;
        CrtRandom random{Seed};
        const Client::Actors::CharacterStepContext context{.world = *world, .random = random, .joints = joints};
        const Client::World::ModelEffectArguments arguments{.type = 0x112, .position = {1000.0f, 2000.0f, 300.0f}};

        const auto made = Client::Scenes::CreateBlastEffect(arguments, nullptr, context);

        ASSERT_TRUE(made.HasValue()) << made.Error();
        ASSERT_TRUE(made.Value().has_value());
        const Client::World::ModelEffect& combo = world->Map().effects.Effects()[*made.Value()];
        EXPECT_EQ(combo.type, 0x112);
        EXPECT_EQ(combo.transform.position, (glm::vec3{1000.0f, 2000.0f, 350.0f}));
        CrtRandom replay{Seed};
        for (std::size_t ray = 0; ray < 60; ++ray)
        {
            SCOPED_TRACE(ray);
            const auto width = static_cast<float>(replay.Next() % 40 + 70);
            if (full)
                continue;
            const JointState& flare = joints[ray];
            ASSERT_TRUE(flare.alive);
            EXPECT_EQ(flare.type, 0x5ac);
            EXPECT_EQ(flare.subtype, 0);
            EXPECT_EQ(flare.ownerContext, nullptr);
            EXPECT_EQ(flare.width, width);
            EXPECT_EQ(flare.position, combo.transform.position);
            EXPECT_EQ(flare.life, 20);
            replay.Discard(3);
        }
        if (!full)
        {
            EXPECT_FALSE(joints[60].alive);
        }
        EXPECT_EQ(random.State(), replay.State());
        CrtRandom counted{Seed};
        counted.Discard(full ? 60U : 240U);
        EXPECT_EQ(random.State(), counted.State());
    }
}
