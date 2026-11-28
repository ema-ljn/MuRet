#include "Crt/CrtRandom.h"
#include "Kernel/WorldObject.h"
#include "Models/ObjectState.h"
#include "Math/OriginalRetainedMath.h"
#include "CastleSiege/CastleSiegeObjectStep.h"
#include "Effects/GameModelEffects.h"
#include "Effects/ModelEffects.h"
#include "Particles/Particles.h"
#include "Terrain/TerrainLighting.h"
#include "WorldObjects/WorldObjectMapCreation.h"
#include "Terrain/DynamicWorldLight.h"
#include "WorldObjects/WorldObjects.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

// The Castle Siege objects (map 30) in the step: 06 § Los objetos de Castle Siege.
namespace
{
using Client::CastleSiege::CastleSiegePrelude;
using Client::Crt::CrtRandom;
using Client::Kernel::ObjectStepGlobals;
using Client::Kernel::WorldObject;
using Client::Terrain::TerrainLighting;

constexpr std::uint8_t CastleSiegeMap = 30;
constexpr std::int16_t Hidden = Client::Kernel::HiddenObject;
const std::int16_t Shown = WorldObject{}.state.hiddenMesh;

WorldObject Object(std::int16_t type)
{
    WorldObject object;
    object.type = type;
    object.transform.position = {9250.0f, 9850.0f, 270.0f};
    object.transform.scale = 1.2f;
    return object;
}

CrtRandom After(std::uint32_t seed, int draws)
{
    CrtRandom random(seed);
    for (int draw = 0; draw < draws; ++draw)
        static_cast<void>(random.Next());
    return random;
}

struct Step
{
    std::unique_ptr<TerrainLighting> lighting = std::make_unique<TerrainLighting>();
    bool siege = false;
    ObjectStepGlobals globals;

    Step()
    {
        globals.dynamicLighting = lighting.get();
        globals.siegeActive = &siege;
        globals.worldClock = 12345.5f;
    }
};

// The light of 0x42c8d4..0x42c904 with the rand() that decides it, independent of the port: L on the x87 and each
// channel stored as a float.
std::unique_ptr<TerrainLighting> TorchLightOf(const WorldObject& object, int draw)
{
    auto lighting = std::make_unique<TerrainLighting>();
    const double level = static_cast<double>((draw & 3) + 3) * static_cast<double>(0.1f);
    const glm::vec3 light(static_cast<float>(level), static_cast<float>(level * static_cast<double>(0.6f)),
                          static_cast<float>(level * static_cast<double>(0.2f)));
    EXPECT_TRUE(
        Client::Terrain::AddDynamicLight(lighting.get(), object.transform.position, light, 3, "oráculo").HasValue());
    return lighting;
}
} // namespace

// 0x63741d..0x637466.
TEST(CastleSiegeObjectsTest, TheBlackFogCoversOnlyTheRectangleWithItsEdges)
{
    using Client::CastleSiege::InTheCastleSiegeFog;
    EXPECT_TRUE(InTheCastleSiegeFog({16100.0f, 18900.0f, 0.0f}));
    EXPECT_TRUE(InTheCastleSiegeFog({19000.0f, 21700.0f, 0.0f}));
    EXPECT_TRUE(InTheCastleSiegeFog({17500.0f, 20000.0f, -50.0f}));
    EXPECT_FALSE(InTheCastleSiegeFog({16099.0f, 20000.0f, 0.0f}));
    EXPECT_FALSE(InTheCastleSiegeFog({19001.0f, 20000.0f, 0.0f}));
    EXPECT_FALSE(InTheCastleSiegeFog({17500.0f, 18899.0f, 0.0f}));
    EXPECT_FALSE(InTheCastleSiegeFog({17500.0f, 21701.0f, 0.0f}));
    EXPECT_FALSE(InTheCastleSiegeFog({std::numeric_limits<float>::quiet_NaN(), 20000.0f, 0.0f}));
}

// 0x42b540: with the music time at 0 or more than 1800 s back, it stores timeGetTime() as a float; always, sound
// 0x1e2. No rand().
TEST(CastleSiegeObjectsTest, ThePreludeRefreshesTheMusicTimeAndAlwaysAsksForTheAmbientSound)
{
    struct Case
    {
        float last;
        std::uint32_t now;
        bool refreshes;
    };
    for (const Case c : {Case{0.0f, 5000u, true}, Case{1000.0f, 1801001u, true}, Case{1000.0f, 1801000u, false},
                         Case{1000.0f, 2000u, false}})
    {
        SCOPED_TRACE(c.now);
        float music = c.last;
        std::vector<std::int32_t> sounds;
        const CastleSiegePrelude prelude{.musicTime = &music,
                                         .systemTime = [&c] { return c.now; },
                                         .sound = [&sounds](Client::Audio::SoundId sound)
                                         { sounds.push_back(sound.Value()); },
                                         .updateMusic =
                                             [&sounds](bool siege)
                                         {
                                             EXPECT_FALSE(siege);
                                             sounds.push_back(-1);
                                         }};
        ASSERT_TRUE(Client::CastleSiege::StepCastleSiegePrelude(prelude).HasValue());
        EXPECT_EQ(music, c.refreshes ? static_cast<float>(c.now) : c.last);
        EXPECT_EQ(sounds, c.refreshes ? (std::vector<std::int32_t>{-1, 0x1e2}) : std::vector<std::int32_t>{0x1e2});
    }
}

// 0x42b5fe: with the siege, after the sound, it stops; without the bindings, where the ASM asks for them.
TEST(CastleSiegeObjectsTest, ThePreludeStopsWithTheSiegeOnOrAMissingBinding)
{
    float music = 0.0f;
    std::vector<std::int32_t> sounds;
    CastleSiegePrelude prelude{.siegeActive = true,
                               .musicTime = &music,
                               .systemTime = [] { return 7000u; },
                               .sound = [&sounds](Client::Audio::SoundId sound) { sounds.push_back(sound.Value()); }};
    const auto siege = Client::CastleSiege::StepCastleSiegePrelude(prelude);
    ASSERT_FALSE(siege.HasValue());
    EXPECT_NE(siege.Error().find("0x42b5fe"), std::string::npos) << siege.Error();
    EXPECT_EQ(sounds, std::vector<std::int32_t>{0x1e2});

    prelude.siegeActive = false;
    prelude.sound = {};
    const auto mute = Client::CastleSiege::StepCastleSiegePrelude(prelude);
    ASSERT_FALSE(mute.HasValue());
    EXPECT_NE(mute.Error().find("0x42b5e9"), std::string::npos) << mute.Error();
    prelude.systemTime = {};
    const auto clockless = Client::CastleSiege::StepCastleSiegePrelude(prelude);
    ASSERT_FALSE(clockless.HasValue());
    EXPECT_NE(clockless.Error().find("0x42b557"), std::string::npos) << clockless.Error();
}

// 0x42c8b1..0x42c904: without the siege, the 0 takes a rand() for its radius 3 light and stays at −2; the 0x35 stays
// at −2 without light. With the siege it is the reverse.
TEST(CastleSiegeObjectsTest, TheTorchesLightTheTerrainOnlyOnTheirSideOfTheSiege)
{
    for (const bool siege : {false, true})
    {
        for (const std::int16_t type : {std::int16_t{0}, std::int16_t{0x35}})
        {
            SCOPED_TRACE(std::to_string(type) + (siege ? " con el asedio" : " sin el asedio"));
            Step step;
            step.siege = siege;
            auto object = Object(type);
            CrtRandom random(11);
            ASSERT_TRUE(
                Client::WorldObjects::StepWorldObjectChecked(object, CastleSiegeMap, nullptr, random, step.globals)
                    .HasValue());
            EXPECT_EQ(object.state.hiddenMesh, Hidden);
            const bool lit = (type == 0) != siege;
            if (lit)
            {
                CrtRandom replay(11);
                const auto expected = TorchLightOf(object, replay.Next());
                EXPECT_EQ(step.lighting->cells, expected->cells);
                EXPECT_EQ(random.State(), After(11, 1).State());
            }
            else
            {
                EXPECT_EQ(step.lighting->cells, TerrainLighting{}.cells);
                EXPECT_EQ(random.State(), CrtRandom(11).State());
            }
        }
    }
}

// 0x42c894..0x42c8a9 and 0x42c950: the 4 scrolls the U with the clock and shows everything; the spearman 0x19f shows
// without the siege and hides with it; the 0x2a, the 0x34 and the 0x36, always hidden. No rand().
TEST(CastleSiegeObjectsTest, TheScrollingWaterTheSpearmenAndTheHiddenMarkers)
{
    Step step;
    auto water = Object(4);
    water.state.hiddenMesh = Hidden;
    CrtRandom random(3);
    ASSERT_TRUE(Client::CastleSiege::StepCastleSiegeObject(water, random, step.globals).HasValue());
    EXPECT_EQ(water.state.hiddenMesh, Shown);
    EXPECT_EQ(water.state.uv.x, static_cast<float>(static_cast<double>(12345.5f) *
                                                   static_cast<double>(std::bit_cast<float>(0x38d1b717u))));
    for (const std::int16_t type : {std::int16_t{0x2a}, std::int16_t{0x34}, std::int16_t{0x36}})
    {
        auto marker = Object(type);
        ASSERT_TRUE(Client::CastleSiege::StepCastleSiegeObject(marker, random, step.globals).HasValue());
        EXPECT_EQ(marker.state.hiddenMesh, Hidden) << type;
    }
    auto spearman = Object(0x19f);
    spearman.state.hiddenMesh = Hidden;
    ASSERT_TRUE(Client::CastleSiege::StepCastleSiegeObject(spearman, random, step.globals).HasValue());
    EXPECT_EQ(spearman.state.hiddenMesh, Shown);
    step.siege = true;
    ASSERT_TRUE(Client::CastleSiege::StepCastleSiegeObject(spearman, random, step.globals).HasValue());
    EXPECT_EQ(spearman.state.hiddenMesh, Hidden);
    EXPECT_EQ(random.State(), CrtRandom(3).State());
}

// 0x42c967..0x42c977: the 0x42 stays at −2 and, with the siege, stops; without the siege byte, it stops before
// touching the object.
TEST(CastleSiegeObjectsTest, TheSiegeOnlyMarkerAndTheMissingSiegeByteStop)
{
    Step step;
    step.siege = true;
    auto marker = Object(0x42);
    CrtRandom random(3);
    const auto siege = Client::CastleSiege::StepCastleSiegeObject(marker, random, step.globals);
    ASSERT_FALSE(siege.HasValue());
    EXPECT_NE(siege.Error().find("0x42c977"), std::string::npos) << siege.Error();
    EXPECT_EQ(marker.state.hiddenMesh, Hidden);

    step.globals.siegeActive = nullptr;
    auto torch = Object(0);
    const auto missing = Client::CastleSiege::StepCastleSiegeObject(torch, random, step.globals);
    ASSERT_FALSE(missing.HasValue());
    EXPECT_NE(missing.Error().find("0x42c850"), std::string::npos) << missing.Error();
    EXPECT_EQ(torch.state.hiddenMesh, Shown);
    EXPECT_EQ(random.State(), CrtRandom(3).State());
}

// 0x42c500: the 0x51 and the 0x53 scroll the V with the clock and keep blendMesh 1 and its light at 1; then,
// 0x42b480 as at creation. The gates, with the siege, stop.
TEST(CastleSiegeObjectsTest, TheSeenFlowsScrollTheirVAndTheGatesStopWithTheSiege)
{
    constexpr float Clock = 9876.25f;
    for (const std::int16_t type : {std::int16_t{0x51}, std::int16_t{0x53}})
    {
        SCOPED_TRACE(type);
        auto object = Object(type);
        ASSERT_TRUE(Client::CastleSiege::StepCastleSiegeSeenObject(object, false, Clock).HasValue());
        EXPECT_EQ(object.state.blendMesh, 1);
        EXPECT_EQ(object.state.light, 1.0f);
        const std::uint32_t bits = type == 0x51 ? 0x3951b717u : 0xb9d1b717u;
        EXPECT_EQ(object.state.uv.y,
                  static_cast<float>(static_cast<double>(Clock) * static_cast<double>(std::bit_cast<float>(bits))));
        auto applied = Object(type);
        applied.state = object.state;
        Client::WorldObjects::ApplyCastleSiegeObjectState(applied, 0);
        EXPECT_EQ(object.state, applied.state);
    }
    for (std::int16_t gate = 0x3d; gate <= 0x40; ++gate)
    {
        auto object = Object(gate);
        ASSERT_TRUE(Client::CastleSiege::StepCastleSiegeSeenObject(object, false, Clock).HasValue()) << gate;
        const auto siege = Client::CastleSiege::StepCastleSiegeSeenObject(object, true, Clock);
        ASSERT_TRUE(siege.HasValue());
        EXPECT_EQ(object.state.hiddenMesh, -1);
    }
}

// 0x51d73b and 0x52973e: the plane 0x135 starts with life 100, the meshes' light at 0.3f and the start point at its
// place; it does not die and, with an owner, z goes up and down (sin(T · 0.001f) + 1) · 200 over the start point.
TEST(CastleSiegeObjectsTest, TheGatePlaneNeverDiesAndFloatsOnlyWithAnOwner)
{
    for (const bool owner : {false, true})
    {
        SCOPED_TRACE(owner);
        Client::Effects::ModelEffects effects;
        const glm::vec3 position(5000.0f, 6000.0f, 300.0f);
        const auto slot = effects.CreateGatePlane(position, glm::vec3(0.0f, 0.0f, 90.0f), glm::vec3(0.5f), owner);
        ASSERT_TRUE(slot.has_value());
        auto& plane = effects.Effects()[*slot];
        EXPECT_EQ(plane.type, Client::Effects::GatePlaneEffect);
        EXPECT_EQ(plane.life, 100);
        EXPECT_EQ(plane.objectState.light, std::bit_cast<float>(0x3e99999au));
        EXPECT_EQ(plane.start, position);
        EXPECT_EQ(plane.hasOwner, owner);
        auto particles = std::make_unique<Client::Particles::Particles>();
        CrtRandom random(9);
        constexpr float Clock = 4000.0f;
        plane.life = 3;
        ASSERT_TRUE(Client::Effects::StepGameModelEffect(plane, nullptr, *particles, Clock, random).HasValue());
        // 0x52973e sets it back to 100 and the common step (StepModelEffect) lowers it: it never reaches 0.
        EXPECT_TRUE(plane.alive);
        EXPECT_EQ(plane.life, 99);
        EXPECT_EQ(random.State(), After(9, 1).State()) << "el sorteo común de 0x5208c5";
        const auto lifted = Client::Math::Detail::SineSumPC53(
            static_cast<double>(Clock) * static_cast<double>(std::bit_cast<float>(0x3a83126fu)), 1.0);
        ASSERT_TRUE(lifted.has_value());
        EXPECT_EQ(plane.transform.position.z,
                  owner ? static_cast<float>(*lifted * 200.0 + static_cast<double>(position.z)) : position.z);
    }
}
