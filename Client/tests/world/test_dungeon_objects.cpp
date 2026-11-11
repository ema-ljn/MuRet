#include "Render/Models/Cloth.h"
#include "Render/Models/ObjectState.h"
#include "World/DungeonObjectStep.h"
#include "World/ModelEffects.h"
#include "World/ObjectModelStepState.h"
#include "World/Particles.h"
#include "World/TerrainLighting.h"
#include "World/WorldObjectStepServices.h"
#include "World/WorldObjects.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

// Los casos de 0x604ab0 del Dungeon (mapa 1, la tabla 0x605bc0), de Lost Tower (mapa 4, 0x605c50 con el
// tipo − 3) y de la Arena (mapa 6, 0x6054fd), y la patada de 0x52f550 (06 § Los objetos del Dungeon y de Lost
// Tower, § Los objetos de la Arena).
namespace
{
using Client::Render::Models::CrtRandom;
using Client::World::KickObject;
using Client::World::ModelEffects;
using Client::World::ObjectModelStepState;
using Client::World::ObjectStepGlobals;
using Client::World::Particles;
using Client::World::StepArenaObject;
using Client::World::StepDungeonObject;
using Client::World::StepLostTowerObject;
using Client::World::TerrainLighting;
using Client::World::WorldObject;

WorldObject Object(std::int16_t type)
{
    WorldObject object;
    object.type = type;
    object.transform.position = {1000.0f, 1500.0f, 20.0f};
    object.transform.angles = {0.0f, 0.0f, 0.0f};
    object.light = {0.25f, 0.5f, 0.75f};
    return object;
}

CrtRandom After(std::uint32_t seed, int draws)
{
    CrtRandom random(seed);
    for (int draw = 0; draw < draws; ++draw)
        static_cast<void>(random.Next());
    return random;
}

// La primera semilla cuyo primer rand() cumple (o no) la condición.
std::uint32_t SeedWhere(bool (*condition)(int))
{
    for (std::uint32_t seed = 1;; ++seed)
    {
        CrtRandom random(seed);
        if (condition(random.Next()))
            return seed;
    }
}

// 0x604ff1..0x60502b: el model+0x88 del tipo en 1 y la V en el resto firmado de ftol(reloj) por 1.000 por
// −0,001f (0x6ba1a4). Sin el estado de los modelos corta antes de escribir.
TEST(DungeonObjectsTest, TheMapOneWaterfallsMarkTheirModelAndScrollTheirV)
{
    for (const std::int16_t type : {std::int16_t{0x16}, std::int16_t{0x17}, std::int16_t{0x18}})
    {
        SCOPED_TRACE(type);
        ObjectModelStepState models;
        ObjectStepGlobals globals;
        globals.worldClock = 12345.0f;
        globals.modelState = &models;
        auto object = Object(type);
        CrtRandom random(7);
        ASSERT_TRUE(StepDungeonObject(object, random, globals).HasValue());
        EXPECT_EQ(models.unlitMeshes.at(type), 1);
        EXPECT_EQ(object.state.uv.y,
                  static_cast<float>(345.0 * static_cast<double>(std::bit_cast<float>(0xba83126fu))));
        EXPECT_EQ(object.state.uv.x, 0.0f);
        EXPECT_EQ(random.State(), 7u);

        globals.worldClock = -2500.0f;
        ASSERT_TRUE(StepDungeonObject(object, random, globals).HasValue());
        EXPECT_EQ(object.state.uv.y,
                  static_cast<float>(-500.0 * static_cast<double>(std::bit_cast<float>(0xba83126fu))))
            << "el resto firmado, sin negar el ftol";

        globals.modelState = nullptr;
        auto bare = Object(type);
        const auto cut = StepDungeonObject(bare, random, globals);
        ASSERT_FALSE(cut.HasValue());
        EXPECT_NE(cut.Error().find("0x605003"), std::string::npos) << cut.Error();
        EXPECT_EQ(bare.state.uv.y, 0.0f);
    }
}

// 0x6059dd (por la tabla del 1): el 0x27, el 0x28 y el 0x33 dejan +0x58 en −2 sin rand().
TEST(DungeonObjectsTest, TheMapOneHiddenTypesOnlyStoreTheirHiddenMesh)
{
    for (const std::int16_t type : {std::int16_t{0x27}, std::int16_t{0x28}, std::int16_t{0x33}})
    {
        SCOPED_TRACE(type);
        auto object = Object(type);
        CrtRandom random(7);
        ASSERT_TRUE(StepDungeonObject(object, random, ObjectStepGlobals{}).HasValue());
        EXPECT_EQ(object.state.hiddenMesh, Client::World::HiddenObject);
        EXPECT_EQ(random.State(), 7u);
    }
    auto other = Object(0x30);
    CrtRandom random(7);
    ASSERT_TRUE(StepDungeonObject(other, random, ObjectStepGlobals{}).HasValue());
    EXPECT_EQ(other.state.hiddenMesh, Client::Render::Models::ObjectState{}.hiddenMesh);
}

// 0x605034 y 0x605052: 0x52f360(0, objeto, (0; −30; 240)) y (0; 0; 190), los fuegos de clase 0. Sin el
// pool de partículas corta con la dirección de la llamada.
TEST(DungeonObjectsTest, TheMapOneFiresAreClassZeroAtTheirNativeOffsets)
{
    struct Fire
    {
        std::int16_t type;
        glm::vec3 offset;
        const char* address;
    };
    for (const Fire& fire :
         {Fire{0x29, {0.0f, -30.0f, 240.0f}, "0x605043"}, Fire{0x2a, {0.0f, 0.0f, 190.0f}, "0x60505e"}})
    {
        SCOPED_TRACE(fire.type);
        auto lighting = std::make_unique<TerrainLighting>();
        auto particles = std::make_unique<Particles>();
        ObjectStepGlobals globals;
        globals.dynamicLighting = lighting.get();
        globals.particles = particles.get();
        auto object = Object(fire.type);
        CrtRandom random(5);
        ASSERT_TRUE(StepDungeonObject(object, random, globals).HasValue());

        auto expectedLighting = std::make_unique<TerrainLighting>();
        auto expectedParticles = std::make_unique<Particles>();
        ObjectStepGlobals direct;
        direct.dynamicLighting = expectedLighting.get();
        direct.particles = expectedParticles.get();
        CrtRandom expected(5);
        ASSERT_TRUE(Client::World::EmitObjectFire(0, Object(fire.type), fire.offset, expected, direct).HasValue());
        EXPECT_EQ(random.State(), expected.State());
        EXPECT_EQ(lighting->cells, expectedLighting->cells);
        ASSERT_TRUE(particles->All().front().alive);
        EXPECT_EQ(particles->All().front().position, expectedParticles->All().front().position);

        ObjectStepGlobals missing;
        CrtRandom again(5);
        const auto cut = StepDungeonObject(object, again, missing);
        ASSERT_FALSE(cut.HasValue());
        EXPECT_NE(cut.Error().find(fire.address), std::string::npos) << cut.Error();
    }
}

// 0x604fb4..0x604fe3: con rand() % 3 en 0, 0x518cf0(0xda, posición, ángulos, luz +0xf4); siempre después,
// +0x58 en −2. Sin la tabla de efectos, el rand() que la pide corta antes de ocultar.
TEST(DungeonObjectsTest, TheMapOneDripDropsAStoneOneTimeInThreeAndAlwaysHides)
{
    const std::uint32_t drops = SeedWhere([](int value) { return value % 3 == 0; });
    const std::uint32_t stays = SeedWhere([](int value) { return value % 3 != 0; });
    {
        ModelEffects effects;
        ObjectStepGlobals globals;
        globals.effects = &effects;
        auto object = Object(0x34);
        CrtRandom random(drops);
        ASSERT_TRUE(StepDungeonObject(object, random, globals).HasValue());
        EXPECT_EQ(object.state.hiddenMesh, Client::World::HiddenObject);
        ASSERT_TRUE(effects.Effects()[0].alive);
        EXPECT_EQ(effects.Effects()[0].type, Client::World::FallingStoneEffect);
        EXPECT_EQ(effects.Effects()[0].light, object.light);
        EXPECT_FALSE(effects.Effects()[1].alive);
        EXPECT_EQ(random.State(), After(drops, 7).State()) << "el % 3 y los seis de 0x51b80a";
    }
    {
        ModelEffects effects;
        ObjectStepGlobals globals;
        globals.effects = &effects;
        auto object = Object(0x34);
        CrtRandom random(stays);
        ASSERT_TRUE(StepDungeonObject(object, random, globals).HasValue());
        EXPECT_EQ(object.state.hiddenMesh, Client::World::HiddenObject);
        EXPECT_FALSE(effects.Effects()[0].alive);
        EXPECT_EQ(random.State(), After(stays, 1).State());
        CrtRandom bare(stays);
        ASSERT_TRUE(StepDungeonObject(object, bare, ObjectStepGlobals{}).HasValue())
            << "sin piedra no hace falta la tabla";
    }
    auto object = Object(0x34);
    CrtRandom random(drops);
    const auto cut = StepDungeonObject(object, random, ObjectStepGlobals{});
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x604fdb"), std::string::npos) << cut.Error();
    EXPECT_EQ(object.state.hiddenMesh, Client::Render::Models::ObjectState{}.hiddenMesh);
    EXPECT_EQ(random.State(), After(drops, 1).State());
}

// 0x605404 (el 3 y el 4) y 0x60542f (el 0x13 y el 0x14, con +0x64 en 4): la U en el −ftol(reloj) negado en
// 32 bits, su resto firmado de 1.000 y 0,001f (0x6b89c8). 0x605461: el 0x12 y el 0x17 llevan +0x64 a 1.
TEST(DungeonObjectsTest, TheLostTowerWaterScrollsItsUAndTheChromeTypesBlend)
{
    for (const std::int16_t type : {std::int16_t{3}, std::int16_t{4}, std::int16_t{0x13}, std::int16_t{0x14}})
    {
        SCOPED_TRACE(type);
        ObjectStepGlobals globals;
        globals.worldClock = 12345.0f;
        auto object = Object(type);
        CrtRandom random(7);
        ASSERT_TRUE(StepLostTowerObject(object, random, globals).HasValue());
        EXPECT_EQ(object.state.uv.x,
                  static_cast<float>(-345.0 * static_cast<double>(std::bit_cast<float>(0x3a83126fu))));
        EXPECT_EQ(object.state.blendMesh, type >= 0x13 ? 4 : Client::Render::Models::ObjectState{}.blendMesh);
        EXPECT_EQ(random.State(), 7u);
    }
    for (const std::int16_t type : {std::int16_t{0x12}, std::int16_t{0x17}})
    {
        auto object = Object(type);
        CrtRandom random(7);
        ASSERT_TRUE(StepLostTowerObject(object, random, ObjectStepGlobals{}).HasValue());
        EXPECT_EQ(object.state.blendMesh, 1);
        EXPECT_EQ(object.state.uv.x, 0.0f);
    }
    // ftol(-2.147.483.648) = 0x80000000: el neg lo deja igual y el resto es −648.
    ObjectStepGlobals wrap;
    wrap.worldClock = -2147483648.0f;
    auto object = Object(3);
    CrtRandom random(7);
    ASSERT_TRUE(StepLostTowerObject(object, random, wrap).HasValue());
    EXPECT_EQ(object.state.uv.x, static_cast<float>(-648.0 * static_cast<double>(std::bit_cast<float>(0x3a83126fu))));
    // NaN da 0 (OriginalFtolLow); el infinito no está acreditado y corta.
    wrap.worldClock = std::numeric_limits<float>::quiet_NaN();
    ASSERT_TRUE(StepLostTowerObject(object, random, wrap).HasValue());
    EXPECT_EQ(object.state.uv.x, 0.0f);
    wrap.worldClock = std::numeric_limits<float>::infinity();
    const auto cut = StepLostTowerObject(object, random, wrap);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x60540a"), std::string::npos) << cut.Error();
}

// 0x6054fd..0x605546: el 0x15 de la Arena lleva el blendMesh a 3 antes del ftol y corre la V con el ftol
// negado; el 0x26 sólo se oculta; los demás tipos no cambian nada. Ninguno saca rand().
TEST(DungeonObjectsTest, TheArenaScrollsTheVOfItsWaterAndHidesItsMarkers)
{
    ObjectStepGlobals globals;
    globals.worldClock = 12345.0f;
    auto water = Object(0x15);
    ASSERT_TRUE(StepArenaObject(water, globals).HasValue());
    EXPECT_EQ(water.state.blendMesh, 3);
    EXPECT_EQ(water.state.uv.y, static_cast<float>(-345.0 * static_cast<double>(std::bit_cast<float>(0x3a83126fu))));
    EXPECT_EQ(water.state.uv.x, 0.0f);
    EXPECT_EQ(water.state.hiddenMesh, WorldObject{}.state.hiddenMesh);

    auto marker = Object(0x26);
    ASSERT_TRUE(StepArenaObject(marker, globals).HasValue());
    EXPECT_EQ(marker.state.hiddenMesh, -2);
    EXPECT_EQ(marker.state.uv, glm::vec2(0.0f));

    for (const std::int16_t type : {std::int16_t{9}, std::int16_t{0x14}, std::int16_t{0x16}})
    {
        SCOPED_TRACE(type);
        auto other = Object(type);
        ASSERT_TRUE(StepArenaObject(other, globals).HasValue());
        EXPECT_EQ(other.state.blendMesh, WorldObject{}.state.blendMesh);
        EXPECT_EQ(other.state.hiddenMesh, WorldObject{}.state.hiddenMesh);
        EXPECT_EQ(other.state.uv, glm::vec2(0.0f));
    }

    // Por StepWorldObjectChecked en el 6, sin rand().
    auto stepped = Object(0x15);
    CrtRandom random(7);
    ASSERT_TRUE(Client::World::StepWorldObjectChecked(stepped, 6, nullptr, random, globals).HasValue());
    EXPECT_EQ(stepped.state.uv.y, water.state.uv.y);
    EXPECT_EQ(random.State(), 7u);

    // Con el ftol fuera de lo acreditado corta después del blendMesh.
    globals.worldClock = std::numeric_limits<float>::infinity();
    auto cut = Object(0x15);
    const auto failed = StepArenaObject(cut, globals);
    ASSERT_FALSE(failed.HasValue());
    EXPECT_NE(failed.Error().find("0x60552a"), std::string::npos) << failed.Error();
    EXPECT_EQ(cut.state.blendMesh, 3);
    EXPECT_EQ(cut.state.uv.y, 0.0f);
}

// 0x60546f: el 0x18 deja +0x58 en −2 y, con rand() & 63 en 0, 0x518cf0(0x5de, …, luz +0xf4); el chorro
// abre y cierra el registro propio. 0x6059dd: el 0x19 sólo se oculta.
TEST(DungeonObjectsTest, TheLostTowerJetHidesAndSometimesSpitsTheFlame)
{
    const std::uint32_t spits = SeedWhere([](int value) { return (value & 63) == 0; });
    const std::uint32_t quiet = SeedWhere([](int value) { return (value & 63) != 0; });
    {
        ModelEffects effects;
        std::uint8_t depth = 1;
        ObjectStepGlobals globals;
        globals.effects = &effects;
        globals.ownRecordDepth = &depth;
        auto object = Object(0x18);
        CrtRandom random(spits);
        ASSERT_TRUE(StepLostTowerObject(object, random, globals).HasValue());
        EXPECT_EQ(object.state.hiddenMesh, Client::World::HiddenObject);
        ASSERT_TRUE(effects.Effects()[0].alive);
        EXPECT_EQ(effects.Effects()[0].type, Client::World::FlameJetEffect);
        EXPECT_EQ(depth, 1);
        EXPECT_EQ(random.State(), After(spits, 1).State()) << "la cuenta abierta no gasta al cerrar";
    }
    {
        auto object = Object(0x18);
        CrtRandom random(quiet);
        ASSERT_TRUE(StepLostTowerObject(object, random, ObjectStepGlobals{}).HasValue());
        EXPECT_EQ(object.state.hiddenMesh, Client::World::HiddenObject);
        EXPECT_EQ(random.State(), After(quiet, 1).State());
    }
    auto object = Object(0x18);
    CrtRandom random(spits);
    const auto cut = StepLostTowerObject(object, random, ObjectStepGlobals{});
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x6054a9"), std::string::npos) << cut.Error();
    EXPECT_EQ(object.state.hiddenMesh, Client::World::HiddenObject) << "se oculta antes del rand()";

    auto pillar = Object(0x19);
    CrtRandom none(7);
    ASSERT_TRUE(StepLostTowerObject(pillar, none, ObjectStepGlobals{}).HasValue());
    EXPECT_EQ(pillar.state.hiddenMesh, Client::World::HiddenObject);
    EXPECT_EQ(none.State(), 7u);
}

struct Kick
{
    glm::vec3 hero{1030.0f, 1520.0f, 0.0f};
    std::uint8_t action = 0xd;
    std::vector<std::int32_t> sounds;
    ObjectStepGlobals globals;

    Kick()
    {
        globals.heroPosition = &hero;
        globals.heroAction = &action;
        globals.sound = [this](std::int32_t sound) { sounds.push_back(sound); };
    }
};

// 0x52f594..0x52f5fc: a menos de 50 (√(dy · dy en float + dx · dx)), la velocidad (dx · −0,4; dy · −0,4; 0),
// el giro dy · −4 en +0x28 y dx · −4 en +0x2c, y el 0x7c. 0x52f608..0x52f6a1: todo por 0,6 y sumado a la
// posición (la x con el producto sin redondear) y a los ángulos.
TEST(DungeonObjectsTest, TheKickPushesTheObjectAwayFromTheHeroAndDecays)
{
    for (const std::uint8_t action : {std::uint8_t{0xd}, std::uint8_t{0x21}})
    {
        SCOPED_TRACE(static_cast<int>(action));
        Kick kick;
        kick.action = action;
        auto object = Object(0x26);
        object.transform.angles = {1.0f, 2.0f, 3.0f};
        ASSERT_TRUE(KickObject(object, kick.globals).HasValue());
        const double push = static_cast<double>(std::bit_cast<float>(0xbecccccdu));
        const double decay = static_cast<double>(std::bit_cast<float>(0x3f19999au));
        const auto pushX = static_cast<double>(static_cast<float>(30.0 * push)) * decay;
        const auto pushY = static_cast<float>(static_cast<double>(static_cast<float>(20.0 * push)) * decay);
        EXPECT_EQ(object.velocity.x, static_cast<float>(pushX));
        EXPECT_EQ(object.velocity.y, pushY);
        EXPECT_EQ(object.velocity.z, 0.0f);
        const auto spinX = static_cast<float>(static_cast<double>(static_cast<float>(20.0 * -4.0)) * decay);
        const auto spinY = static_cast<float>(static_cast<double>(static_cast<float>(30.0 * -4.0)) * decay);
        EXPECT_EQ(object.copiedAngles.x, spinX);
        EXPECT_EQ(object.copiedAngles.y, spinY);
        EXPECT_EQ(object.transform.position.x, static_cast<float>(pushX + 1000.0));
        EXPECT_EQ(object.transform.position.y, static_cast<float>(static_cast<double>(pushY) + 1500.0));
        EXPECT_EQ(object.transform.angles.x, static_cast<float>(static_cast<double>(spinX) + 1.0));
        EXPECT_EQ(object.transform.angles.y, static_cast<float>(static_cast<double>(spinY) + 2.0));
        EXPECT_EQ(object.transform.angles.z, 3.0f);
        EXPECT_EQ(kick.sounds, std::vector<std::int32_t>{0x7c});
    }
}

// Fuera de las acciones 0xd..0x21, a 50 o más, o con la x de la velocidad en 0,1 o más, sólo decae. NaN en
// la velocidad deja patear (fcomp sin orden pone C0).
TEST(DungeonObjectsTest, TheKickNeedsTheActionTheReachAndAStillObject)
{
    const auto decayed = [](float value)
    { return static_cast<float>(static_cast<double>(value) * static_cast<double>(std::bit_cast<float>(0x3f19999au))); };
    {
        Kick kick;
        kick.action = 0xc;
        auto object = Object(0x27);
        object.velocity = {0.05f, 0.0f, 2.0f};
        ASSERT_TRUE(KickObject(object, kick.globals).HasValue());
        EXPECT_EQ(object.velocity.z, decayed(2.0f));
        EXPECT_TRUE(kick.sounds.empty());
    }
    {
        Kick kick;
        kick.action = 0x22;
        auto object = Object(0x27);
        ASSERT_TRUE(KickObject(object, kick.globals).HasValue());
        EXPECT_TRUE(kick.sounds.empty());
    }
    {
        Kick kick;
        kick.hero = {1030.0f, 1540.0f, 0.0f};
        auto object = Object(0x27);
        object.transform.position = {1000.0f, 1490.0f, 20.0f}; // dx 30, dy 50: √3.400 > 50
        ASSERT_TRUE(KickObject(object, kick.globals).HasValue());
        EXPECT_TRUE(kick.sounds.empty());
        kick.hero = {1000.0f, 1540.0f, 0.0f}; // dy exacto 50: no es menor
        ASSERT_TRUE(KickObject(object, kick.globals).HasValue());
        EXPECT_TRUE(kick.sounds.empty());
    }
    {
        Kick kick;
        auto object = Object(0x27);
        object.velocity.x = std::bit_cast<float>(0x3dcccccdu);
        ASSERT_TRUE(KickObject(object, kick.globals).HasValue());
        EXPECT_TRUE(kick.sounds.empty());
        object.velocity.x = std::numeric_limits<float>::quiet_NaN();
        ASSERT_TRUE(KickObject(object, kick.globals).HasValue());
        EXPECT_EQ(kick.sounds, std::vector<std::int32_t>{0x7c});
    }
}

// Sin el héroe publicado corta antes de leer; sin emisor, después de los stores de la patada y antes del
// decaimiento.
TEST(DungeonObjectsTest, TheKickCutsWithoutItsBorrowedBindings)
{
    auto object = Object(0x26);
    const auto cut = KickObject(object, ObjectStepGlobals{});
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x52f556"), std::string::npos) << cut.Error();
    EXPECT_EQ(object.velocity, glm::vec3(0.0f));

    Kick kick;
    kick.globals.sound = nullptr;
    const auto silent = KickObject(object, kick.globals);
    ASSERT_FALSE(silent.HasValue());
    EXPECT_NE(silent.Error().find("0x52f5fc"), std::string::npos) << silent.Error();
    EXPECT_EQ(object.velocity.x, static_cast<float>(30.0 * static_cast<double>(std::bit_cast<float>(0xbecccccdu))));
    EXPECT_EQ(object.transform.position, glm::vec3(1000.0f, 1500.0f, 20.0f));

    // StepLostTowerObject lleva el 0x26 y el 0x27 a la patada.
    Kick routed;
    auto ball = Object(0x27);
    CrtRandom random(7);
    ASSERT_TRUE(StepLostTowerObject(ball, random, routed.globals).HasValue());
    EXPECT_EQ(routed.sounds, std::vector<std::int32_t>{0x7c});
    EXPECT_EQ(random.State(), 7u);
}
} // namespace
