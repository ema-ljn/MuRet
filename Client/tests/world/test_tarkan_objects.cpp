#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Models/ObjectState.h"
#include "Math/OriginalRetainedMath.h"
#include "Models/Skeleton.h"
#include "Kernel/WorldObject.h"
#include "MapScripts/Tarkan/TarkanObjectStep.h"
#include "Terrain/TerrainLighting.h"
#include "Terrain/DynamicWorldLight.h"
#include "WorldObjects/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>

// Tarkan's cases of 0x604ab0 (map 8, the table 0x605cc0 by type − 2), the 8's speed and the load's model+0x88 (06 §
// Los objetos de Tarkan).
namespace
{
using Client::Kernel::ObjectModelStepState;
using Client::Kernel::ObjectStepGlobals;
using Client::Kernel::WorldObject;
using Client::MapScripts::Tarkan::StepTarkanObject;
using Client::Terrain::TerrainLighting;

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

using Client::Math::D;
using Client::Math::F32;

// sin(phase · 0.002f) · 0.35f + 0.65f, with the constants of 0x6b89bc, 0x6b8bc4 and 0x6b8bc0: FSIN's 64-bit sine times
// the float with a single R53 (0x6056c8..0x6056d0).
double Pulse(double phase)
{
    const std::optional<double> wave = Client::Math::Detail::SineProductPC53(
        phase * D(std::bit_cast<float>(0x3b03126fu)), std::bit_cast<float>(0x3eb33333u));
    EXPECT_TRUE(wave.has_value());
    return wave.value_or(0.0) + D(std::bit_cast<float>(0x3f266666u));
}

// The warm light: (L; L · 0.6f; L · 0.2f), each stored in float from the x87.
glm::vec3 Warm(double pulse)
{
    return {F32(pulse), F32(pulse * D(std::bit_cast<float>(0x3f19999au))),
            F32(pulse * D(std::bit_cast<float>(0x3e4ccccdu)))};
}

// What 0x5f9780 leaves with that color and that radius at the object's place.
std::unique_ptr<TerrainLighting> LightOf(const glm::vec3& color, std::int32_t radius)
{
    auto lighting = std::make_unique<TerrainLighting>();
    EXPECT_TRUE(Client::Terrain::AddDynamicLight(lighting.get(), Object(0).transform.position, color, radius, "test")
                    .HasValue());
    return lighting;
}

// 0x605683: the 2 takes the blendMesh to 0 and the U to (−ftol(T) % 1000) · 0.001f.
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

// 0x6056b5..0x605718: the 4 leaves L stored in the blendMesh light, the V at (−ftol(T) % 10000) · 0.0001f and a light
// (L; L; L) of radius 3. Without a light destination it cuts at 0x605718 after what it already wrote.
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

// 0x605727..0x60577d: the 7 adds the z angle times 100 to the clock; L in the blendMesh light and the warm light of
// radius 3.
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

// The V at (−ftol(T) % 10000) · 0.0002f: the 0xb, the 0xd, the 0x49, the 0x4b and the 0x4f; the 0x48, also with the
// blendMesh at 0.
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
        EXPECT_EQ(object.state.blendMesh, type == 0x48 ? 0 : Client::Models::ObjectState{}.blendMesh);
    }
}

// 0x6057b7..0x6057fd: the 0xc scrolls the U and the V with (−ftol(T) % 50000) · 0.00005f, from two ftol.
TEST(TarkanObjectsTest, TheTwelveScrollsBothCoordinatesSlowly)
{
    ObjectStepGlobals globals;
    globals.worldClock = Clock;
    auto object = Object(0xc);
    ASSERT_TRUE(StepTarkanObject(object, globals).HasValue());
    const float slow = F32(-12345.0 * D(std::bit_cast<float>(0x3851b717u)));
    EXPECT_EQ(object.state.uv, glm::vec2(slow, slow));
}

// 0x605831/0x605855: the 0x3d, the 0x41 and the 0x42 take the blendMesh to 1, the V to (−ftol(T) % 1000) · 0.001f and
// a warm light of radius 2, without touching the blendMesh light.
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

// 0x6059dd: the 0x3f and the 0x40 hide (their sprites go in 0x602ed0); 0x605970: the 0x52 takes the blendMesh to 0
// and the object's light to white. A type without a case does not change.
TEST(TarkanObjectsTest, TheHiddenTheWhiteAndTheRest)
{
    for (const std::int16_t type : {std::int16_t{0x3f}, std::int16_t{0x40}})
    {
        auto object = Object(type);
        ASSERT_TRUE(StepTarkanObject(object, ObjectStepGlobals{}).HasValue());
        EXPECT_EQ(object.state.hiddenMesh, Client::Kernel::HiddenObject);
    }
    auto white = Object(0x52);
    ASSERT_TRUE(StepTarkanObject(white, ObjectStepGlobals{}).HasValue());
    EXPECT_EQ(white.state.blendMesh, 0);
    EXPECT_EQ(white.light, glm::vec3(1.0f));

    auto plain = Object(0x51);
    ASSERT_TRUE(StepTarkanObject(plain, ObjectStepGlobals{}).HasValue());
    EXPECT_EQ(plain.state.blendMesh, Client::Models::ObjectState{}.blendMesh);
    EXPECT_EQ(plain.state.hiddenMesh, Client::Models::ObjectState{}.hiddenMesh);
    EXPECT_EQ(plain.light, Object(0x51).light);
}

// A clock that ftol has not credited cuts with its ftol's address; NaN gives 0. The 4 and the 7 do the fsin first
// (0x6056c8 and 0x605755), which with that clock leaves the PC53 profile and cuts first.
TEST(TarkanObjectsTest, AnUnreadClockCutsAtItsFtol)
{
    struct Case
    {
        std::int16_t type;
        const char* address;
    };
    for (const Case& c :
         {Case{2, "0x605690"}, Case{4, "0x6056c8"}, Case{7, "0x605755"}, Case{0xb, "0x605792"}, Case{0xc, "0x6057bd"},
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

// 0x604c70..0x604c8e: the 8 animates with the speed times 4 in a copy; +0xd8 stays. The others, with their own, and on
// another map the 8 does not change either.
TEST(TarkanObjectsTest, TheEightAnimatesFourTimesFaster)
{
    EXPECT_EQ(Client::MapScripts::Tarkan::TarkanAnimationSpeed(8, 0.25f), 1.0f);
    EXPECT_EQ(Client::MapScripts::Tarkan::TarkanAnimationSpeed(9, 0.25f), 0.25f);

    Client::Models::Skeleton skeleton;
    skeleton.actions = {
        Client::Models::ActionInfo{.repeats = true, .speed = 0.0f, .keyCount = 10, .lockPositions = false}};
    for (const std::uint8_t map : {std::uint8_t{8}, std::uint8_t{0}})
    {
        for (const std::int16_t type : {std::int16_t{8}, std::int16_t{9}})
        {
            SCOPED_TRACE(testing::Message() << "mapa " << static_cast<int>(map) << ", tipo " << type);
            auto object = Object(type);
            object.speed = 0.25f;
            object.alpha = 1.0f;
            object.targetAlpha = 1.0f;
            Client::Crt::CrtRandom random(7);
            auto lighting = std::make_unique<TerrainLighting>();
            ObjectStepGlobals globals;
            globals.dynamicLighting = lighting.get();
            ASSERT_TRUE(
                Client::WorldObjects::StepWorldObjectChecked(object, map, &skeleton, random, globals).HasValue());
            EXPECT_EQ(object.animation.frame, map == 8 && type == 8 ? 1.0f : 0.25f);
            EXPECT_EQ(object.speed, 0.25f);
        }
    }
}

// 0x61a145..0x61a18f: on loading map 8, the model+0x88 of 11, 12, 13, 73, 75 and 79 at 0. On another map, nothing.
TEST(TarkanObjectsTest, TheLoadMarksTheScrolledModelsUnlit)
{
    ObjectModelStepState state;
    Client::MapScripts::Tarkan::SeedTarkanModelState(8, state);
    ASSERT_EQ(state.unlitMeshes.size(), 6u);
    for (const std::int16_t type :
         {std::int16_t{11}, std::int16_t{12}, std::int16_t{13}, std::int16_t{73}, std::int16_t{75}, std::int16_t{79}})
        EXPECT_EQ(state.unlitMeshes.at(type), 0) << type;

    ObjectModelStepState other;
    Client::MapScripts::Tarkan::SeedTarkanModelState(1, other);
    EXPECT_TRUE(other.unlitMeshes.empty());
}
} // namespace
