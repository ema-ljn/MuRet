#include "Render/Models/Cloth.h"
#include "Render/Models/ObjectState.h"
#include "Render/Models/Skeleton.h"
#include "World/ObjectModelStepState.h"
#include "World/TarkanObjectStep.h"
#include "World/TerrainLighting.h"
#include "World/WorldObjectStepServices.h"
#include "World/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>

// Los casos de 0x604ab0 de Tarkan (mapa 8, la tabla 0x605cc0 por el tipo − 2), la velocidad del 8 y el
// model+0x88 de la carga (06 § Los objetos de Tarkan).
namespace
{
using Client::World::ObjectModelStepState;
using Client::World::ObjectStepGlobals;
using Client::World::StepTarkanObject;
using Client::World::TerrainLighting;
using Client::World::WorldObject;

constexpr float Clock = 12345.0f;

WorldObject Object(std::int16_t type)
{
    WorldObject object;
    object.type = type;
    object.transform.position = {1000.0f, 1500.0f, 20.0f};
    object.transform.angles = {0.0f, 0.0f, 30.0f};
    object.light = {0.25f, 0.5f, 0.75f};
    return object;
}

double D(float value)
{
    return static_cast<double>(value);
}

float F32(double value)
{
    return static_cast<float>(value);
}

// sin(fase · 0,002f) · 0,35f + 0,65f, con las constantes de 0x6b89bc, 0x6b8bc4 y 0x6b8bc0.
double Pulse(double phase)
{
    return (std::sin(phase * D(std::bit_cast<float>(0x3b03126fu))) * D(std::bit_cast<float>(0x3eb33333u))) +
           D(std::bit_cast<float>(0x3f266666u));
}

// La luz cálida: (L; L · 0,6f; L · 0,2f), cada una guardada en float desde la x87.
glm::vec3 Warm(double pulse)
{
    return {F32(pulse), F32(pulse * D(std::bit_cast<float>(0x3f19999au))),
            F32(pulse * D(std::bit_cast<float>(0x3e4ccccdu)))};
}

// Lo que deja 0x5f9780 con ese color y ese radio en el lugar del objeto.
std::unique_ptr<TerrainLighting> LightOf(const glm::vec3& color, std::int32_t radius)
{
    auto lighting = std::make_unique<TerrainLighting>();
    EXPECT_TRUE(
        Client::World::AddDynamicLight(lighting.get(), Object(0).transform.position, color, radius, "test").HasValue());
    return lighting;
}

// 0x605683: el 2 lleva el blendMesh a 0 y la U a (−ftol(T) % 1.000) · 0,001f.
TEST(TarkanObjectsTest, TheTwoScrollsItsU)
{
    ObjectStepGlobals globals;
    globals.worldClock = Clock;
    auto object = Object(2);
    ASSERT_TRUE(StepTarkanObject(object, globals).HasValue());
    EXPECT_EQ(object.state.blendMesh, 0);
    EXPECT_EQ(object.state.uv.x, F32(-345.0 * D(std::bit_cast<float>(0x3a83126fu))));
    EXPECT_EQ(object.state.uv.y, 0.0f);
}

// 0x6056b5..0x605718: el 4 deja la L guardada en la luz del blendMesh, la V en (−ftol(T) % 10.000) · 0,0001f
// y una luz (L; L; L) de radio 3. Sin destino de luz corta en 0x605718 después de lo que ya escribió.
TEST(TarkanObjectsTest, TheFourPulsesScrollsItsVAndLightsTheGround)
{
    auto lighting = std::make_unique<TerrainLighting>();
    ObjectStepGlobals globals;
    globals.worldClock = Clock;
    globals.dynamicLighting = lighting.get();
    auto object = Object(4);
    ASSERT_TRUE(StepTarkanObject(object, globals).HasValue());
    const double pulse = Pulse(D(Clock));
    EXPECT_EQ(object.state.blendMesh, 0);
    EXPECT_EQ(object.state.light, F32(pulse));
    EXPECT_EQ(object.state.uv.y, F32(-2345.0 * D(std::bit_cast<float>(0x38d1b717u))));
    EXPECT_EQ(lighting->cells, LightOf(glm::vec3(F32(pulse)), 3)->cells);

    ObjectStepGlobals missing;
    missing.worldClock = Clock;
    auto unlit = Object(4);
    const auto cut = StepTarkanObject(unlit, missing);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x605718"), std::string::npos) << cut.Error();
    EXPECT_EQ(unlit.state.light, F32(pulse));
    EXPECT_EQ(unlit.state.uv.y, object.state.uv.y);
}

// 0x605727..0x60577d: el 7 suma el ángulo z por 100 al reloj; la L en la luz del blendMesh y la luz cálida de
// radio 3.
TEST(TarkanObjectsTest, TheSevenPulsesWithItsAngle)
{
    auto lighting = std::make_unique<TerrainLighting>();
    ObjectStepGlobals globals;
    globals.worldClock = Clock;
    globals.dynamicLighting = lighting.get();
    auto object = Object(7);
    ASSERT_TRUE(StepTarkanObject(object, globals).HasValue());
    const double pulse = Pulse((30.0 * 100.0) + D(Clock));
    EXPECT_EQ(object.state.blendMesh, 0);
    EXPECT_EQ(object.state.light, F32(pulse));
    EXPECT_EQ(object.state.uv, glm::vec2(0.0f));
    EXPECT_EQ(lighting->cells, LightOf(Warm(pulse), 3)->cells);
}

// La V en (−ftol(T) % 10.000) · 0,0002f: el 0xb, el 0xd, el 0x49, el 0x4b y el 0x4f; el 0x48, además, con el
// blendMesh en 0.
TEST(TarkanObjectsTest, TheWaterTypesScrollTheirV)
{
    const float v = F32(-2345.0 * D(std::bit_cast<float>(0x3951b717u)));
    for (const std::int16_t type : {std::int16_t{0xb}, std::int16_t{0xd}, std::int16_t{0x48}, std::int16_t{0x49},
                                    std::int16_t{0x4b}, std::int16_t{0x4f}})
    {
        SCOPED_TRACE(type);
        ObjectStepGlobals globals;
        globals.worldClock = Clock;
        auto object = Object(type);
        ASSERT_TRUE(StepTarkanObject(object, globals).HasValue());
        EXPECT_EQ(object.state.uv.y, v);
        EXPECT_EQ(object.state.uv.x, 0.0f);
        EXPECT_EQ(object.state.blendMesh, type == 0x48 ? 0 : Client::Render::Models::ObjectState{}.blendMesh);
    }
}

// 0x6057b7..0x6057fd: el 0xc corre la U y la V con (−ftol(T) % 50.000) · 0,00005f, de dos ftol.
TEST(TarkanObjectsTest, TheTwelveScrollsBothCoordinatesSlowly)
{
    ObjectStepGlobals globals;
    globals.worldClock = Clock;
    auto object = Object(0xc);
    ASSERT_TRUE(StepTarkanObject(object, globals).HasValue());
    const float slow = F32(-12345.0 * D(std::bit_cast<float>(0x3851b717u)));
    EXPECT_EQ(object.state.uv, glm::vec2(slow, slow));
}

// 0x605831/0x605855: el 0x3d, el 0x41 y el 0x42 llevan el blendMesh a 1, la V a (−ftol(T) % 1.000) · 0,001f y
// una luz cálida de radio 2, sin tocar la luz del blendMesh.
TEST(TarkanObjectsTest, TheLavaTypesBlendScrollAndLightNarrowly)
{
    for (const std::int16_t type : {std::int16_t{0x3d}, std::int16_t{0x41}, std::int16_t{0x42}})
    {
        SCOPED_TRACE(type);
        auto lighting = std::make_unique<TerrainLighting>();
        ObjectStepGlobals globals;
        globals.worldClock = Clock;
        globals.dynamicLighting = lighting.get();
        auto object = Object(type);
        const float light = object.state.light;
        ASSERT_TRUE(StepTarkanObject(object, globals).HasValue());
        EXPECT_EQ(object.state.blendMesh, 1);
        EXPECT_EQ(object.state.uv.y, F32(-345.0 * D(std::bit_cast<float>(0x3a83126fu))));
        EXPECT_EQ(object.state.light, light);
        EXPECT_EQ(lighting->cells, LightOf(Warm(Pulse(D(Clock))), 2)->cells);
    }
}

// 0x6059dd: el 0x3f y el 0x40 se ocultan (sus sprites van en 0x602ed0); 0x605970: el 0x52 lleva el blendMesh
// a 0 y la luz del objeto a blanco. Un tipo sin caso no cambia.
TEST(TarkanObjectsTest, TheHiddenTheWhiteAndTheRest)
{
    for (const std::int16_t type : {std::int16_t{0x3f}, std::int16_t{0x40}})
    {
        auto object = Object(type);
        ASSERT_TRUE(StepTarkanObject(object, ObjectStepGlobals{}).HasValue());
        EXPECT_EQ(object.state.hiddenMesh, Client::World::HiddenObject);
    }
    auto white = Object(0x52);
    ASSERT_TRUE(StepTarkanObject(white, ObjectStepGlobals{}).HasValue());
    EXPECT_EQ(white.state.blendMesh, 0);
    EXPECT_EQ(white.light, glm::vec3(1.0f));

    auto plain = Object(0x51);
    ASSERT_TRUE(StepTarkanObject(plain, ObjectStepGlobals{}).HasValue());
    EXPECT_EQ(plain.state.blendMesh, Client::Render::Models::ObjectState{}.blendMesh);
    EXPECT_EQ(plain.state.hiddenMesh, Client::Render::Models::ObjectState{}.hiddenMesh);
    EXPECT_EQ(plain.light, Object(0x51).light);
}

// Un reloj que ftol no tiene acreditado corta con la dirección de su ftol; NaN da 0.
TEST(TarkanObjectsTest, AnUnreadClockCutsAtItsFtol)
{
    struct Case
    {
        std::int16_t type;
        const char* address;
    };
    for (const Case& c : {Case{2, "0x605690"}, Case{4, "0x6056df"}, Case{0xb, "0x605792"}, Case{0xc, "0x6057bd"},
                          Case{0xd, "0x60580c"}, Case{0x3d, "0x60583e"}, Case{0x41, "0x605862"}, Case{0x48, "0x6058ca"},
                          Case{0x49, "0x6058f5"}, Case{0x4b, "0x605920"}, Case{0x4f, "0x60594b"}})
    {
        SCOPED_TRACE(c.type);
        auto lighting = std::make_unique<TerrainLighting>();
        ObjectStepGlobals globals;
        globals.worldClock = std::numeric_limits<float>::infinity();
        globals.dynamicLighting = lighting.get();
        auto object = Object(c.type);
        const auto cut = StepTarkanObject(object, globals);
        ASSERT_FALSE(cut.HasValue());
        EXPECT_NE(cut.Error().find(c.address), std::string::npos) << cut.Error();
    }
    ObjectStepGlobals nan;
    nan.worldClock = std::numeric_limits<float>::quiet_NaN();
    auto object = Object(0xc);
    ASSERT_TRUE(StepTarkanObject(object, nan).HasValue());
    EXPECT_EQ(object.state.uv, glm::vec2(0.0f));
}

// 0x604c70..0x604c8e: el 8 anima con la velocidad por 4 en una copia; el +0xd8 queda. Los demás, con la suya,
// y en otro mapa el 8 tampoco cambia.
TEST(TarkanObjectsTest, TheEightAnimatesFourTimesFaster)
{
    EXPECT_EQ(Client::World::TarkanAnimationSpeed(8, 0.25f), 1.0f);
    EXPECT_EQ(Client::World::TarkanAnimationSpeed(9, 0.25f), 0.25f);

    Client::Render::Models::Skeleton skeleton;
    skeleton.actions = {
        Client::Render::Models::ActionInfo{.repeats = true, .speed = 0.0f, .keyCount = 10, .lockPositions = false}};
    for (const std::uint8_t map : {std::uint8_t{8}, std::uint8_t{0}})
    {
        for (const std::int16_t type : {std::int16_t{8}, std::int16_t{9}})
        {
            SCOPED_TRACE(testing::Message() << "mapa " << static_cast<int>(map) << ", tipo " << type);
            auto object = Object(type);
            object.speed = 0.25f;
            object.alpha = 1.0f;
            object.targetAlpha = 1.0f;
            Client::Render::Models::CrtRandom random(7);
            auto lighting = std::make_unique<TerrainLighting>();
            ObjectStepGlobals globals;
            globals.dynamicLighting = lighting.get();
            ASSERT_TRUE(Client::World::StepWorldObjectChecked(object, map, &skeleton, random, globals).HasValue());
            EXPECT_EQ(object.animation.frame, map == 8 && type == 8 ? 1.0f : 0.25f);
            EXPECT_EQ(object.speed, 0.25f);
        }
    }
}

// 0x61a145..0x61a18f: al cargar el mapa 8, el model+0x88 de 11, 12, 13, 73, 75 y 79 en 0. En otro mapa, nada.
TEST(TarkanObjectsTest, TheLoadMarksTheScrolledModelsUnlit)
{
    ObjectModelStepState state;
    Client::World::SeedTarkanModelState(8, state);
    ASSERT_EQ(state.unlitMeshes.size(), 6u);
    for (const std::int16_t type :
         {std::int16_t{11}, std::int16_t{12}, std::int16_t{13}, std::int16_t{73}, std::int16_t{75}, std::int16_t{79}})
        EXPECT_EQ(state.unlitMeshes.at(type), 0) << type;

    ObjectModelStepState other;
    Client::World::SeedTarkanModelState(1, other);
    EXPECT_TRUE(other.unlitMeshes.empty());
}
} // namespace
