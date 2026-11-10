#include "Scenes/GameTailPasses.h"

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Data/Models/Model.h"
#include "Render/ModelBatch.h"
#include "Render/Pass3D.h"
#include "Render/Models/Skeleton.h"
#include "Render/Billboard.h"
#include "Render/TextureSlot.h"
#include "Scenes/CharacterDrawing.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/SceneCanvas.h"
#include "Scenes/WorldObjectsDrawing.h"
#include "World/Sprites.h"
#include "support/RecordingFrameTarget.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>

namespace
{
using Client::Scenes::GameTailStage;
struct Call
{
    std::optional<GameTailStage> stage;
    std::uint8_t mode = 0;
    friend bool operator==(const Call&, const Call&) = default;
};
class TailRecorder
{
public:
    Client::Scenes::GameTailPassCallbacks Callbacks()
    {
        return {[&](GameTailStage stage, std::uint8_t mode) -> Client::Core::Expected<void, std::string>
                {
                    calls.push_back({stage, mode});
                    if (stage == failingStage && mode == failingMode)
                        return Client::Core::Unexpected(std::string("surface missing"));
                    return {};
                },
                [&]() -> Client::Core::Expected<bool, std::string>
                {
                    calls.push_back({std::nullopt, 0});
                    const auto read = reads++;
                    if (read == failingRead || read >= states.size())
                        return Client::Core::Unexpected(std::string("0x437850: missing Kalima state"));
                    return states[read];
                }};
    }
    std::array<bool, 2> states{};
    std::vector<Call> calls;
    std::size_t reads = 0;
    std::size_t failingRead = 3;
    GameTailStage failingStage = GameTailStage::KalimaWater;
    std::uint8_t failingMode = 255;
};
class SpriteTextures final : public Client::Scenes::IModelLibrary
{
public:
    const Client::Data::Models::Model* Find(std::uint16_t) const override
    {
        return nullptr;
    }
    const Client::Render::Models::Skeleton* SkeletonOf(std::uint16_t) const override
    {
        return nullptr;
    }
    std::span<const Client::Render::TextureSlot> MeshTextures(std::uint16_t) const override
    {
        return {};
    }
    std::optional<glm::vec2> TextureSize(Client::Render::TextureSlot) const override
    {
        return glm::vec2(2);
    }
};
constexpr Call Query{std::nullopt, 0};
} // namespace

TEST(GameSceneKalimaTailTest, OrdinaryWorldRunsDamageBetweenTheTwoNativeWorldQueries)
{
    TailRecorder recorder;
    ASSERT_TRUE(Client::Scenes::RunGameTailPasses(0, 0, recorder.Callbacks()).HasValue());
    const std::vector<Call> expected{{GameTailStage::WorldLeaves, 0},
                                     {GameTailStage::Joints, 0},
                                     {GameTailStage::ModelEffects, 0},
                                     {GameTailStage::Ribbons, 0},
                                     {GameTailStage::MarkSpritesSeen, 0},
                                     {GameTailStage::BeginEye, 0},
                                     {GameTailStage::Sprites, 0},
                                     {GameTailStage::Particles, 0},
                                     Query,
                                     {GameTailStage::Damage, 0},
                                     Query};
    EXPECT_EQ(recorder.calls, expected);
    EXPECT_EQ(recorder.reads, 2u);
}

TEST(GameSceneKalimaTailTest, MapFlagControlsTheFirstModesIndependentlyOfTheWorldPointer)
{
    struct Case
    {
        std::int32_t map;
        std::uint8_t mode;
    };
    constexpr std::array cases{Case{0, 0},  Case{23, 0}, Case{24, 1}, Case{29, 1},
                               Case{30, 0}, Case{35, 0}, Case{36, 1}, Case{37, 0}};
    for (const auto& sample : cases)
    {
        TailRecorder recorder;
        ASSERT_TRUE(Client::Scenes::RunGameTailPasses(sample.map, 0, recorder.Callbacks()).HasValue());
        EXPECT_EQ(recorder.calls[1], (Call{GameTailStage::Joints, sample.mode}));
        EXPECT_EQ(recorder.calls[2], (Call{GameTailStage::ModelEffects, 0}));
        EXPECT_EQ(recorder.calls[4], (Call{GameTailStage::MarkSpritesSeen, sample.mode}));
        EXPECT_EQ(recorder.calls[6], (Call{GameTailStage::Sprites, sample.mode}));
        EXPECT_EQ(recorder.calls[7], (Call{GameTailStage::Particles, sample.mode}));
        EXPECT_EQ(recorder.calls[9], (Call{GameTailStage::Damage, sample.mode}));
    }
}

TEST(GameSceneKalimaTailTest, LoadedWorldRunsTheWaterAndBothTailLayersInNativeOrder)
{
    TailRecorder recorder;
    recorder.states = {true, true};
    ASSERT_TRUE(Client::Scenes::RunGameTailPasses(24, 0, recorder.Callbacks()).HasValue());
    const std::vector<Call> expected{{GameTailStage::WorldLeaves, 0},
                                     {GameTailStage::Joints, 1},
                                     {GameTailStage::ModelEffects, 0},
                                     {GameTailStage::Ribbons, 0},
                                     {GameTailStage::MarkSpritesSeen, 1},
                                     {GameTailStage::BeginEye, 1},
                                     {GameTailStage::Sprites, 1},
                                     {GameTailStage::Particles, 1},
                                     Query,
                                     Query,
                                     {GameTailStage::BeginWorld, 2},
                                     {GameTailStage::KalimaWater, 0},
                                     {GameTailStage::Joints, 2},
                                     {GameTailStage::ModelEffects, 1},
                                     {GameTailStage::Ribbons, 0},
                                     {GameTailStage::MarkSpritesSeen, 2},
                                     {GameTailStage::BeginEye, 2},
                                     {GameTailStage::Sprites, 2},
                                     {GameTailStage::Particles, 2},
                                     {GameTailStage::Damage, 2}};
    EXPECT_EQ(recorder.calls, expected);
}

TEST(GameSceneKalimaTailTest, DeviasRepeatsItsEyeLeavesInBothLayersOnlyUnderTheSignedLayerGuard)
{
    for (const std::int32_t layer : {-1, 0, 2, 3, 9, 10})
    {
        TailRecorder recorder;
        recorder.states = {true, true};
        ASSERT_TRUE(Client::Scenes::RunGameTailPasses(2, layer, recorder.Callbacks()).HasValue());
        std::size_t eyeLeaves = 0;
        std::size_t worldLeaves = 0;
        for (const auto& call : recorder.calls)
        {
            eyeLeaves += call.stage == GameTailStage::EyeLeaves ? 1u : 0u;
            worldLeaves += call.stage == GameTailStage::WorldLeaves ? 1u : 0u;
        }
        EXPECT_EQ(worldLeaves, 1u);
        EXPECT_EQ(eyeLeaves, layer != 3 && layer < 10 ? 2u : 0u);
    }
}

TEST(GameSceneKalimaTailTest, MissingWaterBackendStopsAfterResettingTheWorldPassBeforeModeTwo)
{
    TailRecorder recorder;
    recorder.states = {true, true};
    recorder.failingMode = 0;
    const auto result = Client::Scenes::RunGameTailPasses(24, 0, recorder.Callbacks());
    ASSERT_FALSE(result.HasValue());
    EXPECT_EQ(result.Error(), "surface missing");
    ASSERT_EQ(recorder.calls.size(), 12u);
    EXPECT_EQ(recorder.calls[10], (Call{GameTailStage::BeginWorld, 2}));
    EXPECT_EQ(recorder.calls.back(), (Call{GameTailStage::KalimaWater, 0}));
}

TEST(GameSceneKalimaTailTest, MissingNativeWorldStateFailsAfterTheFirstParticlePass)
{
    TailRecorder recorder;
    recorder.failingRead = 0;
    const auto result = Client::Scenes::RunGameTailPasses(24, 0, recorder.Callbacks());
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x437850"), std::string::npos);
    ASSERT_EQ(recorder.calls.size(), 9u);
    EXPECT_EQ(recorder.calls[7], (Call{GameTailStage::Particles, 1}));
    EXPECT_EQ(recorder.calls.back(), Query);
}

TEST(GameSceneKalimaTailTest, TheSecondNativeWorldQueryIsReadAgainAfterDamage)
{
    TailRecorder recorder;
    recorder.states = {false, true};
    ASSERT_TRUE(Client::Scenes::RunGameTailPasses(0, 0, recorder.Callbacks()).HasValue());
    EXPECT_EQ(recorder.calls[9], (Call{GameTailStage::Damage, 0}));
    EXPECT_EQ(recorder.calls[10], Query);
    EXPECT_EQ(recorder.calls[11], (Call{GameTailStage::BeginWorld, 2}));
    EXPECT_EQ(recorder.calls.back(), (Call{GameTailStage::Damage, 2}));
}

TEST(GameSceneKalimaTailTest, EyeKeepsMaterialAndWorldRestoresItsNativeAxesWithTheSameCamera)
{
    Client::Render::Pass3D world;
    world.view[3][0] = 20;
    world.view[2][2] = 2;
    Client::Tests::RecordingFrameTarget target;
    auto& canvas = target.BeginFrame(Client::Core::Duration{});
    auto& batch = canvas.models;
    batch.Begin(world);
    auto state = batch.State();
    state.blend = Client::Render::ModelBlend::Subtractive;
    state.depthTest = false;
    state.depthWrite = false;
    state.cullEnabled = false;
    state.alphaTest = true;
    state.texture2d = false;
    state.texture = Client::Render::TextureSlot{0x610};
    state.space = Client::Render::ModelCoordinateSpace::Eye;
    batch.SetState(state);
    Client::Scenes::BeginGameTailPass(batch, world, true, canvas.flushModels);
    EXPECT_EQ(batch.Pass().view, glm::mat4(1));
    EXPECT_EQ(batch.State().blend, Client::Render::ModelBlend::Subtractive);
    EXPECT_FALSE(batch.State().depthTest);
    EXPECT_EQ(batch.State().depthWrite, false);
    EXPECT_EQ(batch.State().cullEnabled, false);
    EXPECT_EQ(batch.State().space, Client::Render::ModelCoordinateSpace::World);
    state.space = Client::Render::ModelCoordinateSpace::World;
    EXPECT_EQ(batch.State(), state);
    Client::Scenes::BeginGameTailPass(batch, world, false, canvas.flushModels);
    EXPECT_EQ(batch.Pass().view, world.view);
    EXPECT_TRUE(batch.State().depthTest);
    EXPECT_EQ(batch.State().blend, Client::Render::ModelBlend::Subtractive);
    state.depthTest = true;
    state.depthWrite = true;
    state.cullEnabled = true;
    state.alphaTest = false;
    state.texture2d = true;
    EXPECT_EQ(batch.State(), state);
    target.Present();
}

TEST(GameSceneKalimaTailTest, RealSpritesOverlapBothBandsWhileDamageOnlyUsesTheSecondBand)
{
    Client::World::FrameSprites sprites;
    std::array<Client::Scenes::DamageNumberState, 3> damage{};
    constexpr std::array heights{300.0f, 325.0f, 351.0f};
    for (std::size_t i = 0; i < heights.size(); ++i)
    {
        ASSERT_TRUE(sprites.Create(0x5ac, {0, 0, heights[i]}, 1, {1, 1, 1}, 0, 0));
        damage[i].alive = true;
        damage[i].number = 1;
        damage[i].scale = 10;
        damage[i].speed = 1;
        damage[i].position.z = heights[i];
    }
    SpriteTextures textures;
    Client::Render::ModelBatch batch;
    const Client::Render::Pass3D pass;
    const auto view = Client::Render::ViewRows(pass.view);
    std::array<std::size_t, 2> spriteVertices{};
    std::array<std::uint8_t, 2> spriteModes{};
    std::size_t spritePass = 0;
    std::size_t damagePasses = 0;
    std::size_t damageVertices = 0;
    const Client::Scenes::GameTailPassCallbacks callbacks{
        [&](GameTailStage stage, std::uint8_t mode) -> Client::Core::Expected<void, std::string>
        {
            if (stage == GameTailStage::MarkSpritesSeen)
                sprites.MarkSeen();
            if (stage == GameTailStage::Sprites)
            {
                batch.Begin(pass);
                Client::Scenes::DrawFrameSprites(batch, sprites, textures, view, mode);
                spriteModes.at(spritePass) = mode;
                spriteVertices.at(spritePass++) = batch.Vertices().size();
            }
            if (stage == GameTailStage::Damage)
            {
                batch.Begin(pass);
                ++damagePasses;
                EXPECT_EQ(mode, 2);
                auto drawn = Client::Scenes::DrawDamage(batch, damage, mode, view);
                damageVertices = batch.Vertices().size();
                return drawn;
            }
            return {};
        },
        []() -> Client::Core::Expected<bool, std::string> { return true; }};
    ASSERT_TRUE(Client::Scenes::RunGameTailPasses(24, 0, callbacks).HasValue());
    EXPECT_EQ(spriteModes, (std::array<std::uint8_t, 2>{1, 2}));
    EXPECT_EQ(spriteVertices, (std::array<std::size_t, 2>{12, 12}));
    EXPECT_FALSE(sprites.Any());
    EXPECT_EQ(damagePasses, 1u);
    EXPECT_EQ(damageVertices, 12u);
    EXPECT_TRUE(damage[0].alive);
}
