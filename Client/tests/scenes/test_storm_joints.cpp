#include "Scenes/FaunaJointCreation.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Scenes/GameDrawTail.h"
#include "World/IFishStepServices.h"
#include "World/Particles.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

// El 0x632 (StormJoints.cpp): la /7 y sus tres /6 (0x534b27, 0x534a16 y el paso 0x53bec2), y los subtipos de más de 7.
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
