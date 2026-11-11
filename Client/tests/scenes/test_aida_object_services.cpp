#include "Scenes/AidaObjectServices.h"

#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/OriginalRotation.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/WorldObjectsDrawing.h"
#include "World/KalimaParticleKernels.h"
#include "World/ModelEffects.h"
#include "World/ParticleOwner.h"
#include "World/Particles.h"
#include "World/SelectionParticleKernels.h"
#include "World/Sprites.h"
#include "World/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// 0x425260, el caso del dibujo de los objetos de los mapas 33 y 54 (06 § El mundo de la selección): los
// destellos de los huesos, las cascadas, las neblinas con dueño, la mariposa y la llama, con sus rand() y
// sus cortes.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::GameObjectPass;
using Client::Scenes::StepAidaObjectFrame;
using Client::World::Particle;
using Client::World::WorldObject;

constexpr std::uint8_t SelectionMap = 54;

struct Fixture
{
    std::unique_ptr<Client::World::Particles> particles = std::make_unique<Client::World::Particles>();
    std::unique_ptr<Client::World::FrameSprites> sprites = std::make_unique<Client::World::FrameSprites>();
    std::unique_ptr<Client::World::ModelEffects> effects = std::make_unique<Client::World::ModelEffects>();
    Client::Scenes::FaunaDrawCarry pose;
    CrtRandom random{7};
    std::uint32_t time = 0;

    Fixture()
    {
        for (int bone = 0; bone < 18; ++bone)
        {
            Client::Render::Models::BoneTransform matrix(1.0f);
            matrix[3] = glm::vec3(static_cast<float>(bone) * 10.0f, 1.0f, 2.0f);
            pose.bones.push_back(matrix);
        }
    }

    [[nodiscard]] GameObjectPass Pass()
    {
        GameObjectPass pass;
        pass.map = SelectionMap;
        pass.random = &random;
        pass.worldClock = 100.0f;
        pass.particles = particles.get();
        pass.sprites = sprites.get();
        pass.retainedPose = &pose;
        pass.effects = effects.get();
        pass.systemTime = [this] { return time; };
        pass.scene = 4;
        return pass;
    }

    [[nodiscard]] std::vector<const Particle*> Alive() const
    {
        std::vector<const Particle*> alive;
        for (const Particle& particle : particles->All())
        {
            if (particle.alive)
                alive.push_back(&particle);
        }
        return alive;
    }
};

WorldObject ObjectOf(std::int16_t type)
{
    WorldObject object;
    object.identity = 41;
    object.type = type;
    object.transform.scale = 1.25f;
    object.transform.position = {500.0f, 600.0f, 70.0f};
    object.transform.angles = {0.0f, 0.0f, 90.0f};
    return object;
}

glm::vec3 BonePoint(const Fixture& fixture, std::size_t bone, const glm::vec3& offset)
{
    return Client::Render::Models::OriginalVectorTransform(
        offset, Client::Render::Models::OriginalMatrixOf(fixture.pose.bones[bone]));
}

struct Flare
{
    std::size_t bone;
    glm::vec3 offset;
    float blueScale;
    float redScale;
};
constexpr std::array<Flare, 6> Flares{{
    {6, {0.0f, -3.0f, 1.0f}, 5.5f, 6.0f},
    {7, {0.0f, -3.0f, 0.0f}, 4.5f, 5.5f},
    {8, {0.0f, -3.0f, 0.0f}, 4.0f, 5.0f},
    {12, {-3.0f, -1.0f, 0.0f}, 4.5f, 5.5f},
    {13, {-3.0f, -3.0f, 0.0f}, 4.0f, 5.0f},
    {17, {0.0f, 2.0f, -8.0f}, 4.0f, 5.0f},
}};

// 0x425298..0x4255ee: dos 0x5c6 por hueso, el ancho con su escala y la luz (0,1; 0,1; 0,3), y el de
// escala 3 con 0,15; sin rand().
TEST(AidaObjectServicesTest, TheBlueGuardianLightsTwoSparksOnEachOfItsSixBones)
{
    Fixture fixture;
    WorldObject object = ObjectOf(0x1e);
    ASSERT_TRUE(StepAidaObjectFrame(object, fixture.Pass()).HasValue());
    const auto sprites = fixture.sprites->Sprites();
    const float tenth = std::bit_cast<float>(0x3dcccccdu);
    const float glow = std::bit_cast<float>(0x3e99999au);
    const float fifteenth = std::bit_cast<float>(0x3e19999au);
    for (std::size_t flare = 0; flare < Flares.size(); ++flare)
    {
        const auto& first = sprites[flare * 2];
        const auto& second = sprites[flare * 2 + 1];
        const glm::vec3 point = BonePoint(fixture, Flares[flare].bone, Flares[flare].offset);
        EXPECT_TRUE(first.alive);
        EXPECT_EQ(first.texture, 0x5c6);
        EXPECT_EQ(first.position, point);
        EXPECT_EQ(first.scale, Flares[flare].blueScale);
        EXPECT_EQ(first.light, glm::vec3(tenth, tenth, glow));
        EXPECT_EQ(second.position, point);
        EXPECT_EQ(second.scale, 3.0f);
        EXPECT_EQ(second.light, glm::vec3(fifteenth));
    }
    EXPECT_FALSE(sprites[12].alive);
    EXPECT_EQ(fixture.random.State(), 7u);
}

// 0x4259bf..0x425d1a: el rojo, (0,5; 0; 0) en el hueso 6 y (0,25; 0; 0) en los demás; el segundo, (0,15; 0;
// 0).
TEST(AidaObjectServicesTest, TheRedGuardianBurnsBrighterOnItsFirstBone)
{
    Fixture fixture;
    WorldObject object = ObjectOf(0x47);
    ASSERT_TRUE(StepAidaObjectFrame(object, fixture.Pass()).HasValue());
    const auto sprites = fixture.sprites->Sprites();
    const float fifteenth = std::bit_cast<float>(0x3e19999au);
    for (std::size_t flare = 0; flare < Flares.size(); ++flare)
    {
        EXPECT_EQ(sprites[flare * 2].scale, Flares[flare].redScale);
        EXPECT_EQ(sprites[flare * 2].light, glm::vec3(flare == 0 ? 0.5f : 0.25f, 0.0f, 0.0f));
        EXPECT_EQ(sprites[flare * 2 + 1].light, glm::vec3(fifteenth, 0.0f, 0.0f));
    }
}

// La .bss empieza en cero: un hueso que la pose global todavía no publicó deja el destello en el origen.
// Sin el buffer, corta en 0x4ec110 antes del primer sprite.
TEST(AidaObjectServicesTest, TheGuardianSparksReadTheRetainedPoseOrCutWithoutIt)
{
    Fixture fixture;
    fixture.pose.bones.resize(7);
    WorldObject object = ObjectOf(0x1e);
    ASSERT_TRUE(StepAidaObjectFrame(object, fixture.Pass()).HasValue());
    EXPECT_EQ(fixture.sprites->Sprites()[0].position, BonePoint(fixture, 6, Flares[0].offset));
    EXPECT_EQ(fixture.sprites->Sprites()[2].position, glm::vec3(0.0f));

    Fixture missing;
    GameObjectPass pass = missing.Pass();
    pass.retainedPose = nullptr;
    const auto cut = StepAidaObjectFrame(object, pass);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x4ec110"), std::string::npos);
    EXPECT_FALSE(missing.sprites->Any());
}

// 0x42566d: rand() & 1; con 0, la cascada 6f0/2 con luz blanca, la escala del objeto y sin dueño.
TEST(AidaObjectServicesTest, TheWaterfallFallsOnEvenDraws)
{
    int created = 0;
    int skipped = 0;
    for (const std::uint32_t seed : {1u, 2u, 3u, 4u, 5u, 6u})
    {
        Fixture fixture;
        fixture.random = CrtRandom(seed);
        CrtRandom draws(seed);
        const bool falls = (draws.Next() & 1) == 0;
        WorldObject object = ObjectOf(0x38);
        ASSERT_TRUE(StepAidaObjectFrame(object, fixture.Pass()).HasValue());
        const auto alive = fixture.Alive();
        if (!falls)
        {
            ++skipped;
            EXPECT_TRUE(alive.empty());
            EXPECT_EQ(fixture.random.State(), draws.State());
            continue;
        }
        ++created;
        ASSERT_EQ(alive.size(), 1u);
        EXPECT_EQ(alive[0]->type, Client::World::KalimaWaterfallFirst);
        EXPECT_EQ(alive[0]->subtype, 2);
        EXPECT_EQ(alive[0]->scale, static_cast<float>(1.25 + static_cast<double>(1.6f)));
        EXPECT_EQ(alive[0]->typedOwner, Client::World::ParticleOwner{});
    }
    EXPECT_GT(created, 0);
    EXPECT_GT(skipped, 0);
}

// 0x4256ae: el 0x39 salpica siempre (6f2/4); 0x4256ed: el 0x3a, con rand() % 3 en 0, la neblina 6f1/2.
TEST(AidaObjectServicesTest, TheSplashAlwaysEmitsAndTheMistOnAThirdOfTheDraws)
{
    Fixture splash;
    WorldObject object = ObjectOf(0x39);
    ASSERT_TRUE(StepAidaObjectFrame(object, splash.Pass()).HasValue());
    ASSERT_EQ(splash.Alive().size(), 1u);
    EXPECT_EQ(splash.Alive()[0]->type, Client::World::WaterfallSplashFirst);
    EXPECT_EQ(splash.Alive()[0]->subtype, 4);

    int created = 0;
    for (const std::uint32_t seed : {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u})
    {
        Fixture fixture;
        fixture.random = CrtRandom(seed);
        CrtRandom draws(seed);
        const bool emits = draws.Next() % 3 == 0;
        WorldObject mist = ObjectOf(0x3a);
        ASSERT_TRUE(StepAidaObjectFrame(mist, fixture.Pass()).HasValue());
        ASSERT_EQ(fixture.Alive().size(), emits ? 1u : 0u) << seed;
        if (emits)
        {
            ++created;
            EXPECT_EQ(fixture.Alive()[0]->type, Client::World::KalimaWaterfallMist);
            EXPECT_EQ(fixture.Alive()[0]->subtype, 2);
            EXPECT_EQ(fixture.Alive()[0]->life, 40);
            EXPECT_EQ(fixture.Alive()[0]->light, glm::vec3(0.25f));
        }
    }
    EXPECT_GT(created, 0);
}

// 0x425740..0x425785: veinte 622/1 con el objeto de dueño y la luz del tipo, salvo con +0x58 en −2.
TEST(AidaObjectServicesTest, TheMistsEmitTwentyOwnedCloudsUntilTheirOwnerIsHidden)
{
    const std::array<std::pair<std::int16_t, glm::vec3>, 3> mists{{
        {0x3b,
         {std::bit_cast<float>(0x3c23d70au), std::bit_cast<float>(0x3cf5c28fu), std::bit_cast<float>(0x3d4ccccdu)}},
        {std::int16_t{0x3e}, glm::vec3(std::bit_cast<float>(0x3d4ccccdu))},
        {0x3f,
         {std::bit_cast<float>(0x3d4ccccdu), std::bit_cast<float>(0x3ca3d70au), std::bit_cast<float>(0x3ca3d70au)}},
    }};
    for (const auto& [type, light] : mists)
    {
        Fixture fixture;
        WorldObject object = ObjectOf(type);
        ASSERT_TRUE(StepAidaObjectFrame(object, fixture.Pass()).HasValue());
        const auto alive = fixture.Alive();
        ASSERT_EQ(alive.size(), 20u);
        for (const Particle* particle : alive)
        {
            EXPECT_EQ(particle->type, Client::World::CloudParticle);
            EXPECT_EQ(particle->subtype, 1);
            EXPECT_EQ(particle->light, light);
            EXPECT_EQ(particle->typedOwner,
                      (Client::World::ParticleOwner{Client::World::ParticleOwnerKind::WorldObject, 41}));
        }

        Fixture hidden;
        object.state.hiddenMesh = Client::World::HiddenObject;
        ASSERT_TRUE(StepAidaObjectFrame(object, hidden.Pass()).HasValue());
        EXPECT_TRUE(hidden.Alive().empty());
        EXPECT_EQ(hidden.random.State(), 7u);
    }
}

// 0x425790..0x4257dd: con timeGetTime() & 0x3ff debajo de 10 sale la mariposa 0xae /3 (oculta en la
// escena 4); salga o no, el objeto queda en −2.
TEST(AidaObjectServicesTest, TheButterflyFliesOnlyInTheFirstTenMillisecondsOfEachSecondWindow)
{
    for (const std::uint32_t time : {0x400u + 9u, 0x400u + 10u})
    {
        Fixture fixture;
        fixture.time = time;
        WorldObject object = ObjectOf(0x3c);
        ASSERT_TRUE(StepAidaObjectFrame(object, fixture.Pass()).HasValue());
        EXPECT_EQ(object.state.hiddenMesh, Client::World::HiddenObject);
        const auto& effect = fixture.effects->Effects()[0];
        if (time == 0x400u + 10u)
        {
            EXPECT_FALSE(effect.alive);
            EXPECT_EQ(fixture.random.State(), 7u);
            continue;
        }
        CrtRandom draws(7);
        ASSERT_TRUE(effect.alive);
        EXPECT_EQ(effect.type, Client::World::ButterflyEffect);
        EXPECT_EQ(effect.subtype, 3);
        EXPECT_EQ(effect.objectState.hiddenMesh, Client::World::HiddenObject);
        EXPECT_EQ(effect.life, 250);
        EXPECT_EQ(effect.state, static_cast<std::uint8_t>(draws.Next() & 1));
        EXPECT_EQ(effect.velocity, glm::vec3(0.0f, static_cast<float>(1.25 * -5.0), 0.0f));
        EXPECT_EQ(effect.transform.scale, std::bit_cast<float>(0x3f666666u));
        EXPECT_EQ(effect.transform.position, object.transform.position);
        EXPECT_EQ(fixture.random.State(), draws.State());
    }

    Fixture clockless;
    GameObjectPass pass = clockless.Pass();
    pass.systemTime = {};
    WorldObject object = ObjectOf(0x3c);
    const auto cut = StepAidaObjectFrame(object, pass);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x425790"), std::string::npos);
}

// 0x4258ff..0x42592e: la luz del objeto en 0,3 y la llama 5de/7 con esa luz.
TEST(AidaObjectServicesTest, TheBrazierLightsAtAThirdAndRaisesAFlame)
{
    Fixture fixture;
    WorldObject object = ObjectOf(0x43);
    ASSERT_TRUE(StepAidaObjectFrame(object, fixture.Pass()).HasValue());
    const glm::vec3 light(std::bit_cast<float>(0x3e99999au));
    EXPECT_EQ(object.light, light);
    const auto alive = fixture.Alive();
    ASSERT_EQ(alive.size(), 1u);
    EXPECT_EQ(alive[0]->type, Client::World::FlameParticle);
    EXPECT_EQ(alive[0]->subtype, 7);
    EXPECT_EQ(alive[0]->light, light);
    EXPECT_EQ(alive[0]->life, 33);
}

// 0x42593f..0x4259ae: el 0xb3 del 0x46 sale con rand() % 5 en 1 y el reloj debajo de 30, con la luz 0,1f; la
// creación (0x51900b) saca sus seis rand() y deja su propia luz.
TEST(AidaObjectServicesTest, TheWispLeavesOnlyWhenTheDrawAndTheClockAllowIt)
{
    int created = 0;
    for (const std::uint32_t seed : {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u})
    {
        for (const std::uint32_t time : {29u, 30u})
        {
            Fixture fixture;
            fixture.random = CrtRandom(seed);
            fixture.time = time;
            CrtRandom draws(seed);
            const bool creates = draws.Next() % 5 == 1 && time < 30;
            WorldObject object = ObjectOf(0x46);
            ASSERT_TRUE(StepAidaObjectFrame(object, fixture.Pass()).HasValue());
            const auto& effect = fixture.effects->Effects()[0];
            EXPECT_EQ(effect.alive, creates);
            if (creates)
            {
                ++created;
                for (int draw = 0; draw < 6; ++draw)
                    static_cast<void>(draws.Next());
                EXPECT_EQ(effect.type, Client::World::AidaWispEffect);
                EXPECT_EQ(effect.light, glm::vec3(0.01f, 0.01f, std::bit_cast<float>(0x3cf5c28fu)));
                EXPECT_EQ(effect.life, 1000);
            }
            EXPECT_EQ(fixture.random.State(), draws.State());
        }
    }
    EXPECT_GT(created, 0);

    Fixture poolless;
    GameObjectPass pass = poolless.Pass();
    pass.effects = nullptr;
    for (std::uint32_t seed = 1;; ++seed)
    {
        CrtRandom probe(seed);
        if (probe.Next() % 5 == 1)
        {
            poolless.random = CrtRandom(seed);
            break;
        }
    }
    WorldObject object = ObjectOf(0x46);
    const auto cut = StepAidaObjectFrame(object, pass);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x4259ae"), std::string::npos);
}

// El 0x29 y el 0x41/0x42 sólo dibujan (DrawAidaMeshPass): acá no sacan rand() ni crean nada, como los tipos
// sin caso.
TEST(AidaObjectServicesTest, TheRedrawnBodiesAndTheOtherTypesDoNothingHere)
{
    for (const std::int16_t type : {std::int16_t{0x29}, std::int16_t{0x41}, std::int16_t{0x42}, std::int16_t{0x10}})
    {
        Fixture fixture;
        WorldObject object = ObjectOf(type);
        const WorldObject before = object;
        ASSERT_TRUE(StepAidaObjectFrame(object, fixture.Pass()).HasValue());
        EXPECT_TRUE(fixture.Alive().empty());
        EXPECT_FALSE(fixture.sprites->Any());
        EXPECT_FALSE(fixture.effects->Effects()[0].alive);
        EXPECT_EQ(fixture.random.State(), 7u);
        EXPECT_EQ(object.state, before.state);
        EXPECT_EQ(object.light, before.light);
    }
}
} // namespace
