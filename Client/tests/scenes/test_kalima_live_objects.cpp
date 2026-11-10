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

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace
{
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

// Data real conservada: este test acredita el prefijo vivo, no una escena completa sin objetos.
TEST_P(KalimaLiveObjectsTest, RealObjectsReachTheMovementBoundaryAfterTheirBodyAndWaterOverlays)
{
    OpenScene();
    Step();
    ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();
    Teleport(static_cast<std::uint8_t>(GetParam()), 64, 64);
    ASSERT_FALSE(m_pollFailure.has_value()) << *m_pollFailure;
    ASSERT_EQ(m_world->Map().objects.size(), 2266u);
    // Primer dibujo después del1C, antes del pasoKalima: no nace fauna y quedan2266 objetos.
    Draw();
    ASSERT_TRUE(m_scene->Failure().has_value());
    EXPECT_NE(m_scene->Failure()->find("0x438ad0"), std::string::npos) << *m_scene->Failure();
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

TEST_P(KalimaLiveObjectsTest, TheFirstPopulatedStepAndDrawPreserveThePreciseObjectBoundary)
{
    Teleport(static_cast<std::uint8_t>(GetParam()), 64, 64);
    ASSERT_FALSE(m_pollFailure.has_value()) << *m_pollFailure;
    OpenScene();
    Step();
    ASSERT_FALSE(m_scene->Failure().has_value()) << *m_scene->Failure();
    Draw();
    ASSERT_TRUE(m_scene->Failure().has_value());
    EXPECT_NE(m_scene->Failure()->find("0x438f81"), std::string::npos) << *m_scene->Failure();
    EXPECT_EQ(m_world->Map().objects.size(), 2266u);
}

INSTANTIATE_TEST_SUITE_P(AllSevenAliases, KalimaLiveObjectsTest, testing::Values(24, 25, 26, 27, 28, 29, 36));
} // namespace
