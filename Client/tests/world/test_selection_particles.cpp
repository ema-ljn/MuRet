#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "World/IParticleTrigonometry.h"
#include "World/KalimaParticleKernels.h"
#include "World/ParticleOwner.h"
#include "World/Particles.h"
#include "World/SelectionParticleKernels.h"
#include "World/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

// Las partículas de los objetos del mapa 54 (y del 33) y del aura del elegido de la selección: lo que
// 0x53fe70 escribe después de su prefijo y lo que 0x5446e0 avanza (06 § El mundo de la selección). Los
// valores esperados repiten las fórmulas del ASM con otro CrtRandom de la misma semilla.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::World::Particle;
using Client::World::ParticleError;
using Client::World::Particles;

constexpr std::int32_t SelectionScene = 4;
constexpr std::int32_t GameScene = 5;
constexpr double Hundredth = static_cast<double>(std::bit_cast<float>(0x3c23d70au));
constexpr double Twentieth = static_cast<double>(std::bit_cast<float>(0x3d4ccccdu));
constexpr double Tenth = static_cast<double>(std::bit_cast<float>(0x3dcccccdu));
constexpr double Fiftieth = static_cast<double>(std::bit_cast<float>(0x3ca3d70au));

float F32(double value)
{
    return static_cast<float>(value);
}

const Particle& Create(Particles& particles, std::int32_t type, std::int32_t subtype, float scale, CrtRandom& random,
                       std::int32_t scene = SelectionScene, Client::World::ParticleOwner owner = {})
{
    const auto created =
        particles.CreateOwnedChecked(type, {1000.0f, 2000.0f, 300.0f}, {0.0f, 0.0f, 45.0f}, {0.5f, 0.25f, 0.125f},
                                     subtype, scale, 0.0f, random, scene, owner);
    EXPECT_TRUE(created.HasValue());
    EXPECT_TRUE(created.HasValue() && created.Value().has_value());
    return particles.All()[created.HasValue() && created.Value() ? *created.Value() : 0];
}

// El seno de 0x547723 por el 20 de 0x6b86d0: anota el argumento y devuelve un producto fijo.
class RecordingSine final : public Client::World::IParticleTrigonometry
{
public:
    mutable std::vector<double> arguments;
    double product = 3.0;
    Client::Core::Expected<double, ParticleError> SineProductPC53(double argument, float multiplier) const override
    {
        arguments.push_back(argument);
        EXPECT_EQ(multiplier, 20.0f);
        return product;
    }
    Client::Core::Expected<double, ParticleError> CosineProductPC53(double, float) const override
    {
        ADD_FAILURE() << "622/1 no usa el coseno";
        return Client::Core::Unexpected(ParticleError::UnrepresentableTrigonometry);
    }
};

// Un paso de 0x5446e0 con el reloj del mundo y los objetos de `objects` como dueños.
Client::Core::Expected<void, ParticleError> Step(Particles& particles, CrtRandom& random, float clock,
                                                 RecordingSine& sine,
                                                 std::vector<Client::World::WorldObject>* objects = nullptr)
{
    const std::int32_t map = 54;
    Client::World::ParticleObjectOwners owners;
    if (objects != nullptr)
    {
        owners = [objects](std::uint64_t identity) -> Client::World::WorldObject*
        {
            for (auto& object : *objects)
            {
                if (object.identity == identity)
                    return &object;
            }
            return nullptr;
        };
    }
    return Client::World::StepParticlesChecked(particles, random, std::cref(clock), &map, sine, {}, owners);
}

// 0x543851..0x54389c: el 729/1 se dibuja con Impack03 (0x719), con escala y subida sorteadas.
TEST(SelectionParticlesTest, TheChaseLightRisingSubtypeDrawsWithImpack03AndSixRandomDraws)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(7);
    CrtRandom draws(7);
    const Particle& p = Create(*particles, Client::World::ChaseLightParticle, 1, 1.0f, random);
    const int scale = draws.Next() % 10 + 20;
    const int rise = draws.Next() % 10 + 20;
    const int life = draws.Next() % 3 + 20;
    const int x = draws.Next() % 50 - 25;
    const int y = draws.Next() % 50 - 25;
    const int z = draws.Next() % 200 - 100;
    EXPECT_EQ(p.type, 0x729);
    EXPECT_EQ(p.texture, 0x719);
    EXPECT_EQ(p.scale, F32(scale * Hundredth));
    EXPECT_EQ(p.verticalSpeed, F32(rise * Twentieth));
    EXPECT_EQ(p.life, life);
    EXPECT_EQ(p.position, glm::vec3(F32(x + 1000.0), F32(y + 2000.0), F32((z + 300.0) + 250.0)));
    EXPECT_EQ(p.light, glm::vec3(0.5f, 0.25f, 0.125f));
    EXPECT_EQ(random.State(), draws.State());
}

// 0x54389e..0x5438c1: el 729/0 es la luz ancha, quieta, con su propia textura.
TEST(SelectionParticlesTest, TheWideChaseLightStaysStillAndDrawsFiveTimes)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(11);
    CrtRandom draws(11);
    const Particle& p = Create(*particles, Client::World::ChaseLightParticle, 0, 1.0f, random);
    const int scale = draws.Next() % 5 + 20;
    const int life = draws.Next() % 3 + 20;
    const int x = draws.Next() % 50 - 25;
    const int y = draws.Next() % 50 - 25;
    const int z = draws.Next() % 200 - 100;
    EXPECT_EQ(p.texture, 0x729);
    EXPECT_EQ(p.scale, F32(scale * Tenth));
    EXPECT_EQ(p.verticalSpeed, 0.0f);
    EXPECT_EQ(p.life, life);
    EXPECT_EQ(p.position, glm::vec3(F32(x + 1000.0), F32(y + 2000.0), F32((z + 300.0) + 250.0)));
    EXPECT_EQ(random.State(), draws.State());
}

// 0x547880..0x5478cf: con vida 10 o más (ya descontada) la luz sube 1,16; debajo, baja 0,862.
TEST(SelectionParticlesTest, TheChaseLightBrightensUntilTenStepsAreLeftAndThenFades)
{
    for (const int life : {11, 10})
    {
        auto particles = std::make_unique<Particles>();
        Particle& p = particles->All()[0];
        p.alive = true;
        p.type = p.texture = Client::World::ChaseLightParticle;
        p.subtype = 1;
        p.life = life;
        p.light = glm::vec3(0.5f);
        p.position = {0.0f, 0.0f, 100.0f};
        p.verticalSpeed = 1.25f;
        CrtRandom random(3);
        RecordingSine sine;
        ASSERT_TRUE(Step(*particles, random, 0.0f, sine).HasValue());
        const double factor = life == 11 ? static_cast<double>(std::bit_cast<float>(0x3f947ae1u))
                                         : static_cast<double>(std::bit_cast<float>(0x3f5cb08eu));
        EXPECT_EQ(p.light, glm::vec3(F32(0.5 * factor)));
        EXPECT_EQ(p.position.z, 101.25f);
        EXPECT_TRUE(p.alive);
    }
}

// 0x54150e..0x5415fb: vida 33, la escala baja, el giro, el lado, la subida y la deriva en y (con siete
// de diez); y sigue al corrimiento de x.
TEST(SelectionParticlesTest, TheRisingFlameDrawsItsDriftOnlyWhenTheSeventhOfTenComesOut)
{
    std::vector<bool> drifted;
    for (const std::uint32_t seed : {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u})
    {
        auto particles = std::make_unique<Particles>();
        CrtRandom random(seed);
        CrtRandom draws(seed);
        const Particle& p = Create(*particles, Client::World::FlameParticle, 7, 1.5f, random);
        const float scale = F32(1.5 - (draws.Next() % 20) * Hundredth);
        const int turn = draws.Next() % 360;
        const int side = draws.Next() & 1;
        const float x = F32(side * 0.5 * scale + 1000.0);
        const float rise = F32(((draws.Next() % 100) * Hundredth + static_cast<double>(1.8f)) * scale);
        float drift = 0.0f;
        const bool drifts = draws.Next() % 10 >= 3;
        if (drifts)
            drift = F32(((draws.Next() % 20) * Tenth - 1.0) * scale);
        drifted.push_back(drifts);
        EXPECT_EQ(p.life, 33);
        EXPECT_EQ(p.scale, scale);
        EXPECT_EQ(p.rotation, static_cast<float>(turn - 180));
        EXPECT_EQ(p.start.x, 1000.0f);
        EXPECT_EQ(p.position.x, x);
        EXPECT_EQ(p.position.y, F32((static_cast<double>(x) - 1000.0) + 2000.0));
        EXPECT_EQ(p.verticalSpeed, rise);
        EXPECT_EQ(p.velocity, glm::vec3(0.0f, drift, 0.0f));
        EXPECT_EQ(random.State(), draws.State());
    }
    EXPECT_NE(std::find(drifted.begin(), drifted.end(), true), drifted.end());
    EXPECT_NE(std::find(drifted.begin(), drifted.end(), false), drifted.end());
}

// 0x545e3f..0x545ea9: con la partida a la izquierda de x (C0 del fcomp) gira +2 y si no −2; sube, se achica con la
// subida y se apaga con la escala del x87 en 0 o menos; la luz cae 0,993.
TEST(SelectionParticlesTest, TheRisingFlameTurnsBySideOfItsStartAndDiesWhenItsScaleIsGone)
{
    for (const float startX : {-5.0f, 5.0f})
    {
        auto particles = std::make_unique<Particles>();
        Particle& p = particles->All()[0];
        p.alive = true;
        p.type = p.texture = Client::World::FlameParticle;
        p.subtype = 7;
        p.life = 20;
        p.moves = false;
        p.start.x = startX;
        p.position = {0.0f, 0.0f, 10.0f};
        p.rotation = 30.0f;
        p.verticalSpeed = 2.0f;
        p.scale = 1.0f;
        p.light = glm::vec3(1.0f);
        CrtRandom random(3);
        RecordingSine sine;
        ASSERT_TRUE(Step(*particles, random, 0.0f, sine).HasValue());
        EXPECT_EQ(p.rotation, startX < 0.0f ? 32.0f : 28.0f) << startX;
        EXPECT_EQ(p.position.z, 12.0f);
        EXPECT_EQ(p.scale, F32(1.0 - 2.0 * static_cast<double>(std::bit_cast<float>(0x3c2c7692u))));
        EXPECT_EQ(p.light, glm::vec3(F32(static_cast<double>(std::bit_cast<float>(0x3f7e3870u)))));
        EXPECT_TRUE(p.alive);
    }
    auto particles = std::make_unique<Particles>();
    Particle& p = particles->All()[0];
    p.alive = true;
    p.type = p.texture = Client::World::FlameParticle;
    p.subtype = 7;
    p.life = 20;
    p.moves = false;
    p.verticalSpeed = 95.0f;
    p.scale = 1.0f;
    CrtRandom random(3);
    RecordingSine sine;
    ASSERT_TRUE(Step(*particles, random, 0.0f, sine).HasValue());
    EXPECT_LE(p.scale, 0.0f);
    EXPECT_FALSE(p.alive);
}

// 0x5435cf..0x54363b: 622/1 vive 30, sortea su fase, su lugar, su escala y su giro (siete rand()) y suma
// el argumento de escala al giro; la velocidad queda en 0.
TEST(SelectionParticlesTest, TheOwnedMistDrawsSevenTimesAndAddsTheScaleArgumentToItsTurn)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(5);
    CrtRandom draws(5);
    const Client::World::ParticleOwner owner{Client::World::ParticleOwnerKind::WorldObject, 77};
    const Particle& p = Create(*particles, Client::World::CloudParticle, 1, 0.75f, random, SelectionScene, owner);
    const int phase = draws.Next() % 1000;
    const int x = draws.Next() % 500 - 250;
    const int y = draws.Next() % 500 - 250;
    const int z = draws.Next() % 20 + 20;
    const int start = draws.Next() % 100;
    const int scale = draws.Next() % 20 + 180;
    const int turn = draws.Next() % 30;
    EXPECT_EQ(p.life, 30);
    EXPECT_EQ(p.verticalSpeed, static_cast<float>(phase));
    EXPECT_EQ(p.position, glm::vec3(F32(x + 1000.0), F32(y + 2000.0), F32(z + 300.0)));
    EXPECT_EQ(p.start.z, p.position.z);
    EXPECT_EQ(p.start.y, F32(start * Hundredth));
    EXPECT_EQ(p.scale, F32(scale * Hundredth));
    EXPECT_EQ(p.rotationFactor, F32(turn * Hundredth + 0.75));
    EXPECT_EQ(p.velocity, glm::vec3(0.0f));
    EXPECT_EQ(p.typedOwner, owner);
    EXPECT_EQ(random.State(), draws.State());
}

// 0x5476f0..0x547757: el dueño visto renueva la vida a 50; la altura es el seno del reloj por 20 sobre la
// de partida y el giro, el reloj por el factor por 0,02 más la fase.
TEST(SelectionParticlesTest, TheOwnedMistFloatsWithTheWorldClockWhileItsOwnerIsSeen)
{
    std::vector<Client::World::WorldObject> objects(1);
    objects[0].identity = 77;
    objects[0].seen = true;
    objects[0].state.hiddenMesh = Client::World::HiddenObject;
    auto particles = std::make_unique<Particles>();
    Particle& p = particles->All()[0];
    p.alive = true;
    p.type = p.texture = Client::World::CloudParticle;
    p.subtype = 1;
    p.life = 2;
    p.moves = false;
    p.typedOwner = {Client::World::ParticleOwnerKind::WorldObject, 77};
    p.verticalSpeed = 500.0f;
    p.start = {0.0f, 0.25f, 310.0f};
    p.rotationFactor = 1.5f;
    CrtRandom random(3);
    RecordingSine sine;
    const float clock = 1234.0f;
    ASSERT_TRUE(Step(*particles, random, clock, sine, &objects).HasValue());
    EXPECT_EQ(p.life, 50);
    EXPECT_TRUE(p.alive);
    EXPECT_EQ(objects[0].state.hiddenMesh, Client::World::HiddenObject);
    ASSERT_EQ(sine.arguments.size(), 1u);
    EXPECT_EQ(sine.arguments[0], (1234.0 + 500.0) * static_cast<double>(std::bit_cast<float>(0x3951b717u)));
    EXPECT_EQ(p.position.z, F32(3.0 + 310.0));
    EXPECT_EQ(p.rotation, F32((1234.0 * 1.5) * Fiftieth + 0.25));
}

// 0x547703..0x547711: sin verse y sin vida, la neblina se apaga y su dueño vuelve a emitir (+0x58 en 0);
// igual termina su altura y su giro.
TEST(SelectionParticlesTest, TheOwnedMistLetsItsOwnerEmitAgainWhenItDiesUnseen)
{
    std::vector<Client::World::WorldObject> objects(1);
    objects[0].identity = 77;
    objects[0].seen = false;
    objects[0].state.hiddenMesh = Client::World::HiddenObject;
    auto particles = std::make_unique<Particles>();
    Particle& p = particles->All()[0];
    p.alive = true;
    p.type = p.texture = Client::World::CloudParticle;
    p.subtype = 1;
    p.life = 1;
    p.moves = false;
    p.typedOwner = {Client::World::ParticleOwnerKind::WorldObject, 77};
    CrtRandom random(3);
    RecordingSine sine;
    ASSERT_TRUE(Step(*particles, random, 10.0f, sine, &objects).HasValue());
    EXPECT_FALSE(p.alive);
    EXPECT_EQ(p.life, 0);
    EXPECT_EQ(objects[0].state.hiddenMesh, 0);
    EXPECT_EQ(sine.arguments.size(), 1u);
}

// Sin el objeto dueño entre los del mapa, el paso corta con MissingOwner antes de escribir la partícula.
TEST(SelectionParticlesTest, TheOwnedMistNeedsItsOwnerAmongTheMapObjects)
{
    std::vector<Client::World::WorldObject> objects(1);
    objects[0].identity = 5;
    auto particles = std::make_unique<Particles>();
    Particle& p = particles->All()[0];
    p.alive = true;
    p.type = p.texture = Client::World::CloudParticle;
    p.subtype = 1;
    p.life = 4;
    p.moves = false;
    p.typedOwner = {Client::World::ParticleOwnerKind::WorldObject, 77};
    p.position.z = 9.0f;
    CrtRandom random(3);
    RecordingSine sine;
    const auto stepped = Step(*particles, random, 10.0f, sine, &objects);
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_EQ(stepped.Error(), ParticleError::MissingOwner);
    EXPECT_EQ(p.life, 3);
    EXPECT_EQ(p.position.z, 9.0f);
    EXPECT_TRUE(sine.arguments.empty());
}

// 0x543f04 del 6f0: el /2 de los objetos suma 1,6 (0x6b8bfc) a la escala del argumento.
TEST(SelectionParticlesTest, TheObjectWaterfallAddsItsScaleArgument)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(9);
    CrtRandom draws(9);
    const Particle& p = Create(*particles, Client::World::KalimaWaterfallFirst, 2, 0.5f, random, GameScene);
    const int turn = draws.Next() % 360;
    const int fall = draws.Next() % 5;
    EXPECT_EQ(p.life, 30);
    EXPECT_EQ(p.rotation, static_cast<float>(turn));
    EXPECT_EQ(p.velocity.z, static_cast<float>(-7 - fall));
    EXPECT_EQ(p.scale, F32(0.5 + static_cast<double>(1.6f)));
    EXPECT_EQ(p.light, glm::vec3(0.2f));
    EXPECT_EQ(random.State(), draws.State());
}

// 0x543f49..0x543fae del 6f1: el /2 vive 40 y suma el argumento a su escala; en la selección, luz 0,25.
TEST(SelectionParticlesTest, TheObjectWaterfallMistLivesFortyAndAddsItsScaleArgument)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(13);
    CrtRandom draws(13);
    const Particle& p = Create(*particles, Client::World::KalimaWaterfallMist, 2, 0.5f, random, SelectionScene);
    const int turn = draws.Next() % 360;
    const int size = draws.Next() % 6 + 6;
    const int fall = draws.Next() % 3;
    const int x = draws.Next() % 20 - 10;
    const int y = draws.Next() % 20 - 10;
    const int z = draws.Next() % 40 - 20;
    EXPECT_EQ(p.life, 40);
    EXPECT_EQ(p.rotation, static_cast<float>(turn));
    EXPECT_EQ(p.scale, F32(size * Tenth + 0.5));
    EXPECT_EQ(p.velocity.z, static_cast<float>(-3 - fall));
    EXPECT_EQ(p.light, glm::vec3(0.25f));
    EXPECT_EQ(p.position, glm::vec3(F32(x + 1000.0), F32(y + 2000.0), F32(z + 300.0)));
    EXPECT_EQ(random.State(), draws.State());
}

// 0x54417f..0x544149 del 6f2: el /4 sortea giro y escala, y después vive 20, sube y vuelve a sortear la
// escala con el argumento sumado.
TEST(SelectionParticlesTest, TheObjectSplashDrawsItsScaleTwice)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(17);
    CrtRandom draws(17);
    const Particle& p = Create(*particles, Client::World::WaterfallSplashFirst, 4, 0.5f, random, GameScene);
    const int turn = draws.Next() % 360;
    static_cast<void>(draws.Next());
    const int rise = draws.Next() % 5 + 2;
    const int size = draws.Next() % 10 + 10;
    const int x = draws.Next() % 40 - 20;
    const int y = draws.Next() % 40 - 20;
    const int z = draws.Next() % 20 - 10;
    EXPECT_EQ(p.life, 20);
    EXPECT_EQ(p.rotation, static_cast<float>(turn));
    EXPECT_EQ(p.velocity.z, static_cast<float>(rise));
    EXPECT_EQ(p.scale, F32(size * static_cast<double>(0.02f) + 0.5));
    EXPECT_EQ(p.position, glm::vec3(F32(x + 1000.0), F32(y + 2000.0), F32(z + 300.0)));
    EXPECT_EQ(random.State(), draws.State());
}

// Fuera de las familias con dueño, CreateOwnedChecked no da de alta nada.
TEST(SelectionParticlesTest, OwnedCreationRejectsOtherFamilies)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(1);
    const auto created =
        particles->CreateOwnedChecked(Client::World::CloudParticle, {}, {}, {}, 2, 1.0f, 0.0f, random, SelectionScene);
    ASSERT_FALSE(created.HasValue());
    EXPECT_EQ(created.Error(), ParticleError::UnsupportedSubtype);
    EXPECT_FALSE(particles->All()[0].alive);
    EXPECT_EQ(random.State(), 1u);
}
} // namespace
