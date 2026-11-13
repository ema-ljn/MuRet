#include "Scenes/FaunaJointCreation.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRotation.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameDrawTail.h"
#include "World/IFishStepServices.h"
#include "World/Particles.h"
#include "World/TerrainLighting.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

// El 618/0 y el 618/1 del que mató (CL9 hito 3, fase 2d): 0x530860 (0x532125, 0x5322df, 0x5322f8) y su paso en
// 0x535840 (0x538313..0x5386a4). 06, "El paso del ataque".
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::ActorState;
using Client::Scenes::JointState;

constexpr std::uint32_t Seed = 0x2b1d3e5u;

struct BeamWorld
{
    BeamWorld()
    {
        killer.alive = true;
        killer.position = {1000.0f, 1000.0f, 100.0f};
        glm::vec3 from{1300.0f, 1000.0f, 100.0f};
        glm::vec3 to = from;
        glm::vec3 angles{0.0f, 0.0f, 0.0f};
        for (const std::int32_t subtype : {0, 1})
        {
            const Client::World::FishEffectRequest request{.type = Client::Scenes::KillBeamJoint,
                                                           .firstPosition = from,
                                                           .secondPosition = to,
                                                           .angles = angles,
                                                           .subtype = subtype,
                                                           .ownerContext = &killer,
                                                           .scale = 20.0f,
                                                           .key = -1,
                                                           .skill = 0,
                                                           .skillSerial = 0,
                                                           .target = -1};
            created = created && Client::Scenes::CreateFaunaJoint(joints, request, random).HasValue();
        }
    }

    Client::Core::Expected<void, std::string> Step()
    {
        return Client::Scenes::StepJoints(joints, random, nullptr, &particles, 0.0f, nullptr, lighting.get(),
                                          [this](std::int32_t sound) { sounds.push_back(sound); });
    }

    ActorState killer;
    std::vector<JointState> joints = std::vector<JointState>(4);
    CrtRandom random{Seed};
    Client::World::Particles particles;
    std::unique_ptr<Client::World::TerrainLighting> lighting = std::make_unique<Client::World::TerrainLighting>();
    std::vector<std::int32_t> sounds;
    bool created = true;
};

int AliveLightning(const Client::World::Particles& particles, std::int32_t subtype)
{
    int found = 0;
    for (const Client::World::Particle& particle : particles.All())
    {
        if (particle.alive && particle.type == Client::World::LightningParticle && particle.subtype == subtype)
            ++found;
    }
    return found;
}

// 0x532125..0x5322f8: el ancho, velocidad 3, vida 120, 8 puntos y la luz de cada subtipo, sin rand(). El primer paso
// (vida 120, en 100 o más) mueve 3 con la matriz de los ángulos de antes, gira el rumbo 10 (la /0) o −10 (la /1), sube
// 6 o 5, saca rand() & 3 para la luz dinámica, deja un 0x5d5/0 y agrega la cola.
TEST(KillBeamJointsTest, TheBeamsSpinWhileTheirLifeIsAboveOneHundred)
{
    BeamWorld tested;
    ASSERT_TRUE(tested.created);
    EXPECT_EQ(tested.random.State(), CrtRandom(Seed).State());
    EXPECT_EQ(tested.joints[0].light, (glm::vec3{std::bit_cast<float>(0x3ecccccdu), std::bit_cast<float>(0x3e99999au),
                                                 std::bit_cast<float>(0x3e4ccccdu)}));
    EXPECT_EQ(tested.joints[1].light,
              (glm::vec3{std::bit_cast<float>(0x3dcccccdu), std::bit_cast<float>(0x3dcccccdu), 0.5f}));
    const JointState first = tested.joints[0];
    const JointState second = tested.joints[1];

    ASSERT_TRUE(tested.Step().HasValue());

    CrtRandom expected(Seed);
    static_cast<void>(expected.Next());
    static_cast<void>(expected.Next());
    EXPECT_EQ(tested.random.State(), expected.State());
    for (const auto& [beam, before, turn, rise] :
         {std::tuple{&tested.joints[0], &first, 10.0f, 6.0f}, std::tuple{&tested.joints[1], &second, -10.0f, 5.0f}})
    {
        SCOPED_TRACE(beam->subtype);
        const glm::vec3 moved = Client::Render::Models::OriginalVectorRotate(
            glm::vec3(0.0f, -3.0f, 0.0f), Client::Render::Models::OriginalAngleMatrix(before->angles));
        EXPECT_EQ(beam->position.x, before->position.x + moved.x);
        EXPECT_EQ(beam->position.y, before->position.y + moved.y);
        EXPECT_EQ(beam->position.z, before->position.z + moved.z + rise);
        EXPECT_EQ(beam->angles.z, turn);
        EXPECT_EQ(beam->speed, 3.0f);
        EXPECT_EQ(beam->life, 119);
        EXPECT_TRUE(beam->alive);
    }
    EXPECT_EQ(AliveLightning(tested.particles, Client::World::LightningTrailSubtype), 2);
    EXPECT_TRUE(tested.sounds.empty());
}

// 0x538475..0x5385cd: debajo de vida 100, la velocidad sube de a 5 hasta 30 y 0x4e9810 los gira hacia el que mató (su
// posición, 120 más arriba). A 35 o menos mueren, dejan el 0x5d5/1 y piden el sonido 0x23. Un rand() por joint vivo y
// por paso, y nada más.
TEST(KillBeamJointsTest, TheBeamsComeBackToTheKillerAndSound)
{
    BeamWorld tested;
    ASSERT_TRUE(tested.created);
    int draws = 0;
    int steps = 0;
    while ((tested.joints[0].alive || tested.joints[1].alive) && steps < 130)
    {
        SCOPED_TRACE(steps);
        const int alive = (tested.joints[0].alive ? 1 : 0) + (tested.joints[1].alive ? 1 : 0);
        ASSERT_TRUE(tested.Step().HasValue());
        draws += alive;
        ++steps;
    }
    CrtRandom expected(Seed);
    for (int draw = 0; draw < draws; ++draw)
        static_cast<void>(expected.Next());
    EXPECT_EQ(tested.random.State(), expected.State());
    EXPECT_LT(steps, 120);
    EXPECT_EQ(tested.sounds, (std::vector<std::int32_t>{0x23, 0x23}));
    EXPECT_EQ(AliveLightning(tested.particles, Client::World::LightningHitSubtype), 2);
    EXPECT_EQ(tested.joints[0].speed, 30.0f);
}

// 0x538501: sin el personaje dueño, el paso corta al volver.
TEST(KillBeamJointsTest, AnOwnerlessBeamStopsWhenItTurnsBack)
{
    BeamWorld tested;
    ASSERT_TRUE(tested.created);
    tested.joints[0].ownerContext = nullptr;
    tested.joints[0].life = 99;
    const auto stepped = tested.Step();
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x538501"), std::string::npos);
}
} // namespace
