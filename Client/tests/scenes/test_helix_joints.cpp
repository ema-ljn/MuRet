#include "Scenes/FaunaJointCreation.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Scenes/GameDrawTail.h"
#include "World/IFishStepServices.h"
#include "World/ModelEffects.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// El 0x60f/4 y /0xc (HelixJoints.cpp) que el 0x812 suelta en cada paso (0x52c9da y 0x52c9a7, con él de dueño): el alta
// de 0x533faf..0x5340d7 y el paso de 0x53b08a..0x53b14c, que se repite en el mismo cuadro hasta que la vida que queda
// es múltiplo de 12 (0x53c3c6). El 0x62b/4 y /0xc pasan por los mismos.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Render::Models::Detail::CosineDoubleProductPC53;
using Client::Render::Models::Detail::SineDoubleProductPC53;
using Client::Scenes::JointOwnerKind;
using Client::Scenes::JointState;

constexpr std::uint32_t Seed = 0x60fu;
constexpr float Tenth = std::bit_cast<float>(0x3dcccccdu);           // 0x6b87b8
constexpr float NegativeRadians = std::bit_cast<float>(0xbc8efa35u); // 0x6ba2c4
constexpr float Radians = std::bit_cast<float>(0x3c8efa35u);         // 0x6b8cf8

double D(float value)
{
    return static_cast<double>(value);
}

float F32(double value)
{
    return static_cast<float>(value);
}

struct Helix
{
    Client::Core::Expected<std::optional<std::size_t>, std::string> Create(std::int32_t type, std::int32_t subtype,
                                                                           bool owned = true)
    {
        glm::vec3 from = start;
        glm::vec3 to = target;
        glm::vec3 turn{0.0f, 0.0f, 30.0f};
        const Client::World::FishEffectRequest request{.type = type,
                                                       .firstPosition = from,
                                                       .secondPosition = to,
                                                       .angles = turn,
                                                       .subtype = subtype,
                                                       .ownerContext = owned ? &owner : nullptr,
                                                       .scale = subtype == 0xc ? 100.0f : 50.0f,
                                                       .key = -1,
                                                       .skill = 0,
                                                       .skillSerial = 0,
                                                       .target = -1};
        return Client::Scenes::CreateFaunaJoint(joints, request, random,
                                                owned ? JointOwnerKind::ModelEffect : JointOwnerKind::ByType);
    }

    Client::Core::Expected<void, std::string> Step()
    {
        return Client::Scenes::StepJoints(joints, random, nullptr, nullptr, 0.0f, nullptr);
    }

    std::vector<JointState> joints = std::vector<JointState>(2);
    CrtRandom random{Seed};
    Client::World::ModelEffect owner{};
    glm::vec3 start{1000.0f, 2000.0f, 300.0f};
    glm::vec3 target{1100.0f, 2100.0f, 320.0f};
};

// 0x53b08a..0x53b14c y 0x53b318..0x53b363: una vuelta del paso, con la vida de antes de restarla.
void TurnOnce(JointState& joint)
{
    const double turn = (static_cast<double>(joint.life) + D(joint.velocity.x)) * D(Tenth);
    double horizontal = 0;
    double vertical = 0;
    if (joint.subtype == 0xc)
    {
        horizontal = *CosineDoubleProductPC53(turn, -26.0);
        vertical = *SineDoubleProductPC53(D(F32(turn)), 26.0) -
                   static_cast<double>(0x5a - joint.life) * D(std::bit_cast<float>(0x3e99999au));
    }
    else
    {
        const int grown = joint.life + 0x28;
        const double radius = static_cast<double>(grown > 0xa ? grown : 0xa) * D(std::bit_cast<float>(0x3f266666u));
        horizontal = -*CosineDoubleProductPC53(D(F32(turn)), radius);
        vertical = *SineDoubleProductPC53(D(F32(turn)), radius);
    }
    joint.targetPosition.x = F32(D(joint.velocity.y) + D(joint.targetPosition.x));
    joint.targetPosition.y = F32(D(joint.velocity.z) + D(joint.targetPosition.y));
    const double facing = (90.0 - D(joint.angles.z)) * D(Radians);
    joint.position = {F32(*SineDoubleProductPC53(facing, horizontal) + D(joint.targetPosition.x)),
                      F32(*CosineDoubleProductPC53(facing, horizontal) + D(joint.targetPosition.y)),
                      F32(vertical + D(joint.targetPosition.z))};
    if (joint.life < 10)
        for (int channel = 0; channel < 3; ++channel)
            joint.light[channel] = F32(D(joint.light[channel]) * D(std::bit_cast<float>(0x3f44ec4fu)));
    --joint.life;
}
} // namespace

// 0x533677..0x53368e y 0x533faf..0x5340d7: el ancho de la escala, el destino en el segundo punto y vx en rand() % 360
// (un rand()); vy y vz, el seno y el coseno de −z en radianes por −4 (la /0xc) o −2 (la /4). La /0xc con vida 70, la
// luz (0,1f; 0,1f; 1) y 50 puntos; la /4 con vida 110, la luz del dueño (+0xf4) y 200 puntos que el tope de 0x534cb6
// deja en 50.
TEST(HelixJointsTest, EachHelixIsBornAroundItsTarget)
{
    for (const std::int32_t type : {0x60f, 0x62b})
    {
        for (const std::int32_t subtype : {4, 0xc})
        {
            SCOPED_TRACE(type);
            SCOPED_TRACE(subtype);
            Helix helix;
            helix.owner.light = {0.25f, 0.5f, 0.75f};
            const auto created = helix.Create(type, subtype);
            ASSERT_TRUE(created.HasValue()) << created.Error();
            ASSERT_EQ(created.Value(), std::optional<std::size_t>{0});
            CrtRandom expected{Seed};
            const int turn = expected.Next() % 360;
            EXPECT_EQ(helix.random.State(), expected.State());
            const JointState& joint = helix.joints[0];
            const bool wide = subtype == 0xc;
            const double reach = wide ? -4.0 : -2.0;
            const double argument = D(30.0f) * D(NegativeRadians);
            EXPECT_EQ(joint.width, wide ? 100.0f : 50.0f);
            EXPECT_EQ(joint.life, wide ? 70 : 110);
            EXPECT_EQ(joint.maximumCount, 50);
            EXPECT_EQ(joint.light, wide ? glm::vec3(Tenth, Tenth, 1.0f) : glm::vec3(0.25f, 0.5f, 0.75f));
            EXPECT_EQ(joint.velocity.x, static_cast<float>(turn));
            EXPECT_EQ(joint.velocity.y, F32(*SineDoubleProductPC53(argument, reach)));
            EXPECT_EQ(joint.velocity.z, F32(*CosineDoubleProductPC53(argument, reach)));
            EXPECT_EQ(joint.targetPosition, helix.target);
            EXPECT_EQ(joint.ownerContext, &helix.owner);
        }
    }
}

// 0x534070: la /4 lee la luz de su dueño; sin dueño, el original lee de 0 y MuRet corta antes del rand(). Con la tabla
// llena, el alta no hace nada ni saca rand().
TEST(HelixJointsTest, TheNarrowHelixNeedsItsOwnerAndAFreePlace)
{
    Helix lonely;
    const auto cut = lonely.Create(0x60f, 4, false);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x534070"), std::string::npos);

    Helix full;
    for (JointState& joint : full.joints)
        joint.alive = true;
    for (const std::int32_t subtype : {4, 0xc})
    {
        const auto created = full.Create(0x60f, subtype);
        ASSERT_TRUE(created.HasValue()) << created.Error();
        EXPECT_FALSE(created.Value().has_value());
    }
    EXPECT_EQ(full.random.State(), CrtRandom{Seed}.State());
}

// 0x53b08a..0x53b14c: el destino avanza (vy, vz) y la posición gira alrededor de él; 0x53c393 resta la vida y agrega
// el tramo, y 0x53c3c6 repite el paso mientras la vida que queda no sea múltiplo de 12. Con vida 110 son dos vueltas
// (110 y 109); con 108, doce (108..97). Sin rand().
TEST(HelixJointsTest, TheHelixTurnsUntilItsLifeIsAMultipleOfTwelve)
{
    for (const std::int32_t subtype : {4, 0xc})
    {
        SCOPED_TRACE(subtype);
        Helix helix;
        ASSERT_TRUE(helix.Create(0x60f, subtype).HasValue());
        JointState& joint = helix.joints[0];
        joint.life = 110;
        for (const int turns : {2, 12})
        {
            SCOPED_TRACE(turns);
            JointState expected = joint;
            for (int turn = 0; turn < turns; ++turn)
                TurnOnce(expected);
            const auto state = helix.random.State();
            const std::int32_t count = joint.count;

            const auto stepped = helix.Step();
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();

            EXPECT_EQ(joint.life, expected.life);
            EXPECT_EQ(joint.life % 12, 0);
            EXPECT_EQ(joint.targetPosition, expected.targetPosition);
            EXPECT_EQ(joint.position, expected.position);
            EXPECT_EQ(joint.count, count + turns);
            EXPECT_EQ(helix.random.State(), state);
        }
    }
}

// 0x53b318..0x53b363: con menos de 10 de vida, cada vuelta multiplica la luz por 0,7692308f. Con 9, el cuadro da nueve
// vueltas hasta 0; el siguiente da una y el joint muere (0x53c3bd).
TEST(HelixJointsTest, TheHelixFadesInItsLastTurns)
{
    Helix helix;
    helix.owner.light = {0.25f, 0.5f, 0.75f};
    ASSERT_TRUE(helix.Create(0x60f, 4).HasValue());
    JointState& joint = helix.joints[0];
    joint.life = 9;
    JointState expected = joint;
    for (int turn = 0; turn < 9; ++turn)
        TurnOnce(expected);

    ASSERT_TRUE(helix.Step().HasValue());

    EXPECT_EQ(joint.life, 0);
    EXPECT_EQ(joint.light, expected.light);
    EXPECT_EQ(joint.position, expected.position);
    EXPECT_TRUE(joint.alive);

    ASSERT_TRUE(helix.Step().HasValue());

    EXPECT_FALSE(joint.alive);
}
