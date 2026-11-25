#include "Scenes/FaunaJointCreation.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Models/Cloth.h"
#include "Scenes/GameDrawTail.h"
#include "World/IFishStepServices.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include "World/TerrainLighting.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Las estelas de flecha 0x610/0xc (la 0x11c, 0x51c713) y /0xe (la 0xf9, 0x51c56c): el alta de 0x531c43..0x53200e y el
// paso de 0x537ee6..0x538016, que comparten con la /0xd del misil de Crywolf (test_crywolf_arrow_dependencies); y la
// partícula 0x5f2/34 que suelta la /0xe (0x537f39). 06, "Los joints de las familias".
namespace
{
using Client::Crt::CrtRandom;
using Client::Scenes::JointOwnerKind;
using Client::Scenes::JointState;

constexpr std::uint32_t Seed = 0x610ceu;

using Client::Math::Bits;
using Client::Math::D;
using Client::Math::F32;

struct Trail
{
    Trail(std::int32_t subtype, float width, std::int32_t key)
    {
        owner.alive = true;
        const Client::World::FishEffectRequest request{.type = 0x610,
                                                       .firstPosition = from,
                                                       .secondPosition = from,
                                                       .angles = angles,
                                                       .subtype = subtype,
                                                       .ownerContext = &owner,
                                                       .scale = width,
                                                       .key = key,
                                                       .skill = 0,
                                                       .skillSerial = 0,
                                                       .target = -1};
        made = Client::Scenes::CreateFaunaJoint(joints, request, random, JointOwnerKind::ModelEffect).HasValue();
    }

    Client::Core::Expected<void, std::string> Step()
    {
        return Client::Scenes::StepJoints(joints, random, &cloth, particles.get(), 0.0f, &sprites, lighting.get());
    }

    glm::vec3 from{100.0f, 200.0f, 300.0f};
    glm::vec3 angles{};
    Client::World::ModelEffect owner;
    std::vector<JointState> joints = std::vector<JointState>(2);
    CrtRandom random{Seed};
    bool made = false;
    Client::Render::Models::ClothWorld cloth{.random = CrtRandom{1}};
    std::unique_ptr<Client::World::Particles> particles = std::make_unique<Client::World::Particles>();
    Client::World::FrameSprites sprites;
    std::unique_ptr<Client::World::TerrainLighting> lighting = std::make_unique<Client::World::TerrainLighting>();
};

// 0x531fa3..0x53200e: la /0xc nace con la luz (0,6f; 0,2f; 0,8f) y la mezcla 4 y la /0xe con la luz 1 y la mezcla 1;
// las dos con la key de vida, la key en 0, el ancho, 16 puntos, velocidad 70 y el destino en el segundo punto, sin
// rand(). Un dueño que no es un efecto con modelo no se acepta.
TEST(ArrowTrailJointsTest, TheTrailsAreBornWithTheLightAndBlendOfTheirSubtype)
{
    Trail twentieth(0xc, 40.0f, 80);
    ASSERT_TRUE(twentieth.made);
    const JointState& violet = twentieth.joints[0];
    EXPECT_EQ(violet.life, 80);
    EXPECT_EQ(violet.key, 0);
    EXPECT_EQ(violet.width, 40.0f);
    EXPECT_EQ(violet.maximumCount, 16);
    EXPECT_EQ(violet.speed, 70.0f);
    EXPECT_EQ(Bits(violet.light.x), 0x3f19999au);
    EXPECT_EQ(Bits(violet.light.y), 0x3e4ccccdu);
    EXPECT_EQ(Bits(violet.light.z), 0x3f4ccccdu);
    EXPECT_EQ(violet.blend, 4);
    EXPECT_EQ(violet.texture, 0x610);
    EXPECT_EQ(violet.targetPosition, (glm::vec3{100.0f, 200.0f, 300.0f}));
    EXPECT_EQ(twentieth.random.State(), Seed);

    Trail viper(0xe, 50.0f, 40);
    ASSERT_TRUE(viper.made);
    const JointState& white = viper.joints[0];
    EXPECT_EQ(white.life, 40);
    EXPECT_EQ(white.width, 50.0f);
    EXPECT_EQ(white.maximumCount, 16);
    EXPECT_EQ(white.speed, 70.0f);
    EXPECT_EQ(white.light, glm::vec3(1.0f));
    EXPECT_EQ(white.blend, 1);
    EXPECT_EQ(viper.random.State(), Seed);
}

// 0x537f0b..0x537f94: la /0xe viva, después de avanzar 70, deja en su posición el sprite 0x5c6 de escala 5 con su luz,
// las 0x5f2/0x22 y /0x23 de escala 1 con sus ángulos y su luz y la 0x5c6/0xa de escala 4 con (0,7f; 0,3f; 0,3f); su luz
// queda en (1; 1; 0,2f). Después, el rand() de la luz dinámica (0x537fc8). Cada partícula se repite en un pool gemelo.
TEST(ArrowTrailJointsTest, TheViperTrailLeavesItsSparksWhileAlive)
{
    Trail viper(0xe, 50.0f, 40);
    ASSERT_TRUE(viper.made);
    const glm::vec3 moved{100.0f, 130.0f, 300.0f};

    ASSERT_TRUE(viper.Step().HasValue());

    const JointState& joint = viper.joints[0];
    EXPECT_EQ(joint.position, moved);
    EXPECT_EQ(joint.life, 39);
    EXPECT_EQ(Bits(joint.light.x), Bits(1.0f));
    EXPECT_EQ(Bits(joint.light.y), Bits(1.0f));
    EXPECT_EQ(Bits(joint.light.z), 0x3e4ccccdu);
    const auto& sprite = viper.sprites.Sprites()[0];
    ASSERT_TRUE(sprite.alive);
    EXPECT_EQ(sprite.texture, 0x5c6);
    EXPECT_EQ(sprite.position, moved);
    EXPECT_EQ(sprite.scale, 5.0f);
    EXPECT_EQ(sprite.light, glm::vec3(1.0f));
    EXPECT_EQ(sprite.rotation, 0.0f);
    EXPECT_FALSE(viper.sprites.Sprites()[1].alive);

    CrtRandom replay(Seed);
    auto twin = std::make_unique<Client::World::Particles>();
    const glm::vec3 red{std::bit_cast<float>(0x3f333333u), std::bit_cast<float>(0x3e99999au),
                        std::bit_cast<float>(0x3e99999au)};
    struct Expected
    {
        std::int32_t type;
        std::int32_t subtype;
        glm::vec3 light;
        float scale;
    };
    const std::array expected{Expected{0x5f2, 0x22, glm::vec3(1.0f), 1.0f},
                              Expected{0x5f2, 0x23, glm::vec3(1.0f), 1.0f}, Expected{0x5c6, 0xa, red, 4.0f}};
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        SCOPED_TRACE(index);
        const Expected& want = expected[index];
        const auto replayed =
            twin->CreateChecked(want.type, moved, joint.angles, want.light, want.subtype, want.scale, 0.0f, replay);
        ASSERT_TRUE(replayed.HasValue() && replayed.Value() == std::optional<std::size_t>{index});
        const Client::World::Particle& made = viper.particles->All()[index];
        const Client::World::Particle& reference = twin->All()[index];
        ASSERT_TRUE(made.alive);
        EXPECT_EQ(made.type, want.type);
        EXPECT_EQ(made.subtype, want.subtype);
        EXPECT_EQ(made.position, moved);
        EXPECT_EQ(made.light, reference.light);
        EXPECT_EQ(made.scale, reference.scale);
        EXPECT_EQ(made.life, reference.life);
        EXPECT_EQ(made.rotation, reference.rotation);
        EXPECT_EQ(made.velocity, reference.velocity);
    }
    EXPECT_FALSE(viper.particles->All()[3].alive);
    static_cast<void>(replay.Next());
    EXPECT_EQ(viper.random.State(), replay.State());
}

// 0x537ef1..0x537f05 y 0x537f9b..0x537fbf: con la flecha muerta el joint se apaga y no suelta nada, pero sigue con el
// rand() de la luz; con menos de 15 de vida, la luz (1; 1; 0,2f) que dejó el paso anterior pasa por 0,8333333f después
// de las chispas. La /0xc no suelta nada.
TEST(ArrowTrailJointsTest, TheViperTrailFadesAndStopsWithItsArrow)
{
    Trail dying(0xe, 50.0f, 40);
    ASSERT_TRUE(dying.made);
    dying.owner.alive = false;
    ASSERT_TRUE(dying.Step().HasValue());
    EXPECT_FALSE(dying.joints[0].alive);
    EXPECT_FALSE(dying.sprites.Sprites()[0].alive);
    EXPECT_FALSE(dying.particles->All()[0].alive);
    CrtRandom single(Seed);
    static_cast<void>(single.Next());
    EXPECT_EQ(dying.random.State(), single.State());

    Trail fading(0xe, 50.0f, 40);
    ASSERT_TRUE(fading.made);
    fading.joints[0].life = 14;
    ASSERT_TRUE(fading.Step().HasValue());
    constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f555555u));
    EXPECT_EQ(fading.joints[0].light,
              (glm::vec3{F32(1.0 * Fade), F32(1.0 * Fade), F32(D(std::bit_cast<float>(0x3e4ccccdu)) * Fade)}));
    EXPECT_TRUE(fading.sprites.Sprites()[0].alive);

    Trail violet(0xc, 40.0f, 80);
    ASSERT_TRUE(violet.made);
    const glm::vec3 light = violet.joints[0].light;
    ASSERT_TRUE(violet.Step().HasValue());
    EXPECT_EQ(violet.joints[0].light, light);
    EXPECT_FALSE(violet.sprites.Sprites()[0].alive);
    EXPECT_FALSE(violet.particles->All()[0].alive);
    CrtRandom one(Seed);
    static_cast<void>(one.Next());
    EXPECT_EQ(violet.random.State(), one.State());
}

// 0x541a36..0x541a8c y 0x5467b8..0x5467e5: la 0x5f2/34 nace como la /35 (vida 30, la luz (1; 0,2f; 0,2f) y el giro
// rand() % 360) pero quieta (0x5435bb), y en su paso, como la /35, la luz por 0,9615385f y la escala + 0,06f.
TEST(ArrowTrailJointsTest, TheViperSmokeIsTheStillTwinOfTheExplosionSmoke)
{
    auto still = std::make_unique<Client::World::Particles>();
    auto falling = std::make_unique<Client::World::Particles>();
    CrtRandom first(Seed), second(Seed);
    const glm::vec3 at{100.0f, 200.0f, 300.0f};
    ASSERT_TRUE(still->CreateChecked(0x5f2, at, {}, glm::vec3(1.0f), 34, 1.0f, 0.0f, first).HasValue());
    ASSERT_TRUE(falling->CreateChecked(0x5f2, at, {}, glm::vec3(1.0f), 35, 1.0f, 0.0f, second).HasValue());
    const Client::World::Particle& smoke = still->All()[0];
    const Client::World::Particle& twin = falling->All()[0];
    ASSERT_TRUE(smoke.alive);
    EXPECT_EQ(smoke.life, 30);
    EXPECT_EQ(smoke.light, (glm::vec3{1.0f, 0.2f, 0.2f}));
    EXPECT_EQ(smoke.velocity, glm::vec3(0.0f));
    EXPECT_EQ(twin.velocity, (glm::vec3{0.0f, -5.0f, 0.0f}));
    EXPECT_EQ(smoke.rotation, twin.rotation);
    EXPECT_EQ(first.State(), second.State());

    ASSERT_TRUE(Client::World::StepParticlesChecked(*still, first).HasValue());
    ASSERT_TRUE(Client::World::StepParticlesChecked(*falling, second).HasValue());
    EXPECT_EQ(smoke.light, twin.light);
    EXPECT_EQ(Bits(smoke.light.x), Bits(F32(1.0 * D(std::bit_cast<float>(0x3f762763u)))));
    EXPECT_EQ(smoke.scale, twin.scale);
    EXPECT_EQ(Bits(smoke.scale), Bits(F32(1.0 + D(std::bit_cast<float>(0x3d75c28fu)))));
    EXPECT_EQ(smoke.life, twin.life);
    EXPECT_EQ(first.State(), second.State());
}
} // namespace
