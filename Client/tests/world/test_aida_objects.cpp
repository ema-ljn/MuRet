#include "Data/Terrain/TerrainLight.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/ObjectState.h"
#include "World/TerrainLighting.h"
#include "World/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// 0x4250f0, el paso de los objetos de los mapas 33 y 54 (0x605a97 después de la cadena de 0x605a62, con
// 0x425070): 06 § El mundo de la selección.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::World::ObjectStepGlobals;
using Client::World::StepWorldObjectChecked;
using Client::World::TerrainLighting;
using Client::World::WorldObject;

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
        globals.sound = [this](std::int32_t sound) { sounds.push_back(sound); };
    }
};

// 0x425137..0x42517c (0x1e) y 0x425186..0x4251c9 (0x47): un rand() % 5 por 0,01 que queda en la x87, y
// cada canal guardado de su suma: verde (b + 0,4; b + 0,6; b + 0,4), rojo (b + 0,9; b + 0,2; b + 0,2); con
// radio 2 en el buffer dinámico. Después, el 0x246.
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
            Client::World::AddTerrainLight(*expected, 1000.0f, 1500.0f,
                                           green ? Client::Data::Terrain::TerrainLightColor{first, second, first}
                                                 : Client::Data::Terrain::TerrainLightColor{first, second, second},
                                           2);
            EXPECT_EQ(step.lighting->cells, expected->cells);
            EXPECT_EQ(random.State(), draws.State());
            EXPECT_EQ(step.sounds, std::vector<std::int32_t>{0x246});
        }
    }
}

// 0x425126: la V del 0x19 y del 0x1c baja 0,015 (0x6b8a24) por paso; 0x4251d3: el 0x29 a medio alpha;
// 0x4251e8: el 0x40 a 0,05 keys por paso.
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

// 0x4251df: los que emiten en el dibujo (0x425260) quedan con +0x58 en −2; el 0x3c no, porque se oculta
// en su propio caso del dibujo.
TEST(AidaObjectsTest, TheEmittersHideTheirMeshButNotTheButterflyPost)
{
    Step step;
    for (const int type : {0x38, 0x39, 0x3a, 0x3b, 0x3e, 0x3f, 0x43, 0x46})
    {
        auto object = Object(static_cast<std::int16_t>(type));
        CrtRandom random(7);
        ASSERT_TRUE(StepWorldObjectChecked(object, SelectionMap, nullptr, random, step.globals).HasValue());
        EXPECT_EQ(object.state.hiddenMesh, Client::World::HiddenObject) << type;
    }
    auto post = Object(0x3c);
    CrtRandom random(7);
    ASSERT_TRUE(StepWorldObjectChecked(post, SelectionMap, nullptr, random, step.globals).HasValue());
    EXPECT_EQ(post.state.hiddenMesh, Client::Render::Models::NoHiddenMesh);
}

// 0x4251fb: el 0x246 sale con cada objeto, también de un tipo sin caso; sin emisor, corta después de los
// stores. Sin el buffer dinámico, el 0x1e corta en 0x5f9780 después de su rand().
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
    EXPECT_EQ(mist.state.hiddenMesh, Client::World::HiddenObject);

    ObjectStepGlobals dark;
    dark.sound = [](std::int32_t) {};
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
