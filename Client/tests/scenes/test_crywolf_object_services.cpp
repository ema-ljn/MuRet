#include "Scenes/CrywolfObjectServices.h"

#include "Data/Models/Model.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/Skeleton.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/WorldObjectsDrawing.h"
#include "Scenes/WorldRuntime.h"
#include "World/KalimaParticleKernels.h"
#include "World/ModelEffects.h"
#include "World/ParticleOwner.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include "World/TerrainCulling.h"
#include "World/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

// 0x430510 y 0x4306a0, el frame y el dibujo de los objetos de Crywolf (el mapa 34), con el estado 0 de la .bss y los
// demás que solo ponen los paquetes: 06 § Los objetos de Crywolf.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::CrywolfGlobals;
using Client::Scenes::CrywolfObjectBody;
using Client::Scenes::GameObjectPass;
using Client::Scenes::StepCrywolfObjectFrame;
using Client::World::ObjectDraw;
using Client::World::ObjectDrawKind;
using Client::World::Particle;
using Client::World::WorldObject;

constexpr std::uint8_t CrywolfMap = 34;

struct Fixture
{
    std::unique_ptr<Client::World::Particles> particles = std::make_unique<Client::World::Particles>();
    std::unique_ptr<Client::World::FrameSprites> sprites = std::make_unique<Client::World::FrameSprites>();
    std::unique_ptr<Client::World::ModelEffects> effects = std::make_unique<Client::World::ModelEffects>();
    CrywolfGlobals crywolf;
    std::int32_t drawCounter = 1;
    CrtRandom random{7};

    [[nodiscard]] GameObjectPass Pass()
    {
        GameObjectPass pass;
        pass.map = CrywolfMap;
        pass.random = &random;
        pass.worldClock = 2500.0f;
        pass.particles = particles.get();
        pass.sprites = sprites.get();
        pass.effects = effects.get();
        pass.objectDrawCounter = &drawCounter;
        pass.crywolf = &crywolf;
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

    [[nodiscard]] std::vector<const Client::World::ModelEffect*> Effects() const
    {
        std::vector<const Client::World::ModelEffect*> alive;
        for (const Client::World::ModelEffect& effect : effects->Effects())
            if (effect.alive)
                alive.push_back(&effect);
        return alive;
    }
};

WorldObject ObjectOf(std::int16_t type)
{
    WorldObject object;
    object.identity = 12;
    object.type = type;
    object.transform.scale = 1.25f;
    object.transform.position = {12550.0f, 4250.0f, 170.0f};
    object.transform.angles = {0.0f, 0.0f, 90.0f};
    return object;
}

ObjectDraw BodyDraw()
{
    ObjectDraw draw;
    draw.kind = ObjectDrawKind::Body;
    draw.posed = true;
    draw.color = glm::vec3(0.5f, 0.25f, 0.75f);
    return draw;
}

// La primera semilla cuyo primer rand() % `modulus` da `remainder`, y con un segundo rand() % `second` en
// `secondRemainder` si se pide.
std::uint32_t SeedWith(int modulus, int remainder, int second = 0, int secondRemainder = 0)
{
    for (std::uint32_t seed = 1;; ++seed)
    {
        CrtRandom probe(seed);
        if (probe.Next() % modulus != remainder)
            continue;
        if (second != 0 && probe.Next() % second != secondRemainder)
            continue;
        return seed;
    }
}

float F32(double value)
{
    return static_cast<float>(value);
}

// Una biblioteca sin modelos: el dibujo de 0x6046a0 solo cuenta y no dibuja.
struct NoModels final : Client::Scenes::IModelLibrary
{
    const Client::Data::Models::Model* Find(std::uint16_t) const override
    {
        return nullptr;
    }
    const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t) const override
    {
        return nullptr;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return {};
    }
};
} // namespace

// 0x430523..0x430561: cada objeto suma 1 a 0x723c0c; al pasar de 70.000, un rand() lo vuelve a 0 y el clima
// 0x5763358 queda en rand() % 3. Antes, ningún rand().
TEST(CrywolfObjectServicesTest, TheWeatherIsDrawnOnceEverySeventyThousandAndOneObjectFrames)
{
    Fixture fixture;
    fixture.crywolf.frameCount = 69999;
    const auto other = ObjectOf(0x25);
    CrtRandom draws = fixture.random;
    ASSERT_TRUE(StepCrywolfObjectFrame(other, fixture.Pass()).HasValue());
    EXPECT_EQ(fixture.crywolf.frameCount, 70000);
    EXPECT_EQ(fixture.crywolf.weather, 0);
    EXPECT_EQ(fixture.random.State(), draws.State());

    for (std::uint32_t seed = 1; seed <= 3; ++seed)
    {
        SCOPED_TRACE(seed);
        fixture.random = CrtRandom(seed);
        CrtRandom oracle(seed);
        fixture.crywolf.frameCount = 70000;
        ASSERT_TRUE(StepCrywolfObjectFrame(other, fixture.Pass()).HasValue());
        EXPECT_EQ(fixture.crywolf.weather, oracle.Next() % 3);
        EXPECT_EQ(fixture.crywolf.frameCount, 0);
        EXPECT_EQ(fixture.random.State(), oracle.State());
    }
}

// 0x4305db..0x43068c: el 0x29 gira 1 el brillo (en 360 vuelve a 0, comparando la suma de la x87) y deja dos
// sprites 0x60f de 1,3 en blanco a (x, y, 350 + cuenta + z), con la cuenta de 0x6046a0 o 40 menos ella pasados 20:
// el primero sin giro y el segundo con el giro nuevo. Sin rand().
TEST(CrywolfObjectServicesTest, TheGlowLeavesTwoSpritesThatRiseAndFallWithTheDrawCounter)
{
    struct Case
    {
        std::int32_t counter;
        float rotation;
        float lift;
        float turned;
    };
    for (const Case c : {Case{7, 10.0f, 7.0f, 11.0f}, Case{20, 0.0f, 20.0f, 1.0f}, Case{21, 358.5f, 19.0f, 359.5f},
                         Case{41, 359.0f, -1.0f, 0.0f}})
    {
        SCOPED_TRACE(c.counter);
        Fixture fixture;
        fixture.drawCounter = c.counter;
        fixture.crywolf.glowRotation = c.rotation;
        CrtRandom draws = fixture.random;
        const auto object = ObjectOf(0x29);
        ASSERT_TRUE(StepCrywolfObjectFrame(object, fixture.Pass()).HasValue());
        EXPECT_EQ(fixture.crywolf.glowRotation, c.turned);
        EXPECT_EQ(fixture.random.State(), draws.State());
        const auto sprites = fixture.Sprites();
        ASSERT_EQ(sprites.size(), 2u);
        const glm::vec3 point(12550.0f, 4250.0f, F32((static_cast<double>(c.lift) + 350.0) + 170.0));
        for (const auto* sprite : sprites)
        {
            EXPECT_EQ(sprite->texture, 0x60f);
            EXPECT_EQ(sprite->position, point);
            EXPECT_EQ(sprite->scale, std::bit_cast<float>(0x3fa66666u));
            EXPECT_EQ(sprite->light, glm::vec3(1.0f));
        }
        EXPECT_EQ(sprites[0]->rotation, 0.0f);
        EXPECT_EQ(sprites[1]->rotation, c.turned);
    }
}

// 0x43057d..0x4305cc: el 0x54 saca un rand() y, en 0 módulo 3 solo con el estado de Crywolf en 1, suelta el humo
// 5f2/21 con luz (1, 1, 1), su escala y sin dueño. Con el estado 0 de la .bss, solo el rand().
TEST(CrywolfObjectServicesTest, TheChimneySmokesOnlyWithTheEventStateOne)
{
    for (const std::uint8_t state : {std::uint8_t{0}, std::uint8_t{1}})
    {
        for (const int remainder : {0, 1})
        {
            SCOPED_TRACE(std::to_string(state) + "/" + std::to_string(remainder));
            Fixture fixture;
            fixture.crywolf.state = state;
            fixture.random = CrtRandom(SeedWith(3, remainder));
            CrtRandom draws = fixture.random;
            const auto object = ObjectOf(0x54);
            ASSERT_TRUE(StepCrywolfObjectFrame(object, fixture.Pass()).HasValue());
            static_cast<void>(draws.Next());
            const auto alive = fixture.Alive();
            if (state != 1 || remainder != 0)
            {
                EXPECT_TRUE(alive.empty());
                EXPECT_EQ(fixture.random.State(), draws.State());
                continue;
            }
            ASSERT_EQ(alive.size(), 1u);
            EXPECT_EQ(alive[0]->type, 0x5f2);
            EXPECT_EQ(alive[0]->subtype, 21);
            EXPECT_EQ(alive[0]->light, glm::vec3(1.0f));
            EXPECT_EQ(alive[0]->owner, 0u);
            // 0x542370: tres rand() más.
            static_cast<void>(draws.Next());
            static_cast<void>(draws.Next());
            static_cast<void>(draws.Next());
            EXPECT_EQ(fixture.random.State(), draws.State());
        }
    }
}

// 0x4306db y 0x430b23: el 0x24 dibuja con el color del modelo en 0,4 y el 0xfa en (0; 0; 0,9); el 0x29 con el
// que trae (su luz de 0x4307d5 no se usa). Ninguno saca rand().
TEST(CrywolfObjectServicesTest, TheBodiesWithAColorOfTheirOwnDoNotDraw)
{
    Fixture fixture;
    CrtRandom draws = fixture.random;
    for (const std::int16_t type : {std::int16_t{0x24}, std::int16_t{0xfa}, std::int16_t{0x29}, std::int16_t{0x25}})
    {
        SCOPED_TRACE(type);
        ObjectDraw draw = BodyDraw();
        ASSERT_TRUE(CrywolfObjectBody(ObjectOf(type), draw, fixture.Pass()).HasValue());
        EXPECT_EQ(draw.kind, ObjectDrawKind::Body);
        const glm::vec3 expected = type == 0x24   ? glm::vec3(std::bit_cast<float>(0x3ecccccdu))
                                   : type == 0xfa ? glm::vec3(0.0f, 0.0f, std::bit_cast<float>(0x3f666666u))
                                                  : glm::vec3(0.5f, 0.25f, 0.75f);
        EXPECT_EQ(draw.color, expected);
    }
    EXPECT_EQ(fixture.random.State(), draws.State());
    EXPECT_TRUE(fixture.Alive().empty());
}

// 0x430746..0x4307a3: el 0x38 se dibuja y, con el estado 0, saca la luz (rand() % 10.000 · 0,0001f, en los tres
// canales) y, con otro rand() % 3 en 0, crea el 0x61d/3 con esa luz, sus ángulos y 4 en +0x8a: vida 20 y escala 0,5.
// En un dibujo que no es del original no saca nada; con el estado 1, tampoco.
TEST(CrywolfObjectServicesTest, TheBannerDropsAFadingDecalOnceInThreeWithTheStartingState)
{
    for (const int remainder : {0, 2})
    {
        SCOPED_TRACE(remainder);
        Fixture fixture;
        // El primer rand() es la luz: el segundo decide.
        for (std::uint32_t seed = 1;; ++seed)
        {
            CrtRandom probe(seed);
            static_cast<void>(probe.Next());
            if (probe.Next() % 3 == remainder)
            {
                fixture.random = CrtRandom(seed);
                break;
            }
        }
        CrtRandom draws = fixture.random;
        ObjectDraw draw = BodyDraw();
        const auto object = ObjectOf(0x38);
        ASSERT_TRUE(CrywolfObjectBody(object, draw, fixture.Pass()).HasValue());
        EXPECT_EQ(draw.kind, ObjectDrawKind::Body);
        const float level =
            F32(static_cast<double>(draws.Next() % 10000) * static_cast<double>(std::bit_cast<float>(0x38d1b717u)));
        static_cast<void>(draws.Next());
        EXPECT_EQ(fixture.random.State(), draws.State());
        const auto effects = fixture.Effects();
        if (remainder != 0)
        {
            EXPECT_TRUE(effects.empty());
            continue;
        }
        ASSERT_EQ(effects.size(), 1u);
        EXPECT_EQ(effects[0]->type, Client::World::FadingDecalEffect);
        EXPECT_EQ(effects[0]->subtype, 3);
        EXPECT_EQ(effects[0]->light, glm::vec3(level));
        EXPECT_EQ(effects[0]->transform.position, object.transform.position);
        EXPECT_EQ(effects[0]->transform.angles, object.transform.angles);
        EXPECT_EQ(effects[0]->disturbanceState, 4);
        EXPECT_EQ(effects[0]->life, 20);
        EXPECT_EQ(effects[0]->transform.scale, 0.5f);
    }
    {
        Fixture fixture;
        auto pass = fixture.Pass();
        pass.originalFrame = false;
        CrtRandom draws = fixture.random;
        ObjectDraw draw = BodyDraw();
        ASSERT_TRUE(CrywolfObjectBody(ObjectOf(0x38), draw, pass).HasValue());
        fixture.crywolf.state = 1;
        ASSERT_TRUE(CrywolfObjectBody(ObjectOf(0x38), draw, fixture.Pass()).HasValue());
        EXPECT_EQ(fixture.random.State(), draws.State());
        EXPECT_TRUE(fixture.Effects().empty());
    }
}

// 0x4307f7..0x4308a5: el 0x39 y el 0x47 salen sin cuerpo; con rand() % 3 en 0, la chispa 6ec/5 (vida 20 y dos
// rand()) y, el 0x39, además el humo 5f2/21 (tres rand()), con luz (1, 1, 1), su escala y sin dueño.
TEST(CrywolfObjectServicesTest, TheTorchesHideTheirBodyAndSparkOnceInThree)
{
    for (const std::int16_t type : {std::int16_t{0x39}, std::int16_t{0x47}})
    {
        for (const int remainder : {0, 1})
        {
            SCOPED_TRACE(std::to_string(type) + "/" + std::to_string(remainder));
            Fixture fixture;
            fixture.random = CrtRandom(SeedWith(3, remainder));
            CrtRandom draws = fixture.random;
            ObjectDraw draw = BodyDraw();
            const auto object = ObjectOf(type);
            ASSERT_TRUE(CrywolfObjectBody(object, draw, fixture.Pass()).HasValue());
            EXPECT_EQ(draw.kind, ObjectDrawKind::Hidden);
            static_cast<void>(draws.Next());
            const auto alive = fixture.Alive();
            if (remainder != 0)
            {
                EXPECT_TRUE(alive.empty());
                EXPECT_EQ(fixture.random.State(), draws.State());
                continue;
            }
            ASSERT_EQ(alive.size(), type == 0x39 ? 2u : 1u);
            EXPECT_EQ(alive[0]->type, Client::World::CrywolfFanta);
            EXPECT_EQ(alive[0]->subtype, 5);
            EXPECT_EQ(alive[0]->life, 20);
            EXPECT_EQ(alive[0]->scale, 1.25f);
            EXPECT_EQ(alive[0]->light, glm::vec3(1.0f));
            for (int fanta = 0; fanta < 2; ++fanta)
                static_cast<void>(draws.Next());
            if (type == 0x39)
            {
                EXPECT_EQ(alive[1]->type, 0x5f2);
                EXPECT_EQ(alive[1]->subtype, 21);
                for (int smoke = 0; smoke < 3; ++smoke)
                    static_cast<void>(draws.Next());
            }
            EXPECT_EQ(fixture.random.State(), draws.State());
        }
    }
}

// 0x4309bd..0x430a23: el 0x4d y el 0x4e salen sin cuerpo y, con rand() % 6 en 0, sueltan la nube 622/9 con su
// luz (0,02; 0,03; 0,02) y (0,03; 0,06; 0,05) y siete rand() de 0x54330a.
TEST(CrywolfObjectServicesTest, TheMistObjectsReleaseACloudOnceInSix)
{
    for (const std::int16_t type : {std::int16_t{0x4d}, std::int16_t{0x4e}})
    {
        SCOPED_TRACE(type);
        Fixture fixture;
        fixture.random = CrtRandom(SeedWith(6, 0));
        CrtRandom draws = fixture.random;
        ObjectDraw draw = BodyDraw();
        ASSERT_TRUE(CrywolfObjectBody(ObjectOf(type), draw, fixture.Pass()).HasValue());
        EXPECT_EQ(draw.kind, ObjectDrawKind::Hidden);
        for (int drawn = 0; drawn < 8; ++drawn)
            static_cast<void>(draws.Next());
        EXPECT_EQ(fixture.random.State(), draws.State());
        const auto alive = fixture.Alive();
        ASSERT_EQ(alive.size(), 1u);
        EXPECT_EQ(alive[0]->type, 0x622);
        EXPECT_EQ(alive[0]->subtype, 9);
        EXPECT_EQ(alive[0]->life, 300);
        const glm::vec3 light = type == 0x4d
                                    ? glm::vec3(std::bit_cast<float>(0x3ca3d70au), std::bit_cast<float>(0x3cf5c28fu),
                                                std::bit_cast<float>(0x3ca3d70au))
                                    : glm::vec3(std::bit_cast<float>(0x3cf5c28fu), std::bit_cast<float>(0x3d75c28fu),
                                                std::bit_cast<float>(0x3d4ccccdu));
        EXPECT_EQ(alive[0]->light, light);
    }
}

// 0x430942..0x430950 y 0x43095e: el 0x49 pone la arena del HUD en 1 con el clima en 0, y el 0x4a, con el estado
// 0, solo saca su rand(). Los dos, sin cuerpo.
TEST(CrywolfObjectServicesTest, TheSandAndTheQuietSmokeHideTheirBody)
{
    Fixture fixture;
    fixture.crywolf.weather = 1;
    ObjectDraw draw = BodyDraw();
    ASSERT_TRUE(CrywolfObjectBody(ObjectOf(0x49), draw, fixture.Pass()).HasValue());
    EXPECT_EQ(draw.kind, ObjectDrawKind::Hidden);
    EXPECT_EQ(fixture.crywolf.sandOverlay, 0);
    fixture.crywolf.weather = 0;
    draw = BodyDraw();
    ASSERT_TRUE(CrywolfObjectBody(ObjectOf(0x49), draw, fixture.Pass()).HasValue());
    EXPECT_EQ(fixture.crywolf.sandOverlay, 1);

    fixture.random = CrtRandom(SeedWith(3, 0));
    CrtRandom draws = fixture.random;
    draw = BodyDraw();
    ASSERT_TRUE(CrywolfObjectBody(ObjectOf(0x4a), draw, fixture.Pass()).HasValue());
    EXPECT_EQ(draw.kind, ObjectDrawKind::Hidden);
    static_cast<void>(draws.Next());
    EXPECT_EQ(fixture.random.State(), draws.State());
    EXPECT_TRUE(fixture.Alive().empty());
}

// 0x4308b5..0x430932: el 0x48 sale sin cuerpo y, con rand() % 10 en 0, saca otro rand() para la luz L = ((rand() & 3)
// + 3) · 0,1f (fst del rojo; el verde por 0,6f y el azul por 0,2f desde L sin redondear) y suelta el humo
// 0x53fe70(0x5f2, (x, y, z + 50), ángulos, luz, 33, escala, 0), con los cuatro rand() de 0x541a8d. Sin un frame del
// original, no saca nada; sin el pool, corta en el alta después de sus dos rand().
TEST(CrywolfObjectServicesTest, TheRisingSmokeLeavesOnceInTenFiftyAboveTheObject)
{
    for (const int remainder : {0, 1})
    {
        SCOPED_TRACE(remainder);
        Fixture fixture;
        fixture.random = CrtRandom(SeedWith(10, remainder));
        CrtRandom draws = fixture.random;
        ObjectDraw draw = BodyDraw();
        const WorldObject object = ObjectOf(0x48);
        ASSERT_TRUE(CrywolfObjectBody(object, draw, fixture.Pass()).HasValue());
        EXPECT_EQ(draw.kind, ObjectDrawKind::Hidden);
        static_cast<void>(draws.Next());
        const auto alive = fixture.Alive();
        if (remainder != 0)
        {
            EXPECT_TRUE(alive.empty());
            EXPECT_EQ(fixture.random.State(), draws.State());
            continue;
        }
        ASSERT_EQ(alive.size(), 1u);
        const Particle& smoke = *alive[0];
        const double level =
            static_cast<double>((draws.Next() & 3) + 3) * static_cast<double>(std::bit_cast<float>(0x3dcccccdu));
        EXPECT_EQ(smoke.type, 0x5f2);
        EXPECT_EQ(smoke.subtype, 33);
        EXPECT_EQ(smoke.light,
                  glm::vec3(F32(level), F32(level * static_cast<double>(std::bit_cast<float>(0x3f19999au))),
                            F32(level * static_cast<double>(std::bit_cast<float>(0x3e4ccccdu)))));
        EXPECT_EQ(smoke.life, draws.Next() % 150);
        EXPECT_EQ(smoke.scale, F32(1.25 + static_cast<double>(std::bit_cast<float>(0x3dcccccdu))));
        EXPECT_EQ(smoke.position, glm::vec3(12550.0f, 4250.0f, 220.0f));
        const int tilt = draws.Next() % 90 - 45;
        const int turn = draws.Next() % 360;
        EXPECT_EQ(smoke.angles, glm::vec3(static_cast<float>(tilt), 0.0f, static_cast<float>(turn)));
        EXPECT_EQ(smoke.velocity, glm::vec3(0.0f, -static_cast<float>(draws.Next() & 7), 0.0f));
        EXPECT_EQ(smoke.typedOwner, Client::World::ParticleOwner{});
        EXPECT_EQ(fixture.random.State(), draws.State());
    }

    Fixture between;
    between.random = CrtRandom(SeedWith(10, 0));
    const std::uint32_t before = between.random.State();
    GameObjectPass pass = between.Pass();
    pass.originalFrame = false;
    ObjectDraw draw = BodyDraw();
    ASSERT_TRUE(CrywolfObjectBody(ObjectOf(0x48), draw, pass).HasValue());
    EXPECT_EQ(draw.kind, ObjectDrawKind::Hidden);
    EXPECT_EQ(between.random.State(), before);

    Fixture missing;
    missing.random = CrtRandom(SeedWith(10, 0));
    CrtRandom draws = missing.random;
    pass = missing.Pass();
    pass.particles = nullptr;
    const auto cut = CrywolfObjectBody(ObjectOf(0x48), draw, pass);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x430932"), std::string::npos) << cut.Error();
    static_cast<void>(draws.Next());
    static_cast<void>(draws.Next());
    EXPECT_EQ(missing.random.State(), draws.State());
}

// Lo que no está portado corta con su dirección: el 0x51 con el estado 2 y los casos de monstruos de 0x4339d0. Sin el
// estado, el dibujo no sigue.
TEST(CrywolfObjectServicesTest, TheUnportedCasesCutWhereTheBinaryReachesThem)
{
    Fixture fixture;
    ObjectDraw draw = BodyDraw();
    ASSERT_TRUE(CrywolfObjectBody(ObjectOf(0x51), draw, fixture.Pass()).HasValue());
    EXPECT_EQ(draw.kind, ObjectDrawKind::Hidden);
    fixture.crywolf.state = 2;
    auto cut = CrywolfObjectBody(ObjectOf(0x51), draw, fixture.Pass());
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x430a40"), std::string::npos) << cut.Error();

    cut = CrywolfObjectBody(ObjectOf(0x171), draw, fixture.Pass());
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x4339d0"), std::string::npos) << cut.Error();

    auto pass = fixture.Pass();
    pass.crywolf = nullptr;
    cut = CrywolfObjectBody(ObjectOf(0x24), draw, pass);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x4306a0"), std::string::npos) << cut.Error();
}

// 0x6046be..0x6046ce: con un frame del original, 0x6046a0 pasa la cuenta a 0 después de 40 y le suma 1 antes de
// mirar un objeto, aun sin objetos. Un dibujo de MuRet entre frames no la toca.
TEST(CrywolfObjectServicesTest, TheObjectDrawCountsFromOneToFortyOne)
{
    Fixture fixture;
    const NoModels models;
    Client::Render::ModelBatch batch;
    const Client::World::TerrainPolygon polygon{};
    std::vector<WorldObject> objects;
    fixture.drawCounter = 0;
    std::vector<std::int32_t> seen;
    for (int frame = 0; frame < 43; ++frame)
    {
        ASSERT_TRUE(Client::Scenes::DrawGameWorldObjects(batch, models, objects, polygon, nullptr, {}, fixture.Pass())
                        .HasValue());
        seen.push_back(fixture.drawCounter);
    }
    EXPECT_EQ(seen[0], 1);
    EXPECT_EQ(seen[39], 40);
    EXPECT_EQ(seen[40], 41);
    EXPECT_EQ(seen[41], 1);
    EXPECT_EQ(seen[42], 2);
    auto pass = fixture.Pass();
    pass.originalFrame = false;
    ASSERT_TRUE(Client::Scenes::DrawGameWorldObjects(batch, models, objects, polygon, nullptr, {}, pass).HasValue());
    EXPECT_EQ(fixture.drawCounter, 2);
}
