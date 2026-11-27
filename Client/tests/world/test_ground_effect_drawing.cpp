#include "Terrain/GroundEffectDrawing.h"

#include "Crt/CrtRandom.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Render/ModelBatch.h"
#include "Render/Pass3D.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "World/ModelEffects.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
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
using Client::Crt::CrtRandom;
using Client::Data::Terrain::TerrainHeights;
using Client::Render::Color;
using Client::Render::ModelBatch;
using Client::Render::ModelBlend;
using Client::Render::ModelDrawState;
using Client::Render::ModelStateOperation;
using Client::Render::TextureSlot;
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

TEST(GroundEffectDrawingTest, Type61eStopsWithoutAnOwnerOrAColorAndHidesItsSpawners)
{
    struct Probe
    {
        std::int32_t subtype;
        std::int32_t life;
        const char* address;
    };
    const auto heights = std::make_unique<TerrainHeights>();
    // El /7 sin un personaje de dueño (0x52ded0) y un subtipo sin color (0x52dfdc, la luz local de antes).
    for (const Probe& probe : {Probe{7, 10, "0x52ded0"}, Probe{11, 10, "0x52dfdc"}})
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

// 0x52e010..0x52e0a2: el /6 cada 25 vidas suelta un 0x61e/7 (su rand() % 360) y el /8 en las vidas pares un /9 (sin
// rand()), en el primer lugar libre de la tabla del pase, con su posición, sus ángulos, su luz y su dueño. Llena, nada.
TEST(GroundEffectDrawingTest, Type61eSpawnersDropTheirChildIntoThePassTable)
{
    const auto heights = std::make_unique<TerrainHeights>();
    int owner = 0;
    struct Probe
    {
        std::int32_t subtype;
        std::int32_t life;
        std::optional<std::int32_t> child;
    };
    for (const Probe& probe : {Probe{6, 50, 7}, Probe{6, 49, std::nullopt}, Probe{8, 4, 9}, Probe{8, 3, std::nullopt}})
    {
        std::array<ModelEffect, 3> table{};
        table[0] = Effect(0x61e);
        table[0].subtype = probe.subtype;
        table[0].life = probe.life;
        table[0].ownerContext = &owner;
        table[1].alive = true;
        table[2].transform.angles.z = 99.0f;
        CrtRandom random;
        ModelBatch batch;
        batch.Begin(Client::Render::Pass3D{});
        const auto drawn = DrawGroundEffectCase(
            batch, table[0], GroundEffectResources{.heights = *heights, .random = random, .table = table});
        ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
        EXPECT_TRUE(batch.Vertices().empty());
        if (!probe.child.has_value())
        {
            EXPECT_FALSE(table[2].alive);
            EXPECT_EQ(random.State(), 1u);
            continue;
        }
        ASSERT_TRUE(table[2].alive) << probe.subtype;
        EXPECT_EQ(table[2].type, 0x61e);
        EXPECT_EQ(table[2].subtype, *probe.child);
        EXPECT_EQ(table[2].transform.position, table[0].transform.position);
        EXPECT_EQ(table[2].light, table[0].light);
        EXPECT_EQ(table[2].ownerContext, &owner);
        if (*probe.child == 7)
        {
            CrtRandom expected;
            const int turn = expected.Next() % 360;
            EXPECT_EQ(table[2].life, 40);
            EXPECT_EQ(table[2].transform.angles.z, static_cast<float>(turn));
            EXPECT_EQ(random.State(), expected.State());
        }
        else
        {
            EXPECT_EQ(table[2].life, 10);
            EXPECT_EQ(table[2].transform.scale, std::bit_cast<float>(0x3dcccccdu));
            EXPECT_EQ(table[2].transform.angles.z, table[0].transform.angles.z);
            EXPECT_EQ(random.State(), 1u);
        }
    }
}

// 0x52ddf0..0x52df00 y 0x52dfa0: el /7 se pone en la posición de su dueño y gira 10 más en cada dibujo; con más de 30
// de vida, la luz (40 − vida) · 0,1f; el lado vida · 0,07f y el color (0,9; 0,4; 0,2) por la luz.
TEST(GroundEffectDrawingTest, Type61eChildFollowsItsOwnerAndTurns)
{
    const auto heights = std::make_unique<TerrainHeights>();
    int owner = 0;
    const glm::vec3 ownerPosition(1200.0f, 2100.0f, 40.0f);
    ModelEffect effect = Effect(0x61e);
    effect.subtype = 7;
    effect.life = 35;
    effect.ownerContext = &owner;
    CrtRandom random;
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    batch.SetState(ModelDrawState{.texture = TextureSlot{0x32}, .blend = ModelBlend::Additive, .depthTest = false});
    const auto drawn =
        DrawGroundEffectCase(batch, effect,
                             GroundEffectResources{.heights = *heights,
                                                   .random = random,
                                                   .ownerPosition = [&](const void* asked) -> std::optional<glm::vec3>
                                                   {
                                                       if (asked == &owner)
                                                           return ownerPosition;
                                                       return std::nullopt;
                                                   }});
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(effect.transform.position, ownerPosition);
    EXPECT_EQ(effect.transform.angles.z, 25.0f);
    ASSERT_FALSE(batch.Vertices().empty());
    const double brightness = 5.0 * static_cast<double>(0.1f);
    const glm::vec3 color{static_cast<float>(brightness * static_cast<double>(0.9f)),
                          static_cast<float>(brightness * static_cast<double>(0.4f)),
                          static_cast<float>(brightness * static_cast<double>(0.2f))};
    EXPECT_EQ(batch.Vertices()[0].color.red, color.x);
    EXPECT_EQ(batch.Vertices()[0].color.green, color.y);
    EXPECT_EQ(batch.Vertices()[0].color.blue, color.z);
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

TEST(GroundEffectDrawingTest, Type6ebSkipsTheMissingSpecialOwnerAndKeepsTheSubtypeSixTerrainPath)
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
            ASSERT_FALSE(drawn.error.has_value());
            EXPECT_TRUE(drawn.batch.Vertices().empty());
            EXPECT_EQ(drawn.batch.State().depthWrite, true);
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
