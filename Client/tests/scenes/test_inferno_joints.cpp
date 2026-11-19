#include "Scenes/FaunaJointCreation.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRotation.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameDrawTail.h"
#include "World/IFishStepServices.h"
#include "World/ModelEffects.h"
#include "World/Sprites.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// El 0x61c/2..6 (InfernoJoints.cpp): alta 0x534444..0x53463b y paso 0x53b6a0..0x53b7ec, y el 0x62b/6 que suelta la /4
// (alta 0x533faf..0x5340d7 con la vida y los puntos en 15, paso 0x53b029 con la vida · 0,8f).
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::JointState;

constexpr std::uint32_t Seed = 0x61cu;

double D(float value)
{
    return static_cast<double>(value);
}

float F32(double value)
{
    return static_cast<float>(value);
}

struct Pool
{
    Client::Core::Expected<std::optional<std::size_t>, std::string> Create(std::int32_t subtype)
    {
        glm::vec3 from = origin;
        glm::vec3 turn = angles;
        const Client::World::FishEffectRequest request{.type = 0x61c,
                                                       .firstPosition = from,
                                                       .secondPosition = from,
                                                       .angles = turn,
                                                       .subtype = subtype,
                                                       .ownerContext = nullptr,
                                                       .scale = 40,
                                                       .key = -1,
                                                       .skill = 0,
                                                       .skillSerial = 0,
                                                       .target = -1};
        return Client::Scenes::CreateFaunaJoint(joints, request, random);
    }

    Client::Core::Expected<void, std::string> Step()
    {
        return Client::Scenes::StepJoints(joints, random, nullptr, nullptr, worldClock, sprites.get(), nullptr, {}, {},
                                          {}, {}, effects.get(), hero);
    }

    std::vector<JointState> joints = std::vector<JointState>(4);
    CrtRandom random{Seed};
    std::unique_ptr<Client::World::FrameSprites> sprites = std::make_unique<Client::World::FrameSprites>();
    glm::vec3 origin{1500.0f, 1600.0f, 170.0f};
    glm::vec3 angles{0.0f, 0.0f, 135.0f};
    float worldClock = 1234.5f;
    std::unique_ptr<Client::World::ModelEffects> effects;
    const Client::Scenes::ActorState* hero = nullptr;
};

std::vector<Client::World::Sprite> LiveSprites(Client::World::FrameSprites& sprites)
{
    std::vector<Client::World::Sprite> alive;
    for (const auto& sprite : sprites.Sprites())
        if (sprite.alive)
            alive.push_back(sprite);
    return alive;
}
} // namespace

// 0x534444..0x5345bd: lo común de la /2..6 (velocidad 8, vx 3,5, vz 1, vida 20, 12 puntos, la cuenta en −1, +0x9ec en
// 5, el giro (0, 0, z) en +0x3c) y lo de cada subtipo; el camino (+0x24) en la posición. Sin rand().
TEST(InfernoJointsTest, EachSubtypeBornsWithItsTexture)
{
    struct Probe
    {
        std::int32_t subtype;
        std::uint16_t texture;
        float speed;
        glm::vec3 velocity;
        std::int32_t life;
        std::int32_t points;
        std::int32_t fade;
        float lift;
    };
    for (const Probe& probe :
         {Probe{2, 0x633, 8, {5, 0, 5}, 15, 12, 5, 10}, Probe{3, 0x60f, 10, {10.5f, 0, 2}, 20, 12, 10, 0},
          Probe{4, 0x6ae, 8, {15.5f, 0, 3}, 20, 13, 10, 0}, Probe{5, 0x634, 8, {3.5f, 0, 1}, 20, 12, 5, 0},
          Probe{6, 0x634, 20, {3.5f, 0, 1}, 16, 12, 5, 0}})
    {
        SCOPED_TRACE(probe.subtype);
        Pool pool;
        ASSERT_TRUE(pool.Create(probe.subtype).HasValue());
        const JointState& joint = pool.joints[0];
        EXPECT_EQ(pool.random.State(), CrtRandom{Seed}.State());
        EXPECT_EQ(joint.texture, probe.texture);
        EXPECT_EQ(joint.speed, probe.speed);
        EXPECT_EQ(joint.velocity, probe.velocity);
        EXPECT_EQ(joint.life, probe.life);
        EXPECT_EQ(joint.maximumCount, probe.points);
        EXPECT_EQ(joint.field9ec, probe.fade);
        EXPECT_EQ(joint.count, -1);
        EXPECT_EQ(joint.width, 40.0f);
        EXPECT_EQ(joint.bendAngles, glm::vec3(0.0f, 0.0f, 135.0f));
        EXPECT_EQ(joint.position, pool.origin + glm::vec3(0.0f, 0.0f, probe.lift));
        EXPECT_EQ(joint.pathIncrement, joint.position);
    }
}

// 0x53b6b2..0x53b7ec: la /4 avanza con la velocidad de antes (0x53590e), la velocidad suma vz y vz suma vx (vida 20);
// suelta un 0x62b/6 de ancho 30 desde su posición con sus ángulos (un rand(): vx del 0x62b), que pasa en la misma
// vuelta, el sprite 0x5ac (1,6f) y el 0x5fd (1,5f, girado el reloj · 0,1f), con la luz (0,1; 0,6; 1).
TEST(InfernoJointsTest, TheHoleRisesAndDropsSparks)
{
    Pool pool;
    ASSERT_TRUE(pool.Create(4).HasValue());
    JointState& joint = pool.joints[0];
    CrtRandom expected = pool.random;
    const auto turn = static_cast<float>(expected.Next() % 360);
    const auto matrix = Client::Render::Models::OriginalAngleMatrix(pool.angles);
    const auto moved = Client::Render::Models::OriginalVectorRotate({0, -8.0f, 0}, matrix);
    const glm::vec3 position{F32(D(pool.origin.x) + D(moved.x)), F32(D(pool.origin.y) + D(moved.y)),
                             F32(D(pool.origin.z) + D(moved.z))};

    ASSERT_TRUE(pool.Step().HasValue());

    EXPECT_EQ(pool.random.State(), expected.State());
    EXPECT_EQ(joint.position, position);
    EXPECT_EQ(joint.speed, 11.0f);
    EXPECT_EQ(joint.velocity.z, 18.5f);
    EXPECT_EQ(joint.life, 19);
    EXPECT_EQ(joint.count, 0);
    const JointState& impact = pool.joints[1];
    ASSERT_TRUE(impact.alive);
    EXPECT_EQ(impact.type, 0x62b);
    EXPECT_EQ(impact.subtype, 6);
    EXPECT_EQ(impact.width, 30.0f);
    EXPECT_EQ(impact.maximumCount, 15);
    EXPECT_EQ(impact.life, 14); // 15 del alta y la vida del paso de la misma vuelta.
    EXPECT_EQ(impact.velocity.x, turn);
    EXPECT_EQ(impact.angles, pool.angles);
    const auto sprites = LiveSprites(*pool.sprites);
    ASSERT_EQ(sprites.size(), 2u);
    const glm::vec3 glow{std::bit_cast<float>(0x3dcccccdu), std::bit_cast<float>(0x3f19999au), 1};
    EXPECT_EQ(sprites[0].texture, 0x5ac);
    EXPECT_EQ(sprites[0].scale, std::bit_cast<float>(0x3fcccccdu));
    EXPECT_EQ(sprites[0].rotation, 0.0f);
    EXPECT_EQ(sprites[0].light, glow);
    EXPECT_EQ(sprites[0].position, position);
    EXPECT_EQ(sprites[1].texture, 0x5fd);
    EXPECT_EQ(sprites[1].scale, 1.5f);
    EXPECT_EQ(sprites[1].rotation, F32(D(pool.worldClock) * D(std::bit_cast<float>(0x3dcccccdu))));
}

// 0x53b6cf: con menos de 15 de vida vz sólo suma 0,5; con menos de +0x9ec, la luz por 0,7692308f. Con el pool lleno,
// el 0x62b/6 no está ni saca rand(), y los sprites sí.
TEST(InfernoJointsTest, TheHoleSlowsAndFadesAtTheEnd)
{
    Pool pool;
    ASSERT_TRUE(pool.Create(4).HasValue());
    JointState& joint = pool.joints[0];
    joint.life = 9;
    for (std::size_t slot = 1; slot < pool.joints.size(); ++slot)
    {
        pool.joints[slot].alive = true;
        pool.joints[slot].type = 0x617; // ocupados, sin rand() en su paso
        pool.joints[slot].life = 100;
        pool.joints[slot].tailEnabled = false;
    }
    const CrtRandom before = pool.random;

    ASSERT_TRUE(pool.Step().HasValue());

    EXPECT_EQ(pool.random.State(), before.State());
    EXPECT_EQ(joint.velocity.z, 3.5f);
    const float fade = F32(D(1.0f) * D(std::bit_cast<float>(0x3f44ec4fu)));
    EXPECT_EQ(joint.light, glm::vec3(fade));
    EXPECT_EQ(LiveSprites(*pool.sprites).size(), 2u);
}

// 0x53b709..0x53b732 con +0x9ec en 5 (la /2, la /5 y la /6): con vida 9 no se apagan (con el 10 de la /3 y la /4 sí)
// y con vida 4, la luz por 0,7692308f.
TEST(InfernoJointsTest, TheLavaFadesBelowFive)
{
    const double fade = D(std::bit_cast<float>(0x3f44ec4fu));
    for (const std::int32_t subtype : {2, 5, 6})
    {
        for (const std::int32_t life : {9, 4})
        {
            SCOPED_TRACE(testing::Message() << "subtipo " << subtype << ", vida " << life);
            Pool pool;
            ASSERT_TRUE(pool.Create(subtype).HasValue());
            JointState& joint = pool.joints[0];
            ASSERT_EQ(joint.field9ec, 5);
            joint.life = life;

            ASSERT_TRUE(pool.Step().HasValue());

            EXPECT_EQ(joint.light, glm::vec3(life < 5 ? F32(D(1.0f) * fade) : 1.0f));
            EXPECT_EQ(pool.random.State(), CrtRandom{Seed}.State());
        }
    }
}

// 0x53b6f2..0x53b702: la /6 (la lava rápida) deja de agregar tramos con la cola llena y no suelta nada.
TEST(InfernoJointsTest, TheLavaStopsWithAFullTail)
{
    Pool pool;
    ASSERT_TRUE(pool.Create(6).HasValue());
    JointState& joint = pool.joints[0];
    joint.count = joint.maximumCount - 1;

    ASSERT_TRUE(pool.Step().HasValue());

    EXPECT_FALSE(joint.tailEnabled);
    EXPECT_EQ(joint.count, joint.maximumCount - 1);
    EXPECT_FALSE(pool.joints[1].alive);
    EXPECT_TRUE(LiveSprites(*pool.sprites).empty());
}

// 0x53b735..0x53b768: la /2 con la vida múltiplo de 5 suelta el 0xf6/6 en su camino (+0x24, la partida 10 más
// arriba) con sus ángulos propios (+0x3c), su luz y la clave 20, sin dueño: vida 5, +0xec en 20 y la escala 20 · 0,01f,
// velocidad y luz del modelo en 0,1f, sin rand(). Con otra vida, nada. Sin la tabla de efectos, corta ahí; sin el
// héroe, corta el alta (0x519a4f).
TEST(InfernoJointsTest, TheInfernoDropsItsBurstEveryFiveSteps)
{
    const Client::Scenes::ActorState hero{};
    for (const std::int32_t life : {15, 14, 10})
    {
        SCOPED_TRACE(life);
        Pool pool;
        pool.effects = std::make_unique<Client::World::ModelEffects>();
        pool.hero = &hero;
        ASSERT_TRUE(pool.Create(2).HasValue());
        JointState& joint = pool.joints[0];
        joint.life = life;
        const glm::vec3 light = joint.light;
        ASSERT_EQ(joint.pathIncrement, glm::vec3(1500.0f, 1600.0f, 180.0f));

        ASSERT_TRUE(pool.Step().HasValue());

        EXPECT_EQ(pool.random.State(), CrtRandom{Seed}.State());
        const Client::World::ModelEffect& burst = pool.effects->Effects()[0];
        if (life % 5 != 0)
        {
            EXPECT_FALSE(burst.alive);
            continue;
        }
        ASSERT_TRUE(burst.alive);
        EXPECT_EQ(burst.type, 0xf6);
        EXPECT_EQ(burst.subtype, 6);
        EXPECT_EQ(burst.life, 5);
        EXPECT_EQ(burst.transform.position, glm::vec3(1500.0f, 1600.0f, 180.0f));
        EXPECT_EQ(burst.transform.angles, joint.bendAngles);
        EXPECT_EQ(burst.light, light);
        EXPECT_EQ(burst.flagEc, 20.0f);
        EXPECT_EQ(burst.transform.scale, F32(20.0 * D(0.01f)));
        EXPECT_EQ(burst.speed, 0.1f);
        EXPECT_EQ(burst.objectState.light, 0.1f);
        EXPECT_FALSE(burst.hasOwner);
        EXPECT_FALSE(pool.effects->Effects()[1].alive);
    }

    Pool tableless;
    ASSERT_TRUE(tableless.Create(2).HasValue());
    const auto cut = tableless.Step();
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x53b760"), std::string::npos) << cut.Error();
    Pool heroless;
    heroless.effects = std::make_unique<Client::World::ModelEffects>();
    ASSERT_TRUE(heroless.Create(2).HasValue());
    const auto blind = heroless.Step();
    ASSERT_FALSE(blind.HasValue());
    EXPECT_NE(blind.Error().find("0x519a4f"), std::string::npos) << blind.Error();
}

// El 0x61c/1, el rayo rojo del láser 0xd0/1 de los magos (0x51a5dd), con el efecto de dueño. 0x53439e..0x53442d:
// velocidad 0, el ancho, el pase 1 (+0x64), la luz en 1, 15 puntos, vida 30 y la posición y los ángulos del dueño. Sin
// rand(). Los ángulos no son los del pedido sino los del dueño (+0x1c). Sin dueño, el original apaga el joint,
// desreferencia el puntero nulo (+0x10, 0x5343fb) y revienta: MuRet corta (0x5343f8).
TEST(InfernoJointsTest, TheLaserJointIsBornOnItsOwner)
{
    Client::World::ModelEffect laser{};
    laser.alive = true;
    laser.transform.position = {700.0f, 800.0f, 450.0f};
    laser.transform.angles = {30.0f, 0.0f, 90.0f};
    std::vector<JointState> joints(2);
    CrtRandom random(Seed);
    glm::vec3 from{1.0f, 2.0f, 3.0f};
    glm::vec3 turn{5.0f, 6.0f, 7.0f};
    Client::World::FishEffectRequest request{.type = 0x61c,
                                             .firstPosition = from,
                                             .secondPosition = from,
                                             .angles = turn,
                                             .subtype = 1,
                                             .ownerContext = &laser,
                                             .scale = 180.0f,
                                             .key = -1,
                                             .skill = 0,
                                             .skillSerial = 0,
                                             .target = -1};
    const auto created = Client::Scenes::CreateFaunaJoint(joints, request, random);
    ASSERT_TRUE(created.HasValue()) << created.Error();
    EXPECT_EQ(random.State(), CrtRandom(Seed).State());
    const JointState& joint = joints[0];
    EXPECT_EQ(joint.speed, 0.0f);
    EXPECT_EQ(joint.width, 180.0f);
    EXPECT_EQ(joint.category, 1);
    EXPECT_EQ(joint.light, glm::vec3(1.0f));
    EXPECT_EQ(joint.maximumCount, 15);
    EXPECT_EQ(joint.life, 30);
    EXPECT_EQ(joint.position, laser.transform.position);
    EXPECT_EQ(joint.angles, laser.transform.angles);

    request.ownerContext = nullptr;
    std::vector<JointState> lonely(1);
    const auto cut = Client::Scenes::CreateFaunaJoint(lonely, request, random);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x5343f8"), std::string::npos) << cut.Error();
}

// 0x53b628..0x53b69b: toma el byte vivo del dueño, el ancho baja 1, la posición es la del dueño 100 más arriba y, con
// menos de 15 de vida, la luz por 0,7692308f; después la cola común. Sin rand().
TEST(InfernoJointsTest, TheLaserJointFollowsItsOwnerAndNarrows)
{
    Client::World::ModelEffect laser{};
    laser.alive = true;
    laser.transform.position = {700.0f, 800.0f, 450.0f};
    std::vector<JointState> joints(2);
    CrtRandom random(Seed);
    glm::vec3 from{1.0f, 2.0f, 3.0f};
    glm::vec3 turn{30.0f, 0.0f, 90.0f};
    ASSERT_TRUE(Client::Scenes::CreateFaunaJoint(joints,
                                                 {.type = 0x61c,
                                                  .firstPosition = from,
                                                  .secondPosition = from,
                                                  .angles = turn,
                                                  .subtype = 1,
                                                  .ownerContext = &laser,
                                                  .scale = 180.0f,
                                                  .key = -1,
                                                  .skill = 0,
                                                  .skillSerial = 0,
                                                  .target = -1},
                                                 random)
                    .HasValue());
    JointState& joint = joints[0];
    laser.transform.position = {710.0f, 790.0f, 460.0f};

    ASSERT_TRUE(Client::Scenes::StepJoints(joints, random).HasValue());

    EXPECT_TRUE(joint.alive);
    EXPECT_EQ(joint.width, 179.0f);
    EXPECT_EQ(joint.position, glm::vec3(710.0f, 790.0f, 560.0f));
    EXPECT_EQ(joint.light, glm::vec3(1.0f));
    EXPECT_EQ(joint.life, 29);
    EXPECT_EQ(random.State(), CrtRandom(Seed).State());

    joint.life = 14;
    laser.alive = false;
    ASSERT_TRUE(Client::Scenes::StepJoints(joints, random).HasValue());

    EXPECT_FALSE(joint.alive);
    EXPECT_EQ(joint.width, 178.0f);
    EXPECT_EQ(joint.light, glm::vec3(F32(D(1.0f) * D(std::bit_cast<float>(0x3f44ec4fu)))));
    EXPECT_EQ(joint.life, 13);
}
