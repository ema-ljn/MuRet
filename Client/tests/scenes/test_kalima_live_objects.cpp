#include "test_game_t16_support.h"

#include "Scenes/GameScene.h"
#include "Scenes/GameData.h"
#include "Scenes/GameItemRenderState.h"
#include "Scenes/LoadingScene.h"
#include "Scenes/SceneContext.h"
#include "Scenes/SessionEventApplier.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldState.h"
#include "Render/ModelBatch.h"
#include "support/GoldenDir.h"
#include "support/RecordingFrameTarget.h"
#include "support/SessionHarness.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include "World/Particles.h"
#include "World/ParticleOwner.h"
#include "Render/Models/Skinning.h"
#include <glm/ext/vector_float2.hpp>
#include "World/KalimaWorld.h"
#include "Scenes/FaunaState.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldModels.h"
#include "Scenes/TerrainTiles.h"
#include "Scenes/ModelTextureTable.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/WorldObjectsDrawing.h"
#include "Render/TextureSlot.h"
#include "support/FakeTextureStore.h"
#include "World/SphereFrustum.h"
#include "World/ModelEffects.h"
#include "Scenes/GameParticles.h"
#include "Scenes/TextureMetadata.h"
#include "Scenes/CharacterDrawing.h"
#include "Render/Pass3D.h"
#include "Render/Models/OriginalRotation.h"
#include <array>
#include <span>
#include <optional>
#include <vector>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>

namespace
{
// La semilla que deja el rand() de la CRT en `state` después de `draws` llamadas: el LCG (· 214013 + 2531011) se
// invierte módulo 2^32 con el inverso de 214013.
constexpr std::uint32_t SeedBefore(std::uint32_t state, int draws)
{
    constexpr std::uint32_t InverseMultiplier = 0xb9b33155u;
    for (int i = 0; i < draws; ++i)
        state = (state - 2531011u) * InverseMultiplier;
    return state;
}

// 0x636ded..0x63705b con el registro propio abierto: la barra de skills no saca, y quedan las consultas del
// comercio y de la tienda de NPC de Q, W y E (0x5a343e) y las de IsOpen(0) (0x5a5511).
constexpr int NestedGameUiStepDraws = 2 + 2;

class KalimaLiveObjectsTest : public Client::Tests::T16::SceneTest, public testing::WithParamInterface<int>
{
protected:
    void SetUp() override
    {
        Client::Tests::GoldenDataTest::SetUp();
        if (HasFatalFailure())
            return;
        Client::Scenes::InitializeGameItemRenderState(m_world->Runtime().itemRender, m_cloth->random);
        Client::Scenes::LoadingScene loading({.dataRoot = m_dataRoot,
                                              .textures = m_textures,
                                              .log = m_log,
                                              .cloth = m_cloth.get(),
                                              .world = m_world.get()},
                                             m_models.get());
        ASSERT_TRUE(loading.Enter().HasValue());
        loading.Step(m_input);
        ASSERT_FALSE(loading.WantsToQuit());
        loading.Leave();
        auto data = Client::Scenes::LoadGameData(m_dataRoot);
        ASSERT_TRUE(data.HasValue()) << data.Error();
        m_data = std::move(data).Value();
        m_world->OwnCharacter().ClearEquipmentForEntry();
        m_applier = std::make_unique<Client::Scenes::SessionEventApplier>(Resources());
        m_online = std::make_unique<Client::Tests::LoggedInSession>(m_applier.get());
        ASSERT_NO_FATAL_FAILURE(m_online->LogIn());
        ReceiveEntry();
        m_online->socket.sent.clear();
    }
};

// Data real conservada: los siete aliases deben completar el cuadro poblado.
TEST_P(KalimaLiveObjectsTest, HotEntryDrawFinishesMovementBodiesAndWaterOverlays)
{
    OpenScene();
    Step();
    ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();
    Teleport(static_cast<std::uint8_t>(GetParam()), 64, 64);
    ASSERT_FALSE(m_pollFailure.has_value()) << *m_pollFailure;
    ASSERT_EQ(m_world->Map().objects.size(), 2266u);
    // Primer dibujo después del1C, antes del pasoKalima: no nace fauna y quedan2266 objetos.
    Draw();
    ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();
    EXPECT_EQ(m_world->Map().objects.size(), 2266u);
    ASSERT_FALSE(m_target.frames.empty());
    bool causticPrefix = false;
    for (const Client::Tests::FrameEvent& event : m_target.frames.back().events)
        for (const Client::Render::ModelDrawCommand& command : event.modelCommands)
            if (command.vertexCount > 0 && command.state.texture && command.state.texture->value >= 65 &&
                command.state.texture->value < 97)
                causticPrefix = true;
    EXPECT_TRUE(causticPrefix); // Geometría real de4395ba, no sólo un mensaje de frontera.
}

// El objeto 40 suelta la 0x6f1 con rand() % 4 en 0 (0x438c10). Con los 3.848 rand() de 0x4eb620 al empezar cada frame
// (la espera de las skills, CL9 hito 4), el primero que la saca con la semilla de la fixture es el tercero.
TEST_P(KalimaLiveObjectsTest, TheFirstPopulatedFramesFinishWithNativeWaterfallParticles)
{
    Teleport(static_cast<std::uint8_t>(GetParam()), 64, 64);
    ASSERT_FALSE(m_pollFailure.has_value()) << *m_pollFailure;
    OpenScene();
    constexpr int FramesUntilTheMist = 3;
    std::ptrdiff_t alive = 0;
    for (int frame = 0; frame < FramesUntilTheMist; ++frame)
    {
        EXPECT_EQ(alive, 0) << frame;
        Step();
        ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();
        Draw();
        ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();
        alive = std::count_if(m_world->Particles().All().begin(), m_world->Particles().All().end(),
                              [](const auto& p) { return p.alive && p.type == 0x6f1; });
    }
    EXPECT_GT(alive, 0);
    EXPECT_EQ(m_world->Map().objects.size(), 2266u);
}

TEST_P(KalimaLiveObjectsTest, PopulatedCycleFinishesBodyWaterAndParticlePasses)
{
    Teleport(static_cast<std::uint8_t>(GetParam()), 64, 64);
    ASSERT_FALSE(m_pollFailure.has_value()) << *m_pollFailure;
    OpenScene();
    Step();
    ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();
    DrawCompleteFrame();
    ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();
    EXPECT_EQ(m_world->Map().objects.size(), 2266u);
    bool water = false;
    bool particle = false;
    for (const auto& event : m_target.frames.back().events)
        for (const auto& command : event.modelCommands)
        {
            if (!command.vertexCount || !command.state.texture)
                continue;
            water |= command.state.texture->value == 0x23;
            particle |= command.state.texture->value >= 0x6ed && command.state.texture->value <= 0x6f4;
        }
    EXPECT_TRUE(water);
    EXPECT_TRUE(particle);
}

TEST_P(KalimaLiveObjectsTest, SeveralTicksAndIntermediateFramesPreserveDurablePoolsThenReenter)
{
    Teleport(static_cast<std::uint8_t>(GetParam()), 64, 64);
    OpenScene();
    for (int tick = 0; tick < 8; ++tick)
    {
        Step();
        ASSERT_FALSE(m_scene->Failure()) << *m_scene->Failure();
        DrawCompleteFrame();
        ASSERT_FALSE(m_scene->Failure()) << *m_scene->Failure();
    }
    const auto random = m_cloth->random.State();
    const auto particles = m_world->Particles();
    const auto objects = m_world->Map().objects;
    const auto waves = m_world->Map().kalima->CloneForFrame();
    const auto sounds = m_world->Fauna().Sounds().size();
    const auto effects = std::make_unique<Client::World::ModelEffects>(m_world->Map().effects);
    auto expectedPrepare = m_cloth->random;
    for (int close = 0; close < 4; ++close)
        static_cast<void>(expectedPrepare.Next());
    m_scene->PrepareFrame(); // 6371b7 común: corre antes de Render, también con frame0.
    EXPECT_EQ(m_cloth->random.State(), expectedPrepare.State());
    const auto preparedRandom = m_cloth->random.State();
    auto& projectedCanvas = m_target.BeginFrame(m_clock.Now());
    m_scene->Render(projectedCanvas, 0);
    m_target.Present();
    ASSERT_FALSE(m_scene->Failure()) << *m_scene->Failure();
    EXPECT_NE(preparedRandom, random);
    EXPECT_EQ(m_cloth->random.State(), preparedRandom);
    EXPECT_EQ(m_world->Fauna().Sounds().size(), sounds);
    EXPECT_EQ(m_world->Map().kalima->Waves().planes, waves->Waves().planes);
    for (std::size_t i = 0; i < particles.All().size(); ++i)
    {
        EXPECT_EQ(m_world->Particles().All()[i].alive, particles.All()[i].alive);
        EXPECT_EQ(m_world->Particles().All()[i].position, particles.All()[i].position);
        EXPECT_EQ(m_world->Particles().All()[i].life, particles.All()[i].life);
        EXPECT_EQ(m_world->Particles().All()[i].typedOwner, particles.All()[i].typedOwner);
    }
    for (std::size_t i = 0; i < objects.size(); ++i)
    {
        EXPECT_EQ(m_world->Map().objects[i].transform.position, objects[i].transform.position);
        EXPECT_EQ(m_world->Map().objects[i].velocity, objects[i].velocity);
    }
    for (std::size_t i = 0; i < effects->Effects().size(); ++i)
    {
        EXPECT_EQ(m_world->Map().effects.Effects()[i].life, effects->Effects()[i].life);
        EXPECT_EQ(m_world->Map().effects.Effects()[i].transform.position, effects->Effects()[i].transform.position);
    }
    Teleport(0, 135, 125);
    ASSERT_FALSE(m_pollFailure) << *m_pollFailure;
    Step();
    DrawCompleteFrame();
    ASSERT_FALSE(m_scene->Failure()) << *m_scene->Failure();
    Teleport(24, 64, 64);
    ASSERT_FALSE(m_pollFailure) << *m_pollFailure;
    for (int tick = 0; tick < 3; ++tick)
    {
        Step();
        DrawCompleteFrame();
        ASSERT_FALSE(m_scene->Failure()) << *m_scene->Failure();
    }
    EXPECT_EQ(m_world->Map().objects.size(), 2266u);
    EXPECT_TRUE(m_world->Map().kalima);
    EXPECT_TRUE(m_world->Particles().Any());
}

TEST_P(KalimaLiveObjectsTest, EveryReachableFamilyUsesRealImagesAndTheDepthBackend)
{
    Client::World::Particles particles;
    Client::Render::Models::CrtRandom random(7);
    constexpr std::array<std::array<int, 2>, 9> families{
        {{0x5ac, 6}, {0x5ac, 7}, {0x6ed, 0}, {0x6f0, 0}, {0x6f1, 0}, {0x6f2, 0}, {0x6f3, 0}, {0x6f4, 0}, {0x6f4, 1}}};
    for (const auto& family : families)
    {
        ASSERT_TRUE(particles.CreateKalimaChecked(family[0], {100, 200, 300}, {}, {1, 1, 1}, family[1], 1, 0, random, 5)
                        .HasValue());
        const auto metadata = m_models->TextureMetadataOf(static_cast<std::uint32_t>(family[0]));
        ASSERT_TRUE(metadata);
        EXPECT_EQ(metadata->size, glm::vec2(64));
        EXPECT_EQ(metadata->components, 3);
    }
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    auto state = batch.State();
    state.depthTest = false;
    batch.ApplyState(state);
    const Client::Render::Models::OriginalMatrix view{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    const auto before = random.State();
    const auto drawn = Client::Scenes::DrawGameParticles(batch, particles, *m_models, view, 0);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(batch.Vertices().size(), 54u);
    EXPECT_EQ(random.State(), before);
    for (const auto& command : batch.Commands())
        if (command.vertexCount)
        {
            EXPECT_TRUE(command.state.depthTest);
            EXPECT_EQ(command.state.blend, Client::Render::ModelBlend::Additive);
            ASSERT_TRUE(command.state.texture);
            EXPECT_TRUE(m_textures.Find(*command.state.texture));
        }
}

TEST_P(KalimaLiveObjectsTest, NativeStepSpawnsTheAmbientModelAndDrawsItsWholeLifetime)
{
    Teleport(static_cast<std::uint8_t>(GetParam()), 64, 64);
    OpenScene();
    Step();
    DrawCompleteFrame();
    ASSERT_FALSE(m_scene->Failure()) << *m_scene->Failure();
    // Semilla del contrato4385a0: selección,spawn,XY,tres del5ac/7,guard15 y escala9. La UI del paso saca antes.
    m_world->OwnCharacter().level = 40;
    m_world->Runtime().ownRecordDepth = 1;
    m_cloth->logicSteps = 1;
    m_cloth->random = Client::Render::Models::CrtRandom(SeedBefore(335, NestedGameUiStepDraws));
    Step();
    ASSERT_FALSE(m_scene->Failure()) << *m_scene->Failure();
    auto effects = m_world->Map().effects.Effects();
    const auto found =
        std::find_if(effects.begin(), effects.end(), [](const auto& e) { return e.alive && e.type == 9; });
    ASSERT_NE(found, effects.end());
    const auto index = static_cast<std::size_t>(found - effects.begin());
    EXPECT_EQ(found->life, 29);
    EXPECT_EQ(found->speed, 13);
    EXPECT_TRUE(std::any_of(m_world->Particles().All().begin(), m_world->Particles().All().end(),
                            [](const auto& p)
                            {
                                return p.alive && p.type == 0x5ac && p.subtype == 7 &&
                                       p.typedOwner.kind == Client::World::ParticleOwnerKind::Actor;
                            }));
    EXPECT_TRUE(std::any_of(m_world->Fauna().Sounds().begin(), m_world->Fauna().Sounds().end(),
                            [](const auto& sound) { return sound.id == 0x1d7; }));
    bool seen = false;
    for (int tick = 0; tick < 29; ++tick)
    {
        DrawCompleteFrame();
        ASSERT_FALSE(m_scene->Failure()) << *m_scene->Failure();
        seen |= m_world->Map().effects.Effects()[index].seen;
        Step();
        ASSERT_FALSE(m_scene->Failure()) << *m_scene->Failure();
    }
    EXPECT_TRUE(seen);
    EXPECT_FALSE(m_world->Map().effects.Effects()[index].alive);
    EXPECT_EQ(m_world->Map().effects.Effects()[index].life, 0);
    EXPECT_EQ(m_world->Map().objects.size(), 2266u);
}

TEST_P(KalimaLiveObjectsTest, NativeModelNineDrawsItsRealBodyAndCausticAndKeepsBodyOnMissingResource)
{
    const auto map = static_cast<std::uint8_t>(GetParam());
    Teleport(map, 64, 64);
    Client::Tests::FakeTextureStore store;
    Client::Scenes::ModelTextureTable table;
    Client::Scenes::TerrainTiles tiles(store);
    tiles.BindTextureTable(table);
    std::vector<std::string> skipped;
    ASSERT_TRUE(tiles.Load(m_dataRoot, map, skipped).HasValue());
    Client::Scenes::WorldModels models;
    ASSERT_TRUE(
        models.Load(m_dataRoot, map, store, m_log, &m_models->TextureSlots(), m_models.get(), &table).HasValue());
    auto effects = std::make_unique<Client::World::ModelEffects>();
    Client::Render::Models::CrtRandom random(7);
    ASSERT_EQ(effects->CreateKalimaEffect({6400, 6400, 1000}, {}, {0.3f, 0.8f, 1}, random), 0u);
    Client::Scenes::FaunaDrawCarry pose;
    const Client::Scenes::GameObjectPass pass{
        .map = map, .originalFrame = false, .retainedPose = &pose, .waterTextureFrame = 0u};
    Client::Scenes::ModelEffectDrawResources resources{*m_world->Map().terrain.heights};
    resources.kalimaPass = &pass;
    resources.onPose = [&](const Client::Scenes::DrawnObject& drawn)
    { return Client::Scenes::PublishFaunaPose(pose, models, drawn, true); };
    resources.posedMeshes = [&]() { return std::span<const Client::Render::Models::SkinnedMesh>(pose.meshes); };
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    const auto drawn = Client::Scenes::DrawGameModelEffects(batch, models, effects->Effects(),
                                                            Client::World::SphereFrustum{}, nullptr, {}, 0, resources);
    ASSERT_TRUE(drawn.HasValue()) << drawn.Error();
    EXPECT_EQ(batch.Vertices().size(), 204u); // 34 triángulos del cuerpo y34 de cáusticas.
    ASSERT_EQ(batch.Commands().size(), 2u);
    ASSERT_TRUE(batch.Commands()[0].state.texture);
    EXPECT_TRUE(store.Find(*batch.Commands()[0].state.texture));
    EXPECT_EQ(batch.Commands()[1].state.texture, Client::Render::TextureSlot{65});
    EXPECT_EQ(batch.Commands()[1].state.blend, Client::Render::ModelBlend::Alpha);
    table.Release(Client::Render::TextureSlot{65});
    batch.Begin(Client::Render::Pass3D{});
    const auto missing = Client::Scenes::DrawGameModelEffects(
        batch, models, effects->Effects(), Client::World::SphereFrustum{}, nullptr, {}, 0, resources);
    ASSERT_FALSE(missing.HasValue());
    EXPECT_NE(missing.Error().find("0x4395ba"), std::string::npos);
    EXPECT_EQ(batch.Vertices().size(), 102u);
    EXPECT_TRUE(effects->Effects()[0].seen);
    EXPECT_EQ(random.State(), 0x003d7aaeu);
    models.Release();
    tiles.Release();
}

INSTANTIATE_TEST_SUITE_P(AllSevenAliases, KalimaLiveObjectsTest, testing::Values(24, 25, 26, 27, 28, 29, 36));
} // namespace
