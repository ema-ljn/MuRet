#include "Scenes/GroundEffectFrame.h"

#include "Data/Terrain/TerrainHeights.h"
#include "Render/ModelBatch.h"
#include "Render/Models/Cloth.h"
#include "Render/Pass3D.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "World/GroundEffectDrawing.h"
#include "World/ModelEffects.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

// D-C4 y 06 § El dibujo del juego: el pase 0x52dc20 se repite entre frames originales sin
// adelantar el RNG, los cambios de los efectos ni el reloj que usó el último frame original.
namespace
{
using Client::Data::Terrain::TerrainHeights;
using Client::Render::Color;
using Client::Render::ModelBatch;
using Client::Render::ModelBlend;
using Client::Render::TextureSlot;
using Client::Render::Models::CrtRandom;
using Client::Scenes::GroundEffectFrame;
using Client::World::GroundEffectResources;
using Client::World::ModelEffect;

ModelEffect Effect(std::int16_t type)
{
    ModelEffect effect;
    effect.alive = true;
    effect.seen = true;
    effect.type = type;
    effect.transform.position = glm::vec3(1000.0f, 2000.0f, 30.0f);
    effect.transform.scale = 0.8f;
    effect.light = glm::vec3(0.25f, 0.5f, 0.75f);
    effect.life = 19;
    return effect;
}

void Begin(ModelBatch& batch)
{
    batch.Begin(Client::Render::Pass3D{});
    batch.SetState({.texture = TextureSlot{0x32}, .blend = ModelBlend::Opaque, .depthTest = false});
}

void ExpectSameBatch(const ModelBatch& actual, const ModelBatch& expected)
{
    ASSERT_EQ(actual.Vertices().size(), expected.Vertices().size());
    for (std::size_t index = 0; index < actual.Vertices().size(); ++index)
    {
        SCOPED_TRACE(index);
        EXPECT_EQ(actual.Vertices()[index].position, expected.Vertices()[index].position);
        EXPECT_EQ(actual.Vertices()[index].uv, expected.Vertices()[index].uv);
        EXPECT_EQ(actual.Vertices()[index].color, expected.Vertices()[index].color);
    }
    ASSERT_EQ(actual.Commands().size(), expected.Commands().size());
    for (std::size_t index = 0; index < actual.Commands().size(); ++index)
    {
        SCOPED_TRACE(index);
        EXPECT_EQ(actual.Commands()[index].state, expected.Commands()[index].state);
        EXPECT_EQ(actual.Commands()[index].firstVertex, expected.Commands()[index].firstVertex);
        EXPECT_EQ(actual.Commands()[index].vertexCount, expected.Commands()[index].vertexCount);
        EXPECT_EQ(actual.Commands()[index].operation, expected.Commands()[index].operation);
    }
    EXPECT_EQ(actual.State(), expected.State());
}

void ExpectSameEffect(const ModelEffect& actual, const ModelEffect& expected)
{
    EXPECT_EQ(actual.alive, expected.alive);
    EXPECT_EQ(actual.seen, expected.seen);
    EXPECT_EQ(actual.type, expected.type);
    EXPECT_EQ(actual.subtype, expected.subtype);
    EXPECT_EQ(actual.transform.position, expected.transform.position);
    EXPECT_EQ(actual.transform.angles, expected.transform.angles);
    EXPECT_EQ(actual.transform.scale, expected.transform.scale);
    EXPECT_EQ(actual.transform.lockedRootHeight, expected.transform.lockedRootHeight);
    EXPECT_EQ(actual.life, expected.life);
    EXPECT_EQ(actual.velocity, expected.velocity);
    EXPECT_EQ(actual.speed, expected.speed);
    EXPECT_EQ(actual.turnPending, expected.turnPending);
    EXPECT_EQ(actual.turnDirection, expected.turnDirection);
    EXPECT_EQ(actual.flagEc, expected.flagEc);
    EXPECT_EQ(actual.state, expected.state);
    EXPECT_EQ(actual.reference, expected.reference);
    EXPECT_EQ(actual.animation.action, expected.animation.action);
    EXPECT_EQ(actual.animation.frame, expected.animation.frame);
    EXPECT_EQ(actual.animation.previousAction, expected.animation.previousAction);
    EXPECT_EQ(actual.animation.previousKey, expected.animation.previousKey);
    EXPECT_EQ(actual.light, expected.light);
    EXPECT_EQ(actual.alpha, expected.alpha);
    EXPECT_EQ(actual.objectState, expected.objectState);
    EXPECT_EQ(actual.vertexLight, expected.vertexLight);
    EXPECT_EQ(actual.start, expected.start);
    EXPECT_EQ(actual.previousPosition, expected.previousPosition);
}
} // namespace

TEST(GroundEffectFrameTest, AnIntermediateDrawRequiresAnOriginalSnapshot)
{
    const auto heights = std::make_unique<TerrainHeights>();
    CrtRandom random;
    std::array effects{Effect(0x5de)};
    const ModelEffect before = effects[0];
    GroundEffectFrame frame;
    ModelBatch batch;
    Begin(batch);
    const auto state = batch.State();

    const auto drawn = frame.Draw(batch, effects, {.heights = *heights, .random = random}, false);

    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x52dc20"), std::string::npos);
    EXPECT_TRUE(batch.Vertices().empty());
    EXPECT_TRUE(batch.Commands().empty());
    EXPECT_EQ(batch.State(), state);
    EXPECT_EQ(random.State(), 1u);
    ExpectSameEffect(effects[0], before);
}

TEST(GroundEffectFrameTest, IntermediateDrawsRepeatTheOriginalRandomDecalWithoutTouchingCallerState)
{
    const auto heights = std::make_unique<TerrainHeights>();
    CrtRandom random;
    std::array effects{Effect(0x5de)};
    GroundEffectFrame frame;
    ModelBatch original;
    Begin(original);
    ASSERT_TRUE(frame.Draw(original, effects, {.heights = *heights, .random = random}, true).HasValue());
    // 0x52dc46 saca 41; el 0x5de saca 18467, y (18467 % 4 + 8) * 0,1f da 1,1f.
    EXPECT_EQ(random.State(), 3357800067u);
    ASSERT_EQ(original.Vertices().size(), 49u * 6u);
    EXPECT_EQ(original.Vertices()[0].position, glm::vec3(700.0f, 1700.0f, 5.0f));
    EXPECT_EQ(original.Vertices()[0].color, (Color{1.1f, 1.1f, 1.1f, 1.0f}));
    effects[0].alive = false;
    effects[0].seen = false;
    effects[0].transform.position = glm::vec3(4000.0f);
    effects[0].light = glm::vec3(0.125f);
    static_cast<void>(random.Next()); // otro pase del mismo frame ya avanzó el RNG compartido.
    const auto state = random.State();
    const ModelEffect before = effects[0];
    for (int repeat = 0; repeat < 3; ++repeat)
    {
        ModelBatch between;
        Begin(between);
        ASSERT_TRUE(frame.Draw(between, effects, {.heights = *heights, .random = random, .worldClock = 900.0f}, false)
                        .HasValue());
        ExpectSameBatch(between, original);
        EXPECT_EQ(random.State(), state);
        ExpectSameEffect(effects[0], before);
    }
}

TEST(GroundEffectFrameTest, ANewOriginalFrameUsesTheCurrentInputAndRandomState)
{
    const auto heights = std::make_unique<TerrainHeights>();
    CrtRandom random;
    std::array effects{Effect(0x5de)};
    GroundEffectFrame frame;
    ModelBatch batch;
    Begin(batch);
    ASSERT_TRUE(frame.Draw(batch, effects, {.heights = *heights, .random = random}, true).HasValue());
    effects[0].transform.position.x = 2000.0f;
    Begin(batch);
    ASSERT_TRUE(frame.Draw(batch, effects, {.heights = *heights, .random = random}, true).HasValue());
    EXPECT_EQ(random.State(), 3884216597u);
    ASSERT_EQ(batch.Vertices().size(), 49u * 6u);
    EXPECT_EQ(batch.Vertices()[0].position, glm::vec3(1700.0f, 1700.0f, 5.0f));
    EXPECT_EQ(batch.Vertices()[0].color, (Color{0.8f, 0.8f, 0.8f, 1.0f}));
    ModelBatch between;
    Begin(between);
    ASSERT_TRUE(frame.Draw(between, effects, {.heights = *heights, .random = random}, false).HasValue());
    ExpectSameBatch(between, batch);
    EXPECT_EQ(random.State(), 3884216597u);
}

TEST(GroundEffectFrameTest, ReplayingAGrowingDecalStartsFromItsInputBeforeTheDrawMutation)
{
    const auto heights = std::make_unique<TerrainHeights>();
    CrtRandom random;
    std::array effects{Effect(0x5d5)};
    effects[0].life = 7;
    GroundEffectFrame frame;
    ModelBatch original;
    Begin(original);
    ASSERT_TRUE(frame.Draw(original, effects, {.heights = *heights, .random = random}, true).HasValue());
    ASSERT_EQ(original.Vertices().size(), 25u * 6u);
    EXPECT_EQ(original.Vertices()[0].color, (Color{0.7f, 0.7f, 0.7f, 1.0f}));
    EXPECT_EQ(effects[0].transform.scale, 1.0f);
    const auto state = random.State();
    const ModelEffect before = effects[0];
    for (int repeat = 0; repeat < 3; ++repeat)
    {
        ModelBatch between;
        Begin(between);
        ASSERT_TRUE(frame.Draw(between, effects, {.heights = *heights, .random = random}, false).HasValue());
        ExpectSameBatch(between, original);
        EXPECT_EQ(random.State(), state);
        ExpectSameEffect(effects[0], before);
    }
    Begin(original);
    ASSERT_TRUE(frame.Draw(original, effects, {.heights = *heights, .random = random}, true).HasValue());
    EXPECT_EQ(effects[0].transform.scale, 1.2f);
    EXPECT_EQ(random.State(), 3357800067u);
}

TEST(GroundEffectFrameTest, IntermediateArrivalRingsKeepTheClockOfTheOriginalFrame)
{
    const auto heights = std::make_unique<TerrainHeights>();
    CrtRandom random;
    std::array effects{Effect(Client::World::ArrivalEffect)};
    GroundEffectFrame frame;
    ModelBatch original;
    Begin(original);
    ASSERT_TRUE(frame.Draw(original, effects, {.heights = *heights, .random = random}, true).HasValue());
    ASSERT_EQ(original.Vertices().size(), (2u * 12u * 6u) + (9u * 6u));
    const glm::vec3 first = original.Vertices()[0].position;
    EXPECT_EQ(first, glm::vec3(1000.0f, 2090.0f, 30.0f));
    const auto state = random.State();
    ModelBatch between;
    Begin(between);
    ASSERT_TRUE(
        frame.Draw(between, effects, {.heights = *heights, .random = random, .worldClock = 900.0f}, false).HasValue());
    ExpectSameBatch(between, original);
    EXPECT_EQ(random.State(), state);
    Begin(original);
    ASSERT_TRUE(
        frame.Draw(original, effects, {.heights = *heights, .random = random, .worldClock = 900.0f}, true).HasValue());
    ASSERT_EQ(original.Vertices().size(), (2u * 12u * 6u) + (9u * 6u));
    EXPECT_NE(original.Vertices()[0].position, first);
}

TEST(GroundEffectFrameTest, AFailedFrameReplaysItsPrefixAndErrorWithoutVisitingLaterEffects)
{
    const auto heights = std::make_unique<TerrainHeights>();
    CrtRandom random;
    std::array effects{Effect(0x5d5), Effect(0x61e), Effect(0x5d5)};
    effects[1].subtype = 7;
    GroundEffectFrame frame;
    ModelBatch original;
    Begin(original);
    const auto drawn = frame.Draw(original, effects, {.heights = *heights, .random = random}, true);
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x52ded0"), std::string::npos);
    ASSERT_EQ(original.Vertices().size(), 25u * 6u);
    EXPECT_EQ(effects[0].transform.scale, 1.0f);
    EXPECT_EQ(effects[2].transform.scale, 0.8f);
    EXPECT_EQ(random.State(), 3357800067u);
    effects[1].subtype = 0; // el replay debe seguir fallando en la entrada que guardó.
    const auto before = effects;
    ModelBatch between;
    Begin(between);
    const auto repeated = frame.Draw(between, effects, {.heights = *heights, .random = random}, false);
    ASSERT_FALSE(repeated.HasValue());
    EXPECT_EQ(repeated.Error(), drawn.Error());
    ExpectSameBatch(between, original);
    EXPECT_EQ(random.State(), 3357800067u);
    for (std::size_t index = 0; index < effects.size(); ++index)
    {
        ExpectSameEffect(effects[index], before[index]);
    }
}

TEST(GroundEffectFrameTest, AFailureAfterADrawMutationStillSnapshotsTheOriginalInput)
{
    const auto heights = std::make_unique<TerrainHeights>();
    CrtRandom random;
    std::array effects{Effect(0x5de), Effect(0x5d5), Effect(0x5d5)};
    effects[1].transform.position.x = 1.0e30f;
    GroundEffectFrame frame;
    ModelBatch original;
    Begin(original);
    const auto drawn = frame.Draw(original, effects, {.heights = *heights, .random = random}, true);
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x5fb378"), std::string::npos);
    ASSERT_EQ(original.Vertices().size(), 49u * 6u);
    EXPECT_EQ(effects[1].transform.scale, 1.0f);
    EXPECT_EQ(effects[2].transform.scale, 0.8f);
    const auto state = random.State();
    const auto before = effects;
    ModelBatch between;
    Begin(between);
    const auto repeated = frame.Draw(between, effects, {.heights = *heights, .random = random}, false);
    ASSERT_FALSE(repeated.HasValue());
    EXPECT_EQ(repeated.Error(), drawn.Error());
    ExpectSameBatch(between, original);
    EXPECT_EQ(random.State(), state);
    for (std::size_t index = 0; index < effects.size(); ++index)
    {
        ExpectSameEffect(effects[index], before[index]);
    }
}

TEST(GroundEffectFrameTest, AnIntermediateDrawRejectsAMapMismatchUntilTheNextOriginalFrame)
{
    const auto heights = std::make_unique<TerrainHeights>();
    CrtRandom random;
    std::array effects{Effect(0x5d5)};
    GroundEffectFrame frame;
    ModelBatch batch;
    Begin(batch);
    ASSERT_TRUE(frame.Draw(batch, effects, {.heights = *heights, .random = random, .map = 0}, true).HasValue());
    const auto state = random.State();
    const auto before = effects[0];
    Begin(batch);
    const auto batchState = batch.State();
    const auto drawn = frame.Draw(batch, effects, {.heights = *heights, .random = random, .map = 2}, false);
    ASSERT_FALSE(drawn.HasValue());
    EXPECT_NE(drawn.Error().find("0x52dc20"), std::string::npos);
    EXPECT_TRUE(batch.Vertices().empty());
    EXPECT_TRUE(batch.Commands().empty());
    EXPECT_EQ(batch.State(), batchState);
    EXPECT_EQ(random.State(), state);
    ExpectSameEffect(effects[0], before);
    ASSERT_TRUE(frame.Draw(batch, effects, {.heights = *heights, .random = random, .map = 2}, true).HasValue());
    EXPECT_EQ(effects[0].transform.scale, 1.2f);
}

TEST(GroundEffectFrameTest, AnEmptyOriginalSnapshotIsValidUntilReset)
{
    const auto heights = std::make_unique<TerrainHeights>();
    CrtRandom random;
    GroundEffectFrame frame;
    ModelBatch batch;
    Begin(batch);
    ASSERT_TRUE(frame.Draw(batch, {}, {.heights = *heights, .random = random}, true).HasValue());
    std::array effects{Effect(0x5de)};
    ASSERT_TRUE(frame.Draw(batch, effects, {.heights = *heights, .random = random}, false).HasValue());
    EXPECT_TRUE(batch.Vertices().empty());
    EXPECT_TRUE(batch.Commands().empty());
    EXPECT_EQ(random.State(), 1u);
    frame.Reset();
    EXPECT_FALSE(frame.Draw(batch, effects, {.heights = *heights, .random = random}, false).HasValue());
}
