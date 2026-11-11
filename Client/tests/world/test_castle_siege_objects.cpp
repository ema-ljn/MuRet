#include "Render/Models/Cloth.h"
#include "Render/Models/ObjectState.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "World/CastleSiegeObjectStep.h"
#include "World/GameModelEffects.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/TerrainLighting.h"
#include "World/WorldObjectMapCreation.h"
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
#include <vector>

// Los objetos de Castle Siege (el mapa 30) en el paso: 06 § Los objetos de Castle Siege.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::World::CastleSiegePrelude;
using Client::World::ObjectStepGlobals;
using Client::World::TerrainLighting;
using Client::World::WorldObject;

constexpr std::uint8_t CastleSiegeMap = 30;
constexpr std::int16_t Hidden = Client::World::HiddenObject;
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

// La luz de 0x42c8d4..0x42c904 con el rand() que la decide, independiente del port: L en la x87 y cada
// canal guardado en float.
std::unique_ptr<TerrainLighting> TorchLightOf(const WorldObject& object, int draw)
{
    auto lighting = std::make_unique<TerrainLighting>();
    const double level = static_cast<double>((draw & 3) + 3) * static_cast<double>(0.1f);
    const glm::vec3 light(static_cast<float>(level), static_cast<float>(level * static_cast<double>(0.6f)),
                          static_cast<float>(level * static_cast<double>(0.2f)));
    EXPECT_TRUE(
        Client::World::AddDynamicLight(lighting.get(), object.transform.position, light, 3, "oráculo").HasValue());
    return lighting;
}
} // namespace

// 0x63741d..0x637466: la niebla negra solo dentro del rectángulo, con los bordes.
TEST(CastleSiegeObjectsTest, TheBlackFogCoversOnlyTheRectangleWithItsEdges)
{
    using Client::World::InTheCastleSiegeFog;
    EXPECT_TRUE(InTheCastleSiegeFog({16100.0f, 18900.0f, 0.0f}));
    EXPECT_TRUE(InTheCastleSiegeFog({19000.0f, 21700.0f, 0.0f}));
    EXPECT_TRUE(InTheCastleSiegeFog({17500.0f, 20000.0f, -50.0f}));
    EXPECT_FALSE(InTheCastleSiegeFog({16099.0f, 20000.0f, 0.0f}));
    EXPECT_FALSE(InTheCastleSiegeFog({19001.0f, 20000.0f, 0.0f}));
    EXPECT_FALSE(InTheCastleSiegeFog({17500.0f, 18899.0f, 0.0f}));
    EXPECT_FALSE(InTheCastleSiegeFog({17500.0f, 21701.0f, 0.0f}));
    EXPECT_FALSE(InTheCastleSiegeFog({std::numeric_limits<float>::quiet_NaN(), 20000.0f, 0.0f}));
}

// 0x42b540: con la hora de la música en 0 o más de 1.800 s atrás, guarda timeGetTime() en float; siempre, el
// sonido 0x1e2. Sin rand().
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
                                         .sound = [&sounds](std::int32_t sound) { sounds.push_back(sound); }};
        ASSERT_TRUE(Client::World::StepCastleSiegePrelude(prelude).HasValue());
        EXPECT_EQ(music, c.refreshes ? static_cast<float>(c.now) : c.last);
        EXPECT_EQ(sounds, std::vector<std::int32_t>{0x1e2});
    }
}

// 0x42b5f8: con el asedio, después del sonido, corta; sin los bindings, donde los pide el ASM.
TEST(CastleSiegeObjectsTest, ThePreludeStopsWithTheSiegeOnOrAMissingBinding)
{
    float music = 0.0f;
    std::vector<std::int32_t> sounds;
    CastleSiegePrelude prelude{.siegeActive = true,
                               .musicTime = &music,
                               .systemTime = [] { return 7000u; },
                               .sound = [&sounds](std::int32_t sound) { sounds.push_back(sound); }};
    const auto siege = Client::World::StepCastleSiegePrelude(prelude);
    ASSERT_FALSE(siege.HasValue());
    EXPECT_NE(siege.Error().find("0x42b5f8"), std::string::npos) << siege.Error();
    EXPECT_EQ(sounds, std::vector<std::int32_t>{0x1e2});

    prelude.siegeActive = false;
    prelude.sound = {};
    const auto mute = Client::World::StepCastleSiegePrelude(prelude);
    ASSERT_FALSE(mute.HasValue());
    EXPECT_NE(mute.Error().find("0x42b5e9"), std::string::npos) << mute.Error();
    prelude.systemTime = {};
    const auto clockless = Client::World::StepCastleSiegePrelude(prelude);
    ASSERT_FALSE(clockless.HasValue());
    EXPECT_NE(clockless.Error().find("0x42b557"), std::string::npos) << clockless.Error();
}

// 0x42c8b1..0x42c904: sin el asedio, el 0 saca un rand() para su luz de radio 3 y queda en −2; el 0x35 queda en
// −2 sin luz. Con el asedio es al revés.
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
            ASSERT_TRUE(Client::World::StepWorldObjectChecked(object, CastleSiegeMap, nullptr, random, step.globals)
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

// 0x42c894..0x42c8a9 y 0x42c950: el 4 corre la U con el reloj y muestra todo; el lancero 0x19f se muestra sin el
// asedio y se oculta con él; el 0x2a, el 0x34 y el 0x36, siempre ocultos. Sin rand().
TEST(CastleSiegeObjectsTest, TheScrollingWaterTheSpearmenAndTheHiddenMarkers)
{
    Step step;
    auto water = Object(4);
    water.state.hiddenMesh = Hidden;
    CrtRandom random(3);
    ASSERT_TRUE(Client::World::StepCastleSiegeObject(water, random, step.globals).HasValue());
    EXPECT_EQ(water.state.hiddenMesh, Shown);
    EXPECT_EQ(water.state.uv.x, static_cast<float>(static_cast<double>(12345.5f) *
                                                   static_cast<double>(std::bit_cast<float>(0x38d1b717u))));
    for (const std::int16_t type : {std::int16_t{0x2a}, std::int16_t{0x34}, std::int16_t{0x36}})
    {
        auto marker = Object(type);
        ASSERT_TRUE(Client::World::StepCastleSiegeObject(marker, random, step.globals).HasValue());
        EXPECT_EQ(marker.state.hiddenMesh, Hidden) << type;
    }
    auto spearman = Object(0x19f);
    spearman.state.hiddenMesh = Hidden;
    ASSERT_TRUE(Client::World::StepCastleSiegeObject(spearman, random, step.globals).HasValue());
    EXPECT_EQ(spearman.state.hiddenMesh, Shown);
    step.siege = true;
    ASSERT_TRUE(Client::World::StepCastleSiegeObject(spearman, random, step.globals).HasValue());
    EXPECT_EQ(spearman.state.hiddenMesh, Hidden);
    EXPECT_EQ(random.State(), CrtRandom(3).State());
}

// 0x42c967..0x42c977: el 0x42 queda en −2 y, con el asedio, corta; sin el byte del asedio, corta antes de tocar
// el objeto.
TEST(CastleSiegeObjectsTest, TheSiegeOnlyMarkerAndTheMissingSiegeByteStop)
{
    Step step;
    step.siege = true;
    auto marker = Object(0x42);
    CrtRandom random(3);
    const auto siege = Client::World::StepCastleSiegeObject(marker, random, step.globals);
    ASSERT_FALSE(siege.HasValue());
    EXPECT_NE(siege.Error().find("0x42c977"), std::string::npos) << siege.Error();
    EXPECT_EQ(marker.state.hiddenMesh, Hidden);

    step.globals.siegeActive = nullptr;
    auto torch = Object(0);
    const auto missing = Client::World::StepCastleSiegeObject(torch, random, step.globals);
    ASSERT_FALSE(missing.HasValue());
    EXPECT_NE(missing.Error().find("0x42c850"), std::string::npos) << missing.Error();
    EXPECT_EQ(torch.state.hiddenMesh, Shown);
    EXPECT_EQ(random.State(), CrtRandom(3).State());
}

// 0x42c500: el 0x51 y el 0x53 corren la V con el reloj y quedan con el blendMesh 1 y su luz en 1; después,
// 0x42b480 como en la creación. Las puertas, con el asedio, cortan.
TEST(CastleSiegeObjectsTest, TheSeenFlowsScrollTheirVAndTheGatesStopWithTheSiege)
{
    constexpr float Clock = 9876.25f;
    for (const std::int16_t type : {std::int16_t{0x51}, std::int16_t{0x53}})
    {
        SCOPED_TRACE(type);
        auto object = Object(type);
        ASSERT_TRUE(Client::World::StepCastleSiegeSeenObject(object, false, Clock).HasValue());
        EXPECT_EQ(object.state.blendMesh, 1);
        EXPECT_EQ(object.state.light, 1.0f);
        const std::uint32_t bits = type == 0x51 ? 0x3951b717u : 0xb9d1b717u;
        EXPECT_EQ(object.state.uv.y,
                  static_cast<float>(static_cast<double>(Clock) * static_cast<double>(std::bit_cast<float>(bits))));
        auto applied = Object(type);
        applied.state = object.state;
        Client::World::ApplyCastleSiegeObjectState(applied, 0);
        EXPECT_EQ(object.state, applied.state);
    }
    for (std::int16_t gate = 0x3d; gate <= 0x40; ++gate)
    {
        auto object = Object(gate);
        ASSERT_TRUE(Client::World::StepCastleSiegeSeenObject(object, false, Clock).HasValue()) << gate;
        const auto siege = Client::World::StepCastleSiegeSeenObject(object, true, Clock);
        ASSERT_FALSE(siege.HasValue());
        EXPECT_NE(siege.Error().find("0x42c596"), std::string::npos) << siege.Error();
    }
}

// 0x51d73b y 0x52973e: el plano 0x135 nace con vida 100, la luz de las mallas en 0,3f y la partida en su
// lugar; no muere y, con dueño, la z sube y baja (sin(T · 0,001f) + 1) · 200 sobre la partida.
TEST(CastleSiegeObjectsTest, TheGatePlaneNeverDiesAndFloatsOnlyWithAnOwner)
{
    for (const bool owner : {false, true})
    {
        SCOPED_TRACE(owner);
        Client::World::ModelEffects effects;
        const glm::vec3 position(5000.0f, 6000.0f, 300.0f);
        const auto slot = effects.CreateGatePlane(position, glm::vec3(0.0f, 0.0f, 90.0f), glm::vec3(0.5f), owner);
        ASSERT_TRUE(slot.has_value());
        auto& plane = effects.Effects()[*slot];
        EXPECT_EQ(plane.type, Client::World::GatePlaneEffect);
        EXPECT_EQ(plane.life, 100);
        EXPECT_EQ(plane.objectState.light, std::bit_cast<float>(0x3e99999au));
        EXPECT_EQ(plane.start, position);
        EXPECT_EQ(plane.hasOwner, owner);
        auto particles = std::make_unique<Client::World::Particles>();
        CrtRandom random(9);
        constexpr float Clock = 4000.0f;
        plane.life = 3;
        ASSERT_TRUE(Client::World::StepGameModelEffect(plane, nullptr, *particles, Clock, random).HasValue());
        // 0x52973e la repone en 100 y el paso común (StepModelEffect) la baja: nunca llega a 0.
        EXPECT_TRUE(plane.alive);
        EXPECT_EQ(plane.life, 99);
        EXPECT_EQ(random.State(), After(9, 1).State()) << "el sorteo común de 0x5208c5";
        const auto lifted = Client::Render::Models::Detail::SineSumPC53(
            static_cast<double>(Clock) * static_cast<double>(std::bit_cast<float>(0x3a83126fu)), 1.0);
        ASSERT_TRUE(lifted.has_value());
        EXPECT_EQ(plane.transform.position.z,
                  owner ? static_cast<float>(*lifted * 200.0 + static_cast<double>(position.z)) : position.z);
    }
}
