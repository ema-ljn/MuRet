#include "Scenes/FaunaJointCreation.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/OriginalRotation.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/WorldState.h"
#include "World/IFishStepServices.h"
#include "World/Sprites.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// El 0x62c (SpiralJoints.cpp): alta 0x5346d8 y paso 0x53b93f.
namespace
{
using Client::Crt::CrtRandom;
using Client::Render::Models::OriginalAngleMatrix;
using Client::Render::Models::OriginalVectorRotate;
using Client::Scenes::JointState;

constexpr std::uint32_t Seed = 0x62cu;

float F32(double value)
{
    return static_cast<float>(value);
}

glm::vec3 Sum(const glm::vec3& first, const glm::vec3& second)
{
    return {F32(static_cast<double>(first.x) + second.x), F32(static_cast<double>(first.y) + second.y),
            F32(static_cast<double>(first.z) + second.z)};
}

struct Spirals
{
    Client::Core::Expected<std::optional<std::size_t>, std::string>
    Create(std::int32_t subtype, float width = 100.0f, std::int32_t key = -1, std::int32_t skill = 0)
    {
        glm::vec3 from = origin;
        glm::vec3 to = origin;
        glm::vec3 turn = angles;
        const Client::World::FishEffectRequest request{.type = 0x62c,
                                                       .firstPosition = from,
                                                       .secondPosition = to,
                                                       .angles = turn,
                                                       .subtype = subtype,
                                                       .ownerContext = owner,
                                                       .scale = width,
                                                       .key = key,
                                                       .skill = skill,
                                                       .skillSerial = 0,
                                                       .target = -1};
        return Client::Scenes::CreateFaunaJoint(joints, request, random);
    }

    Client::Core::Expected<void, std::string> Step(Client::Scenes::WorldState* world = nullptr,
                                                   Client::World::FrameSprites* sprites = nullptr)
    {
        return Client::Scenes::StepJoints(joints, random, nullptr, nullptr, 0.0f, sprites, nullptr, {}, {}, {}, {},
                                          nullptr, nullptr, world);
    }

    std::vector<JointState> joints = std::vector<JointState>(4);
    CrtRandom random{Seed};
    glm::vec3 origin{1000.0f, 2000.0f, 50.0f};
    glm::vec3 angles{0.0f, 0.0f, 30.0f};
    void* owner = nullptr;
};

// 0x5346d8..0x534887: la /1 lleva la tanda rand() % 3 + 2 en +0x9eb y en la vida (20 más), el radio 80 y el giro 180
// en +0x58/+0x5c, y nace en el camino (la posición 150 más alta) más (0, 0, 80) girado por (80, 180, 0).
TEST(SpiralJointsTest, TheTurningSpiralsBornOnTheirRadius)
{
    Spirals spirals;
    CrtRandom expected{Seed};
    const auto volley = static_cast<std::uint8_t>(expected.Next() % 3 + 2);

    ASSERT_TRUE(spirals.Create(1).HasValue());

    const JointState& joint = spirals.joints[0];
    EXPECT_EQ(spirals.random.State(), expected.State());
    EXPECT_EQ(joint.texture, 0x614);
    EXPECT_FALSE(joint.tailEnabled);
    EXPECT_EQ(joint.width, 100.0f);
    EXPECT_EQ(joint.speed, -3.0f);
    EXPECT_EQ(joint.volley, volley);
    EXPECT_EQ(joint.life, 20 + volley);
    EXPECT_EQ(joint.maximumCount, 30);
    EXPECT_EQ(joint.field9ec, 1);
    EXPECT_EQ(joint.light, glm::vec3(1.0f, std::bit_cast<float>(0x3f4ccccdu), 1.0f));
    EXPECT_EQ(joint.targetPosition, glm::vec3(80.0f, 180.0f, 0.0f));
    const glm::vec3 path{1000.0f, 2000.0f, 200.0f};
    EXPECT_EQ(joint.pathIncrement, path);
    const auto radius = OriginalVectorRotate({0, 0, 80}, OriginalAngleMatrix({80, 180, 0}));
    EXPECT_EQ(joint.position, Sum(radius, path));
}

// 0x5347b0..0x534824: la /0xb..0xd tienen su color y, como la /2 y la /4, el giro −180; la /3 y la /4, sin tanda.
// La /0 no saca rand() y nace en el camino.
TEST(SpiralJointsTest, EachSubtypeHasItsColorTurnAndVolley)
{
    struct Probe
    {
        std::int32_t subtype;
        glm::vec3 light;
        float turn;
        bool volley;
    };
    const float pale = std::bit_cast<float>(0x3f4ccccdu), soft = std::bit_cast<float>(0x3f333333u),
                dim = std::bit_cast<float>(0x3f19999au);
    for (const Probe& probe : {Probe{2, {1, pale, 1}, -180, true}, Probe{3, {1, pale, 1}, 180, false},
                               Probe{4, {1, pale, 1}, -180, false}, Probe{0xb, {soft, 1, soft}, -180, true},
                               Probe{0xc, {1, dim, dim}, -180, true}, Probe{0xd, {soft, soft, 1}, -180, true}})
    {
        SCOPED_TRACE(probe.subtype);
        Spirals spirals;
        ASSERT_TRUE(spirals.Create(probe.subtype).HasValue());
        const JointState& joint = spirals.joints[0];
        EXPECT_EQ(joint.light, probe.light);
        EXPECT_EQ(joint.targetPosition.y, probe.turn);
        EXPECT_EQ(joint.volley != 0, probe.volley);
        EXPECT_EQ(joint.life, 20 + joint.volley);
        EXPECT_NE(spirals.random.State(), CrtRandom(Seed).State());
    }
    Spirals column;
    ASSERT_TRUE(column.Create(0, 250.0f).HasValue());
    EXPECT_EQ(column.random.State(), CrtRandom(Seed).State());
    EXPECT_EQ(column.joints[0].texture, 0x614);
    EXPECT_EQ(column.joints[0].width, 250.0f);
    EXPECT_EQ(column.joints[0].position, glm::vec3(1000.0f, 2000.0f, 200.0f));
    EXPECT_EQ(column.joints[0].targetPosition, glm::vec3(0.0f));
}

// 0x534897..0x534926 y 0x530aa5: la /5..7 salta el primer tramo; textura 0x60d, el hueso en +0x9ec (el skill), una
// vuelta de cola, vida 15, la velocidad (0, −4, 0) (0 con una key), la tanda 30 y (30, 0, subtipo · 90) en +0x58.
TEST(SpiralJointsTest, TheOrbitsWaitOnTheirOwnerBone)
{
    Spirals spirals;
    ASSERT_TRUE(spirals.Create(6, 20.0f, -1, 0x123).HasValue());
    const JointState& joint = spirals.joints[0];
    EXPECT_EQ(joint.texture, 0x60d);
    EXPECT_EQ(joint.field9ec, 0x23);
    EXPECT_EQ(joint.skill, 0);
    EXPECT_EQ(joint.speed, 0.0f);
    EXPECT_EQ(joint.velocity, glm::vec3(0.0f, -4.0f, 0.0f));
    EXPECT_EQ(joint.life, 15);
    EXPECT_EQ(joint.maximumCount, 1);
    EXPECT_EQ(joint.volley, 30);
    EXPECT_EQ(joint.targetPosition, glm::vec3(30.0f, 0.0f, 540.0f));
    EXPECT_EQ(joint.position, glm::vec3(1000.0f, 2000.0f, 200.0f));
    EXPECT_EQ(joint.points[0][0], glm::vec3(0.0f)); // sin el primer tramo
    EXPECT_EQ(spirals.random.State(), CrtRandom(Seed).State());

    Spirals keyed;
    ASSERT_TRUE(keyed.Create(5, 20.0f, 7).HasValue());
    EXPECT_EQ(keyed.joints[0].velocity, glm::vec3(0.0f));

    // 0x53bbe6..0x53be1c: sin dueño muere.
    ASSERT_TRUE(spirals.Step().HasValue());
    EXPECT_FALSE(joint.alive);
}

// 0x53bbf1..0x53be17: con dueño, la posición del dueño va al modelo compartido de su tipo (la escala y la acción
// quedan) y el camino arranca en el punto (0, 20, 0) de su hueso +0x9ec. Cada paso da min(+0x6c, tanda) tramos: el
// camino avanza la velocidad girada por el hueso (con la key en −1, 0,1f más baja por tramo, sólo en la local), +0x5c
// gira 40 (la /5 y la /7 para un lado, la /6 para el otro) desde +0x60, la posición es (0, 0, radio) girado por (+0x5c,
// 0, 0) sobre el camino, el radio baja 0,15f y en los tramos pares queda el sprite 0x60f con media luz roja. +0x6c
// crece uno.
TEST(SpiralJointsTest, TheOrbitsCircleTheirOwnerBone)
{
    using Client::Render::Models::OriginalMatrixOf;
    using Client::Render::Models::OriginalTransformPosition;
    Client::Scenes::ActorState owner;
    owner.type = 0x1a5;
    owner.position = {100.0f, 200.0f, 300.0f};
    owner.bodyPose.assign(3, Client::Render::Models::BoneTransform(0.0f));
    owner.bodyPose[2] =
        Client::Render::Models::BoneTransform(glm::vec3{0.6f, 0.8f, 0.0f}, glm::vec3{-0.8f, 0.6f, 0.0f},
                                              glm::vec3{0.0f, 0.0f, 1.0f}, glm::vec3{10.0f, 20.0f, 30.0f});
    auto world = std::make_unique<Client::Scenes::WorldState>();
    world->PlaceModel(owner.type, {.scale = 1.25f, .origin = {9.0f, 9.0f, 9.0f}, .action = 4});
    Client::World::FrameSprites sprites;

    for (const std::int32_t subtype : {5, 6})
    {
        SCOPED_TRACE(subtype);
        Spirals spirals;
        spirals.owner = &owner;
        ASSERT_TRUE(spirals.Create(subtype, 20.0f, -1, 2).HasValue());
        JointState& joint = spirals.joints[0];
        joint.light = {0.75f, 0.5f, 0.25f};
        sprites.Clear();

        const auto bone = OriginalMatrixOf(owner.bodyPose[2]);
        const double swing = subtype == 5 ? 40.0 : -40.0;
        float radius = 30.0f;
        int expectedSprites = 0;
        for (std::int32_t step = 1; step <= 2; ++step)
        {
            ASSERT_TRUE(spirals.Step(world.get(), &sprites).HasValue());
            const auto placement = world->ModelPlacement(owner.type);
            EXPECT_EQ(placement.scale, 1.25f);
            EXPECT_EQ(placement.origin, owner.position);
            EXPECT_EQ(placement.action, 4);
            glm::vec3 path = OriginalTransformPosition(bone, {0.0f, 20.0f, 0.0f}, 1.25f, owner.position);
            glm::vec3 velocity{0.0f, -4.0f, 0.0f};
            float turn = 450.0f + static_cast<float>(subtype - 5) * 90.0f;
            glm::vec3 position{};
            for (std::int32_t segment = 0; segment < step; ++segment)
            {
                path = Sum(OriginalVectorRotate(velocity, bone), path);
                turn = F32(static_cast<double>(turn) + swing);
                position =
                    Sum(OriginalVectorRotate({0.0f, 0.0f, radius}, OriginalAngleMatrix({turn, 0.0f, 0.0f})), path);
                radius = F32(static_cast<double>(radius) - static_cast<double>(std::bit_cast<float>(0x3e19999au)));
                velocity.y =
                    F32(static_cast<double>(velocity.y) - static_cast<double>(std::bit_cast<float>(0x3dcccccdu)));
                if (segment % 2 == 0)
                {
                    const Client::World::Sprite& sprite = sprites.Sprites()[static_cast<std::size_t>(expectedSprites)];
                    ++expectedSprites;
                    EXPECT_TRUE(sprite.alive);
                    EXPECT_EQ(sprite.texture, 0x60f);
                    EXPECT_EQ(sprite.position, path);
                    EXPECT_EQ(sprite.scale, 0.375f);
                    EXPECT_EQ(sprite.light, joint.light);
                }
            }
            EXPECT_EQ(joint.pathIncrement, path);
            EXPECT_EQ(joint.position, position);
            EXPECT_EQ(joint.targetPosition,
                      (glm::vec3{radius, turn, 450.0f + static_cast<float>(subtype - 5) * 90.0f}));
            EXPECT_EQ(joint.velocity, (glm::vec3{0.0f, -4.0f, 0.0f}));
            EXPECT_EQ(joint.maximumCount, step + 1);
            EXPECT_TRUE(joint.alive);
        }
        EXPECT_EQ(expectedSprites, 2);
        EXPECT_FALSE(sprites.Sprites()[2].alive);
        EXPECT_EQ(spirals.random.State(), CrtRandom(Seed).State());
    }

    // Sin los modelos compartidos, o con un dueño que no es un personaje, corta.
    Spirals unserved;
    unserved.owner = &owner;
    ASSERT_TRUE(unserved.Create(7, 20.0f, -1, 2).HasValue());
    const auto cut = unserved.Step(nullptr, &sprites);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_TRUE(cut.Error().starts_with("0x53bc42")) << cut.Error();
    unserved.joints[0].ownerKind = Client::Scenes::JointOwnerKind::Helper;
    const auto stranger = unserved.Step(world.get(), &sprites);
    ASSERT_FALSE(stranger.HasValue());
    EXPECT_TRUE(stranger.Error().starts_with("0x53bc27")) << stranger.Error();
}

// 0x53b974..0x53ba27: la /0 da +0x9ec − 1 tramos por paso (0, 2, 4…): cada uno la cola con la matriz de los ángulos y
// (0, velocidad, 0) girado por ella sobre el camino, y la velocidad 2 menos.
TEST(SpiralJointsTest, TheColumnRisesWithMoreStretchesEachStep)
{
    Spirals spirals;
    ASSERT_TRUE(spirals.Create(0).HasValue());
    JointState& joint = spirals.joints[0];
    const std::int32_t count = joint.count;
    const glm::vec3 start = joint.pathIncrement;

    ASSERT_TRUE(spirals.Step().HasValue());
    EXPECT_EQ(joint.field9ec, 3);
    EXPECT_EQ(joint.count, count);
    EXPECT_EQ(joint.position, start);
    EXPECT_EQ(joint.life, 19);

    ASSERT_TRUE(spirals.Step().HasValue());
    const auto matrix = OriginalAngleMatrix(spirals.angles);
    glm::vec3 path = Sum(OriginalVectorRotate({0, -3, 0}, matrix), start);
    path = Sum(OriginalVectorRotate({0, -5, 0}, matrix), path);
    EXPECT_EQ(joint.field9ec, 5);
    EXPECT_EQ(joint.count, count + 2);
    EXPECT_EQ(joint.pathIncrement, path);
    EXPECT_EQ(joint.position, path);
    EXPECT_EQ(joint.speed, -7.0f);
    EXPECT_EQ(spirals.random.State(), CrtRandom(Seed).State());
}

// 0x53ba4c..0x53bbe1: la /1 espera su tanda y después sube como la /0, corrida por (0, 0, +0x58) girado por (0,
// +0x5c, la z de los ángulos), con +0x5c 20 menos (siempre: Q-C1624) y +0x58 2,5 menos por tramo. La /3 resta el
// corrimiento.
TEST(SpiralJointsTest, TheSpiralsWaitTheirVolleyAndTurnAroundTheirPath)
{
    for (const std::int32_t subtype : {1, 3})
    {
        SCOPED_TRACE(subtype);
        Spirals spirals;
        ASSERT_TRUE(spirals.Create(subtype).HasValue());
        JointState& joint = spirals.joints[0];
        const std::uint8_t volley = joint.volley;
        for (std::uint8_t wait = 0; wait < volley; ++wait)
            ASSERT_TRUE(spirals.Step().HasValue());
        EXPECT_EQ(joint.volley, 0);
        EXPECT_EQ(joint.field9ec, 1);
        ASSERT_TRUE(spirals.Step().HasValue()); // sin tramos
        EXPECT_EQ(joint.field9ec, 3);
        const glm::vec3 start = joint.pathIncrement;
        const std::int32_t count = joint.count;

        ASSERT_TRUE(spirals.Step().HasValue());

        const auto matrix = OriginalAngleMatrix(spirals.angles);
        glm::vec3 path = start;
        glm::vec3 position{};
        float turn = 180.0f, radius = 80.0f, speed = -3.0f;
        for (int stretch = 0; stretch < 2; ++stretch)
        {
            path = Sum(OriginalVectorRotate({0, speed, 0}, matrix), path);
            turn -= 20.0f;
            const auto offset = OriginalVectorRotate({0, 0, radius}, OriginalAngleMatrix({0, turn, 30.0f}));
            position = subtype == 3 ? glm::vec3(F32(static_cast<double>(path.x) - offset.x),
                                                F32(static_cast<double>(path.y) - offset.y),
                                                F32(static_cast<double>(path.z) - offset.z))
                                    : Sum(offset, path);
            speed -= 2.0f;
            radius -= 2.5f;
        }
        EXPECT_EQ(joint.count, count + 2);
        EXPECT_EQ(joint.pathIncrement, path);
        EXPECT_EQ(joint.position, position);
        EXPECT_EQ(joint.targetPosition, glm::vec3(radius, turn, 0.0f));
        EXPECT_EQ(joint.speed, speed);
    }
}

// 0x53b95d..0x53b966 y 0x53be4c..0x53b698: con la cola llena (la cuenta en los puntos − 1) no se mueve; con menos de 10
// de vida, la luz por 0,7692308f en cada paso.
TEST(SpiralJointsTest, TheFullSpiralsOnlyFade)
{
    Spirals spirals;
    ASSERT_TRUE(spirals.Create(0).HasValue());
    JointState& joint = spirals.joints[0];
    joint.count = joint.maximumCount - 1;
    joint.field9ec = 7;
    joint.life = 9;
    const glm::vec3 position = joint.position;
    const glm::vec3 light = joint.light;

    ASSERT_TRUE(spirals.Step().HasValue());

    const double fade = static_cast<double>(std::bit_cast<float>(0x3f44ec4fu));
    EXPECT_EQ(joint.position, position);
    EXPECT_EQ(joint.field9ec, 7);
    EXPECT_EQ(joint.light, glm::vec3(F32(light.x * fade), F32(light.y * fade), F32(light.z * fade)));
    EXPECT_EQ(joint.life, 8);
}
} // namespace
