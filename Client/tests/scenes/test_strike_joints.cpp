#include "Scenes/FaunaJointCreation.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Scenes/GameDrawTail.h"
#include "World/IFishStepServices.h"
#include "World/KalimaParticleKernels.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/Sprites.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// El 0x615/5 del 0x615 de Kalima (0x52b560) y el /6 del pulso 0x61c/1 del asedio (0x52c31e y 0x52c342): el alta de
// 0x531b0c..0x531bfc y el paso de 0x53768f..0x5379ef, que comparten con el /2 de Chaos Castle y el /7 de Crywolf.
namespace
{
using Client::Crt::CrtRandom;
using Client::Scenes::JointOwnerKind;
using Client::Scenes::JointState;

constexpr std::uint32_t Seed = 0x615u;

using Client::Math::D;
using Client::Math::F32;

struct Strike
{
    Client::Core::Expected<std::optional<std::size_t>, std::string> Create(std::int32_t subtype)
    {
        glm::vec3 from = start;
        glm::vec3 turn{10.0f, 20.0f, 30.0f};
        const bool owned = subtype == 6;
        const Client::World::FishEffectRequest request{.type = 0x615,
                                                       .firstPosition = from,
                                                       .secondPosition = from,
                                                       .angles = turn,
                                                       .subtype = subtype,
                                                       .ownerContext = owned ? &owner : nullptr,
                                                       .scale = 80.0f,
                                                       .key = -1,
                                                       .skill = 0,
                                                       .skillSerial = 0,
                                                       .target = -1};
        return Client::Scenes::CreateFaunaJoint(joints, request, random,
                                                owned ? JointOwnerKind::ModelEffect : JointOwnerKind::ByType);
    }

    Client::Core::Expected<void, std::string> Step()
    {
        return Client::Scenes::StepJoints(joints, random, nullptr, particles.get(), 0.0f, sprites.get(), nullptr, {},
                                          [this](float, float) { return ground; });
    }

    std::vector<JointState> joints = std::vector<JointState>(2);
    CrtRandom random{Seed};
    std::unique_ptr<Client::World::Particles> particles = std::make_unique<Client::World::Particles>();
    std::unique_ptr<Client::World::FrameSprites> sprites = std::make_unique<Client::World::FrameSprites>();
    Client::World::ModelEffect owner{};
    glm::vec3 start{1000.0f, 2000.0f, 300.0f};
    double ground = 250.0;
};
} // namespace

// 0x531b0c..0x531bfc: la textura 0x614, 50 puntos, vida 20, la x y la y corridas rand() % 10 − 5, 800 más arriba, los
// ángulos en 0, el destino en el segundo punto y +0x24 en la posición corrida. La /5 con la luz (1; 0,5; 0,2f); la /6
// con la velocidad 0,3f, la luz en 1 y otros 700 más arriba sin tocar +0x24. Dos rand().
TEST(StrikeJointsTest, EachBoltIsBornAboveItsPoint)
{
    for (const std::int32_t subtype : {5, 6})
    {
        SCOPED_TRACE(subtype);
        Strike strike;
        ASSERT_TRUE(strike.Create(subtype).HasValue());
        CrtRandom expected{Seed};
        const int x = expected.Next() % 10 - 5;
        const int y = expected.Next() % 10 - 5;
        EXPECT_EQ(strike.random.State(), expected.State());
        const JointState& joint = strike.joints[0];
        const glm::vec3 shifted{F32(static_cast<double>(x) + D(strike.start.x)),
                                F32(static_cast<double>(y) + D(strike.start.y)), F32(D(strike.start.z) + 800.0)};
        EXPECT_EQ(joint.texture, 0x614);
        EXPECT_EQ(joint.width, 80.0f);
        EXPECT_EQ(joint.maximumCount, 50);
        EXPECT_EQ(joint.life, 20);
        EXPECT_EQ(joint.angles, glm::vec3(0.0f));
        EXPECT_EQ(joint.targetPosition, strike.start);
        EXPECT_EQ(joint.pathIncrement, shifted);
        EXPECT_TRUE(joint.tailEnabled);
        if (subtype == 5)
        {
            EXPECT_EQ(joint.position, shifted);
            EXPECT_EQ(joint.speed, 0.0f);
            EXPECT_EQ(joint.light, glm::vec3(1.0f, 0.5f, std::bit_cast<float>(0x3e4ccccdu)));
            EXPECT_EQ(joint.ownerContext, nullptr);
        }
        else
        {
            EXPECT_EQ(joint.position, glm::vec3(shifted.x, shifted.y, F32(D(shifted.z) + 700.0)));
            EXPECT_EQ(joint.speed, std::bit_cast<float>(0x3e99999au));
            EXPECT_EQ(joint.light, glm::vec3(1.0f));
            EXPECT_EQ(joint.ownerContext, &strike.owner);
        }
    }
}

// 0x53768f..0x5379ef: el rayo se rehace desde +0x24: 45 tramos que corren x e y rand() % 20 − 10 y bajan la z (16 la
// /5, 13 la /6), cuatro que se acercan al destino y el destino. La /5 no suelta nada; la /6 pone la z del destino 30
// sobre el terreno (0x5f93c0) y deja ahí la partícula 0x6ed/0 de escala 2 con sus ángulos. Con más de 10 de vida, la
// cola común agrega el tramo 51 (0x53c393).
TEST(StrikeJointsTest, TheBoltRedrawsItsPathEachStep)
{
    for (const std::int32_t subtype : {5, 6})
    {
        SCOPED_TRACE(subtype);
        Strike strike;
        ASSERT_TRUE(strike.Create(subtype).HasValue());
        const JointState& joint = strike.joints[0];
        const glm::vec3 from = joint.pathIncrement;
        const CrtRandom before = strike.random;
        CrtRandom expected = strike.random;

        ASSERT_TRUE(strike.Step().HasValue());

        // Dos por tramo de la bajada y de la llegada y, con la /6, los dos de la velocidad de la 0x6ed (0x543b82).
        for (int draw = 0; draw < 45 * 2 + 4 * 2 + (subtype == 6 ? 2 : 0); ++draw)
            static_cast<void>(expected.Next());
        EXPECT_EQ(strike.random.State(), expected.State());
        // Cada tramo de la bajada corre la x y después la y rand() % 20 − 10 (sumados en la x87) y baja la z.
        const double drop = subtype == 6 ? 13.0 : 16.0;
        CrtRandom jitter = before;
        glm::vec3 lowest = from;
        for (int tramo = 0; tramo < 45; ++tramo)
        {
            lowest.x = F32(static_cast<double>(jitter.Next() % 20 - 10) + D(lowest.x));
            lowest.y = F32(static_cast<double>(jitter.Next() % 20 - 10) + D(lowest.y));
            lowest.z = F32(D(lowest.z) - drop);
        }
        // El tramo 45 (el último de la bajada) quedó seis lugares atrás: los cuatro de la llegada, el destino y la
        // cola. Con los ángulos en 0, su punto 2 es el centro 40 más abajo y el 0, 40 a la izquierda.
        EXPECT_EQ(joint.points[6][2].x, lowest.x);
        EXPECT_EQ(joint.points[6][2].y, lowest.y);
        EXPECT_EQ(joint.points[6][0].z, lowest.z);
        EXPECT_NE(lowest.x, from.x);
        EXPECT_NE(lowest.y, from.y);
        EXPECT_EQ(joint.count, 49);
        EXPECT_EQ(joint.life, 19);
        EXPECT_TRUE(joint.tailEnabled);
        const auto made =
            std::ranges::count_if(strike.particles->All(), [](const auto& particle)
                                  { return particle.alive && particle.type == Client::World::KalimaFanta; });
        if (subtype == 5)
        {
            EXPECT_EQ(made, 0);
            EXPECT_EQ(joint.position, strike.start);
            continue;
        }
        // El destino baja a 30 sobre el terreno después de copiarse en la posición (0x53782a), que no lo sigue.
        const glm::vec3 landed{strike.start.x, strike.start.y, 280.0f};
        EXPECT_EQ(joint.targetPosition, landed);
        EXPECT_EQ(joint.position, strike.start);
        ASSERT_EQ(made, 1);
        const auto& spark = strike.particles->All().front();
        EXPECT_EQ(spark.subtype, 0);
        EXPECT_EQ(spark.scale, 2.0f);
        EXPECT_EQ(spark.position, landed);
    }
}

// 0x5379d3..0x5379ef: con 10 de vida o menos, la /5 y la /6 apagan la cola (sin el tramo de 0x53c393); con menos de 4,
// la luz por 0,8333333f.
TEST(StrikeJointsTest, TheBoltFadesAndStopsItsTail)
{
    for (const std::int32_t subtype : {5, 6})
    {
        SCOPED_TRACE(subtype);
        Strike strike;
        ASSERT_TRUE(strike.Create(subtype).HasValue());
        JointState& joint = strike.joints[0];
        joint.life = 3;
        const glm::vec3 light = joint.light;

        ASSERT_TRUE(strike.Step().HasValue());

        EXPECT_FALSE(joint.tailEnabled);
        EXPECT_EQ(joint.life, 2);
        const double fade = D(std::bit_cast<float>(0x3f555555u));
        EXPECT_EQ(joint.light, glm::vec3(F32(D(light.x) * fade), F32(D(light.y) * fade), F32(D(light.z) * fade)));
    }
}
