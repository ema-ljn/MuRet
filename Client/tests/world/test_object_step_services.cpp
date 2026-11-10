#include "Data/Terrain/TerrainGrid.h"
#include "Render/Models/Cloth.h"
#include "World/Particles.h"
#include "World/TerrainLighting.h"
#include "World/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace
{
using Client::Render::Models::CrtRandom;
using Client::World::ObjectStepGlobals;
using Client::World::Particles;
using Client::World::StepWorldObjectChecked;
using Client::World::TerrainLighting;
using Client::World::WorldObject;

WorldObject Object(std::int16_t type)
{
    WorldObject object;
    object.type = type;
    object.transform.position = {1000.0f, 1000.0f, 20.0f};
    object.copiedPosition = object.transform.position;
    object.light = {0.2f, 0.3f, 0.4f};
    return object;
}

const auto& Cell(const TerrainLighting& lighting, std::uint8_t x, std::uint8_t y)
{
    return lighting.cells[Client::Data::Terrain::CellIndex(x, y)];
}

TEST(ObjectStepServicesTest, LampsKeepTheRetainedRedForTheirOtherChannelsAndUseRadiusThree)
{
    for (const auto type : {0x5a, 0x96})
    {
        SCOPED_TRACE(type);
        auto lighting = std::make_unique<TerrainLighting>();
        ObjectStepGlobals globals;
        globals.dynamicLighting = lighting.get();
        auto object = Object(static_cast<std::int16_t>(type));
        CrtRandom random(7);
        const auto stepped = StepWorldObjectChecked(object, 0, nullptr, random, globals);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        const auto& center = Cell(*lighting, 10, 10);
        const std::vector<std::uint32_t> expected =
            type == 0x5a ? std::vector<std::uint32_t>{0x3f333333, 0x3f0f5c29, 0x3ed70a3e}
                         : std::vector<std::uint32_t>{0x3ecccccd, 0x3e75c290, 0x3da3d70b};
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            EXPECT_EQ(std::bit_cast<std::uint32_t>(center[channel]), expected[channel]);
        }
        EXPECT_GT(Cell(*lighting, 12, 10)[0], 0.0f);
        EXPECT_EQ(Cell(*lighting, 13, 10)[0], 0.0f);
        EXPECT_EQ(object.light, glm::vec3(0.2f, 0.3f, 0.4f));
        EXPECT_EQ(random.State(), 0x003d7aaeu);
    }
}

TEST(ObjectStepServicesTest, AFireWithoutAParticleStillLightsItsJitteredPosition)
{
    auto lighting = std::make_unique<TerrainLighting>();
    ObjectStepGlobals globals;
    globals.dynamicLighting = lighting.get();
    auto object = Object(0x32);
    CrtRandom random(7);
    const auto stepped = StepWorldObjectChecked(object, 0, nullptr, random, globals);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(random.State(), 0xbca151d2u);
    EXPECT_GT(Cell(*lighting, 14, 10)[0], 0.0f); // Centro 10,05: radio 4, no 3.
    EXPECT_FLOAT_EQ(Cell(*lighting, 10, 10)[0], 0.784379542f);
    EXPECT_EQ(Cell(*lighting, 15, 10)[0], 0.0f);
}

TEST(ObjectStepServicesTest, FireCreatesTheSelectedSubtypeBeforeLightingAndDoesNotScaleItsOffset)
{
    auto lighting = std::make_unique<TerrainLighting>();
    auto particles = std::make_unique<Particles>();
    ObjectStepGlobals globals;
    globals.dynamicLighting = lighting.get();
    globals.particles = particles.get();
    auto object = Object(0x32);
    object.transform.scale = 3.0f;
    CrtRandom random(2);
    const auto stepped = StepWorldObjectChecked(object, 0, nullptr, random, globals);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    const auto& particle = particles->All().front();
    ASSERT_TRUE(particle.alive);
    EXPECT_EQ(particle.type, 0x5d9);
    EXPECT_EQ(particle.subtype, 2);
    EXPECT_EQ(particle.position, glm::vec3(1005.0f, 992.0f, 218.0f));
    EXPECT_EQ(particle.owner, 0u);
    EXPECT_FLOAT_EQ(particle.light.x, 1.1f);
    EXPECT_GT(Cell(*lighting, 10, 10)[0], 0.0f);
    CrtRandom expected(2);
    for (int draw = 0; draw < 8; ++draw)
    {
        static_cast<void>(expected.Next());
    }
    EXPECT_EQ(random.State(), expected.State());
}

TEST(ObjectStepServicesTest, FullParticlePoolSkipsKernelRandomButKeepsTheFollowingLight)
{
    auto lighting = std::make_unique<TerrainLighting>();
    auto particles = std::make_unique<Particles>();
    for (auto& particle : particles->All())
    {
        particle.alive = true;
    }
    ObjectStepGlobals globals;
    globals.dynamicLighting = lighting.get();
    globals.particles = particles.get();
    auto object = Object(0x82);
    CrtRandom random(2);
    const auto stepped = StepWorldObjectChecked(object, 0, nullptr, random, globals);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(object.state.hiddenMesh, Client::World::HiddenObject);
    EXPECT_GT(Cell(*lighting, 10, 10)[0], 0.0f);
    EXPECT_EQ(random.State(), 0x4cc2cb10u); // Seis llamadas del emisor; ninguna del pool lleno.
}

TEST(ObjectStepServicesTest, SmokeMarkersUseTheirOwnLightAndHideOnlyAfterTheEmissionReturns)
{
    for (const auto type : {0x83, 0x84})
    {
        auto particles = std::make_unique<Particles>();
        ObjectStepGlobals globals;
        globals.particles = particles.get();
        auto object = Object(static_cast<std::int16_t>(type));
        CrtRandom random(7);
        const auto stepped = StepWorldObjectChecked(object, 0, nullptr, random, globals);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        const auto& particle = particles->All().front();
        ASSERT_TRUE(particle.alive);
        EXPECT_EQ(particle.type, 0x5f2);
        EXPECT_EQ(particle.subtype, type == 0x83 ? 0 : 2);
        EXPECT_EQ(particle.position, glm::vec3(1005.0f, 1006.0f, 27.0f));
        EXPECT_EQ(particle.light, object.light);
        EXPECT_EQ(object.state.hiddenMesh, Client::World::HiddenObject);
        CrtRandom expected(7);
        for (int draw = 0; draw < (type == 0x83 ? 6 : 7); ++draw)
        {
            static_cast<void>(expected.Next());
        }
        EXPECT_EQ(random.State(), expected.State());
    }
}

TEST(ObjectStepServicesTest, MissingParticleBindingKeepsRandomPrefixAndDoesNotHideTheMarker)
{
    auto object = Object(0x82);
    CrtRandom random(2);
    const auto stepped = StepWorldObjectChecked(object, 0, nullptr, random, {});
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x53fe70"), std::string::npos);
    EXPECT_NE(object.state.hiddenMesh, Client::World::HiddenObject);
    CrtRandom expected(2);
    for (int draw = 0; draw < 6; ++draw)
    {
        static_cast<void>(expected.Next());
    }
    EXPECT_EQ(random.State(), expected.State());
}

TEST(ObjectStepServicesTest, MissingLightKeepsTheParticleAlreadyEmittedButNotTheMarkerStore)
{
    auto particles = std::make_unique<Particles>();
    ObjectStepGlobals globals;
    globals.particles = particles.get();
    auto object = Object(0x82);
    CrtRandom random(2);
    const auto stepped = StepWorldObjectChecked(object, 0, nullptr, random, globals);
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x52f534"), std::string::npos);
    EXPECT_TRUE(particles->All().front().alive);
    EXPECT_NE(object.state.hiddenMesh, Client::World::HiddenObject);
    CrtRandom expected(2);
    for (int draw = 0; draw < 8; ++draw)
    {
        static_cast<void>(expected.Next());
    }
    EXPECT_EQ(random.State(), expected.State());
}

TEST(ObjectStepServicesTest, AFailureInTheSecondBridgeFireKeepsTheFirstLightAndItsRandomPrefix)
{
    auto lighting = std::make_unique<TerrainLighting>();
    ObjectStepGlobals globals;
    globals.dynamicLighting = lighting.get();
    auto object = Object(0x50);
    CrtRandom random(7);
    const auto stepped = StepWorldObjectChecked(object, 0, nullptr, random, globals);
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x53fe70"), std::string::npos);
    EXPECT_GT(Cell(*lighting, 11, 8)[0], 0.0f);
    EXPECT_EQ(Cell(*lighting, 11, 14)[0], 0.0f);
    CrtRandom expected(7);
    for (int draw = 0; draw < 11; ++draw)
    {
        static_cast<void>(expected.Next());
    }
    EXPECT_EQ(random.State(), expected.State());
}

TEST(ObjectStepServicesTest, BonfireWritesItsFlickerAfterTheFireService)
{
    auto lighting = std::make_unique<TerrainLighting>();
    ObjectStepGlobals globals;
    globals.dynamicLighting = lighting.get();
    auto object = Object(0x34);
    CrtRandom random(7);
    const auto stepped = StepWorldObjectChecked(object, 0, nullptr, random, globals);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_FLOAT_EQ(object.state.light, 0.4f); // R6=6516: %6+4.
    EXPECT_EQ(random.State(), 0x9974714du);
}

TEST(ObjectStepServicesTest, NearSlidingDoorsMoveFromTheCopiedPositionAndSoundEveryStep)
{
    const glm::vec3 hero{1100.0f, 1000.0f, 9000.0f};
    std::vector<std::int32_t> sounds;
    ObjectStepGlobals globals;
    globals.heroPosition = &hero;
    globals.sound = [&](std::int32_t id) { sounds.push_back(id); };
    auto object = Object(0x56);
    object.transform.angles.z = 90.0f;
    CrtRandom random(7);
    for (int step = 0; step < 2; ++step)
    {
        ASSERT_TRUE(StepWorldObjectChecked(object, 2, nullptr, random, globals).HasValue());
        EXPECT_EQ(object.transform.position, glm::vec3(1000.0f, 1200.0f, 20.0f));
    }
    EXPECT_EQ(sounds, (std::vector<std::int32_t>{0x12, 0x12}));
    EXPECT_EQ(random.State(), 7u);
}

TEST(ObjectStepServicesTest, HingedDoorsUseTheirCopiedYawAndSoundAfterTheMutation)
{
    const glm::vec3 hero{1100.0f, 1000.0f, 0.0f};
    for (const auto type : {0x14, 0x41, 0x58})
    {
        auto object = Object(static_cast<std::int16_t>(type));
        object.copiedAngles.z = 270.0f;
        object.transform.angles.z = 17.0f;
        ObjectStepGlobals globals;
        globals.heroPosition = &hero;
        int calls = 0;
        globals.sound = [&](std::int32_t id)
        {
            EXPECT_EQ(id, 0x11);
            EXPECT_EQ(object.transform.angles.z, 380.0f);
            ++calls;
        };
        CrtRandom random(7);
        ASSERT_TRUE(StepWorldObjectChecked(object, 2, nullptr, random, globals).HasValue());
        EXPECT_EQ(calls, 1);
    }
}

TEST(ObjectStepServicesTest, TheExactDistanceBoundaryClosesWithoutSoundAndApproachesBothCoordinates)
{
    const glm::vec3 hero{1200.0f, 1000.0f, 0.0f};
    ObjectStepGlobals globals;
    globals.heroPosition = &hero;
    auto object = Object(0x56);
    object.transform.position = {1200.0f, 900.0f, 77.0f};
    object.transform.angles.z = 60.0f;
    object.copiedAngles.z = 90.0f;
    CrtRandom random(7);
    const auto stepped = StepWorldObjectChecked(object, 2, nullptr, random, globals);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(object.transform.position, glm::vec3(1160.0f, 920.0f, 77.0f));
    EXPECT_EQ(object.transform.angles.z, 70.0f);
    EXPECT_EQ(random.State(), 7u);
}

TEST(ObjectStepServicesTest, EditorModeBypassesDoorBindingsAndMissingSoundPreservesTheOpening)
{
    auto object = Object(0x14);
    ObjectStepGlobals globals;
    globals.editorMode = true;
    CrtRandom random(7);
    ASSERT_TRUE(StepWorldObjectChecked(object, 2, nullptr, random, globals).HasValue());
    EXPECT_EQ(object.transform.angles.z, 0.0f);
    globals.editorMode = false;
    const glm::vec3 hero{1000.0f, 1000.0f, 0.0f};
    globals.heroPosition = &hero;
    const auto stepped = StepWorldObjectChecked(object, 2, nullptr, random, globals);
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x41ed10"), std::string::npos);
    EXPECT_EQ(object.transform.angles.z, 200.0f);
}
TEST(ObjectStepServicesTest, EveryFireCallerKeepsItsNativeOffsetBeforeJitter)
{
    struct Probe
    {
        std::int16_t type;
        std::uint8_t map;
        glm::vec3 position;
    };
    for (const Probe& probe : {Probe{0x32, 0, {1005.0f, 992.0f, 218.0f}}, Probe{0x33, 0, {1005.0f, 962.0f, 78.0f}},
                               Probe{0x34, 0, {1005.0f, 992.0f, 78.0f}}, Probe{0x37, 0, {855.0f, 842.0f, 158.0f}},
                               Probe{0x50, 0, {1095.0f, 792.0f, 48.0f}}, Probe{0x82, 0, {1005.0f, 992.0f, 18.0f}},
                               Probe{0x1e, 2, {1005.0f, 992.0f, 68.0f}}, Probe{0x42, 2, {1005.0f, 992.0f, 68.0f}}})
    {
        SCOPED_TRACE(probe.type);
        auto lighting = std::make_unique<TerrainLighting>();
        auto particles = std::make_unique<Particles>();
        ObjectStepGlobals globals;
        globals.dynamicLighting = lighting.get();
        globals.particles = particles.get();
        auto object = Object(probe.type);
        CrtRandom random(2);
        const auto stepped = StepWorldObjectChecked(object, probe.map, nullptr, random, globals);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        ASSERT_TRUE(particles->All().front().alive);
        EXPECT_EQ(particles->All().front().position, probe.position);
        if (probe.type == 0x37)
        {
            EXPECT_GT(Cell(*lighting, 12, 8)[0], 0.0f); // Segunda antorcha: x=1150, y=850.
        }
        if (probe.type == 0x50)
        {
            EXPECT_GT(Cell(*lighting, 11, 14)[0], 0.0f); // Segundo fuego: y=1200, radio 4.
        }
    }
}

TEST(ObjectStepServicesTest, FireRotatesItsOffsetUsingTheObjectAngles)
{
    auto lighting = std::make_unique<TerrainLighting>();
    auto particles = std::make_unique<Particles>();
    ObjectStepGlobals globals;
    globals.dynamicLighting = lighting.get();
    globals.particles = particles.get();
    auto object = Object(0x33);
    object.transform.angles.z = 90.0f;
    object.transform.scale = 5.0f;
    CrtRandom random(2);
    ASSERT_TRUE(StepWorldObjectChecked(object, 0, nullptr, random, globals).HasValue());
    EXPECT_EQ(particles->All().front().position, glm::vec3(1035.0f, 992.0f, 78.0f));
}

TEST(ObjectStepServicesTest, AllDoorOrientationsKeepTheirNativeOpeningFormulas)
{
    const glm::vec3 hero{1100.0f, 1000.0f, 0.0f};
    struct Probe
    {
        float yaw;
        glm::vec3 position;
        float hingedYaw;
    };
    for (const Probe& probe :
         {Probe{90.0f, {1000.0f, 1200.0f, 20.0f}, -20.0f}, Probe{270.0f, {1000.0f, 800.0f, 20.0f}, 380.0f},
          Probe{-0.0f, {1200.0f, 1000.0f, 20.0f}, 250.0f}, Probe{180.0f, {800.0f, 1000.0f, 20.0f}, 290.0f},
          Probe{45.0f, {1000.0f, 1000.0f, 20.0f}, 45.0f}})
    {
        ObjectStepGlobals globals;
        globals.heroPosition = &hero;
        int sounds = 0;
        globals.sound = [&](std::int32_t) { ++sounds; };
        auto sliding = Object(0x56);
        auto hinged = Object(0x14);
        sliding.transform.angles.z = probe.yaw;
        hinged.transform.angles.z = hinged.copiedAngles.z = probe.yaw;
        CrtRandom random(7);
        ASSERT_TRUE(StepWorldObjectChecked(sliding, 2, nullptr, random, globals).HasValue());
        ASSERT_TRUE(StepWorldObjectChecked(hinged, 2, nullptr, random, globals).HasValue());
        EXPECT_EQ(sliding.transform.position, probe.position);
        EXPECT_EQ(hinged.transform.angles.z, probe.hingedYaw);
        EXPECT_EQ(sounds, 2);
    }
}

TEST(ObjectStepServicesTest, TheDoorDistanceBranchUsesTheRetainedValueBeforeItsFloatStore)
{
    const glm::vec3 hero{199.9999847412109375f, 0.06f, 0.0f};
    ObjectStepGlobals globals;
    globals.heroPosition = &hero;
    int sounds = 0;
    globals.sound = [&](std::int32_t) { ++sounds; };
    auto object = Object(0x56);
    object.copiedPosition = glm::vec3(0.0f);
    CrtRandom random(7);
    ASSERT_TRUE(StepWorldObjectChecked(object, 2, nullptr, random, globals).HasValue());
    EXPECT_EQ(sounds, 1); // Distancia retenida <200; el store f32 ya redondeó a200.
    EXPECT_EQ(object.transform.position.x, 0.0f);
}

TEST(ObjectStepServicesTest, UnorderedDoorComparisonsKeepTheirNativeBranches)
{
    const glm::vec3 hero{1100.0f, 1000.0f, 0.0f};
    ObjectStepGlobals globals;
    globals.heroPosition = &hero;
    globals.sound = [](std::int32_t) {};
    auto object = Object(0x56);
    object.transform.angles.z = std::numeric_limits<float>::quiet_NaN();
    CrtRandom random(7);
    ASSERT_TRUE(StepWorldObjectChecked(object, 2, nullptr, random, globals).HasValue());
    EXPECT_EQ(object.transform.position.x, 1200.0f);
    auto hinged = Object(0x14);
    hinged.copiedAngles.z = std::numeric_limits<float>::quiet_NaN();
    ASSERT_TRUE(StepWorldObjectChecked(hinged, 2, nullptr, random, globals).HasValue());
    EXPECT_EQ(hinged.transform.angles.z, 250.0f);
}
} // namespace
