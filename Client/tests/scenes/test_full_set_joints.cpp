#include "Joints/FaunaJointCreation.h"
#include "Kernel/HelperTable.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Math/OriginalAngles.h"
#include "Math/OriginalRetainedMath.h"
#include "Math/OriginalRotation.h"
#include "Kernel/ActorState.h"
#include "Scenes/GameDrawTail.h"
#include "Pets/HelperTable.h"
#include "Joints/JointKernels.h"
#include "Kernel/JointRequest.h"
#include "Particles/Sprites.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

// Los joints del conjunto completo (0x5102c3..0x5104cb) y el del 5ac/3 (0x52aa5d), con el personaje de dueño: el 60f/0
// y el 60f/0x12 con su caída al 60f/1, el 610/7 y el 619/9. Los esperados repiten las fórmulas del ASM de 0x530860 y
// 0x535840 con otro CrtRandom de la misma semilla.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Kernel::HelperState;
using Client::Kernel::JointOwnerKind;
using Client::Kernel::JointRequest;
using Client::Math::Detail::CosineProductPC53;
using Client::Math::Detail::SineProductPC53;
using Client::Scenes::JointState;

using Client::Math::D;
using Client::Math::F32;

float Float(std::uint32_t bits)
{
    return std::bit_cast<float>(bits);
}

glm::vec3 Faded(const glm::vec3& light, std::uint32_t factorBits)
{
    const double factor = D(Float(factorBits));
    return {F32(D(light.x) * factor), F32(D(light.y) * factor), F32(D(light.z) * factor)};
}

double Cosine(double argument, float multiplier)
{
    const std::optional<double> product = CosineProductPC53(argument, multiplier);
    EXPECT_TRUE(product.has_value());
    return product.value_or(0.0);
}

double Sine(double argument, float multiplier)
{
    const std::optional<double> product = SineProductPC53(argument, multiplier);
    EXPECT_TRUE(product.has_value());
    return product.value_or(0.0);
}

struct Request
{
    std::int32_t type;
    std::int32_t subtype;
    float width;
    std::int32_t key;
    std::int32_t skill;
    glm::vec3 first{100.0f, 200.0f, 300.0f};
    glm::vec3 second{110.0f, 190.0f, 305.0f};
    glm::vec3 angles{};
};

Client::Core::Expected<std::optional<std::size_t>, std::string>
Create(std::span<JointState> joints, Request request, void* owner, CrtRandom& random,
       JointOwnerKind ownerKind = JointOwnerKind::ByType)
{
    const JointRequest made{request.type,
                            request.first,
                            request.second,
                            request.angles,
                            request.subtype,
                            owner,
                            request.width,
                            request.key,
                            request.skill,
                            0,
                            -1};
    return Client::Joints::CreateFaunaJoint(joints, made, random, ownerKind);
}

// La cola común (0x53c393) con los ángulos en 0: count sube hasta maximumCount − 1.
std::int32_t Appended(std::int32_t count, std::int32_t maximum)
{
    return std::min(count + 1, maximum - 1);
}

// Lo que un 60f/0 o 60f/0x12 hace en un paso de 0x53b239 con la key en −1, sin la caída.
void TurnSparkle(JointState& expected)
{
    const double tenth = D(Float(0x3dcccccdu));
    double argument = 0;
    if (expected.subtype == 0x12)
    {
        argument = (static_cast<double>(expected.life * 2) + D(expected.velocity.y)) * tenth;
        if (expected.skill == 1)
            argument = argument * -1.0;
    }
    else
        argument = (static_cast<double>(expected.life) + D(expected.velocity.y)) * tenth;
    const double cosine = Cosine(argument, expected.speed);
    const double sine = Sine(argument, expected.speed);
    expected.position.x = F32(cosine + D(expected.targetPosition.x));
    expected.position.y = F32(D(expected.targetPosition.y) - sine);
    if (expected.subtype == 0x12)
    {
        expected.position.z = F32(D(expected.velocity.z) * D(Float(0x3f8ccccdu)) + D(expected.position.z));
        expected.speed = F32(D(expected.speed) + tenth);
        expected.width = F32(D(expected.width) + D(Float(0x3e4ccccdu)));
    }
    else
        expected.position.z = F32(D(expected.velocity.z) + D(expected.position.z));
    if (expected.life < 10)
        expected.light = Faded(expected.light, 0x3f44ec4fu);
}
} // namespace

// 0x533677 y 0x5341fc..0x5342d2: el 60f/0 de ancho 10 (0x51037c): vida 100, 20 puntos, velocidad 40, vz (rand() % 150)
// · 0,01f, vy rand() % 500 − 250, la luz del personaje y el destino en el segundo punto. Dos rand().
TEST(FullSetJointsTest, TheSparkleIsBornWithTheLightOfItsOwnerAndTwoRandoms)
{
    ActorState actor;
    actor.light = {0.25f, 0.5f, 0.75f};
    std::array<JointState, 1> joints{};
    CrtRandom random(1);
    const Request request{0x60f, 0, 10.0f, -1, 0};

    const auto created = Create(joints, request, &actor, random);

    ASSERT_TRUE(created.HasValue()) << created.Error();
    ASSERT_EQ(created.Value(), std::optional<std::size_t>(0));
    CrtRandom draws(1);
    const int rise = draws.Next();
    const int drift = draws.Next();
    EXPECT_EQ(random.State(), draws.State());
    const JointState& joint = joints[0];
    EXPECT_TRUE(joint.alive);
    EXPECT_EQ(joint.type, 0x60f);
    EXPECT_EQ(joint.subtype, 0);
    EXPECT_EQ(joint.texture, 0x60f);
    EXPECT_EQ(joint.blend, 1);
    EXPECT_EQ(joint.life, 100);
    EXPECT_EQ(joint.maximumCount, 20);
    EXPECT_EQ(joint.width, 10.0f);
    EXPECT_EQ(joint.speed, 40.0f);
    EXPECT_EQ(joint.velocity, glm::vec3(0.0f, static_cast<float>(drift % 500 - 250),
                                        F32(static_cast<double>(rise % 150) * D(Float(0x3c23d70au)))));
    EXPECT_EQ(joint.light, actor.light);
    EXPECT_EQ(joint.position, request.first);
    EXPECT_EQ(joint.targetPosition, request.second);
    EXPECT_EQ(joint.ownerContext, &actor);
    EXPECT_EQ(joint.key, -1);
    EXPECT_TRUE(joint.tailEnabled);
}

// Sin el personaje, el 60f/0 no puede leer su luz (0x53429b).
TEST(FullSetJointsTest, TheSparkleNeedsItsOwner)
{
    std::array<JointState, 1> joints{};
    CrtRandom random(1);

    const auto created = Create(joints, {0x60f, 0x12, 20.0f, -1, 0}, nullptr, random);

    ASSERT_FALSE(created.HasValue());
    EXPECT_NE(created.Error().find("0x53429b"), std::string::npos) << created.Error();
}

// 0x53b239..0x53b3d1: el 60f/0 gira a 40 alrededor de su destino con (vida + vy) · 0,1f, sube vz y, con la vida debajo
// de 10, la luz por 0,7692308f; la cola común lo agrega y muere en el paso 101. Sin rand().
TEST(FullSetJointsTest, TheSparkleTurnsAroundItsTargetUntilItsLifeRunsOut)
{
    ActorState actor;
    actor.light = {0.25f, 0.5f, 0.75f};
    std::array<JointState, 1> joints{};
    CrtRandom random(1);
    ASSERT_TRUE(Create(joints, {0x60f, 0, 10.0f, -1, 0}, &actor, random).HasValue());
    JointState expected = joints[0];
    const std::uint32_t state = random.State();

    for (int step = 1; step <= 101; ++step)
    {
        SCOPED_TRACE(step);
        ASSERT_TRUE(Client::Joints::StepJoints(joints, random).HasValue());
        TurnSparkle(expected);
        expected.count = Appended(expected.count, expected.maximumCount);
        --expected.life;
        const JointState& joint = joints[0];
        EXPECT_EQ(joint.position, expected.position);
        EXPECT_EQ(joint.light, expected.light);
        EXPECT_EQ(joint.count, expected.count);
        EXPECT_EQ(joint.life, expected.life);
        EXPECT_EQ(joint.subtype, 0);
        EXPECT_EQ(joint.alive, step < 101);
    }
    EXPECT_EQ(random.State(), state);
}

// 0x53b378..0x53b3a9: con el ancho debajo de 40 y la acción del dueño en [0xd, 0x21] o [0x38, 0x3c], después de girar,
// pasa al 60f/1 sin cola y con la vida en 10 a lo sumo. El 60f/1 no se mueve (0x5358bc), sólo se apaga con la vida
// debajo de 10, y muere cuando se le acaba la vida (Q-C383).
TEST(FullSetJointsTest, TheSparkleFallsWithTheActionsOfItsOwnerAndThenOnlyFades)
{
    struct Case
    {
        std::uint8_t action;
        bool falls;
    };
    constexpr std::array cases{Case{0xc, false},  Case{0xd, true},  Case{0x21, true}, Case{0x22, false},
                               Case{0x37, false}, Case{0x38, true}, Case{0x3c, true}, Case{0x3d, false}};
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(static_cast<int>(probe.action));
        ActorState actor;
        actor.light = glm::vec3(1.0f);
        std::array<JointState, 1> joints{};
        CrtRandom random(1);
        ASSERT_TRUE(Create(joints, {0x60f, 0, 10.0f, -1, 0}, &actor, random).HasValue());
        JointState expected = joints[0];
        for (int step = 0; step < 3; ++step)
        {
            ASSERT_TRUE(Client::Joints::StepJoints(joints, random).HasValue());
            TurnSparkle(expected);
            expected.count = Appended(expected.count, expected.maximumCount);
            --expected.life;
        }

        actor.animation.action = probe.action;
        ASSERT_TRUE(Client::Joints::StepJoints(joints, random).HasValue());
        TurnSparkle(expected);
        JointState& joint = joints[0];
        EXPECT_EQ(joint.position, expected.position);
        if (!probe.falls)
        {
            EXPECT_EQ(joint.subtype, 0);
            EXPECT_TRUE(joint.tailEnabled);
            EXPECT_EQ(joint.count, Appended(expected.count, expected.maximumCount));
            EXPECT_EQ(joint.life, expected.life - 1);
            continue;
        }
        EXPECT_EQ(joint.subtype, 1);
        EXPECT_FALSE(joint.tailEnabled);
        EXPECT_EQ(joint.count, expected.count);
        EXPECT_EQ(joint.life, 9);

        glm::vec3 light = joint.light;
        for (int step = 1; step <= 10; ++step)
        {
            SCOPED_TRACE(step);
            ASSERT_TRUE(Client::Joints::StepJoints(joints, random).HasValue());
            light = Faded(light, 0x3f44ec4fu);
            EXPECT_EQ(joint.position, expected.position);
            EXPECT_EQ(joint.light, light);
            EXPECT_EQ(joint.count, expected.count);
            EXPECT_EQ(joint.life, 9 - step);
            EXPECT_EQ(joint.alive, step < 10);
        }
    }
}

// 0x444740: el 60f/0 del caballo oscuro lleva su HelperState de dueño, y 0x53429b lee de él la luz propia (+0xf4, la
// que 0x443d70 pone en 3), no la del personaje. Lo demás nace igual que con el personaje.
TEST(FullSetJointsTest, TheSparkleOfTheHorseIsBornWithTheLightOfTheHorse)
{
    HelperState horse;
    horse.light = {3.0f, 2.0f, 1.0f};
    horse.horse.animation.action = 0xd;
    std::array<JointState, 1> joints{};
    CrtRandom random(1);
    const Request request{0x60f, 0, 40.0f, 2, 0};

    const auto created = Create(joints, request, &horse, random, JointOwnerKind::Helper);

    ASSERT_TRUE(created.HasValue()) << created.Error();
    ASSERT_EQ(created.Value(), std::optional<std::size_t>(0));
    CrtRandom draws(1);
    static_cast<void>(draws.Next());
    const int drift = draws.Next();
    const int rise = draws.Next();
    EXPECT_EQ(random.State(), draws.State());
    const JointState& joint = joints[0];
    EXPECT_EQ(joint.ownerContext, &horse);
    EXPECT_EQ(joint.ownerKind, JointOwnerKind::Helper);
    EXPECT_EQ(joint.light, horse.light);
    EXPECT_EQ(joint.life, 50);
    EXPECT_EQ(joint.key, 2);
    EXPECT_EQ(joint.velocity, glm::vec3(0.0f, static_cast<float>(drift % 500 - 250),
                                        F32(static_cast<double>(rise % 250 + 200) * D(Float(0x3c23d70au)))));
    EXPECT_EQ(joint.targetPosition, request.second);

    // 0x530860 escribe el tipo de dueño en cada alta: el lugar vuelve a ByType con el personaje.
    joints[0].alive = false;
    ActorState actor;
    actor.light = glm::vec3(0.5f);
    ASSERT_TRUE(Create(joints, {0x60f, 0, 10.0f, -1, 0}, &actor, random).HasValue());
    EXPECT_EQ(joints[0].ownerKind, JointOwnerKind::ByType);
    EXPECT_EQ(joints[0].light, actor.light);
}

// 0x53b378..0x53b3a9 con el caballo de dueño: debajo de 40 de ancho la caída mira la acción del caballo (+0x111, la de
// DarkHorse), no la del personaje.
TEST(FullSetJointsTest, TheSparkleOfTheHorseFallsWithTheActionOfTheHorse)
{
    for (const std::uint8_t action : {std::uint8_t{0xc}, std::uint8_t{0xd}})
    {
        SCOPED_TRACE(static_cast<int>(action));
        HelperState horse;
        horse.light = glm::vec3(3.0f);
        std::array<JointState, 1> joints{};
        CrtRandom random(1);
        ASSERT_TRUE(Create(joints, {0x60f, 0, 10.0f, -1, 0}, &horse, random, JointOwnerKind::Helper).HasValue());
        JointState expected = joints[0];
        horse.horse.animation.action = action;

        ASSERT_TRUE(Client::Joints::StepJoints(joints, random).HasValue());

        TurnSparkle(expected);
        const JointState& joint = joints[0];
        EXPECT_EQ(joint.position, expected.position);
        EXPECT_EQ(joint.subtype, action == 0xd ? 1 : 0);
        EXPECT_EQ(joint.tailEnabled, action != 0xd);
        EXPECT_EQ(joint.life, action == 0xd ? 9 : 99);
    }
}

// MuRet sólo sabe leer un ayudante de dueño en el 60f/0 y el 60f/0x12: con otro joint corta antes de escribir el lugar
// y sin rand(); con el pool lleno no lee los argumentos.
TEST(FullSetJointsTest, OnlyTheSparklesTakeAHelperForOwner)
{
    HelperState horse;
    std::array<JointState, 1> joints{};
    CrtRandom random(1);

    const auto created = Create(joints, {0x610, 7, 10.0f, -1, 0}, &horse, random, JointOwnerKind::Helper);

    ASSERT_FALSE(created.HasValue());
    EXPECT_NE(created.Error().find("0x530860"), std::string::npos) << created.Error();
    EXPECT_FALSE(joints[0].alive);
    EXPECT_EQ(random.State(), CrtRandom(1).State());

    joints[0].alive = true;
    const auto full = Create(joints, {0x610, 7, 10.0f, -1, 0}, &horse, random, JointOwnerKind::Helper);
    ASSERT_TRUE(full.HasValue()) << full.Error();
    EXPECT_FALSE(full.Value().has_value());
}

// 0x53c393: un 60f/1 con la cola usa la matriz que dejó en [ebp−0xe8] el último joint de la vuelta que la calculó (el
// 60f/0 que cae la apaga, así que sólo se llega con un estado armado a mano). Sin ninguno antes, la de la pila: con
// dibujo corta (Q-C1623).
TEST(FullSetJointsTest, AFallenSparkleWithItsTailUsesThePreviousMatrix)
{
    std::array<JointState, 2> joints{};
    joints[1].alive = true;
    joints[1].type = 0x60f;
    joints[1].subtype = 1;
    joints[1].tailEnabled = true;
    joints[1].drawEnabled = 3;
    joints[1].width = 20.0f;
    joints[1].maximumCount = 5;
    joints[1].life = 30;
    joints[1].position = {10.0f, 20.0f, 30.0f};
    CrtRandom random(1);
    auto alone = joints;
    const auto stepped = Client::Joints::StepJoints(alone, random);
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x53c3a5"), std::string::npos) << stepped.Error();

    // El 0x613/0xa de antes calcula la matriz con sus ángulos de antes del caso (0, 0, 90).
    joints[0].alive = true;
    joints[0].type = 0x613;
    joints[0].subtype = 0xa;
    joints[0].angles = {0.0f, 0.0f, 90.0f};
    joints[0].life = 10;
    JointState expected = joints[1];
    ASSERT_TRUE(Client::Joints::JointKernels::AppendJointTail(
                    expected, Client::Math::OriginalAngleMatrix(glm::vec3{0.0f, 0.0f, 90.0f}))
                    .HasValue());
    ASSERT_TRUE(Client::Joints::StepJoints(joints, random).HasValue());
    EXPECT_EQ(joints[1].count, expected.count);
    EXPECT_EQ(joints[1].points[0], expected.points[0]);
    EXPECT_EQ(joints[1].position, (glm::vec3{10.0f, 20.0f, 30.0f}));
}

// 0x534cb6: los 60f/0x12 (0x5103be) restan (mezcla 4) y, con el ancho 20, viven 50 con un tercer rand() para vz:
// (rand() % 250 + 200) · 0,01f. Giran con la vida doble, el skill 1 al revés, suben vz · 1,1f y crecen la velocidad
// 0,1f y el ancho 0,2f por paso.
TEST(FullSetJointsTest, TheWideSparklesTurnBothWaysAndGrow)
{
    for (const std::int32_t skill : {0, 1})
    {
        SCOPED_TRACE(skill);
        ActorState actor;
        actor.light = glm::vec3(1.0f);
        std::array<JointState, 1> joints{};
        CrtRandom random(1);

        ASSERT_TRUE(Create(joints, {0x60f, 0x12, 20.0f, -1, skill}, &actor, random).HasValue());

        CrtRandom draws(1);
        const int rise = draws.Next();
        const int drift = draws.Next();
        const int wideRise = draws.Next();
        static_cast<void>(rise);
        EXPECT_EQ(random.State(), draws.State());
        JointState expected = joints[0];
        EXPECT_EQ(expected.blend, 4);
        EXPECT_EQ(expected.life, 50);
        EXPECT_EQ(expected.width, 20.0f);
        EXPECT_EQ(expected.skill, static_cast<std::uint8_t>(skill));
        EXPECT_EQ(expected.velocity, glm::vec3(0.0f, static_cast<float>(drift % 500 - 250),
                                               F32(static_cast<double>(wideRise % 250 + 200) * D(Float(0x3c23d70au)))));

        for (int step = 1; step <= 51; ++step)
        {
            SCOPED_TRACE(step);
            ASSERT_TRUE(Client::Joints::StepJoints(joints, random).HasValue());
            TurnSparkle(expected);
            expected.count = Appended(expected.count, expected.maximumCount);
            --expected.life;
            const JointState& joint = joints[0];
            EXPECT_EQ(joint.position, expected.position);
            EXPECT_EQ(joint.speed, expected.speed);
            EXPECT_EQ(joint.width, expected.width);
            EXPECT_EQ(joint.light, expected.light);
            EXPECT_EQ(joint.life, expected.life);
            EXPECT_EQ(joint.alive, step < 51);
        }
        EXPECT_EQ(random.State(), draws.State());
    }
}

// 0x53b378: con el ancho en 40 o más, la acción del dueño no lo hace caer.
TEST(FullSetJointsTest, AWideSparkleOfFortyDoesNotFall)
{
    ActorState actor;
    actor.animation.action = 0xd;
    std::array<JointState, 1> joints{};
    CrtRandom random(1);
    ASSERT_TRUE(Create(joints, {0x60f, 0x12, 40.0f, -1, 0}, &actor, random).HasValue());

    ASSERT_TRUE(Client::Joints::StepJoints(joints, random).HasValue());

    EXPECT_EQ(joints[0].subtype, 0x12);
    EXPECT_TRUE(joints[0].tailEnabled);
    EXPECT_EQ(joints[0].life, 49);
}

// 0x531c43..0x531ec7: el 610/7 de +13 (0x5104a8): la vida en la key (0x28), la luz en 1, vy y vz en 0, mezcla 4,
// velocidad 10, la textura 0x60f, el ancho, 16 puntos y vx 15. La x y la y se corren 40 desde el segundo punto con el
// coseno y el seno de rand() % 360 tomado en radianes (Q-C384); la partida (+0x24) es esa posición.
TEST(FullSetJointsTest, TheSetTrailIsBornFortyAwayFromItsSecondPoint)
{
    ActorState actor;
    actor.light = {0.25f, 0.5f, 0.75f};
    std::array<JointState, 1> joints{};
    CrtRandom random(1);
    const Request request{0x610, 7, 20.0f, 0x28, 1};

    const auto created = Create(joints, request, &actor, random);

    ASSERT_TRUE(created.HasValue()) << created.Error();
    CrtRandom draws(1);
    const int across = draws.Next() % 360;
    const int along = draws.Next() % 360;
    EXPECT_EQ(random.State(), draws.State());
    const JointState& joint = joints[0];
    const glm::vec3 at{F32(Cosine(static_cast<double>(across), 40.0f) + D(request.second.x)),
                       F32(D(request.second.y) - Sine(static_cast<double>(along), 40.0f)), request.first.z};
    EXPECT_EQ(joint.position, at);
    EXPECT_EQ(joint.pathIncrement, at);
    EXPECT_EQ(joint.targetPosition, request.second);
    EXPECT_EQ(joint.life, 0x28);
    EXPECT_EQ(joint.key, 0);
    EXPECT_EQ(joint.light, glm::vec3(1.0f));
    EXPECT_EQ(joint.velocity, glm::vec3(15.0f, 0.0f, 0.0f));
    EXPECT_EQ(joint.blend, 4);
    EXPECT_EQ(joint.speed, 10.0f);
    EXPECT_EQ(joint.texture, 0x60f);
    EXPECT_EQ(joint.width, 20.0f);
    EXPECT_EQ(joint.maximumCount, 16);
    EXPECT_EQ(joint.skill, 1);
}

// 0x531c7b: con el color 3 y sin dueño, lo común del 0x610 lee la luz de un nulo.
TEST(FullSetJointsTest, TheSetTrailOfColorThreeNeedsItsOwner)
{
    std::array<JointState, 1> joints{};
    CrtRandom random(1);

    const auto created = Create(joints, {0x610, 7, 20.0f, 0x28, 3}, nullptr, random);

    ASSERT_FALSE(created.HasValue());
    EXPECT_NE(created.Error().find("0x531c7b"), std::string::npos) << created.Error();
    EXPECT_EQ(random.State(), CrtRandom(1).State());
}

// 0x537c82..0x537d17: el 610/7 se mece desde su partida con sen(vida · 0,4f) · vx, sube la velocidad (que baja 0,1f,
// como vx) y, con la vida debajo de 15, la luz por 0,8333333f. Muere en el paso 41. Sin rand().
TEST(FullSetJointsTest, TheSetTrailSwaysUpwardsUntilItsLifeRunsOut)
{
    ActorState actor;
    std::array<JointState, 1> joints{};
    CrtRandom random(1);
    ASSERT_TRUE(Create(joints, {0x610, 7, 20.0f, 0x28, 1}, &actor, random).HasValue());
    JointState expected = joints[0];
    const std::uint32_t state = random.State();
    const double tenth = D(Float(0x3dcccccdu));

    for (int step = 1; step <= 41; ++step)
    {
        SCOPED_TRACE(step);
        ASSERT_TRUE(Client::Joints::StepJoints(joints, random).HasValue());
        const float argument = F32(static_cast<double>(expected.life) * D(Float(0x3ecccccdu)));
        expected.position.x = F32(Sine(D(argument), expected.velocity.x) + D(expected.pathIncrement.x));
        expected.position.y = expected.pathIncrement.y;
        expected.position.z = F32(D(expected.speed) + D(expected.position.z));
        expected.speed = F32(D(expected.speed) - tenth);
        expected.velocity.x = F32(D(expected.velocity.x) - tenth);
        if (expected.life < 15)
            expected.light = Faded(expected.light, 0x3f555555u);
        expected.count = Appended(expected.count, expected.maximumCount);
        --expected.life;
        const JointState& joint = joints[0];
        EXPECT_EQ(joint.position, expected.position);
        EXPECT_EQ(joint.speed, expected.speed);
        EXPECT_EQ(joint.velocity.x, expected.velocity.x);
        EXPECT_EQ(joint.light, expected.light);
        EXPECT_EQ(joint.count, expected.count);
        EXPECT_EQ(joint.alive, step < 41);
    }
    EXPECT_EQ(random.State(), state);
}

// 0x5323d3 y 0x53254f..0x5325c2: el 619/9 del 5ac/3: vida 90, 20 puntos, count 0, +0x9ec en ftol del giro z, vx 50,
// la luz (0,09090909f; 0,04545455f; 0,09090909f) y la posición y el destino en el personaje. Sin rand(); sin el
// personaje, corta (0x5325a2).
TEST(FullSetJointsTest, TheSetEnergyIsBornOnItsOwnerWithTheTurnOfTheFlare)
{
    ActorState actor;
    actor.position = {130.0f, 170.0f, 290.0f};
    std::array<JointState, 1> joints{};
    CrtRandom random(1);
    Request request{0x619, 9, 10.0f, -1, 0};
    request.angles = {0.0f, 0.0f, 213.3f};

    const auto created = Create(joints, request, &actor, random);

    ASSERT_TRUE(created.HasValue()) << created.Error();
    EXPECT_EQ(random.State(), CrtRandom(1).State());
    const JointState& joint = joints[0];
    EXPECT_EQ(joint.life, 90);
    EXPECT_EQ(joint.maximumCount, 20);
    EXPECT_EQ(joint.count, 0);
    EXPECT_EQ(joint.field9ec, 213);
    EXPECT_EQ(joint.velocity, glm::vec3(50.0f, 0.0f, 0.0f));
    EXPECT_EQ(joint.light, glm::vec3(Float(0x3dba2e8cu), Float(0x3d3a2e8cu), Float(0x3dba2e8cu)));
    EXPECT_EQ(joint.position, actor.position);
    EXPECT_EQ(joint.targetPosition, actor.position);
    EXPECT_EQ(joint.width, 10.0f);
    EXPECT_EQ(joint.speed, 0.0f);
    EXPECT_EQ(joint.ownerContext, &actor);

    std::array<JointState, 1> ownerless{};
    const auto cut = Create(ownerless, request, nullptr, random);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x5325a2"), std::string::npos) << cut.Error();
}

// 0x5387a9..0x5388e2: el 619/9 sigue al personaje en vivo: corre los tramos de count − 1 a 0 lo que se movió, gira a vx
// del destino con +0x9ec · 0,1f (+0x9ec sube 2), la z es el destino más 90 − vida más 10; vx baja 1 y en el paso 51,
// por debajo de 0, queda en 0 con la vida en 0. La luz por 1,25 por encima de 80 y por 0,9090909f debajo de 50.
TEST(FullSetJointsTest, TheSetEnergyCirclesItsMovingOwnerUntilItsRadiusRunsOut)
{
    ActorState actor;
    actor.position = {130.0f, 170.0f, 290.0f};
    std::array<JointState, 1> joints{};
    CrtRandom random(1);
    Request request{0x619, 9, 10.0f, -1, 0};
    request.angles = {0.0f, 0.0f, 213.3f};
    ASSERT_TRUE(Create(joints, request, &actor, random).HasValue());
    JointState expected = joints[0];
    const double tenth = D(Float(0x3dcccccdu));

    for (int step = 1; step <= 51; ++step)
    {
        SCOPED_TRACE(step);
        if (step == 12 || step == 30)
            actor.position = {F32(D(actor.position.x) + 3.5), F32(D(actor.position.y) - 2.25),
                              F32(D(actor.position.z) + 1.0)};
        const JointState before = joints[0];
        ASSERT_TRUE(Client::Joints::StepJoints(joints, random).HasValue());

        glm::vec3 delta{};
        for (int axis = 0; axis < 3; ++axis)
            delta[axis] = F32(D(expected.targetPosition[axis]) - D(actor.position[axis]));
        expected.targetPosition = actor.position;
        const double argument = static_cast<double>(expected.field9ec) * tenth;
        expected.field9ec += 2;
        const double sine = expected.velocity.x > 0 ? Sine(argument, expected.velocity.x) : 0.0;
        const double cosine = expected.velocity.x > 0 ? Cosine(argument, expected.velocity.x) : 0.0;
        expected.position.x = F32(sine + D(expected.targetPosition.x));
        expected.position.y = F32(cosine + D(expected.targetPosition.y));
        expected.position.z = F32((static_cast<double>(90 - expected.life) + D(expected.targetPosition.z)) + 10.0);
        const double reduced = D(expected.velocity.x) - 1.0;
        expected.velocity.x = F32(reduced);
        if (!(reduced >= 0))
        {
            expected.velocity.x = 0.0f;
            expected.life = 0;
        }
        if (expected.life < 50)
            expected.light = Faded(expected.light, 0x3f68ba2eu);
        else if (expected.life > 80)
            expected.light = Faded(expected.light, 0x3fa00000u);
        expected.count = Appended(expected.count, expected.maximumCount);
        --expected.life;

        const JointState& joint = joints[0];
        EXPECT_EQ(joint.targetPosition, expected.targetPosition);
        EXPECT_EQ(joint.field9ec, expected.field9ec);
        EXPECT_EQ(joint.position, expected.position);
        EXPECT_EQ(joint.velocity.x, expected.velocity.x);
        EXPECT_EQ(joint.light, expected.light);
        EXPECT_EQ(joint.count, expected.count);
        EXPECT_EQ(joint.life, expected.life);
        EXPECT_EQ(joint.alive, step < 51);
        // Los tramos de count − 1 a 0 corridos y, con la cola, un lugar más atrás; el último no se corre.
        for (std::int32_t segment = 0; segment < joint.count; ++segment)
        {
            SCOPED_TRACE(segment);
            glm::vec3 corner = before.points[static_cast<std::size_t>(segment)][2];
            if (segment < before.count)
                for (int axis = 0; axis < 3; ++axis)
                    corner[axis] = F32(D(corner[axis]) - D(delta[axis]));
            EXPECT_EQ(joint.points[static_cast<std::size_t>(segment) + 1][2], corner);
        }
    }
    EXPECT_EQ(random.State(), CrtRandom(1).State());
}

// 0x5325c7..0x532659: el 619/0xa de la 0x35: como el /9, con vida 80, vx 80 y +0x9d4 en 1 sólo con el giro 225 o 405.
TEST(FullSetJointsTest, TheSkillEnergyTurnsForwardOnlyFromTwoAngles)
{
    for (const float turn : {45.0f, 405.0f, 225.0f, 450.0f})
    {
        SCOPED_TRACE(turn);
        ActorState actor;
        actor.position = {130.0f, 170.0f, 290.0f};
        std::array<JointState, 1> joints{};
        CrtRandom random(1);
        Request request{0x619, 0xa, 15.0f, -1, 0};
        request.angles = {-45.0f, 0.0f, turn};

        const auto created = Create(joints, request, &actor, random);

        ASSERT_TRUE(created.HasValue()) << created.Error();
        EXPECT_EQ(random.State(), CrtRandom(1).State());
        const JointState& joint = joints[0];
        EXPECT_EQ(joint.life, 80);
        EXPECT_EQ(joint.maximumCount, 20);
        EXPECT_EQ(joint.count, 0);
        EXPECT_EQ(joint.field9ec, static_cast<std::int32_t>(turn));
        EXPECT_EQ(joint.field9d4, turn == 225.0f || turn == 405.0f ? 1 : 0);
        EXPECT_EQ(joint.velocity, glm::vec3(80.0f, 0.0f, 0.0f));
        EXPECT_EQ(joint.light, glm::vec3(Float(0x3dba2e8cu), Float(0x3d3a2e8cu), Float(0x3dba2e8cu)));
        EXPECT_EQ(joint.position, actor.position);
        EXPECT_EQ(joint.targetPosition, actor.position);
        EXPECT_EQ(joint.width, 15.0f);
    }
}

// 0x5388e7..0x538a4d: el 619/0xa sigue al dueño como el /9, con la z en el destino más vida · 0,0125f · 300, +0x9ec
// que sube 2 con +0x9d4 y si no baja 2, y la luz por 1,2f por encima de 68 y por 0,9090909f debajo de 40. En el paso
// 81, vx por debajo de 0, queda en 0 con la vida en 0. Los valores, por sus bits, salen de un cálculo aparte del ASM
// (FSIN/FCOS a 64 bits, PC53 y fstp a float) en los pasos 1 y 12 (la luz sube), 20 (el dueño se mueve 3,5; −2,25 y 1
// antes del paso), 42 (la luz baja), 80 (vx en 0) y 81 (la vida en 0 y muerto).
TEST(FullSetJointsTest, TheSkillEnergyCirclesItsOwnerAndClimbsWithItsLife)
{
    struct Checkpoint
    {
        float turn;
        int step;
        std::array<std::uint32_t, 3> position;
        std::uint32_t radius;
        std::array<std::uint32_t, 3> light;
        std::int32_t field9ec;
        std::int32_t life;
    };
    constexpr std::array<Checkpoint, 12> Checkpoints{{
        {45, 1, {0x424f30bbu, 0x431922e7u, 0x44138000u}, 0x429e0000u, {0x3ddf6b0fu, 0x3d5f6b0fu, 0x3ddf6b0fu}, 43, 79},
        {45, 12, {0x43357423u, 0x42f80dcdu, 0x44093000u}, 0x42880000u, {0x3f4f8087u, 0x3ecf8087u, 0x3f4f8087u}, 21, 68},
        {45, 20, {0x432ccc1bu, 0x435667c7u, 0x4401f000u}, 0x42700000u, {0x3f4f8087u, 0x3ecf8087u, 0x3f4f8087u}, 5, 60},
        {45,
         42,
         {0x431a29e2u, 0x4306ac92u, 0x43daa000u},
         0x42180000u,
         {0x3f3ca363u, 0x3ebca363u, 0x3f3ca363u},
         -39,
         38},
        {45, 80, {0x4306743bu, 0x43280cbcu, 0x43936000u}, 0, {0x3ca161fdu, 0x3c2161fdu, 0x3ca161fdu}, -115, 0},
        {45, 81, {0x43058000u, 0x4327c000u, 0x43918000u}, 0, {0x3c92b62bu, 0x3c12b62bu, 0x3c92b62bu}, -117, -1},
        {225,
         1,
         {0x42b60d52u, 0x42c84573u, 0x44138000u},
         0x429e0000u,
         {0x3ddf6b0fu, 0x3d5f6b0fu, 0x3ddf6b0fu},
         227,
         79},
        {225,
         12,
         {0x42ca20d4u, 0x4368a3bau, 0x44093000u},
         0x42880000u,
         {0x3f4f8087u, 0x3ecf8087u, 0x3f4f8087u},
         249,
         68},
        {225,
         20,
         {0x433d99b0u, 0x433fb3ffu, 0x4401f000u},
         0x42700000u,
         {0x3f4f8087u, 0x3ecf8087u, 0x3f4f8087u},
         265,
         60},
        {225,
         42,
         {0x42d7ceadu, 0x43452cc9u, 0x43daa000u},
         0x42180000u,
         {0x3f3ca363u, 0x3ebca363u, 0x3f3ca363u},
         309,
         38},
        {225, 80, {0x430610bcu, 0x43289328u, 0x43936000u}, 0, {0x3ca161fdu, 0x3c2161fdu, 0x3ca161fdu}, 385, 0},
        {225, 81, {0x43058000u, 0x4327c000u, 0x43918000u}, 0, {0x3c92b62bu, 0x3c12b62bu, 0x3c92b62bu}, 387, -1},
    }};
    const auto vector = [](const std::array<std::uint32_t, 3>& value)
    { return glm::vec3(Float(value[0]), Float(value[1]), Float(value[2])); };
    for (const float turn : {45.0f, 225.0f})
    {
        SCOPED_TRACE(turn);
        ActorState actor;
        actor.position = {130.0f, 170.0f, 290.0f};
        std::array<JointState, 1> joints{};
        CrtRandom random(1);
        Request request{0x619, 0xa, 15.0f, -1, 0};
        request.angles = {-45.0f, 0.0f, turn};
        ASSERT_TRUE(Create(joints, request, &actor, random).HasValue());

        for (int step = 1; step <= 81; ++step)
        {
            SCOPED_TRACE(step);
            if (step == 20)
                actor.position = {133.5f, 167.75f, 291.0f};
            ASSERT_TRUE(Client::Joints::StepJoints(joints, random).HasValue());

            const JointState& joint = joints[0];
            EXPECT_EQ(joint.targetPosition, actor.position);
            EXPECT_EQ(joint.alive, step < 81);
            for (const Checkpoint& checkpoint : Checkpoints)
            {
                if (checkpoint.turn != turn || checkpoint.step != step)
                    continue;
                EXPECT_EQ(joint.position, vector(checkpoint.position));
                EXPECT_EQ(joint.velocity.x, Float(checkpoint.radius));
                EXPECT_EQ(joint.light, vector(checkpoint.light));
                EXPECT_EQ(joint.field9ec, checkpoint.field9ec);
                EXPECT_EQ(joint.life, checkpoint.life);
            }
        }
        EXPECT_EQ(random.State(), CrtRandom(1).State());
    }
}

// El 619/1 del calco de la mascota y el 619/3 del calco de las invocaciones (0x518900), con el héroe de dueño.
namespace
{
// 4e9810 con el destino: el yaw y el pitch giran de a `turn` (lo que hace SteerJoint en el paso).
void Steer(JointState& expected, const glm::vec3& target, float turn)
{
    using Client::Math::CreateAngle;
    using Client::Math::TurnAngle;
    const auto yaw = F32(CreateAngle(expected.position.x, expected.position.y, target.x, target.y));
    expected.angles.z = F32(TurnAngle(expected.angles.z, yaw, turn));
    const double x = D(expected.position.x) - target.x;
    const double y = D(expected.position.y) - target.y;
    const auto horizontal = F32(std::sqrt(D(F32(x * x)) + D(F32(y * y))));
    const auto pitch = F32(360.0 - CreateAngle(expected.position.z, horizontal, target.z, 0));
    expected.angles.x = F32(TurnAngle(expected.angles.x, pitch, turn));
}
} // namespace

// 0x5323d3..0x5323f8 y 0x5325ca: el 619/1 y el 619/3 sólo tienen lo común del 0x619: velocidad 0, vida 12, el ancho y 2
// puntos. Sin rand(); la posición es el primer punto y el destino no se escribe, porque tiene dueño.
TEST(FullSetJointsTest, TheSummonEnergyIsBornWithTheCommonFieldsOfThe619)
{
    for (const std::int32_t subtype : {1, 3})
    {
        SCOPED_TRACE(subtype);
        ActorState hero;
        hero.position = {1300.0f, 1700.0f, 290.0f};
        std::array<JointState, 1> joints{};
        joints[0].targetPosition = {1.0f, 2.0f, 3.0f};
        CrtRandom random(1);
        Request request{0x619, subtype, 5.0f, -1, 0};
        request.angles = {40.0f, 0.0f, 213.0f};

        const auto created = Create(joints, request, &hero, random);

        ASSERT_TRUE(created.HasValue()) << created.Error();
        EXPECT_EQ(random.State(), CrtRandom(1).State());
        const JointState& joint = joints[0];
        EXPECT_TRUE(joint.alive);
        EXPECT_EQ(joint.life, 12);
        EXPECT_EQ(joint.maximumCount, 2);
        EXPECT_EQ(joint.count, 0);
        EXPECT_EQ(joint.speed, 0.0f);
        EXPECT_EQ(joint.width, 5.0f);
        EXPECT_EQ(joint.texture, 0x619);
        EXPECT_EQ(joint.light, glm::vec3(1.0f));
        EXPECT_EQ(joint.position, request.first);
        EXPECT_EQ(joint.angles, request.angles);
        EXPECT_EQ(joint.targetPosition, glm::vec3(1.0f, 2.0f, 3.0f));
        EXPECT_EQ(joint.ownerContext, &hero);
    }
}

namespace
{
// 0x538a52..0x538d28 con el 619/1 o el 619/3 (lo comprueba el test de abajo).
void ClimbTowardsTheOwner(std::int32_t subtype)
{
    ActorState hero;
    hero.position = {1300.0f, 1700.0f, 290.0f};
    std::array<JointState, 1> joints{};
    CrtRandom random(1);
    Request request{0x619, subtype, 5.0f, -1, 0};
    request.first = {1450.0f, 1600.0f, 380.0f};
    request.angles = {40.0f, 0.0f, 213.0f};
    ASSERT_TRUE(Create(joints, request, &hero, random).HasValue());
    JointState expected = joints[0];
    Client::Particles::FrameSprites sprites;
    CrtRandom draws(1);
    const double tenth = D(Float(0x3dcccccdu));

    for (int step = 1; step <= 13; ++step)
    {
        SCOPED_TRACE(step);
        if (step == 6)
            hero.position = {1310.0f, 1690.0f, 300.0f};
        ASSERT_TRUE(Client::Joints::StepJoints(joints, random, nullptr, nullptr, 0, &sprites).HasValue());

        const auto matrix = Client::Math::OriginalAngleMatrix(expected.angles);
        const glm::vec3 moved = Client::Math::OriginalVectorRotate({0.0f, -expected.speed, 0.0f}, matrix);
        if (expected.speed > 0)
            for (int axis = 0; axis < 3; ++axis)
                expected.position[axis] = F32(D(expected.position[axis]) + D(moved[axis]));
        expected.speed = F32(4.0 + D(expected.speed));
        const glm::vec3 target{hero.position.x, hero.position.y, F32(D(hero.position.z) + 120.0)};
        Steer(expected, target, 10.0f);
        const double brightness = static_cast<double>(12 - expected.life) * tenth;
        if (subtype == 3)
            expected.light = {F32(brightness), F32(brightness * D(Float(0x3f19999au))),
                              F32(brightness * D(Float(0x3ecccccdu)))};
        else
            expected.light = {F32(brightness * D(Float(0x3ecccccdu))), F32(brightness * D(Float(0x3f19999au))),
                              F32(brightness)};
        if (expected.life == 1)
        {
            const auto rotation = static_cast<float>(draws.Next() % 360);
            const auto scale = F32(static_cast<double>((draws.Next() & 7) + 8) * D(Float(0x3e4ccccdu)));
            const auto shown = std::ranges::find_if(sprites.Sprites(), [](const auto& sprite) { return sprite.alive; });
            ASSERT_NE(shown, sprites.Sprites().end());
            EXPECT_EQ(shown->texture, 0x5fd);
            EXPECT_EQ(shown->position, target);
            EXPECT_EQ(shown->scale, scale);
            EXPECT_EQ(shown->light, expected.light);
            EXPECT_EQ(shown->rotation, rotation);
        }
        // El sprite de la última vida queda en la tabla del frame: ninguno antes y ése solo después.
        EXPECT_EQ(std::ranges::count_if(sprites.Sprites(), [](const auto& sprite) { return sprite.alive; }),
                  step >= 12 ? 1 : 0);
        expected.count = Appended(expected.count, expected.maximumCount);
        --expected.life;

        const JointState& joint = joints[0];
        EXPECT_EQ(joint.speed, expected.speed);
        EXPECT_EQ(joint.position, expected.position);
        EXPECT_EQ(joint.angles, expected.angles);
        EXPECT_EQ(joint.light, expected.light);
        EXPECT_EQ(joint.count, expected.count);
        EXPECT_EQ(joint.life, expected.life);
        EXPECT_EQ(joint.alive, step < 13);
        EXPECT_EQ(random.State(), draws.State());
    }
}

} // namespace

// 0x538a52..0x538d28: después del avance común con la velocidad de antes, sube 4; gira de a 10 hacia el héroe 120 más
// arriba, leído en vivo; la luz es (12 − vida) · 0,1f por (0,4f; 0,6f; 1) la /1 y por (1; 0,6f; 0,4f) la /3
// (0x538bc8) y, con la vida en 1, el sprite 0x5fd en ese destino con el giro rand() % 360 y la escala
// ((rand() & 7) + 8) · 0,2f. La cola común con la matriz del principio del paso; muere en el paso 13.
TEST(FullSetJointsTest, TheSummonEnergyClimbsTowardsItsOwnerAndShinesOnItsLastLife)
{
    for (const std::int32_t subtype : {1, 3})
    {
        SCOPED_TRACE(subtype);
        ClimbTowardsTheOwner(subtype);
    }
}

// 0x538b17: sin el héroe no tiene a quién seguir; 0x538cce: en la última vida, sin el RNG o los sprites corta antes de
// tocar nada.
TEST(FullSetJointsTest, TheSummonEnergyNeedsItsOwnerAndItsLastSprite)
{
    std::array<JointState, 1> joints{};
    CrtRandom random(1);
    Request request{0x619, 1, 5.0f, -1, 0};
    ASSERT_TRUE(Create(joints, request, nullptr, random).HasValue());
    const auto ownerless = Client::Joints::StepJoints(joints, random);
    ASSERT_FALSE(ownerless.HasValue());
    EXPECT_NE(ownerless.Error().find("0x538b17"), std::string::npos) << ownerless.Error();

    ActorState hero;
    std::array<JointState, 1> last{};
    ASSERT_TRUE(Create(last, request, &hero, random).HasValue());
    last[0].life = 1;
    const JointState before = last[0];
    const auto blind = Client::Joints::StepJoints(last, random);
    ASSERT_FALSE(blind.HasValue());
    EXPECT_NE(blind.Error().find("0x538cce"), std::string::npos) << blind.Error();
    EXPECT_EQ(last[0].speed, before.speed);
    EXPECT_EQ(last[0].light, before.light);
    EXPECT_EQ(last[0].life, 1);
}

// El 619/6 de los rayos del 0x813 (0x52cb85), sin dueño. 0x5323d3..0x532415: lo común del 0x619 y la luz (1; 1; 0,5)
// con 4 puntos; el destino es el segundo punto. Sin rand().
TEST(FullSetJointsTest, TheBoltEnergyIsBornWithAPaleLight)
{
    std::array<JointState, 1> joints{};
    CrtRandom random(1);
    Request request{0x619, 6, 5.0f, -1, 0};
    request.angles = {40.0f, 0.0f, 213.0f};

    const auto created = Create(joints, request, nullptr, random);

    ASSERT_TRUE(created.HasValue()) << created.Error();
    EXPECT_EQ(random.State(), CrtRandom(1).State());
    const JointState& joint = joints[0];
    EXPECT_EQ(joint.life, 12);
    EXPECT_EQ(joint.maximumCount, 4);
    EXPECT_EQ(joint.speed, 0.0f);
    EXPECT_EQ(joint.width, 5.0f);
    EXPECT_EQ(joint.light, glm::vec3(1.0f, 1.0f, 0.5f));
    EXPECT_EQ(joint.position, request.first);
    EXPECT_EQ(joint.targetPosition, request.second);
    EXPECT_EQ(joint.ownerContext, nullptr);
}

// 0x538a52..0x538a70 y 0x538c25..0x538ca9: después del avance común con la velocidad de antes, sube 2, sin girar ni
// cambiar la luz. Con 10 de vida o menos, el sprite 0x5fd en el destino con la luz gris (6 − |vida − 6|) · 0,15f, el
// giro rand() % 360 y la escala ((rand() & 7) + 8) · 0,05f: once sprites, del paso 3 al 13, en que muere.
TEST(FullSetJointsTest, TheBoltEnergyGlidesAndSparksInItsLastLives)
{
    std::array<JointState, 1> joints{};
    CrtRandom random(1);
    Request request{0x619, 6, 5.0f, -1, 0};
    request.angles = {40.0f, 0.0f, 213.0f};
    ASSERT_TRUE(Create(joints, request, nullptr, random).HasValue());
    JointState expected = joints[0];
    Client::Particles::FrameSprites sprites;
    CrtRandom draws(1);
    std::size_t made = 0;

    for (int step = 1; step <= 13; ++step)
    {
        SCOPED_TRACE(step);
        ASSERT_TRUE(Client::Joints::StepJoints(joints, random, nullptr, nullptr, 0, &sprites).HasValue());

        const auto matrix = Client::Math::OriginalAngleMatrix(expected.angles);
        const glm::vec3 moved = Client::Math::OriginalVectorRotate({0.0f, -expected.speed, 0.0f}, matrix);
        if (expected.speed > 0)
            for (int axis = 0; axis < 3; ++axis)
                expected.position[axis] = F32(D(expected.position[axis]) + D(moved[axis]));
        expected.speed = F32(2.0 + D(expected.speed));
        if (expected.life <= 10)
        {
            const auto rotation = static_cast<float>(draws.Next() % 360);
            const auto scale = F32(static_cast<double>((draws.Next() & 7) + 8) * D(Float(0x3d4ccccdu)));
            const int distance = expected.life < 6 ? 6 - expected.life : expected.life - 6;
            const auto glow = F32(static_cast<double>(6 - distance) * D(Float(0x3e19999au)));
            ASSERT_LT(made, sprites.Sprites().size());
            const auto& shown = sprites.Sprites()[made++];
            EXPECT_TRUE(shown.alive);
            EXPECT_EQ(shown.texture, 0x5fd);
            EXPECT_EQ(shown.position, request.second);
            EXPECT_EQ(shown.scale, scale);
            EXPECT_EQ(shown.light, glm::vec3(glow));
            EXPECT_EQ(shown.rotation, rotation);
        }
        EXPECT_EQ(std::ranges::count_if(sprites.Sprites(), [](const auto& sprite) { return sprite.alive; }),
                  static_cast<std::ptrdiff_t>(made));
        expected.count = Appended(expected.count, expected.maximumCount);
        --expected.life;

        const JointState& joint = joints[0];
        EXPECT_EQ(joint.speed, expected.speed);
        EXPECT_EQ(joint.position, expected.position);
        EXPECT_EQ(joint.angles, request.angles);
        EXPECT_EQ(joint.light, glm::vec3(1.0f, 1.0f, 0.5f));
        EXPECT_EQ(joint.count, expected.count);
        EXPECT_EQ(joint.life, expected.life);
        EXPECT_EQ(joint.alive, step < 13);
        EXPECT_EQ(random.State(), draws.State());
    }
    EXPECT_EQ(made, 11u);
}

// 0x538c5d: con 10 de vida o menos, sin el RNG o los sprites corta; antes, no los pide.
TEST(FullSetJointsTest, TheBoltEnergyNeedsTheSpritesOnlyForItsSparks)
{
    std::array<JointState, 1> joints{};
    CrtRandom random(1);
    Request request{0x619, 6, 5.0f, -1, 0};
    ASSERT_TRUE(Create(joints, request, nullptr, random).HasValue());
    ASSERT_TRUE(Client::Joints::StepJoints(joints, random).HasValue());
    ASSERT_TRUE(Client::Joints::StepJoints(joints, random).HasValue());
    EXPECT_EQ(joints[0].life, 10);
    const auto blind = Client::Joints::StepJoints(joints, random);
    ASSERT_FALSE(blind.HasValue());
    EXPECT_NE(blind.Error().find("0x538c5d"), std::string::npos) << blind.Error();
}

// El 619/8 de la skill cargada del mago (0x5189f0), con el personaje de dueño. Los esperados son literales, calculados
// aparte del port (Fraction y redondeos de la x87; el primer rand() de la semilla 1 es 41).
namespace
{
constexpr std::int32_t ChargedEnergy = 8;
} // namespace

// 0x5323d3..0x5323f8 y 0x5324ea..0x53254a: lo común del 0x619 (velocidad 0, el ancho) y el caso 8: vida 0x11, 3 puntos,
// la luz (0,5; 0,5; (41 & 0x7f) · 1/255f + 0,5) y el destino en el segundo punto con z + 100, aunque tenga dueño. Un
// rand(); la posición y los ángulos, los del pedido.
TEST(FullSetJointsTest, TheChargedEnergyIsBornWithAHalfLightAndOneRandom)
{
    ActorState hero;
    std::array<JointState, 1> joints{};
    CrtRandom random(1);
    Request request{0x619, ChargedEnergy, 10.0f, -1, 0};
    request.angles = {40.0f, 0.0f, 213.0f};

    const auto created = Create(joints, request, &hero, random);

    ASSERT_TRUE(created.HasValue()) << created.Error();
    EXPECT_EQ(created.Value(), std::optional<std::size_t>(0));
    CrtRandom once(1);
    static_cast<void>(once.Next());
    EXPECT_EQ(random.State(), once.State());
    const JointState& joint = joints[0];
    EXPECT_TRUE(joint.alive);
    EXPECT_EQ(joint.type, 0x619);
    EXPECT_EQ(joint.subtype, ChargedEnergy);
    EXPECT_EQ(joint.texture, 0x619);
    EXPECT_EQ(joint.life, 17);
    EXPECT_EQ(joint.maximumCount, 3);
    EXPECT_EQ(joint.count, 0);
    EXPECT_EQ(joint.speed, 0.0f);
    EXPECT_EQ(joint.width, 10.0f);
    EXPECT_EQ(joint.light, glm::vec3(0.5f, 0.5f, Float(0x3f292929u)));
    EXPECT_EQ(joint.position, glm::vec3(100.0f, 200.0f, 300.0f));
    EXPECT_EQ(joint.angles, glm::vec3(40.0f, 0.0f, 213.0f));
    EXPECT_EQ(joint.targetPosition, glm::vec3(110.0f, 190.0f, 405.0f));
    EXPECT_EQ(joint.ownerContext, &hero);
}

// 0x530860..0x53088a: con la tabla llena sale antes del caso, sin el rand() de la luz.
TEST(FullSetJointsTest, TheChargedEnergyIsNotBornInAFullPoolNorDrawsItsRandom)
{
    ActorState hero;
    std::array<JointState, 2> joints{};
    for (JointState& joint : joints)
        joint.alive = true;
    CrtRandom random(1);

    const auto created = Create(joints, Request{0x619, ChargedEnergy, 10.0f, -1, 0}, &hero, random);

    ASSERT_TRUE(created.HasValue()) << created.Error();
    EXPECT_FALSE(created.Value().has_value());
    EXPECT_EQ(random.State(), CrtRandom(1).State());
}

// 0x538a52..0x538a70, 0x538a94..0x538b15 y 0x53c393, con 17 de vida y la velocidad en 0 (sin avance común): la
// velocidad en 4; gira de a 10 hacia el destino (300, 200, 400) desde (100, 200, 400): el rumbo 90 deja la z en 10 y la
// inclinación 360 (0x4e93f0 sin x da 0) deja la x en 360; el sprite 0x5ac en la posición con la escala 1, la luz, el
// giro 0 y la mezcla 0. La luz no cambia (0x53c628 lleva la /8 a 0x538c23). Sin rand(); la cola común: un punto y 16.
TEST(FullSetJointsTest, TheChargedEnergySteersTowardsItsTargetAndLeavesAFlare)
{
    ActorState hero;
    std::array<JointState, 1> joints{};
    CrtRandom random(1);
    Request request{0x619, ChargedEnergy, 10.0f, -1, 0};
    request.first = {100.0f, 200.0f, 400.0f};
    request.second = {300.0f, 200.0f, 300.0f};
    ASSERT_TRUE(Create(joints, request, &hero, random).HasValue());
    const auto afterBirth = random.State();
    Client::Particles::FrameSprites sprites;

    const auto stepped = Client::Joints::StepJoints(joints, random, nullptr, nullptr, 0, &sprites);

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(random.State(), afterBirth);
    const JointState& joint = joints[0];
    const glm::vec3 light{0.5f, 0.5f, Float(0x3f292929u)};
    EXPECT_EQ(joint.speed, 4.0f);
    EXPECT_EQ(joint.position, glm::vec3(100.0f, 200.0f, 400.0f));
    EXPECT_EQ(joint.angles, glm::vec3(360.0f, 0.0f, 10.0f));
    EXPECT_EQ(joint.light, light);
    EXPECT_EQ(joint.targetPosition, glm::vec3(300.0f, 200.0f, 400.0f));
    EXPECT_EQ(joint.count, 1);
    EXPECT_EQ(joint.life, 16);
    EXPECT_TRUE(joint.alive);
    ASSERT_EQ(std::ranges::count_if(sprites.Sprites(), [](const auto& sprite) { return sprite.alive; }), 1);
    const auto& flare = sprites.Sprites()[0];
    EXPECT_EQ(flare.texture, 0x5ac);
    EXPECT_EQ(flare.position, glm::vec3(100.0f, 200.0f, 400.0f));
    EXPECT_EQ(flare.scale, 1.0f);
    EXPECT_EQ(flare.light, light);
    EXPECT_EQ(flare.rotation, 0.0f);
    EXPECT_EQ(flare.blend, 0);
}

// 0x538aad..0x538af5: con 10 de vida o menos no gira; con menos de 6, cada canal de la luz por 0,5555556f (0x6ba234):
// 0,5 da 0x3e8e38e4 y 0x3f292929 da 0x3ebbf4d9. El sprite 0x5ac sale igual, con la luz que quedó.
TEST(FullSetJointsTest, TheChargedEnergyStopsTurningAndFadesBelowSixLives)
{
    struct Case
    {
        std::int32_t life;
        glm::vec3 light;
    };
    const glm::vec3 born{0.5f, 0.5f, Float(0x3f292929u)};
    for (const Case& probe :
         {Case{10, born}, Case{6, born}, Case{5, {Float(0x3e8e38e4u), Float(0x3e8e38e4u), Float(0x3ebbf4d9u)}}})
    {
        SCOPED_TRACE(probe.life);
        ActorState hero;
        std::array<JointState, 1> joints{};
        CrtRandom random(1);
        Request request{0x619, ChargedEnergy, 10.0f, -1, 0};
        request.first = {100.0f, 200.0f, 400.0f};
        request.second = {300.0f, 200.0f, 300.0f};
        request.angles = {20.0f, 0.0f, 30.0f};
        ASSERT_TRUE(Create(joints, request, &hero, random).HasValue());
        joints[0].life = probe.life;
        Client::Particles::FrameSprites sprites;

        ASSERT_TRUE(Client::Joints::StepJoints(joints, random, nullptr, nullptr, 0, &sprites).HasValue());

        EXPECT_EQ(joints[0].angles, request.angles);
        EXPECT_EQ(joints[0].light, probe.light);
        EXPECT_EQ(joints[0].life, probe.life - 1);
        EXPECT_EQ(sprites.Sprites()[0].light, probe.light);
        EXPECT_TRUE(sprites.Sprites()[0].alive);
    }
}

// 0x538b0d: el sprite de cada paso pide la tabla de sprites del frame; sin ella corta antes de tocar el joint.
TEST(FullSetJointsTest, TheChargedEnergyNeedsTheFrameSprites)
{
    ActorState hero;
    std::array<JointState, 1> joints{};
    CrtRandom random(1);
    ASSERT_TRUE(Create(joints, Request{0x619, ChargedEnergy, 10.0f, -1, 0}, &hero, random).HasValue());

    const auto blind = Client::Joints::StepJoints(joints, random);

    ASSERT_FALSE(blind.HasValue());
    EXPECT_NE(blind.Error().find("0x538b0d"), std::string::npos) << blind.Error();
    EXPECT_EQ(joints[0].speed, 0.0f);
    EXPECT_EQ(joints[0].life, 17);
}
