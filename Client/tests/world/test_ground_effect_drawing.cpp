#include "World/GroundEffectDrawing.h"

#include "Data/Terrain/TerrainHeights.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Cloth.h"
#include "Render/Pass3D.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "World/ModelEffects.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// 06 § Después de los objetos, P-C55 (b): contratos estáticos de 0x52dc20 y 0x53d2c0,
// build c32bbd5f. Pruebas de la reimplementación, sin ejecutar el binario original.
namespace
{
using Client::Data::Terrain::TerrainHeights;
using Client::Render::Color;
using Client::Render::ModelBatch;
using Client::Render::ModelBlend;
using Client::Render::ModelDrawState;
using Client::Render::ModelStateOperation;
using Client::Render::TextureSlot;
using Client::Render::Models::CrtRandom;
using Client::World::DrawGroundEffectCase;
using Client::World::GroundEffectResources;
using Client::World::ModelEffect;
constexpr std::size_t RingVertices = 12u * 6u;
constexpr std::size_t BothRings = 2u * RingVertices;

// La geometría sigue en dos tramos; los perfiles intermedios se acreditan aparte como operaciones.
std::vector<Client::Render::ModelDrawCommand> GeometryCommands(const ModelBatch& batch)
{
    std::vector<Client::Render::ModelDrawCommand> commands;
    for (const auto& command : batch.Commands())
    {
        if (command.operation == ModelStateOperation::Draw)
            commands.push_back(command);
    }
    return commands;
}

ModelEffect Effect(std::int16_t type = Client::World::ArrivalEffect)
{
    ModelEffect effect;
    effect.type = type;
    effect.alive = true;
    effect.transform.position = glm::vec3(1000.0f, 2000.0f, 30.0f);
    effect.transform.angles.z = 15.0f;
    effect.transform.scale = 1.0f;
    effect.light = glm::vec3(0.25f, 0.5f, 0.75f);
    effect.life = 19;
    return effect;
}

struct Drawn
{
    ModelBatch batch;
    std::optional<std::string> error;
};

Drawn Draw(ModelEffect& effect, const TerrainHeights& heights, CrtRandom& random, float clock = 0.0f,
           std::uint8_t map = 0)
{
    Drawn drawn;
    drawn.batch.Begin(Client::Render::Pass3D{});
    drawn.batch.SetState(
        ModelDrawState{.texture = TextureSlot{0x32}, .blend = ModelBlend::Additive, .depthTest = false});
    if (auto result = DrawGroundEffectCase(drawn.batch, effect, GroundEffectResources{heights, random, clock, map});
        !result.HasValue())
    {
        drawn.error = result.Error();
    }
    return drawn;
}
} // namespace

TEST(GroundEffectDrawingTest, ArrivalDrawsTwoWhiteToBlackRingsBeforeTheTerrainDecal)
{
    const auto heights = std::make_unique<TerrainHeights>();
    ModelEffect effect = Effect();
    CrtRandom random;
    const Drawn drawn = Draw(effect, *heights, random);
    ASSERT_FALSE(drawn.error.has_value()) << *drawn.error;
    const auto commands = GeometryCommands(drawn.batch);
    ASSERT_EQ(commands.size(), 2u);
    EXPECT_EQ(commands[0].state.texture, TextureSlot{0x61f});
    EXPECT_EQ(commands[0].vertexCount, BothRings);
    EXPECT_EQ(commands[1].state.texture, TextureSlot{0x61e});
    ASSERT_EQ(drawn.batch.Vertices().size(), BothRings + 9u * 6u);
    const auto vertices = drawn.batch.Vertices();
    EXPECT_EQ(vertices[0].position, glm::vec3(1000.0f, 2090.0f, 30.0f));
    EXPECT_EQ(vertices[0].uv, glm::vec2(0.0f, 1.0f));
    EXPECT_EQ(vertices[0].color, (Color{1.0f, 1.0f, 1.0f, 1.0f}));
    EXPECT_EQ(vertices[2].position.x, 1065.0f);
    EXPECT_EQ(vertices[2].position.z, 230.0f);
    EXPECT_EQ(vertices[2].uv, glm::vec2(std::bit_cast<float>(0x3daaaaabu), 0.0f));
    EXPECT_EQ(vertices[2].color, (Color{0.0f, 0.0f, 0.0f, 1.0f}));
    EXPECT_EQ(vertices[5].position, glm::vec3(1000.0f, 2130.0f, 230.0f));
    EXPECT_EQ(vertices[BothRings].position, glm::vec3(900.0f, 1900.0f, 5.0f));
    EXPECT_EQ(vertices[BothRings].color, (Color{1.0f, 0.4f, 0.2f, 1.0f}));
    EXPECT_FALSE(drawn.batch.State().depthTest);
    EXPECT_EQ(random.State(), 1u);
    EXPECT_EQ(effect.life, 19);
}

// 0x5fc9c0 es la transpuesta de AngleMatrix: el primer giro es horario.
TEST(GroundEffectDrawingTest, ArrivalTruncatesTheClockAndTurnsItsRingsInOppositeDirections)
{
    const auto heights = std::make_unique<TerrainHeights>();
    for (const float clock : {36900.9f, -900.9f})
    {
        ModelEffect effect = Effect();
        CrtRandom random;
        const Drawn drawn = Draw(effect, *heights, random, clock);
        ASSERT_FALSE(drawn.error.has_value()) << *drawn.error;
        EXPECT_EQ(drawn.batch.Vertices()[0].position, glm::vec3(clock > 0.0f ? 1090.0f : 910.0f, 2000.0f, 30.0f));
        EXPECT_EQ(drawn.batch.Vertices()[RingVertices].position,
                  glm::vec3(clock > 0.0f ? 910.0f : 1090.0f, 2000.0f, 30.0f));
    }
}

TEST(GroundEffectDrawingTest, ArrivalKeepsUnroundedFadeProductsAndDoesNotFadeTheRings)
{
    const auto heights = std::make_unique<TerrainHeights>();
    ModelEffect effect = Effect();
    effect.life = 0;
    CrtRandom random;
    const Drawn drawn = Draw(effect, *heights, random);
    ASSERT_FALSE(drawn.error.has_value()) << *drawn.error;
    ASSERT_EQ(drawn.batch.Vertices().size(), BothRings + 81u * 6u);
    EXPECT_EQ(drawn.batch.Vertices()[0].color.red, 1.0f);
    const Color& color = drawn.batch.Vertices()[BothRings].color;
    EXPECT_EQ(std::bit_cast<std::uint32_t>(color.red), 0xb2800000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(color.green), 0xb1cccccdu);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(color.blue), 0xb14ccccdu);
    effect.life = 20;
    const Drawn full = Draw(effect, *heights, random);
    ASSERT_FALSE(full.error.has_value()) << *full.error;
    EXPECT_EQ(full.batch.Vertices().size(), BothRings + 9u * 6u);
}

TEST(GroundEffectDrawingTest, ArrivalUsesTheLowSignedClockDwordAndAcceptsTheObservedNanFtol)
{
    const auto heights = std::make_unique<TerrainHeights>();
    ModelEffect effect = Effect();
    CrtRandom random;
    const Drawn low = Draw(effect, *heights, random, 2048.0f);
    const Drawn wrapped = Draw(effect, *heights, random, 4294969344.0f);
    const Drawn zero = Draw(effect, *heights, random);
    const Drawn nan = Draw(effect, *heights, random, std::numeric_limits<float>::quiet_NaN());
    ASSERT_FALSE(low.error.has_value());
    ASSERT_FALSE(wrapped.error.has_value());
    ASSERT_FALSE(nan.error.has_value());
    ASSERT_FALSE(zero.error.has_value());
    EXPECT_EQ(low.batch.Vertices()[0].position, wrapped.batch.Vertices()[0].position);
    EXPECT_EQ(zero.batch.Vertices()[0].position, nan.batch.Vertices()[0].position);
}

TEST(GroundEffectDrawingTest, ArrivalSubtypeTwoDrawsOnlyItsDecalAndStopsAtTheUnwrittenSide)
{
    const auto heights = std::make_unique<TerrainHeights>();
    ModelEffect effect = Effect();
    effect.subtype = 2;
    effect.life = 11;
    CrtRandom random;
    const Drawn drawn = Draw(effect, *heights, random);
    ASSERT_FALSE(drawn.error.has_value()) << *drawn.error;
    const auto commands = GeometryCommands(drawn.batch);
    ASSERT_EQ(commands.size(), 1u);
    EXPECT_EQ(commands[0].state.texture, TextureSlot{0x61e});
    EXPECT_EQ(drawn.batch.Vertices().size(), 121u * 6u);
    effect.life = 10;
    const Drawn unwritten = Draw(effect, *heights, random);
    ASSERT_TRUE(unwritten.error.has_value());
    EXPECT_NE(unwritten.error->find("0x52e312"), std::string::npos);
    EXPECT_TRUE(unwritten.batch.Vertices().empty());
}

TEST(GroundEffectDrawingTest, AnArrivalTerrainErrorKeepsTheRingsButAnInvalidClockDrawsNothing)
{
    const auto heights = std::make_unique<TerrainHeights>();
    ModelEffect effect = Effect();
    effect.transform.position.x = 1e30f;
    CrtRandom random;
    const Drawn terrainError = Draw(effect, *heights, random);
    ASSERT_TRUE(terrainError.error.has_value());
    EXPECT_NE(terrainError.error->find("0x5fb378"), std::string::npos);
    EXPECT_EQ(terrainError.batch.Vertices().size(), BothRings);
    EXPECT_EQ(terrainError.batch.State().texture, TextureSlot{0x61e});
    const Drawn clockError = Draw(effect, *heights, random, std::numeric_limits<float>::infinity());
    ASSERT_TRUE(clockError.error.has_value());
    EXPECT_NE(clockError.error->find("0x52e1cd"), std::string::npos);
    EXPECT_TRUE(clockError.batch.Vertices().empty());
    EXPECT_EQ(clockError.batch.State().texture, TextureSlot{0x32});
}

TEST(GroundEffectDrawingTest, Type61dDrawsBeforeFadingItsLightAndGrowing)
{
    const auto heights = std::make_unique<TerrainHeights>();
    ModelEffect effect = Effect(0x61d);
    effect.subtype = 1;
    CrtRandom random;
    const Drawn drawn = Draw(effect, *heights, random);
    ASSERT_FALSE(drawn.error.has_value()) << *drawn.error;
    ASSERT_FALSE(drawn.batch.Vertices().empty());
    EXPECT_EQ(drawn.batch.Vertices()[0].color, (Color{0.25f, 0.5f, 0.75f, 1.0f}));
    EXPECT_EQ(effect.transform.scale, 1.05f);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(effect.light.x), 0x3e68ba2eu);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(effect.light.y), 0x3ee8ba2eu);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(effect.light.z), 0x3f2e8ba2u);
}

TEST(GroundEffectDrawingTest, Type5deUsesItsExtraRandomExceptWithSubtypeThree)
{
    const auto heights = std::make_unique<TerrainHeights>();
    ModelEffect effect = Effect(0x5de);
    CrtRandom random;
    CrtRandom expected;
    static_cast<void>(expected.Next()); // 41 % 4 + 8 = 9.
    const Drawn drawn = Draw(effect, *heights, random);
    ASSERT_FALSE(drawn.error.has_value()) << *drawn.error;
    ASSERT_EQ(drawn.batch.Vertices().size(), 49u * 6u);
    // 9 por el float de 0x6b87b8, un ULP por arriba del literal 0,9f.
    constexpr float Light = std::bit_cast<float>(0x3f666667u);
    EXPECT_EQ(drawn.batch.Vertices()[0].color, (Color{Light, Light, Light, 1.0f}));
    EXPECT_EQ(random.State(), expected.State());
    effect.subtype = 3;
    const Drawn skipped = Draw(effect, *heights, random);
    EXPECT_FALSE(skipped.error.has_value());
    EXPECT_TRUE(skipped.batch.Vertices().empty());
    EXPECT_EQ(random.State(), expected.State());
}

TEST(GroundEffectDrawingTest, Type5d5GrowsBeforeDrawingAndBeforeATerrainError)
{
    const auto heights = std::make_unique<TerrainHeights>();
    ModelEffect effect = Effect(0x5d5);
    effect.transform.scale = 0.8f;
    effect.life = 7;
    CrtRandom random;
    const Drawn drawn = Draw(effect, *heights, random);
    ASSERT_FALSE(drawn.error.has_value()) << *drawn.error;
    EXPECT_EQ(effect.transform.scale, 1.0f);
    ASSERT_EQ(drawn.batch.Vertices().size(), 25u * 6u);
    EXPECT_EQ(drawn.batch.Vertices()[0].color, (Color{0.7f, 0.7f, 0.7f, 1.0f}));
    effect.transform.position.x = 1e30f;
    ASSERT_TRUE(Draw(effect, *heights, random).error.has_value());
    EXPECT_EQ(effect.transform.scale, 1.2f);
}

TEST(GroundEffectDrawingTest, Type61eChoosesItsSubtypeColor)
{
    struct Probe
    {
        std::int32_t subtype;
        Color color;
    };
    const auto heights = std::make_unique<TerrainHeights>();
    for (const Probe& probe :
         {Probe{0, {0.4f, 0.6f, 1.0f, 1.0f}}, Probe{1, {0.4f, 0.6f, 1.0f, 1.0f}}, Probe{2, {0.4f, 1.0f, 0.6f, 1.0f}},
          Probe{3, {1.0f, 0.6f, 0.4f, 1.0f}}, Probe{9, {0.9f, 0.4f, 0.2f, 1.0f}}})
    {
        ModelEffect effect = Effect(0x61e);
        effect.subtype = probe.subtype;
        effect.life = 10;
        CrtRandom random;
        const Drawn drawn = Draw(effect, *heights, random);
        ASSERT_FALSE(drawn.error.has_value()) << *drawn.error;
        ASSERT_FALSE(drawn.batch.Vertices().empty());
        EXPECT_EQ(drawn.batch.Vertices()[0].color, probe.color);
    }
}

TEST(GroundEffectDrawingTest, Type61eStopsAtUnportedOwnerSpawnAndUnknownColorBranches)
{
    struct Probe
    {
        std::int32_t subtype;
        std::int32_t life;
        const char* address;
    };
    const auto heights = std::make_unique<TerrainHeights>();
    for (const Probe& probe :
         {Probe{7, 10, "0x52ded0"}, Probe{6, 50, "0x52e04c"}, Probe{8, 4, "0x52e0a2"}, Probe{11, 10, "0x52dfdc"}})
    {
        ModelEffect effect = Effect(0x61e);
        effect.subtype = probe.subtype;
        effect.life = probe.life;
        CrtRandom random;
        const Drawn drawn = Draw(effect, *heights, random);
        ASSERT_TRUE(drawn.error.has_value()) << probe.subtype;
        EXPECT_NE(drawn.error->find(probe.address), std::string::npos) << *drawn.error;
        EXPECT_TRUE(drawn.batch.Vertices().empty());
        EXPECT_EQ(random.State(), 1u);
    }
    for (const std::int32_t subtype : {5, 6, 8})
    {
        ModelEffect effect = Effect(0x61e);
        effect.subtype = subtype;
        effect.life = 3;
        CrtRandom random;
        const Drawn drawn = Draw(effect, *heights, random);
        EXPECT_FALSE(drawn.error.has_value());
        EXPECT_TRUE(drawn.batch.Vertices().empty());
    }
}

TEST(GroundEffectDrawingTest, Type61ePulsesSubtypeFourAndTenWithTheStoredScale)
{
    const auto heights = std::make_unique<TerrainHeights>();
    for (const std::int32_t subtype : {4, 10})
    {
        ModelEffect effect = Effect(0x61e);
        effect.subtype = subtype;
        effect.life = 10;
        effect.transform.scale = 2.0f;
        CrtRandom random;
        const Drawn drawn = Draw(effect, *heights, random);
        ASSERT_FALSE(drawn.error.has_value()) << *drawn.error;
        ASSERT_EQ(drawn.batch.Vertices().size(), 49u * 6u);
        const Color& color = drawn.batch.Vertices()[0].color;
        // sin(50 * f32(0,05)) + 0,5; productos antes de guardar la luz en float.
        EXPECT_EQ(std::bit_cast<std::uint32_t>(color.red), subtype == 4 ? 0x3f8c9abcu : 0x3de0f793u);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(color.green), 0x3f0c9abcu);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(color.blue), subtype == 4 ? 0x3de0f793u : 0x3f8c9abcu);
    }
}

TEST(GroundEffectDrawingTest, Type636UsesTheStoredSidesAndNormalAlphaBlending)
{
    const auto heights = std::make_unique<TerrainHeights>();
    ModelEffect effect = Effect(0x636);
    effect.start = glm::vec3(2.0f, 1.0f, 0.0f);
    CrtRandom random;
    const Drawn drawn = Draw(effect, *heights, random);
    ASSERT_FALSE(drawn.error.has_value()) << *drawn.error;
    ASSERT_EQ(drawn.batch.Vertices().size(), 49u * 6u);
    EXPECT_EQ(drawn.batch.State().texture, TextureSlot{0x636});
    EXPECT_EQ(drawn.batch.State().blend, ModelBlend::Alpha);
    EXPECT_EQ(drawn.batch.State().cullEnabled, false);
    EXPECT_EQ(drawn.batch.State().depthWrite, true);
    EXPECT_EQ(drawn.batch.State().alphaTest, true);
    EXPECT_EQ(drawn.batch.State().texture2d, true);
    EXPECT_FALSE(drawn.batch.State().depthTest);
    EXPECT_EQ(drawn.batch.Vertices()[0].uv, glm::vec2(-2.5f, -1.0f));
}

TEST(GroundEffectDrawingTest, Type6ebStopsOnSpecialMapsExceptWithSubtypeSix)
{
    const auto heights = std::make_unique<TerrainHeights>();
    for (const std::uint8_t map :
         {std::uint8_t{0}, std::uint8_t{2}, std::uint8_t{24}, std::uint8_t{29}, std::uint8_t{36}})
    {
        ModelEffect effect = Effect(0x6eb);
        CrtRandom random;
        const Drawn drawn = Draw(effect, *heights, random, 0.0f, map);
        if (map == 0 || map == 2)
        {
            ASSERT_FALSE(drawn.error.has_value()) << *drawn.error;
            EXPECT_EQ(drawn.batch.Vertices().size(), 25u * 6u);
            EXPECT_EQ(drawn.batch.State().texture, TextureSlot{0x6eb});
        }
        else
        {
            ASSERT_TRUE(drawn.error.has_value());
            EXPECT_NE(drawn.error->find("0x52e12f"), std::string::npos);
            EXPECT_TRUE(drawn.batch.Vertices().empty());
            EXPECT_EQ(drawn.batch.State().depthWrite, false);
            ASSERT_FALSE(drawn.batch.Commands().empty());
            EXPECT_EQ(drawn.batch.Commands()[0].operation, ModelStateOperation::DepthWrite);
        }
        effect.subtype = 6;
        const Drawn subtypeSix = Draw(effect, *heights, random, 0.0f, map);
        EXPECT_FALSE(subtypeSix.error.has_value());
        EXPECT_EQ(subtypeSix.batch.Vertices().size(), 25u * 6u);
    }
}

TEST(GroundEffectDrawingTest, AnUnlistedModelHasNoGroundCase)
{
    const auto heights = std::make_unique<TerrainHeights>();
    ModelEffect effect = Effect(Client::World::DragonEffect);
    CrtRandom random;
    const Drawn drawn = Draw(effect, *heights, random);
    EXPECT_FALSE(drawn.error.has_value());
    EXPECT_TRUE(drawn.batch.Vertices().empty());
    EXPECT_EQ(random.State(), 1u);
}
