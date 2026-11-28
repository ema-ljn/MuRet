#include "Crt/CrtRandom.h"
#include "Data/Terrain/TerrainLight.h"
#include "Models/ObjectState.h"
#include "Math/OriginalRetainedMath.h"
#include "Effects/ModelEffects.h"
#include "Terrain/TerrainLighting.h"
#include "WorldObjects/WorldObjects.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// 0x43ffc0, the Land of Trials objects' step (map 31, 0x43ff10) after 0x438a90 and 0x42c850: 06 § Los objetos de Land
// of Trials.
namespace
{
using Client::Crt::CrtRandom;
using Client::Terrain::TerrainLighting;
using Client::WorldObjects::ObjectStepGlobals;
using Client::WorldObjects::StepWorldObjectChecked;
using Client::WorldObjects::WorldObject;

constexpr std::uint8_t TrialsMap = 31;
constexpr std::int16_t Hidden = -2;
// +0x58 of a freshly created object or effect: −1, all meshes.
const std::int16_t Shown = WorldObject{}.state.hiddenMesh;

WorldObject Object(std::int16_t type)
{
    WorldObject object;
    object.type = type;
    object.transform.position = {1000.0f, 1500.0f, 20.0f};
    object.transform.angles = {0.0f, 0.0f, 45.0f};
    object.transform.scale = 1.5f;
    return object;
}

struct Step
{
    std::unique_ptr<TerrainLighting> lighting = std::make_unique<TerrainLighting>();
    std::unique_ptr<Client::Effects::ModelEffects> effects = std::make_unique<Client::Effects::ModelEffects>();
    std::vector<std::int32_t> sounds;
    std::vector<std::uint32_t> times;
    std::uint32_t ambient = 0;
    std::uint32_t time = 1000;
    ObjectStepGlobals globals;

    Step()
    {
        globals.dynamicLighting = lighting.get();
        globals.effects = effects.get();
        globals.sound = [this](Client::Audio::SoundId sound) { sounds.push_back(sound.Value()); };
        globals.systemTime = [this]
        {
            times.push_back(time);
            return time;
        };
        globals.trialsAmbientTime = &ambient;
        globals.worldClock = 4321.5f;
    }
};
} // namespace

// 0x43fffb, 0x440051 and 0x44008e: with timeGetTime() & 0x3ff below 10, the butterfly 0xae /0, /1 or /2 with light (1,
// 1, 1) and the owner object's scale; with or without it, +0x58 ends at −2. They draw no rand() beyond the creation's.
TEST(LandOfTrialsObjectsTest, TheButterflyObjectsReleaseOneOnlyInsideTheWindowAndHide)
{
    struct Case
    {
        std::int16_t type;
        std::int32_t subtype;
    };
    for (const Case c : {Case{1, 0}, Case{0x2c, 1}, Case{0x2d, 2}})
    {
        SCOPED_TRACE(c.type);
        for (const std::uint32_t time : {0x400u + 9u, 0x400u + 10u})
        {
            Step step;
            step.time = time;
            step.ambient = time;
            auto object = Object(c.type);
            CrtRandom random(7);
            ASSERT_TRUE(StepWorldObjectChecked(object, TrialsMap, nullptr, random, step.globals).HasValue());
            EXPECT_EQ(object.state.hiddenMesh, Hidden);
            const auto& effect = step.effects->Effects()[0];
            if (time % 0x400 < 10)
            {
                ASSERT_TRUE(effect.alive);
                EXPECT_EQ(effect.type, Client::Effects::ButterflyEffect);
                EXPECT_EQ(effect.subtype, c.subtype);
                EXPECT_EQ(effect.transform.position, object.transform.position);
                EXPECT_EQ(effect.light, glm::vec3(1.0f));
                EXPECT_EQ(effect.objectState.hiddenMesh, Shown);
                // 0x51913a: the speed comes from the owner's scale times −5.
                EXPECT_EQ(effect.velocity.y, -7.5f);
                CrtRandom draws(7);
                static_cast<void>(draws.Next());
                EXPECT_EQ(random.State(), draws.State());
            }
            else
            {
                EXPECT_FALSE(effect.alive);
                EXPECT_EQ(random.State(), CrtRandom(7).State());
            }
            // The window and the ambient comparison: two clock reads.
            EXPECT_EQ(step.times.size(), 2u);
            EXPECT_TRUE(step.sounds.empty());
        }
    }
}

// 0x44014a: the 3 and the 0x35 emit in the frame (0x4401e0) and in the step they only hide; the other types do not
// touch +0x58.
TEST(LandOfTrialsObjectsTest, TheEmittersHideAndTheOtherTypesKeepTheirMesh)
{
    for (const std::int16_t type : {std::int16_t{3}, std::int16_t{0x35}, std::int16_t{0x31}, std::int16_t{0x34}})
    {
        SCOPED_TRACE(type);
        Step step;
        step.ambient = step.time;
        auto object = Object(type);
        CrtRandom random(7);
        ASSERT_TRUE(StepWorldObjectChecked(object, TrialsMap, nullptr, random, step.globals).HasValue());
        EXPECT_EQ(object.state.hiddenMesh, type == 3 || type == 0x35 ? Hidden : Shown);
        EXPECT_EQ(object.transform.position, glm::vec3(1000.0f, 1500.0f, 20.0f));
        EXPECT_EQ(random.State(), CrtRandom(7).State());
    }
}

// 0x4400ce..0x4400eb: the 0x1b's z adds sin(clock · 0.0024f + phase) · 0.3f over the previous one, each step; it does
// not hide.
TEST(LandOfTrialsObjectsTest, TheFloatingObjectSwaysItsHeightWithTheClockAndItsPhase)
{
    Step step;
    step.ambient = step.time;
    auto object = Object(0x1b);
    object.field84 = 3.25f;
    CrtRandom random(7);
    float z = object.transform.position.z;
    for (int pass = 0; pass < 3; ++pass)
    {
        ASSERT_TRUE(StepWorldObjectChecked(object, TrialsMap, nullptr, random, step.globals).HasValue());
        const double argument =
            static_cast<double>(step.globals.worldClock) * static_cast<double>(std::bit_cast<float>(0x3b1d4952u)) +
            3.25;
        const auto sway = Client::Math::Detail::SineProductPC53(argument, 0.3f);
        ASSERT_TRUE(sway.has_value());
        z = static_cast<float>(*sway + static_cast<double>(z));
        EXPECT_EQ(object.transform.position.z, z);
        step.globals.worldClock += 40.0f;
    }
    EXPECT_NE(z, 20.0f);
    EXPECT_EQ(object.state.hiddenMesh, Shown);
    EXPECT_EQ(random.State(), CrtRandom(7).State());
}

// 0x4400f0..0x440142: ((rand() & 3) + 3) · 0.1f in the x87, times 0.9f, 0.2f and 0.1f, to the dynamic light with
// radius 3; then +0x58 at −2.
TEST(LandOfTrialsObjectsTest, TheTorchLightsTheDynamicBufferWithOneDrawAndHides)
{
    Step step;
    step.ambient = step.time;
    auto object = Object(0x2a);
    CrtRandom random(7);
    CrtRandom draws(7);
    ASSERT_TRUE(StepWorldObjectChecked(object, TrialsMap, nullptr, random, step.globals).HasValue());
    const double level =
        static_cast<double>((draws.Next() & 3) + 3) * static_cast<double>(std::bit_cast<float>(0x3dcccccdu));
    auto expected = std::make_unique<TerrainLighting>();
    Client::Terrain::AddTerrainLight(
        *expected, 1000.0f, 1500.0f,
        Client::Data::Terrain::TerrainLightColor{static_cast<float>(level * static_cast<double>(0.9f)),
                                                 static_cast<float>(level * static_cast<double>(0.2f)),
                                                 static_cast<float>(level * static_cast<double>(0.1f))},
        3);
    EXPECT_EQ(step.lighting->cells, expected->cells);
    EXPECT_EQ(random.State(), draws.State());
    EXPECT_EQ(object.state.hiddenMesh, Hidden);

    Step missing;
    missing.globals.dynamicLighting = nullptr;
    auto other = Object(0x2a);
    const auto cut = StepWorldObjectChecked(other, TrialsMap, nullptr, random, missing.globals);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x440142"), std::string::npos) << cut.Error();
    EXPECT_EQ(other.state.hiddenMesh, Shown);
}

// 0x440151..0x440170: with any type, if more than 300000 system ms passed since the last (unsigned), a second clock
// read is stored at 0x723c4c and 0x1ef sounds.
TEST(LandOfTrialsObjectsTest, TheAmbientSoundsEveryFiveMinutesOfTheSystemClock)
{
    Step step;
    step.ambient = 1000;
    step.time = 1000 + 300000;
    auto object = Object(0x34);
    CrtRandom random(7);
    ASSERT_TRUE(StepWorldObjectChecked(object, TrialsMap, nullptr, random, step.globals).HasValue());
    EXPECT_TRUE(step.sounds.empty());
    EXPECT_EQ(step.ambient, 1000u);

    step.time = 1000 + 300001;
    ASSERT_TRUE(StepWorldObjectChecked(object, TrialsMap, nullptr, random, step.globals).HasValue());
    EXPECT_EQ(step.sounds, std::vector<std::int32_t>{0x1ef});
    EXPECT_EQ(step.ambient, 301001u);
    EXPECT_EQ(step.times.size(), 3u);

    // Unsigned jbe: with the system clock behind the stored time, the subtraction wraps and it sounds.
    step.time = 5;
    ASSERT_TRUE(StepWorldObjectChecked(object, TrialsMap, nullptr, random, step.globals).HasValue());
    EXPECT_EQ(step.sounds, (std::vector<std::int32_t>{0x1ef, 0x1ef}));
    EXPECT_EQ(step.ambient, 5u);
}

// Without the system clock, the step stops where it reads it; without the ambient time or the emitter, after the
// case's stores. The time is stored before the sound request.
TEST(LandOfTrialsObjectsTest, AMissingBindingCutsWhereTheBinaryReadsIt)
{
    {
        Step step;
        step.globals.systemTime = {};
        auto object = Object(1);
        CrtRandom random(7);
        const auto cut = StepWorldObjectChecked(object, TrialsMap, nullptr, random, step.globals);
        ASSERT_FALSE(cut.HasValue());
        EXPECT_NE(cut.Error().find("0x43fffb"), std::string::npos) << cut.Error();
        EXPECT_EQ(object.state.hiddenMesh, Shown);
    }
    {
        Step step;
        step.globals.trialsAmbientTime = nullptr;
        auto object = Object(3);
        CrtRandom random(7);
        const auto cut = StepWorldObjectChecked(object, TrialsMap, nullptr, random, step.globals);
        ASSERT_FALSE(cut.HasValue());
        EXPECT_NE(cut.Error().find("0x440153"), std::string::npos) << cut.Error();
        EXPECT_EQ(object.state.hiddenMesh, Hidden);
    }
    {
        Step step;
        step.globals.sound = {};
        step.time = 400000;
        auto object = Object(0x1b);
        CrtRandom random(7);
        const auto cut = StepWorldObjectChecked(object, TrialsMap, nullptr, random, step.globals);
        ASSERT_FALSE(cut.HasValue());
        EXPECT_NE(cut.Error().find("0x440170"), std::string::npos) << cut.Error();
        EXPECT_EQ(step.ambient, 400000u);
    }
    {
        Step step;
        step.globals.effects = nullptr;
        step.time = 3;
        auto object = Object(0x2c);
        CrtRandom random(7);
        const auto cut = StepWorldObjectChecked(object, TrialsMap, nullptr, random, step.globals);
        ASSERT_FALSE(cut.HasValue());
        EXPECT_NE(cut.Error().find("0x440044"), std::string::npos) << cut.Error();
    }
}
