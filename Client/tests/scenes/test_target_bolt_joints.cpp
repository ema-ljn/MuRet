#include "Scenes/FaunaJointCreation.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalAngles.h"
#include "Render/Models/OriginalRotation.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameDrawTail.h"
#include "World/IFishStepServices.h"
#include "World/TerrainLighting.h"
#include "World/WorldObjectStepServices.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// El 0x610/6 que el 0x95 de Kalima tira con la 0x11 hacia su blanco (0x43b0ed, 0x43b11c y 0x43b14e): el alta de
// 0x531c43..0x531cc7 y 0x531e04..0x531e27 y el paso de 0x537b36..0x537c7d, con el blanco de dueño.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorState;
using Client::Scenes::JointOwnerKind;
using Client::Scenes::JointState;

constexpr std::uint32_t Seed = 0x610u;

double D(float value)
{
    return static_cast<double>(value);
}

float F32(double value)
{
    return static_cast<float>(value);
}

struct BoltWorld
{
    BoltWorld()
    {
        target.alive = true;
        target.position = {1600.0f, 1000.0f, 150.0f};
    }

    Client::Core::Expected<std::optional<std::size_t>, std::string> Create(std::uint8_t skill = 0)
    {
        glm::vec3 from = start;
        glm::vec3 to = target.position;
        glm::vec3 angles{0.0f, 0.0f, 40.0f};
        const Client::World::FishEffectRequest request{.type = 0x610,
                                                       .firstPosition = from,
                                                       .secondPosition = to,
                                                       .angles = angles,
                                                       .subtype = 6,
                                                       .ownerContext = owned ? &target : nullptr,
                                                       .scale = 30.0f,
                                                       .key = 0x32,
                                                       .skill = skill,
                                                       .skillSerial = 0,
                                                       .target = -1};
        return Client::Scenes::CreateFaunaJoint(joints, request, random,
                                                owned ? JointOwnerKind::Actor : JointOwnerKind::ByType);
    }

    Client::Core::Expected<void, std::string> Step()
    {
        return Client::Scenes::StepJoints(joints, random, nullptr, nullptr, 0.0f, nullptr, lighting.get());
    }

    ActorState target;
    bool owned = true;
    glm::vec3 start{1000.0f, 1000.0f, 300.0f};
    std::vector<JointState> joints = std::vector<JointState>(2);
    CrtRandom random{Seed};
    std::unique_ptr<Client::World::TerrainLighting> lighting = std::make_unique<Client::World::TerrainLighting>();
};

// 0x4e9810 con el destino: el yaw y el pitch giran de a `turn`; devuelve la distancia sin redondear.
double Steer(JointState& expected, const glm::vec3& target, float turn)
{
    using Client::Render::Models::CreateAngle;
    using Client::Render::Models::TurnAngle;
    const auto yaw = F32(CreateAngle(expected.position.x, expected.position.y, target.x, target.y));
    expected.angles.z = F32(TurnAngle(expected.angles.z, yaw, turn));
    const double x = D(expected.position.x) - target.x;
    const double y = D(expected.position.y) - target.y;
    const auto z = F32(D(expected.position.z) - target.z);
    const auto horizontal = F32(std::sqrt(D(F32(x * x)) + D(F32(y * y))));
    const auto pitch = F32(360.0 - CreateAngle(expected.position.z, horizontal, target.z, 0));
    expected.angles.x = F32(TurnAngle(expected.angles.x, pitch, turn));
    return std::sqrt((D(z) * z + D(F32(x * x))) + D(F32(y * y)));
}

// El avance común (0x53590e) con la velocidad de antes y los ángulos de antes.
void MoveAhead(JointState& expected)
{
    const glm::vec3 moved = Client::Render::Models::OriginalVectorRotate(
        {0.0f, -expected.speed, 0.0f}, Client::Render::Models::OriginalAngleMatrix(expected.angles));
    for (int axis = 0; axis < 3; ++axis)
        expected.position[axis] = F32(D(expected.position[axis]) + D(moved[axis]));
}
} // namespace

// 0x531c43..0x531cc7, 0x531e04..0x531e27 y 0x532003: la vida en la key (50) y la key en 0; la luz en 1, la mezcla 4, el
// ancho, 16 puntos, velocidad 5 y el destino en el segundo punto. Sin rand(). Con el color 3 sin dueño, el original
// desreferencia el puntero nulo (+0xf4, 0x531c7b) y revienta: MuRet corta.
TEST(TargetBoltJointsTest, TheBoltIsBornWithTheKeyAsItsLife)
{
    BoltWorld tested;
    const auto created = tested.Create();
    ASSERT_TRUE(created.HasValue()) << created.Error();
    EXPECT_EQ(tested.random.State(), CrtRandom(Seed).State());
    const JointState& joint = tested.joints[0];
    EXPECT_EQ(joint.life, 50);
    EXPECT_EQ(joint.key, 0);
    EXPECT_EQ(joint.maximumCount, 16);
    EXPECT_EQ(joint.light, glm::vec3(1.0f));
    EXPECT_EQ(joint.blend, 4);
    EXPECT_EQ(joint.width, 30.0f);
    EXPECT_EQ(joint.speed, 5.0f);
    EXPECT_EQ(joint.position, tested.start);
    EXPECT_EQ(joint.targetPosition, tested.target.position);
    EXPECT_EQ(joint.ownerContext, &tested.target);
    EXPECT_EQ(joint.texture, 0x610);

    BoltWorld lonely;
    lonely.owned = false;
    const auto cut = lonely.Create(3);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x531c7b"), std::string::npos) << cut.Error();
}

// 0x537b4a..0x537c7d: con 30 de vida o más, la z de los ángulos baja 10 y la posición sube 2,5 después del avance
// común; la velocidad sube 5 y gira con ella hacia el blanco 150 más arriba; lejos (más de 70) no frena. Con 15 de
// vida o más, la luz no cambia. Un rand() para la luz dinámica.
TEST(TargetBoltJointsTest, TheBoltRisesAndTurnsTowardsItsTarget)
{
    BoltWorld tested;
    ASSERT_TRUE(tested.Create().HasValue());
    JointState expected = tested.joints[0];

    ASSERT_TRUE(tested.Step().HasValue());

    MoveAhead(expected);
    expected.angles.z = F32(D(expected.angles.z) - 10.0);
    expected.position.z = F32(D(expected.position.z) + 2.5);
    expected.speed = 10.0f;
    const glm::vec3 aim{tested.target.position.x, tested.target.position.y, F32(D(tested.target.position.z) + 150.0)};
    EXPECT_GT(Steer(expected, aim, 10.0f), 70.0);
    const JointState& joint = tested.joints[0];
    EXPECT_EQ(joint.position, expected.position);
    EXPECT_EQ(joint.angles, expected.angles);
    EXPECT_EQ(joint.speed, 10.0f);
    EXPECT_EQ(joint.light, glm::vec3(1.0f));
    EXPECT_EQ(joint.life, 49);
    CrtRandom draws(Seed);
    static_cast<void>(draws.Next());
    EXPECT_EQ(tested.random.State(), draws.State());
}

// 0x537b6b..0x537c29: la velocidad tiene tope 30; a 70 o menos del blanco, con el giro z de más de 20 y la velocidad
// de 20 o más, baja 10. Con menos de 15 de vida, los tres canales son la x por 0,8333333f.
TEST(TargetBoltJointsTest, TheBoltBrakesNearItsTargetAndFades)
{
    BoltWorld tested;
    tested.target.position = {1060.0f, 975.0f, 150.0f};
    ASSERT_TRUE(tested.Create().HasValue());
    JointState& joint = tested.joints[0];
    joint.angles = {};
    joint.speed = 25.0f;
    joint.life = 10;
    joint.light = {0.9f, 0.5f, 0.25f};
    JointState expected = joint;

    ASSERT_TRUE(tested.Step().HasValue());

    MoveAhead(expected);
    const glm::vec3 aim{tested.target.position.x, tested.target.position.y, F32(D(tested.target.position.z) + 150.0)};
    EXPECT_LE(Steer(expected, aim, 30.0f), 70.0);
    EXPECT_GT(std::abs(D(expected.angles.z)), 20.0);
    EXPECT_EQ(joint.position, expected.position);
    EXPECT_EQ(joint.angles, expected.angles);
    EXPECT_EQ(joint.speed, 20.0f);
    EXPECT_EQ(joint.light, glm::vec3(F32(D(0.9f) * D(std::bit_cast<float>(0x3f555555u)))));
    EXPECT_EQ(joint.life, 9);
}

// 0x537b3f..0x537b44 y 0x53be97: sin dueño se apaga sin rand(), con la cola y la vida comunes.
TEST(TargetBoltJointsTest, TheBoltWithoutItsTargetGoesOut)
{
    BoltWorld tested;
    ASSERT_TRUE(tested.Create().HasValue());
    JointState& joint = tested.joints[0];
    joint.ownerContext = nullptr;

    ASSERT_TRUE(tested.Step().HasValue());

    EXPECT_FALSE(joint.alive);
    EXPECT_EQ(joint.life, 49);
    EXPECT_EQ(tested.random.State(), CrtRandom(Seed).State());
}

// 0x537bf3..0x537c29 en el borde de 70: desde (1000, 1025, 300) con los ángulos en 0, velocidad 25 y vida 20, el
// avance común deja el joint en (1000, 1000, 300) y el blanco 150 más arriba queda a la misma altura, 70 más allá en x.
// Con la y del blanco en 1000 la distancia es 70 justo y frena (30 − 10); con la siguiente y en float, apenas más de
// 70, no. En los dos, la luz dinámica de radio 2 en la posición con ((rand() & 3) + 8) · 0,03f por (0,4f; 1; 0,8f).
TEST(TargetBoltJointsTest, TheBoltBrakesOnlyWithinSeventy)
{
    for (const float y : {std::nextafter(1000.0f, 2000.0f), 1000.0f})
    {
        SCOPED_TRACE(y);
        BoltWorld tested;
        tested.start = {1000.0f, 1025.0f, 300.0f};
        tested.target.position = {1070.0f, y, 150.0f};
        ASSERT_TRUE(tested.Create().HasValue());
        JointState& joint = tested.joints[0];
        joint.angles = {};
        joint.speed = 25.0f;
        joint.life = 20;
        CrtRandom draws = tested.random;

        const auto stepped = tested.Step();

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(joint.position, glm::vec3(1000.0f, 1000.0f, 300.0f));
        EXPECT_EQ(joint.speed, y == 1000.0f ? 20.0f : 30.0f);
        EXPECT_EQ(joint.light, glm::vec3(1.0f));
        const double luminosity =
            static_cast<double>((draws.Next() & 3) + 8) * D(std::bit_cast<float>(0x3cf5c28fu)); // 0x6b8bd4
        EXPECT_EQ(tested.random.State(), draws.State());
        const glm::vec3 color{F32(luminosity * D(std::bit_cast<float>(0x3ecccccdu))), F32(luminosity),
                              F32(luminosity * D(std::bit_cast<float>(0x3f4ccccdu)))};
        auto lit = std::make_unique<Client::World::TerrainLighting>();
        ASSERT_TRUE(Client::World::AddDynamicLight(lit.get(), joint.position, color, 2, "prueba").HasValue());
        EXPECT_TRUE(lit->cells == tested.lighting->cells);
        const auto dark = std::make_unique<Client::World::TerrainLighting>();
        EXPECT_FALSE(lit->cells == dark->cells);
    }
}
