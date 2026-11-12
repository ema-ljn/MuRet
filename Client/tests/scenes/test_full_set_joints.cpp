#include "Scenes/FaunaJointCreation.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalAngles.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Render/Models/OriginalRotation.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameDrawTail.h"
#include "World/IFishStepServices.h"
#include "World/Sprites.h"

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
using Client::Render::Models::CrtRandom;
using Client::Render::Models::Detail::CosineProductPC53;
using Client::Render::Models::Detail::SineProductPC53;
using Client::Scenes::ActorState;
using Client::Scenes::JointState;
using Client::World::FishEffectRequest;

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

Client::Core::Expected<std::optional<std::size_t>, std::string> Create(std::span<JointState> joints, Request request,
                                                                       void* owner, CrtRandom& random)
{
    const FishEffectRequest made{request.type,
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
    return Client::Scenes::CreateFaunaJoint(joints, made, random);
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
        ASSERT_TRUE(Client::Scenes::StepJoints(joints, random).HasValue());
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
            ASSERT_TRUE(Client::Scenes::StepJoints(joints, random).HasValue());
            TurnSparkle(expected);
            expected.count = Appended(expected.count, expected.maximumCount);
            --expected.life;
        }

        actor.animation.action = probe.action;
        ASSERT_TRUE(Client::Scenes::StepJoints(joints, random).HasValue());
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
            ASSERT_TRUE(Client::Scenes::StepJoints(joints, random).HasValue());
            light = Faded(light, 0x3f44ec4fu);
            EXPECT_EQ(joint.position, expected.position);
            EXPECT_EQ(joint.light, light);
            EXPECT_EQ(joint.count, expected.count);
            EXPECT_EQ(joint.life, 9 - step);
            EXPECT_EQ(joint.alive, step < 10);
        }
    }
}

// 0x53c393: un 60f/1 con la cola usaría la matriz que dejó el joint anterior; el 60f/0 que cae la apaga, así que sólo
// se llega con un estado armado a mano.
TEST(FullSetJointsTest, AFallenSparkleWithItsTailIsNotPorted)
{
    std::array<JointState, 1> joints{};
    joints[0].alive = true;
    joints[0].type = 0x60f;
    joints[0].subtype = 1;
    joints[0].tailEnabled = true;
    CrtRandom random(1);

    const auto stepped = Client::Scenes::StepJoints(joints, random);

    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x53c393"), std::string::npos) << stepped.Error();
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
            ASSERT_TRUE(Client::Scenes::StepJoints(joints, random).HasValue());
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

    ASSERT_TRUE(Client::Scenes::StepJoints(joints, random).HasValue());

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
        ASSERT_TRUE(Client::Scenes::StepJoints(joints, random).HasValue());
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
        ASSERT_TRUE(Client::Scenes::StepJoints(joints, random).HasValue());

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

// El 619/1 del calco de la mascota (0x518900), con el héroe de dueño.
namespace
{
// 4e9810 con el destino: el yaw y el pitch giran de a `turn` (lo que hace SteerJoint en el paso).
void Steer(JointState& expected, const glm::vec3& target, float turn)
{
    using Client::Render::Models::CreateAngle;
    using Client::Render::Models::TurnAngle;
    const auto yaw = F32(CreateAngle(expected.position.x, expected.position.y, target.x, target.y));
    expected.angles.z = F32(TurnAngle(expected.angles.z, yaw, turn));
    const double x = D(expected.position.x) - target.x;
    const double y = D(expected.position.y) - target.y;
    const auto horizontal = F32(std::sqrt(D(F32(x * x)) + D(F32(y * y))));
    const auto pitch = F32(360.0 - CreateAngle(expected.position.z, horizontal, target.z, 0));
    expected.angles.x = F32(TurnAngle(expected.angles.x, pitch, turn));
}
} // namespace

// 0x5323d3..0x5323f8 y 0x5325ca: el 619/1 sólo tiene lo común del 0x619: velocidad 0, vida 12, el ancho y 2 puntos. Sin
// rand(); la posición es el primer punto y el destino no se escribe, porque tiene dueño.
TEST(FullSetJointsTest, TheSummonEnergyIsBornWithTheCommonFieldsOfThe619)
{
    ActorState hero;
    hero.position = {1300.0f, 1700.0f, 290.0f};
    std::array<JointState, 1> joints{};
    joints[0].targetPosition = {1.0f, 2.0f, 3.0f};
    CrtRandom random(1);
    Request request{0x619, 1, 5.0f, -1, 0};
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

// 0x538a52..0x538d28: después del avance común con la velocidad de antes, sube 4; gira de a 10 hacia el héroe 120 más
// arriba, leído en vivo; la luz es (12 − vida) · 0,1f por (0,4f; 0,6f; 1) y, con la vida en 1, el sprite 0x5fd en ese
// destino con el giro rand() % 360 y la escala ((rand() & 7) + 8) · 0,2f. La cola común con la matriz del principio del
// paso; muere en el paso 13.
TEST(FullSetJointsTest, TheSummonEnergyClimbsTowardsItsOwnerAndShinesOnItsLastLife)
{
    ActorState hero;
    hero.position = {1300.0f, 1700.0f, 290.0f};
    std::array<JointState, 1> joints{};
    CrtRandom random(1);
    Request request{0x619, 1, 5.0f, -1, 0};
    request.first = {1450.0f, 1600.0f, 380.0f};
    request.angles = {40.0f, 0.0f, 213.0f};
    ASSERT_TRUE(Create(joints, request, &hero, random).HasValue());
    JointState expected = joints[0];
    Client::World::FrameSprites sprites;
    CrtRandom draws(1);
    const double tenth = D(Float(0x3dcccccdu));

    for (int step = 1; step <= 13; ++step)
    {
        SCOPED_TRACE(step);
        if (step == 6)
            hero.position = {1310.0f, 1690.0f, 300.0f};
        ASSERT_TRUE(Client::Scenes::StepJoints(joints, random, nullptr, nullptr, 0, &sprites).HasValue());

        const auto matrix = Client::Render::Models::OriginalAngleMatrix(expected.angles);
        const glm::vec3 moved = Client::Render::Models::OriginalVectorRotate({0.0f, -expected.speed, 0.0f}, matrix);
        if (expected.speed > 0)
            for (int axis = 0; axis < 3; ++axis)
                expected.position[axis] = F32(D(expected.position[axis]) + D(moved[axis]));
        expected.speed = F32(4.0 + D(expected.speed));
        const glm::vec3 target{hero.position.x, hero.position.y, F32(D(hero.position.z) + 120.0)};
        Steer(expected, target, 10.0f);
        const double brightness = static_cast<double>(12 - expected.life) * tenth;
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

// 0x538b17: sin el héroe no tiene a quién seguir; 0x538cce: en la última vida, sin el RNG o los sprites corta antes de
// tocar nada.
TEST(FullSetJointsTest, TheSummonEnergyNeedsItsOwnerAndItsLastSprite)
{
    std::array<JointState, 1> joints{};
    CrtRandom random(1);
    Request request{0x619, 1, 5.0f, -1, 0};
    ASSERT_TRUE(Create(joints, request, nullptr, random).HasValue());
    const auto ownerless = Client::Scenes::StepJoints(joints, random);
    ASSERT_FALSE(ownerless.HasValue());
    EXPECT_NE(ownerless.Error().find("0x538b17"), std::string::npos) << ownerless.Error();

    ActorState hero;
    std::array<JointState, 1> last{};
    ASSERT_TRUE(Create(last, request, &hero, random).HasValue());
    last[0].life = 1;
    const JointState before = last[0];
    const auto blind = Client::Scenes::StepJoints(last, random);
    ASSERT_FALSE(blind.HasValue());
    EXPECT_NE(blind.Error().find("0x538cce"), std::string::npos) << blind.Error();
    EXPECT_EQ(last[0].speed, before.speed);
    EXPECT_EQ(last[0].light, before.light);
    EXPECT_EQ(last[0].life, 1);
}
