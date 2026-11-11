#include "Scenes/LandOfTrialsObjectServices.h"

#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Render/Models/OriginalRotation.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/WorldObjectsDrawing.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include "World/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// 0x4401e0 y 0x4403b0, el frame y el dibujo de los objetos de Land of Trials (el mapa 31): 06 § Los objetos
// de Land of Trials.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::GameObjectPass;
using Client::Scenes::LandOfTrialsBody;
using Client::Scenes::StepLandOfTrialsObjectFrame;
using Client::World::Particle;
using Client::World::WorldObject;

constexpr std::uint8_t TrialsMap = 31;

struct Fixture
{
    std::unique_ptr<Client::World::Particles> particles = std::make_unique<Client::World::Particles>();
    std::unique_ptr<Client::World::FrameSprites> sprites = std::make_unique<Client::World::FrameSprites>();
    Client::Scenes::FaunaDrawCarry pose;
    CrtRandom random{7};

    Fixture()
    {
        for (int bone = 0; bone < 6; ++bone)
        {
            Client::Render::Models::BoneTransform matrix(1.0f);
            matrix[3] = glm::vec3(static_cast<float>(bone) * 10.0f, 1.0f, 2.0f);
            pose.bones.push_back(matrix);
        }
    }

    [[nodiscard]] GameObjectPass Pass()
    {
        GameObjectPass pass;
        pass.map = TrialsMap;
        pass.random = &random;
        pass.worldClock = 2500.0f;
        pass.particles = particles.get();
        pass.sprites = sprites.get();
        pass.retainedPose = &pose;
        pass.scene = 5;
        return pass;
    }

    [[nodiscard]] std::vector<const Particle*> Alive() const
    {
        std::vector<const Particle*> alive;
        for (const Particle& particle : particles->All())
            if (particle.alive)
                alive.push_back(&particle);
        return alive;
    }

    [[nodiscard]] std::vector<const Client::World::Sprite*> Sprites() const
    {
        std::vector<const Client::World::Sprite*> alive;
        for (const Client::World::Sprite& sprite : sprites->Sprites())
            if (sprite.alive)
                alive.push_back(&sprite);
        return alive;
    }
};

WorldObject ObjectOf(std::int16_t type)
{
    WorldObject object;
    object.identity = 12;
    object.type = type;
    object.transform.scale = 1.25f;
    object.transform.position = {500.0f, 600.0f, 70.0f};
    object.transform.angles = {0.0f, 0.0f, 90.0f};
    return object;
}

// La semilla cuyo primer rand() % 3 da `remainder`.
std::uint32_t SeedWith(int remainder)
{
    for (std::uint32_t seed = 1;; ++seed)
    {
        CrtRandom probe(seed);
        if (probe.Next() % 3 == remainder)
            return seed;
    }
}
} // namespace

// 0x440355 y 0x440212: con rand() % 3 en 0, el 3 suelta la salpicadura 6f2/3 y el 0x35 el humo 5f2/22, sobre
// el objeto, con sus ángulos, luz (1, 1, 1) y su escala; con otro resto, nada más que ese rand().
TEST(LandOfTrialsObjectServicesTest, TheEmittersReleaseTheirParticleOnceInThree)
{
    struct Case
    {
        std::int16_t type;
        std::int32_t particle;
        std::int32_t subtype;
        int draws;
    };
    for (const Case c : {Case{3, 0x6f2, 3, 4}, Case{0x35, 0x5f2, 22, 3}})
    {
        SCOPED_TRACE(c.type);
        for (const int remainder : {0, 1, 2})
        {
            Fixture fixture;
            fixture.random = CrtRandom(SeedWith(remainder));
            CrtRandom draws = fixture.random;
            const auto object = ObjectOf(c.type);
            ASSERT_TRUE(StepLandOfTrialsObjectFrame(object, fixture.Pass()).HasValue());
            static_cast<void>(draws.Next());
            const auto alive = fixture.Alive();
            if (remainder != 0)
            {
                EXPECT_TRUE(alive.empty());
                EXPECT_EQ(fixture.random.State(), draws.State());
                continue;
            }
            ASSERT_EQ(alive.size(), 1u);
            EXPECT_EQ(alive[0]->type, c.particle);
            EXPECT_EQ(alive[0]->subtype, c.subtype);
            EXPECT_EQ(alive[0]->start, object.transform.position);
            EXPECT_EQ(alive[0]->angles, object.transform.angles);
            EXPECT_EQ(alive[0]->light, glm::vec3(1.0f));
            for (int k = 0; k < c.draws; ++k)
                static_cast<void>(draws.Next());
            EXPECT_EQ(fixture.random.State(), draws.State());
        }
    }
    EXPECT_TRUE(Fixture().Sprites().empty());
}

// 0x440263..0x440346: en el hueso 3 de la pose global, 10 más abajo en la y: un 0x5ac rojo de escala
// ((sin(reloj · 0,002f) · 0,35f + 0,65f) · 1,2f + 0,3f), otro de 0,5 y un 0x5fd gris 0,3 de 1,2 girado
// rand() % 360, el único rand() del caso.
TEST(LandOfTrialsObjectServicesTest, TheBrazierLeavesTwoRedFlaresAndATurnedShineOnItsBone)
{
    Fixture fixture;
    CrtRandom draws = fixture.random;
    const auto object = ObjectOf(0x31);
    ASSERT_TRUE(StepLandOfTrialsObjectFrame(object, fixture.Pass()).HasValue());
    const glm::vec3 point = Client::Render::Models::OriginalVectorTransform(
        glm::vec3(0.0f, -10.0f, 0.0f), Client::Render::Models::OriginalMatrixOf(fixture.pose.bones[3]));
    const auto pulse = Client::Render::Models::Detail::SineProductPC53(
        2500.0 * static_cast<double>(std::bit_cast<float>(0x3b03126fu)), std::bit_cast<float>(0x3eb33333u));
    ASSERT_TRUE(pulse.has_value());
    const float scale = static_cast<float>(((*pulse + static_cast<double>(0.65f)) * static_cast<double>(1.2f)) +
                                           static_cast<double>(0.3f));
    const auto sprites = fixture.Sprites();
    ASSERT_EQ(sprites.size(), 3u);
    EXPECT_EQ(sprites[0]->texture, 0x5ac);
    EXPECT_EQ(sprites[0]->position, point);
    EXPECT_EQ(sprites[0]->scale, scale);
    EXPECT_EQ(sprites[0]->light, glm::vec3(1.0f, 0.0f, 0.0f));
    EXPECT_EQ(sprites[1]->texture, 0x5ac);
    EXPECT_EQ(sprites[1]->scale, 0.5f);
    EXPECT_EQ(sprites[1]->light, glm::vec3(1.0f, 0.0f, 0.0f));
    EXPECT_EQ(sprites[2]->texture, 0x5fd);
    EXPECT_EQ(sprites[2]->position, point);
    EXPECT_EQ(sprites[2]->scale, 1.2f);
    EXPECT_EQ(sprites[2]->light, glm::vec3(0.3f));
    EXPECT_EQ(sprites[2]->rotation, static_cast<float>(draws.Next() % 360));
    EXPECT_EQ(fixture.random.State(), draws.State());
    EXPECT_TRUE(fixture.Alive().empty());
}

// Los demás tipos no hacen nada en el frame. Sin pose o sin sprites el 0x31 corta, y sin rand() corta todo.
TEST(LandOfTrialsObjectServicesTest, TheOtherTypesDoNothingAndMissingServicesCut)
{
    for (const std::int16_t type : {std::int16_t{1}, std::int16_t{0xa}, std::int16_t{0x1b}, std::int16_t{0x2a}})
    {
        Fixture fixture;
        const CrtRandom before = fixture.random;
        ASSERT_TRUE(StepLandOfTrialsObjectFrame(ObjectOf(type), fixture.Pass()).HasValue());
        EXPECT_EQ(fixture.random.State(), before.State());
        EXPECT_TRUE(fixture.Alive().empty());
        EXPECT_TRUE(fixture.Sprites().empty());
    }
    {
        Fixture fixture;
        auto pass = fixture.Pass();
        pass.retainedPose = nullptr;
        EXPECT_FALSE(StepLandOfTrialsObjectFrame(ObjectOf(0x31), pass).HasValue());
    }
    {
        Fixture fixture;
        auto pass = fixture.Pass();
        pass.sprites = nullptr;
        const auto cut = StepLandOfTrialsObjectFrame(ObjectOf(0x31), pass);
        ASSERT_FALSE(cut.HasValue());
        EXPECT_NE(cut.Error().find("0x4402e0"), std::string::npos) << cut.Error();
    }
    {
        Fixture fixture;
        auto pass = fixture.Pass();
        pass.random = nullptr;
        EXPECT_FALSE(StepLandOfTrialsObjectFrame(ObjectOf(3), pass).HasValue());
    }
    {
        Fixture fixture;
        fixture.random = CrtRandom(SeedWith(0));
        auto pass = fixture.Pass();
        pass.particles = nullptr;
        const auto cut = StepLandOfTrialsObjectFrame(ObjectOf(0x35), pass);
        ASSERT_FALSE(cut.HasValue());
        EXPECT_NE(cut.Error().find("0x440254"), std::string::npos) << cut.Error();
    }
}

// 0x4404a2..0x4404ec y 0x440432..0x440467: el 0x1b y el 0x36 multiplican el color del modelo por
// sin(reloj · 0,0012f + fase) · 0,5f + 0,9f; el 0x34 por el mismo pulso con 0,0009f, y se dibuja dos veces
// (Q-C317). 0x4403e9: el 0xa, (0,56; 0,8; 0,81). Los demás, sin caso.
TEST(LandOfTrialsObjectServicesTest, TheBodiesPulseWithTheClockAndTheirPhase)
{
    const glm::vec3 color{0.7f, 0.45f, 0.3f};
    const float clock = 98765.0f;
    struct Case
    {
        std::int16_t type;
        std::uint32_t rate;
        bool twice;
    };
    for (const Case c : {Case{0x1b, 0x3a9d4952u, false}, Case{0x36, 0x3a9d4952u, false}, Case{0x34, 0x3a6bedfau, true}})
    {
        SCOPED_TRACE(c.type);
        auto object = ObjectOf(c.type);
        object.field84 = 4.5f;
        const auto body = LandOfTrialsBody(object, color, clock);
        ASSERT_TRUE(body.HasValue());
        ASSERT_TRUE(body.Value().has_value());
        const double argument = static_cast<double>(clock) * static_cast<double>(std::bit_cast<float>(c.rate)) + 4.5;
        const auto pulse = Client::Render::Models::Detail::SineProductPC53(argument, 0.5f);
        ASSERT_TRUE(pulse.has_value());
        const double level = *pulse + static_cast<double>(0.9f);
        EXPECT_EQ(body.Value()->color, glm::vec3(static_cast<float>(level * static_cast<double>(color.x)),
                                                 static_cast<float>(level * static_cast<double>(color.y)),
                                                 static_cast<float>(level * static_cast<double>(color.z))));
        EXPECT_EQ(body.Value()->twice, c.twice);
    }
    const auto fixed = LandOfTrialsBody(ObjectOf(0xa), color, clock);
    ASSERT_TRUE(fixed.HasValue() && fixed.Value().has_value());
    EXPECT_EQ(fixed.Value()->color, glm::vec3(0.56f, 0.8f, 0.81f));
    EXPECT_FALSE(fixed.Value()->twice);
    for (const std::int16_t type : {std::int16_t{1}, std::int16_t{3}, std::int16_t{0x31}, std::int16_t{0x35}})
    {
        const auto none = LandOfTrialsBody(ObjectOf(type), color, clock);
        ASSERT_TRUE(none.HasValue());
        EXPECT_FALSE(none.Value().has_value());
    }
}
