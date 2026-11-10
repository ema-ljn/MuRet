#include "Render/Models/Cloth.h"
#include "World/ObjectModelStepState.h"
#include "World/Particles.h"
#include "World/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>
#include <cstdint>
#include <limits>
#include <memory>

namespace
{
TEST(NoriaObjectsTest, ClockCasesStoreTheirNativeUvAndSharedModelSelection)
{
    struct Probe
    {
        std::int16_t type;
        float u;
        float v;
        std::int16_t blend;
    };
    for (const Probe& probe :
         {Probe{0x12, 0.0f, 0.234f, -1}, Probe{0x29, 0.0f, 0.617f, 0}, Probe{0x2a, -0.468f, 0.0f, -1},
          Probe{0x2b, 0.468f, 0.0f, -1}, Probe{0x27, 0.0f, 0.0f, 1}})
    {
        SCOPED_TRACE(probe.type);
        Client::World::WorldObject object;
        object.type = probe.type;
        Client::World::ObjectModelStepState models;
        Client::World::ObjectStepGlobals globals;
        globals.worldClock = 1234.75f;
        globals.modelState = &models;
        Client::Render::Models::CrtRandom random(7);
        const auto stepped = Client::World::StepWorldObjectChecked(object, 3, nullptr, random, globals);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_FLOAT_EQ(object.state.uv.x, probe.u);
        EXPECT_FLOAT_EQ(object.state.uv.y, probe.v);
        EXPECT_EQ(object.state.blendMesh, probe.blend);
        EXPECT_EQ(random.State(), 7u);
        if (probe.type == 0x2a || probe.type == 0x2b)
            EXPECT_EQ(models.unlitMeshes.at(probe.type), 0);
        else
            EXPECT_TRUE(models.unlitMeshes.empty());
    }
}

TEST(NoriaObjectsTest, NoriaNeverRunsTheLoginMarkersOrWhirlpools)
{
    for (const std::int16_t type : {std::int16_t{0x54}, std::int16_t{0x56}, std::int16_t{0x5a}})
    {
        Client::World::WorldObject object;
        object.type = type;
        object.transform.angles.z = 90.0f;
        Client::Render::Models::CrtRandom random(7);
        ASSERT_TRUE(Client::World::StepWorldObjectChecked(object, 3, nullptr, random, {}).HasValue());
        EXPECT_EQ(object.transform.angles.z, 90.0f);
        EXPECT_EQ(object.state.hiddenMesh, -1);
        EXPECT_EQ(object.alpha, 1.0f);
    }
}

TEST(NoriaObjectsTest, NoriaKeepsTheModelAndBlendPrefixWhenTheClockCannotConvert)
{
    for (const std::int16_t type : {std::int16_t{0x29}, std::int16_t{0x2a}, std::int16_t{0x2b}})
    {
        Client::World::WorldObject object;
        object.type = type;
        Client::World::ObjectModelStepState models;
        Client::World::ObjectStepGlobals globals;
        globals.modelState = &models;
        globals.worldClock = std::numeric_limits<float>::infinity();
        Client::Render::Models::CrtRandom random(7);
        const auto stepped = Client::World::StepWorldObjectChecked(object, 3, nullptr, random, globals);
        ASSERT_FALSE(stepped.HasValue());
        if (type == 0x29)
            EXPECT_EQ(object.state.blendMesh, 0);
        else
            EXPECT_EQ(models.unlitMeshes.at(type), 0);
        EXPECT_EQ(random.State(), 7u);
    }
}

TEST(NoriaObjectsTest, ARegisteredTowerEmitsItsMapSparkBeforeScrolling)
{
    Client::World::WorldObject object;
    object.type = 0x142;
    object.transform.position = {1000, 2000, 300};
    auto particles = std::make_unique<Client::World::Particles>();
    Client::World::ObjectStepGlobals globals;
    globals.particles = particles.get();
    globals.worldClock = 1234.75f;
    Client::Render::Models::CrtRandom random(7);
    const auto stepped = Client::World::StepWorldObjectChecked(object, 3, nullptr, random, globals);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    const auto& particle = particles->All().front();
    EXPECT_TRUE(particle.alive);
    EXPECT_EQ(particle.type, 0x5c6);
    EXPECT_EQ(particle.subtype, 9);
    EXPECT_EQ(particle.light, glm::vec3(0.5f));
    EXPECT_FLOAT_EQ(object.state.uv.y, 0.234f);
}
} // namespace
