#include "Crt/CrtRandom.h"
#include "Data/Terrain/TerrainLight.h"
#include "Models/ObjectState.h"
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

// 0x4250f0, the step of the objects of maps 33 and 54 (0x605a97 after the chain of 0x605a62, with 0x425070): 06 § El
// mundo de la selección.
namespace
{
using Client::Crt::CrtRandom;
using Client::Terrain::TerrainLighting;
using Client::WorldObjects::ObjectStepGlobals;
using Client::WorldObjects::StepWorldObjectChecked;
using Client::WorldObjects::WorldObject;

constexpr std::uint8_t SelectionMap = 54;
constexpr std::uint8_t AidaMap = 33;

WorldObject Object(std::int16_t type)
{
    WorldObject object;
    object.type = type;
    object.transform.position = {1000.0f, 1500.0f, 20.0f};
    return object;
}

struct Step
{
    std::unique_ptr<TerrainLighting> lighting = std::make_unique<TerrainLighting>();
    std::vector<std::int32_t> sounds;
    ObjectStepGlobals globals;

    Step()
    {
        globals.dynamicLighting = lighting.get();
        globals.sound = [this](Client::Audio::SoundId sound) { sounds.push_back(sound.Value()); };
    }
};

// 0x425137..0x42517c (0x1e) and 0x425186..0x4251c9 (0x47): one rand() % 5 times 0.01 that stays in the x87, and each
// channel stored from its sum: green (b + 0.4; b + 0.6; b + 0.4), red (b + 0.9; b + 0.2; b + 0.2); with radius 2 in
// the dynamic buffer. Then, the 0x246.
TEST(AidaObjectsTest, TheGuardiansLightTheDynamicBufferWithOneDraw)
{
    for (const std::uint8_t map : {SelectionMap, AidaMap})
    {
        for (const std::int16_t type : {std::int16_t{0x1e}, std::int16_t{0x47}})
        {
            SCOPED_TRACE(type);
            Step step;
            auto object = Object(type);
            CrtRandom random(7);
            CrtRandom draws(7);
            ASSERT_TRUE(StepWorldObjectChecked(object, map, nullptr, random, step.globals).HasValue());
            const double base =
                static_cast<double>(draws.Next() % 5) * static_cast<double>(std::bit_cast<float>(0x3c23d70au));
            const bool green = type == 0x1e;
            const float first = static_cast<float>(base + static_cast<double>(green ? 0.4f : 0.9f));
            const float second = static_cast<float>(base + static_cast<double>(green ? 0.6f : 0.2f));
            auto expected = std::make_unique<TerrainLighting>();
            Client::Terrain::AddTerrainLight(*expected, 1000.0f, 1500.0f,
                                             green ? Client::Data::Terrain::TerrainLightColor{first, second, first}
                                                   : Client::Data::Terrain::TerrainLightColor{first, second, second},
                                             2);
            EXPECT_EQ(step.lighting->cells, expected->cells);
            EXPECT_EQ(random.State(), draws.State());
            EXPECT_EQ(step.sounds, std::vector<std::int32_t>{0x246});
        }
    }
}

// 0x425126: the V of the 0x19 and the 0x1c drops 0.015 (0x6b8a24) per step; 0x4251d3: the 0x29 at half alpha;
// 0x4251e8: the 0x40 at 0.05 keys per step.
TEST(AidaObjectsTest, TheScrollingTheHalfTransparentAndTheSlowObjectsStoreTheirFields)
{
    Step step;
    for (const std::int16_t type : {std::int16_t{0x19}, std::int16_t{0x1c}})
    {
        auto object = Object(type);
        object.state.uv.y = 0.5f;
        CrtRandom random(7);
        ASSERT_TRUE(StepWorldObjectChecked(object, SelectionMap, nullptr, random, step.globals).HasValue());
        EXPECT_EQ(object.state.uv.y, static_cast<float>(0.5 - static_cast<double>(std::bit_cast<float>(0x3c75c28fu))));
        EXPECT_EQ(random.State(), 7u);
    }
    auto glass = Object(0x29);
    CrtRandom random(7);
    ASSERT_TRUE(StepWorldObjectChecked(glass, SelectionMap, nullptr, random, step.globals).HasValue());
    EXPECT_EQ(glass.alpha, 0.5f);
    auto slow = Object(0x40);
    ASSERT_TRUE(StepWorldObjectChecked(slow, SelectionMap, nullptr, random, step.globals).HasValue());
    EXPECT_EQ(slow.speed, std::bit_cast<float>(0x3d4ccccdu));
    EXPECT_EQ(step.sounds, (std::vector<std::int32_t>{0x246, 0x246, 0x246, 0x246}));
}

// 0x4251df: those that emit in the draw (0x425260) stay with +0x58 at −2; the 0x3c does not, because it hides in its
// own draw case.
TEST(AidaObjectsTest, TheEmittersHideTheirMeshButNotTheButterflyPost)
{
    Step step;
    for (const int type : {0x38, 0x39, 0x3a, 0x3b, 0x3e, 0x3f, 0x43, 0x46})
    {
        auto object = Object(static_cast<std::int16_t>(type));
        CrtRandom random(7);
        ASSERT_TRUE(StepWorldObjectChecked(object, SelectionMap, nullptr, random, step.globals).HasValue());
        EXPECT_EQ(object.state.hiddenMesh, Client::WorldObjects::HiddenObject) << type;
    }
    auto post = Object(0x3c);
    CrtRandom random(7);
    ASSERT_TRUE(StepWorldObjectChecked(post, SelectionMap, nullptr, random, step.globals).HasValue());
    EXPECT_EQ(post.state.hiddenMesh, Client::Models::NoHiddenMesh);
}

// 0x4251fb: the 0x246 goes out with every object, also of a type without a case; without an emitter, it cuts after
// the stores. Without the dynamic buffer, the 0x1e cuts at 0x5f9780 after its rand().
TEST(AidaObjectsTest, EveryObjectRequestsTheAmbientSoundAndTheGuardianNeedsTheDynamicLight)
{
    Step step;
    auto plain = Object(0x10);
    CrtRandom random(7);
    ASSERT_TRUE(StepWorldObjectChecked(plain, SelectionMap, nullptr, random, step.globals).HasValue());
    EXPECT_EQ(step.sounds, std::vector<std::int32_t>{0x246});

    ObjectStepGlobals silent;
    auto mist = Object(0x3b);
    const auto cut = StepWorldObjectChecked(mist, SelectionMap, nullptr, random, silent);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x4251fb"), std::string::npos);
    EXPECT_EQ(mist.state.hiddenMesh, Client::WorldObjects::HiddenObject);

    ObjectStepGlobals dark;
    dark.sound = [](Client::Audio::SoundId) {};
    auto guardian = Object(0x1e);
    CrtRandom drawn(7);
    const auto unlit = StepWorldObjectChecked(guardian, SelectionMap, nullptr, drawn, dark);
    ASSERT_FALSE(unlit.HasValue());
    EXPECT_NE(unlit.Error().find("0x42517c"), std::string::npos);
    CrtRandom expected(7);
    static_cast<void>(expected.Next());
    EXPECT_EQ(drawn.State(), expected.State());
}
} // namespace
