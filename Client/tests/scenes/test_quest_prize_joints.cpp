#include "Scenes/FaunaJointCreation.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameDrawTail.h"
#include "World/IFishStepServices.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

// El 60f/0xa del premio de una misión (A3 0xc9..0xcb, 0x4de0fe), con el personaje de dueño: nace como el 60f/0
// (0x53373a..0x5337da y 0x53428f..0x5342d2) y en el paso (0x539f1b → 0x53b239) gira igual pero baja vz (0x53b2d5). Los
// esperados repiten las fórmulas del ASM con otro CrtRandom de la misma semilla.
namespace
{
using Client::Crt::CrtRandom;
using Client::Render::Models::Detail::CosineProductPC53;
using Client::Render::Models::Detail::SineProductPC53;
using Client::Scenes::ActorState;
using Client::Scenes::JointState;
using Client::World::FishEffectRequest;

constexpr std::int32_t Sparkle = 0x60f, FallingSubtype = 0xa;
constexpr float PrizeWidth = 40.0f;
constexpr std::int32_t PrizeKey = 2;

double D(float value)
{
    return static_cast<double>(value);
}

float F32(double value)
{
    return static_cast<float>(value);
}

float Float(std::uint32_t bits)
{
    return std::bit_cast<float>(bits);
}

// Como lo pide 0x4de0fe: el punto copiado con la z + 200 en los dos puntos, los ángulos y el dueño del personaje, el
// ancho 40 y la key 2.
Client::Core::Expected<std::optional<std::size_t>, std::string>
CreateFalling(std::span<JointState> joints, ActorState& owner, CrtRandom& random, float width = PrizeWidth)
{
    glm::vec3 raised{100.0f, 200.0f, 500.0f};
    glm::vec3 angles{};
    const FishEffectRequest request{.type = Sparkle,
                                    .firstPosition = raised,
                                    .secondPosition = raised,
                                    .angles = angles,
                                    .subtype = FallingSubtype,
                                    .ownerContext = &owner,
                                    .scale = width,
                                    .key = PrizeKey,
                                    .skill = 0,
                                    .skillSerial = 0,
                                    .target = -1};
    return Client::Scenes::CreateFaunaJoint(joints, request, random);
}

// Lo que el 60f/0xa hace en un paso de 0x53b239 con la key 2, sin la caída. Con los ángulos en 0, el movimiento común
// (0x53590e) sólo corre la y, que el giro pisa.
void TurnFalling(JointState& expected)
{
    const double argument = (static_cast<double>(expected.life) + D(expected.velocity.y)) / PrizeKey;
    const std::optional<double> cosine = CosineProductPC53(argument, expected.speed);
    const std::optional<double> sine = SineProductPC53(argument, expected.speed);
    ASSERT_TRUE(cosine && sine);
    expected.position.x = F32(*cosine + D(expected.targetPosition.x));
    expected.position.y = F32(D(expected.targetPosition.y) - *sine);
    expected.position.z = F32(D(expected.position.z) - D(expected.velocity.z));
    if (expected.life < 10)
    {
        const double factor = D(Float(0x3f44ec4fu));
        expected.light = {F32(D(expected.light.x) * factor), F32(D(expected.light.y) * factor),
                          F32(D(expected.light.z) * factor)};
    }
}
} // namespace

// 0x533677, 0x53373a..0x5337da y 0x53428f..0x5342d2: vida 100 y 20 puntos; vz (rand() % 150) · 0,01f, vy rand() % 500 −
// 250 y velocidad 40 (la 140 es de la /0x19); con el ancho 40, por encima de 10, vida 50 y vz (rand() % 250 + 200) ·
// 0,01f. La luz del personaje y el destino en el segundo punto. Tres rand(), sin la mezcla de la /0x12.
TEST(QuestPrizeJointsTest, TheFallingSparkleIsBornLikeTheSparkle)
{
    ActorState owner;
    owner.light = {0.25f, 0.5f, 0.75f};
    std::array<JointState, 2> joints{};
    CrtRandom random(7);

    const auto created = CreateFalling(joints, owner, random);

    ASSERT_TRUE(created.HasValue()) << created.Error();
    ASSERT_EQ(created.Value(), std::optional<std::size_t>(0));
    CrtRandom draws(7);
    static_cast<void>(draws.Next());
    const int drift = draws.Next();
    const int rise = draws.Next();
    EXPECT_EQ(random.State(), draws.State());
    const JointState& joint = joints[0];
    EXPECT_TRUE(joint.alive);
    EXPECT_EQ(joint.type, Sparkle);
    EXPECT_EQ(joint.texture, Sparkle);
    EXPECT_EQ(joint.subtype, FallingSubtype);
    EXPECT_EQ(joint.blend, 1);
    EXPECT_EQ(joint.ownerContext, &owner);
    EXPECT_EQ(joint.light, owner.light);
    EXPECT_EQ(joint.life, 50);
    EXPECT_EQ(joint.maximumCount, 20);
    EXPECT_EQ(joint.width, PrizeWidth);
    EXPECT_EQ(joint.key, PrizeKey);
    EXPECT_EQ(joint.speed, 40.0f);
    EXPECT_EQ(joint.velocity, glm::vec3(0.0f, static_cast<float>(drift % 500 - 250),
                                        F32(static_cast<double>(rise % 250 + 200) * D(Float(0x3c23d70au)))));
    EXPECT_EQ(joint.position, glm::vec3(100.0f, 200.0f, 500.0f));
    EXPECT_EQ(joint.targetPosition, glm::vec3(100.0f, 200.0f, 500.0f));
    EXPECT_FALSE(joints[1].alive);
}

// 0x53b239..0x53b2e1 con la key 2: (vida + vy) / 2 de argumento, la x y la y alrededor del destino, y la z que baja vz
// en cada paso (fsub, 0x53b2d5). Con la vida debajo de 10, la luz por 0,7692308f. Con el ancho 40 no lee al dueño
// aunque su acción sea de caída: vive sus 50 pasos.
TEST(QuestPrizeJointsTest, TheFallingSparkleTurnsAndGoesDown)
{
    ActorState owner;
    owner.light = glm::vec3(1.0f);
    owner.animation.action = 0xd;
    std::array<JointState, 1> joints{};
    CrtRandom random(7);
    ASSERT_TRUE(CreateFalling(joints, owner, random).HasValue());
    JointState expected = joints[0];
    const CrtRandom born = random;

    for (int step = 1; step <= 51; ++step)
    {
        SCOPED_TRACE(step);
        ASSERT_TRUE(Client::Scenes::StepJoints(joints, random).HasValue());
        TurnFalling(expected);
        expected.count = std::min(expected.count + 1, expected.maximumCount - 1);
        --expected.life;
        const JointState& joint = joints[0];
        EXPECT_EQ(joint.position, expected.position);
        EXPECT_EQ(joint.light, expected.light);
        EXPECT_EQ(joint.subtype, FallingSubtype);
        EXPECT_EQ(joint.life, expected.life);
        EXPECT_EQ(joint.alive, step < 51);
    }
    EXPECT_LT(joints[0].position.z, 500.0f);
    EXPECT_EQ(random.State(), born.State());
}

// 0x53b366..0x53b3d1: la /0xa también cae al 60f/1 con el ancho debajo de 40 y la acción del dueño en [0xd, 0x21] o
// [0x38, 0x3c]: sin cola y la vida en 10 a lo sumo.
TEST(QuestPrizeJointsTest, ANarrowFallingSparkleFallsWithItsOwner)
{
    for (const std::uint8_t action : {std::uint8_t{0xc}, std::uint8_t{0x38}})
    {
        SCOPED_TRACE(static_cast<int>(action));
        ActorState owner;
        owner.light = glm::vec3(1.0f);
        owner.animation.action = action;
        std::array<JointState, 1> joints{};
        CrtRandom random(7);
        ASSERT_TRUE(CreateFalling(joints, owner, random, 10.0f).HasValue());
        EXPECT_EQ(joints[0].life, 100);

        ASSERT_TRUE(Client::Scenes::StepJoints(joints, random).HasValue());

        const bool falls = action == 0x38;
        EXPECT_EQ(joints[0].subtype, falls ? 1 : FallingSubtype);
        EXPECT_EQ(joints[0].tailEnabled, !falls);
        EXPECT_EQ(joints[0].life, falls ? 9 : 99);
    }
}
