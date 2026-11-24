#include "Scenes/FaunaJointCreation.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Scenes/ActorState.h"
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

// El 0x60f y el 0x62b con el subtipo 0x10, 0xe, 0xf y 0x13 (OrbitJoints.cpp y DropJoints.cpp): el alta 0x533677
// (0x5337fa, 0x534132 y 0x533a28) y el paso 0x539f1b (0x53a5c7, 0x53b18a y 0x53a12c). Los esperados de la /0xe y la
// /0xf salen de otra corrida (Fraction exacta y el seno con 80 dígitos, el x87.py del revisor).
namespace
{
using Client::Crt::CrtRandom;
using Client::Scenes::ActorState;
using Client::Scenes::JointOwnerKind;
using Client::Scenes::JointState;

constexpr std::uint32_t Seed = 0x60fu;

std::uint32_t Bits(float value)
{
    return std::bit_cast<std::uint32_t>(value);
}

float F32(double value)
{
    return static_cast<float>(value);
}

struct OrbitWorld
{
    Client::Core::Expected<std::optional<std::size_t>, std::string> Create(std::int32_t type, std::int32_t subtype,
                                                                           void* owner, float scale,
                                                                           JointOwnerKind kind = JointOwnerKind::ByType)
    {
        glm::vec3 from = start;
        glm::vec3 to = second;
        glm::vec3 angles{};
        const Client::World::FishEffectRequest request{.type = type,
                                                       .firstPosition = from,
                                                       .secondPosition = to,
                                                       .angles = angles,
                                                       .subtype = subtype,
                                                       .ownerContext = owner,
                                                       .scale = scale,
                                                       .key = -1,
                                                       .skill = 0,
                                                       .skillSerial = 0,
                                                       .target = -1};
        return Client::Scenes::CreateFaunaJoint(joints, request, random, kind);
    }

    Client::Core::Expected<void, std::string> Step()
    {
        return Client::Scenes::StepJoints(joints, random, nullptr, nullptr, 0.0f, nullptr);
    }

    glm::vec3 start{1000.0f, 2000.0f, 100.0f};
    glm::vec3 second{1010.0f, 2020.0f, 400.0f};
    std::vector<JointState> joints = std::vector<JointState>(2);
    CrtRandom random{Seed};
};
} // namespace

// 0x5337fa..0x533855: la /0x10 de la lluvia de sombras (0x51bcb0, con el personaje de dueño y ancho 120), sin rand():
// sin la cola común, la luz en 0,1f, vida 60, el destino en el segundo punto y el ancho; veinte veces la z + 50 y un
// tramo. La cuenta queda en 19 y el primer tramo del alta (el de z 100) se cae en el vigésimo.
TEST(OrbitJointsTest, TheShadowColumnRisesTwentyTimesFifty)
{
    for (const std::int32_t type : {0x60f, 0x62b})
    {
        SCOPED_TRACE(type);
        OrbitWorld tested;
        ActorState owner{};
        ASSERT_TRUE(tested.Create(type, 0x10, &owner, 120.0f).HasValue());

        EXPECT_EQ(tested.random.State(), CrtRandom{Seed}.State());
        const JointState& joint = tested.joints[0];
        EXPECT_FALSE(joint.tailEnabled);
        EXPECT_EQ(Bits(joint.light.x), 0x3dcccccdu);
        EXPECT_EQ(joint.light.x, joint.light.z);
        EXPECT_EQ(joint.life, 60);
        EXPECT_EQ(joint.maximumCount, 20);
        EXPECT_EQ(joint.width, 120.0f);
        EXPECT_EQ(joint.speed, 0.0f);
        EXPECT_EQ(joint.texture, type);
        EXPECT_EQ(joint.targetPosition, tested.second);
        EXPECT_EQ(joint.position, (glm::vec3{1000.0f, 2000.0f, 1100.0f}));
        EXPECT_EQ(joint.count, 19);
        EXPECT_EQ(joint.points[0][0], (glm::vec3{940.0f, 2000.0f, 1100.0f}));
        EXPECT_EQ(joint.points[19][3], (glm::vec3{1000.0f, 2000.0f, 210.0f}));
    }
}

// 0x53a5c7..0x53a6ae: el personaje que se movió (10, −20, 5) desde el destino corre los tramos 18..0 lo mismo (el 19,
// el de la cuenta, no) y el destino pasa a su posición. La luz: con vida 60, por 1,04f; con 30, en 0,25; con 15, por
// 0,9090909f; con 5, además por 0,7692308f (0x53b318). Sin la cola común ni movimiento: la cuenta sigue en 19.
TEST(OrbitJointsTest, TheShadowColumnFollowsItsOwner)
{
    constexpr float Dim = std::bit_cast<float>(0x3dcccccdu);
    struct Probe
    {
        std::int32_t life;
        float light;
    };
    for (const Probe& probe :
         {Probe{60, F32(static_cast<double>(Dim) * static_cast<double>(1.04f))}, Probe{30, 0.25f},
          Probe{15, F32(static_cast<double>(Dim) * static_cast<double>(std::bit_cast<float>(0x3f68ba2eu)))},
          Probe{5, F32(static_cast<double>(
                           F32(static_cast<double>(Dim) * static_cast<double>(std::bit_cast<float>(0x3f68ba2eu)))) *
                       static_cast<double>(std::bit_cast<float>(0x3f44ec4fu)))}})
    {
        SCOPED_TRACE(probe.life);
        OrbitWorld tested;
        ActorState owner{};
        ASSERT_TRUE(tested.Create(0x60f, 0x10, &owner, 120.0f).HasValue());
        JointState& joint = tested.joints[0];
        joint.life = probe.life;
        owner.position = tested.second + glm::vec3(10.0f, -20.0f, 5.0f);
        const glm::vec3 first = joint.points[0][1];
        const glm::vec3 eighteenth = joint.points[18][2];
        const glm::vec3 last = joint.points[19][0];

        ASSERT_TRUE(tested.Step().HasValue());

        EXPECT_EQ(joint.targetPosition, owner.position);
        EXPECT_EQ(joint.points[0][1], first + glm::vec3(10.0f, -20.0f, 5.0f));
        EXPECT_EQ(joint.points[18][2], eighteenth + glm::vec3(10.0f, -20.0f, 5.0f));
        EXPECT_EQ(joint.points[19][0], last);
        EXPECT_EQ(joint.position, (glm::vec3{1000.0f, 2000.0f, 1100.0f}));
        EXPECT_EQ(joint.count, 19);
        EXPECT_EQ(Bits(joint.light.y), Bits(probe.light));
        EXPECT_EQ(joint.life, probe.life - 1);
    }

    // Sin dueño (0x53a5cc), nada: ni los tramos ni la luz.
    OrbitWorld ownerless;
    ASSERT_TRUE(ownerless.Create(0x62b, 0x10, nullptr, 120.0f).HasValue());
    const glm::vec3 kept = ownerless.joints[0].points[3][0];
    ASSERT_TRUE(ownerless.Step().HasValue());
    EXPECT_EQ(ownerless.joints[0].points[3][0], kept);
    EXPECT_EQ(Bits(ownerless.joints[0].light.x), 0x3dcccccdu);
}

// 0x534132..0x5341f7: la /0xe y la /0xf con la mezcla 4, la textura 0x613, el ancho, vz (rand() % 150) · 0,01f, vy
// rand() % 500 − 250, velocidad 30, la luz (0,2f; 0,4f; 0,8f), vida 20 y la z + rand() % 25. Con el ancho de más de 10,
// vz se vuelve a sortear: (rand() % 250 + 150) · 0,01f; con 10 justo, no.
TEST(OrbitJointsTest, TheWhirlBornsWithThreeOrFourDraws)
{
    constexpr double Hundredth = static_cast<double>(0.01f);
    for (const float width : {10.0f, 40.0f})
    {
        SCOPED_TRACE(width);
        OrbitWorld tested;
        Client::World::ModelEffect effect{};
        ASSERT_TRUE(tested.Create(0x62b, 0xf, &effect, width, JointOwnerKind::ModelEffect).HasValue());
        CrtRandom replay{Seed};
        float lift = F32(static_cast<double>(replay.Next() % 150) * Hundredth);
        const auto turn = static_cast<float>(replay.Next() % 500 - 250);
        if (width > 10.0f)
            lift = F32(static_cast<double>(replay.Next() % 250 + 150) * Hundredth);
        const auto height = static_cast<float>(replay.Next() % 25);

        EXPECT_EQ(tested.random.State(), replay.State());
        const JointState& joint = tested.joints[0];
        EXPECT_EQ(joint.blend, 4);
        EXPECT_EQ(joint.texture, 0x613);
        EXPECT_TRUE(joint.tailEnabled);
        EXPECT_EQ(joint.width, width);
        EXPECT_EQ(joint.velocity, (glm::vec3{0.0f, turn, lift}));
        EXPECT_EQ(joint.speed, 30.0f);
        EXPECT_EQ(Bits(joint.light.x), 0x3e4ccccdu);
        EXPECT_EQ(Bits(joint.light.y), 0x3ecccccdu);
        EXPECT_EQ(Bits(joint.light.z), 0x3f4ccccdu);
        EXPECT_EQ(joint.life, 20);
        EXPECT_EQ(joint.maximumCount, 20);
        EXPECT_EQ(joint.position.z, F32(static_cast<double>(height) + 100.0));
        EXPECT_EQ(joint.targetPosition, tested.second);
        EXPECT_EQ(joint.ownerContext, &effect);
    }
}

// 0x53b18a..0x53b234: con dueño, después del movimiento común (con los ángulos en 0, (0, −30, 0)), la x y la y giran
// alrededor del destino con a = vy · 0,1f (la /0xf, −a) y la velocidad de radio; la z + vz. Con vida de menos de 20 la
// velocidad + 5; el ancho + 1, vy + 4 y vz + (rand() % 200 + 200) · 0,01f. Sigue la cola común, con su tramo.
TEST(OrbitJointsTest, TheWhirlTurnsAroundItsTarget)
{
    struct Probe
    {
        std::int32_t subtype;
        float turn, speed;
        glm::vec3 target;
        float lift, height;
        std::uint32_t x, y, z;
        float nextSpeed;
    };
    for (
        const Probe& probe :
        {Probe{
             0xe, 123.0f, 30.0f, {1100.5f, 2100.25f, 0.0f}, 1.5f, 300.0f, 0x448d2e25u, 0x4503c25au, 0x4396c000u, 35.0f},
         Probe{
             0xf, 123.0f, 30.0f, {1100.5f, 2100.25f, 0.0f}, 1.5f, 300.0f, 0x448d2e25u, 0x4502c5a6u, 0x4396c000u, 35.0f},
         Probe{0xe,
               -237.0f,
               47.0f,
               {-350.75f, 812.5f, 0.0f},
               2.25f,
               77.0f,
               0xc3ac2418u,
               0x443f7c9fu,
               0x429e8000u,
               52.0f}})
    {
        SCOPED_TRACE(probe.y);
        OrbitWorld tested;
        Client::World::ModelEffect effect{};
        ASSERT_TRUE(tested.Create(0x62b, probe.subtype, &effect, 40.0f, JointOwnerKind::ModelEffect).HasValue());
        JointState& joint = tested.joints[0];
        joint.angles = glm::vec3(0.0f);
        joint.velocity = {0.0f, probe.turn, probe.lift};
        joint.speed = probe.speed;
        joint.targetPosition = probe.target;
        joint.position.z = probe.height;
        joint.life = 15;
        CrtRandom replay = tested.random;
        const float lift =
            F32(static_cast<double>(replay.Next() % 200 + 200) * static_cast<double>(0.01f) + probe.lift);

        ASSERT_TRUE(tested.Step().HasValue());

        EXPECT_EQ(tested.random.State(), replay.State());
        EXPECT_EQ(Bits(joint.position.x), probe.x);
        EXPECT_EQ(Bits(joint.position.y), probe.y);
        EXPECT_EQ(Bits(joint.position.z), probe.z);
        EXPECT_EQ(joint.speed, probe.nextSpeed);
        EXPECT_EQ(joint.width, 41.0f);
        EXPECT_EQ(joint.velocity.y, probe.turn + 4.0f);
        EXPECT_EQ(joint.velocity.z, lift);
        EXPECT_EQ(joint.count, 1);
        EXPECT_EQ(joint.life, 14);
    }

    // Con vida 20 la velocidad no crece; sin dueño (0x53b18f) no gira ni saca rand(): sólo el movimiento común.
    OrbitWorld steady;
    Client::World::ModelEffect effect{};
    ASSERT_TRUE(steady.Create(0x60f, 0xe, &effect, 40.0f, JointOwnerKind::ModelEffect).HasValue());
    ASSERT_TRUE(steady.Step().HasValue());
    EXPECT_EQ(steady.joints[0].speed, 30.0f);
    OrbitWorld ownerless;
    ASSERT_TRUE(ownerless.Create(0x60f, 0xe, nullptr, 40.0f).HasValue());
    ownerless.joints[0].angles = glm::vec3(0.0f);
    const glm::vec3 before = ownerless.joints[0].position;
    const CrtRandom drawn = ownerless.random;
    ASSERT_TRUE(ownerless.Step().HasValue());
    EXPECT_EQ(ownerless.random.State(), drawn.State());
    EXPECT_EQ(ownerless.joints[0].position, before - glm::vec3(0.0f, 30.0f, 0.0f));
    EXPECT_EQ(ownerless.joints[0].width, 40.0f);
}

// 0x533a28..0x533a40 y 0x5340fe..0x53412d: la /0x13 (las columnas del círculo 0xc3, 0x5236ce, con el 0x62b, sin dueño
// y ancho 40) nace como la gota pero con vz negada y sin la textura 0x62d. 0x53a12c: con vida de más de 25 se queda;
// con 25 o menos, vz − 5 y la z + vz (la suma sin redondear), x e y en el destino.
TEST(OrbitJointsTest, TheSinkingDropFallsWithItsNegatedSpeed)
{
    OrbitWorld tested;
    ASSERT_TRUE(tested.Create(0x62b, 0x13, nullptr, 40.0f).HasValue());
    CrtRandom replay{Seed};
    const auto fall = static_cast<float>(-(replay.Next() % 20 + 35));
    const std::int32_t life = replay.Next() % 50 + 25;
    EXPECT_EQ(tested.random.State(), replay.State());
    JointState& joint = tested.joints[0];
    EXPECT_EQ(joint.texture, 0x62b);
    EXPECT_EQ(joint.velocity.z, fall);
    EXPECT_EQ(joint.life, life);
    EXPECT_EQ(joint.width, 40.0f);
    EXPECT_EQ(joint.light, glm::vec3(1.0f));
    EXPECT_EQ(joint.targetPosition, tested.second);

    joint.life = 26;
    ASSERT_TRUE(tested.Step().HasValue());
    EXPECT_EQ(joint.position, tested.start);
    EXPECT_EQ(joint.velocity.z, fall);

    joint.velocity.z = -35.0f;
    ASSERT_TRUE(tested.Step().HasValue());
    EXPECT_EQ(joint.velocity.z, -40.0f);
    EXPECT_EQ(joint.position, (glm::vec3{1010.0f, 2020.0f, 60.0f}));
    EXPECT_EQ(joint.life, 24);

    // El 0x62b/2 también es la gota de siempre (vz positiva).
    OrbitWorld drop;
    ASSERT_TRUE(drop.Create(0x62b, 2, nullptr, 40.0f).HasValue());
    CrtRandom dropReplay{Seed};
    EXPECT_EQ(drop.joints[0].velocity.z, static_cast<float>(dropReplay.Next() % 20 + 35));
}
