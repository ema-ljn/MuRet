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
        return Client::Scenes::StepJoints(joints, random, nullptr, &particles, 0.0f, nullptr, nullptr, {},
                                          [this](float, float) { return ground; });
    }

    std::vector<JointState> joints = std::vector<JointState>(8);
    CrtRandom random{Seed};
    Client::World::Particles particles;
    double ground = -1000.0;
};

// 0x53bf7f..0x53c0a0: desde el décimo paso, con la z por encima del suelo (0x5f93c0), el ancho 40, la cola cruzada
// (0x535020 con la matriz del paso) y, con la vida por debajo de la mitad de la tanda, la luz · 0,7692308f. Sólo en el
// suelo o debajo corta, en el 0x614/4 (0x53bfd3).
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

    storm.ground = 1.0e6;
    const auto touched = storm.Step();
    ASSERT_FALSE(touched.HasValue());
    EXPECT_TRUE(touched.Error().starts_with("0x53bfd3")) << touched.Error();
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
        return Client::Scenes::StepJoints(joints, random, nullptr, nullptr, 0.0f, sprites.get(), nullptr, {},
                                          [this](float, float) { return ground; });
    }

    std::vector<JointState> joints = std::vector<JointState>(2);
    CrtRandom random{Seed};
    std::unique_ptr<Client::World::FrameSprites> sprites = std::make_unique<Client::World::FrameSprites>();
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

// 0x53590e y 0x53c0c9..0x53c38b: en el aire avanza con la velocidad (el movimiento común), sube 10, agrega la cola,
// sube otros 10 y avanza de nuevo girado por +0x3c; el sprite 0x60f de escala 2,5 con la luz y rand() % 360. La cola
// común agrega otro tramo.
TEST(StormJointsTest, TheFallingStarSpeedsUpInTheAir)
{
    FallingStar star;
    ASSERT_TRUE(star.Create().HasValue());
    JointState& joint = star.joints[0];
    const float speed = joint.speed;
    CrtRandom expected = star.random;

    ASSERT_TRUE(star.Step().HasValue());

    const auto matrix = Client::Render::Models::OriginalAngleMatrix(star.angles);
    const auto first = Client::Render::Models::OriginalVectorRotate({0, -speed, 0}, matrix);
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

// 0x53c16d..0x53c2e0: debajo de la altura del terreno menos 150, la z queda ahí; la primera vez (la clave en 0) pide el
// efecto 0x6eb/4 (0x518cf0, lote altos) y corta; después (la clave en 1) la velocidad pasa a −20 y la luz a ·
// 0,8333333f, y el sprite sale igual.
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

        EXPECT_EQ(joint.position.z, 4850.0f);
        if (key == 0)
        {
            ASSERT_FALSE(stepped.HasValue());
            EXPECT_NE(stepped.Error().find("0x53c295"), std::string::npos) << stepped.Error();
            EXPECT_EQ(star.random.State(), before.State());
            continue;
        }
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(joint.speed, -20.0f);
        const float faded = F32(1.0 * D(std::bit_cast<float>(0x3f555555u)));
        EXPECT_EQ(joint.light, glm::vec3(faded));
        CrtRandom expected = before;
        static_cast<void>(expected.Next());
        EXPECT_EQ(star.random.State(), expected.State());
    }
}
