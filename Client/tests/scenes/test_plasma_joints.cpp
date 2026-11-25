#include "Scenes/FaunaJointCreation.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Math/OriginalAngles.h"
#include "Math/OriginalRotation.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameDrawTail.h"
#include "World/IFishStepServices.h"
#include "World/Particles.h"
#include "World/TerrainLighting.h"
#include "World/WorldObjectStepServices.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// El 0x14f (PlasmaJoints.cpp): el alta 0x530ddd..0x530ed8 y el paso 0x535b37..0x535d4a, con el pedido de 0x5026a4
// (0x530860(0x14f, punto, blanco + 0x10, giro, 0x4f2230(o), blanco, 100, −1, 0, 0, −1)). Los esperados del paso repiten
// las fórmulas del ASM con otro CrtRandom de la misma semilla.
namespace
{
using Client::Crt::CrtRandom;
using Client::Scenes::ActorState;
using Client::Scenes::JointState;

constexpr std::uint32_t Seed = 0x14fu;

using Client::Math::D;
using Client::Math::F32;

struct PlasmaWorld
{
    Client::Core::Expected<std::optional<std::size_t>, std::string> Create(std::int32_t subtype, float width,
                                                                           void* owner)
    {
        glm::vec3 from = start;
        glm::vec3 to = second;
        glm::vec3 angles{};
        const Client::World::FishEffectRequest request{.type = 0x14f,
                                                       .firstPosition = from,
                                                       .secondPosition = to,
                                                       .angles = angles,
                                                       .subtype = subtype,
                                                       .ownerContext = owner,
                                                       .scale = width,
                                                       .key = -1,
                                                       .skill = 0,
                                                       .skillSerial = 0,
                                                       .target = -1};
        return Client::Scenes::CreateFaunaJoint(joints, request, random);
    }

    Client::Core::Expected<void, std::string> Step()
    {
        return Client::Scenes::StepJoints(joints, random, nullptr, particles.get(), 0.0f, nullptr, lighting.get());
    }

    glm::vec3 start{1000.0f, 2000.0f, 300.0f};
    glm::vec3 second{1000.0f, 1500.0f, 220.0f};
    std::vector<JointState> joints = std::vector<JointState>(2);
    std::unique_ptr<Client::World::Particles> particles = std::make_unique<Client::World::Particles>();
    std::unique_ptr<Client::World::TerrainLighting> lighting = std::make_unique<Client::World::TerrainLighting>();
    CrtRandom random{Seed};
};

// 0x535c26..0x535c77: la luz dinámica de radio 2 con ((rand() & 3) + 4) · 0,04f · 0,4f, por los bits de cada sorteo
// (de una cuenta aparte: cada producto a 53 bits y el fst a float).
constexpr std::array<std::uint32_t, 4> GlowBits{0x3d83126fu, 0x3da3d70au, 0x3dc49ba6u, 0x3de56041u};

// Una vuelta del paso con `replay`: el giro hacia el destino, el sacudón y, si no llega, la luz (en `lighting`, si
// está) y el avance. Devuelve si llegó.
bool Turn(JointState& joint, CrtRandom& replay, Client::World::TerrainLighting* lighting = nullptr)
{
    const float distance = F32(Client::Math::MoveToward(joint.position, joint.angles, joint.targetPosition, 50.0f));
    joint.velocity.x = F32(static_cast<double>((replay.Next() & 0x3ff) - 0x200) / D(joint.width));
    const double turn = static_cast<double>((replay.Next() & 0x3ff) - 0x200) / D(joint.width);
    joint.velocity.z = F32(turn);
    const glm::vec3 shaken{F32(D(joint.angles.x) + D(joint.velocity.x)), F32(D(joint.angles.y) + D(joint.velocity.y)),
                           F32(turn + D(joint.angles.z))};
    if (D(joint.speed) * 1.5 > D(distance))
        return true;
    const auto shade = static_cast<std::size_t>(replay.Next() & 3);
    if (lighting != nullptr)
    {
        const glm::vec3 glow(std::bit_cast<float>(GlowBits[shade]));
        EXPECT_TRUE(Client::World::AddDynamicLight(lighting, joint.position, glow, 2, "prueba").HasValue());
    }
    const auto moved =
        Client::Math::OriginalVectorRotate({0.0f, -joint.speed, 0.0f}, Client::Math::OriginalAngleMatrix(shaken));
    for (int axis = 0; axis < 3; ++axis)
        joint.position[axis] = F32(D(moved[axis]) + D(joint.position[axis]));
    return false;
}
} // namespace

// 0x530ddd..0x530ed8: sin rand(); velocidad 50, la textura repetida, el ancho, 50 puntos y vida 20; la textura y la luz
// por subtipo (0..2 la 0x614, 3..5 la 0x632); otro subtipo queda con las del prefijo (el tipo y la luz blanca).
TEST(PlasmaJointsTest, EachSubtypeBornsWithItsColor)
{
    constexpr auto Bits = [](std::uint32_t bits) { return std::bit_cast<float>(bits); };
    struct Probe
    {
        std::int32_t subtype;
        std::uint16_t texture;
        glm::vec3 light;
    };
    for (const Probe& probe :
         {Probe{0, 0x614, {Bits(0x3f333333u), 1.0f, Bits(0x3f333333u)}},
          Probe{1, 0x614, {1.0f, Bits(0x3f19999au), Bits(0x3f19999au)}},
          Probe{2, 0x614, {Bits(0x3f333333u), Bits(0x3f333333u), 1.0f}},
          Probe{3, 0x632, {Bits(0x3dcccccdu), Bits(0x3f4ccccdu), Bits(0x3dcccccdu)}},
          Probe{4, 0x632, {1.0f, Bits(0x3e99999au), Bits(0x3e4ccccdu)}},
          Probe{5, 0x632, {Bits(0x3e4ccccdu), Bits(0x3e99999au), 1.0f}}, Probe{6, 0x14f, glm::vec3(1.0f)}})
    {
        SCOPED_TRACE(probe.subtype);
        PlasmaWorld tested;
        ActorState target{};
        ASSERT_TRUE(tested.Create(probe.subtype, 100.0f, &target).HasValue());
        EXPECT_EQ(tested.random.State(), CrtRandom{Seed}.State());
        const JointState& joint = tested.joints[0];
        EXPECT_EQ(joint.texture, probe.texture);
        EXPECT_EQ(joint.light, probe.light);
        EXPECT_EQ(joint.speed, 50.0f);
        EXPECT_TRUE(joint.repeatedTexture);
        EXPECT_TRUE(joint.tailEnabled);
        EXPECT_EQ(joint.width, 100.0f);
        EXPECT_EQ(joint.maximumCount, 50);
        EXPECT_EQ(joint.life, 20);
        EXPECT_EQ(joint.ownerContext, &target);
    }
}

// 0x535b37..0x535cd4: después del movimiento común (50 por sus ángulos), una vuelta por punto: el destino es el dueño
// 80 más arriba, gira hacia él de a 50 (0x4e9810), saca vx y vz ((rand() & 0x3ff) − 512) / ancho, agrega el tramo y, si
// no llega (velocidad · 1,5 contra la distancia), con el ancho de 50 o más deja la luz dinámica y avanza.
// Con ancho 100, al llegar no suelta nada. Sigue la cola común.
TEST(PlasmaJointsTest, ThePlasmaTurnsTowardItsOwnerUntilItArrives)
{
    PlasmaWorld tested;
    ActorState target{};
    target.position = {1000.0f, 1500.0f, 220.0f};
    ASSERT_TRUE(tested.Create(0, 100.0f, &target).HasValue());
    JointState expected = tested.joints[0];

    ASSERT_TRUE(tested.Step().HasValue());

    CrtRandom replay{Seed};
    const auto first =
        Client::Math::OriginalVectorRotate({0.0f, -50.0f, 0.0f}, Client::Math::OriginalAngleMatrix(expected.angles));
    for (int axis = 0; axis < 3; ++axis)
        expected.position[axis] = F32(D(expected.position[axis]) + D(first[axis]));
    expected.targetPosition = {1000.0f, 1500.0f, 300.0f};
    auto lit = std::make_unique<Client::World::TerrainLighting>();
    int turns = 0;
    for (; turns < 50; ++turns)
        if (Turn(expected, replay, lit.get()))
            break;
    ASSERT_LT(turns, 50);
    EXPECT_GT(turns, 3);
    const JointState& joint = tested.joints[0];
    EXPECT_EQ(tested.random.State(), replay.State());
    EXPECT_EQ(joint.targetPosition, expected.targetPosition);
    EXPECT_EQ(joint.position, expected.position);
    EXPECT_EQ(joint.angles, expected.angles);
    EXPECT_EQ(joint.velocity, expected.velocity);
    // La luz dinámica de cada vuelta que no llega, en la posición de antes de avanzar.
    EXPECT_TRUE(lit->cells == tested.lighting->cells);
    const auto dark = std::make_unique<Client::World::TerrainLighting>();
    EXPECT_FALSE(lit->cells == dark->cells);
    // Un tramo por vuelta (también la de la llegada) y el de la cola común.
    EXPECT_EQ(joint.count, turns + 2);
    EXPECT_EQ(joint.life, 19);
    EXPECT_EQ(std::ranges::count_if(tested.particles->All(),
                                    [](const Client::World::Particle& particle) { return particle.alive; }),
              0);
}

// 0x535cdf..0x535d4a: con el ancho en 50 justo, al llegar la partícula 0x5ca y, con rand() % 8 en 0, tres rand() sin
// usar y la 0x5f2/0. Sin dueño, el destino es el segundo punto del alta.
TEST(PlasmaJointsTest, TheFiftyWidePlasmaSparksWhenItArrives)
{
    int smokes = 0;
    for (std::uint32_t seed = Seed; seed < Seed + 16; ++seed)
    {
        SCOPED_TRACE(seed);
        PlasmaWorld tested;
        tested.random = CrtRandom{seed};
        tested.second = {1000.0f, 1960.0f, 300.0f};
        ASSERT_TRUE(tested.Create(0, 50.0f, nullptr).HasValue());
        JointState expected = tested.joints[0];
        ASSERT_TRUE(tested.Step().HasValue());

        CrtRandom replay{seed};
        const auto first = Client::Math::OriginalVectorRotate({0.0f, -50.0f, 0.0f},
                                                              Client::Math::OriginalAngleMatrix(expected.angles));
        for (int axis = 0; axis < 3; ++axis)
            expected.position[axis] = F32(D(expected.position[axis]) + D(first[axis]));
        ASSERT_TRUE(Turn(expected, replay));
        auto twin = std::make_unique<Client::World::Particles>();
        ASSERT_TRUE(
            twin->CreateChecked(0x5ca, expected.position, expected.angles, expected.light, 0, 1.0f, 0.0f, replay)
                .HasValue());
        const bool smoke = replay.Next() % 8 == 0;
        if (smoke)
        {
            ++smokes;
            for (int unused = 0; unused < 3; ++unused)
                static_cast<void>(replay.Next());
            ASSERT_TRUE(
                twin->CreateChecked(0x5f2, expected.position, expected.angles, expected.light, 0, 1.0f, 0.0f, replay)
                    .HasValue());
        }
        EXPECT_EQ(tested.random.State(), replay.State());
        EXPECT_EQ(tested.joints[0].targetPosition, tested.second);
        EXPECT_EQ(tested.joints[0].position, expected.position);
        const auto count = [&tested](std::int32_t type)
        {
            return std::ranges::count_if(tested.particles->All(), [type](const Client::World::Particle& particle)
                                         { return particle.alive && particle.type == type; });
        };
        EXPECT_EQ(count(0x5ca), 1);
        EXPECT_EQ(count(0x5f2), smoke ? 1 : 0);
    }
    EXPECT_GT(smokes, 0);
}
