#include "Scenes/FaunaJointCreation.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRotation.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameDrawTail.h"
#include "World/IFishStepServices.h"
#include "World/Particles.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// El 0x61c/0 (SweepJoints.cpp): el alta 0x5342e4..0x534399 y el paso 0x53b3e1..0x53b623, con el pedido del efecto 0x5be
// (0x52a72a: 0x530860(0x61c, posición + 100 en z, lo mismo, +0x28, 0, +0x108, 150, +0x8a, +0x89, 0, −1)).
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorState;
using Client::Scenes::JointState;

constexpr std::uint32_t Seed = 0x61cu;
constexpr std::size_t Last = 19;

float F32(double value)
{
    return static_cast<float>(value);
}

struct SweepWorld
{
    // El barrido en el último lugar: los rayos 0x614/3 que suelta caen en los primeros, que este paso ya pasó.
    Client::Core::Expected<std::optional<std::size_t>, std::string> Create(glm::vec3 angles, void* owner = nullptr)
    {
        for (std::size_t slot = 0; slot < Last; ++slot)
            joints[slot].alive = true;
        glm::vec3 from = start;
        const Client::World::FishEffectRequest request{.type = 0x61c,
                                                       .firstPosition = from,
                                                       .secondPosition = from,
                                                       .angles = angles,
                                                       .subtype = 0,
                                                       .ownerContext = owner,
                                                       .scale = 150.0f,
                                                       .key = 7,
                                                       .skill = 3,
                                                       .skillSerial = 0,
                                                       .target = -1};
        auto created = Client::Scenes::CreateFaunaJoint(joints, request, random);
        for (std::size_t slot = 0; slot < Last; ++slot)
            joints[slot].alive = false;
        return created;
    }

    Client::Core::Expected<void, std::string> Step()
    {
        return Client::Scenes::StepJoints(joints, random, nullptr, particles.get(), 0.0f, nullptr);
    }

    glm::vec3 start{1000.0f, 2000.0f, 400.0f};
    std::vector<JointState> joints = std::vector<JointState>(Last + 1);
    std::unique_ptr<Client::World::Particles> particles = std::make_unique<Client::World::Particles>();
    CrtRandom random{Seed};
};
} // namespace

// 0x5342e4..0x534399: sin rand(); la z de los ángulos + 30, la velocidad (+0x9dc) con esos ángulos, el destino en la
// partida, vida 20, 18 puntos, la cuenta en −1, sin la cola común, el ancho; la posición, (0, −180, 0) girado por los
// ángulos nuevos y sumado, y la luz en 1. Con la z en −30 los ángulos quedan en 0 y el giro es exacto.
TEST(SweepJointsTest, TheSweepBornsHundredEightyAway)
{
    SweepWorld tested;
    ASSERT_TRUE(tested.Create({0.0f, 0.0f, -30.0f}).HasValue());
    EXPECT_EQ(tested.random.State(), CrtRandom{Seed}.State());
    const JointState& joint = tested.joints[Last];
    EXPECT_TRUE(joint.alive);
    EXPECT_FALSE(joint.tailEnabled);
    EXPECT_EQ(joint.speed, 0.0f);
    EXPECT_EQ(joint.width, 150.0f);
    EXPECT_EQ(joint.angles, glm::vec3(0.0f));
    EXPECT_EQ(joint.velocity, glm::vec3(0.0f));
    EXPECT_EQ(joint.targetPosition, tested.start);
    EXPECT_EQ(joint.position, (glm::vec3{1000.0f, 1820.0f, 400.0f}));
    EXPECT_EQ(joint.life, 20);
    EXPECT_EQ(joint.maximumCount, 18);
    EXPECT_EQ(joint.count, -1);
    EXPECT_EQ(joint.light, glm::vec3(1.0f));
    EXPECT_EQ(joint.key, 7);
    EXPECT_EQ(joint.skill, 3);

    SweepWorld turned;
    ASSERT_TRUE(turned.Create({10.0f, 0.0f, 60.0f}).HasValue());
    const JointState& other = turned.joints[Last];
    EXPECT_EQ(other.angles, (glm::vec3{10.0f, 0.0f, 90.0f}));
    EXPECT_EQ(other.velocity, other.angles);
    const auto reach = Client::Render::Models::OriginalVectorRotate(
        {0.0f, -180.0f, 0.0f}, Client::Render::Models::OriginalAngleMatrix(other.angles));
    EXPECT_EQ(other.position,
              (glm::vec3{F32(static_cast<double>(reach.x) + 1000.0), F32(static_cast<double>(reach.y) + 2000.0),
                         F32(static_cast<double>(reach.z) + 400.0)}));
}

// 0x53b3f1..0x53b5c3: sin dueño, ocho tramos: la posición es el destino más (0, −180, 0) girado por la velocidad, un
// tramo, vz − 11; con rand() & 1 en 0 la partícula 0x5d9 y siempre dos 0x614/3 sin dueño desde la posición de antes
// hasta la nueva, con la key 5 (su vida) y el skill 0xa (sus puntos), de ancho rand() % 10 + 5 y (rand() & 7) + 4.
// Después la luz es la vida · 0,033333335f (0x53b611).
TEST(SweepJointsTest, TheSweepShedsTwoSparksForEachOfItsEightSegments)
{
    SweepWorld tested;
    ASSERT_TRUE(tested.Create({0.0f, 0.0f, -30.0f}).HasValue());
    ASSERT_TRUE(tested.Step().HasValue());

    const JointState& joint = tested.joints[Last];
    EXPECT_EQ(joint.count, 7);
    EXPECT_EQ(joint.velocity.z, -88.0f);
    EXPECT_EQ(joint.life, 19);
    const float light = F32(20.0 * static_cast<double>(std::bit_cast<float>(0x3d088889u)));
    EXPECT_EQ(joint.light, glm::vec3(light));
    // El primer tramo, con la velocidad en 0: 180 hacia −y del destino.
    EXPECT_EQ(tested.joints[0].targetPosition, (glm::vec3{1000.0f, 1820.0f, 400.0f}));
    EXPECT_EQ(tested.joints[0].position, (glm::vec3{1000.0f, 1820.0f, 400.0f}));

    CrtRandom replay{Seed};
    auto twin = std::make_unique<Client::World::Particles>();
    int fires = 0;
    glm::vec3 velocity{};
    for (std::size_t turn = 0; turn < 8; ++turn)
    {
        SCOPED_TRACE(turn);
        const auto swing = Client::Render::Models::OriginalVectorRotate(
            {0.0f, -180.0f, 0.0f}, Client::Render::Models::OriginalAngleMatrix(velocity));
        const glm::vec3 point{F32(static_cast<double>(swing.x) + 1000.0), F32(static_cast<double>(swing.y) + 2000.0),
                              F32(static_cast<double>(swing.z) + 400.0)};
        velocity.z = F32(static_cast<double>(velocity.z) - 11.0);
        if ((replay.Next() & 1) == 0)
        {
            ASSERT_TRUE(
                twin->CreateChecked(0x5d9, point, glm::vec3(0.0f), glm::vec3(1.0f), 0, 1.0f, 0.0f, replay).HasValue());
            ++fires;
        }
        const auto first = static_cast<float>(replay.Next() % 10 + 5);
        const auto second = static_cast<float>((replay.Next() & 7) + 4);
        const JointState& small = tested.joints[2 * turn];
        const JointState& large = tested.joints[2 * turn + 1];
        for (const JointState* spark : {&small, &large})
        {
            EXPECT_TRUE(spark->alive);
            EXPECT_EQ(spark->type, 0x614);
            EXPECT_EQ(spark->subtype, 3);
            EXPECT_EQ(spark->life, 5);
            EXPECT_EQ(spark->maximumCount, 10);
            EXPECT_EQ(spark->ownerContext, nullptr);
            EXPECT_EQ(spark->targetPosition, point);
        }
        EXPECT_EQ(small.width, first);
        EXPECT_EQ(large.width, second);
    }
    EXPECT_EQ(tested.random.State(), replay.State());
    std::ptrdiff_t alive = 0;
    for (const auto& particle : tested.particles->All())
        alive += particle.alive && particle.type == 0x5d9 ? 1 : 0;
    EXPECT_EQ(alive, fires);
}

// 0x53b3f8: con la cuenta en los puntos − 1 (17) no agrega tramos ni saca rand(); con la vida debajo de 10, la x de la
// luz por 0,6666667f en los tres canales (0x53b5fa).
TEST(SweepJointsTest, TheFullSweepOnlyDims)
{
    SweepWorld tested;
    ASSERT_TRUE(tested.Create({0.0f, 0.0f, -30.0f}).HasValue());
    JointState& joint = tested.joints[Last];
    joint.count = 17;
    joint.life = 9;
    joint.light = {0.5f, 0.25f, 0.75f};
    const glm::vec3 before = joint.position;

    ASSERT_TRUE(tested.Step().HasValue());

    EXPECT_EQ(tested.random.State(), CrtRandom{Seed}.State());
    EXPECT_EQ(joint.position, before);
    EXPECT_EQ(joint.count, 17);
    const float dim = F32(0.5 * static_cast<double>(std::bit_cast<float>(0x3f2aaaabu)));
    EXPECT_EQ(joint.light, glm::vec3(dim));
    EXPECT_EQ(joint.life, 8);
    EXPECT_FALSE(tested.joints[0].alive);
}

// 0x53b545..0x53b5b6: con dueño, vida de más de 18 y la vuelta múltiplo de 5, el original lo compara con el héroe
// ([0x743a170]) y pide el asedio o los blancos de la skill: corta después del primer tramo. Con vida 18, no.
TEST(SweepJointsTest, TheOwnedSweepCutsWhereItLooksForTheHero)
{
    ActorState owner{};
    SweepWorld tested;
    ASSERT_TRUE(tested.Create({0.0f, 0.0f, -30.0f}, &owner).HasValue());
    const auto cut = tested.Step();
    ASSERT_FALSE(cut.HasValue());
    EXPECT_TRUE(cut.Error().starts_with("0x53b548")) << cut.Error();
    EXPECT_EQ(tested.joints[Last].count, 0);

    SweepWorld late;
    ASSERT_TRUE(late.Create({0.0f, 0.0f, -30.0f}, &owner).HasValue());
    late.joints[Last].life = 18;
    ASSERT_TRUE(late.Step().HasValue());
    EXPECT_EQ(late.joints[Last].count, 7);
}
