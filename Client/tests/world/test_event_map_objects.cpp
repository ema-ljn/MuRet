#include "Crt/CrtRandom.h"
#include "Kernel/WorldObject.h"
#include "Math/X87.h"
#include "Models/ObjectState.h"
#include "Events/EventMapObjectStep.h"
#include "Particles/Particles.h"
#include "Terrain/TerrainLighting.h"
#include "Terrain/DynamicWorldLight.h"
#include "WorldObjects/WorldObjects.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// 0x604ab0 with Devil Square (map 9) and the Blood Castles (11 to 17), and the Blood Castle part of 0x6061c0 (06 § Los
// objetos de Devil Square y de Blood Castle).
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ObjectStepGlobals;
using Client::Kernel::WorldObject;
using Client::Terrain::TerrainLighting;
using Client::WorldObjects::StepWorldObjectChecked;

constexpr std::uint8_t DevilSquareMap = 9;

using Client::Math::F32;

double Bits(std::uint32_t bits)
{
    return static_cast<double>(std::bit_cast<float>(bits));
}

WorldObject Object(std::int16_t type)
{
    WorldObject object;
    object.type = type;
    object.transform.position = {1000.0f, 1500.0f, 20.0f};
    object.transform.scale = 1.0f;
    return object;
}

struct Step
{
    std::unique_ptr<TerrainLighting> lighting = std::make_unique<TerrainLighting>();
    glm::vec3 hero{3000.5f, 4000.25f, 100.0f};
    std::vector<std::int32_t> looped;
    ObjectStepGlobals globals;

    explicit Step(float clock)
    {
        globals.dynamicLighting = lighting.get();
        globals.heroPosition = &hero;
        globals.worldClock = clock;
        globals.loopedSound = [this](Client::Audio::SoundId sound) { looped.push_back(sound.Value()); };
    }
};

// The first seed whose first rand() % 100 gives 0.
std::uint32_t FlashingSeed()
{
    for (std::uint32_t seed = 1;; ++seed)
    {
        CrtRandom probe(seed);
        if (probe.Next() % 100 == 0)
            return seed;
    }
}
} // namespace

// 0x604ac9..0x604ae2: outside the window (ftol(clock) % 4000 from 1000) it draws no rand() and lights nothing; ambient
// 1 plays looped with each object.
TEST(EventMapObjectsTest, TheDevilSquareAmbientLoopsOutsideTheFlashWindow)
{
    Step step(5500.0f);
    WorldObject object = Object(3);
    CrtRandom random(7);
    const auto stepped = StepWorldObjectChecked(object, DevilSquareMap, nullptr, random, step.globals);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(random.State(), CrtRandom(7).State());
    EXPECT_EQ(step.looped, std::vector<std::int32_t>{1});
    EXPECT_TRUE(step.lighting->cells == TerrainLighting{}.cells);
}

// 0x604ae8..0x604b8e: inside the window, with rand() % 100 at 0, the light of radius 12 with L = (r % 12 + 4) · 0.1f
// and (L · 0.2f, L · 0.3f, L · 0.5) on the hero shifted by rand() % 1200 − 600, y before x.
TEST(EventMapObjectsTest, TheDevilSquareFlashLightsAroundTheHeroInsideTheWindow)
{
    const std::uint32_t seed = FlashingSeed();
    for (const float clock : {4500.0f, -500.0f})
    {
        SCOPED_TRACE(clock);
        Step step(clock);
        CrtRandom random(seed);
        ASSERT_TRUE(Client::Events::StepDevilSquarePrelude(random, step.globals).HasValue());
        CrtRandom draws(seed);
        static_cast<void>(draws.Next());
        const double level = (draws.Next() % 12 + 4) * Bits(0x3dcccccdu);
        const glm::vec3 color{F32(level * Bits(0x3e4ccccdu)), F32(level * Bits(0x3e99999au)), F32(level * 0.5)};
        const float y = F32((draws.Next() % 1200 + static_cast<double>(step.hero.y)) - 600.0);
        const float x = F32((draws.Next() % 1200 + static_cast<double>(step.hero.x)) - 600.0);
        EXPECT_EQ(random.State(), draws.State());
        auto expected = std::make_unique<TerrainLighting>();
        ASSERT_TRUE(Client::Terrain::AddDynamicLight(expected.get(), {x, y, 0.0f}, color, 12, "test").HasValue());
        EXPECT_TRUE(step.lighting->cells == expected->cells);
        EXPECT_EQ(step.looped, std::vector<std::int32_t>{1});
    }
}

// 0x604b53 and 0x604b9b: without the hero it stops after three rand(); without the looped sound, after the light.
TEST(EventMapObjectsTest, TheDevilSquarePreludeCutsWhereItsBindingsAreRead)
{
    const std::uint32_t seed = FlashingSeed();
    Step noHero(4500.0f);
    noHero.globals.heroPosition = nullptr;
    CrtRandom random(seed);
    const auto cut = Client::Events::StepDevilSquarePrelude(random, noHero.globals);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x604b53"), std::string::npos) << cut.Error();
    CrtRandom draws(seed);
    for (int k = 0; k < 3; ++k)
        static_cast<void>(draws.Next());
    EXPECT_EQ(random.State(), draws.State());

    Step silent(5500.0f);
    silent.globals.loopedSound = {};
    WorldObject object = Object(3);
    CrtRandom quiet(7);
    const auto unheard = StepWorldObjectChecked(object, DevilSquareMap, nullptr, quiet, silent.globals);
    ASSERT_FALSE(unheard.HasValue());
    EXPECT_NE(unheard.Error().find("0x604b9b"), std::string::npos) << unheard.Error();
}

// 0x605991: in the Blood Castles, 9 and 10 stay hidden except with +0x8a at 4; other types, nothing.
TEST(EventMapObjectsTest, TheBloodCastleGatesHideUntilTheyOpen)
{
    for (std::uint8_t map = 11; map <= 17; ++map)
    {
        SCOPED_TRACE(static_cast<int>(map));
        for (const std::int16_t type : {std::int16_t{9}, std::int16_t{10}, std::int16_t{11}})
        {
            WorldObject closed = Object(type);
            WorldObject open = Object(type);
            open.field8a = 4;
            CrtRandom random(7);
            ASSERT_TRUE(StepWorldObjectChecked(closed, map, nullptr, random, ObjectStepGlobals{}).HasValue());
            ASSERT_TRUE(StepWorldObjectChecked(open, map, nullptr, random, ObjectStepGlobals{}).HasValue());
            EXPECT_EQ(closed.state.hiddenMesh,
                      type == 11 ? WorldObject{}.state.hiddenMesh : Client::Kernel::HiddenObject);
            EXPECT_EQ(open.state.hiddenMesh, WorldObject{}.state.hiddenMesh);
            EXPECT_EQ(random.State(), CrtRandom(7).State());
        }
    }
}

// 0x6062e6..0x6063c3: a point around the hero with three rand() and, with rand() & 3 at 0, the orbit 0x60f/3 there,
// with light (1, 1, 1) and scale 0.19f; otherwise none. Without the pool, stops only if it had to create it.
TEST(EventMapObjectsTest, TheBloodCastlePreludeReleasesAnOrbitAroundTheHero)
{
    const glm::vec3 hero{3000.5f, 4000.25f, 100.0f};
    const float clock = 1000.0f;
    int created = 0;
    int skipped = 0;
    for (std::uint32_t seed = 1; seed <= 16; ++seed)
    {
        SCOPED_TRACE(seed);
        CrtRandom draws(seed);
        const int dx = draws.Next() % 900 - 300;
        const int dy = draws.Next() % 900 - 300;
        const int dz = draws.Next() % 50;
        const bool releases = draws.Next() % 4 == 0;
        Client::Particles::Particles particles;
        CrtRandom random(seed);
        ASSERT_TRUE(Client::Events::StepBloodCastlePrelude(random, hero, &particles, clock).HasValue());
        CrtRandom without(seed);
        const auto bare = Client::Events::StepBloodCastlePrelude(without, hero, nullptr, clock);
        EXPECT_EQ(bare.HasValue(), !releases);
        if (!releases)
        {
            ++skipped;
            EXPECT_FALSE(particles.Any());
            EXPECT_EQ(random.State(), draws.State());
            continue;
        }
        ++created;
        EXPECT_NE(bare.Error().find("0x6063be"), std::string::npos) << bare.Error();
        const Client::Particles::Particle& orbit = particles.All()[0];
        EXPECT_EQ(orbit.type, Client::Particles::OrbitParticle);
        EXPECT_EQ(orbit.subtype, 3);
        EXPECT_EQ(orbit.start, glm::vec3(F32(dx + static_cast<double>(hero.x)), F32(dy + static_cast<double>(hero.y)),
                                         F32((dz + static_cast<double>(hero.z)) + 250.0)));
        EXPECT_EQ(orbit.light, glm::vec3(1.0f));
        EXPECT_EQ(orbit.life, Client::Particles::OrbitLife);
    }
    EXPECT_GT(created, 0);
    EXPECT_GT(skipped, 0);
}
