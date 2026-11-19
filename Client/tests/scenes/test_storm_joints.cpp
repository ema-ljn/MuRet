#include "Scenes/FaunaJointCreation.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRotation.h"
#include "Scenes/GameDrawTail.h"
#include "World/IFishStepServices.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/Sprites.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// El 0x632 (StormJoints.cpp): la /7 y sus tres /6 (0x534b27, 0x534a16 y el paso 0x53bec2), los subtipos de más de 7 y
// la /5 del pulso del asedio (0x534bcb y el paso 0x53c0a5).
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::JointState;

constexpr std::uint32_t Seed = 0x632u;

struct Storm
{
    Client::Core::Expected<std::optional<std::size_t>, std::string> Create(std::int32_t subtype)
    {
        glm::vec3 from{1000.0f, 1000.0f, 0.0f};
        glm::vec3 to = from;
        glm::vec3 angles{0.0f, 0.0f, 0.0f};
        const Client::World::FishEffectRequest request{.type = 0x632,
                                                       .firstPosition = from,
                                                       .secondPosition = to,
                                                       .angles = angles,
                                                       .subtype = subtype,
                                                       .ownerContext = nullptr,
                                                       .scale = 40.0f,
                                                       .key = -1,
                                                       .skill = 0,
                                                       .skillSerial = 0,
                                                       .target = -1};
        return Client::Scenes::CreateFaunaJoint(joints, request, random);
    }

    Client::Core::Expected<void, std::string> Step()
    {
        return Client::Scenes::StepJoints(
            joints, random, nullptr, &particles, 0.0f, nullptr, nullptr,
            [this](std::int32_t sound) { sounds.push_back(sound); }, [this](float, float) { return ground; });
    }

    std::vector<JointState> joints = std::vector<JointState>(8);
    CrtRandom random{Seed};
    Client::World::Particles particles;
    double ground = -1000.0;
    std::vector<std::int32_t> sounds;
};

// 0x53bf7f..0x53c0a0: desde el décimo paso, con la z por encima del suelo (0x5f93c0), el ancho 40, la cola cruzada
// (0x535020 con la matriz del paso) y, con la vida por debajo de la mitad de la tanda, la luz · 0,7692308f.
TEST(StormJointsTest, TheBoltFallsUntilItTouchesTheGround)
{
    Storm storm;
    ASSERT_TRUE(storm.Create(7).HasValue());
    for (std::size_t slot : {0u, 2u, 3u})
        storm.joints[slot].alive = false;
    JointState& bolt = storm.joints[1];
    ASSERT_EQ(bolt.subtype, 6);
    ASSERT_EQ(bolt.volley, 0x19);
    bolt.life = 12;
    const glm::vec3 light = bolt.light;
    CrtRandom expected = storm.random;

    ASSERT_TRUE(storm.Step().HasValue());

    static_cast<void>(expected.Next()); // el temblor de la y
    EXPECT_EQ(storm.random.State(), expected.State());
    EXPECT_EQ(bolt.width, 40.0f);
    EXPECT_EQ(bolt.count, 1);
    EXPECT_EQ(bolt.light, light); // 12 no es menos que 0x19 / 2.
    EXPECT_EQ(bolt.life, 11);

    ASSERT_TRUE(storm.Step().HasValue());

    const auto faded = [](float value)
    { return static_cast<float>(static_cast<double>(value) * static_cast<double>(std::bit_cast<float>(0x3f44ec4fu))); };
    EXPECT_EQ(bolt.light, glm::vec3(faded(light.x), faded(light.y), faded(light.z)));
    EXPECT_EQ(bolt.count, 2);
}

// 0x53bfa9..0x53c04f: en el suelo (la z del terreno, 1234,567 en float, no por debajo de la del joint), antes de la
// cola, el joint 0x614/4 desde su posición con sus ángulos, de escala 60 y sin dueño; con la z de la velocidad en 0 o
// menos, FSIN(velocidad) · 20; la z en el suelo + 5 y la partícula 0x6ac/2 de escala 3 en una copia 30 más arriba, con
// su luz. Los bits salen de una cuenta aparte (el seno exacto a 64 bits); el joint y la partícula se repiten con
// gemelos que sacan los rand() del mismo CrtRandom. Con la z de la velocidad positiva, no la toca.
TEST(StormJointsTest, TheBoltHitsTheGround)
{
    for (const float rise : {-3.0f, 50.0f})
    {
        SCOPED_TRACE(rise);
        auto storm = std::make_unique<Storm>();
        ASSERT_TRUE(storm->Create(7).HasValue());
        for (std::size_t slot : {0u, 2u, 3u})
            storm->joints[slot].alive = false;
        JointState& bolt = storm->joints[1];
        bolt.life = 12;
        bolt.speed = 2.5f;
        bolt.velocity.z = rise;
        auto above = std::make_unique<Storm>(*storm);
        storm->ground = static_cast<double>(1234.567f);
        CrtRandom replay = storm->random;

        ASSERT_TRUE(storm->Step().HasValue());
        ASSERT_TRUE(above->Step().HasValue());

        const glm::vec3 moved = above->joints[1].position;
        EXPECT_EQ(bolt.position.x, moved.x);
        EXPECT_EQ(bolt.position.y, moved.y);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(bolt.position.z), 0x449af225u);
        if (rise < 0.0f)
        {
            EXPECT_EQ(std::bit_cast<std::uint32_t>(bolt.velocity.z), 0x413f82d7u);
        }
        else
        {
            EXPECT_EQ(bolt.velocity.z, 40.0f);
        }
        EXPECT_EQ(bolt.count, above->joints[1].count);
        EXPECT_EQ(bolt.life, 11);

        static_cast<void>(replay.Next()); // el temblor de la y
        std::vector<JointState> twinJoints(1);
        glm::vec3 from = moved;
        glm::vec3 angles = bolt.angles;
        const Client::World::FishEffectRequest request{.type = 0x614,
                                                       .firstPosition = from,
                                                       .secondPosition = from,
                                                       .angles = angles,
                                                       .subtype = 4,
                                                       .ownerContext = nullptr,
                                                       .scale = 60.0f,
                                                       .key = -1,
                                                       .skill = 0,
                                                       .skillSerial = 0,
                                                       .target = -1};
        ASSERT_TRUE(Client::Scenes::CreateFaunaJoint(twinJoints, request, replay).HasValue());
        const JointState& spark = storm->joints[0];
        ASSERT_TRUE(spark.alive);
        EXPECT_EQ(spark.type, 0x614);
        EXPECT_EQ(spark.subtype, 4);
        EXPECT_EQ(spark.ownerContext, nullptr);
        EXPECT_EQ(spark.position, twinJoints[0].position);
        EXPECT_EQ(spark.angles, twinJoints[0].angles);
        EXPECT_EQ(spark.width, twinJoints[0].width);
        EXPECT_EQ(spark.speed, twinJoints[0].speed);
        EXPECT_EQ(spark.life, twinJoints[0].life);

        auto twin = std::make_unique<Client::World::Particles>();
        const glm::vec3 flame{moved.x, moved.y, std::bit_cast<float>(0x449eb225u)};
        ASSERT_TRUE(twin->CreateChecked(0x6ac, flame, bolt.angles, bolt.light, 2, 3.0f, 0.0f, replay).HasValue());
        const auto& made = storm->particles.All()[0];
        const auto& expected = twin->All()[0];
        ASSERT_TRUE(made.alive);
        EXPECT_EQ(made.type, 0x6ac);
        EXPECT_EQ(made.subtype, 2);
        EXPECT_EQ(made.position, expected.position);
        EXPECT_EQ(made.scale, expected.scale);
        EXPECT_EQ(made.light, expected.light);
        EXPECT_EQ(made.life, expected.life);
        EXPECT_EQ(made.velocity, expected.velocity);
        EXPECT_EQ(storm->random.State(), replay.State());
    }
}

// 0x53bf3c..0x53bf76: en los diez primeros pasos de la tanda, la llamarada 0x5ee/0 (0x53bf6e) en +0x24, con los
// ángulos (+0x30), la luz (+0x48) y escala 3, sin rand(), y el sonido 0x40 de su alta (0x542aa7); el ancho 100. Con la
// tabla llena, ni la llamarada ni el sonido.
TEST(StormJointsTest, TheBoltBlazesInItsFirstTenSteps)
{
    for (const bool full : {false, true})
    {
        SCOPED_TRACE(full);
        Storm storm;
        ASSERT_TRUE(storm.Create(7).HasValue());
        for (std::size_t slot : {0u, 2u, 3u})
            storm.joints[slot].alive = false;
        JointState& bolt = storm.joints[1];
        ASSERT_EQ(bolt.subtype, 6);
        bolt.life = 20;
        if (full)
        {
            for (auto& particle : storm.particles.All())
                particle.alive = true;
        }
        const glm::vec3 point = bolt.pathIncrement;
        const glm::vec3 angles = bolt.angles;
        const glm::vec3 light = bolt.light;
        CrtRandom expected = storm.random;

        ASSERT_TRUE(storm.Step().HasValue());

        static_cast<void>(expected.Next()); // el temblor de la y
        EXPECT_EQ(storm.random.State(), expected.State());
        EXPECT_EQ(bolt.width, 100.0f);
        if (full)
        {
            EXPECT_TRUE(storm.sounds.empty());
            continue;
        }
        EXPECT_EQ(storm.sounds, std::vector<std::int32_t>{0x40});
        const auto& all = storm.particles.All();
        const auto blaze = std::ranges::find_if(all, [](const Client::World::Particle& particle)
                                                { return particle.alive && particle.type == 0x5ee; });
        ASSERT_NE(blaze, all.end());
        EXPECT_EQ(blaze->subtype, 0);
        EXPECT_EQ(blaze->position, point);
        EXPECT_EQ(blaze->angles, angles);
        EXPECT_EQ(blaze->light, light);
        EXPECT_EQ(blaze->scale, 3.0f);
        EXPECT_EQ(blaze->life, 12);
    }
}

// 0x534b1e y 0x53bec5: los de más de 7 sólo hacen el primer tramo y el tope de 50 puntos, y su paso va a la cola.
TEST(StormJointsTest, TheSubtypesAboveSevenOnlyAge)
{
    Storm storm;
    storm.joints[0].maximumCount = 70;
    const auto created = storm.Create(8);
    ASSERT_TRUE(created.HasValue()) << created.Error();
    JointState& joint = storm.joints[0];
    EXPECT_EQ(joint.maximumCount, 50);
    const std::int32_t life = joint.life;

    ASSERT_TRUE(storm.Step().HasValue());

    EXPECT_EQ(joint.life, life - 1);
    EXPECT_EQ(storm.random.State(), CrtRandom(Seed).State());
}
} // namespace

namespace
{
double D(float value)
{
    return static_cast<double>(value);
}

float F32(double value)
{
    return static_cast<float>(value);
}

// La /5 con un efecto de dueño (que no lee), los ángulos (90, 0, 0) del pulso y la altura del terreno de `ground`.
struct FallingStar
{
    Client::Core::Expected<std::optional<std::size_t>, std::string> Create()
    {
        glm::vec3 from = start;
        glm::vec3 turn = angles;
        const Client::World::FishEffectRequest request{.type = 0x632,
                                                       .firstPosition = from,
                                                       .secondPosition = from,
                                                       .angles = turn,
                                                       .subtype = 5,
                                                       .ownerContext = &owner,
                                                       .scale = 110.0f,
                                                       .key = -1,
                                                       .skill = 0,
                                                       .skillSerial = 0,
                                                       .target = -1};
        return Client::Scenes::CreateFaunaJoint(joints, request, random, Client::Scenes::JointOwnerKind::ModelEffect);
    }

    Client::Core::Expected<void, std::string> Step()
    {
        return Client::Scenes::StepJoints(
            joints, random, nullptr, nullptr, 0.0f, sprites.get(), nullptr, {}, [this](float, float) { return ground; },
            {}, {}, effects.get());
    }

    std::vector<JointState> joints = std::vector<JointState>(2);
    CrtRandom random{Seed};
    std::unique_ptr<Client::World::FrameSprites> sprites = std::make_unique<Client::World::FrameSprites>();
    std::unique_ptr<Client::World::ModelEffects> effects = std::make_unique<Client::World::ModelEffects>();
    Client::World::ModelEffect owner{};
    glm::vec3 start{1000.0f, 2000.0f, 1500.0f};
    glm::vec3 angles{90.0f, 0.0f, 0.0f};
    double ground = 200.0;
};
} // namespace

// 0x534bcb..0x534cb6 con la /5: 15 puntos, vida 40, el ancho, +0x9f1 en 2 (después de guardar rand() & 1), la clave en
// 0, +0x9ec en 150, la textura 0x60f, la velocidad rand() % 20 + 10 y +0x3c con los ángulos. Dos rand(); el efecto de
// dueño queda en +0x54. Con el pool lleno, nada ni rand().
TEST(StormJointsTest, TheFallingStarIsBornWithItsSpeed)
{
    FallingStar star;
    ASSERT_TRUE(star.Create().HasValue());
    CrtRandom expected{Seed};
    static_cast<void>(expected.Next());
    const int speed = expected.Next() % 20;
    EXPECT_EQ(star.random.State(), expected.State());
    const JointState& joint = star.joints[0];
    EXPECT_EQ(joint.texture, 0x60f);
    EXPECT_EQ(joint.width, 110.0f);
    EXPECT_EQ(joint.maximumCount, 15);
    EXPECT_EQ(joint.life, 40);
    EXPECT_EQ(joint.speed, static_cast<float>(speed + 10));
    EXPECT_EQ(joint.orientation, 2);
    EXPECT_EQ(joint.key, 0);
    EXPECT_EQ(joint.field9ec, 150);
    EXPECT_EQ(joint.bendAngles, star.angles);
    EXPECT_EQ(joint.position, star.start);
    EXPECT_EQ(joint.ownerContext, &star.owner);
    EXPECT_TRUE(joint.tailEnabled);

    for (auto& other : star.joints)
        other.alive = true;
    const CrtRandom before = star.random;
    const auto full = star.Create();
    ASSERT_TRUE(full.HasValue());
    EXPECT_FALSE(full.Value().has_value());
    EXPECT_EQ(star.random.State(), before.State());
}

// 0x53590e y 0x53c0c9..0x53c38b: en el aire avanza con la velocidad girada por los ángulos (el movimiento común), sube
// 10, agrega la cola, sube otros 10 y avanza de nuevo girado por +0x3c, que no son los ángulos; el sprite 0x60f de
// escala 2,5 con la luz y rand() % 360. La cola común agrega otro tramo.
TEST(StormJointsTest, TheFallingStarSpeedsUpInTheAir)
{
    FallingStar star;
    ASSERT_TRUE(star.Create().HasValue());
    JointState& joint = star.joints[0];
    const glm::vec3 heading{30.0f, 0.0f, 45.0f};
    joint.angles = heading;
    const float speed = joint.speed;
    CrtRandom expected = star.random;

    ASSERT_TRUE(star.Step().HasValue());

    const auto first = Client::Render::Models::OriginalVectorRotate(
        {0, -speed, 0}, Client::Render::Models::OriginalAngleMatrix(heading));
    const auto matrix = Client::Render::Models::OriginalAngleMatrix(star.angles);
    const glm::vec3 moved{F32(D(star.start.x) + D(first.x)), F32(D(star.start.y) + D(first.y)),
                          F32(D(star.start.z) + D(first.z))};
    const float faster = F32(D(F32(D(speed) + 10.0)) + 10.0);
    const auto second = Client::Render::Models::OriginalVectorRotate({0, -faster, 0}, matrix);
    EXPECT_EQ(joint.position,
              glm::vec3(F32(D(second.x) + D(moved.x)), F32(D(second.y) + D(moved.y)), F32(D(second.z) + D(moved.z))));
    EXPECT_EQ(joint.speed, faster);
    EXPECT_EQ(joint.count, 2);
    EXPECT_EQ(joint.life, 39);
    const auto rotation = static_cast<float>(expected.Next() % 360);
    EXPECT_EQ(star.random.State(), expected.State());
    const auto sprites = star.sprites->Sprites();
    const auto live = std::ranges::find_if(sprites, [](const auto& sprite) { return sprite.alive; });
    ASSERT_NE(live, sprites.end());
    EXPECT_EQ(live->texture, 0x60f);
    EXPECT_EQ(live->scale, 2.5f);
    EXPECT_EQ(live->rotation, rotation);
    EXPECT_EQ(live->light, glm::vec3(1.0f));
    EXPECT_EQ(live->position, joint.position);
}

// 0x53c16d..0x53c2e0: debajo de la altura del terreno menos 150, la z queda ahí; la primera vez (la clave en 0) suelta
// la onda 0x6eb/4 en la posición, con los ángulos y la luz (1; 0,8f; 0,3f) (0x53c295, con el rand() de su escala si hay
// lugar); siempre la clave pasa a 1, la velocidad a −20 y la luz a · 0,8333333f, y el sprite sale igual.
TEST(StormJointsTest, TheFallingStarBouncesOnTheGround)
{
    for (const std::int16_t key : {std::int16_t{0}, std::int16_t{1}})
    {
        SCOPED_TRACE(key);
        FallingStar star;
        star.ground = 5000.0;
        ASSERT_TRUE(star.Create().HasValue());
        JointState& joint = star.joints[0];
        joint.key = key;
        const CrtRandom before = star.random;

        const auto stepped = star.Step();

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(joint.position.z, 4850.0f);
        EXPECT_EQ(joint.key, 1);
        EXPECT_EQ(joint.speed, -20.0f);
        const float faded = F32(1.0 * D(std::bit_cast<float>(0x3f555555u)));
        EXPECT_EQ(joint.light, glm::vec3(faded));
        CrtRandom expected = before;
        const auto waves =
            std::ranges::count_if(star.effects->Effects(), [](const auto& effect) { return effect.alive; });
        if (key == 0)
        {
            ASSERT_EQ(waves, 1);
            const Client::World::ModelEffect& wave = star.effects->Effects()[0];
            EXPECT_EQ(wave.type, Client::World::ShockWaveEffect);
            EXPECT_EQ(wave.subtype, 4);
            EXPECT_EQ(wave.transform.position, joint.position);
            EXPECT_EQ(wave.transform.angles, joint.angles);
            EXPECT_EQ(wave.life, 10);
            EXPECT_EQ(wave.light,
                      glm::vec3(1.0f, std::bit_cast<float>(0x3f4ccccdu), std::bit_cast<float>(0x3e99999au)));
            EXPECT_EQ(wave.transform.scale,
                      F32((static_cast<double>(expected.Next() % 6) + 6.0) * D(std::bit_cast<float>(0x3dcccccdu))));
        }
        else
            EXPECT_EQ(waves, 0);
        static_cast<void>(expected.Next());
        EXPECT_EQ(star.random.State(), expected.State());
    }
}

// 0x53c190..0x53c19b: con la cola apagada (+0x9f2), la clave en 0 no suelta la onda (ni su rand()); el rebote sigue
// igual.
TEST(StormJointsTest, TheFallingStarWithoutItsTailBouncesWithoutAWave)
{
    FallingStar star;
    star.ground = 5000.0;
    ASSERT_TRUE(star.Create().HasValue());
    JointState& joint = star.joints[0];
    joint.tailEnabled = false;
    joint.key = 0;
    CrtRandom expected = star.random;

    const auto stepped = star.Step();

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(joint.position.z, 4850.0f);
    EXPECT_EQ(joint.key, 1);
    EXPECT_EQ(joint.speed, -20.0f);
    EXPECT_EQ(std::ranges::count_if(star.effects->Effects(), [](const auto& effect) { return effect.alive; }), 0);
    static_cast<void>(expected.Next());
    EXPECT_EQ(star.random.State(), expected.State());
}

// 0x53c295: sin la tabla de efectos, la onda corta; con la tabla llena, no hay onda ni su rand().
TEST(StormJointsTest, TheFallingStarWaveNeedsTheEffectTable)
{
    FallingStar tableless;
    tableless.ground = 5000.0;
    tableless.effects.reset();
    ASSERT_TRUE(tableless.Create().HasValue());
    const auto cut = tableless.Step();
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x53c295"), std::string::npos) << cut.Error();

    FallingStar full;
    full.ground = 5000.0;
    for (Client::World::ModelEffect& effect : full.effects->Effects())
        effect.alive = true;
    ASSERT_TRUE(full.Create().HasValue());
    const CrtRandom before = full.random;
    ASSERT_TRUE(full.Step().HasValue());
    CrtRandom expected = before;
    static_cast<void>(expected.Next());
    EXPECT_EQ(full.random.State(), expected.State());
    EXPECT_EQ(full.joints[0].key, 1);
}
