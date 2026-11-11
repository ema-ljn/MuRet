#include "Render/Models/Cloth.h"
#include "Render/Models/ObjectState.h"
#include "World/AtlansObjectStep.h"
#include "World/Particles.h"
#include "World/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <string>

// Los casos de 0x604ab0 de Atlans (mapa 7, la tabla 0x605c94 por el tipo − 0x16; 06 § Los objetos de Atlans).
namespace
{
using Client::Render::Models::CrtRandom;
using Client::World::ObjectStepGlobals;
using Client::World::Particles;
using Client::World::StepAtlansObject;
using Client::World::WorldObject;

WorldObject Object(std::int16_t type)
{
    WorldObject object;
    object.type = type;
    object.transform.position = {1000.0f, 1500.0f, 20.0f};
    object.transform.angles = {0.0f, 0.0f, 30.0f};
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

// fld del reloj, fmul por la tasa (float), fsin, fmul por el factor y fadd: en double, que es la precisión
// de la x87 del original, y guardado en float.
float Swing(float clock, std::uint32_t rate, float factor, double addend)
{
    const double argument = static_cast<double>(clock) * static_cast<double>(std::bit_cast<float>(rate));
    return static_cast<float>(std::sin(argument) * static_cast<double>(factor) + addend);
}

constexpr std::uint32_t SlowRate = 0x3b03126fu;              // 0x6b89bc, 0,002f
constexpr std::uint32_t FastRate = 0x3b83126fu;              // 0x6b89e8, 0,004f
const float SwingFactor = std::bit_cast<float>(0x3e99999au); // 0x6b89dc, 0,3f

// 0x60556e..0x6055c8: el 0x16 se oculta, sube su fase (+0x84) 0,1f y la guarda en float; si la suma sin redondear
// pasa de 10 vuelve a 0, y con la fase guardada por encima de 5 suelta una burbuja 0x607/0 (dos rand()) en su
// posición y con sus ángulos.
TEST(AtlansObjectsTest, TheBubblesRiseAndBlowAboveFive)
{
    // La fase guardada después de sumar 0,1f (0x6b87b8) en la x87.
    const auto rise = [](float phase) { return static_cast<float>(static_cast<double>(phase) + 0.1f); };
    struct Case
    {
        float phase;
        float after;
        bool blows;
    };
    // 9,95f + 0,1f pasa de 10 y vuelve a 0. 9,9f + 0,1f queda debajo de 10 sin redondear y se guarda como
    // 10,0f: no vuelve a 0 y sopla.
    for (const Case& probe : {Case{0.0f, rise(0.0f), false}, Case{4.8f, rise(4.8f), false},
                              Case{5.0f, rise(5.0f), true}, Case{9.95f, 0.0f, false}, Case{9.9f, 10.0f, true}})
    {
        SCOPED_TRACE(::testing::Message() << "fase " << probe.phase);
        WorldObject bubbles = Object(0x16);
        bubbles.field84 = probe.phase;
        Particles particles;
        ObjectStepGlobals globals;
        globals.particles = &particles;
        CrtRandom random(7);

        const auto stepped = StepAtlansObject(bubbles, random, globals);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(bubbles.state.hiddenMesh, -2);
        EXPECT_EQ(bubbles.field84, probe.after);
        EXPECT_EQ(particles.All()[0].alive, probe.blows);
        EXPECT_EQ(random.State(), After(7, probe.blows ? 2 : 0).State());
        if (probe.blows)
        {
            EXPECT_EQ(particles.All()[0].type, Client::World::DropParticle);
            EXPECT_EQ(particles.All()[0].subtype, 0);
            EXPECT_EQ(particles.All()[0].position, bubbles.transform.position);
            EXPECT_EQ(particles.All()[0].angles, bubbles.transform.angles);
        }
    }

    // Sin el pool corta en la llamada, después de ocultarse y de guardar la fase.
    WorldObject cut = Object(0x16);
    cut.field84 = 5.0f;
    CrtRandom random(7);
    const auto failed = StepAtlansObject(cut, random, ObjectStepGlobals{});
    ASSERT_FALSE(failed.HasValue());
    EXPECT_NE(failed.Error().find("0x6055c8"), std::string::npos) << failed.Error();
    EXPECT_EQ(cut.state.hiddenMesh, -2);
    EXPECT_EQ(cut.field84, rise(5.0f));
    EXPECT_EQ(random.State(), 7u);
}

// 0x6055d7..0x60565b: las luces del reloj. El 0x17 con blendMesh 0 y sin(T · 0,002f) · 0,3f + 0,5; el 0x20 y el
// 0x22 con blendMesh 1 y (sin(T · 0,004f) + 1) · 0,5; el 0x28 con blendMesh 0, la velocidad en 0,05f y
// sin(T · 0,004f) · 0,3f + 0,5; el 0x26 solo con blendMesh 0. Ninguno saca rand() ni se oculta.
TEST(AtlansObjectsTest, TheLightsSwingWithTheWorldClock)
{
    ObjectStepGlobals globals;
    globals.worldClock = 12345.0f;
    CrtRandom random(7);

    WorldObject weed = Object(0x17);
    weed.state.blendMesh = 5;
    ASSERT_TRUE(StepAtlansObject(weed, random, globals).HasValue());
    EXPECT_EQ(weed.state.blendMesh, 0);
    EXPECT_EQ(weed.state.light, Swing(globals.worldClock, SlowRate, SwingFactor, 0.5));

    for (const std::int16_t type : {std::int16_t{0x20}, std::int16_t{0x22}})
    {
        SCOPED_TRACE(type);
        WorldObject lamp = Object(type);
        ASSERT_TRUE(StepAtlansObject(lamp, random, globals).HasValue());
        EXPECT_EQ(lamp.state.blendMesh, 1);
        const double argument =
            static_cast<double>(globals.worldClock) * static_cast<double>(std::bit_cast<float>(FastRate));
        EXPECT_EQ(lamp.state.light, static_cast<float>((std::sin(argument) + 1.0) * 0.5));
    }

    WorldObject statue = Object(0x26);
    statue.state.blendMesh = 5;
    ASSERT_TRUE(StepAtlansObject(statue, random, globals).HasValue());
    EXPECT_EQ(statue.state.blendMesh, 0);
    EXPECT_EQ(statue.state.light, WorldObject{}.state.light);

    WorldObject coral = Object(0x28);
    coral.state.blendMesh = 5;
    ASSERT_TRUE(StepAtlansObject(coral, random, globals).HasValue());
    EXPECT_EQ(coral.state.blendMesh, 0);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(coral.speed), 0x3d4ccccdu);
    EXPECT_EQ(coral.state.light, Swing(globals.worldClock, FastRate, SwingFactor, 0.5));

    for (const std::int16_t type :
         {std::int16_t{0x15}, std::int16_t{0x18}, std::int16_t{0x21}, std::int16_t{0x27}, std::int16_t{0x29}})
    {
        SCOPED_TRACE(type);
        WorldObject other = Object(type);
        ASSERT_TRUE(StepAtlansObject(other, random, globals).HasValue());
        EXPECT_EQ(other.state.blendMesh, WorldObject{}.state.blendMesh);
        EXPECT_EQ(other.state.hiddenMesh, WorldObject{}.state.hiddenMesh);
        EXPECT_EQ(other.state.light, WorldObject{}.state.light);
    }
    EXPECT_EQ(random.State(), 7u);

    // Por StepWorldObjectChecked en el 7.
    WorldObject stepped = Object(0x17);
    ASSERT_TRUE(Client::World::StepWorldObjectChecked(stepped, 7, nullptr, random, globals).HasValue());
    EXPECT_EQ(stepped.state.light, weed.state.light);
}
} // namespace
